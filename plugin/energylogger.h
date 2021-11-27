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
        int maxSamples = 0;
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
