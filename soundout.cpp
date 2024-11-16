#include "soundout.h"

#include <QDateTime>
#include <QAudioOutput>
#include <QSysInfo>
#include <qmath.h>
#include <QDebug>

#include "moc_soundout.cpp"

bool SoundOutput::checkStream () const
{
  bool result {false};

  Q_ASSERT_X (m_stream, "SoundOutput", "programming error");
  if (m_stream) {
    switch (m_stream->error ())
      {
      case QAudio::OpenError:
        Q_EMIT error (tr ("An error opening the audio output device has occurred."));
        break;

      case QAudio::IOError:
        Q_EMIT error (tr ("An error occurred during write to the audio output device."));
        break;

      case QAudio::UnderrunError:
        Q_EMIT error (tr ("Audio data not being fed to the audio output device fast enough."));
        break;

      case QAudio::FatalError:
        Q_EMIT error (tr ("Non-recoverable error, audio output device not usable at this time."));
        break;

      case QAudio::NoError:
        result = true;
        break;
      }
  }
  return result;
}

void SoundOutput::setFormat (QAudioDevice const &device, unsigned channels, unsigned msBuffered){

    Q_ASSERT (0 < channels && channels < 3);

    QAudioFormat format(device.preferredFormat());

    format.setChannelCount(channels);
    format.setSampleRate  (48000);
    format.setSampleFormat(QAudioFormat::Int16);

    setDeviceFormat(device, format, msBuffered);
}

void SoundOutput::setDeviceFormat (QAudioDevice const &device, QAudioFormat const &format, unsigned msBuffered)
{
  if (!format.isValid())
  {
    Q_EMIT error (tr ("Requested output audio format is not valid."));
  }
  if (!device.isFormatSupported(format))
  {
    Q_EMIT error (tr ("Requested output audio format is not supported on device."));
  }

  m_device     = device;
  m_format     = format;
  m_msBuffered = msBuffered;
}

void SoundOutput::restart (QIODevice * source)
{
  if (!m_device.isNull())
  {
    m_stream.reset(new QAudioSink(m_device, m_format));
    qDebug () << "SoundOutput::restart Selected audio output format:" << m_stream -> format();
	  checkStream ();
    m_stream->setVolume (m_volume);
    m_error = false;

    connect (m_stream.data(), &QAudioSink::stateChanged, this, &SoundOutput::handleStateChanged);
  }

  if (!m_stream)
  {
    if (!m_error)
    {
      m_error = true;        // only signal error once
      Q_EMIT error (tr ("No audio output device configured."));
    }
    return;
  }
  else
  {
    m_error = false;
  }

  // we have to set this before every start on the stream because the
  // Windows implementation seems to forget the buffer size after a
  // stop.
  if (m_msBuffered > 0)
  {
    m_stream->setBufferSize(m_stream->format().bytesForDuration(m_msBuffered));
  }

  m_stream->start (source);
}

void SoundOutput::suspend ()
{
  if (m_stream && QAudio::ActiveState == m_stream->state ())
    {
      m_stream->suspend ();
      checkStream ();
    }
}

void SoundOutput::resume ()
{
  if (m_stream && QAudio::SuspendedState == m_stream->state ())
    {
      m_stream->resume ();
      checkStream ();
    }
}

void SoundOutput::reset ()
{
  if (m_stream)
    {
      m_stream->reset ();
      checkStream ();
    }
}

void SoundOutput::stop ()
{
  if (m_stream)
  {
    m_stream->reset ();
    m_stream->stop ();
  }
  //m_stream.reset ();  // XXX in WSJTX, seems like a bug, will assert after stop checks
}

qreal SoundOutput::attenuation () const
{
  return -(20. * qLn (m_volume) / qLn (10.));
}

QAudioFormat SoundOutput::format() const{
    return m_format;
}

void SoundOutput::setAttenuation (qreal a)
{
  Q_ASSERT (0. <= a && a <= 999.);
  m_volume = qPow(10.0, -a/20.0);
  // qDebug () << "SoundOut: attn = " << a << ", vol = " << m_volume;
  if (m_stream)
    {
      m_stream->setVolume (m_volume);
    }
}

void SoundOutput::resetAttenuation ()
{
  m_volume = 1.;
  if (m_stream)
    {
      m_stream->setVolume (m_volume);
    }
}

void SoundOutput::handleStateChanged (QAudio::State newState) const
{
  switch (newState)
    {
    case QAudio::IdleState:
      Q_EMIT status (tr ("Idle"));
      break;

    case QAudio::ActiveState:
      Q_EMIT status (tr ("Sending"));
      break;

    case QAudio::SuspendedState:
      Q_EMIT status (tr ("Suspended"));
      break;

    case QAudio::StoppedState:
      if (!checkStream ())
        {
          Q_EMIT status (tr ("Error"));
        }
      else
        {
          Q_EMIT status (tr ("Stopped"));
        }
      break;
    }
}
