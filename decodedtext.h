// -*- Mode: C++ -*-
/*
 * Class to handle the formatted string as returned from the fortran decoder
 *
 * VK3ACF August 2013
 */


#ifndef DECODEDTEXT_H
#define DECODEDTEXT_H

#include <QString>
#include <QStringList>



/*
012345678901234567890123456789012345678901
^    ^    ^   ^    ^  ^
2343 -11  0.8 1259 #  CQ VP2X/GM4WJS GL33
2343 -11  0.8 1259 #  CQ 999 VP2V/GM4WJS
2343 -11  0.8 1259 #  YV6BFE F6GUU R-08
2343 -19  0.3  718 #  VE6WQ SQ2NIJ -14
2343  -7  0.3  815 #  KK4DSD W7VP -16
2343 -13  0.1 3627 @  CT1FBK IK5YZT R+02

0605  Tx      1259 #  CQ VK3ACF QF22
*/

class DecodedText
{
public:
  explicit DecodedText (QString const& message);
  explicit DecodedText (QString const& js8callmessage, int bits, int submode);

  bool tryUnpack();
  bool tryUnpackHeartbeat();
  bool tryUnpackCompound();
  bool tryUnpackDirected();
  bool tryUnpackData();
  bool tryUnpackFastData();

  quint8 frameType() const { return frameType_; }
  QString frame() const { return frame_; }

  QString extra() const { return extra_; }
  QString compoundCall() const { return compound_; }
  bool isCompound() const { return !compound_.isEmpty(); }

  bool isHeartbeat() const { return isHeartbeat_; }
  bool isAlt() const { return isAlt_; }

  QStringList directedMessage() const { return directed_; }
  bool isDirectedMessage() const { return !directed_.isEmpty() && directed_.length() > 2; }

  QString string() const { return string_; }
  QString message() const { return message_; }
  QStringList messageWords () const;
  int indexOf(QString s) const { return string_.indexOf(s); }
  int indexOf(QString s, int i) const { return string_.indexOf(s,i); }
  QString mid(int f, int t) const { return string_.mid(f,t); }
  QString left(int i) const { return string_.left(i); }

  void clear() { string_.clear(); }

  QString CQersCall() const;

  bool isJT65() const;
  bool isJT9() const;
  bool isTX() const;
  bool isStandardMessage () const {return is_standard_;}
  bool isLowConfidence () const;
  int frequencyOffset() const;  // hertz offset from the tuned dial or rx frequency, aka audio frequency
  int snr() const;
  bool hasBits() const { return !string_.right(5).trimmed().isEmpty(); }
  int bits() const { return string_.right(5).trimmed().toShort(); }
  float dt() const;
  int submode() const { return submode_; }

  // find and extract any report. Returns true if this is a standard message
  bool report(QString const& myBaseCall, QString const& dxBaseCall, /*mod*/QString& report) const;

  // get the first message text word, usually the call
  QString call() const;

  // get the second word, most likely the de call and the third word, most likely grid
  void deCallAndGrid(/*out*/QString& call, QString& grid) const;

  unsigned timeInSeconds() const;

  // returns a string of the SNR field with a leading + or - followed by two digits
  QString report() const;

private:
  // These define the columns in the decoded text where fields are to be found.
  // We rely on these columns being the same in the fortran code (lib/decoder.f90) that formats the decoded text
  enum Columns {column_time    = 0,
      column_snr     = 5,
      column_dt      = 9,
      column_freq    = 14,
      column_mode    = 19,
      column_qsoText = 22 };

  quint8 frameType_;
  bool isHeartbeat_;
  bool isAlt_;
  QString compound_;
  QString extra_;
  QStringList directed_;
  QString string_;
  int padding_;
  QString message_;
  bool is_standard_;
  int bits_;
  int submode_;
  QString frame_;
};

#endif // DECODEDTEXT_H
