#include "TCITransceiver.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <QDebug>
#include <QEventLoop>
#include <QLocale>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QtEndian>
#include <QtWebSockets/QWebSocket>

#include "TCIAudio.hpp"

#include "moc_TCITransceiver.cpp"

namespace
{
  quint32 constexpr kHeaderBytes = 64;
  quint32 constexpr kRxAudio = 1;
  quint32 constexpr kTxAudio = 2;
  quint32 constexpr kTxChrono = 3;

  int constexpr kFreeRunPeriodMs = 20;
  int constexpr kFreeRunFrames48 = 48000 / (1000 / kFreeRunPeriodMs); // 960
}

void TCITransceiver::register_transceivers (TransceiverFactory::Transceivers * registry, int id1, int id2)
{
  (*registry)[name_rx1 ()] = TransceiverFactory::Capabilities (id1, TransceiverFactory::Capabilities::tci, true, false, false, true);
  (*registry)[name_rx2 ()] = TransceiverFactory::Capabilities (id2, TransceiverFactory::Capabilities::tci, true, false, false, true);
}

TCITransceiver::TCITransceiver (QString const& rx_index, QString const& address, int poll_interval, QObject * parent)
  : PollingTransceiver {poll_interval, parent}
  , rx_ {rx_index}
  , server_ {address.trimmed ()}
{
}

TCITransceiver::~TCITransceiver ()
{
  if (socket_)
    {
      socket_->disconnect (this);
      socket_->close ();
    }
}

int TCITransceiver::sample_bytes (quint32 format)
{
  switch (format)
    {
    case 0: return 2; // int16
    case 1: return 3; // int24
    case 2: return 4; // int32
    case 3: return 4; // float32
    case 4: return 8; // float64
    default: return 0;
    }
}

bool TCITransceiver::parse_header (QByteArray const& data, Hdr& hdr)
{
  if (data.size () < int (kHeaderBytes)) return false;
  uchar const * p = reinterpret_cast<uchar const *> (data.constData ());
  auto word = [p] (int index) {return qFromLittleEndian<quint32> (p + index * 4);};
  hdr.receiver = word (0);
  hdr.sampleRate = word (1);
  hdr.format = word (2);
  hdr.length = word (5);
  hdr.type = word (6);
  hdr.channelsHint = word (7);
  return true;
}

bool TCITransceiver::parse_bool (QString const& text)
{
  QString const s = text.trimmed ().toLower ();
  return s == QLatin1String ("true") || s == QLatin1String ("1") || s == QLatin1String ("on") || s == QLatin1String ("tci");
}

auto TCITransceiver::parse_frequency (QString const& text) -> Frequency
{
  QString s = text.trimmed ();
  s.remove (' ');
  bool ok = false;
  double const value = QLocale::c ().toDouble (s, &ok);
  if (!ok || value < 0) return 0;
  return Frequency (std::llround (value));
}

auto TCITransceiver::mode_from_string (QString const& mode) -> MODE
{
  QString m = mode.trimmed ().toLower ();
  m.remove ('_');
  m.remove ('-');
  m.remove (' ');
  if (m == QLatin1String ("digu") || m == QLatin1String ("usbdigi") || m == QLatin1String ("datau") || m == QLatin1String ("pktusb")) return DIG_U;
  if (m == QLatin1String ("digl") || m == QLatin1String ("lsbdigi") || m == QLatin1String ("datal") || m == QLatin1String ("pktlsb")) return DIG_L;
  if (m == QLatin1String ("usb")) return USB;
  if (m == QLatin1String ("lsb")) return LSB;
  if (m == QLatin1String ("cw")) return CW;
  if (m == QLatin1String ("cwr")) return CW_R;
  if (m == QLatin1String ("am") || m == QLatin1String ("dsb") || m == QLatin1String ("sam")) return AM;
  if (m == QLatin1String ("wfm") || m == QLatin1String ("fm")) return FM;
  if (m == QLatin1String ("nfm") || m == QLatin1String ("digfm")) return DIG_FM;
  return UNK;
}

