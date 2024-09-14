#include "PSKReporter.hpp"

// Interface for posting spots to PSK Reporter web site
// Implemented by Edson Pereira PY2SDR
// Updated by Bill Somerville, G4WJS
// Updated by Allan Bazinet, W6BAZ
//
// Reports will be sent in batch mode every 5 minutes.

#include <algorithm>
#include <ctime>
#include <cstddef>
#include <QByteArray>
#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QObject>
#include <QQueue>
#include <QRandomGenerator>
#include <QSharedPointer>
#include <QString>
#include <QTcpSocket>
#include <QTimer>
#include <QUdpSocket>

#include "Configuration.hpp"
#include "DriftingDateTime.h"
#include "pimpl_impl.hpp"

#include "moc_PSKReporter.cpp"

/******************************************************************************/
// Constants
/******************************************************************************/

namespace
{
  using namespace Qt::Literals::StringLiterals;

  constexpr auto             HOST               = "report.pskreporter.info"_L1;
  constexpr quint16          PORT               = 4739; // 14739 for test
  constexpr int              MIN_SEND_INTERVAL  = 120;  // in seconds
  constexpr int              FLUSH_INTERVAL     = 125;  // in send intervals
  constexpr qsizetype        MAX_STRING_LENGTH  = 254;  // PSK reporter spec
  constexpr std::time_t      CACHE_TIMEOUT      = 300;  // in seconds
  constexpr Radio::Frequency CACHE_BYPASS_FREQ  = 49000000;
  constexpr int              MIN_PAYLOAD_LENGTH = 508;
  constexpr int              MAX_PAYLOAD_LENGTH = 10000;
}

/******************************************************************************/
// Utility Functions
/******************************************************************************/

namespace
{
  // Write the string to the data stream in UTF-8 format, preceded by
  // a size byte.
  //
  // From https://pskreporter.info/pskdev.html
  //
  //   The data that follows is encoded as three (or four — the number
  //   depends on the number of fields in the record format descriptor)
  //   fields of byte length code followed by UTF-8 (use ASCII if you
  //   don't know what UTF-8 is) data. The length code is the number of
  //   bytes of data and does not include the length code itself. Each
  //   field is limited to a length code of no more than 254 bytes.
  //   Finally, the record is null padded to a multiple of 4 bytes.
  //
  // From https://datatracker.ietf.org/doc/rfc7011/
  //
  // 6.1.6.  string and octetArray
  // 
  //    The "string" data type represents a finite-length string of valid
  //    characters of the Unicode character encoding set.  The string data
  //    type MUST be encoded in UTF-8 [RFC3629] format.  The string is sent
  //    as an array of zero or more octets using an Information Element of
  //    fixed or variable length.  IPFIX Exporting Processes MUST NOT send
  //    IPFIX Messages containing ill-formed UTF-8 string values for
  //    Information Elements of the string data type; Collecting Processes
  //    SHOULD detect and ignore such values.  See [UTF8-EXPLOIT] for
  //    background on this issue.

  void
  writeUtfString(QDataStream   & out,
                 QString const & s)
  {
    auto utf = s.toUtf8();

    // The original code would just truncate the string to a maximum length
    // of 254 bytes blindly here, but that might land us in the middle of
    // a code point, thus violating 6.1.6. Therefore, if we must truncate,
    // we need to do so at a point where we stay legal.
 
    if (utf.size() > MAX_STRING_LENGTH)
    {
      // Walk back through the UTF-8 data and see where we can truncate.
      // Continuation bytes in UTF-8 sequences are in the range 0x80-0xBF.
      // Going backward from the limit, attempt to find the first starting
      // byte at which the string can be truncated safely. Since UTF-8 byte
      // sequences aren't longer than 4 bytes, this should not take more
      // than 4 loop iterations to find the correct position. Worst case,
      // we're going to emit a zero-length string.

      auto const truncatePosition = [&utf]() -> qsizetype
      {
        for (auto i = MAX_STRING_LENGTH; i > 0; i--)
        {
          if (auto const byte = static_cast<std::byte>(utf.at(i));
                        (byte & std::byte{0xC0}) != std::byte{0x80})
          {
            return i;
          }
        }
        return 0;
      };

      // Truncate at the position found. This will truncate at a codepoint
      // boundary, but it may change the characters in the string, rather
      // than just cutting them off; e.g. it might result in "résumé" being
      // turned into "résume". Never promised you a perfect solution here,
      // just a legal one.

      utf.truncate(truncatePosition());
    }

    out <<         quint8(utf.size());
    out.writeRawData(utf, utf.size());
  }

