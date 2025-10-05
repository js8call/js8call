#ifndef DRIFTINGDATETIME_H
#define DRIFTINGDATETIME_H

#include <QDateTime>

/**
 * JS8Call allows the user to manipulate the clock.
 *
 * One intention is: If a QSO partner's clock is off too much, we can
 * set our clock to correspond to that partner's clock.  This increases
 * the probability of successful decodes on both sides.
 *
 * Alternatively, we may not be able to set our own computer's clock
 * with appropriate precision.  If so, we can manipulate our
 * JS8Call-internal clock, e.g., based on incoming JS8 signals, to fit
 * other folks' clocks.
 *
 * Conceptually, there is only one, central clock for the entire
 * application.  So mere methods are used, the clock isn't an object.
 *
 * The manipulated clock is ubiquitously used throughout JS8Call.  This
 * includes timestamps logged to ALL.TXT or via Qt logging.  This makes
 * it easier to judge, especially when reading the Qt logs, whether
 * JS8Call's timing is as it should be.
 **/

namespace DriftingDateTime
{
    qint64    drift();
    void      setDrift(qint64);
    qint64    incrementDrift(qint64);
    QDateTime currentDateTime();
    QDateTime currentDateTimeUtc();
    qint64    currentMSecsSinceEpoch();
    qint64    currentSecsSinceEpoch();
};

#endif // DRIFTINGDATETIME_H
