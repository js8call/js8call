#include "HRDTransceiver.hpp"

#include <QHostAddress>
#include <QByteArray>
#include <QRegularExpression>
#include <QTcpSocket>
#include <QThread>
#include <QStandardPaths>
#include <QDir>

#include "NetworkServerLookup.hpp"

namespace
{
  char const * const HRD_transceiver_name = "Ham Radio Deluxe";

  // some commands require a settling time, particularly "RX A" and
  // "RX B" on the Yaesu FTdx3000.
  int constexpr yaesu_delay {250};
}

void HRDTransceiver::register_transceivers (TransceiverFactory::Transceivers * registry, int id)
{
  (*registry)[HRD_transceiver_name] = TransceiverFactory::Capabilities (id, TransceiverFactory::Capabilities::network, true, true /* maybe */);
}

struct HRDMessage
{
  // Placement style new overload for outgoing messages that does the
  // construction too.
  static void * operator new (size_t size, QString const& payload)
  {
    size += sizeof (QChar) * (payload.size () + 1); // space for terminator too
    HRDMessage * storage (reinterpret_cast<HRDMessage *> (new char[size]));
    storage->size_ = size ;
    ushort const * pl (payload.utf16 ());
    std::copy (pl, pl + payload.size () + 1, storage->payload_); // copy terminator too
    storage->magic_1_ = magic_1_value_;
    storage->magic_2_ = magic_2_value_;
    storage->checksum_ = 0;
    return storage;
  }

  // Placement style new overload for incoming messages that does the
  // construction too.
  //
  // No memory allocation here.
  static void * operator new (size_t /* size */, QByteArray const& message)
  {
    // Nasty const_cast here to avoid copying the message buffer.
    return const_cast<HRDMessage *> (reinterpret_cast<HRDMessage const *> (message.data ()));
  }

  void operator delete (void * p, size_t)
  {
    delete [] reinterpret_cast<char *> (p); // Mirror allocation in operator new above.
  }

  quint32 size_;
  qint32 magic_1_;
  qint32 magic_2_;
  qint32 checksum_;            // Apparently not used.
  QChar payload_[0];           // UTF-16 (which is wchar_t on Windows)

  static qint32 constexpr magic_1_value_ = 0x1234ABCD;
  static qint32 constexpr magic_2_value_ = 0xABCD1234;
};

HRDTransceiver::HRDTransceiver (std::unique_ptr<TransceiverBase> wrapped
                                , QString const& server
                                , bool use_for_ptt
                                , TransceiverFactory::TXAudioSource audio_source
                                , int poll_interval
                                , QObject * parent)
  : PollingTransceiver {poll_interval, parent}
  , wrapped_ {std::move (wrapped)}
  , use_for_ptt_ {use_for_ptt}
  , audio_source_ {audio_source}
  , server_ {server}
  , hrd_ {0}
  , protocol_ {none}
  , current_radio_ {0}
  , vfo_count_ {0}
  , vfo_A_button_ {-1}
  , vfo_B_button_ {-1}
  , vfo_toggle_button_ {-1}
  , mode_A_dropdown_ {-1}
  , mode_B_dropdown_ {-1}
  , data_mode_toggle_button_ {-1}
  , data_mode_on_button_ {-1}
  , data_mode_off_button_ {-1}
  , data_mode_dropdown_ {-1}
  , split_mode_button_ {-1}
  , split_mode_dropdown_ {-1}
  , split_mode_dropdown_write_only_ {false}
  , split_off_button_ {-1}
  , tx_A_button_ {-1}
  , tx_B_button_ {-1}
  , rx_A_button_ {-1}
  , rx_B_button_ {-1}
  , receiver_dropdown_ {-1}
  , ptt_button_ {-1}
  , alt_ptt_button_ {-1}
  , reversed_ {false}
{
}

