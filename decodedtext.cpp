#include "commons.h"
#include "decodedtext.h"
#include <QStringBuilder>
#include <varicode.h>

/******************************************************************************/
// Constants
/******************************************************************************/

namespace
{
  // Quality level below which we'll consider a decode to be suspect;
  // the UI will generally enclose the decode within [] characters to
  // denote it as being sketchy.

  constexpr auto QUALITY_THRESHOLD = 0.17f;
}

/******************************************************************************/
// Local Routines
/******************************************************************************/

namespace
{
  // Translation of standard submode IDs to their character equivalents.
  // this is only used when writing out to ALL.TXT, so we've defined it
  // here, but arguably it should be part of JS8::Submode or Varicode.

  QChar
  submodeChar(int const submode)
  {
    switch (submode)
    {
      case 0:  return 'A';
      case 1:  return 'B';
      case 2:  return 'C';
      case 4:  return 'E';
      case 8:  return 'I';
      default: return '~';
    }
  }

  // Create and return a potentially compound call from the provided
  // parts; the parts are at this point guaranteed to be at least of
  // size 2, but any part might be empty.

  QString
  buildCompound(QStringList const & parts)
  {
    auto   subset = parts.mid(0, 2);
           subset.removeAll("");
    return subset.join('/');
  }
}

/******************************************************************************/
// Private Implementation
/******************************************************************************/

// Core constructor, called by the two public constructors.
// Attempts to unpack, using the unpack strategies defined
// in the order of the unpack strategies array, until one
// of them works or all of them have failed.

DecodedText::DecodedText(QString const & frame,
                         int             bits,
                         int             submode,
                         bool            isLowConfidence,
                         int             time,
                         int             frequencyOffset,
                         float           snr,
                         float           dt)
: frameType_      (Varicode::FrameUnknown)
, frame_          (frame)
, isAlt_          (false)
, isHeartbeat_    (false)
, isLowConfidence_(isLowConfidence)
, message_        (frame_)
, bits_           (bits)
, submode_        (submode)
, time_           (time)
, frequencyOffset_(frequencyOffset)
, snr_            (snr)
, dt_             (dt)
{
  auto const m = message().trimmed();

  if (m.length() < 12 || m.contains(' ')) return;

  for (auto unpack : unpackStrategies)
  {
    if ((this->*unpack)(m)) break;
  }
}

bool
DecodedText::tryUnpackFastData(QString const & m)
{
  if ((bits_ & Varicode::JS8CallData) != Varicode::JS8CallData) return false;

  if (auto const data = Varicode::unpackFastDataMessage(m);
                 data.isEmpty())
  {
    return false;
  }
  else
  {
    message_   = data;
    frameType_ = Varicode::FrameData;

    return true;
  }
}

bool
DecodedText::tryUnpackData(QString const & m)
{
  if ((bits_ & Varicode::JS8CallData) == Varicode::JS8CallData) return false;

  if (auto const data = Varicode::unpackDataMessage(m);
                 data.isEmpty())
  {
    return false;
  }
  else
  {
    message_   = data;
    frameType_ = Varicode::FrameData;

    return true;
  }
}

bool
DecodedText::tryUnpackHeartbeat(QString const & m)
{
  if ((bits_ & Varicode::JS8CallData) == Varicode::JS8CallData) return false;

  bool       isAlt = false;
  quint8     type  = Varicode::FrameUnknown;
  quint8     bits3 = 0;
  auto const parts = Varicode::unpackHeartbeatMessage(m, &type, &isAlt, &bits3);

  if (parts.length() < 2) return false;

  // Heartbeat Alt Type
  // ------------------
  // 1         0   HB
  // 1         1   CQ

  frameType_   = type;
  isHeartbeat_ = true;
  isAlt_       = isAlt;
  extra_       = parts.value(2, QString());  
  compound_    = buildCompound(parts);
  message_     = compound_ % ": ";

  if (isAlt)
  {
    message_ += "@ALLCALL " % Varicode::cqString(bits3);
  }
  else
  {
    auto const sbits3 = Varicode::hbString(bits3);
    message_ += "@HB " % (sbits3 == "HB" ? "HEARTBEAT" : sbits3);
  }

  message_ += ' ' % extra_ % ' ';
               
  return true;
}

