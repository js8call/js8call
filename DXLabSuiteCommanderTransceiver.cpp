#include "DXLabSuiteCommanderTransceiver.hpp"

#include <QLoggingCategory>
#include <QTcpSocket>
#include <QRegularExpression>
#include <QLocale>
#include <QThread>
#include <QDateTime>

#include "DriftingDateTime.h"

#include "NetworkServerLookup.hpp"

#include "moc_DXLabSuiteCommanderTransceiver.cpp"

Q_DECLARE_LOGGING_CATEGORY(dxlabsuitecommandertransceiver_js8)

namespace
{
  char const * const commander_transceiver_name {"DX Lab Suite Commander"};

  QString map_mode (Transceiver::MODE mode)
  {
    switch (mode)
      {
      case Transceiver::AM: return "AM";
      case Transceiver::CW: return "CW";
      case Transceiver::CW_R: return "CW-R";
      case Transceiver::USB: return "USB";
      case Transceiver::LSB: return "LSB";
      case Transceiver::FSK: return "RTTY";
      case Transceiver::FSK_R: return "RTTY-R";
      case Transceiver::DIG_L: return "DATA-L";
      case Transceiver::DIG_U: return "DATA-U";
      case Transceiver::FM:
      case Transceiver::DIG_FM:
        return "FM";
      default: break;
      }
    return "USB";
  }
}

void DXLabSuiteCommanderTransceiver::register_transceivers (TransceiverFactory::Transceivers * registry, int id)
{
  (*registry)[commander_transceiver_name] = TransceiverFactory::Capabilities {id, TransceiverFactory::Capabilities::network, true};
}

DXLabSuiteCommanderTransceiver::DXLabSuiteCommanderTransceiver (std::unique_ptr<TransceiverBase> wrapped,
                                                                QString const& address, bool use_for_ptt,
                                                                int poll_interval, QObject * parent)
  : PollingTransceiver {poll_interval, parent}
  , wrapped_ {std::move (wrapped)}
  , use_for_ptt_ {use_for_ptt}
  , server_ {address}
  , commander_ {nullptr}
{
}

int DXLabSuiteCommanderTransceiver::do_start ()
{
  TRACE_CAT ("DXLabSuiteCommanderTransceiver", "starting");
  if (wrapped_) wrapped_->start (0);

  auto server_details = network_server_lookup (server_, 52002u, QHostAddress::LocalHost, QAbstractSocket::IPv4Protocol);

  if (!commander_)
    {
      commander_ = new QTcpSocket {this}; // QObject takes ownership
    }

  commander_->connectToHost (std::get<0> (server_details), std::get<1> (server_details));
  if (!commander_->waitForConnected ())
    {
      TRACE_CAT ("DXLabSuiteCommanderTransceiver", "failed to connect" << commander_->errorString ());
      throw error {tr ("Failed to connect to DX Lab Suite Commander\n") + commander_->errorString ()};
    }

  // sleeps here are to ensure Commander has actually queried the rig
  // rather than returning cached data which maybe stale or simply
  // read backs of not yet committed values, the 2s delays are
  // arbitrary but hopefully enough as the actual time required is rig
  // and Commander setting dependent
  int resolution {0};
  QThread::msleep (2000);
  auto reply = command_with_reply ("<command:10>CmdGetFreq<parameters:0>");
  if (0 == reply.indexOf ("<CmdFreq:"))
    {
      auto f = string_to_frequency (reply.mid (reply.indexOf ('>') + 1));
      if (f && !(f % 10))
        {
          auto test_frequency = f - f % 100 + 55;
          auto f_string = frequency_to_string (test_frequency);
          auto params =  ("<xcvrfreq:%1>" + f_string).arg (f_string.size ());
          simple_command (("<command:10>CmdSetFreq<parameters:%1>" + params).arg (params.size ()));
          QThread::msleep (2000);
          reply = command_with_reply ("<command:10>CmdGetFreq<parameters:0>");
          if (0 == reply.indexOf ("<CmdFreq:"))
            {
              auto new_frequency = string_to_frequency (reply.mid (reply.indexOf ('>') + 1));
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
                  f_string = frequency_to_string (test_frequency);
                  params =  ("<xcvrfreq:%1>" + f_string).arg (f_string.size ());
                  simple_command (("<command:10>CmdSetFreq<parameters:%1>" + params).arg (params.size ()));
                  QThread::msleep (2000);
                  reply = command_with_reply ("<command:10>CmdGetFreq<parameters:0>");
                  new_frequency = string_to_frequency (reply.mid (reply.indexOf ('>') + 1));
                  if (9 == static_cast<Radio::FrequencyDelta> (new_frequency - test_frequency))
                    {
                      resolution = 2;   // 20Hz rounded
                    }
                }
              f_string = frequency_to_string (f);
              params =  ("<xcvrfreq:%1>" + f_string).arg (f_string.size ());
              simple_command (("<command:10>CmdSetFreq<parameters:%1>" + params).arg (params.size ()));
            }
        }
    }
  else
    {
      TRACE_CAT ("DXLabSuiteCommanderTransceiver", "get frequency unexpected response" << reply);
      throw error {tr ("DX Lab Suite Commander didn't respond correctly reading frequency: ") + reply};
    }

  poll ();
  return resolution;
}