int HRDTransceiver::do_start ()
{
  TRACE_CAT ("HRDTransceiver", "starting");
  if (wrapped_) wrapped_->start (0);

  auto server_details = network_server_lookup (server_, 7809u);
  if (!hrd_)
    {
      hrd_ = new QTcpSocket {this}; // QObject takes ownership
    }
  hrd_->connectToHost (std::get<0> (server_details), std::get<1> (server_details));
  if (!hrd_->waitForConnected ())
    {
      TRACE_CAT ("HRDTransceiver", "failed to connect:" <<  hrd_->errorString ());
      throw error {tr ("Failed to connect to Ham Radio Deluxe\n") + hrd_->errorString ()};
    }

  if (none == protocol_)
    {
      try
        {
          protocol_ = v5;	// try this first (works for v6 too)
          send_command ("get context", false, false);
        }
      catch (error const&)
        {
          protocol_ = none;
        }
    }

  if (none == protocol_)
    {
      hrd_->close ();

      protocol_ = v4;		// try again with older protocol
      hrd_->connectToHost (std::get<0> (server_details), std::get<1> (server_details));
      if (!hrd_->waitForConnected ())
        {
          TRACE_CAT ("HRDTransceiver", "failed to connect:" <<  hrd_->errorString ());
          throw error {tr ("Failed to connect to Ham Radio Deluxe\n") + hrd_->errorString ()};
        }

      send_command ("get context", false, false);
    }

  QFile HRD_info_file {QDir {QStandardPaths::writableLocation (QStandardPaths::AppDataLocation)}.absoluteFilePath ("HRD Interface Information.txt")};
  if (!HRD_info_file.open (QFile::WriteOnly | QFile::Text | QFile::Truncate))
    {
      throw error {tr ("Failed to open file \"%1\": %2.").arg (HRD_info_file.fileName ()).arg (HRD_info_file.errorString ())};
    }
  QTextStream HRD_info {&HRD_info_file};

  auto id = send_command ("get id", false, false);
  auto version = send_command ("get version", false, false);

  TRACE_CAT ("HRDTransceiver", "Id:" << id << "Version:" << version);
  HRD_info << "Id: " << id << "\n";
  HRD_info << "Version: " << version << "\n";

  auto radios = send_command ("get radios", false, false).trimmed ().split (',', Qt::SkipEmptyParts);
  if (radios.isEmpty ())
    {
      TRACE_CAT ("HRDTransceiver", "no rig found");
      throw error {tr ("Ham Radio Deluxe: no rig found")};
    }

  HRD_info << "Radios:\n";
  Q_FOREACH (auto const& radio, radios)
    {
      HRD_info << "\t" << radio << "\n";
      auto entries = radio.trimmed ().split (':', Qt::SkipEmptyParts);
      radios_.push_back (std::forward_as_tuple (entries[0].toUInt (), entries[1]));
    }

#if WSJT_TRACE_CAT
  TRACE_CAT ("HRDTransceiver", "radios:-");
  Q_FOREACH (auto const& radio, radios_)
    {
      TRACE_CAT ("HRDTransceiver", "\t[" << std::get<0> (radio) << "] " << std::get<1> (radio));
    }
#endif

  auto current_radio_name = send_command ("get radio", false, false, true);
  HRD_info << "Current radio: " << current_radio_name << "\n";
  if (current_radio_name.isEmpty ())
    {
      TRACE_CAT ("HRDTransceiver", "no rig found");
      throw error {tr ("Ham Radio Deluxe: no rig found")};
    }

  vfo_count_ = send_command ("get vfo-count").toUInt ();
  HRD_info << "VFO count: " << vfo_count_ << "\n";
  TRACE_CAT ("HRDTransceiver", "vfo count:" << vfo_count_);

  buttons_ = send_command ("get buttons").trimmed ().split (',', Qt::SkipEmptyParts).replaceInStrings (" ", "~");
  TRACE_CAT ("HRDTransceiver", "HRD Buttons: " << buttons_);
  HRD_info << "Buttons: {" << buttons_.join (", ") << "}\n";

  dropdown_names_ = send_command ("get dropdowns").trimmed ().split (',', Qt::SkipEmptyParts);
  TRACE_CAT ("HRDTransceiver", "Dropdowns:");
  HRD_info << "Dropdowns:\n";
  Q_FOREACH (auto const& dd, dropdown_names_)
    {
      auto selections = send_command ("get dropdown-list {" + dd + "}").trimmed ().split (',');
      TRACE_CAT ("HRDTransceiver", "\t" << dd << ": {" << selections.join (", ") << "}");
      HRD_info << "\t" << dd << ": {" << selections.join (", ") << "}\n";
      dropdowns_[dd] = selections;
    }

  slider_names_ = send_command ("get sliders").trimmed ().split (',', Qt::SkipEmptyParts).replaceInStrings (" ", "~");
  TRACE_CAT ("HRDTransceiver", "Sliders:-");
  HRD_info << "Sliders:\n";
  Q_FOREACH (auto const& s, slider_names_)
    {
      auto range = send_command ("get slider-range " + current_radio_name + " " + s).trimmed ().split (',', Qt::SkipEmptyParts);
      TRACE_CAT ("HRDTransceiver", "\t" << s << ": {" << range.join (", ") << "}");
      HRD_info << "\t" << s << ": {" << range.join (", ") << "}\n";
      sliders_[s] = range;
    }

  // set RX VFO
  rx_A_button_ = find_button (QRegularExpression ("^(RX~A)$"));
  rx_B_button_ = find_button (QRegularExpression ("^(RX~B)$"));

  // select VFO (sometime set as well)
  vfo_A_button_ = find_button (QRegularExpression ("^(VFO~A|Main)$"));
  vfo_B_button_ = find_button (QRegularExpression ("^(VFO~B|Sub)$"));

  vfo_toggle_button_ = find_button (QRegularExpression ("^(A~/~B)$"));

  split_mode_button_ = find_button (QRegularExpression ("^(Spl~On|Spl_On|Split|Split~On)$"));
  split_off_button_ = find_button (QRegularExpression ("^(Spl~Off|Spl_Off|Split~Off)$"));

  if ((split_mode_dropdown_ = find_dropdown (QRegularExpression ("^(Split)$"))) >= 0)
    {
      split_mode_dropdown_selection_on_ = find_dropdown_selection (split_mode_dropdown_, QRegularExpression ("^(On)$"));
      split_mode_dropdown_selection_off_ = find_dropdown_selection (split_mode_dropdown_, QRegularExpression ("^(Off)$"));
    }
  else if ((receiver_dropdown_ = find_dropdown (QRegularExpression ("^Receiver$"))) >= 0)
    {
      rx_A_selection_ = find_dropdown_selection (receiver_dropdown_, QRegularExpression ("^(RX / Off)$"));
      rx_B_selection_ = find_dropdown_selection (receiver_dropdown_, QRegularExpression ("^(Mute / RX)$"));
    }

  tx_A_button_ = find_button (QRegularExpression ("^(TX~main|TX~-~A|TX~A)$"));
  tx_B_button_ = find_button (QRegularExpression ("^(TX~sub|TX~-~B|TX~B)$"));

  if ((mode_A_dropdown_ = find_dropdown (QRegularExpression ("^(Main Mode|Mode|Mode A)$"))) >= 0)
    {
      map_modes (mode_A_dropdown_, &mode_A_map_);
    }
  else
    {
      Q_ASSERT (mode_A_dropdown_ <= 0);
    }

  if ((mode_B_dropdown_ = find_dropdown (QRegularExpression ("^(Sub Mode|Mode B)$"))) >= 0)
    {
      map_modes (mode_B_dropdown_, &mode_B_map_);
    }

  // Can't do this with ^Data$ as the button name because some Kenwood
  // rigs have a "Data" button which is for turning the DSP on and off
  // so we must filter by rig name as well
  if (current_radio_name.startsWith ("IC")) // works with Icom transceivers
    {
      data_mode_toggle_button_ = find_button (QRegularExpression ("^(Data)$"));
    }

  data_mode_on_button_ = find_button (QRegularExpression ("^(DATA-ON\\(mid\\))$"));
  data_mode_off_button_ = find_button (QRegularExpression ("^(DATA-OFF)$"));

  // Some newer Icoms have a Data drop down with (Off,On,D1,D2,D3)
  // Some newer Icoms have a Data drop down with (Off,D1,D2,D3)
  // Some newer Icoms (IC-7300) have a Data drop down with
  // (Off,,D1-FIL1,D1-FIL2,D1-FIL3) the missing value counts as an
  // index value - I think it is a drop down separator line - this
  // appears to be an HRD defect and we cannot work around it
  if ((data_mode_dropdown_ = find_dropdown (QRegularExpression ("^(Data)$"))) >= 0)
    {
      data_mode_dropdown_selection_on_ = find_dropdown_selection (data_mode_dropdown_, QRegularExpression ("^(On|Data1|D1|D1-FIL1)$"));
      data_mode_dropdown_selection_off_ = find_dropdown_selection (data_mode_dropdown_, QRegularExpression ("^(Off)$"));
    }

  ptt_button_ = find_button (QRegularExpression ("^(TX)$"));
  alt_ptt_button_ = find_button (QRegularExpression ("^(TX~Alt|TX~Data)$"));

  if (vfo_count_ == 1 && ((vfo_B_button_ >= 0 && vfo_A_button_ >= 0) || vfo_toggle_button_ >= 0))
    {
      // put the rig into a known state for tricky cases like Icoms

      auto f = send_command ("get frequency").toUInt ();
      auto m = get_data_mode (lookup_mode (get_dropdown (mode_A_dropdown_), mode_A_map_));
      set_button (vfo_B_button_ >= 0 ? vfo_B_button_ : vfo_toggle_button_);
      auto fo = send_command ("get frequency").toUInt ();
      update_other_frequency (fo);
      auto mo = get_data_mode (lookup_mode (get_dropdown (mode_A_dropdown_), mode_A_map_));
      set_button (vfo_A_button_ >= 0 ? vfo_A_button_ : vfo_toggle_button_);
      if (f != fo || m != mo)
        {
          // we must have started with A/MAIN
          update_rx_frequency (f);
          update_mode (m);
        }
      else
        {
          update_rx_frequency (send_command ("get frequency").toUInt ());
          update_mode (get_data_mode (lookup_mode (get_dropdown (mode_A_dropdown_), mode_A_map_)));
        }
    }

  int resolution {0};
  auto f = send_command ("get frequency").toUInt ();
  if (f && !(f % 10))
    {
      auto test_frequency = f - f % 100 + 55;
      send_simple_command ("set frequency-hz " + QString::number (test_frequency));
      auto new_frequency = send_command ("get frequency").toUInt ();
      switch (static_cast<Radio::FrequencyDelta> (new_frequency - test_frequency))
        {
        case -5: resolution = -1; break;  // 10Hz truncated
        case 5: resolution = 1; break;    // 10Hz rounded
        case -15: resolution = -2; break; // 20Hz truncated
        case -55: resolution = -2; break; // 100Hz truncated
        case 45: resolution = 2; break;   // 100Hz rounded
        }
      if (1 == resolution)      // may be 20Hz rounded
        {
          test_frequency = f - f % 100 + 51;
          send_simple_command ("set frequency-hz " + QString::number (test_frequency));
          new_frequency = send_command ("get frequency").toUInt ();
          if (9 == static_cast<Radio::FrequencyDelta> (new_frequency - test_frequency))
            {
              resolution = 2;   // 20Hz rounded
            }
        }
      send_simple_command ("set frequency-hz " + QString::number (f));
    }
  return resolution;
}

