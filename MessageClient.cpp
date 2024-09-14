#include "MessageClient.hpp"

#include <stdexcept>
#include <vector>
#include <algorithm>

#include <QApplication>
#include <QUdpSocket>
#include <QHostInfo>
#include <QTimer>
#include <QQueue>
#include <QByteArray>
#include <QHostAddress>
#include <QColor>

#include "DriftingDateTime.h"

#include "pimpl_impl.hpp"

#include "moc_MessageClient.cpp"


class MessageClient::impl
  : public QUdpSocket
{
  Q_OBJECT

public:
  impl (QString const& id, QString const& version, QString const& revision,
        port_type server_port, MessageClient * self)
    : self_ {self}
    , id_ {id}
    , version_ {version}
    , revision_ {revision}
    , server_port_ {server_port}
    , schema_ {2}  // use 2 prior to negotiation not 1 which is broken
    , heartbeat_timer_ {new QTimer {this}}
  {
    connect (heartbeat_timer_, &QTimer::timeout, this, &impl::heartbeat);
    connect (this, &QIODevice::readyRead, this, &impl::pending_datagrams);

    heartbeat_timer_->start (15 * 1000);

    // bind to an ephemeral port
    bind ();
  }

  ~impl ()
  {
    closedown ();
  }

  enum StreamStatus {Fail, Short, OK};

  void parse_message (QByteArray const& msg);
  void pending_datagrams ();
  void heartbeat ();
  void closedown ();
  StreamStatus check_status (QDataStream const&) const;
  void send_message (QByteArray const&);
  void send_message (QDataStream const& out, QByteArray const& message)
  {
      if (OK == check_status (out))
        {
          send_message (message);
        }
      else
        {
          Q_EMIT self_->error ("Error creating UDP message");
        }
  }

  Q_SLOT void host_info_results (QHostInfo);

  MessageClient * self_;
  QString id_;
  QString version_;
  QString revision_;
  QString server_string_;
  port_type server_port_;
  QHostAddress server_;
  quint32 schema_;
  QTimer * heartbeat_timer_;
  std::vector<QHostAddress> blocked_addresses_;

  // hold messages sent before host lookup completes asynchronously
  QQueue<QByteArray> pending_messages_;
  QByteArray last_message_;
};


#include "MessageClient.moc"

void MessageClient::impl::host_info_results (QHostInfo host_info)
{
  if (QHostInfo::NoError != host_info.error ())
    {
      Q_EMIT self_->error ("UDP server lookup failed:\n" + host_info.errorString ());
      pending_messages_.clear (); // discard
    }
  else if (host_info.addresses ().size ())
    {
      auto server = host_info.addresses ()[0];
      if (blocked_addresses_.end () == std::find (blocked_addresses_.begin (), blocked_addresses_.end (), server))
        {
          server_ = server;

          // send initial heartbeat which allows schema negotiation
          heartbeat ();

          // clear any backlog
          while (pending_messages_.size ())
            {
              send_message (pending_messages_.dequeue ());
            }
        }
      else
        {
          Q_EMIT self_->error ("UDP server blocked, please try another");
          pending_messages_.clear (); // discard
        }
    }
}

void MessageClient::impl::pending_datagrams ()
{
  while (hasPendingDatagrams ())
    {
      QByteArray datagram;
      datagram.resize (pendingDatagramSize ());
      QHostAddress sender_address;
      port_type sender_port;
      if (0 <= readDatagram (datagram.data (), datagram.size (), &sender_address, &sender_port))
        {
          parse_message (datagram);
        }
    }
}

void MessageClient::impl::parse_message (QByteArray const& msg)
{
  try
    {
        if(msg.isEmpty()){
            return;
        }

        QJsonParseError e;
        QJsonDocument d = QJsonDocument::fromJson(msg, &e);
        if(e.error != QJsonParseError::NoError){
            Q_EMIT self_->error(QString {"MessageClient json parse error:  %1"}.arg(e.errorString()));
            return;
        }

        if(!d.isObject()){
            Q_EMIT self_->error(QString {"MessageClient json parse error: json is not an object"});
            return;
        }

        Message m;
        m.read(d.object());
        Q_EMIT self_->message(m);

    }
  catch (std::exception const& e)
    {
      Q_EMIT self_->error (QString {"MessageClient exception: %1"}.arg (e.what ()));
    }
  catch (...)
    {
      Q_EMIT self_->error ("Unexpected exception in MessageClient");
    }
}

