#include "MessageServer.h"
#include <stdexcept>
#include <QDebug>


MessageServer::MessageServer(QObject *parent) :
    QTcpServer(parent)
{
}

MessageServer::~MessageServer(){
    stop();
}

bool MessageServer::start()
{
    if(isListening()){
        qDebug() << "MessageServer already listening:" << m_host << m_port;
        return false;
    }

    auto address = QHostAddress();
    if(m_host.isEmpty() || !address.setAddress(m_host)){
        qDebug() << "MessageServer address invalid:" << m_host << m_port;
        return false;
    }

    if(m_port <= 0){
        qDebug() << "MessageServer port invalid:" << m_host << m_port;
        return false;
    }

    bool listening = listen(address, m_port);
    qDebug() << "MessageServer listening:" << listening << m_host << m_port;

    return listening;
}

void MessageServer::stop()
{
    // disconnect all clients
    foreach(auto client, m_clients){
        client->close();
    }

    // then close the server
    close();
}

void MessageServer::setServer(QString host, quint16 port){
    bool listening = isListening();
    if(listening && (m_host != host || m_port != port)){
        stop();
    }

    m_host = host;
    m_port = port;

    if(listening){
        start();
    }
}

void MessageServer::setPause(bool paused)
{
    m_paused = paused;

    if(paused){
        pauseAccepting();
    } else {
        resumeAccepting();
    }
}

void MessageServer::setMaxConnections(int n){
    // set the maximum number of connections allowed
    m_maxConnections = n;

    // then, prune old ones greater than the max (fifo)
    pruneConnections();
}

int MessageServer::activeConnections(){
   int i = 0;
   foreach(auto client, m_clients){
       if(client->isConnected()) i++;
   }
   return i;
}

void MessageServer::pruneConnections(){
    // keep only the n most recent connections (fifo)
    if(m_maxConnections && m_maxConnections < activeConnections()){
        for(int i = m_maxConnections; i < activeConnections(); i++){
            auto client = m_clients.first();
            client->close();
            m_clients.removeFirst();
        }
    }
}

void MessageServer::send(const Message &message){
    foreach(auto client, m_clients){
        if(!client->awaitingResponse(message.id())){
            continue;
        }
        client->send(message);
    }
}

void MessageServer::incomingConnection(qintptr handle)
{
    qDebug() << "MessageServer incomingConnection" << handle;

    auto client = new Client(this, this);
    client->setSocket(handle);

#if JS8_MESSAGESERVER_IS_SINGLE_CLIENT
    while(!m_clients.isEmpty()){
        auto client = m_clients.first();
        client->close();
        m_clients.removeFirst();
    }
#endif

    if(m_maxConnections && m_maxConnections <= activeConnections()){
        qDebug() << "MessageServer connections full, dropping incoming connection";
        client->send(Message("API.ERROR", "Connections Full"));
        client->close();
        return;
    }

    m_clients.append(client);
}

Client::Client(MessageServer * server, QObject *parent):
    QObject(parent),
    m_server {server}
{
    setConnected(true);
}

void Client::setSocket(qintptr handle){
    m_socket = new QTcpSocket(this);

    connect(m_socket, &QTcpSocket::disconnected, this, &Client::onDisconnected);
    connect(m_socket, &QTcpSocket::readyRead, this, &Client::readyRead);

    m_socket->setSocketDescriptor(handle);
}

void Client::setConnected(bool connected){
    m_connected = connected;
}

void Client::close(){
    if(!m_socket){
        return;
    }

    m_socket->close();
    m_socket = nullptr;
}

void Client::send(const Message &message){
    if(!isConnected()){
        return;
    }

    if(!m_socket){
        return;
    }

    if(!m_socket->isOpen()){
        qDebug() << "client socket isn't open";
        return;
    }

    qDebug() << "client writing" << message.toJson();
    m_socket->write(message.toJson());
    m_socket->write("\n");
    m_socket->flush();

    // remove if needed
    if(m_requests.contains(message.id())){
        m_requests.remove(message.id());
    }
}

void Client::onDisconnected(){
    qDebug() << "MessageServer client disconnected";
    setConnected(false);
}

void Client::readyRead(){
    qDebug() << "MessageServer client readyRead";

    while(m_socket->canReadLine())
    {
        auto const msg = m_socket->readLine().trimmed();
        qDebug() << "-> Client" << m_socket->socketDescriptor() << msg;

        if (msg.isEmpty()) return;

        try
        {
            auto m = Message::fromJson(msg);
            m_requests[m.ensureId()] = m;
            emit m_server->message(m);
        }
        catch (std::exception const & e)
        {
            send({"API.ERROR", e.what()});
            return;
        }
    }
}
