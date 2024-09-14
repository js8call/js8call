#include "APRSISClient.h"
#include <QRandomGenerator>

#include <cmath>

#include "DriftingDateTime.h"
#include "varicode.h"

const int PACKET_TIMEOUT_SECONDS = 300;

APRSISClient::APRSISClient(QString const host,
                           quint16 const port,
                           QObject     * parent)
  : QTcpSocket{parent},
    m_timer   {this}
{
    setServer(host, port);

    connect(&m_timer, &QTimer::timeout, this, &APRSISClient::sendReports);
    m_timer.setInterval(60*1000); // every 60 seconds
    m_timer.start();
}

quint32 APRSISClient::hashCallsign(QString callsign){
    // based on: https://github.com/hessu/aprsc/blob/master/src/passcode.c
    QByteArray rootCall = QString(callsign.split("-").first().toUpper()).toLocal8Bit() + '\0';
    quint32 hash = 0x73E2;

    int i = 0;
    int len = rootCall.length();

    while(i+1 < len){
        hash ^= rootCall.at(i) << 8;
        hash ^= rootCall.at(i+1);
        i += 2;
    }

    return hash & 0x7FFF;
}

QString APRSISClient::loginFrame(QString callsign){
    auto loginFrame = QString("user %1 pass %2 ver %3\n");
    loginFrame = loginFrame.arg(callsign);
    loginFrame = loginFrame.arg(hashCallsign(callsign));
    loginFrame = loginFrame.arg("JS8Call");
    return loginFrame;
}

QList<QStringList> findall(QRegularExpression re, QString content){
    int pos = 0;

    QList<QStringList> all;
    while(pos < content.length()){
        auto match = re.match(content, pos);
        if(!match.hasMatch()){
            break;
        }

        all.append(match.capturedTexts());
        pos = match.capturedEnd();
    }

    return all;
}


inline long
floordiv (long num, long den)
{
  if (0 < (num^den))
    return num/den;
  else
    {
      ldiv_t res = ldiv(num,den);
      return (res.rem)? res.quot-1
                      : res.quot;
    }
}

// convert an arbitrary length grid locator to a high precision lat/lon
QPair<float, float> APRSISClient::grid2deg(QString locator){
    QString grid = locator.toUpper();

    float lat = -90;
    float lon = -90;

    auto lats = findall(QRegularExpression("([A-X])([A-X])"), grid);
    auto lons = findall(QRegularExpression("(\\d)(\\d)"), grid);

    int valx[22];
    int valy[22];

    int i = 0;
    int tot = 0;
    char A = 'A';
    foreach(QStringList matched, lats){
        char x = matched.at(1).at(0).toLatin1();
        char y = matched.at(2).at(0).toLatin1();

        valx[i*2] = x-A;
        valy[i*2] = y-A;

        i++;
        tot++;
    }

    i = 0;
    foreach(QStringList matched, lons){
        int x = matched.at(1).toInt();
        int y = matched.at(2).toInt();
        valx[i*2+1]=x;
        valy[i*2+1]=y;

        i++;
        tot++;
    }

    for(int i = 0; i < tot; i++){
        int x = valx[i];
        int y = valy[i];

        int z = i - 1;
        float scale = pow(10, floordiv(-(z-1), 2)) * pow(24, floordiv(-z, 2));

        lon += scale * x;
        lat += scale * y;
    }

    lon *= 2;

    return {lat, lon};
}

// convert an arbitrary length grid locator to a high precision lat/lon in aprs format
QPair<QString, QString> APRSISClient::grid2aprs(QString grid){
    auto geo = APRSISClient::grid2deg(grid);
    auto lat = geo.first;
    auto lon = geo.second;

    QString latDir = "N";
    if(lat < 0){
        lat *= -1;
        latDir = "S";
    }

    QString lonDir = "E";
    if(lon < 0){
        lon *= -1;
        lonDir = "W";
    }

    double iLat, fLat, iLon, fLon, iLatMin, fLatMin, iLonMin, fLonMin, iLatSec, iLonSec;
    fLat = modf(lat, &iLat);
    fLon = modf(lon, &iLon);

    fLatMin = modf(fLat * 60, &iLatMin);
    fLonMin = modf(fLon * 60, &iLonMin);

    iLatSec = round(fLatMin * 60);
    iLonSec = round(fLonMin * 60);

    if(iLatSec == 60){
        iLatMin += 1;
        iLatSec = 0;
    }

    if(iLonSec == 60){
        iLonMin += 1;
        iLonSec = 0;
    }

    if(iLatMin == 60){
        iLat += 1;
        iLatMin = 0;
    }

    if(iLonMin == 60){
        iLon += 1;
        iLonMin = 0;
    }

    double aprsLat = iLat * 100 + iLatMin + (iLatSec / 60.0);
    double aprsLon = iLon * 100 + iLonMin + (iLonSec / 60.0);

    return {
        QString("%1%2").arg(aprsLat, 7, 'f', 2, QChar('0')).arg(latDir),
        QString("%1%2").arg(aprsLon, 8, 'f', 2, QChar('0')).arg(lonDir)
    };
}

