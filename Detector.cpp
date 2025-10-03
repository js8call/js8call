#include "Detector.hpp"
#include <algorithm>
#include <cmath>
#include <QDateTime>
#include <QLoggingCategory>
#include <QMutexLocker>
#include <QtAlgorithms>
#include "commons.h"
#include "DriftingDateTime.h"

/******************************************************************************/
// FIR Filter Coefficients
/******************************************************************************/

namespace
{
  // Filter coefficients for an FIR lowpass filter designed using ScopeFIR.
  //
  //   fsample     = 48000 Hz
  //   Ntaps       = 49
  //   fc          = 4500  Hz
  //   fstop       = 6000  Hz
  //   Ripple      = 1     dB
  //   Stop Atten  = 40    dB
  //   fout        = 12000 Hz

  constexpr std::array LOWPASS
  {
     0.000861074040f,  0.010051920210f,  0.010161983649f,  0.011363155076f,
     0.008706594219f,  0.002613872664f, -0.005202883094f, -0.011720748164f,
    -0.013752163325f, -0.009431602741f,  0.000539063909f,  0.012636767098f,
     0.021494659597f,  0.021951235065f,  0.011564169382f, -0.007656470131f,
    -0.028965787341f, -0.042637874109f, -0.039203309748f, -0.013153301537f,
     0.034320769178f,  0.094717832646f,  0.154224604789f,  0.197758325022f,
     0.213715139513f,  0.197758325022f,  0.154224604789f,  0.094717832646f,
     0.034320769178f, -0.013153301537f, -0.039203309748f, -0.042637874109f,
    -0.028965787341f, -0.007656470131f,  0.011564169382f,  0.021951235065f,
     0.021494659597f,  0.012636767098f,  0.000539063909f, -0.009431602741f,
    -0.013752163325f, -0.011720748164f, -0.005202883094f,  0.002613872664f,
     0.008706594219f,  0.011363155076f,  0.010161983649f,  0.010051920210f,
     0.000861074040f
  };
}

/******************************************************************************/
// Implementation
/******************************************************************************/

#include "moc_Detector.cpp"

Q_DECLARE_LOGGING_CATEGORY(detector_js8)

Detector::Detector(unsigned  frameRate,
                   unsigned  periodLengthInSeconds,
                   QObject * parent)
  : AudioDevice (parent)
  , m_frameRate (frameRate)
  , m_period    (periodLengthInSeconds)
  , m_filter    (LOWPASS)
{
  clear();
}

void
Detector::setBlockSize (unsigned n)
{
  m_samplesPerFFT = n;
}

bool
Detector::reset()
{
  clear ();
  // don't call base call reset because it calls seek(0) which causes
  // a warning
  return isOpen ();
}

void
Detector::clear()
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

void
Detector::resetBufferPosition()
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

  qCDebug(detector_js8) << "advancing detector buffer from" << prevKin << "to" << dec_data.params.kin << "delta" << delta;

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

void
Detector::resetBufferContent()
{
  QMutexLocker mutex(&m_lock);

  std::fill(std::begin(dec_data.d2), std::end(dec_data.d2), 0);
  qCDebug(detector_js8) << "clearing detector buffer content";
}

qint64
Detector::writeData(char const * const data,
                    qint64       const maxSize)
{
  QMutexLocker mutex(&m_lock);

  // When ns has wrapped around to zero, restart the buffers.

  int const ns = secondInPeriod();
  if(ns < m_ns) {
    dec_data.params.kin = 0;
    m_bufferPos         = 0;
  }
  m_ns = ns;

  // No torn frames.
  
  Q_ASSERT (!(maxSize % static_cast<qint64>(bytesPerFrame())));

  // These are in terms of input frames (not down sampled).

  size_t const framesAcceptable = (sizeof(dec_data.d2) / sizeof(dec_data.d2[0]) - dec_data.params.kin) * Filter::NDOWN;
  size_t const framesAccepted   = qMin(static_cast<size_t>(maxSize /bytesPerFrame()), framesAcceptable);

  if (framesAccepted < static_cast<size_t>(maxSize / bytesPerFrame()))
  {
    qCDebug(detector_js8) << "dropped " << maxSize / bytesPerFrame () - framesAccepted
              << " frames of data on the floor!"
              << dec_data.params.kin
              << ns;
  }

  for (auto remaining = framesAccepted;
                remaining;)
  {
    size_t const numFramesProcessed = qMin(m_samplesPerFFT * Filter::NDOWN - m_bufferPos, remaining);

    store (&data[(framesAccepted - remaining) * bytesPerFrame()],
           numFramesProcessed,
           &m_buffer[m_bufferPos]);

    m_bufferPos += numFramesProcessed;

    if (m_bufferPos == m_samplesPerFFT * Filter::NDOWN)
    {
      if (dec_data.params.kin >= 0 &&
          dec_data.params.kin < static_cast<int>(JS8_NTMAX * 12000 - m_samplesPerFFT))
      {
        for (std::size_t i = 0; i < m_samplesPerFFT; ++i)
        {
          dec_data.d2[dec_data.params.kin++] = m_filter.downSample(&m_buffer[i * Filter::NDOWN]);
        }
      }
      Q_EMIT framesWritten (dec_data.params.kin);
      m_bufferPos = 0;
    }
    remaining -= numFramesProcessed;
  }

  // We drop any data past the end of the buffer on the floor
  // until the next period starts

  return maxSize;
}

unsigned
Detector::secondInPeriod() const
{
  // we take the time of the data as the following assuming no latency
  // delivering it to us (not true but close enough for us)
  qint64 now (DriftingDateTime::currentMSecsSinceEpoch ());
  unsigned secondInToday ((now % 86400000LL) / 1000);
  return secondInToday % m_period;
}

/******************************************************************************/

Q_LOGGING_CATEGORY(detector_js8, "detector.js8", QtWarningMsg)