void DXLabSuiteCommanderTransceiver::do_stop ()
{
  if (commander_)
    {
      commander_->close ();
      delete commander_, commander_ = nullptr;
    }

  if (wrapped_) wrapped_->stop ();
  TRACE_CAT ("DXLabSuiteCommanderTransceiver", "stopped");
}

void DXLabSuiteCommanderTransceiver::do_ptt (bool on)
{
  TRACE_CAT ("DXLabSuiteCommanderTransceiver", on << state ());
  if (use_for_ptt_)
    {
      simple_command (on  ? "<command:5>CmdTX<parameters:0>" : "<command:5>CmdRX<parameters:0>");

      bool tx {!on};
      auto start = DriftingDateTime::currentMSecsSinceEpoch ();
      // we must now wait for Tx on the rig, we will wait a short while
      // before bailing out
      while (tx != on && DriftingDateTime::currentMSecsSinceEpoch () - start < 1000)
        {
          auto reply = command_with_reply ("<command:9>CmdSendTx<parameters:0>");
          if (0 == reply.indexOf ("<CmdTX:"))
            {
              auto state = reply.mid (reply.indexOf ('>') + 1);
              if ("ON" == state)
                {
                  tx = true;
                }
              else if ("OFF" == state)
                {
                  tx = false;
                }
              else
                {
                  TRACE_CAT ("DXLabSuiteCommanderTransceiver", "unexpected TX state" << state);
                  throw error {tr ("DX Lab Suite Commander sent an unrecognised TX state: ") + state};
                }
            }
          else
            {
              TRACE_CAT ("DXLabSuiteCommanderTransceiver", "get TX unexpected response" << reply);
              throw error {tr ("DX Lab Suite Commander didn't respond correctly polling TX status: ") + reply};
            }
          if (tx != on) QThread::msleep (10); // don't thrash Commander
        }
      update_PTT (tx);
      if (tx != on)
        {
          TRACE_CAT ("DXLabSuiteCommanderTransceiver", "rig failed to respond to PTT: " << on);
          throw error {tr ("DX Lab Suite Commander rig did not respond to PTT: ") + (on ? "ON" : "OFF")};
        }
    }
  else
    {
      Q_ASSERT (wrapped_);
      TransceiverState new_state {wrapped_->state ()};
      new_state.ptt (on);
      wrapped_->set (new_state, 0);
      update_PTT (on);
    }
}

void DXLabSuiteCommanderTransceiver::do_frequency (Frequency f, MODE m, bool /*no_ignore*/)
{
  TRACE_CAT ("DXLabSuiteCommanderTransceiver", f << state ());
  auto f_string = frequency_to_string (f);
  if (UNK != m && m != get_mode ())
    {
      auto m_string = map_mode (m);
      auto params =  ("<xcvrfreq:%1>" + f_string + "<xcvrmode:%2>" + m_string + "<preservesplitanddual:1>Y").arg (f_string.size ()).arg (m_string.size ());
      simple_command (("<command:14>CmdSetFreqMode<parameters:%1>" + params).arg (params.size ()));
      update_mode (m);
    }
  else
    {
      auto params =  ("<xcvrfreq:%1>" + f_string).arg (f_string.size ());
      simple_command (("<command:10>CmdSetFreq<parameters:%1>" + params).arg (params.size ()));
    }
  update_rx_frequency (f);
}

void DXLabSuiteCommanderTransceiver::do_tx_frequency (Frequency tx, MODE mode, bool /*no_ignore*/)
{
  TRACE_CAT ("DXLabSuiteCommanderTransceiver", tx << state ());
  if (tx)
    {
      auto f_string = frequency_to_string (tx);
      auto params = ("<xcvrfreq:%1>" + f_string + "<SuppressDual:1>Y").arg (f_string.size ());
      if (UNK == mode)
        {
          params += "<SuppressModeChange:1>Y";
        }
      simple_command (("<command:11>CmdQSXSplit<parameters:%1>" + params).arg (params.size ()));
    }
  else
    {
      simple_command ("<command:8>CmdSplit<parameters:8><1:3>off");
    }
  update_split (tx);
  update_other_frequency (tx);
}