QString TCITransceiver::mode_to_string (MODE mode)
{
  switch (mode)
    {
    case AM: return QStringLiteral ("am");
    case CW: return QStringLiteral ("cw");
    case CW_R: return QStringLiteral ("cw");
    case USB: return QStringLiteral ("usb");
    case LSB: return QStringLiteral ("lsb");
    case DIG_L: return QStringLiteral ("digl");
    case DIG_U: return QStringLiteral ("digu");
    case FM: return QStringLiteral ("wfm");
    case DIG_FM: return QStringLiteral ("nfm");
    default: break;
    }
  return {};
}

void TCITransceiver::note_update ()
{
  if (!started_) return;
  update_complete ();
}

void TCITransceiver::send_text (QString const& command)
{
  if (!socket_ || !connected_) return;
  QString text = command;
  if (!text.endsWith (QLatin1Char (';'))) text += QLatin1Char (';');
  socket_->sendTextMessage (text);
}

void TCITransceiver::note_ready ()
{
  if (ready_) return;
  ready_ = true;
}

void TCITransceiver::on_connected ()
{
  connected_ = true;
  qInfo () << "TCI connected to" << server_ << "rx" << rx_;
}

void TCITransceiver::on_disconnected ()
{
  bool const was = connected_;
  connected_ = false;
  ready_ = false;
  audio_on_ = false;
  if (was)
    {
      qInfo () << "TCI disconnected";
    }
}

void TCITransceiver::on_error ()
{
  if (stopping_ || !socket_) return;
  QString message = socket_->errorString ();
  if (message.isEmpty ()) message = tr ("WebSocket error");
  qWarning () << "TCI socket error:" << message;
  if (!started_)
    {
      // do_start is still inside its connect loop. Let that loop fail
      // instead of shutting the rig down reentrantly.
      connected_ = false;
      return;
    }
  connected_ = false;
  offline (tr ("TCI connection failed: %1").arg (message));
}

void TCITransceiver::on_text (QString const& message)
{
  text_acc_ += message;
  int idx;
  while ((idx = text_acc_.indexOf (QLatin1Char (';'))) >= 0)
    {
      QString one = text_acc_.left (idx).trimmed ();
      text_acc_.remove (0, idx + 1);
      if (!one.isEmpty ()) handle_command (one);
    }
  if (text_acc_.size () > 65536) text_acc_.clear ();
}