void HRDTransceiver::do_stop ()
{
  if (hrd_)
    {
      hrd_->close ();
    }

  if (wrapped_) wrapped_->stop ();
  TRACE_CAT ("HRDTransceiver", "stopped" << state () << "reversed" << reversed_);
}

int HRDTransceiver::find_button (QRegularExpression const& re) const
{
  return buttons_.indexOf (re);
}

int HRDTransceiver::find_dropdown (QRegularExpression const& re) const
{
  return dropdown_names_.indexOf (re);
}

std::vector<int> HRDTransceiver::find_dropdown_selection (int dropdown, QRegularExpression const& re) const
{
  std::vector<int> indices;     // this will always contain at least a
                                // -1
  auto list = dropdowns_.value (dropdown_names_.value (dropdown));
  int index {0};
  while (-1 != (index = list.lastIndexOf (re, index - 1)))
    {
      // search backwards because more specialized modes tend to be
      // later in list
      indices.push_back (index);
      if (!index)
        {
          break;
        }
    }
  return indices;
}

void HRDTransceiver::map_modes (int dropdown, ModeMap *map)
{
  // order matters here (both in the map and in the regexps)
  map->push_back (std::forward_as_tuple (CW, find_dropdown_selection (dropdown, QRegularExpression ("^(CW|CW\\(N\\))|CWL$"))));
  map->push_back (std::forward_as_tuple (CW_R, find_dropdown_selection (dropdown, QRegularExpression ("^(CW-R|CW-R\\(N\\)|CW|CWU)$"))));
  map->push_back (std::forward_as_tuple (LSB, find_dropdown_selection (dropdown, QRegularExpression ("^(LSB\\(N\\)|LSB)$"))));
  map->push_back (std::forward_as_tuple (USB, find_dropdown_selection (dropdown, QRegularExpression ("^(USB\\(N\\)|USB)$"))));
  map->push_back (std::forward_as_tuple (DIG_U, find_dropdown_selection (dropdown, QRegularExpression ("^(DIG|DIGU|DATA-U|PKT-U|DATA|USER-U|USB)$"))));
  map->push_back (std::forward_as_tuple (DIG_L, find_dropdown_selection (dropdown, QRegularExpression ("^(DIG|DIGL|DATA-L|PKT-L|DATA-R|USER-L|LSB)$"))));
  map->push_back (std::forward_as_tuple (FSK, find_dropdown_selection (dropdown, QRegularExpression ("^(DIG|FSK|RTTY|RTTY-LSB)$"))));
  map->push_back (std::forward_as_tuple (FSK_R, find_dropdown_selection (dropdown, QRegularExpression ("^(DIG|FSK-R|RTTY-R|RTTY|RTTY-USB)$"))));
  map->push_back (std::forward_as_tuple (AM, find_dropdown_selection (dropdown, QRegularExpression ("^(AM|DSB|SAM|DRM)$"))));
  map->push_back (std::forward_as_tuple (FM, find_dropdown_selection (dropdown, QRegularExpression ("^(FM|FM\\(N\\)|FM-N|WFM)$"))));
  map->push_back (std::forward_as_tuple (DIG_FM, find_dropdown_selection (dropdown, QRegularExpression ("^(PKT-FM|PKT|FM)$"))));

#if WSJT_TRACE_CAT
  TRACE_CAT ("HRDTransceiver", "for dropdown" << dropdown_names_[dropdown]);
  std::for_each (map->begin (), map->end (), [this, dropdown] (ModeMap::value_type const& item)
                 {
                   auto const& rhs = std::get<1> (item);
                   TRACE_CAT ("HRDTransceiver", '\t' << std::get<0> (item) << "<->" << (rhs.size () ? dropdowns_[dropdown_names_[dropdown]][rhs.front ()] : "None"));
                 });
#endif
}

