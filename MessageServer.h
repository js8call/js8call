#ifndef MESSAGESERVER_H
#define MESSAGESERVER_H

#include <QTcpServer>
#include <QTcpSocket>
#include <QAbstractSocket>
#include <QScopedPointer>
#include <QList>

#include "Message.hpp"

class Client;

class MessageServer : public QTcpServer
{
    Q_OBJECT
public:
    explicit MessageServer(QObject *parent = 0);
    virtual ~MessageServer();

protected:
    int activeConnections();
    void pruneConnections();
    void incomingConnection(qintptr handle);

signals:
    void message(Message const &message);
    void error (QString const&) const;

public slots:
    void setServer(QString host, quint16 port=2442);
    void setPause(bool paused);
    bool start();
    void stop();
    void setMaxConnections(int n);
    void setServerHost(const QString &host){ setServer(host, m_port); }
    void setServerPort(quint16 port){ setServer(m_host, port); }
    void send(Message const &message);

private:
    bool m_paused;
    QString m_host;
    quint16 m_port;
    int m_maxConnections;

    QList<Client*> m_clients;
};

class Client : public QObject
{
    Q_OBJECT
public:
    explicit Client(MessageServer *server, QObject *parent = 0);

    bool isConnected() const { return m_connected; }
    void setSocket(qintptr handle);
    void send(const Message &message);
    void close();
    bool awaitingResponse(int id){
        return id <= 0 || m_requests.contains(id);
    }
signals:

public slots:
    void setConnected(bool connected);
    void onDisconnected();
    void readyRead();

private:
    QMap<int, Message> m_requests;
    MessageServer * m_server;
    QTcpSocket * m_socket;
    bool m_connected;
};



#endif // MESSAGESERVER_H