void TCITransceiver::handle_command (QString const& command)
{
  int const colon = command.indexOf (QLatin1Char (':'));
  QString const name = (colon < 0 ? command : command.left (colon)).trimmed ().toLower ();
  QStringList args;
  if (colon >= 0)
    {
      args = command.mid (colon + 1).split (QLatin1Char (','));
      for (QString& arg : args) arg = arg.trimmed ();
    }

  int const rx = rx_.toInt ();

  if (name == QLatin1String ("ready"))
    {
      note_ready ();
      return;
    }
  if (name == QLatin1String ("protocol") || name == QLatin1String ("device") || name == QLatin1String ("app"))
    {
      // Any of these means the server is alive. "ready" still ends the burst.
      qInfo () << "TCI" << name << args.join (QLatin1Char (','));
      return;
    }
  if (name == QLatin1String ("audio_samplerate") && !args.isEmpty ())
    {
      int const rate = args.last ().toInt ();
      if (rate >= 8000 && rate <= 192000) sample_rate_ = quint32 (rate);
      return;
    }
  if (name == QLatin1String ("audio_stream_sample_type") && !args.isEmpty ())
    {
      int const format = args.last ().toInt ();
      if (sample_bytes (quint32 (format)) > 0) sample_format_ = quint32 (format);
      return;
    }
  if (name == QLatin1String ("audio_stream_channels") && !args.isEmpty ())
    {
      int const channels = args.last ().toInt ();
      if (channels == 1 || channels == 2) channels_ = channels;
      return;
    }
  if (name == QLatin1String ("audio_start"))
    {
      if (args.isEmpty () || args.at (0).toInt () == rx) audio_on_ = true;
      return;
    }
  if (name == QLatin1String ("audio_stop"))
    {
      if (args.isEmpty () || args.at (0).toInt () == rx) audio_on_ = false;
      return;
    }
  if ((name == QLatin1String ("vfo") || name == QLatin1String ("dds")) && args.size () >= 2)
    {
      int const trx = args.at (0).toInt ();
      if (trx != rx) return;
      int vfo = 0;
      QString freq_text;
      if (args.size () >= 3)
        {
          vfo = args.at (1).toInt ();
          freq_text = args.at (2);
        }
      else
        {
          freq_text = args.at (1);
        }
      Frequency const freq = parse_frequency (freq_text);
      if (!freq) return;
      // dds is the LO; prefer an explicit vfo report for the dial.
      if (name == QLatin1String ("dds") && rx_freq_) return;
      if (vfo == 0)
        {
          rx_freq_ = freq;
          update_rx_frequency (freq);
          note_update ();
        }
      else if (vfo == 1)
        {
          tx_freq_ = freq;
          update_other_frequency (freq);
          note_update ();
        }
      return;
    }
  if ((name == QLatin1String ("modulation") || name == QLatin1String ("mode")) && args.size () >= 2)
    {
      if (args.at (0).toInt () != rx) return;
      MODE const mode = mode_from_string (args.at (1));
      if (mode == UNK) return;
      mode_text_ = mode_to_string (mode);
      update_mode (mode);
      note_update ();
      return;
    }
  if (name == QLatin1String ("trx") && args.size () >= 2)
    {
      if (args.at (0).toInt () != rx) return;
      ptt_ = parse_bool (args.at (1));
      update_PTT (ptt_);
      note_update ();
      return;
    }
  if (name == QLatin1String ("split_enable") && args.size () >= 2)
    {
      if (args.at (0).toInt () != rx) return;
      split_ = parse_bool (args.at (1));
      update_split (split_);
      note_update ();
      return;
    }
  if (name == QLatin1String ("error") || name == QLatin1String ("error_code"))
    {
      qWarning () << "TCI server:" << command;
    }
}

void TCITransceiver::negotiate_audio ()
{
  // Ask for the rate and format JS8Call's modem already uses internally
  // (48 kHz). Float32 stereo is what ExpertSDR, Thetis and WSJT-X exchange.
  send_text (QStringLiteral ("audio_samplerate:48000"));
  send_text (QStringLiteral ("audio_stream_sample_type:3"));
  send_text (QStringLiteral ("audio_stream_channels:2"));
  send_text (QStringLiteral ("audio_stream_samples:2048"));
  send_text (QStringLiteral ("tx_enable:%1,true").arg (rx_));
  if (rx_ != QLatin1String ("0"))
    {
      send_text (QStringLiteral ("rx_enable:%1,true").arg (rx_));
    }
  send_text (QStringLiteral ("audio_start:%1").arg (rx_));
  audio_on_ = true;
  TCIAudio::instance ()->setEnabled (true);
  TCIAudio::instance ()->clearTx ();
}

