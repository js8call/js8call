#include "DriftingDateTime.h"

namespace
{
    qint64 driftMS = 0;
}

namespace DriftingDateTime
{
    qint64
    drift()
    {
        return driftMS;
    }

    void
    setDrift(qint64 const ms)
    {
        driftMS = ms;
    }

    qint64
    incrementDrift(qint64 const msDelta)
    {
        return driftMS += msDelta;
    }

    QDateTime
    currentDateTime()
    {
        return QDateTime::currentDateTime().addMSecs(driftMS);
    }

    QDateTime
    currentDateTimeUtc()
    {
        return QDateTime::currentDateTimeUtc().addMSecs(driftMS);
    }

    qint64
    currentMSecsSinceEpoch()
    {
        return QDateTime::currentMSecsSinceEpoch() + driftMS;
    }

    qint64
    currentSecsSinceEpoch()
    {
        return currentMSecsSinceEpoch() / 1000;
    }
}