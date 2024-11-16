#include "NotificationAudio.h"
#include <QDebug>
#include "Audio/BWFFile.hpp"
#include "soundout.h"

/******************************************************************************/
// Public Implementation
/******************************************************************************/

NotificationAudio::NotificationAudio(QObject * parent)
    : QObject  {parent}
    , m_stream {new SoundOutput}
{
    connect(m_stream.data(), &SoundOutput::status, this, &NotificationAudio::status);
    connect(m_stream.data(), &SoundOutput::error,  this, &NotificationAudio::error);
}

NotificationAudio::~NotificationAudio()
{
    stop();
}

void NotificationAudio::status(QString const message)
{
    if (message == "Idle") stop();
}

void NotificationAudio::error(QString const message)
{
    qDebug() << "notification error:" << message;
}

void
NotificationAudio::setDevice(QAudioDevice const & device,
                             unsigned     const   msBuffer)
{
    m_device   = device;
    m_msBuffer = msBuffer;
}

void
NotificationAudio::play(QString const & filePath)
{
    if (auto const it  = m_cache.constFind(filePath);
                   it != m_cache.constEnd())
    {
        playEntry(it);
    }
    else if (auto file = BWFFile(QAudioFormat{}, filePath);
                  file.open(QIODevice::ReadOnly))
    {
        if (auto data = file.readAll();
                !data.isEmpty())
        {
            playEntry(m_cache.emplace(filePath, file.format(), data));
        }
    }
}

void
NotificationAudio::stop()
{
    m_stream->stop();
}

/******************************************************************************/
// Private Implementation
/******************************************************************************/

void
NotificationAudio::playEntry(Cache::const_iterator const it)
{
    if (m_buffer.isOpen()) m_buffer.close();

    auto const & [format, data] = *it;

    m_buffer.setData(data);

    if (m_buffer.open(QIODevice::ReadOnly))
    {
        m_stream->setDeviceFormat(m_device, format, m_msBuffer);
        m_stream->restart(&m_buffer);
    }
}

/******************************************************************************/
