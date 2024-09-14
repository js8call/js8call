#ifndef VARICODE_H
#define VARICODE_H

/**
 * (C) 2018 Jordan Sherer <kn4crd@gmail.com> - All Rights Reserved
 **/

#include <QBitArray>
#include <QRegularExpression>
#include <QString>
#include <QVector>
#include <QThread>


class Varicode
{
public:
    // extra information out of buildMessageFrames
    struct MessageInfo {
        QString dirTo;
        QString dirCmd;
        QString dirNum;
    };

    // submode types
    enum SubmodeType {
        JS8CallNormal    = 0,
        JS8CallFast      = 1,
        JS8CallTurbo     = 2,
        JS8CallSlow      = 4,
        JS8CallUltra     = 8
    };

    // frame type transmitted via itype and decoded by the ft8 decoded
    enum TransmissionType {
        JS8Call          = 0, // [000] <- any other frame of the message
        JS8CallFirst     = 1, // [001] <- the first frame of a message
        JS8CallLast      = 2, // [010] <- the last frame of a message
        JS8CallData      = 4, // [100] <- flagged frame (no frame type header)
    };

    /*

    000 = heartbeat
    001 = compound
    010 = compound directed
    011 = directed
    1XX = data, with the X lsb bits dropped
    */
    enum FrameType {
        FrameUnknown          = 255, // [11111111] <- only used as a sentinel
        FrameHeartbeat        = 0,   // [000]
        FrameCompound         = 1,   // [001]
        FrameCompoundDirected = 2,   // [010]
        FrameDirected         = 3,   // [011]
        FrameData             = 4,   // [10X] // but this only encodes the first 2 msb bits and drops the lsb
        FrameDataCompressed   = 6,   // [11X] // but this only encodes the first 2 msb bits and drops the lsb
    };

    static const quint8 FrameTypeMax = 6;

    static QString frameTypeString(quint8 type) {
        const char* FrameTypeStrings[] = {
            "FrameHeartbeat",
            "FrameCompound",
            "FrameCompoundDirected",
            "FrameDirected",
            "FrameData",
            "FrameUnknown", // 5
            "FrameDataCompressed",
        };

        if(type > FrameTypeMax){
            return "FrameUnknown";
        }
        return FrameTypeStrings[type];
    }

    //Varicode();

    static QString extendedChars();

    static QString escape(const QString &text);
    static QString unescape(const QString &text);

    static QString rstrip(const QString& str);
    static QString lstrip(const QString& str);

    static QMap<QString, QString> defaultHuffTable();
    static QString cqString(int number);
    static QString hbString(int number);
    static bool startsWithCQ(QString text);
    static bool startsWithHB(QString text);
    static QString formatSNR(int snr);
    static QString formatPWR(int dbm);

    static QString checksum16(QString const &input);
    static bool checksum16Valid(QString const &checksum, QString const &input);

    static QString checksum32(QString const &input);
    static bool checksum32Valid(QString const &checksum, QString const &input);

    static QStringList parseCallsigns(QString const &input);
    static QStringList parseGrids(QString const &input);

    static QList<QPair<int, QVector<bool>>> huffEncode(const QMap<QString, QString> &huff, QString const& text);
    static QString huffDecode(const QMap<QString, QString> &huff, QVector<bool> const& bitvec);
    static QSet<QString> huffValidChars(const QMap<QString, QString> &huff);

    static QVector<bool> bytesToBits(char * bitvec, int n);
    static QVector<bool> strToBits(QString const& bitvec);
    static QString bitsToStr(QVector<bool> const& bitvec);

    static QVector<bool> intToBits(quint64 value, int expected=0);
    static quint64 bitsToInt(QVector<bool> const value);
    static quint64 bitsToInt(QVector<bool>::ConstIterator start, int n);
    static QVector<bool> bitsListToBits(QList<QVector<bool>> &list);