int HRDTransceiver::lookup_mode (MODE mode, ModeMap const& map) const
{
  auto it = std::find_if (map.begin (), map.end (), [mode] (ModeMap::value_type const& item) {return std::get<0> (item) == mode;});
  if (map.end () == it)
    {
      throw error {tr ("Ham Radio Deluxe: rig doesn't support mode")};
    }
  return std::get<1> (*it).front ();
}

auto HRDTransceiver::lookup_mode (int mode, ModeMap const& map) const -> MODE
{
  if (mode < 0)
    {
      return UNK;               // no mode dropdown
    }

  auto it = std::find_if (map.begin (), map.end (), [mode] (ModeMap::value_type const& item)
                          {
                            auto const& indices = std::get<1> (item);
                            return indices.cend () != std::find (indices.cbegin (), indices.cend (), mode);
                          });
  if (map.end () == it)
    {
      throw error {tr ("Ham Radio Deluxe: sent an unrecognised mode")};
    }
  return std::get<0> (*it);
}

int HRDTransceiver::get_dropdown (int dd, bool no_debug)
{
  if (dd < 0)
    {
      return -1;                // no dropdown to interrogate
    }

  auto dd_name = dropdown_names_.value (dd);
  auto reply = send_command ("get dropdown-text {" + dd_name + "}", no_debug);
  auto colon_index = reply.indexOf (':');

  if (colon_index < 0)
    {
      return -1;
    }

  Q_ASSERT (reply.left (colon_index).trimmed () == dd_name);
  return dropdowns_.value (dropdown_names_.value (dd)).indexOf (reply.mid (colon_index + 1).trimmed ());
}

void HRDTransceiver::set_dropdown (int dd, int value)
{
  auto dd_name = dropdown_names_.value (dd);
  if (value >= 0)
    {
      send_simple_command ("set dropdown " + dd_name.replace (' ', '~') + ' ' + dropdowns_.value (dd_name).value (value).replace (' ', '~') + ' ' + QString::number (value));
    }
  else
    {
      TRACE_CAT ("HRDTransceiver", "item" << value << "not found in" << dd_name);
      throw error {tr ("Ham Radio Deluxe: item not found in %1 dropdown list").arg (dd_name)};
    }
}

void HRDTransceiver::do_ptt (bool on)
{
  TRACE_CAT ("HRDTransceiver", on);
  if (use_for_ptt_)
    {
      if (alt_ptt_button_ >= 0 && TransceiverFactory::TX_audio_source_rear == audio_source_)
        {
          set_button (alt_ptt_button_, on);
        }
      else if (ptt_button_ >= 0)
        {
          set_button (ptt_button_, on);
        }
      // else
      // allow for pathological HRD rig interfaces that don't do
      // PTT by simply not even trying
    }
  else
    {
      Q_ASSERT (wrapped_);
      TransceiverState new_state {wrapped_->state ()};
      new_state.ptt (on);
      wrapped_->set (new_state, 0);
    }
  update_PTT (on);
}

void HRDTransceiver::set_button (int button_index, bool checked)
{
  if (button_index >= 0)
    {
      send_simple_command ("set button-select " + buttons_.value (button_index) + (checked ? " 1" : " 0"));
      if (button_index == rx_A_button_ || button_index == rx_B_button_)
        {
          QThread::msleep (yaesu_delay);
        }
    }
  else
    {
      TRACE_CAT ("HRDTransceiver", "invalid button");
      throw error {tr ("Ham Radio Deluxe: button not available")};
    }
}

