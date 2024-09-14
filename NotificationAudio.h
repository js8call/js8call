#ifndef NOTIFICATIONAUDIO_H
#define NOTIFICATIONAUDIO_H

#include <QIODevice>
#include <QBuffer>
#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioOutput>
#include <QFile>
#include <QPair>
#include <QPointer>
#include <QByteArray>
#include <QCache>
#include <QScopedPointer>

#include "AudioDevice.hpp"
#include "AudioDecoder.h"
#include "Audio/BWFFile.hpp"
#include "soundout.h"


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
    void playBytes(const QAudioFormat &format, QByteArray *bytes);

private:
    QMap<QString, QPair<QAudioFormat, QByteArray*>> m_cache;
    QPointer<SoundOutput> m_stream;
    QPointer<AudioDecoder> m_decoder;
    QPointer<BWFFile> m_file;
    QAudioDevice m_device;
    QBuffer m_buffer;
    unsigned m_msBuffer;
};

#endif // NOTIFICATIONAUDIO_H
