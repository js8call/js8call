#ifndef CONFIGURATION_HPP_
#define CONFIGURATION_HPP_

#include <QObject>
#include <QFont>

#include "Radio.hpp"
#include "IARURegions.hpp"
#include "AudioDevice.hpp"
#include "Transceiver.hpp"

#include "pimpl_h.hpp"

class QSettings;
class QWidget;
class QAudioDevice;
class QString;
class QDir;
class Bands;
class FrequencyList_v2;
class StationList;
class QStringListModel;
class QHostAddress;

//
// Class Configuration
//
//  Encapsulates the control, access  and, persistence of user defined
//  settings for the GUI.  Setting values are accessed through a
//  QDialog window containing concept orientated tab windows.
//
// Responsibilities
//
//  Provides management of  the CAT and PTT  rig interfaces, providing
//  control access  via a minimal generic  set of Qt slots  and status
//  updates via Qt signals.  Internally  the rig control capability is
//  farmed out  to a  separate thread  since many  of the  rig control
//  functions are blocking.
//
//  All  user  settings  required  by  the  GUI  are  exposed  through
//  query methods.  Settings only become  visible once they  have been
//  accepted by the user which is  done by clicking the "OK" button on
//  the settings dialog.
//
//  The QSettings instance  passed to the constructor is  used to read
//  and write user settings.
//
//  Pointers to three QAbstractItemModel  objects are provided to give
//  access to amateur band  information, user working frequencies and,
//  user operating  band information.  These porovide  consistent data
//  models that can  be used in GUI lists or  tables or simply queried
//  for user defined bands, default operating frequencies and, station
//  descriptions.
//
class Configuration final
  : public QObject
{
  Q_OBJECT
  Q_ENUMS (DataMode)

public:
  using MODE = Transceiver::MODE;
  using TransceiverState = Transceiver::TransceiverState;
  using Frequency = Radio::Frequency;
  using port_type = quint16;

  enum DataMode {data_mode_none, data_mode_USB, data_mode_data};
  Q_ENUM (DataMode)

  explicit Configuration (QDir const& temp_directory, QSettings * settings,
                          QWidget * parent = nullptr);
  ~Configuration ();

  void select_tab (int);
  int exec ();
  bool is_active () const;

  QDir temp_dir () const;
  QDir doc_dir () const;
  QDir data_dir () const;
  QDir writeable_data_dir () const;

  QAudioDevice const& audio_input_device () const;
  AudioDevice::Channel audio_input_channel () const;
  QAudioDevice const& audio_output_device () const;
  AudioDevice::Channel audio_output_channel () const;
  QAudioDevice const& notification_audio_output_device () const;

  bool notifications_enabled() const;
  QString notification_path(const QString &key) const;
  Q_SIGNAL void test_notify(const QString &key);

  // These query methods should be used after a call to exec() to
  // determine if either the audio input or audio output stream
  // parameters have changed. The respective streams should be
  // re-opened if they return true.
  bool restart_audio_input () const;
  bool restart_audio_output () const;
  bool restart_notification_audio_output () const;

  bool use_dynamic_grid() const;
  QString my_callsign () const;
  QString my_grid () const;
  QSet<QString> my_groups() const;
  void addGroup(QString const &group);
  void removeGroup(QString const &group);
  QSet<QString> auto_whitelist() const;
  QSet<QString> auto_blacklist() const;
  QSet<QString> hb_blacklist() const;
  QSet<QString> spot_blacklist() const;
  QSet<QString> primary_highlight_words() const;
  QSet<QString> secondary_highlight_words() const;
  int activity_aging() const;
  int callsign_aging() const;
  QString eot() const;
  QString mfi() const;
  QString my_info () const;
  QString my_status () const;
  QString hb_message () const;
  QString cq_message () const;
  QString reply_message () const;
  QFont table_font() const;
  QFont text_font () const;
  QFont rx_text_font () const;
  QFont tx_text_font () const;
  QFont compose_text_font () const;
  double txDelay() const;
  bool write_logs() const;
  bool reset_activity() const;
  bool check_for_updates() const;
  bool tx_qsy_allowed () const;
  bool spot_to_reporting_networks () const;
  void set_spot_to_reporting_networks (bool);
  bool spot_to_aprs() const;
  bool transmit_directed() const;
  bool autoreply_on_at_startup () const;
  bool autoreply_confirmation () const;
  bool heartbeat_anywhere() const;
  bool heartbeat_qso_pause() const;
  bool heartbeat_ack_snr() const;
  bool relay_off() const;
  bool psk_reporter_tcpip () const;
  bool monitor_off_at_startup () const;
  bool transmit_off_at_startup () const;
  bool monitor_last_used () const;
  bool insert_blank () const;
  bool DXCC () const;
  bool ppfx() const;
  bool miles () const;
  bool hold_ptt() const;
  bool avoid_forced_identify() const;
  bool avoid_allcall () const;
  void set_avoid_allcall (bool avoid);
  bool spellcheck() const;
  int heartbeat () const;
  int watchdog () const;
  bool TX_messages () const;
  bool split_mode () const;
  QString opCall() const;
  QString ptt_command() const;
  QString aprs_server_name () const;
  port_type aprs_server_port () const;
  QString udp_server_name () const;
  port_type udp_server_port () const;
  QString tcp_server_name () const;
  port_type tcp_server_port () const;
  QString n1mm_server_name () const;
  port_type n1mm_server_port () const;
  bool valid_n1mm_info () const;
  bool broadcast_to_n1mm() const;
  QString n3fjp_server_name () const;
  port_type n3fjp_server_port () const;
  bool valid_n3fjp_info () const;
  bool broadcast_to_n3fjp() const;
  bool accept_udp_requests () const;
  bool accept_tcp_requests () const;
  bool udpEnabled () const;
  bool tcpEnabled () const;
  int tcp_max_connections () const;
  Bands * bands ();
  Bands const * bands () const;
  IARURegions::Region region () const;
  FrequencyList_v2 * frequencies ();
  FrequencyList_v2 const * frequencies () const;
  StationList * stations ();
  StationList const * stations () const;
  bool auto_switch_bands() const;
  QStringListModel * macros ();
  QStringListModel const * macros () const;
  QDir save_directory () const;
  QString rig_name () const;
  QColor color_table_background() const;
  QColor color_table_highlight() const;
  QColor color_table_foreground() const;
  QColor color_primary_highlight () const;
  QColor color_secondary_highlight () const;
  QColor color_CQ () const;
  QColor color_MyCall () const;
  QColor color_rx_background () const;
  QColor color_rx_foreground () const;
  QColor color_tx_foreground () const;
  QColor color_compose_background () const;
  QColor color_compose_foreground () const;
  QColor color_DXCC () const;
  QColor color_NewCall () const;
  bool pwrBandTxMemory () const;
  bool pwrBandTuneMemory () const;

  struct CalibrationParams
  {
    CalibrationParams ()
      : intercept {0.}
      , slope_ppm {0.}
    {
    }

    CalibrationParams (double the_intercept, double the_slope_ppm)
      : intercept {the_intercept}
      , slope_ppm {the_slope_ppm}
    {
    }

    double intercept;           // Hertz
    double slope_ppm;           // Hertz
  };

  // Temporarily enable or disable calibration adjustments.
  void enable_calibration (bool = true);

  // Set the calibration parameters and enable calibration corrections.
  void set_calibration (CalibrationParams);

  // Set the dynamic grid which is only used if configuration setting is enabled.
  void set_dynamic_location (QString const&);

  // Set the dynamic station info message which is only used if configuration setting is enabled.
  void set_dynamic_station_info(QString const& info);

  // Set the dynamic station status message which is only used if configuration setting is enabled.
  void set_dynamic_station_status(QString const& status);

  // This method queries if a CAT and PTT connection is operational.
  bool is_transceiver_online () const;

  // Start the rig connection, safe and normal to call when rig is
  // already open.
  bool transceiver_online ();

  // check if a real rig is configured
  bool is_dummy_rig () const;

  // Frequency resolution of the rig
  //
  //  0 - 1Hz
  //  1 - 10Hz rounded
  // -1 - 10Hz truncated
  //  2 - 100Hz rounded
  // -2 - 100Hz truncated
  int transceiver_resolution () const;

  // Close down connection to rig.
  void transceiver_offline ();

  // Set transceiver frequency in Hertz.
  Q_SLOT void transceiver_frequency (Frequency);

  // Setting a non zero TX frequency means split operation
  // rationalise_mode means ensure TX uses same mode as RX.
  Q_SLOT void transceiver_tx_frequency (Frequency = 0u);

  // Set transceiver mode.
  //
  // Rationalise means ensure TX uses same mode as RX.
  Q_SLOT void transceiver_mode (MODE);

  // Set/unset PTT.
  //
  // Note that this must be called even if VOX PTT is selected since
  // the "Emulate Split" mode requires PTT information to coordinate
  // frequency changes.
  Q_SLOT void transceiver_ptt (bool = true);

  // Attempt to (re-)synchronise transceiver state.
  //
  // Force signal guarantees either a transceiver_update or a
  // transceiver_failure signal.
  //
  // The enforce_mode_and_split parameter ensures that future
  // transceiver updates have the correct mode and split setting
  // i.e. the transceiver is ready for use.
  Q_SLOT void sync_transceiver (bool force_signal = false, bool enforce_mode_and_split = false);

  Q_SLOT void invalidate_audio_input_device (QString error);
  Q_SLOT void invalidate_audio_output_device (QString error);
  Q_SLOT void invalidate_notification_audio_output_device (QString error);

  //
  // These signals indicate a font has been selected and accepted for
  // the application text and decoded text respectively.
  //
  Q_SIGNAL void gui_text_font_changed (QFont);
  Q_SIGNAL void tx_text_font_changed (QFont);
  Q_SIGNAL void rx_text_font_changed (QFont);
  Q_SIGNAL void compose_text_font_changed (QFont);
  Q_SIGNAL void table_font_changed (QFont);
  Q_SIGNAL void colors_changed ();

  //
  // This signal is emitted when the UDP & TCP server changes
  //
  Q_SIGNAL void udp_server_name_changed (QString const& name);
  Q_SIGNAL void udp_server_port_changed (port_type port);
  Q_SIGNAL void tcp_server_changed (QString const& host);
  Q_SIGNAL void tcp_server_port_changed (port_type port);
  Q_SIGNAL void tcp_max_connections_changed (int n);

  // This signal is emitted when the band schedule changes
  Q_SIGNAL void band_schedule_changed (StationList &stations);

  //
  // These signals are emitted and reflect transceiver state changes
  //

  // signals a change in one of the TransceiverState members
  Q_SIGNAL void transceiver_update (Transceiver::TransceiverState const&) const;

  // Signals a failure of a control rig CAT or PTT connection.
  //
  // A failed rig CAT or PTT connection is fatal and the underlying
  // connections are closed automatically. The connections can be
  // re-established with a call to transceiver_online(true) assuming
  // the fault condition has been rectified or is transient.
  Q_SIGNAL void transceiver_failure (QString const& reason) const;

  // signal announces audio devices are being enumerated
  //
  // As this can take some time, particularly on Linux, consumers
  // might like to notify the user.
  Q_SIGNAL void enumerating_audio_devices ();

private:
  class impl;
  pimpl<impl> m_;
};

#if QT_VERSION < 0x050500
Q_DECLARE_METATYPE (Configuration::DataMode);
#endif

#if !defined (QT_NO_DEBUG_STREAM)
ENUM_QDEBUG_OPS_DECL (Configuration, DataMode);
#endif

ENUM_QDATASTREAM_OPS_DECL (Configuration, DataMode);

ENUM_CONVERSION_OPS_DECL (Configuration, DataMode);

#endif
