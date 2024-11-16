#include "Detector.hpp"
#include <QDateTime>
#include <QtAlgorithms>
#include <QDebug>
#include <algorithm>
#include "commons.h"

#include "DriftingDateTime.h"

#include "moc_Detector.cpp"

extern "C" {
  void   fil4_(qint16*, qint32*, qint16*, qint32*);
}

Detector::Detector (unsigned frameRate, unsigned periodLengthInSeconds,
                    unsigned downSampleFactor, QObject * parent)
  : AudioDevice (parent)
  , m_frameRate (frameRate)
  , m_period (periodLengthInSeconds)
  , m_downSampleFactor (downSampleFactor)
  , m_samplesPerFFT {max_buffer_size}
  , m_ns (999)
  , m_buffer ((downSampleFactor > 1) ?
              new short [max_buffer_size * downSampleFactor] : nullptr)
  , m_bufferPos (0)
{
  (void)m_frameRate;            // quell compiler warning
  clear ();
}

void Detector::setBlockSize (unsigned n)
{
  m_samplesPerFFT = n;
}

bool Detector::reset ()
{
  clear ();
  // don't call base call reset because it calls seek(0) which causes
  // a warning
  return isOpen ();
}

void Detector::clear ()
{
#if JS8_RING_BUFFER
  resetBufferPosition();
  resetBufferContent();
#else
  dec_data.params.kin = 0;
  m_bufferPos = 0;
#endif

  // fill buffer with zeros (G4WJS commented out because it might cause decoder hangs)
  // qFill (dec_data.d2, dec_data.d2 + sizeof (dec_data.d2) / sizeof (dec_data.d2[0]), 0);
}

void Detector::resetBufferPosition()
{
    QMutexLocker mutex(&m_lock);

    // set index to roughly where we are in time (1ms resolution)
    qint64   const now        = DriftingDateTime::currentMSecsSinceEpoch ();
    unsigned const msInPeriod = (now % 86400000LL) % (m_period * 1000);
    int      const prevKin    = dec_data.params.kin;

    dec_data.params.kin = qMin ((msInPeriod * m_frameRate) / 1000, static_cast<unsigned> (sizeof (dec_data.d2) / sizeof (dec_data.d2[0])));
    m_bufferPos         = 0;
    m_ns                = secondInPeriod();

    int const delta = dec_data.params.kin - prevKin;

    qDebug() << "advancing detector buffer from" << prevKin << "to" << dec_data.params.kin << "delta" << delta;

    // rotate buffer moving the contents that were at prevKin to the new kin position
    if (delta < 0)
    {
      std::rotate(std::begin(dec_data.d2),
                  std::begin(dec_data.d2) - delta,
                  std::end  (dec_data.d2));
    }
    else
    {
      std::rotate(std::rbegin(dec_data.d2),
                  std::rbegin(dec_data.d2) + delta,
                  std::rend  (dec_data.d2));
    }
}

void Detector::resetBufferContent()
{
    QMutexLocker mutex(&m_lock);

    std::fill(std::begin(dec_data.d2), std::end(dec_data.d2), 0);
    qDebug() << "clearing detector buffer content";
}

qint64 Detector::writeData (char const * data, qint64 maxSize)
{
  QMutexLocker mutex(&m_lock);

  int ns=secondInPeriod();
  if(ns < m_ns) {                      // When ns has wrapped around to zero, restart the buffers
    dec_data.params.kin = 0;
    m_bufferPos = 0;
  }
  m_ns=ns;

  // no torn frames
  Q_ASSERT (!(maxSize % static_cast<qint64> (bytesPerFrame ())));
  // these are in terms of input frames (not down sampled)
  size_t framesAcceptable ((sizeof (dec_data.d2) /
                            sizeof (dec_data.d2[0]) - dec_data.params.kin) * m_downSampleFactor);
  size_t framesAccepted (qMin (static_cast<size_t> (maxSize /
                                                    bytesPerFrame ()), framesAcceptable));

  if (framesAccepted < static_cast<size_t> (maxSize / bytesPerFrame ())) {
    qDebug () << "dropped " << maxSize / bytesPerFrame () - framesAccepted
                << " frames of data on the floor!"
                << dec_data.params.kin << ns;
    }

    for (unsigned remaining = framesAccepted; remaining; ) {
      size_t numFramesProcessed (qMin (m_samplesPerFFT *
                                       m_downSampleFactor - m_bufferPos, remaining));

      if(m_downSampleFactor > 1) {
        store (&data[(framesAccepted - remaining) * bytesPerFrame ()],
               numFramesProcessed, &m_buffer[m_bufferPos]);
        m_bufferPos += numFramesProcessed;

        if(m_bufferPos==m_samplesPerFFT*m_downSampleFactor) {
          qint32 framesToProcess (m_samplesPerFFT * m_downSampleFactor);
          qint32 framesAfterDownSample (m_samplesPerFFT);
          if(m_downSampleFactor > 1 && dec_data.params.kin>=0 &&
             dec_data.params.kin < (NTMAX*12000 - framesAfterDownSample)) {
            fil4_(&m_buffer[0], &framesToProcess, &dec_data.d2[dec_data.params.kin],
                  &framesAfterDownSample);
            dec_data.params.kin += framesAfterDownSample;
          } else {
            // qDebug() << "framesToProcess     = " << framesToProcess;
            // qDebug() << "dec_data.params.kin = " << dec_data.params.kin;
            // qDebug() << "secondInPeriod      = " << secondInPeriod();
            // qDebug() << "framesAfterDownSample" << framesAfterDownSample;
          }
          Q_EMIT framesWritten (dec_data.params.kin);
          m_bufferPos = 0;
        }

      } else {
        store (&data[(framesAccepted - remaining) * bytesPerFrame ()],
               numFramesProcessed, &dec_data.d2[dec_data.params.kin]);
        m_bufferPos += numFramesProcessed;
        dec_data.params.kin += numFramesProcessed;
        if (m_bufferPos == static_cast<unsigned> (m_samplesPerFFT)) {
          Q_EMIT framesWritten (dec_data.params.kin);
          m_bufferPos = 0;
        }
      }
      remaining -= numFramesProcessed;
    }

  return maxSize;    // we drop any data past the end of the buffer on
  // the floor until the next period starts
}

unsigned Detector::secondInPeriod () const
{
  // we take the time of the data as the following assuming no latency
  // delivering it to us (not true but close enough for us)
  qint64 now (DriftingDateTime::currentMSecsSinceEpoch ());
  unsigned secondInToday ((now % 86400000LL) / 1000);
  return secondInToday % m_period;
}
