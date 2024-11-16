#include "AudioDecoder.h"
#include <QUrl>

AudioDecoder::AudioDecoder(QObject *parent) :
    QIODevice(parent),
    m_state { AudioDecoder::Stopped },
    m_input { &m_data, this },
    m_output { &m_data, this },
    m_init { false },
    m_isDecodingFinished { false }
{
    setOpenMode(QIODevice::ReadOnly);

    m_decoder = new QAudioDecoder(this);
    connect(m_decoder, &QAudioDecoder::bufferReady, this, &AudioDecoder::bufferReady);
    connect(m_decoder, static_cast<void(QAudioDecoder::*)(QAudioDecoder::Error)>(&QAudioDecoder::error), this, &AudioDecoder::errored);
    connect(m_decoder, &QAudioDecoder::finished, this, &AudioDecoder::finished);
}

AudioDecoder::~AudioDecoder(){
    stop();
}

// initialize an audio device
void AudioDecoder::init(const QAudioFormat &format) {
    m_decoder->setAudioFormat(format);

    if (!m_output.open(QIODevice::ReadOnly) || !m_input.open(QIODevice::WriteOnly)){
        m_init = false;
        return;
    }

    m_init = true;
    emit initialized();
}

// play an audio file
void AudioDecoder::start(const QString &filePath){
    if(!m_init){
        return;
    }

    if(m_state == AudioDecoder::Decoding){
        return;
    }

    m_state = AudioDecoder::Decoding;
    m_decoder->setSource(QUrl::fromLocalFile(filePath));
    m_decoder->start();
}

// Stop playing audio
void AudioDecoder::stop() {
    m_state = AudioDecoder::Stopped;
    m_decoder->stop();
    m_data.clear();
    m_isDecodingFinished = false;
}


// io device, read into buffer.
qint64 AudioDecoder::readData(char* data, qint64 maxlen) {
    memset(data, 0, maxlen);

    if (m_state == AudioDecoder::Decoding){
        m_output.read(data, maxlen);

        // Emulate QAudioProbe behaviour for audio data that is sent to output device
        if (maxlen > 0){
            QByteArray buff(data, maxlen);
            emit newData(buff);
        }

        // Is finish of file
        if (atEnd()){
            stop();
        }
    }

    return maxlen;
}

// io device, unused.
qint64 AudioDecoder::writeData(const char* data, qint64 len) {
    Q_UNUSED(data);
    Q_UNUSED(len);

    return 0;
}

// io device, at end of device
bool AudioDecoder::atEnd() const {
    bool value = m_output.size()
        && m_output.atEnd()
        && m_isDecodingFinished;
    return value;
}

// handle buffered data ready
void AudioDecoder::bufferReady() {
    const QAudioBuffer &buffer = m_decoder->read();
    if(!buffer.isValid()){
        return;
    }

    const int length = buffer.byteCount();
    const char *data = buffer.constData<char>();

    m_input.write(data, length);
}

// handle buffered data decoding is finished
void AudioDecoder::finished() {
    m_isDecodingFinished = true;
}

// handle buffered data decoding error
void AudioDecoder::errored(QAudioDecoder::Error /*error*/) {
    stop();
}