void DXLabSuiteCommanderTransceiver::do_mode (MODE m)
{
  TRACE_CAT ("DXLabSuiteCommanderTransceiver", m << state ());
  auto m_string = map_mode (m);
  auto params =  ("<1:%1>" + m_string).arg (m_string.size ());
  simple_command (("<command:10>CmdSetMode<parameters:%1>" + params).arg (params.size ()));
  update_mode (m);
}

void DXLabSuiteCommanderTransceiver::poll ()
{
  bool quiet {!transceiverbase_js8().isDebugEnabled()};

  auto reply = command_with_reply ("<command:10>CmdGetFreq<parameters:0>", quiet);
  if (0 == reply.indexOf ("<CmdFreq:"))
    {
      auto f = string_to_frequency (reply.mid (reply.indexOf ('>') + 1));
      if (f)
        {
          if (!state ().ptt ()) // Commander is not reliable on frequency
                                // polls while transmitting
            {
              update_rx_frequency (f);
            }
        }
    }
  else
    {
      TRACE_CAT_POLL ("DXLabSuiteCommanderTransceiver", "get frequency unexpected response" << reply);
      throw error {tr ("DX Lab Suite Commander didn't respond correctly polling frequency: ") + reply};
    }

  if (state ().split ())
    {
      reply = command_with_reply ("<command:12>CmdGetTXFreq<parameters:0>", quiet);
      if (0 == reply.indexOf ("<CmdTXFreq:"))
        {
          auto f = string_to_frequency (reply.mid (reply.indexOf ('>') + 1));
          if (f)
            {
              if (!state ().ptt ()) // Commander is not reliable on frequency
                                // polls while transmitting
                {
                  update_other_frequency (f);
                }
            }
        }
      else
        {
          TRACE_CAT_POLL ("DXLabSuiteCommanderTransceiver", "get tx frequency unexpected response" << reply);
          throw error {tr ("DX Lab Suite Commander didn't respond correctly polling TX frequency: ") + reply};
        }
    }

  reply = command_with_reply ("<command:12>CmdSendSplit<parameters:0>", quiet);
  if (0 == reply.indexOf ("<CmdSplit:"))
    {
      auto split = reply.mid (reply.indexOf ('>') + 1);
      if ("ON" == split)
        {
          update_split (true);
        }
      else if ("OFF" == split)
        {
          update_split (false);
        }
      else
        {
          TRACE_CAT_POLL ("DXLabSuiteCommanderTransceiver", "unexpected split state" << split);
          throw error {tr ("DX Lab Suite Commander sent an unrecognised split state: ") + split};
        }
    }
  else
    {
      TRACE_CAT_POLL ("DXLabSuiteCommanderTransceiver", "get split mode unexpected response" << reply);
      throw error {tr ("DX Lab Suite Commander didn't respond correctly polling split status: ") + reply};
    }

  get_mode (quiet);
}

auto DXLabSuiteCommanderTransceiver::get_mode (bool no_debug) -> MODE
{
  MODE m {UNK};
  auto reply = command_with_reply ("<command:11>CmdSendMode<parameters:0>", no_debug);
  if (0 == reply.indexOf ("<CmdMode:"))
    {
      auto mode = reply.mid (reply.indexOf ('>') + 1);
      if ("AM" == mode)
        {
          m = AM;
        }
      else if ("CW" == mode)
        {
          m = CW;
        }
      else if ("CW-R" == mode)
        {
          m = CW_R;
        }
      else if ("FM" == mode || "WBFM" == mode)
        {
          m = FM;
        }
      else if ("LSB" == mode)
        {
          m = LSB;
        }
      else if ("USB" == mode)
        {
          m = USB;
        }
      else if ("RTTY" == mode)
        {
          m = FSK;
        }
      else if ("RTTY-R" == mode)
        {
          m = FSK_R;
        }
      else if ("PKT" == mode || "DATA-L" == mode || "Data-L" == mode || "DIGL" == mode)
        {
          m = DIG_L;
        }
      else if ("PKT-R" == mode || "DATA-U" == mode || "Data-U" == mode || "DIGU" == mode)
        {
          m = DIG_U;
        }
      else
        {
          TRACE_CAT_POLL ("DXLabSuiteCommanderTransceiver", "unexpected mode name" << mode);
          throw error {tr ("DX Lab Suite Commander sent an unrecognised mode: \"") + mode + '"'};
        }
      update_mode (m);
    }
  else
    {
      TRACE_CAT_POLL ("DXLabSuiteCommanderTransceiver", "unexpected response" << reply);
      throw error {tr ("DX Lab Suite Commander didn't respond correctly polling mode: ") + reply};
    }
  return m;
}

