#include "logbook.h"
#include <QDebug>
#include <QFontMetrics>
#include <QStandardPaths>
#include <QDir>

namespace
{
  auto logFileName = "js8call_log.adi";
  auto countryFileName = "cty.dat";
}

void LogBook::init()
{
  QDir dataPath {QStandardPaths::writableLocation (QStandardPaths::AppDataLocation)};
  QString countryDataFilename;
  if (dataPath.exists (countryFileName))
    {
      // User override
      countryDataFilename = dataPath.absoluteFilePath (countryFileName);
    }
  else
    {
      countryDataFilename = QString {":/"} + countryFileName;
    }

  _countries.init(countryDataFilename);
  _countries.load();

  _worked.init(_countries.getCountryNames());

  _log.init(dataPath.absoluteFilePath (logFileName));
  _log.load();

  _setAlreadyWorkedFromLog();
}


void LogBook::_setAlreadyWorkedFromLog()
{
  QList<QString> calls = _log.getCallList();
  QString c;
  foreach(c,calls)
    {
      QString countryName = _countries.find(c);
      if (countryName.length() > 0)
        {
          _worked.setAsWorked(countryName);
        }
    }
}

bool LogBook::hasWorkedBefore(const QString &call, const QString &band){
    return _log.match(call, band);
}

void LogBook::match(/*in*/const QString call,
                    /*out*/ QString &countryName,
                    bool &callWorkedBefore,
                    bool &countryWorkedBefore) const
{
    if(call.isEmpty()){
        return;
    }

    QString currentBand = "";  // match any band
    callWorkedBefore = _log.match(call, currentBand);
    countryName = _countries.find(call);

    if (countryName.length() > 0){  //  country was found
        countryWorkedBefore = _worked.getHasWorked(countryName);
    } else {
        countryName = "where?"; //error: prefix not found
        countryWorkedBefore = false;
    }
}

bool LogBook::findCallDetails(
                    /*in*/
                    const QString call,
                    /*out*/
                    QString &grid,
                    QString &date,
                    QString &name,
                    QString &comment) const
{
    if(call.isEmpty()){
        return false;
    }

    auto qsos = _log.find(call);
    if(qsos.isEmpty()){
        return false;
    }

    foreach(auto qso, qsos){
        if(grid.isEmpty() && !qso.grid.isEmpty()) grid = qso.grid;
        if(date.isEmpty() && !qso.date.isEmpty()) date = qso.date;
        if(name.isEmpty() && !qso.name.isEmpty()) name = qso.name;
        if(comment.isEmpty() && !qso.comment.isEmpty()) comment = qso.comment;
    }

    return true;
}

void LogBook::addAsWorked(const QString call, const QString band, const QString mode, const QString submode, const QString grid, const QString date, const QString name, const QString comment)
{
  _log.add(call,band,mode,submode,grid,date,name,comment);
  QString countryName = _countries.find(call);
  if (countryName.length() > 0)
    _worked.setAsWorked(countryName);
}



