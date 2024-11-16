#ifndef MESSAGE_CLIENT_HPP__
#define MESSAGE_CLIENT_HPP__

#include <QByteArray>
#include <QHostAddress>
#include <QObject>
#include <QString>
#include "Message.hpp"
#include "Radio.hpp"
#include "pimpl_h.hpp"

//
// MessageClient - Manage messages sent and replies received from a
//                 matching server (MessageServer) at the other end of
//                 the wire
//
//
// Each outgoing message type is a Qt slot
//
class MessageClient
  : public QObject
{
  Q_OBJECT

public:

  // instantiate and initiate a host lookup on the server;
  // messages will be queued until a server host lookup is complete
  MessageClient (QString const & server_name,
                 quint16         server_port,
                 QObject       * parent = nullptr);

  // query server details
  QHostAddress server_host() const;
  quint16      server_port() const;

  // initiate a new server host lookup or is the server name is empty
  // the sending of messages is disabled
  Q_SLOT void set_server_name (QString const& server_name = {});

  // change the server port messages are sent to
  Q_SLOT void set_server_port (quint16 server_port = 0u);

  // this slot is used to send an arbitrary message
  Q_SLOT void send (Message const &message);

  // this signal is emitted when a message is received
  Q_SIGNAL void message (Message const &message);

  // this signal is emitted when network errors occur or if a host
  // lookup fails
  Q_SIGNAL void error (QString const&) const;

private:
  class impl;
  pimpl<impl> m_;
};

#endif