int TCITransceiver::do_start ()
{
  // An update emitted before startup() marks the rig online makes the
  // GUI close and delete this object while do_start is still running.
  started_ = false;
  chrono_paced_ = false;
  ptt_ = false;
  split_ = false;
  audio_on_ = false;
  ready_ = false;
  text_acc_.clear ();
  rx_phase_ = 0;
  rx_have_prev_ = false;
  rx_rate_ = 0;
  sample_rate_ = 48000;
  sample_format_ = 3;
  channels_ = 2;

  if (!socket_)
    {
      socket_ = new QWebSocket {QString (), QWebSocketProtocol::VersionLatest, this};
      connect (socket_, &QWebSocket::connected, this, &TCITransceiver::on_connected);
      connect (socket_, &QWebSocket::disconnected, this, &TCITransceiver::on_disconnected);
      connect (socket_, &QWebSocket::textMessageReceived, this, &TCITransceiver::on_text);
      connect (socket_, &QWebSocket::binaryMessageReceived, this, &TCITransceiver::on_binary);
      connect (socket_, &QWebSocket::errorOccurred, this, &TCITransceiver::on_error);
    }
  if (!free_run_)
    {
      free_run_ = new QTimer {this};
      free_run_->setTimerType (Qt::PreciseTimer);
      connect (free_run_, &QTimer::timeout, this, &TCITransceiver::free_run_tx);
    }

  QString address = server_;
  if (address.isEmpty ()) address = QStringLiteral ("localhost:50001");
  if (!address.startsWith (QLatin1String ("ws://")) && !address.startsWith (QLatin1String ("wss://")))
    {
      address.prepend (QStringLiteral ("ws://"));
    }
  QUrl url {address};
  if (!url.isValid () || url.host ().isEmpty ())
    {
      throw error {tr ("TCI address \"%1\" is not a host:port or ws:// URL").arg (server_)};
    }
  if (url.port () < 0) url.setPort (50001);

  connected_ = false;
  QEventLoop connect_loop;
  QTimer connect_timer;
  connect_timer.setSingleShot (true);
  connect (&connect_timer, &QTimer::timeout, &connect_loop, &QEventLoop::quit);
  connect (socket_, &QWebSocket::connected, &connect_loop, &QEventLoop::quit);
  connect (socket_, &QWebSocket::errorOccurred, &connect_loop, &QEventLoop::quit);
  connect_timer.start (4000);
  if (socket_->state () != QAbstractSocket::UnconnectedState) socket_->abort ();
  socket_->open (url);
  connect_loop.exec ();

  if (!connected_ || socket_->state () != QAbstractSocket::ConnectedState)
    {
      QString const reason = socket_->errorString ().isEmpty ()
        ? tr ("timed out")
        : socket_->errorString ();
      socket_->close ();
      throw error {tr ("Could not connect to TCI at %1 (%2)").arg (url.toString (), reason)};
    }

  QEventLoop ready_loop;
  QTimer ready_timer;
  ready_timer.setSingleShot (true);
  connect (&ready_timer, &QTimer::timeout, &ready_loop, &QEventLoop::quit);
  QTimer pulse;
  connect (&pulse, &QTimer::timeout, &ready_loop, [&] {
    if (ready_) ready_loop.quit ();
  });
  pulse.start (50);
  ready_timer.start (3000);
  ready_loop.exec ();
  pulse.stop ();

  if (!connected_)
    {
      throw error {tr ("TCI connection closed before the server was ready")};
    }
  if (!ready_)
    {
      // Some servers skip the literal "ready" token but already sent their
      // device dump. Continue if the socket is still up.
      qInfo () << "TCI server did not send ready; continuing";
      ready_ = true;
    }

  negotiate_audio ();
  qInfo () << "TCI audio started on rx" << rx_ << "at" << url.toString ();
  // startup() sets online only after we return. Publish on the next
  // turn of this thread, once that has happened.
  QTimer::singleShot (0, this, [this] {
    if (!connected_ || stopping_) return;
    started_ = true;
    note_update ();
  });
  return 0; // 1 Hz frequency resolution
}

void TCITransceiver::do_stop ()
{
  started_ = false;
  if (stopping_) return;
  stopping_ = true;
  if (free_run_) free_run_->stop ();
  chrono_paced_ = false;
  if (socket_ && connected_)
    {
      if (ptt_) send_text (QStringLiteral ("trx:%1,false,tci").arg (rx_));
      send_text (QStringLiteral ("audio_stop:%1").arg (rx_));
      socket_->close ();
    }
  connected_ = false;
  ready_ = false;
  audio_on_ = false;
  ptt_ = false;
  TCIAudio::instance ()->clearTx ();
  stopping_ = false;
}

