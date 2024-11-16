#include "decodedtext.h"

#include <QStringList>
#include <QRegularExpression>
#include <QDebug>

#include <varicode.h>

extern "C" {
  bool stdmsg_(char const * msg, fortran_charlen_t);
}

namespace
{
  QRegularExpression words_re {R"(^(?:(?<word1>(?:CQ|DE|QRZ)(?:\s?DX|\s(?:[A-Z]{2}|\d{3}))|[A-Z0-9/]+)\s)(?:(?<word2>[A-Z0-9/]+)(?:\s(?<word3>[-+A-Z0-9]+)(?:\s(?<word4>(?:OOO|(?!RR73)[A-R]{2}[0-9]{2})))?)?)?)"};
}

DecodedText::DecodedText (QString const& the_string)
  : frameType_(Varicode::FrameUnknown)
  , isHeartbeat_(false)
  , isAlt_(false)
  , string_ {the_string.left (the_string.indexOf (QChar::Nbsp))} // discard appended info
  , padding_ {string_.indexOf (" ") > 4 ? 2 : 0} // allow for seconds
  , message_ {string_.mid (column_qsoText + padding_).trimmed ()}
  , is_standard_ {false}
  , bits_{0}
  , submode_{ string_.mid(column_mode + padding_, 3).trimmed().at(0).cell() - 'A' }
  , frame_ { string_.mid (column_qsoText + padding_, 12).trimmed () }
{
    if(message_.length() >= 1) {
        message_ = message_.left (21).remove (QRegularExpression {"[<>]"});
        int i1 = message_.indexOf ('\r');
        if (i1 > 0) {
            message_ = message_.left (i1 - 1);
        }

        if (message_.contains (QRegularExpression {"^(CQ|QRZ)\\s"})) {
            // TODO this magic position 16 is guaranteed to be after the
            // last space in a decoded CQ or QRZ message but before any
            // appended DXCC entity name or worked before information
            auto eom_pos = message_.indexOf (' ', 16);
            // we always want at least the characters to position 16
            if (eom_pos < 16) eom_pos = message_.size () - 1;
            // remove DXCC entity and worked B4 status. TODO need a better way to do this
            message_ = message_.left (eom_pos + 1);
        }

        // stdmsg is a fortran routine that packs the text, unpacks it
        // and compares the result
        auto message_c_string = message_.toLocal8Bit ();
        message_c_string += QByteArray {22 - message_c_string.size (), ' '};
        is_standard_ = stdmsg_ (message_c_string.constData (), 22);

        // We're only going to unpack standard messages for CQs && pings...
        // TODO: jsherer - this is a hack for now...
        if(is_standard_){
            is_standard_ = QRegularExpression("^(CQ|DE|QRZ)\\s").match(message_).hasMatch();
        }
    }

    bits_ = bits();

    tryUnpack();
}

DecodedText::DecodedText (QString const& js8callmessage, int bits, int submode):
    frameType_(Varicode::FrameUnknown),
    isHeartbeat_(false),
    isAlt_(false),
    message_(js8callmessage),
    bits_(bits),
    submode_(submode),
    frame_(js8callmessage)
{
    is_standard_ = QRegularExpression("^(CQ|DE|QRZ)\\s").match(message_).hasMatch();

    tryUnpack();
}

bool DecodedText::tryUnpack(){
    if(is_standard_){
        message_ = message_.append(" ");
        return false;
    }

    bool unpacked = false;
    if(!unpacked){
        unpacked = tryUnpackFastData();
    }

     if(!unpacked){
        unpacked = tryUnpackData();
     }

    if(!unpacked){
        unpacked = tryUnpackHeartbeat();
    }

    if(!unpacked){
        unpacked = tryUnpackCompound();
    }

    if(!unpacked){
        unpacked = tryUnpackDirected();
    }

    return unpacked;
}

