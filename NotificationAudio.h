#ifndef NOTIFICATIONAUDIO_H
#define NOTIFICATIONAUDIO_H

#include <QAudioDevice>
#include <QBuffer>
#include <QByteArray>
#include <QHash>
#include <QPair>
#include <QScopedPointer>

class SoundOutput;

class NotificationAudio :
    public QObject
{
    Q_OBJECT

public:
    explicit NotificationAudio(QObject * parent=nullptr);
    ~NotificationAudio();

public slots:
    void status(QString message);
    void error(QString message);
    void setDevice(const QAudioDevice &device, unsigned msBuffer=0);
    void play(const QString &filePath);
    void stop();

private:

    using Entry = QPair<QAudioFormat, QByteArray>;
    using Cache = QHash<QString,      Entry>;

    void playEntry(Cache::const_iterator);

    QScopedPointer<SoundOutput> m_stream;
    Cache                       m_cache;
    QAudioDevice                m_device;
    QBuffer                     m_buffer;
    unsigned                    m_msBuffer;
};

#endif // NOTIFICATIONAUDIO_H
