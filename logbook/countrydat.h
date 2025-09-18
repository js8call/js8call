/*
 * Reads cty.dat file
 * Establishes a map between prefixes and their country names
 * VK3ACF July 2013
 */


#ifndef __COUNTRYDAT_H
#define __COUNTRYDAT_H


#include <QString>
#include <QStringList>
#include <QHash>


class CountryDat
{
public:
  void init(const QString filename);
  void load();
  QString find(QString prefix) const; // return country name or ""
  QStringList  getCountryNames() const { return _countryNames; };

private:
  QString _extractName(const QString line) const;
  void _removeBrackets(QString &line, const QString a, const QString b) const;
  QStringList _extractPrefix(QString &line, bool &more) const;
  QString fixup (QString country, QString const& call) const;

  QString _filename;
  QStringList _countryNames;
  QHash<QString, QString> _data;
};

#endif