void MessageClient::impl::heartbeat ()
{
   if (server_port_ && !server_.isNull ())
    {
      Message m("PING", "", QMap<QString, QVariant>{
        {"NAME", QVariant(QApplication::applicationName())},
        {"VERSION", QVariant(QApplication::applicationVersion())},
        {"UTC", QVariant(DriftingDateTime::currentDateTimeUtc().toMSecsSinceEpoch())}
      });
      writeDatagram (m.toJson(), server_, server_port_);
    }
}

void MessageClient::impl::closedown ()
{
   if (server_port_ && !server_.isNull ())
    {
       Message m("CLOSE");
       writeDatagram (m.toJson(), server_, server_port_);
    }
}

void MessageClient::impl::send_message (QByteArray const& message)
{
  if (server_port_)
    {
      if (!server_.isNull ())
        {
          if (message != last_message_) // avoid duplicates
            {
              writeDatagram (message, server_, server_port_);
              last_message_ = message;
            }
        }
      else
        {
          pending_messages_.enqueue (message);
        }
    }
}

auto MessageClient::impl::check_status (QDataStream const& stream) const -> StreamStatus
{
  auto stat = stream.status ();
  StreamStatus result {Fail};
  switch (stat)
    {
    case QDataStream::ReadPastEnd:
      result = Short;
      break;

    case QDataStream::ReadCorruptData:
      Q_EMIT self_->error ("Message serialization error: read corrupt data");
      break;

    case QDataStream::WriteFailed:
      Q_EMIT self_->error ("Message serialization error: write error");
      break;

    default:
      result = OK;
      break;
    }
  return result;
}

MessageClient::MessageClient (QString const& id, QString const& version, QString const& revision,
                              QString const& server, port_type server_port, QObject * self)
  : QObject {self}
  , m_ {id, version, revision, server_port, this}
{
  connect (&*m_, &impl::errorOccurred
           , [this] (impl::SocketError e)
           {
#if defined (Q_OS_WIN) && QT_VERSION >= 0x050500
             if (e != impl::NetworkError // take this out when Qt 5.5
                                         // stops doing this
                                         // spuriously
                 && e != impl::ConnectionRefusedError) // not
                                                       // interested
                                                       // in this with
                                                       // UDP socket
#else
             Q_UNUSED (e);
#endif
               {
                 Q_EMIT error (m_->errorString ());
               }
           });
  set_server (server);
}

QHostAddress MessageClient::server_address () const
{
  return m_->server_;
}

auto MessageClient::server_port () const -> port_type
{
  return m_->server_port_;
}

void MessageClient::set_server (QString const& server)
{
    qDebug() << "server changed to" << server;
  m_->server_.clear ();
  m_->server_string_ = server;
  if (!server.isEmpty ())
    {
      // queue a host address lookup
      QHostInfo::lookupHost (server, &*m_, SLOT (host_info_results (QHostInfo)));
    }
}

void MessageClient::set_server_port (port_type server_port)
{
  m_->server_port_ = server_port;
}

void MessageClient::send(Message const &message){
  m_->send_message(message.toJson());
}

void MessageClient::send_raw_datagram (QByteArray const& message, QHostAddress const& dest_address
                                       , port_type dest_port)
{
  if (dest_port && !dest_address.isNull ())
    {
      m_->writeDatagram (message, dest_address, dest_port);
    }
}

void MessageClient::add_blocked_destination (QHostAddress const& a)
{
  m_->blocked_addresses_.push_back (a);
  if (a == m_->server_)
    {
      m_->server_.clear ();
      Q_EMIT error ("UDP server blocked, please try another");
      m_->pending_messages_.clear (); // discard
    }
}
