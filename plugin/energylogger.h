/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
*
* Copyright 2013 - 2024, nymea GmbH
* Contact: contact@nymea.io
*
* This file is part of nymea.
* This project including source code and documentation is protected by
* copyright law, and remains the property of nymea GmbH. All rights, including
* reproduction, publication, editing and translation, are reserved. The use of
* this project is subject to the terms of a license agreement to be concluded
* with nymea GmbH in accordance with the terms of use of nymea GmbH, available
* under https://nymea.io/license
*
* GNU General Public License Usage
* Alternatively, this project may be redistributed and/or modified under the
* terms of the GNU General Public License as published by the Free Software
* Foundation, GNU version 3. This project is distributed in the hope that it
* will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty
* of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
* Public License for more details.
*
* You should have received a copy of the GNU General Public License along with
* this project. If not, see <https://www.gnu.org/licenses/>.
*
* For any further details and any questions please contact us under
* contact@nymea.io or see our FAQ/Licensing Information on
* https://nymea.io/license/faq
*
* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#ifndef ENERGYLOGGER_H
#define ENERGYLOGGER_H

#include "energylogs.h"

#include <typeutils.h>

#include <QObject>
#include <QDateTime>
#include <QSqlDatabase>
#include <QSqlResult>
#include <QTimer>
#include <QMap>

class EnergyLogger : public EnergyLogs
{
    Q_OBJECT
public:
    explicit EnergyLogger(QObject *parent = nullptr);

    void logPowerBalance(double consumption, double production, double acquisition, double storage, double totalConsumption, double totalProduction, double totalAcquisition, double totalReturn);
    void logThingPower(const ThingId &thingId, double currentPower, double totalConsumption, double totalProduction);

    PowerBalanceLogEntries powerBalanceLogs(SampleRate sampleRate, const QDateTime &from = QDateTime(), const QDateTime &to = QDateTime()) const override;
    ThingPowerLogEntries thingPowerLogs(SampleRate sampleRate, const QList<ThingId> &thingIds, const QDateTime &from = QDateTime(), const QDateTime &to = QDateTime()) const override;

    PowerBalanceLogEntry latestLogEntry(SampleRate sampleRate);
    ThingPowerLogEntry latestLogEntry(SampleRate sampleRate, const ThingId &thingId);

    void removeThingLogs(const ThingId &thingId);
    QList<ThingId> loggedThings() const;

    // For internal use, the energymanager needs to cache some values to track things total values
    // This is really only here to have a single storage and not keep a separate cache file. Shouldn't be used for anything else
    // Note that the returned ThingPowerLogEntry will be incomplete. It won't have a timestamp nor a currentPower value!
    void cacheThingEntry(const ThingId &thingId, double totalEnergyConsumed, double totalEnergyProduced);
    ThingPowerLogEntry cachedThingEntry(const ThingId &thingId);

private slots:
    void sample();

private:
    bool initDB();
    void addConfig(SampleRate sampleRate, SampleRate baseSampleRate, int maxSamples);
    QDateTime getOldestPowerBalanceSampleTimestamp(SampleRate sampleRate);
    QDateTime getNewestPowerBalanceSampleTimestamp(SampleRate sampleRate);
    QDateTime getOldestThingPowerSampleTimestamp(const ThingId &thingId, SampleRate sampleRate);
    QDateTime getNewestThingPowerSampleTimestamp(const ThingId &thingId, SampleRate sampleRate);

    QDateTime nextSampleTimestamp(SampleRate sampleRate, const QDateTime &dateTime);
    void scheduleNextSample(SampleRate sampleRate);
    QDateTime calculateSampleStart(const QDateTime &sampleEnd, SampleRate sampleRate, int sampleCount = 1);

    void rectifySamples(SampleRate sampleRate, EnergyLogger::SampleRate baseSampleRate);

    bool samplePowerBalance(SampleRate sampleRate, SampleRate baseSampleRate, const QDateTime &sampleEnd);
    bool insertPowerBalance(const QDateTime &timestamp, SampleRate sampleRate, double consumption, double production, double acquisition, double storage, double totalConsumption, double totalProduction, double totalAcquisition, double totalReturn);
    bool sampleThingsPower(SampleRate sampleRate, SampleRate baseSampleRate, const QDateTime &sampleEnd);
    bool sampleThingPower(const ThingId &thingId, SampleRate sampleRate, SampleRate baseSampleRate, const QDateTime &sampleEnd);
    bool insertThingPower(const QDateTime &timestamp, SampleRate sampleRate, const ThingId &thingId, double currentPower, double totalConsumption, double totalProduction);
    void trimPowerBalance(SampleRate sampleRate, const QDateTime &beforeTime);
    void trimThingPower(const ThingId &thingId, SampleRate sampleRate, const QDateTime &beforeTime);

    PowerBalanceLogEntry queryResultToBalanceLogEntry(const QSqlRecord &record) const;
    ThingPowerLogEntry queryResultToThingPowerLogEntry(const QSqlRecord &record) const;

private:
    struct SampleConfig {
        SampleRate baseSampleRate;
        uint maxSamples = 0;
    };

    PowerBalanceLogEntries m_balanceLiveLog;
    QHash<ThingId, ThingPowerLogEntries> m_thingsPowerLiveLogs;

    QTimer m_sampleTimer;
    QHash<SampleRate, QDateTime> m_nextSamples;

    QSqlDatabase m_db;

    int m_maxMinuteSamples = 0;
    QMap<SampleRate, SampleConfig> m_configs;
};

#endif // ENERGYLOGGER_H
