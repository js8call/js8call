#ifndef MODULATOR_HPP__
#define MODULATOR_HPP__

#include <QAudio>
#include <QPointer>
#include "AudioDevice.hpp"

class SoundOutput;

//
// Input device that generates PCM audio frames that encode a message.
//
// Output can be muted while underway, preserving waveform timing when
// transmission is resumed.
//

class Modulator final
  : public AudioDevice
{
  Q_OBJECT;

public:

  enum class State
  {
    Synchronizing,
    Active,
    Idle
  };

  // Constructor

  explicit Modulator(QObject * parent = nullptr)
  : AudioDevice {parent}
  {}

  // Inline accessors

  bool   isIdle()    const { return m_state == State::Idle; }
  bool   isTuning()  const { return m_tuning;               }
  double frequency() const { return m_frequency;            }

  // Manipulators

  void close() override;

  // Signals

  Q_SIGNAL void stateChanged(State) const;

  // Inline slots

  Q_SLOT void setFrequency(double const frequency)
  {
    m_frequency = frequency;
  }

  // Slots

  Q_SLOT void start(double        frequency,
                    int           submode,
                    SoundOutput * stream,
                    Channel       channel);
  Q_SLOT void stop(bool quick = false);
  Q_SLOT void tune(bool state = true);

protected:

  // QIODevice protocol

  qint64 readData (char       *, qint64) override;
  qint64 writeData(char const *, qint64) override
  {
    return -1;			// we don't consume data
  }

#if defined(Q_OS_WIN)
// On Windows, bytesAvailable() must return a size that exceeds some threshold 
// in order for the AudioSink to go into Active state and start pulling data.
// See: https://bugreports.qt.io/browse/QTBUG-108672
  qint64 bytesAvailable() const
  {
    return 8000;
  }
#endif

private:

  // Data members

  QPointer<SoundOutput> m_stream;
  State                 m_state      = State::Idle;
  bool                  m_quickClose = false;
  bool                  m_tuning     = false;
  double                m_frequency;
  double                m_frequency0;
  double                m_toneSpacing;
  double                m_phi;
  double                m_dphi;
  double                m_amp;
  double                m_nsps;
  qint64                m_silentFrames;
  unsigned              m_ic;
  unsigned              m_isym0;
};

#endif