bool DecodedText::tryUnpackHeartbeat(){
    QString m = message().trimmed();

    // directed calls will always be 12+ chars and contain no spaces.
    if(m.length() < 12 || m.contains(' ')){
      return false;
    }

    if((bits_ & Varicode::JS8CallData) == Varicode::JS8CallData){
            return false;
    }

    bool isAlt = false;
    quint8 type = Varicode::FrameUnknown;
    quint8 bits3 = 0;
    QStringList parts = Varicode::unpackHeartbeatMessage(m, &type, &isAlt, &bits3);

    if(parts.isEmpty() || parts.length() < 2){
        return false;
    }

    // Heartbeat Alt Type
    // ---------------
    // 1      0   HB
    // 1      1   CQ
    isHeartbeat_ = true;
    isAlt_ = isAlt;
    extra_ = parts.length() < 3 ? "" : parts.at(2);

    QStringList cmp;
    if(!parts.at(0).isEmpty()){
        cmp.append(parts.at(0));
    }
    if(!parts.at(1).isEmpty()){
        cmp.append(parts.at(1));
    }
    compound_ = cmp.join("/");

    if(isAlt){
        auto sbits3 = Varicode::cqString(bits3);
        message_ = QString("%1: @ALLCALL %2 %3 ").arg(compound_).arg(sbits3).arg(extra_);
        frameType_ = type;
    } else {
        auto sbits3 = Varicode::hbString(bits3);
        if(sbits3 == "HB"){
            message_ = QString("%1: @HB HEARTBEAT %2 ").arg(compound_).arg(extra_);
            frameType_ = type;
        } else {
            message_ = QString("%1: @HB %2 %3 ").arg(compound_).arg(sbits3).arg(extra_);
            frameType_ = type;
        }
    }

    return true;
}

bool DecodedText::tryUnpackCompound(){
    auto m = message().trimmed();
    // directed calls will always be 12+ chars and contain no spaces.
    if(m.length() < 12 || m.contains(' ')){
        return false;
    }

    quint8 type = Varicode::FrameUnknown;
    quint8 bits3 = 0;
    auto parts = Varicode::unpackCompoundMessage(m, &type, &bits3);
    if(parts.isEmpty() || parts.length() < 2){
        return false;
    }

    if((bits_ & Varicode::JS8CallData) == Varicode::JS8CallData){
        return false;
    }

    QStringList cmp;
    if(!parts.at(0).isEmpty()){
        cmp.append(parts.at(0));
    }
    if(!parts.at(1).isEmpty()){
        cmp.append(parts.at(1));
    }
    compound_ = cmp.join("/");
    extra_ = parts.length() < 3 ? "" : parts.mid(2).join(" ");

    if(type == Varicode::FrameCompound){
        message_ = QString("%1: ").arg(compound_);
    } else if(type == Varicode::FrameCompoundDirected){
        message_ = QString("%1%2 ").arg(compound_).arg(extra_);
        directed_ = QStringList{ "<....>", compound_ } + parts.mid(2);
    }

    frameType_ = type;
    return true;
}

bool DecodedText::tryUnpackDirected(){
    QString m = message().trimmed();

    // directed calls will always be 12+ chars and contain no spaces.
    if(m.length() < 12 || m.contains(' ')){
      return false;
    }

    if((bits_ & Varicode::JS8CallData) == Varicode::JS8CallData){
        return false;
    }

    quint8 type = Varicode::FrameUnknown;
    QStringList parts = Varicode::unpackDirectedMessage(m, &type);

    if(parts.isEmpty()){
      return false;
    }

    if(parts.length() == 3){
      // replace it with the correct unpacked (directed)
      message_ = QString("%1: %2%3 ").arg(parts.at(0), parts.at(1), parts.at(2));
    } else if(parts.length() == 4){
      // replace it with the correct unpacked (directed numeric)
      message_ = QString("%1: %2%3 %4 ").arg(parts.at(0), parts.at(1), parts.at(2), parts.at(3));
    } else {
      // replace it with the correct unpacked (freetext)
      message_ = QString(parts.join(""));
    }

    directed_ = parts;
    frameType_ = type;
    return true;
}

bool DecodedText::tryUnpackData(){
    QString m = message().trimmed();

    // data frames calls will always be 12+ chars and contain no spaces.
    if(m.length() < 12 || m.contains(' ')){
        return false;
    }

    if((bits_ & Varicode::JS8CallData) == Varicode::JS8CallData){
        return false;
    }

    QString data = Varicode::unpackDataMessage(m);

    if(data.isEmpty()){
      return false;
    }

    message_ = data;
    frameType_ = Varicode::FrameData;
    return true;
}

bool DecodedText::tryUnpackFastData(){
    QString m = message().trimmed();

    // data frames calls will always be 12+ chars and contain no spaces.
    if(m.length() < 12 || m.contains(' ')){
        return false;
    }

    if((bits_ & Varicode::JS8CallData) != Varicode::JS8CallData){
        return false;
    }

    QString data = Varicode::unpackFastDataMessage(m);

    if(data.isEmpty()){
      return false;
    }

    message_ = data;
    frameType_ = Varicode::FrameData;
    return true;
}

QStringList DecodedText::messageWords () const
{
  if (is_standard_)
    {
      // extract up to the first four message words
      return words_re.match (message_).capturedTexts ();
    }
  // simple word split for free text messages
  auto words = message_.split (' ', Qt::SkipEmptyParts);
  // add whole message as item 0 to mimic RE capture list
  words.prepend (message_);
  return words;
}