void TCITransceiver::do_frequency (Frequency f, MODE m, bool /*no_ignore*/)
{
  if (f)
    {
      send_text (QStringLiteral ("vfo:%1,0,%2").arg (rx_).arg (f));
      rx_freq_ = f;
      update_rx_frequency (f);
    }
  if (m != UNK)
    {
      QString const text = mode_to_string (m);
      if (!text.isEmpty () && text != mode_text_)
        {
          send_text (QStringLiteral ("modulation:%1,%2").arg (rx_, text));
          mode_text_ = text;
        }
      update_mode (m);
    }
}

void TCITransceiver::do_tx_frequency (Frequency tx, MODE mode, bool /*no_ignore*/)
{
  if (tx)
    {
      send_text (QStringLiteral ("split_enable:%1,true").arg (rx_));
      send_text (QStringLiteral ("vfo:%1,1,%2").arg (rx_).arg (tx));
      split_ = true;
      tx_freq_ = tx;
      update_split (true);
      update_other_frequency (tx);
    }
  else
    {
      send_text (QStringLiteral ("split_enable:%1,false").arg (rx_));
      split_ = false;
      tx_freq_ = 0;
      update_split (false);
      update_other_frequency (0);
    }
  if (mode != UNK)
    {
      QString const text = mode_to_string (mode);
      if (!text.isEmpty () && text != mode_text_)
        {
          send_text (QStringLiteral ("modulation:%1,%2").arg (rx_, text));
          mode_text_ = text;
          update_mode (mode);
        }
    }
}

void TCITransceiver::do_mode (MODE m)
{
  QString const text = mode_to_string (m);
  if (text.isEmpty ()) return;
  if (text != mode_text_)
    {
      send_text (QStringLiteral ("modulation:%1,%2").arg (rx_, text));
      mode_text_ = text;
    }
  update_mode (m);
}

void TCITransceiver::do_ptt (bool on)
{
  if (on == ptt_ && !on)
    {
      update_PTT (false);
      return;
    }
  // The third argument selects the TCI audio stream as the TX source.
  // Without it ExpertSDR3 keeps the microphone and the tones never leave.
  send_text (QStringLiteral ("trx:%1,%2,tci").arg (rx_, on ? QStringLiteral ("true") : QStringLiteral ("false")));
  ptt_ = on;
  update_PTT (on);
  if (on)
    {
      // Pull as soon as samples exist. Waiting for TX_CHRONO here lets
      // the modulator run ahead and the transmission lands late.
      // The first chrono frame, if the server sends one, takes over.
      if (!chrono_paced_ && free_run_ && !free_run_->isActive ())
        {
          free_run_->start (kFreeRunPeriodMs);
        }
    }
  else if (free_run_)
    {
      free_run_->stop ();
      TCIAudio::instance ()->clearTx ();
    }
}

void TCITransceiver::poll ()
{
  if (!connected_)
    {
      throw error {tr ("TCI connection lost")};
    }
  if (rx_freq_) update_rx_frequency (rx_freq_);
  if (!mode_text_.isEmpty ())
    {
      MODE const mode = mode_from_string (mode_text_);
      if (mode != UNK) update_mode (mode);
    }
  update_PTT (ptt_);
  update_split (split_);
  if (split_ && tx_freq_) update_other_frequency (tx_freq_);
}

void TCITransceiver::on_binary (QByteArray const& data)
{
  handle_binary (data);
}

