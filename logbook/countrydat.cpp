/*
#Sov Mil Order of Malta:   15:  28:  EU:   41.90:   -12.43:    -1.0:  1A:
    #1A;
#Spratly Islands:          26:  50:  AS:    9.88:  -114.23:    -8.0:  1S:
    #1S,9M0,BV9S;
#Monaco:                   14:  27:  EU:   43.73:    -7.40:    -1.0:  3A:
    #3A;
#Heard Island:             39:  68:  AF:  -53.08:   -73.50:    -5.0:  VK0H:
    #=VK0IR;
#Macquarie Island:         30:  60:  OC:  -54.60:  -158.88:   -10.0:  VK0M:
    #=VK0KEV;
#Cocos-Keeling:            29:  54:  OC:  -12.15:   -96.82:    -6.5:  VK9C:
    #AX9C,AX9Y,VH9C,VH9Y,VI9C,VI9Y,VJ9C,VJ9Y,VK9C,VK9Y,VL9C,VL9Y,VM9C,VM9Y,
    #VN9C,VN9Y,VZ9C,VZ9Y,=VK9AA;
*/


#include "countrydat.h"
#include <QFile>
#include <QTextStream>
#include <QDebug>
#include "Radio.hpp"

void CountryDat::init(const QString filename)
{
    _filename = filename;
    _data.clear();
}

QString CountryDat::_extractName(const QString line) const
{
    int s1 = line.indexOf(':');
    if (s1>=0)
    {
        QString name = line.mid(0,s1);
        return name;
    }
    return "";
}

void CountryDat::_removeBrackets(QString &line, const QString a, const QString b) const
{
    int s1 = line.indexOf(a);
    while (s1 >= 0)
    {
      int s2 = line.indexOf(b);
      line = line.mid(0,s1) + line.mid(s2+1,-1);
      s1 = line.indexOf(a);
    }
}

QStringList CountryDat::_extractPrefix(QString &line, bool &more) const
{
    line = line.remove(" \n");
    line = line.replace(" ","");

    _removeBrackets(line,"(",")");
    _removeBrackets(line,"[","]");
    _removeBrackets(line,"<",">");
    _removeBrackets(line,"~","~");

    int s1 = line.indexOf(';');
    more = true;
    if (s1 >= 0)
    {
      line = line.mid(0,s1);
      more = false;
    }

    QStringList r = line.split(',');

    return r;
}


void CountryDat::load()
{
    _data.clear();
    _countryNames.clear(); //used by countriesWorked

    QFile inputFile(_filename);
    if (inputFile.open(QIODevice::ReadOnly))
    {
       QTextStream in(&inputFile);
       while ( !in.atEnd() )
       {
          QString line1 = in.readLine();
          if ( !in.atEnd() )
          {
            QString line2 = in.readLine();

            QString name = _extractName(line1);
            if (name.length()>0)
            {
              QString continent=line1.mid(36,2);
              QString principalPrefix=line1.mid(69,4);
              int i1=principalPrefix.indexOf(":");
              if(i1>0) principalPrefix=principalPrefix.mid(0,i1);
              name += "; " + principalPrefix + "; " + continent;
                _countryNames << name;
                bool more = true;
                QStringList prefixs;
                while (more)
                {
                    QStringList p = _extractPrefix(line2,more);
                    prefixs += p;
                    if (more)
                        line2 = in.readLine();
                }

                QString p;
                foreach(p,prefixs)
                {
                    if (p.length() > 0)
                        _data.insert(p,name);
                }
            }
          }
       }
    inputFile.close();
    }
}

// return country name else ""
QString CountryDat::find(QString call) const
{
  call = call.toUpper ();

  // check for exact match first
  if (_data.contains ("=" + call))
    {
      return fixup (_data.value ("=" + call), call);
    }

  auto prefix = Radio::effective_prefix (call);
  auto match_candidate = prefix;
  while (match_candidate.size () >= 1)
    {
      if (_data.contains (match_candidate))
        {
          return fixup (_data.value (match_candidate), prefix);
        }
      match_candidate = match_candidate.left (match_candidate.size () - 1);
    }
  return QString {};
}

QString CountryDat::fixup (QString country, QString const& call) const
{
  //
  // deal with special rules that cty.dat does not cope with
  //

  // KG4 2x1 and 2x3 calls that map to Gitmo are mainland US not Gitmo
  if (call.startsWith ("KG4") && call.size () != 5 && call.size () != 3)
    {
      country.replace ("Guantanamo Bay; KG4; NA", "United States; K; NA");
    }
  return country;
}