QString APRSISClient::stripSSID(QString call){
    return QString(call.split("-").first().toUpper());
}

QString APRSISClient::replaceCallsignSuffixWithSSID(QString call, QString base){
    if(call != base){
        QRegularExpression re("[/](?<ssid>(\\d+))");
        auto matcher = re.globalMatch(call);
        if(matcher.hasNext()){
            auto match = matcher.next();
            auto ssid = match.captured("ssid");
            call = base + "-" + ssid;
        } else {
            call = base;
        }
    }
    return call;
}

void APRSISClient::enqueueSpot(QString by_call, QString from_call, QString grid, QString comment){
    if(!isPasscodeValid()){
        return;
    }

    auto geo = APRSISClient::grid2aprs(grid);
    auto spotFrame = QString("%1>APJ8CL,qAS,%2:=%3/%4G#JS8 %5\n");
    spotFrame = spotFrame.arg(from_call);
    spotFrame = spotFrame.arg(by_call);
    spotFrame = spotFrame.arg(geo.first);
    spotFrame = spotFrame.arg(geo.second);
    spotFrame = spotFrame.arg(comment.left(42));
    enqueueRaw(spotFrame);
}

void APRSISClient::enqueueThirdParty(QString by_call, QString from_call, QString text){
    if(!isPasscodeValid()){
        return;
    }

    auto frame = QString("%1>APJ8CL,qAS,%2:%3\n");
    frame = frame.arg(from_call);
    frame = frame.arg(by_call);
    frame = frame.arg(text);

    enqueueRaw(frame);
}

void APRSISClient::enqueueRaw(QString aprsFrame){
    m_frameQueue.enqueue({ aprsFrame, DriftingDateTime::currentDateTimeUtc() });
}

void APRSISClient::processQueue(bool disconnect){
    // don't process queue if we haven't set our local callsign
    if(m_localCall.isEmpty()) return;

    // don't process queue if there's nothing to process
    if(m_frameQueue.isEmpty()) return;

    // don't process queue if there's no host
    if(m_host.isEmpty() || m_port == 0){
        // no host, so let's clear the queue and exit
        m_frameQueue.clear();
        return;
    }

    // 1. connect (and read)
    // 2. login (and read)
    // 3. for each raw frame in queue, send
    // 4. disconnect

    if(state() != QTcpSocket::ConnectedState){
        qDebug() << "APRSISClient Connecting:" << m_host << m_port;
        connectToHost(m_host, m_port);
        if(!waitForConnected(5000)){
            qDebug() << "APRSISClient Connection Error:" << errorString();
            return;
        }
    }

    auto re = QRegularExpression("(full|unavailable|busy)");
    auto line = QString(readLine());
    if(line.toLower().indexOf(re) >= 0){
        qDebug() << "APRSISClient Connection Busy:" << line;
        return;
    }

    if(write(loginFrame(m_localCall).toLocal8Bit()) == -1){
        qDebug() << "APRSISClient Write Login Error:" << errorString();
        return;
    }

    if(!waitForReadyRead(5000)){
        qDebug() << "APRSISClient Login Error: Server Not Responding";
        return;
    }

    line = QString(readAll());
    if(line.toLower().indexOf(re) >= 0){
        qDebug() << "APRSISClient Server Busy:" << line;
        return;
    }

    QQueue<QPair<QString, QDateTime>> delayed;

    while(!m_frameQueue.isEmpty()){
        auto pair = m_frameQueue.head();
        auto frame = pair.first;
        auto timestamp = pair.second;

        // if the packet is older than the timeout, drop it.
        if(timestamp.secsTo(DriftingDateTime::currentDateTimeUtc()) > PACKET_TIMEOUT_SECONDS){
            qDebug() << "APRSISClient Packet Timeout:" << frame;
            m_frameQueue.dequeue();
            continue;
        }

        // random delay 25% of the time for throttling (a skip will add 60 seconds to the processing time)
        if(m_skipPercent > 0 && QRandomGenerator::global()->generate() % 100 <= (m_skipPercent*100)){
            qDebug() << "APRSISClient Throttle: Skipping Frame";
            delayed.enqueue(m_frameQueue.dequeue());
            continue;
        }

        QByteArray data = frame.toLocal8Bit();
        if(write(data) == -1){
            qDebug() << "APRSISClient Write Error:" << errorString();
            return;
        }

        qDebug() << "APRSISClient Write:" << data;
        if(waitForReadyRead(5000)){
            line = QString(readLine());

            qDebug() << "APRSISClient Read:" << line;

            if(line.toLower().indexOf(re) >= 0){
                qDebug() << "APRSISClient Cannot Write Error:" << line;
                return;
            }
        }

        m_frameQueue.dequeue();
    }

    // enqueue the delayed frames for later processing
    while(!delayed.isEmpty()){
        m_frameQueue.enqueue(delayed.dequeue());
    }

    if(disconnect){
        disconnectFromHost();
    }
}