  // As mentioned above, from the PSK reporter spec, records must be null
  // padded to a multiple of 4 bytes. Given a value representing a buffer
  // length, return the number of additional bytes required to make it an
  // even multiple of 4.

  qsizetype
  num_pad_bytes(qsizetype const n)
  {
    return ((n + 3) & ~0x3) - n;
  }

  // If the buffer isn't landing on a 4-byte boundary, pad with nulls.
  // Rewind the data stream to 2 bytes in, punch in the length of the
  // buffer, and reposition to after the buffer, plus any alignment.

  void
  set_length(QDataStream      & out,
             QByteArray const & b)
  {
    // Pad out to 4-byte alignment with NUL bytes, if necessary.

    if (auto const padSize  = num_pad_bytes(b.size());
                   padSize != 0)
    {
      out.writeRawData(QByteArray(padSize, '\0'), padSize);
    }

    // Remember where we are, then position to punch in the length,
    // which is always after an initial 16-bit field, i.e. after a
    // message header version field or a template set ID field.

    auto const pos = out.device()->pos();
    out.device()->seek(sizeof(quint16));

    // Insert the length, not including any nulls that we might have
    // added, and move back to where we were.

    out << static_cast<quint16>(b.size());
    out.device()->seek(pos);
  }

  // Append a Sender Information Descriptor to the provided message.

  void
  appendSIDTo(QDataStream & message)
  {
      QByteArray  buffer;
      QDataStream stream{&buffer, QIODevice::WriteOnly};

      stream
        << quint16 (2u)           // Template Set ID
        << quint16 (0u)           // Length (place-holder)
        << quint16 (0x50e3)       // Link ID
        << quint16 (7u)           // Field Count
        << quint16 (0x8000 + 1u)  // Option 1 Information Element ID (senderCallsign)
        << quint16 (0xffff)       // Option 1 Field Length (variable)
        << quint32 (30351u)       // Option 1 Enterprise Number
        << quint16 (0x8000 + 5u)  // Option 2 Information Element ID (frequency)
        << quint16 (5u)           // Option 2 Field Length
        << quint32 (30351u)       // Option 2 Enterprise Number
        << quint16 (0x8000 + 6u)  // Option 3 Information Element ID (sNR)
        << quint16 (1u)           // Option 3 Field Length
        << quint32 (30351u)       // Option 3 Enterprise Number
        << quint16 (0x8000 + 10u) // Option 4 Information Element ID (mode)
        << quint16 (0xffff)       // Option 4 Field Length (variable)
        << quint32 (30351u)       // Option 4 Enterprise Number
        << quint16 (0x8000 + 3u)  // Option 5 Information Element ID (senderLocator)
        << quint16 (0xffff)       // Option 5 Field Length (variable)
        << quint32 (30351u)       // Option 5 Enterprise Number
        << quint16 (0x8000 + 11u) // Option 6 Information Element ID (informationSource)
        << quint16 (1u)           // Option 6 Field Length
        << quint32 (30351u)       // Option 6 Enterprise Number
        << quint16 (150u)         // Option 7 Information Element ID (dateTimeSeconds)
        << quint16 (4u);          // Option 7 Field Length

    set_length(stream, buffer);
    message.writeRawData(buffer, buffer.size());
  }

  // Append a Receiver Information Descriptor to the provided message.

  void
  appendRIDTo(QDataStream & message)
  {
    QByteArray  buffer;
    QDataStream stream{&buffer, QIODevice::WriteOnly};

    stream
      << quint16 (3u)          // Options Template Set ID
      << quint16 (0u)          // Length (place-holder)
      << quint16 (0x50e2)      // Link ID
      << quint16 (4u)          // Field Count
      << quint16 (0u)          // Scope Field Count
      << quint16 (0x8000 + 2u) // Option 1 Information Element ID (receiverCallsign)
      << quint16 (0xffff)      // Option 1 Field Length (variable)
      << quint32 (30351u)      // Option 1 Enterprise Number
      << quint16 (0x8000 + 4u) // Option 2 Information Element ID (receiverLocator)
      << quint16 (0xffff)      // Option 2 Field Length (variable)
      << quint32 (30351u)      // Option 2 Enterprise Number
      << quint16 (0x8000 + 8u) // Option 3 Information Element ID (decodingSoftware)
      << quint16 (0xffff)      // Option 3 Field Length (variable)
      << quint32 (30351u)      // Option 3 Enterprise Number
      << quint16 (0x8000 + 9u) // Option 4 Information Element ID (antennaInformation)
      << quint16 (0xffff)      // Option 4 Field Length (variable)
      << quint32 (30351u);     // Option 4 Enterprise Number
  
    set_length(stream, buffer);
    message.writeRawData(buffer, buffer.size());
  }
}