    static quint8 unpack5bits(QString const& value);
    static QString pack5bits(quint8 packed);

    static quint8 unpack6bits(QString const& value);
    static QString pack6bits(quint8 packed);

    static quint16 unpack16bits(QString const& value);
    static QString pack16bits(quint16 packed);

    static quint32 unpack32bits(QString const& value);
    static QString pack32bits(quint32 packed);

    static quint64 unpack64bits(QString const& value);
    static QString pack64bits(quint64 packed);

    static quint64 unpack72bits(QString const& value, quint8 *pRem);
    static QString pack72bits(quint64 value, quint8 rem);

    static quint32 packAlphaNumeric22(QString const& value, bool isFlag);
    static QString unpackAlphaNumeric22(quint32 packed, bool *isFlag);

    static quint64 packAlphaNumeric50(QString const& value);
    static QString unpackAlphaNumeric50(quint64 packed);

    static quint32 packCallsign(QString const& value, bool *pPortable);
    static QString unpackCallsign(quint32 value, bool portable);

    static QString deg2grid(float dlong, float dlat);
    static QPair<float, float> grid2deg(QString const &grid);
    static quint16 packGrid(QString const& value);
    static QString unpackGrid(quint16 value);

    static quint8 packNum(QString const &num, bool *ok);
    static quint8 packPwr(QString const &pwr, bool *ok);
    static quint8 packCmd(quint8 cmd, quint8 num, bool *pPackedNum);
    static quint8 unpackCmd(quint8 value, quint8 *pNum);

    static bool isSNRCommand(const QString &cmd);
    static bool isCommandAllowed(const QString &cmd);
    static bool isCommandBuffered(const QString &cmd);
    static int isCommandChecksumed(const QString &cmd);
    static bool isCommandAutoreply(const QString &cmd);
    static bool isValidCallsign(const QString &callsign, bool *pIsCompound);
    static bool isCompoundCallsign(const QString &callsign);
    static bool isGroupAllowed(const QString &group);

    static QString packHeartbeatMessage(QString const &text, QString const&callsign, int *n);
    static QStringList unpackHeartbeatMessage(const QString &text, quint8 *pType, bool *isAlt, quint8 *pBits3);

    static QString packCompoundMessage(QString const &text, int *n);
    static QStringList unpackCompoundMessage(const QString &text, quint8 *pType, quint8 *pBits3);

    static QString packCompoundFrame(const QString &callsign, quint8 type, quint16 num, quint8 bits3);
    static QStringList unpackCompoundFrame(const QString &text, quint8 *pType, quint16 *pNum, quint8 *pBits3);

    static QString packDirectedMessage(QString const& text, QString const& mycall, QString *pTo, bool *pToCompound, QString * pCmd, QString *pNum, int *n);
    static QStringList unpackDirectedMessage(QString const& text, quint8 *pType);

    static QString packDataMessage(QString const& text, int *n);
    static QString unpackDataMessage(QString const& text);

    static QString packFastDataMessage(QString const& text, int *n);
    static QString unpackFastDataMessage(QString const& text);

    static QList<QPair<QString, int>> buildMessageFrames(QString const& mycall,
        QString const& mygrid,
        QString const& selectedCall,
        QString const& text,
        bool forceIdentify,
        bool forceData,
        int submode,
        MessageInfo *pInfo=nullptr);
};


class BuildMessageFramesThread : public QThread
{
    Q_OBJECT
public:
    BuildMessageFramesThread(QString const& mycall,
                             QString const& mygrid,
                             QString const& selectedCall,
                             QString const& text,
                             bool forceIdentify,
                             bool forceData,
                             int submode,
                             QObject *parent=nullptr);
    void run() override;
signals:
    void resultReady(QString, int);

private:
    QString m_mycall;
    QString m_mygrid;
    QString m_selectedCall;
    QString m_text;
    bool m_forceIdentify;
    bool m_forceData;
    int m_submode;
};

#endif // VARICODE_H