QString DecodedText::CQersCall() const
{
  QRegularExpression callsign_re {R"(^(CQ|DE|QRZ)(\s?DX|\s([A-Z]{2}|\d{3}))?\s(?<callsign>[A-Z0-9/]{2,})(\s[A-R]{2}[0-9]{2})?)"};
  return callsign_re.match (message_).captured ("callsign");
}


bool DecodedText::isJT65() const
{
    return string_.indexOf("#") == column_mode + padding_;
}

bool DecodedText::isJT9() const
{
    return string_.indexOf("@") == column_mode + padding_;
}

bool DecodedText::isTX() const
{
    int i = string_.indexOf("Tx");
    return (i >= 0 && i < 15); // TODO guessing those numbers. Does Tx ever move?
}

bool DecodedText::isLowConfidence () const
{
  return QChar {'?'} == string_.mid (padding_ + column_qsoText + 21, 1);
}

int DecodedText::frequencyOffset() const
{
    return string_.mid(column_freq + padding_,4).toInt();
}

int DecodedText::snr() const
{
  int i1=string_.indexOf(" ")+1;
  return string_.mid(i1,3).toInt();
}

float DecodedText::dt() const
{
  return string_.mid(column_dt + padding_,5).toFloat();
}

/*
2343 -11  0.8 1259 # YV6BFE F6GUU R-08
2343 -19  0.3  718 # VE6WQ SQ2NIJ -14
2343  -7  0.3  815 # KK4DSD W7VP -16
2343 -13  0.1 3627 @ CT1FBK IK5YZT R+02

0605  Tx      1259 # CQ VK3ACF QF22
*/

// find and extract any report. Returns true if this is a standard message
bool DecodedText::report(QString const& myBaseCall, QString const& dxBaseCall, /*mod*/QString& report) const
{
  if (message_.size () < 1) return false;

  QStringList const& w = message_.split(" ",Qt::SkipEmptyParts);
  if (w.size ()
      && is_standard_ && (w[0] == myBaseCall
                          || w[0].endsWith ("/" + myBaseCall)
                          || w[0].startsWith (myBaseCall + "/")
                          || (w.size () > 1 && !dxBaseCall.isEmpty ()
                              && (w[1] == dxBaseCall
                                  || w[1].endsWith ("/" + dxBaseCall)
                                  || w[1].startsWith (dxBaseCall + "/")))))
    {
      QString tt="";
      if(w.size() > 2) tt=w[2];
      bool ok;
      auto i1=tt.toInt(&ok);
      if (ok and i1>=-50 and i1<50)
        {
          report = tt;
        }
      else
        {
          if (tt.mid(0,1)=="R")
            {
              i1=tt.mid(1).toInt(&ok);
              if(ok and i1>=-50 and i1<50)
                {
                  report = tt.mid(1);
                }
            }
        }
    }
  return is_standard_;
}

// get the first text word, usually the call
QString DecodedText::call() const
{
  return words_re.match (message_).captured ("word1");
}

// get the second word, most likely the de call and the third word, most likely grid
void DecodedText::deCallAndGrid(/*out*/QString& call, QString& grid) const
{
  auto const& match = words_re.match (message_);
  call = match.captured ("word2");
  grid = match.captured ("word3");
}

unsigned DecodedText::timeInSeconds() const
{
  return 3600 * string_.mid (column_time, 2).toUInt ()
    + 60 * string_.mid (column_time + 2, 2).toUInt()
    + (padding_ ? string_.mid (column_time + 2 + padding_, 2).toUInt () : 0U);
}

/*
2343 -11  0.8 1259 # YV6BFE F6GUU R-08
2343 -19  0.3  718 # VE6WQ SQ2NIJ -14
2343  -7  0.3  815 # KK4DSD W7VP -16
2343 -13  0.1 3627 @ CT1FBK IK5YZT R+02

0605  Tx      1259 # CQ VK3ACF QF22
*/

QString DecodedText::report() const // returns a string of the SNR field with a leading + or - followed by two digits
{
    int sr = snr();
    if (sr<-50)
        sr = -50;
    else
        if (sr > 49)
            sr = 49;

    QString rpt = QString("%1").arg(abs(sr));
    if (sr > 9)
        rpt = "+" + rpt;
    else
        if (sr >= 0)
            rpt = "+0" + rpt;
        else
            if (sr >= -9)
                rpt = "-0" + rpt;
            else
                rpt = "-" + rpt;
    return rpt;
}