void DXLabSuiteCommanderTransceiver::simple_command (QString const& cmd, bool no_debug)
{
  Q_ASSERT (commander_);

  if (!no_debug)
    {
      TRACE_CAT ("DXLabSuiteCommanderTransceiver", cmd);
    }

  if (!write_to_port (cmd))
    {
      TRACE_CAT ("DXLabSuiteCommanderTransceiver", "failed:" << commander_->errorString ());
      throw error {tr ("DX Lab Suite Commander send command failed\n") + commander_->errorString ()};
    }
}

QString DXLabSuiteCommanderTransceiver::command_with_reply (QString const& cmd, bool no_debug)
{
  Q_ASSERT (commander_);

  if (!write_to_port (cmd))
    {
      TRACE_CAT ("DXLabSuiteCommanderTransceiver", "failed to send command:" << commander_->errorString ());
      throw error {
        tr ("DX Lab Suite Commander failed to send command \"%1\": %2\n")
          .arg (cmd)
          .arg (commander_->errorString ())
          };
    }

  // waitForReadReady appears to be unreliable on Windows timing out
  // when data is waiting so retry a few times
  unsigned retries {5};
  bool replied {false};
  while (!replied && --retries)
    {
      replied = commander_->waitForReadyRead ();
      if (!replied && commander_->error () != commander_->SocketTimeoutError)
        {
          TRACE_CAT ("DXLabSuiteCommanderTransceiver", cmd << "failed to read reply:" << commander_->errorString ());
          throw error {
            tr ("DX Lab Suite Commander send command \"%1\" read reply failed: %2\n")
              .arg (cmd)
              .arg (commander_->errorString ())
              };
        }
    }

  if (!replied)
    {
      TRACE_CAT ("DXLabSuiteCommanderTransceiver", cmd << "retries exhausted");
      throw error {
        tr ("DX Lab Suite Commander retries exhausted sending command \"%1\"")
          .arg (cmd)
          };
    }

  auto result = commander_->readAll ();
  // qCDebug (dxlabsuitecommandertransceiver_js8) << "result: " << result;
  // for (int i = 0; i < result.size (); ++i)
  //   {
  //     qCDebug (dxlabsuitecommandertransceiver_js8) << i << ":" << hex << int (result[i]);
  //   }

  if (!no_debug)
    {
      TRACE_CAT ("DXLabSuiteCommanderTransceiver", cmd << "->" << result);
    }

  return result;                // converting raw UTF-8 bytes to QString
}

bool DXLabSuiteCommanderTransceiver::write_to_port (QString const& s)
{
  auto data = s.toLocal8Bit ();
  auto to_send = data.constData ();
  auto length = data.size ();

  qint64 total_bytes_sent {0};
  while (total_bytes_sent < length)
    {
      auto bytes_sent = commander_->write (to_send + total_bytes_sent, length - total_bytes_sent);
      if (bytes_sent < 0 || !commander_->waitForBytesWritten ())
        {
          return false;
        }

      total_bytes_sent += bytes_sent;
    }
  return true;
}

QString DXLabSuiteCommanderTransceiver::frequency_to_string (Frequency f) const
{
  // number is localized and in kHz, avoid floating point translation
  // errors by adding a small number (0.1Hz)
  return QString {"%L1"}.arg (f / 1e3 + 1e-4, 10, 'f', 3);
}

auto DXLabSuiteCommanderTransceiver::string_to_frequency (QString s) const -> Frequency
{
  // temporary hack because Commander is returning invalid UTF-8 bytes
  s.replace (QChar {QChar::ReplacementCharacter}, locale_.groupSeparator ());

  // remove DP - relies on n.nnn kHz format so we can do ulonglong
  // conversion to Hz
  bool ok;

  // auto f = locale_.toDouble (s, &ok); // use when CmdSendFreq and
                                      // CmdSendTxFreq reinstated

  auto f = QLocale::c ().toDouble (s, &ok); // temporary fix

  if (!ok)
    {
      throw error {tr ("DX Lab Suite Commander sent an unrecognized frequency")};
    }
  return (f + 1e-4) * 1e3;
}

Q_LOGGING_CATEGORY(dxlabsuitecommandertransceiver_js8, "dxlabsuitecommandertransceiver.js8", QtWarningMsg)