void HRDTransceiver::set_data_mode (MODE m)
{
  if (data_mode_toggle_button_ >= 0)
    {
      switch (m)
        {
        case DIG_U:
        case DIG_L:
        case DIG_FM:
          set_button (data_mode_toggle_button_, true);
          break;
        default:
          set_button (data_mode_toggle_button_, false);
          break;
        }
    }
  else if (data_mode_on_button_ >= 0 && data_mode_off_button_ >= 0)
    {
      switch (m)
        {
        case DIG_U:
        case DIG_L:
        case DIG_FM:
          set_button (data_mode_on_button_, true);
          break;
        default:
          set_button (data_mode_off_button_, true);
          break;
        }
    }
  else if (data_mode_dropdown_ >= 0
      && data_mode_dropdown_selection_off_.size ()
      && data_mode_dropdown_selection_on_.size ())
    {
      switch (m)
        {
        case DIG_U:
        case DIG_L:
        case DIG_FM:
          set_dropdown (data_mode_dropdown_, data_mode_dropdown_selection_on_.front ());
          break;
        default:
          set_dropdown (data_mode_dropdown_, data_mode_dropdown_selection_off_.front ());
          break;
        }
    }
}

auto HRDTransceiver::get_data_mode (MODE m, bool quiet) -> MODE
{
  if (data_mode_dropdown_ >= 0
      && data_mode_dropdown_selection_off_.size ())
    {
      auto selection = get_dropdown (data_mode_dropdown_, quiet);
      // can't check for on here as there may be multiple on values so
      // we must rely on the initial parse finding valid on values
      if (selection >= 0 && selection != data_mode_dropdown_selection_off_.front ())
        {
          switch (m)
            {
            case USB: m = DIG_U; break;
            case LSB: m = DIG_L; break;
            case FM: m = DIG_FM; break;
            default: break;
            }
        }
    }
  return m;
}

void HRDTransceiver::do_frequency (Frequency f, MODE m, bool /*no_ignore*/)
{
  TRACE_CAT ("HRDTransceiver", f << "reversed" << reversed_);
  if (UNK != m)
    {
      do_mode (m);
    }
  auto fo_string = QString::number (f);
  if (vfo_count_ > 1 && reversed_)
    {
      auto frequencies = send_command ("get frequencies").trimmed ().split ('-', Qt::SkipEmptyParts);
      send_simple_command ("set frequencies-hz " + QString::number (frequencies[0].toUInt ()) + ' ' + fo_string);
    }
  else
    {
      send_simple_command ("set frequency-hz " + QString::number (f));
    }
  update_rx_frequency (f);
}

void HRDTransceiver::do_tx_frequency (Frequency tx, MODE mode, bool /*no_ignore*/)
{
  TRACE_CAT ("HRDTransceiver", tx << "reversed" << reversed_);

  // re-check if reversed VFOs
  bool rx_A {true};
  bool rx_B {false};
  if (receiver_dropdown_ >= 0 && rx_A_selection_.size ())
    {
      auto selection = get_dropdown (receiver_dropdown_);
      rx_A = selection == rx_A_selection_.front ();
      if (!rx_A && rx_B_selection_.size ())
        {
          rx_B = selection == rx_B_selection_.front ();
        }
    }
  else if (vfo_B_button_ >= 0 || rx_B_button_ >= 0)
    {
      rx_A = is_button_checked (rx_A_button_ >= 0 ? rx_A_button_ : vfo_A_button_);
      if (!rx_A)
        {
          rx_B = is_button_checked (rx_B_button_ >= 0 ? rx_B_button_ : vfo_B_button_);
        }
    }
  reversed_ = rx_B;

  bool split {tx != 0};
  if (split)
    {
      if (mode != UNK)
        {
          if (!reversed_ && mode_B_dropdown_ >= 0)
            {
              set_dropdown (mode_B_dropdown_, lookup_mode (mode, mode_B_map_));
            }
          else if (reversed_ && mode_B_dropdown_ >= 0)
            {
              set_dropdown (mode_A_dropdown_, lookup_mode (mode, mode_A_map_));
            }
          else
            {
              Q_ASSERT (mode_A_dropdown_ >= 0 && ((vfo_A_button_ >=0 && vfo_B_button_ >=0) || vfo_toggle_button_ >= 0));

              if (rx_B_button_ >= 0)
                {
                  set_button (reversed_ ? rx_A_button_ : rx_B_button_);
                  set_dropdown (mode_A_dropdown_, lookup_mode (mode, mode_A_map_));
                  set_data_mode (mode);
                  set_button (reversed_ ? rx_B_button_ : rx_A_button_);
                }
              else if (receiver_dropdown_ >= 0
                       && rx_A_selection_.size () && rx_B_selection_.size ())
                {
                  set_dropdown (receiver_dropdown_, (reversed_ ? rx_A_selection_ : rx_B_selection_).front ());
                  set_dropdown (mode_A_dropdown_, lookup_mode (mode, mode_A_map_));
                  set_data_mode (mode);
                  set_dropdown (receiver_dropdown_, (reversed_ ? rx_B_selection_ : rx_A_selection_).front ());
                }
              else if (vfo_count_ > 1 && ((vfo_A_button_ >=0 && vfo_B_button_ >=0) || vfo_toggle_button_ >= 0))
                {
                  set_button (vfo_A_button_ >= 0 ? (reversed_ ? vfo_A_button_ : vfo_B_button_) : vfo_toggle_button_);
                  set_dropdown (mode_A_dropdown_, lookup_mode (mode, mode_A_map_));
                  set_data_mode (mode);
                  set_button (vfo_A_button_ >= 0 ? (reversed_ ? vfo_B_button_ : vfo_A_button_) : vfo_toggle_button_);
                }
              // else Tx VFO mode gets set with frequency below or we
              // don't have a way of setting it so we assume it is
              // always the same as the Rx VFO mode
            }
        }

      auto fo_string = QString::number (tx);
      if (reversed_)
        {
          Q_ASSERT (vfo_count_ > 1);

          auto frequencies = send_command ("get frequencies").trimmed ().split ('-', Qt::SkipEmptyParts);
          send_simple_command ("set frequencies-hz " + fo_string + ' ' + QString::number (frequencies[1].toUInt ()));
        }
      else
        {
          if (vfo_count_ > 1)
            {
              auto frequencies = send_command ("get frequencies").trimmed ().split ('-', Qt::SkipEmptyParts);
              send_simple_command ("set frequencies-hz " + QString::number (frequencies[0].toUInt ()) + ' ' + fo_string);
            }
          else if ((vfo_B_button_ >= 0 && vfo_A_button_ >= 0) || vfo_toggle_button_ >= 0)
            {
              // we rationalise the modes here as well as the frequencies
              set_button (vfo_B_button_ >= 0 ? vfo_B_button_ : vfo_toggle_button_);
              send_simple_command ("set frequency-hz " + fo_string);
              if (mode != UNK && mode_B_dropdown_ < 0)
                {
                  // do this here rather than later so we only
                  // toggle/switch VFOs once
                  set_dropdown (mode_A_dropdown_, lookup_mode (mode, mode_A_map_));
                  set_data_mode (mode);
                }
              set_button (vfo_A_button_ >= 0 ? vfo_A_button_ : vfo_toggle_button_);
            }
        }
    }
  update_other_frequency (tx);

  if (split_mode_button_ >= 0)
    {
      if (split_off_button_ >= 0 && !split)
        {
          set_button (split_off_button_);
        }
      else
        {
          set_button (split_mode_button_, split);
        }
    }
  else if (split_mode_dropdown_ >= 0
           && split_mode_dropdown_selection_off_.size ()
           && split_mode_dropdown_selection_on_.size ())
    {
      set_dropdown (split_mode_dropdown_, split ? split_mode_dropdown_selection_on_.front () : split_mode_dropdown_selection_off_.front ());
    }
  else if (vfo_A_button_ >= 0 && vfo_B_button_ >= 0 && tx_A_button_ >= 0 && tx_B_button_ >= 0)
    {
      if (split)
        {
          if (reversed_ != is_button_checked (tx_A_button_))
            {
              if (rx_A_button_ >= 0 && rx_B_button_ >= 0)
                {
                  set_button (reversed_ ? rx_B_button_ : rx_A_button_);
                }
              else if (receiver_dropdown_ >= 0
                       && rx_A_selection_.size () && rx_B_selection_.size ())
                {
                  set_dropdown (receiver_dropdown_, (reversed_ ? rx_B_selection_ : rx_A_selection_).front ());
                }
              else
                {
                  set_button (reversed_ ? vfo_B_button_ : vfo_A_button_);
                }
              set_button (reversed_ ? tx_A_button_ : tx_B_button_);
            }
        }
      else
        {
          if (reversed_ != is_button_checked (tx_B_button_))
            {
              if (rx_A_button_ >= 0 && rx_B_button_ >= 0)
                {
                  set_button (reversed_ ? rx_B_button_ : rx_A_button_);
                }
              else if (receiver_dropdown_ >= 0
                       && rx_A_selection_.size () && rx_B_selection_.size ())
                {
                  set_dropdown (receiver_dropdown_, (reversed_ ? rx_B_selection_ : rx_A_selection_).front ());
                }
              else
                {
                  set_button (reversed_ ? vfo_B_button_ : vfo_A_button_);
                }
              set_button (reversed_ ? tx_B_button_ : tx_A_button_);
            }
        }
    }
  update_split (split);
}