/******************************************************************************/
// Private Implementation
/******************************************************************************/

class PSKReporter::impl final : public QObject
{
  Q_OBJECT

public:

  // POD describing a spot; we queue these for later delivery.

  struct Spot
  {
    QString          call_;
    QString          grid_;
    int              snr_;
    Radio::Frequency freq_;
    QString          mode_;
    QDateTime        time_;
  };

  // Data members

  PSKReporter                   * self_;
  Configuration           const * config_;
  QString                         prog_id_;
  QTimer                          report_timer_;
  QTimer                          descriptor_timer_;
  QVector<QDateTime>              eclipseDates_;
  QSharedPointer<QAbstractSocket> socket_;
  QString                         rx_call_;
  QString                         rx_grid_;
  QString                         rx_ant_;
  QByteArray                      tx_data_;
  QByteArray                      tx_residue_;
  QByteArray                      payload_;
  QQueue<Spot>                    spots_;
  QHash<QString, std::time_t>     calls_;
  quint32                         observation_id_   = QRandomGenerator::global()->generate();
  quint32                         sequence_number_  = 0u;
  unsigned                        send_descriptors_ = 0u;
  unsigned                        flush_counter_    = 0u;
  
  // Constructor

  impl(PSKReporter         * self,
       Configuration const * config,
       QString       const & program_info)
    : self_    {self}
    , config_  {config}
    , prog_id_ {program_info}
  {
    // This timer sets the interval to check for spots to send.
  
    connect(&report_timer_,
            &QTimer::timeout,
            [this]()
    {
      send_report();
    });

    // This timer repeats the sending of IPFIX templates and receiver
    // information if we are using UDP, in case the server has been
    // restarted and lost cached information.

    connect(&descriptor_timer_,
            &QTimer::timeout,
            [this]()
    {
      if (socket_ && QAbstractSocket::UdpSocket == socket_->socketType())
      {
        send_descriptors_ = 3; // Send format descriptors again, 3 times.
      }
    });

    // Attempt to load up the eclipse dates. Not a big deal if this fails;
    // just means that we won't bypass the spot cache during eclipse periods.
  
    if (auto file = QFile(config->data_dir().absoluteFilePath("eclipse.txt"));
             file.open(QIODevice::ReadOnly))
    {
      auto text = QTextStream(&file);

      for (QString line; text.readLineInto(&line);)
      {
        if (line.isEmpty()) continue;
        if (line[0] == '#') continue;

        if (auto const date = QDateTime::fromString(line, Qt::ISODate);
                       date.isValid())
        {
          eclipseDates_.append(date);
        }
      }
    }
  }

  void
  check_connection()
  {
    if (!socket_
        || QAbstractSocket::UnconnectedState == socket_->state ()
        || (socket_->socketType () != (config_->psk_reporter_tcpip () ? QAbstractSocket::TcpSocket : QAbstractSocket::UdpSocket)))
    {
      // we need to create the appropriate socket
      if (socket_
          && QAbstractSocket::UnconnectedState != socket_->state ()
          && QAbstractSocket::ClosingState     != socket_->state ())
      {
        // handle re-opening asynchronously
        auto connection = QSharedPointer<QMetaObject::Connection>::create();
        *connection = connect(socket_.data(),
                              &QAbstractSocket::disconnected,
                              [this, connection]()
                              {
                                disconnect(*connection);
                                check_connection();
                              });

        // close gracefully
        send_report (true);
        socket_->close ();
      }
      else
      {
        reconnect();
      }
    }
  }

  void
  handle_socket_error(QAbstractSocket::SocketError e)
  {
    qWarning() << "[PSK]socket error:" << socket_->errorString();
    switch (e)
    {
      case QAbstractSocket::RemoteHostClosedError:
        socket_->disconnectFromHost();
        break;

      case QAbstractSocket::TemporaryError:
        break;

      default:
        spots_.clear();
        Q_EMIT self_->errorOccurred(socket_->errorString ());
        break;
    }
  }

