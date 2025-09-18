#include "AudioDevice.hpp"

bool AudioDevice::initialize (OpenMode mode, Channel channel)
{
  m_channel = channel;

  // open and ensure we are unbuffered if possible
  return QIODevice::open (mode | QIODevice::Unbuffered);
}

