#ifndef TCI_TRANSCEIVER_HPP__
#define TCI_TRANSCEIVER_HPP__

#include <QString>
#include <QVector>

#include "TransceiverFactory.hpp"
#include "PollingTransceiver.hpp"

class QWebSocket;
class QTimer;

//
// Native TCI (Transceiver Control Interface) client.
//
// One WebSocket carries CAT (frequency, mode, split, PTT) and the
// bidirectional RX/TX audio used by the JS8 modem. Selecting
// "TCI Client RX1" or "TCI Client RX2" replaces both the CAT port
// and the sound card for receive and transmit.
//
// Wire format follows TCI 1.x/2.0 as implemented by ExpertSDR3,
// Thetis and AetherSDR, and by the WSJT-X TCI transceiver:
//   text commands terminated by ';'
//   binary frames with a 64-byte little-endian header
//   length = number of scalar samples in the payload
//   RX/TX audio default float32 stereo, 48 kHz
//   TX audio is paced by TX_CHRONO (type 3); a short free-run
//   fallback covers servers that do not send it
//
class TCITransceiver final
  : public PollingTransceiver
{
  Q_OBJECT

public:
  static void register_transceivers (TransceiverFactory::Transceivers * registry, int id1, int id2);

  static char const * name_rx1 () {return "TCI Client RX1";}
  static char const * name_rx2 () {return "TCI Client RX2";}
  static bool is_tci_rig (QString const& name)
  {
    return name == QLatin1String (name_rx1 ()) || name == QLatin1String (name_rx2 ());
  }

  // rx_index is "0" or "1". address is host:port or a ws:// / wss:// URL.
  // Empty address becomes localhost:50001 (ExpertSDR3 default).
  explicit TCITransceiver (QString const& rx_index, QString const& address,
                           int poll_interval, QObject * parent = nullptr);
  ~TCITransceiver () override;

protected:
  int do_start () override;
  void do_stop () override;
  void do_frequency (Frequency, MODE, bool no_ignore) override;
  void do_tx_frequency (Frequency, MODE, bool no_ignore) override;
  void do_mode (MODE) override;
  void do_ptt (bool on) override;
  void poll () override;

private:
  struct Hdr
  {
    quint32 receiver {0};
    quint32 sampleRate {0};
    quint32 format {3};
    quint32 length {0};
    quint32 type {0};
    quint32 channelsHint {0};
  };

  void open_socket ();
  void send_text (QString const& command);
  void negotiate_audio ();
  void handle_command (QString const& command);
  void handle_binary (QByteArray const& data);
  void note_ready ();
  void send_tx_frame (qint16 const * mono48, int frames48, quint32 format, quint32 rate, int channels, quint32 length_scalars);
  void on_tx_chrono (Hdr const& hdr);
  void free_run_tx ();
  static bool parse_header (QByteArray const& data, Hdr& hdr);
  static int sample_bytes (quint32 format);
  static MODE mode_from_string (QString const& mode);
  static QString mode_to_string (MODE mode);
  static bool parse_bool (QString const& text);
  static Frequency parse_frequency (QString const& text);
  void note_update ();

  Q_SLOT void on_connected ();
  Q_SLOT void on_disconnected ();
  Q_SLOT void on_text (QString const& message);
  Q_SLOT void on_binary (QByteArray const& data);
  Q_SLOT void on_error ();

  QString rx_;
  QString server_;
  QWebSocket * socket_ {nullptr};
  QTimer * free_run_ {nullptr};
  QString text_acc_;
  bool connected_ {false};
  bool ready_ {false};
  bool audio_on_ {false};
  bool chrono_paced_ {false};
  bool ptt_ {false};
  bool split_ {false};
  Frequency rx_freq_ {0};
  Frequency tx_freq_ {0};
  QString mode_text_;
  quint32 sample_rate_ {48000};
  quint32 sample_format_ {3}; // float32
  int channels_ {2};
  double rx_phase_ {0};
  float rx_prev_ {0};
  bool rx_have_prev_ {false};
  int rx_rate_ {0};
  bool stopping_ {false};
  // False until start() has marked the rig online. Publishing earlier
  // makes the GUI treat the rig as failed and destroy this object.
  bool started_ {false};
};

#endif