  void
  reconnect()
  {
    // Using deleteLater for the deleter as we may eventually
    // be called from the disconnected handler above.

    if (config_->psk_reporter_tcpip())
    {
      socket_.reset(new QTcpSocket, &QObject::deleteLater);
      send_descriptors_ = 1;
    }
    else
    {
      socket_.reset(new QUdpSocket, &QObject::deleteLater);
      send_descriptors_ = 3;
    }

    connect(socket_.get(),
            &QAbstractSocket::errorOccurred,
            this,
            &PSKReporter::impl::handle_socket_error);

    // use this for pseudo connection with UDP, allows us to use
    // QIODevice::write() instead of QUDPSocket::writeDatagram()

    socket_->connectToHost(HOST, PORT, QAbstractSocket::WriteOnly);

    qDebug() << "[PSK]server:" << HOST << ':' << PORT;

    if (!report_timer_.isActive())
    {
      report_timer_.start(MIN_SEND_INTERVAL + 1 * 1000); // we add 1 to give some more randomization
    }

    if (!descriptor_timer_.isActive())
    {
      descriptor_timer_.start(1 * 60 * 60 * 1000); // hourly
    }
  }

  void
  stop()
  {
    if (socket_)
    {
      socket_->disconnectFromHost();
    }
    descriptor_timer_.stop();
    report_timer_.stop();
  }

  void
  build_preamble(QDataStream & message)
  {
    // Message Header
    message
      << quint16 (10u)          // Version Number
      << quint16 (0u)           // Length (place-holder filled in later)
      << quint32 (0u)           // Export Time (place-holder filled in later)
      << ++sequence_number_     // Sequence Number
      << observation_id_;       // Observation Domain ID

    // We send the record format descriptors every so often; if we're due to
    // send them again, then append them to the message. Note that while we
    // add these to the message in the order of sender, recipient, the order
    // is documented not to matter to PSKReporter.

    if (send_descriptors_)
    {
      --send_descriptors_;
      appendSIDTo(message);
      appendRIDTo(message);
      qDebug() << "[PSK]sent descriptors";
    }

    // As opposed to the record format descriptors, which can be omitted once
    // they have been transmitted a few times (to ensure that the server has
    // cached them), the receiver information record must be sent every time.

    QByteArray  record;
    QDataStream stream{&record, QIODevice::WriteOnly};

    // Set up the header; we'll fill in the length below, later.

    stream
      << quint16 (0x50e2) // Template ID
      << quint16 (0u);    // Length (place-holder)

    // Stream the data into the record as UTF-8 strings, each one up to 254
    // bytes in length.

    writeUtfString(stream, rx_call_);
    writeUtfString(stream, rx_grid_);
    writeUtfString(stream, prog_id_);
    writeUtfString(stream, rx_ant_);

    // Run back to the length field and update it, if necessary padding out
    // the record to 4-byte alignment with NUL bytes, and append it to the
    // message.

    set_length(stream, record);
    message.writeRawData(record, record.size());
  }

  void
  send_report(bool const send_residue = false)
  {
    if (QAbstractSocket::ConnectedState != socket_->state()) return;

    QDataStream message {&payload_, QIODevice::WriteOnly | QIODevice::Append};
    QDataStream tx_out  {&tx_data_, QIODevice::WriteOnly | QIODevice::Append};

    if (!payload_.size())
    {
      // Build header, optional descriptors, and receiver information
      build_preamble(message);
    }

    auto flush = flushing() || send_residue;
    while (spots_.size() || flush)
    {
      if (!payload_.size())
      {
        // Build header, optional descriptors, and receiver information
        build_preamble (message);
      }

      if (!tx_data_.size () && (spots_.size () || tx_residue_.size ()))
      {
        // Set Header
        tx_out
          << quint16 (0x50e3)     // Template ID
          << quint16 (0u);        // Length (place-holder)
      }

      // insert any residue
      if (tx_residue_.size())
      {
        tx_out.writeRawData(tx_residue_, tx_residue_.size());
        tx_residue_.clear();
      }

      qDebug() << "[PSK]pending spots:" << spots_.size();
      while (spots_.size() || flush)
      {
        auto tx_data_size = tx_data_.size();
        if (spots_.size())
        {
          auto const& spot = spots_.dequeue();

          // Sender information
          writeUtfString(tx_out, spot.call_);
          tx_out // BigEndian
            << static_cast<quint8>(spot.freq_ >> 32)
            << static_cast<quint8>(spot.freq_ >> 24)
            << static_cast<quint8>(spot.freq_ >> 16)
            << static_cast<quint8>(spot.freq_ >>  8)
            << static_cast<quint8>(spot.freq_)
            << static_cast<qint8> (spot.snr_);
          writeUtfString(tx_out, spot.mode_);
          writeUtfString(tx_out, spot.grid_);
          tx_out
            << quint8 (1u)          // REPORTER_SOURCE_AUTOMATIC
            << static_cast<quint32>(spot.time_.toSecsSinceEpoch());
        }

        auto len = payload_.size() + tx_data_.size();
        len     += num_pad_bytes(tx_data_.size());
        len     += num_pad_bytes(len);

        if (len > MAX_PAYLOAD_LENGTH // our upper datagram size limit
            || (!spots_.size() && len > MIN_PAYLOAD_LENGTH) // spots drained and above lower datagram size limit
            || (flush && !spots_.size())) // send what we have, possibly no spots
        {
          if (tx_data_.size())
          {
            if (len <= MAX_PAYLOAD_LENGTH)
            {
              tx_data_size = tx_data_.size();
            }
            QByteArray tx {tx_data_.left (tx_data_size)};
            QDataStream out {&tx, QIODevice::WriteOnly | QIODevice::Append};
            // insert Length
            set_length(out, tx);
            message.writeRawData(tx, tx.size());
          }

          // insert Length and Export Time
          set_length(message, payload_);
          message.device()->seek(2 * sizeof(quint16));
          message << static_cast<quint32>(DriftingDateTime::currentDateTime().toSecsSinceEpoch());

          // Send data to PSK Reporter site
          socket_->write (payload_); // TODO: handle errors
          qDebug() << "[PSK]sent spots";
          flush = false;    // break loop
          message.device()->seek(0u);
          payload_.clear();  // Fresh message
          // Save unsent spots
          tx_residue_ = tx_data_.right(tx_data_.size() - tx_data_size);
          tx_out.device()->seek(0u);
          tx_data_.clear();
          break;
        }
      }
      qDebug() << "[PSK]remaining spots:" << spots_.size();
    }
  }

