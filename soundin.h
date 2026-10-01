// -*- Mode: C++ -*-
#ifndef SOUNDIN_H__
#define SOUNDIN_H__

#include <QObject>
#include <QString>
#include <QDateTime>
#include <QScopedPointer>
#include <QPointer>
#include <QAudioDevice>
#include <QAudioSource>
#include "AudioDevice.hpp"

class QByteArray;

// Gets audio data from sound sample source and passes it to a sink device
class SoundInput
  : public QObject
{
  Q_OBJECT;

public:
  SoundInput (QObject * parent = nullptr)
    : QObject {parent}
    , m_sink {nullptr}
  {
  }

  ~SoundInput ();

  // sink must exist from the start call until the next start call or
  // stop call
  Q_SLOT void start(QAudioDevice const&, int framesPerBuffer, AudioDevice * sink, AudioDevice::Channel = AudioDevice::Mono);
  // 48 kHz mono from the TCI transceiver, instead of a sound card.
  Q_SLOT void startTci (AudioDevice * sink);
  Q_SLOT void suspend ();
  Q_SLOT void resume ();
  Q_SLOT void stop ();

  Q_SIGNAL void error (QString message) const;
  Q_SIGNAL void status (QString message) const;

private:
  Q_SLOT void handleStateChanged (QAudio::State) const;
  Q_SLOT void onRxPcm (QByteArray pcm);
  bool audioError () const;

  QScopedPointer<QAudioSource> m_stream;
  QPointer<AudioDevice> m_sink;
  bool m_tci {false};
  bool m_tciSuspended {false};
};

#endif
