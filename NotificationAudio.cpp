#include "NotificationAudio.h"


NotificationAudio::NotificationAudio(QObject *parent):
    QObject(parent)
{
    m_file = new BWFFile(QAudioFormat{}, this);

    m_stream = new SoundOutput();

    connect(m_stream, &SoundOutput::status, this, &NotificationAudio::status);
    connect(m_stream, &SoundOutput::error, this, &NotificationAudio::error);
}

NotificationAudio::~NotificationAudio(){
    // stop the audio
    stop();

    // clean cache
    foreach(auto pair, m_cache){
        delete pair.second;
    }
}

void NotificationAudio::status(QString message){
    if(message == "Idle"){
        stop();
    }
}

void NotificationAudio::error(QString message){
    qDebug() << "notification error:" << message;
}

void NotificationAudio::setDevice(const QAudioDevice &device, unsigned msBuffer){
    m_device   = device;
    m_msBuffer = msBuffer;
    m_stream->setFormat(device, 2, msBuffer);
}

void NotificationAudio::play(const QString &filePath){
    if(m_cache.contains(filePath)){
        auto pair = m_cache.value(filePath);
        auto format = pair.first;
        auto bytes = pair.second;

        qDebug() << "notification: playing" << filePath << "with format" << format << "from cache";
        playBytes(format, bytes);
        return;
    }

    if(m_file->isOpen()){
        m_file->close();
    }
    m_file->setFileName(filePath);
    if(m_file->open(BWFFile::ReadOnly)){
        QAudioFormat format = m_file->format();
        QByteArray *bytes = new QByteArray(m_file->readAll());

        qDebug() << "notification: playing" << filePath << "with format" << format << "from disk";
        playBytes(format, bytes);

        // cache the buffer
        m_cache.insert(filePath, {format, bytes});
    }
}

void NotificationAudio::playBytes(const QAudioFormat &format, QByteArray *bytes){
    if(bytes == nullptr){
        return;
    }

    if(m_buffer.isOpen()){
        m_buffer.close();
    }

    m_buffer.setBuffer(bytes);

    if(!m_buffer.open(QIODevice::ReadOnly)){
        return;
    }

    m_stream->setDeviceFormat(m_device, format, m_msBuffer);
    m_stream->restart(&m_buffer);
}

void NotificationAudio::stop(){
    m_stream->stop();

    if(m_file->isOpen()){
        m_file->close();
    }
}
