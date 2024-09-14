#include "soundin.h"

#include <QAudioFormat>
#include <QSysInfo>
#include <QDebug>

#include "DriftingDateTime.h"

#include "moc_soundin.cpp"

bool SoundInput::audioError () const
{
  bool result (true);

  Q_ASSERT_X (m_stream, "SoundInput", "programming error");
  if (m_stream)
    {
      switch (m_stream->error ())
        {
        case QAudio::OpenError:
          Q_EMIT error (tr ("An error opening the audio input device has occurred."));
          break;

        case QAudio::IOError:
          Q_EMIT error (tr ("An error occurred during read from the audio input device."));
          break;

        case QAudio::UnderrunError:
          Q_EMIT error (tr ("Audio data not being fed to the audio input device fast enough."));
          break;

        case QAudio::FatalError:
          Q_EMIT error (tr ("Non-recoverable error, audio input device not usable at this time."));
          break;

        case QAudio::NoError:
          result = false;
          break;
        }
    }
  return result;
}

void SoundInput::start(QAudioDevice const& device, int framesPerBuffer, AudioDevice * sink, unsigned downSampleFactor, AudioDevice::Channel channel)
{
  Q_ASSERT (sink);

  stop ();

  m_sink = sink;

  QAudioFormat format (device.preferredFormat());
//  qDebug () << "Preferred audio input format:" << format;
  format.setSampleFormat (QAudioFormat::Int16);
  format.setChannelCount (AudioDevice::Mono == channel ? 1 : 2);
  format.setSampleRate (12000 * downSampleFactor);
  if (!format.isValid ())
    {
      Q_EMIT error (tr ("Requested input audio format is not valid."));
      return;
    }

  if (!device.isFormatSupported (format))
    {
//      qDebug () << "Nearest supported audio format:" << device.nearestFormat (format);
      Q_EMIT error (tr ("Requested input audio format is not supported on device."));
      return;
    }
//  qDebug () << "Selected audio input format:" << format;

  m_stream.reset (new QAudioSource {device, format});
  if (audioError ())
    {
      return;
    }

  connect (m_stream.data(), &QAudioSource::stateChanged, this, &SoundInput::handleStateChanged);

  m_stream->setBufferSize (m_stream->format ().bytesForFrames (framesPerBuffer));
  if (sink->initialize (QIODevice::WriteOnly, channel))
    {
      m_stream->start (sink);
      audioError ();
    }
  else
    {
      Q_EMIT error (tr ("Failed to initialize audio sink device"));
    }
}

void SoundInput::suspend ()
{
  if (m_stream)
    {
      m_stream->suspend ();
      audioError ();
    }
}

void SoundInput::resume ()
{
//  qDebug() << "Resume" << fmod(0.001*DriftingDateTime::currentMSecsSinceEpoch(),6.0);
  if (m_sink)
    {
      m_sink->reset ();
    }

  if (m_stream)
    {
      m_stream->resume ();
      audioError ();
    }
}

void SoundInput::handleStateChanged (QAudio::State newState) const
{
  // qDebug () << "SoundInput::handleStateChanged: newState:" << newState;

  switch (newState)
    {
    case QAudio::IdleState:
      Q_EMIT status (tr ("Idle"));
      break;

    case QAudio::ActiveState:
      Q_EMIT status (tr ("Receiving"));
      break;

    case QAudio::SuspendedState:
      Q_EMIT status (tr ("Suspended"));
      break;

    case QAudio::StoppedState:
      if (audioError ())
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

void SoundInput::stop()
{
  if (m_stream)
    {
      m_stream->stop ();
    }
  m_stream.reset ();

  if (m_sink)
    {
      m_sink->close ();
    }
}

SoundInput::~SoundInput ()
{
  stop ();
}
