#ifndef MODULATOR_HPP__
#define MODULATOR_HPP__

#include <QAudio>
#include <QPointer>

#include "AudioDevice.hpp"

class SoundOutput;

//
// Input device that generates PCM audio frames that encode a message
// and an optional CW ID.
//
// Output can be muted while underway, preserving waveform timing when
// transmission is resumed.
//
class Modulator
  : public AudioDevice
{
  Q_OBJECT;

public:
  enum ModulatorState {Synchronizing, Active, Idle};

  Modulator (unsigned frameRate, unsigned periodLengthInSeconds, QObject * parent = nullptr);

  void close () override;

  bool isTuning () const {return m_tuning;}
  double frequency () const {return m_frequency;}
  bool isActive () const {return m_state != Idle;}
  void setSpread(double s) {m_fSpread=s;}
  void setTRPeriod(unsigned p) {m_period=p;}
  void set_nsym(int n) {m_symbolsLength=n;}

  Q_SLOT void start (unsigned symbolsLength, double framesPerSymbol, double frequency,
                     double toneSpacing, SoundOutput *, Channel = Mono,
                     bool synchronize = true, bool fastMode = false,
                     double dBSNR = 99., int TRperiod=60);
  Q_SLOT void stop (bool quick = false);
  Q_SLOT void tune (bool newState = true);
  Q_SLOT void setFrequency (double newFrequency) {m_frequency = newFrequency;}
  Q_SIGNAL void stateChanged (ModulatorState) const;

protected:
  qint64 readData (char * data, qint64 maxSize) override;
  qint64 writeData (char const * /* data */, qint64 /* maxSize */) override
  {
    return -1;			// we don't consume data
  }
#if defined(Q_OS_WIN)
// On Windows, bytesAvailable() must return a size that exceeds some threshold 
// in order for the AudioSink to go into Active state and start pulling data.
// See: https://bugreports.qt.io/browse/QTBUG-108672
  qint64 bytesAvailable () const
  {
    return 8000;
  }
#endif

private:
  qint16 postProcessSample (qint16 sample) const;

  QPointer<SoundOutput> m_stream;
  bool m_quickClose;

  unsigned m_symbolsLength;

  static double constexpr m_twoPi = 2.0 * 3.141592653589793238462;
  unsigned m_nspd = 2048 + 512; // CW ID WPM factor = 22.5 WPM

  double m_phi;
  double m_dphi;
  double m_amp;
  double m_nsps;
  double m_frequency;
  double m_frequency0;
  double m_snr;
  double m_fac;
  double m_toneSpacing;
  double m_fSpread;

  qint64 m_silentFrames;
  qint32 m_TRperiod;
  qint16 m_ramp;

  unsigned m_frameRate;
  unsigned m_period;
  ModulatorState m_state;

  bool m_tuning;
  bool m_addNoise;
  bool m_bFastMode;

  bool m_cwLevel;
  unsigned m_ic;
  unsigned m_isym0;
  int m_j0;
  double m_toneFrequency0;
};

#endif
