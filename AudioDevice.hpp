#ifndef AUDIODEVICE_HPP__
#define AUDIODEVICE_HPP__

#include <QIODevice>

class QDataStream;

//
// abstract base class for audio devices
//
class AudioDevice : public QIODevice
{
public:
  enum Channel {Mono, Left, Right, Both}; // these are mapped to combobox index so don't change

  static char const * toString (Channel c)
  {
    switch (c)
    {
      case Mono:  return "Mono";
      case Left:  return "Left";
      case Right: return "Right";
      default:    return "Both";
    }
  }

  static Channel fromString (QString const& str)
  {
    QString const s (str.toCaseFolded ().trimmed ().toLatin1 ());

    if      (s == "both")  return Both;
    else if (s == "right") return Right;
    else if (s == "left")  return Left;
    else                   return Mono;
  }

  bool initialize (OpenMode mode, Channel channel);

  bool isSequential () const override {return true;}

  size_t bytesPerFrame () const {return sizeof (qint16) * (Mono == m_channel ? 1 : 2);}

  Channel channel () const {return m_channel;}

protected:
  explicit AudioDevice (QObject * parent = nullptr)
    : QIODevice (parent)
  {
  }

  void store (char const * source, size_t numFrames, qint16 * dest)
  {
    qint16 const * begin (reinterpret_cast<qint16 const *> (source));
    for ( qint16 const * i = begin; i != begin + numFrames * (bytesPerFrame () / sizeof (qint16)); i += bytesPerFrame () / sizeof (qint16))
      {
	switch (m_channel)
	  {
	  case Mono:
	    *dest++ = *i;
	    break;

	  case Right:
	    *dest++ = *(i + 1);
	    break;

	  case Both:		// should be able to happen but if it
				// does we'll take left
	    Q_ASSERT (Both == m_channel);
      [[fallthrough]];
	  case Left:
	    *dest++ = *i;
	    break;
	  }
      }
  }

  qint16 * load (qint16 const sample, qint16 * dest)
  {
    switch (m_channel)
      {
      case Mono:
	*dest++ = sample;
	break;

      case Left:
	*dest++ = sample;
	*dest++ = 0;
	break;

      case Right:
	*dest++ = 0;
	*dest++ = sample;
	break;

      case Both:
	*dest++ = sample;
	*dest++ = sample;
	break;
      }
    return dest;
  }

private:
  Channel m_channel;
};

Q_DECLARE_METATYPE (AudioDevice::Channel);

#endif