void TCITransceiver::handle_binary (QByteArray const& data)
{
  Hdr hdr;
  if (!parse_header (data, hdr)) return;
  if (int (hdr.receiver) != rx_.toInt ()) return;

  if (hdr.type == kTxChrono)
    {
      on_tx_chrono (hdr);
      return;
    }
  if (hdr.type != kRxAudio || !TCIAudio::instance ()->enabled ()) return;

  int const bps = sample_bytes (hdr.format);
  int const payload = data.size () - int (kHeaderBytes);
  if (bps <= 0 || payload < bps) return;

  int scalars = payload / bps;
  if (hdr.length > 0 && int (hdr.length) * bps <= payload)
    {
      scalars = int (hdr.length);
    }
  int channels = (hdr.channelsHint == 1 || hdr.channelsHint == 2) ? int (hdr.channelsHint) : channels_;
  if (channels != 1 && channels != 2) channels = 2;
  if (scalars % channels) channels = 1;
  int const frames = scalars / channels;
  if (frames <= 0) return;

  uchar const * bytes = reinterpret_cast<uchar const *> (data.constData ()) + kHeaderBytes;
  QVector<float> mono (frames);
  for (int i = 0; i < frames; ++i)
    {
      int const offset = i * channels * bps;
      float sample = 0.f;
      switch (hdr.format)
        {
        case 0:
          {
            qint16 v = 0;
            std::memcpy (&v, bytes + offset, 2);
            sample = float (qFromLittleEndian (v)) / 32768.f;
          }
          break;
        case 1:
          {
            int v = int (bytes[offset]) | (int (bytes[offset + 1]) << 8) | (int (bytes[offset + 2]) << 16);
            if (v & 0x800000) v |= ~0xFFFFFF;
            sample = float (v) / 8388608.f;
          }
          break;
        case 2:
          {
            qint32 v = 0;
            std::memcpy (&v, bytes + offset, 4);
            sample = float (qFromLittleEndian (v)) / 2147483648.f;
          }
          break;
        case 3:
          {
            quint32 bits = 0;
            std::memcpy (&bits, bytes + offset, 4);
            bits = qFromLittleEndian (bits);
            std::memcpy (&sample, &bits, 4);
          }
          break;
        case 4:
          {
            quint64 bits = 0;
            std::memcpy (&bits, bytes + offset, 8);
            bits = qFromLittleEndian (bits);
            double d = 0;
            std::memcpy (&d, &bits, 8);
            sample = float (d);
          }
          break;
        default:
          return;
        }
      mono[i] = sample;
    }

  int const rate = hdr.sampleRate >= 8000 ? int (hdr.sampleRate) : int (sample_rate_);
  if (rate != rx_rate_)
    {
      rx_rate_ = rate;
      rx_phase_ = 0;
      rx_have_prev_ = false;
      if (rate > 0) sample_rate_ = quint32 (rate);
    }

  QVector<qint16> pcm;
  if (rate == 48000)
    {
      pcm.resize (frames);
      for (int i = 0; i < frames; ++i)
        {
          float const s = std::clamp (mono[i], -1.f, 1.f);
          pcm[i] = qint16 (std::lrintf (s * 32767.f));
        }
      if (frames) {rx_prev_ = mono[frames - 1]; rx_have_prev_ = true;}
    }
  else
    {
      double const step = double (rate) / 48000.0;
      double p = rx_phase_;
      while (p < frames)
        {
          int const i = int (p);
          float const frac = float (p - i);
          float const b = mono[std::min (i, frames - 1)];
          float const a = (i > 0) ? mono[i - 1] : (rx_have_prev_ ? rx_prev_ : b);
          float const s = std::clamp (a + (b - a) * frac, -1.f, 1.f);
          pcm.append (qint16 (std::lrintf (s * 32767.f)));
          p += step;
        }
      rx_phase_ = p - frames;
      if (frames) {rx_prev_ = mono[frames - 1]; rx_have_prev_ = true;}
    }

  if (!pcm.isEmpty ())
    {
      TCIAudio::instance ()->submitRx (pcm.constData (), pcm.size ());
    }
}

void TCITransceiver::on_tx_chrono (Hdr const& hdr)
{
  // Once the server paces us, stop the free-running sender so the
  // same audio is not transmitted twice.
  if (!chrono_paced_)
    {
      chrono_paced_ = true;
      if (free_run_) free_run_->stop ();
      qInfo () << "TCI TX clock is TX_CHRONO, length" << hdr.length << "rate" << hdr.sampleRate;
    }

  quint32 const format = sample_bytes (hdr.format) ? hdr.format : sample_format_;
  quint32 const rate = hdr.sampleRate >= 8000 ? hdr.sampleRate : sample_rate_;
  int channels = (hdr.channelsHint == 1 || hdr.channelsHint == 2) ? int (hdr.channelsHint) : channels_;
  quint32 scalars = hdr.length;
  if (scalars == 0) scalars = quint32 (channels * (rate / 50)); // 20 ms fallback
  if (channels != 1 && channels != 2) channels = 2;
  if (scalars % quint32 (channels)) channels = 1;
  int const frames = int (scalars / quint32 (channels));
  int const need48 = std::max (1, int ((qint64 (frames) * 48000) / std::max (quint32 (1), rate)));

  QVector<qint16> mono48 (need48);
  TCIAudio::instance ()->pullTx (mono48.data (), need48);
  send_tx_frame (mono48.constData (), need48, format, rate, channels, scalars);
}

