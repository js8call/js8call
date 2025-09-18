/*
 * Reads an ADIF log file into memory
 * Searches log for call, band and mode
 * VK3ACF July 2013
 */


#ifndef __ADIF_H
#define __ADIF_H

#if defined (QT5)
#include <QList>
#include <QString>
#include <QStringList>
#include <QMultiHash>
#include <QVariant>
#else
#include <QtGui>
#endif

#include "fileutils.h"

class QDateTime;

extern const QStringList ADIF_FIELDS;

class ADIF
{
	public:

    struct QSO;

	void init(QString const& filename);
	void load();
    void add(QString const& call, QString const& band, QString const& mode, const QString &submode, QString const& grid, QString const& date, const QString &name, const QString &comment);
    bool match(QString const& call, QString const& band) const;
    QList<ADIF::QSO> find(QString const& call) const;
	QList<QString> getCallList() const;
	qsizetype getCount() const;

        // open ADIF file and append the QSO details. Return true on success
	bool addQSOToFile(QByteArray const& ADIF_record);

    QByteArray QSOToADIF(QString const& hisCall, QString const& hisGrid, QString const& mode, QString const& submode, QString const& rptSent
                                             , QString const& rptRcvd, QDateTime const& dateTimeOn, QDateTime const& dateTimeOff
                                             , QString const& band, QString const& comments, QString const& name
                                             , QString const& strDialFreq, QString const& m_myCall, QString const& m_myGrid
                                             , QString const& operator_call, const QMap<QString, QVariant> &additionalFields);


    struct QSO
    {
      QString call,band,mode,submode,grid,date,name,comment;
    };

    private:
		QMultiHash<QString, QSO> _data;
		QString _filename;

		QString extractField(QString const& line, QString const& fieldName) const;
};


#endif

