/*
 * maintains a list of country names that have been worked
 * VK3ACF July 2013
 */

#ifndef __COUNTRIESWORKDED_H
#define __COUNTRIESWORKDED_H

#include <QList>
#include <QString>
#include <QStringList>
#include <QHash>


class CountriesWorked
{
 public:
	void init(const QStringList countryNames);
	void setAsWorked(const QString countryName);
	bool getHasWorked(const QString countryName) const;
	qsizetype getWorkedCount() const;
	qsizetype getSize() const;

 private:
	QHash<QString, bool> _data;
};

#endif