void TCITransceiver::free_run_tx ()
{
  if (chrono_paced_ || !ptt_ || !connected_)
    {
      if (free_run_) free_run_->stop ();
      return;
    }
  QVector<qint16> mono48 (kFreeRunFrames48);
  TCIAudio::instance ()->pullTx (mono48.data (), kFreeRunFrames48);
  int const channels = channels_ == 1 ? 1 : 2;
  quint32 const rate = sample_rate_ ? sample_rate_ : 48000;
  int const frames = std::max (1, int ((qint64 (kFreeRunFrames48) * rate) / 48000));
  send_tx_frame (mono48.constData (), kFreeRunFrames48, sample_format_, rate, channels, quint32 (frames * channels));
}

void TCITransceiver::send_tx_frame (qint16 const * mono48, int frames48, quint32 format, quint32 rate, int channels, quint32 length_scalars)
{
  if (!socket_ || !connected_ || !mono48 || frames48 <= 0 || channels < 1) return;
  int const bps = sample_bytes (format);
  if (bps <= 0) return;
  if (!rate) rate = 48000;
  int const frames = std::max (1, int (length_scalars / quint32 (channels)));
  int const scalars = frames * channels;

  QByteArray frame (int (kHeaderBytes) + scalars * bps, '\0');
  auto put = [&frame] (int index, quint32 value) {
    qToLittleEndian (value, reinterpret_cast<uchar *> (frame.data ()) + index * 4);
  };
  put (0, quint32 (rx_.toInt ()));
  put (1, rate);
  put (2, format);
  put (3, 0);
  put (4, 0);
  put (5, quint32 (scalars)); // length counts scalar samples
  put (6, kTxAudio);
  put (7, quint32 (channels));

  uchar * payload = reinterpret_cast<uchar *> (frame.data ()) + kHeaderBytes;
  for (int i = 0; i < frames; ++i)
    {
      double const pos = (frames48 <= 1) ? 0.0 : (double (i) * double (frames48 - 1) / double (std::max (1, frames - 1)));
      int const i0 = std::clamp (int (pos), 0, frames48 - 1);
      int const i1 = std::min (frames48 - 1, i0 + 1);
      float const frac = float (pos - i0);
      float const s = std::clamp ((float (mono48[i0]) + (float (mono48[i1]) - float (mono48[i0])) * frac) / 32768.f, -1.f, 1.f);
      for (int c = 0; c < channels; ++c)
        {
          uchar * dest = payload + (i * channels + c) * bps;
          switch (format)
            {
            case 0:
              qToLittleEndian (qint16 (std::lrintf (s * 32767.f)), dest);
              break;
            case 2:
              qToLittleEndian (qint32 (std::lrintf (s * 2147483647.f)), dest);
              break;
            case 3:
              {
                quint32 bits = 0;
                std::memcpy (&bits, &s, 4);
                qToLittleEndian (bits, dest);
              }
              break;
            case 4:
              {
                double d = s;
                quint64 bits = 0;
                std::memcpy (&bits, &d, 8);
                qToLittleEndian (bits, dest);
              }
              break;
            case 1:
              {
                int v = int (std::lrintf (s * 8388607.f));
                dest[0] = uchar (v);
                dest[1] = uchar (v >> 8);
                dest[2] = uchar (v >> 16);
              }
              break;
            default:
              break;
            }
        }
    }
  socket_->sendBinaryMessage (frame);
}