void HRDTransceiver::do_mode (MODE mode)
{
  TRACE_CAT ("HRDTransceiver", mode);
  if (reversed_ && mode_B_dropdown_ >= 0)
    {
      set_dropdown (mode_B_dropdown_, lookup_mode (mode, mode_B_map_));
    }
  else
    {
      set_dropdown (mode_A_dropdown_, lookup_mode (mode, mode_A_map_));
    }
  if (mode != UNK && state ().split ()) // rationalise mode if split
    {
      if (reversed_)
        {
          if (mode_B_dropdown_ >= 0)
            {
              set_dropdown (mode_A_dropdown_, lookup_mode (mode, mode_A_map_));
            }
          else
            {
              Q_ASSERT ((vfo_B_button_ >= 0 && vfo_A_button_ >= 0) || vfo_toggle_button_ >= 0);

              if (rx_B_button_ >= 0)
                {
                  set_button (rx_A_button_);
                  set_dropdown (mode_A_dropdown_, lookup_mode (mode, mode_A_map_));
                  set_button (rx_B_button_);
                }
              else if (receiver_dropdown_ >= 0
                       && rx_A_selection_.size () && rx_B_selection_.size ())
                {
                  set_dropdown (receiver_dropdown_, rx_A_selection_.front ());
                  set_dropdown (mode_A_dropdown_, lookup_mode (mode, mode_A_map_));
                  set_dropdown (receiver_dropdown_, rx_B_selection_.front ());
                }
              else if (vfo_count_ > 1 && ((vfo_A_button_ >=0 && vfo_B_button_ >=0) || vfo_toggle_button_ >= 0))
                {
                  set_button (vfo_A_button_ >= 0 ? vfo_A_button_ : vfo_toggle_button_);
                  set_dropdown (mode_A_dropdown_, lookup_mode (mode, mode_A_map_));
                  set_button (vfo_B_button_ >= 0 ? vfo_B_button_ : vfo_toggle_button_);
                }
              // else Tx VFO mode gets set when Tx VFO frequency is
              // set

              if ( tx_A_button_ >= 0)
                {
                  set_button (tx_A_button_);
                }
            }
        }
      else
        {
          if (mode_B_dropdown_ >= 0)
            {
              set_dropdown (mode_B_dropdown_, lookup_mode (mode, mode_B_map_));
            }
          else
            {
              Q_ASSERT ((vfo_B_button_ >= 0 && vfo_A_button_ >= 0) || vfo_toggle_button_ >= 0);

              if (rx_B_button_ >= 0)
                {
                  set_button (rx_B_button_);
                  set_dropdown (mode_A_dropdown_, lookup_mode (mode, mode_A_map_));
                  set_button (rx_A_button_);
                }
              else if (receiver_dropdown_ >= 0
                       && rx_A_selection_.size () && rx_B_selection_.size ())
                {
                  set_dropdown (receiver_dropdown_, rx_B_selection_.front ());
                  set_dropdown (mode_A_dropdown_, lookup_mode (mode, mode_A_map_));
                  set_dropdown (receiver_dropdown_, rx_A_selection_.front ());
                }
              else if (vfo_count_ > 1 && ((vfo_A_button_ >=0 && vfo_B_button_ >=0) || vfo_toggle_button_ >= 0))
                {
                  set_button (vfo_B_button_ >= 0 ? vfo_B_button_ : vfo_toggle_button_);
                  set_dropdown (mode_A_dropdown_, lookup_mode (mode, mode_A_map_));
                  set_button (vfo_A_button_ >= 0 ? vfo_A_button_ : vfo_toggle_button_);
                }
              // else Tx VFO mode gets set when Tx VFO frequency is
              // set

              if ( tx_B_button_ >= 0)
                {
                  set_button (tx_B_button_);
                }
            }
        }
    }
  set_data_mode (mode);
  update_mode (mode);
}

