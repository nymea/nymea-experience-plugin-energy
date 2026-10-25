// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ENERGYSAMPLETIMESTAMPS_H
#define ENERGYSAMPLETIMESTAMPS_H

#include <energylogs.h>

namespace EnergySampleTimestamps {

inline QDateTime nextSampleTimestamp(EnergyLogs::SampleRate sampleRate, const QDateTime &dateTime)
{
    if (!dateTime.isValid()) {
        return QDateTime();
    }

    QTime time = dateTime.time();
    QDate date = dateTime.date();
    QDateTime next;
    switch (sampleRate) {
    case EnergyLogs::SampleRateAny:
        return QDateTime();
    case EnergyLogs::SampleRate1Min:
    case EnergyLogs::SampleRate15Mins:
    case EnergyLogs::SampleRate1Hour: {
        // Advance on the timeline rather than reconstructing local wall time.
        // During the autumn DST transition the same wall time occurs twice.
        const qint64 interval = qint64(sampleRate) * 60 * 1000;
        const qint64 elapsed = (time.minute() % int(sampleRate)) * 60 * 1000
                + time.second() * 1000 + time.msec();
        next = dateTime.addMSecs(interval - elapsed);
        break;
    }
    case EnergyLogs::SampleRate3Hours:
        time.setHMS(time.hour() - (time.hour() % 3), 0, 0);
        next = QDateTime(date, time).addMSecs(3 * 60 * 60 * 1000);
        if (next.time().hour() == 2) {
            next = next.addMSecs(60 * 60 * 1000);
        }
        break;
    case EnergyLogs::SampleRate1Day:
        next = QDateTime(date, QTime()).addDays(1);
        break;
    case EnergyLogs::SampleRate1Week:
        date = date.addDays(-date.dayOfWeek() + 1);
        next = QDateTime(date, QTime()).addDays(7);
        break;
    case EnergyLogs::SampleRate1Month:
        date = date.addDays(-date.day() + 1);
        next = QDateTime(date, QTime()).addMonths(1);
        break;
    case EnergyLogs::SampleRate1Year:
        date.setDate(date.year(), 1, 1);
        next = QDateTime(date, QTime()).addYears(1);
        break;
    }

    return next;
}

}

#endif // ENERGYSAMPLETIMESTAMPS_H
