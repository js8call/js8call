#ifndef TCI_AUDIO_HPP__
#define TCI_AUDIO_HPP__

#include <QByteArray>
#include <QMutex>
#include <QObject>
#include <atomic>

//
// Process-wide bridge between the TCI transceiver thread and the
// JS8Call audio thread.
//
// RX: transceiver thread emits 48 kHz mono int16. The audio thread
//     writes that into the detector.
// TX: audio thread pushes 48 kHz mono int16 from the modulator.
//     The transceiver thread pulls it when the TCI server asks
//     (TX_CHRONO) or on a free-running timer.
//
class TCIAudio final
  : public QObject
{
  Q_OBJECT

public:
  static TCIAudio * instance ();

  void setEnabled (bool on);
  bool enabled () const;

  // Audio thread -> rig. 48 kHz mono.
  void pushTx (qint16 const * samples, int frames);
  void clearTx ();

  // Rig thread. Zeros the destination, then copies what is buffered.
  // Returns the number of real samples copied (the rest are silence).
  int pullTx (qint16 * samples, int frames);

  // Rig thread. 48 kHz mono int16, little-endian host order.
  void submitRx (qint16 const * samples, int frames);

Q_SIGNALS:
  void rxPcm (QByteArray pcm);

private:
  TCIAudio ();

  static constexpr int cap_ = 48000; // 1 second at 48 kHz
  qint16 buf_[cap_] {};
  int read_ {0};
  int fill_ {0};
  mutable QMutex txMutex_;
  std::atomic<bool> enabled_ {false};
};

#endif