bool HRDTransceiver::is_button_checked (int button_index, bool no_debug)
{
  if (button_index < 0)
    {
      return false;
    }

  auto reply = send_command ("get button-select " + buttons_.value (button_index), no_debug);
  if ("1" != reply && "0" != reply)
    {
      TRACE_CAT ("HRDTransceiver", "bad response");
      throw error {tr ("Ham Radio Deluxe didn't respond as expected")};
    }
  return "1" == reply;
}

void HRDTransceiver::poll ()
{
#if WSJT_TRACE_CAT && WSJT_TRACE_CAT_POLLS
  bool quiet {false};
  qDebug () << "+++++++ poll dump +++++++";
  qDebug () << "reversed:" << reversed_;
  is_button_checked (vfo_A_button_);
  is_button_checked (vfo_B_button_);
  is_button_checked (vfo_toggle_button_);
  is_button_checked (split_mode_button_);
  is_button_checked (split_off_button_);
  is_button_checked (rx_A_button_);
  is_button_checked (rx_B_button_);
  get_dropdown (receiver_dropdown_);
  is_button_checked (tx_A_button_);
  is_button_checked (tx_B_button_);
  is_button_checked (ptt_button_);
  is_button_checked (alt_ptt_button_);
  get_dropdown (mode_A_dropdown_);
  get_dropdown (mode_B_dropdown_);
  is_button_checked (data_mode_toggle_button_);
  is_button_checked (data_mode_on_button_);
  is_button_checked (data_mode_off_button_);
  if (data_mode_dropdown_ >=0
      && data_mode_dropdown_selection_off_.size ()
      && data_mode_dropdown_selection_on_.size ())
    {
      get_dropdown (data_mode_dropdown_);
    }
  if (!split_mode_dropdown_write_only_)
    {
      get_dropdown (split_mode_dropdown_);
    }
  qDebug () << "------- poll dump -------";
#else
  bool quiet {true};
#endif

  if (split_off_button_ >= 0)
    {
      // we are probably dealing with an Icom and have to guess SPLIT mode :(
    }
  else if (split_mode_button_ >= 0 && !(tx_A_button_ >= 0 && tx_B_button_ >= 0))
    {
      update_split (is_button_checked (split_mode_button_, quiet));
    }
  else if (split_mode_dropdown_ >= 0)
    {
      if (!split_mode_dropdown_write_only_)
        {
          auto selection = get_dropdown (split_mode_dropdown_, quiet);
          if (selection >= 0
              && split_mode_dropdown_selection_off_.size ())
            {
              update_split (selection == split_mode_dropdown_selection_on_.front ());
            }
          else
            {
              // leave split alone as we can't query it - it should be
              // correct so long as rig or HRD haven't been changed
              split_mode_dropdown_write_only_ = true;
            }
        }
    }
  else if (vfo_A_button_ >= 0 && vfo_B_button_ >= 0 && tx_A_button_ >= 0 && tx_B_button_ >= 0)
    {
      bool rx_A {true};         // no Rx taken as not reversed
      bool rx_B {false};

      auto tx_A = is_button_checked (tx_A_button_, quiet);

      // some rigs have dual Rx, we take VFO A/MAIN receiving as
      // normal and only say reversed when only VFO B/SUB is active
      // i.e. VFO A/MAIN muted VFO B/SUB active
      if (receiver_dropdown_ >= 0 && rx_A_selection_.size ())
        {
          auto selection = get_dropdown (receiver_dropdown_);
          rx_A = selection == rx_A_selection_.front ();
          if (!rx_A && rx_B_selection_.size ())
            {
              rx_B = selection == rx_B_selection_.front ();
            }
        }
      else if (vfo_B_button_ >= 0 || rx_B_button_ >= 0)
        {
          rx_A = is_button_checked (rx_A_button_ >= 0 ? rx_A_button_ : vfo_A_button_, quiet);
          if (!rx_A)
            {
              rx_B = is_button_checked (rx_B_button_ >= 0 ? rx_B_button_ : vfo_B_button_, quiet);
            }
        }

      update_split (rx_B == tx_A);
      reversed_ = rx_B;
    }

  if (vfo_count_ > 1)
    {
      auto frequencies = send_command ("get frequencies", quiet).trimmed ().split ('-', Qt::SkipEmptyParts);
      update_rx_frequency (frequencies[reversed_ ? 1 : 0].toUInt ());
      update_other_frequency (frequencies[reversed_ ? 0 : 1].toUInt ());
    }
  else
    {
      // read frequency is unreliable on single VFO addressing rigs
      // while transmitting
      if (!state ().ptt ())
        {
          update_rx_frequency (send_command ("get frequency", quiet).toUInt ());
        }
    }

  // read mode is unreliable on single VFO addressing rigs while
  // transmitting
  if (vfo_count_ > 1 || !state ().ptt ())
    {
      update_mode (get_data_mode (lookup_mode (get_dropdown (mode_A_dropdown_, quiet), mode_A_map_), quiet));
    }
}