bool
DecodedText::tryUnpackCompound(QString const & m)
{
  quint8     type  = Varicode::FrameUnknown;
  quint8     bits3 = 0;
  auto const parts = Varicode::unpackCompoundMessage(m, &type, &bits3);

  if (parts.length() < 2 ||
     (bits_ & Varicode::JS8CallData) == Varicode::JS8CallData) return false;

  frameType_ = type;
  extra_     = parts.mid(2).join(' ');
  compound_  = buildCompound(parts);

  if (type == Varicode::FrameCompound)
  {
    message_ = compound_ % ": ";
  }
  else if (type == Varicode::FrameCompoundDirected)
  {
    message_ = compound_ % extra_ % ' ';

    directed_.reserve(parts.size() - 2 + 2);
    directed_  = { "<....>", compound_ };
    directed_ += parts.mid(2);
  }

  return true;
}

bool
DecodedText::tryUnpackDirected(QString const & m)
{
  if ((bits_ & Varicode::JS8CallData) == Varicode::JS8CallData) return false;

  quint8            type  = Varicode::FrameUnknown;
  QStringList const parts = Varicode::unpackDirectedMessage(m, &type);

  if (parts.isEmpty()) return false;

  switch (parts.length())
  {
    case 3: // Directed message         => "0: 12 "
    case 4: // Directed numeric message => "0: 12 3 "
      message_ = parts.at(0) % ": " % parts.at(1) % parts.mid(2).join(' ') % ' ';
      break;
    default: // Free text message
      message_ = parts.join("");
      break;
  }

  directed_  = parts;
  frameType_ = type;

  return true;
}

/******************************************************************************/
// Public Implementation
/******************************************************************************/

// Main constructor, used to interpret Decoded events emitted by the JS8
// decoder. This function used to be handled via parsing strings issued by
// the Fortran decoder.
//
// Of note here is the quality check; that was present in the previous code,
// but did not seem to be looking in the right place for the annotation that
// the Fortran decoded emitted.

DecodedText::DecodedText(JS8::Event::Decoded const & decoded)
: DecodedText(QString::fromStdString(decoded.data),
              decoded.type,
              decoded.mode,
              decoded.quality < QUALITY_THRESHOLD,
              decoded.utc,
              decoded.frequency,
              decoded.snr,
              decoded.xdt)
{}

// Constructor used internally; we're basically taking advantage of the ability
// of this class to unpack, and as such this probably doesn't belong here, but
// keeping it aligned with the previous code for now.

DecodedText::DecodedText(QString const & frame,
                        int      const   bits,
                        int      const   submode)
: DecodedText(frame,
              bits,
              submode,
              false,
              0,
              0,
              0.0f,
              0.0f)
{}

// Simple word split for free text messages; preallocate memory for
// efficiency; add whole message as item 0 to mimic regular expression
// capture list.

QStringList
DecodedText::messageWords() const
{
  QStringList words;

  words.reserve(message_.count(' ') + 2);
  words.append(message_);
  words.append(message_.split(' ', Qt::SkipEmptyParts));
  
  return words;
}

// Format as a string suitable for appending to ALL.TXT. Original
// code has no space between time and SNR; matching that here.

QString
DecodedText::string() const
{
  struct hour_minute_second hms = decode_time(time_);

  return QStringLiteral("%1:%2:%3%4 %5 %6 %7  %8         %9   ")
    .arg(hms.hour,         2, 10, QChar('0'))  // Right-aligned integer with 2 characters, padded with zeroes.
    .arg(hms.minute,       2, 10, QChar('0'))  // Right-aligned integer with 2 characters, padded with zeroes.
    .arg(hms.second,       2, 10, QChar('0'))  // Right-aligned integer with 2 characters, padded with zeroes.
    .arg(snr_,             3, 10, QChar(' '))  // Right-aligned integer with 3 characters, padded with spaces
    .arg(dt_,              4, 'f', 1)          // Right-aligned float with 1 decimal point
    .arg(frequencyOffset_, 4, 10, QChar(' '))  // Right-aligned integer with 4 characters, passed with spaces
    .arg(submodeChar(submode_))                // Single character
    .arg(frame_)                               // Fixed string, 12 characters
    .arg(bits_);                               // Single 3-bit integer
}

/******************************************************************************/
