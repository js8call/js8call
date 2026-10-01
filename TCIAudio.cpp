#include "TCIAudio.hpp"

#include <algorithm>

#include <QMutexLocker>

#include "moc_TCIAudio.cpp"

TCIAudio::TCIAudio () = default;

TCIAudio * TCIAudio::instance ()
{
  static TCIAudio * hub = new TCIAudio;
  return hub;
}

void TCIAudio::setEnabled (bool on)
{
  enabled_.store (on, std::memory_order_release);
  if (!on) clearTx ();
}

bool TCIAudio::enabled () const
{
  return enabled_.load (std::memory_order_acquire);
}

void TCIAudio::pushTx (qint16 const * samples, int frames)
{
  if (!samples || frames <= 0) return;
  QMutexLocker lock (&txMutex_);
  for (int i = 0; i < frames; ++i)
    {
      if (fill_ == cap_)
        {
          // Radio is behind. Drop the oldest sample so latency stays bounded.
          read_ = (read_ + 1) % cap_;
          --fill_;
        }
      buf_[(read_ + fill_) % cap_] = samples[i];
      ++fill_;
    }
}

void TCIAudio::clearTx ()
{
  QMutexLocker lock (&txMutex_);
  read_ = 0;
  fill_ = 0;
}

int TCIAudio::pullTx (qint16 * samples, int frames)
{
  if (!samples || frames <= 0) return 0;
  QMutexLocker lock (&txMutex_);
  int const got = std::min (frames, fill_);
  for (int i = 0; i < frames; ++i)
    {
      if (i < got)
        {
          samples[i] = buf_[read_];
          read_ = (read_ + 1) % cap_;
          --fill_;
        }
      else
        {
          samples[i] = 0;
        }
    }
  return got;
}

void TCIAudio::submitRx (qint16 const * samples, int frames)
{
  if (!enabled_.load (std::memory_order_acquire) || !samples || frames <= 0) return;
  QByteArray pcm (reinterpret_cast<char const *> (samples), frames * int (sizeof (qint16)));
  Q_EMIT rxPcm (pcm);
}