  bool
  flushing()
  {
    return !(++flush_counter_ % FLUSH_INTERVAL);
  }

  // Check the eclipse dates and see if the provided date falls within a
  // +/- 6 hour window of an eclipse. Given how few items are going to be
  // in the list, there's unlikely to be any data structure that's going
  // to perform better than a vector.

  bool
  eclipse_active(QDateTime const & date) const
  {
    return std::any_of(eclipseDates_.begin(),
                       eclipseDates_.end(),
                      [=](auto const check)
    {
      // +- 6 hour window
      return qAbs(check.secsTo(date)) <= (3600 * 6); // 6 hour check
    });
  }
};

/******************************************************************************/
// Implementation
/******************************************************************************/
  
#include "PSKReporter.moc"

PSKReporter::PSKReporter(Configuration const * config,
                         QString       const & program_info)
  : m_ {this, config, program_info}
{}

PSKReporter::~PSKReporter() = default;

void PSKReporter::reconnect()
{
  m_->reconnect();
}

void
PSKReporter::setLocalStation(QString const & call,
                             QString const & grid,
                             QString const & ant)
{
  m_->check_connection();

  if (call != m_->rx_call_ ||
      grid != m_->rx_grid_ ||
      ant  != m_->rx_ant_)
  {
    m_->rx_call_ = call;
    m_->rx_grid_ = grid;
    m_->rx_ant_  = ant;
  }
}

bool
PSKReporter::addRemoteStation(QString           const & call,
                               QString          const & grid,
                               Radio::Frequency const   freq,
                               QString          const & mode,
                               int              const   snr)
{
  m_->check_connection();

  if (!m_->socket_)            return false;
  if (!m_->socket_->isValid()) return false;

  if (QAbstractSocket::UnconnectedState == m_->socket_->state())
  {
    reconnect();
  }

  // If this call is not already in the cache, or it's there but expired, or the
  // frequency is interesting, or an eclipse is active, (we allow all spots through
  // +/- 6 hours around an eclipse for the HamSCI group) then we're going to send
  // the spot; cache the fact that we've done so, either by adding a new cache
  // entry or updating an existing one with an updated time value.

  if (auto const it  = m_->calls_.find(call);
                 it == m_->calls_.end()         ||
                 it.value() > CACHE_TIMEOUT     ||
                 freq       > CACHE_BYPASS_FREQ ||
                 m_->eclipse_active(DriftingDateTime::currentDateTime().toUTC()))
  {
    m_->spots_.enqueue({call, grid, snr, freq, mode, DriftingDateTime::currentDateTimeUtc()});
    m_->calls_.insert(call, std::time(nullptr));
  }

  // Perform cache cleanup; anything that's been around for more than twice the cache
  // timeout period can go.

  m_->calls_.removeIf([now = std::time(nullptr)](auto const it)
  {
    return now - it.value() > (CACHE_TIMEOUT * 2);
  });

  return true;
}

void PSKReporter::sendReport(bool const last)
{
  m_->check_connection();

  if (m_->socket_ && QAbstractSocket::ConnectedState == m_->socket_->state())
  {
    m_->send_report(true);
  }

  if (last)
  {
    m_->stop();
  }
}

/******************************************************************************/