QString HRDTransceiver::send_command (QString const& cmd, bool no_debug, bool prepend_context, bool recurse)
{
  Q_ASSERT (hrd_);

  QString result;

  if (current_radio_ && prepend_context && vfo_count_ < 2)
    {
      // required on some radios because commands don't get executed
      // correctly otherwise (ICOM for example)
      QThread::msleep (50);
   }

  if (!recurse && prepend_context)
    {
      auto radio_name = send_command ("get radio", true, current_radio_, true);
      auto radio_iter = std::find_if (radios_.begin (), radios_.end (), [&radio_name] (RadioMap::value_type const& radio)
                                      {
                                        return std::get<1> (radio) == radio_name;
                                      });
      if (radio_iter == radios_.end ())
        {
          TRACE_CAT ("HRDTransceiver", "rig disappeared or changed");
          throw error {tr ("Ham Radio Deluxe: rig has disappeared or changed")};
        }

      if (0u == current_radio_ || std::get<0> (*radio_iter) != current_radio_)
        {
          current_radio_ = std::get<0> (*radio_iter);
        }
    }

  auto context = '[' + QString::number (current_radio_) + "] ";

  if (QTcpSocket::ConnectedState != hrd_->state ())
    {
      TRACE_CAT ("HRDTransceiver", cmd << "failed" << hrd_->errorString ());
      throw error {
        tr ("Ham Radio Deluxe send command \"%1\" failed %2\n")
          .arg (cmd)
          .arg (hrd_->errorString ())
          };
    }

  if (v4 == protocol_)
    {
      auto message = ((prepend_context ? context + cmd : cmd) + "\r").toLocal8Bit ();
      if (!write_to_port (message.constData (), message.size ()))
        {
          TRACE_CAT ("HRDTransceiver", "failed to write command" << cmd << "to HRD");
          throw error {
            tr ("Ham Radio Deluxe: failed to write command \"%1\"")
              .arg (cmd)
              };
        }
    }
  else
    {
      auto string = prepend_context ? context + cmd : cmd;
      QScopedPointer<HRDMessage> message {new (string) HRDMessage};
      if (!write_to_port (reinterpret_cast<char const *> (message.data ()), message->size_))
        {
          TRACE_CAT ("HRDTransceiver", "failed to write command" << cmd << "to HRD");
          throw error {
            tr ("Ham Radio Deluxe: failed to write command \"%1\"")
              .arg (cmd)
              };
        }
    }
  auto buffer = read_reply (cmd);
  if (v4 == protocol_)
    {
      result = QString {buffer}.trimmed ();
    }
  else
    {
      HRDMessage const * reply {new (buffer) HRDMessage};
      if (reply->magic_1_value_ != reply->magic_1_ && reply->magic_2_value_ != reply->magic_2_)
        {
          TRACE_CAT ("HRDTransceiver", cmd << "invalid reply");
          throw error {
            tr ("Ham Radio Deluxe sent an invalid reply to our command \"%1\"")
              .arg (cmd)
              };
        }

      // keep reading until expected size arrives
      while (buffer.size () - offsetof (HRDMessage, size_) < reply->size_)
        {
          if (!no_debug)
            {
              TRACE_CAT ("HRDTransceiver", cmd << "reading more reply data");
            }
          buffer += read_reply (cmd);
          reply = new (buffer) HRDMessage;
        }

      result = QString {reply->payload_}; // this is not a memory leak (honest!)
    }
  if (!no_debug)
    {
      TRACE_CAT ("HRDTransceiver", cmd << " ->" << result);
    }
  return result;
}

bool HRDTransceiver::write_to_port (char const * data, qint64 length)
{
  qint64 total_bytes_sent {0};
  while (total_bytes_sent < length)
    {
      auto bytes_sent = hrd_->write (data + total_bytes_sent, length - total_bytes_sent);
      if (bytes_sent < 0 || !hrd_->waitForBytesWritten ())
        {
          return false;
        }

      total_bytes_sent += bytes_sent;
    }
  return true;
}

QByteArray HRDTransceiver::read_reply (QString const& cmd)
{
  // waitForReadReady appears to be occasionally unreliable on Windows
  // timing out when data is waiting so retry a few times
  unsigned retries {3};
  bool replied {false};
  while (!replied && retries--)
    {
      replied = hrd_->waitForReadyRead ();
      if (!replied && hrd_->error () != hrd_->SocketTimeoutError)
        {
          TRACE_CAT ("HRDTransceiver", cmd << "failed to reply" << hrd_->errorString ());
          throw error {
            tr ("Ham Radio Deluxe failed to reply to command \"%1\" %2\n")
              .arg (cmd)
              .arg (hrd_->errorString ())
              };
        }
    }
  if (!replied)
    {
      TRACE_CAT ("HRDTransceiver", cmd << "retries exhausted");
      throw error {
        tr ("Ham Radio Deluxe retries exhausted sending command \"%1\"")
          .arg (cmd)
          };
    }
  return hrd_->readAll ();
}

void HRDTransceiver::send_simple_command (QString const& command, bool no_debug)
{
  if ("OK" != send_command (command, no_debug))
    {
      TRACE_CAT ("HRDTransceiver", command << "unexpected response");
      throw error {
        tr ("Ham Radio Deluxe didn't respond to command \"%1\" as expected")
          .arg (command)
          };
    }
}
