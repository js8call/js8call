#ifndef APRSISCLIENT_H
#define APRSISCLIENT_H

#include <QLoggingCategory>
#include <QtGlobal>
#include <QDateTime>
#include <QTcpSocket>
#include <QQueue>
#include <QPair>
#include <QTimer>

Q_DECLARE_LOGGING_CATEGORY(aprsisclient_js8)

class APRSISClient : public QTcpSocket
{
public:
    APRSISClient(QString host, quint16 port, QObject *parent = nullptr);

    static quint32 hashCallsign(QString callsign);
    static QString loginFrame(QString callsign);
    static QPair<float, float> grid2deg(QString grid);
    static QPair<QString, QString> grid2aprs(QString grid);
    static QString stripSSID(QString call);
    static QString replaceCallsignSuffixWithSSID(QString call, QString base);

    bool isPasscodeValid(){ return m_localPasscode == QString::number(hashCallsign(m_localCall)); }

    void enqueueRaw(QString aprsFrame);
    void processQueue(bool disconnect=true);

public slots:

    void setSkipPercent(float skipPercent){
        m_skipPercent = skipPercent;
    }

    void setServer(QString host, quint16 port){
        if(state() == QTcpSocket::ConnectedState){
            disconnectFromHost();
        }

        m_host = host;
        m_port = port;

        qCDebug(aprsisclient_js8) << "APRSISClient Server Change:" << m_host << m_port;
    }

    void setPaused(bool paused){
        m_paused = paused;
    }

    void setLocalStation(QString mycall, QString passcode){
        m_localCall = mycall;
        m_localPasscode = passcode;
    }

    void enqueueSpot(QString by_call, QString from_call, QString grid, QString comment);
    void enqueueThirdParty(QString by_call, QString from_call, QString text);

    void sendReports(){
        if(m_paused) return;

        processQueue(true);
    }

private:
    QString m_localCall;
    QString m_localPasscode;

    QQueue<QPair<QString, QDateTime>> m_frameQueue;
    QString m_host;
    quint16 m_port;
    QTimer m_timer;
    bool m_paused;
    float m_skipPercent;
};

#endif // APRSISCLIENT_H
