#include "energylogger.h"

#include <nymeasettings.h>

#include <QStandardPaths>
#include <QDir>
#include <QSqlQuery>
#include <QSqlError>
#include <QSqlRecord>
#include <QSqlIndex>
#include <QSettings>

#include <QLoggingCategory>
Q_DECLARE_LOGGING_CATEGORY(dcEnergyExperience)

EnergyLogger::EnergyLogger(QObject *parent) : EnergyLogs(parent)
{
    if (!initDB()) {
        qCCritical(dcEnergyExperience()) << "Unable to open energy log. Energy logs will not be available.";
        return;
    }

    // Logging configuration
    // Note: SampleRate1Min is always sampled as it is the base series for others
    // Make sure your base series always has enough samples to build a full sample
    // of all series building on it.

    // Disk space considerations;
    // Each entry takes approx 50 bytes for powerBalance + 60 bytes for thingCurrentPower per thing of disk space
    // SQLite adds metadata and overhead of about 5%
    // The resulting database size can be estimated with (count being the sum of all numbers below):
    // (count * 50 bytes) + (count * things * 60 bytes) + 5%
    // ~40000 entries, with 5 energy things => ~15MB
    // Note: use sqlite3_analyzer to see the approx. size per entry in each table.

    // One day 1440 min, let's keep one week
    m_maxMinuteSamples = 10080;

    addConfig(SampleRate15Mins, SampleRate1Min, 16128); // 6 months
    addConfig(SampleRate1Hour, SampleRate15Mins, 8760); // 1 year
    addConfig(SampleRate3Hours, SampleRate15Mins, 2920); // 1 year
    addConfig(SampleRate1Day, SampleRate1Hour, 1095); // 3 years
    addConfig(SampleRate1Week, SampleRate1Day, 168); // 3 years
    addConfig(SampleRate1Month, SampleRate1Day, 240); // 20 years
    addConfig(SampleRate1Year, SampleRate1Month, 20); // 20 years

    // Load last values from thingsPower logs so we have at least one base sample available for sampling, even if a thing might not produce any logs for a while.
    foreach (const ThingId &thingId, loggedThings()) {
        m_thingsPowerLiveLogs[thingId].append(latestLogEntry(SampleRate1Min, thingId));
    }

    // Start the scheduling
    scheduleNextSample(SampleRate1Min);
    foreach (SampleRate sampleRate, m_configs.keys()) {
        scheduleNextSample(sampleRate);
    }

    // Now all the data is initialized. We can start with sampling.

    // First check if we missed any samplings (e.g. because the system was offline at the time when it should have created a sample)
    QDateTime startTime = QDateTime::currentDateTime();
    foreach(SampleRate sampleRate, m_configs.keys()) {
        rectifySamples(sampleRate, m_configs.value(sampleRate).baseSampleRate);
    }
    qCInfo(dcEnergyExperience()) << "Resampled energy DB logs in" << startTime.msecsTo(QDateTime::currentDateTime()) << "ms.";

    // And start the sampler timer
    connect(&m_sampleTimer, &QTimer::timeout, this, &EnergyLogger::sample);
    m_sampleTimer.start(1000);
}

void EnergyLogger::logPowerBalance(double consumption, double production, double acquisition, double storage, double totalConsumption, double totalProduction, double totalAcquisition, double totalReturn)
{
    PowerBalanceLogEntry entry(QDateTime::currentDateTime(), consumption, production, acquisition, storage, totalConsumption, totalProduction, totalAcquisition, totalReturn);

    // Add everything to livelog, keep that for one day, in memory only
    m_balanceLiveLog.prepend(entry);
    while (m_balanceLiveLog.count() > 1 && m_balanceLiveLog.last().timestamp().addDays(1) < QDateTime::currentDateTime()) {
        qCDebug(dcEnergyExperience) << "Discarding livelog entry from" << m_balanceLiveLog.last().timestamp().toString();
        m_balanceLiveLog.removeLast();
    }
}

void EnergyLogger::logThingPower(const ThingId &thingId, double currentPower, double totalConsumption, double totalProduction)
{
    ThingPowerLogEntry entry(QDateTime::currentDateTime(), thingId, currentPower, totalConsumption, totalProduction);

    m_thingsPowerLiveLogs[thingId].prepend(entry);
    while (m_thingsPowerLiveLogs[thingId].count() > 1 && m_thingsPowerLiveLogs[thingId].last().timestamp().addDays(1) < QDateTime::currentDateTime()) {
        qCDebug(dcEnergyExperience()) << "Discarding thing power livelog entry for thing" << thingId << "from" << m_thingsPowerLiveLogs[thingId].last().timestamp().toString();
        m_thingsPowerLiveLogs[thingId].removeLast();
    }
}

PowerBalanceLogEntries EnergyLogger::powerBalanceLogs(SampleRate sampleRate, const QDateTime &from, const QDateTime &to) const
{
    PowerBalanceLogEntries result;

    QSqlQuery query(m_db);
    QString queryString = "SELECT * FROM powerBalance WHERE sampleRate = ?";
    QVariantList bindValues;
    bindValues << sampleRate;
    qCDebug(dcEnergyExperience()) << "Fetching logs. Timestamp:" << from << from.isNull();
    if (!from.isNull()) {
        queryString += " AND timestamp >= ?";
        bindValues << from.toMSecsSinceEpoch();
    }
    if (!to.isNull()) {
        queryString += " AND timestamp <= ?";
        bindValues << to.toMSecsSinceEpoch();
    }
    query.prepare(queryString);
    foreach (const QVariant &bindValue, bindValues) {
        query.addBindValue(bindValue);
    }

    qCDebug(dcEnergyExperience()) << "Executing" << queryString << bindValues;
    query.exec();
    if (query.lastError().isValid()) {
        qCWarning(dcEnergyExperience()) << "Error fetching power balance logs:" << query.lastError() << query.executedQuery();
        return result;
    }

    while (query.next()) {
//        qCDebug(dcEnergyExperience()) << "Adding result";
        result.append(queryResultToBalanceLogEntry(query.record()));
    }
    return result;
}

ThingPowerLogEntries EnergyLogger::thingPowerLogs(SampleRate sampleRate, const QList<ThingId> &thingIds, const QDateTime &from, const QDateTime &to) const
{
    ThingPowerLogEntries result;

    QSqlQuery query(m_db);
    QString queryString = "SELECT * FROM thingPower WHERE sampleRate = ?";
    QVariantList bindValues;
    bindValues << sampleRate;

    qCDebug(dcEnergyExperience()) << "Fetching thing power logs for" << thingIds;

    QStringList thingsQuery;
    foreach (const ThingId &thingId, thingIds) {
        thingsQuery.append("thingId = ?");
        bindValues << thingId;
    }
    if (!thingsQuery.isEmpty()) {
        queryString += " AND (" + thingsQuery.join(" OR ") + " )";
    }

    if (!from.isNull()) {
        queryString += " AND timestamp >= ?";
        bindValues << from.toMSecsSinceEpoch();
    }
    if (!to.isNull()) {
        queryString += " AND timestamp <= ?";
        bindValues << to.toMSecsSinceEpoch();
    }
    query.prepare(queryString);
    foreach (const QVariant &bindValue, bindValues) {
        query.addBindValue(bindValue);
    }
    query.exec();
    if (query.lastError().isValid()) {
        qCWarning(dcEnergyExperience()) << "Error fetching power balance logs:" << query.lastError() << query.executedQuery();
        return result;
    }

    while (query.next()) {
        result.append(ThingPowerLogEntry(
                          QDateTime::fromMSecsSinceEpoch(query.value("timestamp").toLongLong()),
                          query.value("thingId").toUuid(),
                          query.value("currentPower").toDouble(),
                          query.value("totalConsumption").toDouble(),
                          query.value("totalProduction").toDouble()));
    }
    return result;

}

PowerBalanceLogEntry EnergyLogger::latestLogEntry(SampleRate sampleRate)
{
    if (sampleRate == SampleRateAny) {
        if (m_balanceLiveLog.count() > 0) {
            return m_balanceLiveLog.first();
        }
    }
    QSqlQuery query(m_db);
    QString queryString = "SELECT MAX(timestamp) as timestamp, consumption, production, acquisition, storage, totalConsumption, totalProduction, totalAcquisition, totalReturn FROM powerBalance";
    QVariantList bindValues;
    if (sampleRate != SampleRateAny) {
        queryString += " WHERE sampleRate = ?";
        bindValues.append(sampleRate);
    }
    queryString += ";";
    query.prepare(queryString);
    foreach (const QVariant &value, bindValues) {
        query.addBindValue(value);
    }
    query.exec();
    if (query.lastError().isValid()) {
        qCWarning(dcEnergyExperience()) << "Error obtaining latest log entry from DB:" << query.lastError() << query.executedQuery();
        return PowerBalanceLogEntry();
    }
    if (!query.next()) {
        qCDebug(dcEnergyExperience()) << "No power balance log entry in DB for sample rate:" << sampleRate;
        return PowerBalanceLogEntry();
    }
    return queryResultToBalanceLogEntry(query.record());
}

ThingPowerLogEntry EnergyLogger::latestLogEntry(SampleRate sampleRate, const ThingId &thingId)
{
    if (sampleRate == SampleRateAny) {
        if (m_thingsPowerLiveLogs.value(thingId).count() > 0) {
            return m_thingsPowerLiveLogs.value(thingId).first();
        }
    }

    QSqlQuery query(m_db);
    query.prepare("SELECT MAX(timestamp) as timestamp, currentPower, totalConsumption, totalProduction from thingPower WHERE sampleRate = ? AND thingId = ?;");
    query.addBindValue(sampleRate);
    query.addBindValue(thingId);
    if (!query.exec()) {
        qCWarning(dcEnergyExperience()) << "Error fetching latest thing log entry from DB:" << query.lastError() << query.executedQuery();
        return ThingPowerLogEntry();
    }
    if (!query.next()) {
        qCDebug(dcEnergyExperience()) << "No thing power log entry in DB for sample rate:" << sampleRate;
        return ThingPowerLogEntry();
    }
    return queryResultToThingPowerLogEntry(query.record());

}

void EnergyLogger::removeThingLogs(const ThingId &thingId)
{
    m_thingsPowerLiveLogs.remove(thingId);

    QSqlQuery query(m_db);
    query.prepare("DELETE FROM thingPower WHERE thingId = ?;");
    query.addBindValue(thingId);
    query.exec();
    if (query.lastError().isValid()) {
        qCWarning(dcEnergyExperience()) << "Error removing thing energy logs for thing id" << thingId << query.lastError() << query.executedQuery();
    }
}

QList<ThingId> EnergyLogger::loggedThings() const
{
    QList<ThingId> ret;

    QSqlQuery query(m_db);
    query.prepare("SELECT DISTINCT thingId FROM thingPower;");
    query.exec();
    if (query.lastError().isValid()) {
        qCWarning(dcEnergyExperience()) << "Failed to load existing things from logs:" << query.lastError();
    } else {
        while (query.next()) {
            ret.append(query.value("thingId").toUuid());
        }
    }
    return ret;
}

void EnergyLogger::cacheThingEntry(const ThingId &thingId, double totalEnergyConsumed, double totalEnergyProduced)
{
    QSqlQuery query(m_db);
    query.prepare("INSERT OR REPLACE INTO thingCache (thingId, totalEnergyConsumed, totalEnergyProduced) VALUES (?, ?, ?);");
    query.addBindValue(thingId);
    query.addBindValue(totalEnergyConsumed);
    query.addBindValue(totalEnergyProduced);
    query.exec();
    if (query.lastError().isValid()) {
        qCWarning(dcEnergyExperience()) << "Failed to store thing cache entry:" << query.lastError() << query.executedQuery();
    }
}

ThingPowerLogEntry EnergyLogger::cachedThingEntry(const ThingId &thingId)
{
    QSqlQuery query(m_db);
    query.prepare("SELECT * FROM thingCache WHERE thingId = ?;");
    query.addBindValue(thingId);
    query.exec();
    if (query.lastError().isValid()) {
        qCWarning(dcEnergyExperience()) << "Failed to retrieve thing cache entry:" << query.lastError() << query.executedQuery();
        return ThingPowerLogEntry();
    }
    if (!query.next()) {
        qCDebug(dcEnergyExperience()) << "No cached thing entry for" << thingId;
        return ThingPowerLogEntry();
    }
    return ThingPowerLogEntry(QDateTime(), thingId, 0, query.value("totalEnergyConsumed").toDouble(), query.value("totalEnergyProduced").toDouble());
}

void EnergyLogger::sample()
{
    QDateTime now = QDateTime::currentDateTime();

    if (now >= m_nextSamples.value(SampleRate1Min)) {
        QDateTime sampleEnd = m_nextSamples.value(SampleRate1Min);
        QDateTime sampleStart = sampleEnd.addMSecs(-60 * 1000);

        PowerBalanceLogEntry newestInDB = latestLogEntry(SampleRate1Min);

        qCDebug(dcEnergyExperience()) << "Sampling power balance for 1 min from" << sampleStart.toString() << sampleEnd.toString() << "newest in DB:" << newestInDB.timestamp().toString();

        if (newestInDB.timestamp().isValid() && newestInDB.timestamp() < sampleStart) {
            QDateTime oldestTimestamp = sampleStart.addMSecs(-(qint64)m_maxMinuteSamples * 60 * 1000);
            QDateTime timestamp = newestInDB.timestamp();
            if (oldestTimestamp > newestInDB.timestamp()) {
                // In this case we'd be adding m_maxMinuteSamples of 0 values but trim anything before
                // Spare the time by just adding the latest one to keep the totals
                timestamp = sampleStart.addMSecs(-60000);
            }
            qCWarning(dcEnergyExperience()).nospace() << "Clock skew detected. Adding missing samples.";
            QDateTime now = QDateTime::currentDateTime();
            int i = 0;
            m_db.transaction();
            while (timestamp < sampleStart) {
                timestamp = timestamp.addMSecs(60000);
                insertPowerBalance(timestamp, SampleRate1Min, 0, 0, 0, 0, newestInDB.totalConsumption(), newestInDB.totalProduction(), newestInDB.totalAcquisition(), newestInDB.totalReturn());
                i++;
            }
            m_db.commit();
            qCDebug(dcEnergyExperience()) << "Added missing" << i << "minute-samples in" << now.msecsTo(QDateTime::currentDateTime()) << "ms";
        }

        double medianConsumption = 0;
        double medianProduction = 0;
        double medianAcquisition = 0;
        double medianStorage = 0;
        for (int i = 0; i < m_balanceLiveLog.count(); i++) {
            const PowerBalanceLogEntry &entry = m_balanceLiveLog.at(i);
            QDateTime frameStart = (entry.timestamp() < sampleStart) ? sampleStart : entry.timestamp();
            QDateTime frameEnd = i == 0 ? sampleEnd : m_balanceLiveLog.at(i-1).timestamp();
            int frameDuration = frameStart.msecsTo(frameEnd);
            qCDebug(dcEnergyExperience()) << "Frame" << i << "duration:" << frameDuration << "value:" << entry.consumption() << "start" << frameStart.toString() << "end" << frameEnd.toString();

            medianConsumption += entry.consumption() * frameDuration;
            medianProduction += entry.production() * frameDuration;
            medianAcquisition += entry.acquisition() * frameDuration;
            medianStorage += entry.storage() * frameDuration;
            if (entry.timestamp() < sampleStart) {
                break;
            }
        }
        medianConsumption /= sampleStart.msecsTo(sampleEnd);
        medianProduction /= sampleStart.msecsTo(sampleEnd);
        medianAcquisition /= sampleStart.msecsTo(sampleEnd);
        medianStorage /= sampleStart.msecsTo(sampleEnd);

        PowerBalanceLogEntry newest = latestLogEntry(SampleRateAny);
        double totalConsumption = newest.totalConsumption();
        double totalProduction = newest.totalProduction();
        double totalAcquisition = newest.totalAcquisition();
        double totalReturn = newest.totalReturn();

        qCDebug(dcEnergyExperience()) << "Sampled power balance:" << SampleRate1Min << "🔥:" << medianConsumption << "🌞:" << medianProduction << "💵:" << medianAcquisition << "🔋:" << medianStorage << "Totals:" << "🔥:" << totalConsumption << "🌞:" << totalProduction << "💵↓:" << totalAcquisition << "💵↑:" << totalReturn;
        insertPowerBalance(sampleEnd, SampleRate1Min, medianConsumption, medianProduction, medianAcquisition, medianStorage, totalConsumption, totalProduction, totalAcquisition, totalReturn);

        foreach (const ThingId &thingId, m_thingsPowerLiveLogs.keys()) {

            ThingPowerLogEntry newestInDB = latestLogEntry(SampleRate1Min, thingId);
            qCDebug(dcEnergyExperience()) << "Sampling thing power for" << thingId.toString() << SampleRate1Min << "from" << sampleStart.toString() << "to" <<  sampleEnd.toString() << "newest in DB" << newestInDB.timestamp().toString();

            if (newestInDB.timestamp().isValid() && newestInDB.timestamp() < sampleStart) {
                QDateTime oldestTimestamp = sampleStart.addMSecs(-(qint64)m_maxMinuteSamples * 60 * 1000);
                QDateTime timestamp = qMax(newestInDB.timestamp(), oldestTimestamp);
                qCWarning(dcEnergyExperience()).nospace() << "Clock skew detected. Adding missing samples.";
                QDateTime now = QDateTime::currentDateTime();
                int i = 0;
                m_db.transaction();
                while (timestamp < sampleStart) {
                    timestamp = timestamp.addMSecs(60000);
                    insertThingPower(timestamp, SampleRate1Min, thingId, 0, newestInDB.totalConsumption(), newestInDB.totalProduction());
                    i++;
                }
                m_db.commit();
                qCDebug(dcEnergyExperience()) << "Added missing" << i << "minute-samples in" << now.msecsTo(QDateTime::currentDateTime()) << "ms";
            }

            double medianPower = 0;
            ThingPowerLogEntries entries = m_thingsPowerLiveLogs.value(thingId);
            for (int i = 0; i < entries.count(); i++) {
                const ThingPowerLogEntry &entry = entries.at(i);
                QDateTime frameStart = (entry.timestamp() < sampleStart) ? sampleStart : entry.timestamp();
                QDateTime frameEnd = i == 0 ? sampleEnd : entries.at(i-1).timestamp();
                int frameDuration = frameStart.msecsTo(frameEnd);
                qCDebug(dcEnergyExperience()) << "Frame" << i << "duration:" << frameDuration << "value:" << entry.currentPower();
                medianPower += entry.currentPower() * frameDuration;
                if (entry.timestamp() < sampleStart) {
                    break;
                }
            }
            medianPower /= sampleStart.msecsTo(sampleEnd);

            ThingPowerLogEntry newest = latestLogEntry(SampleRateAny, thingId);
            double totalConsumption = newest.totalConsumption();
            double totalProduction = newest.totalProduction();

            qCDebug(dcEnergyExperience()) << "Sampled thing power for" << thingId << SampleRate1Min << "🔥/🌞:" << medianPower << "Totals:" << "🔥:" << totalConsumption << "🌞:" << totalProduction;
            insertThingPower(sampleEnd, SampleRate1Min, thingId, medianPower, totalConsumption, totalProduction);
        }
    }

    // First sample all the configs.
    foreach (SampleRate sampleRate, m_configs.keys()) {
        if (now >= m_nextSamples.value(sampleRate)) {
            QDateTime sampleTime = m_nextSamples.value(sampleRate);
            SampleRate baseSampleRate = m_configs.value(sampleRate).baseSampleRate;
            QDateTime sampleStart = calculateSampleStart(sampleTime, sampleRate);
            QDateTime newestInDB = getNewestPowerBalanceSampleTimestamp(sampleRate);

            if (newestInDB < sampleStart) {
                qCWarning(dcEnergyExperience()) << "Clock skew detected. Recovering samples...";
                rectifySamples(sampleRate, baseSampleRate);
            }

            samplePowerBalance(sampleRate, baseSampleRate, sampleTime);
            sampleThingsPower(sampleRate, baseSampleRate, sampleTime);
        }
    }

    // and then trim them
    if (now > m_nextSamples.value(SampleRate1Min)) {
        QDateTime sampleTime = m_nextSamples.value(SampleRate1Min);
        QDateTime oldestTimestamp = sampleTime.addMSecs(-(qint64)m_maxMinuteSamples * 60 * 1000);
        trimPowerBalance(SampleRate1Min, oldestTimestamp);
        foreach (const ThingId &thingId, m_thingsPowerLiveLogs.keys()) {
            trimThingPower(thingId, SampleRate1Min, oldestTimestamp);
        }
    }
    foreach (SampleRate sampleRate, m_configs.keys()) {
        if (now >= m_nextSamples.value(sampleRate)) {
            uint maxSamples = m_configs.value(sampleRate).maxSamples;
            QDateTime sampleTime = m_nextSamples.value(sampleRate);
            QDateTime oldestTimestamp = calculateSampleStart(sampleTime, sampleRate, maxSamples);
            trimPowerBalance(sampleRate, oldestTimestamp);
            foreach (const ThingId &thingId, m_thingsPowerLiveLogs.keys()) {
                trimThingPower(thingId, sampleRate, oldestTimestamp);
            }
        }
    }

    // Lastly we reschedule the next sample for each config
    // Note: keep this at the end as the previous stuff uses the schedule to work
    if (now > m_nextSamples.value(SampleRate1Min)) {
        scheduleNextSample(SampleRate1Min);
    }
    foreach (SampleRate sampleRate, m_configs.keys()) {
        if (now >= m_nextSamples.value(sampleRate)) {
            scheduleNextSample(sampleRate);
        }
    }
}

bool EnergyLogger::initDB()
{
    m_db.close();

    m_db = QSqlDatabase::addDatabase("QSQLITE", "energylogs");
    QDir path = QDir(NymeaSettings::storagePath());
    if (!path.exists()) {
        path.mkpath(path.path());
    }
    m_db.setDatabaseName(path.filePath("energylogs.sqlite"));

    bool opened = m_db.open();
    if (!opened) {
        qCWarning(dcEnergyExperience()) << "Cannot open energy log DB at" << m_db.databaseName() << m_db.lastError();
        return false;
    }

    if (!m_db.tables().contains("metadata")) {
        qCDebug(dcEnergyExperience()) << "No \"metadata\" table in database. Creating it.";
        m_db.exec("CREATE TABLE metadata (version INT);");
        m_db.exec("INSERT INTO metadata (version) VALUES (1);");

        if (m_db.lastError().isValid()) {
            qCWarning(dcEnergyExperience()) << "Error creating metadata table in energy log database. Driver error:" << m_db.lastError().driverText() << "Database error:" << m_db.lastError().databaseText();
            return false;
        }
    }

    if (!m_db.tables().contains("powerBalance")) {
        qCDebug(dcEnergyExperience()) << "No \"powerBalance\" table in database. Creating it.";
        m_db.exec("CREATE TABLE powerBalance "
                  "("
                  "timestamp BIGINT,"
                  "sampleRate INT,"
                  "consumption FLOAT,"
                  "production FLOAT,"
                  "acquisition FLOAT,"
                  "storage FLOAT,"
                  "totalConsumption FLOAT,"
                  "totalProduction FLOAT,"
                  "totalAcquisition FLOAT,"
                  "totalReturn FLOAT"
                  ");");

        if (m_db.lastError().isValid()) {
            qCWarning(dcEnergyExperience()) << "Error creating powerBalance table in energy log database. Driver error:" << m_db.lastError().driverText() << "Database error:" << m_db.lastError().databaseText();
            return false;
        }
    }
    m_db.exec("CREATE INDEX IF NOT EXISTS idx_powerBalance ON powerBalance(sampleRate, timestamp);");
    if (m_db.lastError().isValid()) {
        qCWarning(dcEnergyExperience()) << "Error creating powerBalance table index in energy log database. Driver error:" << m_db.lastError().driverText() << "Database error:" << m_db.lastError().databaseText();
        return false;
    }

    if (!m_db.tables().contains("thingPower")) {
        qCDebug(dcEnergyExperience()) << "No \"thingPower\" table in database. Creating it.";
        m_db.exec("CREATE TABLE thingPower "
                  "("
                  "timestamp BIGINT,"
                  "sampleRate INT,"
                  "thingId VARCHAR(38),"
                  "currentPower FLOAT,"
                  "totalConsumption FLOAT,"
                  "totalProduction FLOAT"
                  ");");
        if (m_db.lastError().isValid()) {
            qCWarning(dcEnergyExperience()) << "Error creating thingPower table in energy log database. Driver error:" << m_db.lastError().driverText() << "Database error:" << m_db.lastError().databaseText();
            return false;
        }
    }
    m_db.exec("CREATE INDEX IF NOT EXISTS idx_thingPower ON thingPower(thingId, sampleRate, timestamp);");
    if (m_db.lastError().isValid()) {
        qCWarning(dcEnergyExperience()) << "Error creating thingPower table index in energy log database. Driver error:" << m_db.lastError().driverText() << "Database error:" << m_db.lastError().databaseText();
        return false;
    }

    if (!m_db.tables().contains("thingCache")) {
        qCDebug(dcEnergyExperience()) << "No \"thingCache\" table in database. Creating it.";
        m_db.exec("CREATE TABLE thingCache "
                  "("
                  "thingId VARCHAR(38) PRIMARY KEY,"
                  "totalEnergyConsumed FLOAT,"
                  "totalEnergyProduced FLOAT"
                  ");");
        if (m_db.lastError().isValid()) {
            qCWarning(dcEnergyExperience()) << "Error creating thingCache table in energy log database. Driver error:" << m_db.lastError().driverText() << "Database error:" << m_db.lastError().databaseText();
            return false;
        }
    }

    qCDebug(dcEnergyExperience()) << "Initialized logging DB successfully." << m_db.databaseName();
    return true;
}

void EnergyLogger::addConfig(SampleRate sampleRate, SampleRate baseSampleRate, int maxSamples)
{
    SampleConfig config;
    config.baseSampleRate = baseSampleRate;
    config.maxSamples = maxSamples;
    m_configs.insert(sampleRate, config);
}

QDateTime EnergyLogger::getOldestPowerBalanceSampleTimestamp(SampleRate sampleRate)
{
    QSqlQuery query(m_db);
    query.prepare("SELECT MIN(timestamp) AS oldestTimestamp FROM powerBalance WHERE sampleRate = ?;");
    query.addBindValue(sampleRate);
    query.exec();
    if (query.next() && !query.value("oldestTimestamp").isNull()) {
        return QDateTime::fromMSecsSinceEpoch(query.value("oldestTimestamp").toLongLong());
    }
    return QDateTime();
}

QDateTime EnergyLogger::getNewestPowerBalanceSampleTimestamp(SampleRate sampleRate)
{
    QSqlQuery query(m_db);
    query.prepare("SELECT MAX(timestamp) AS latestTimestamp FROM powerBalance WHERE sampleRate = ?;");
    query.addBindValue(sampleRate);
    query.exec();
    if (query.next() && !query.value("latestTimestamp").isNull()) {
        return QDateTime::fromMSecsSinceEpoch(query.value("latestTimestamp").toLongLong());
    }
    return QDateTime();
}

QDateTime EnergyLogger::getOldestThingPowerSampleTimestamp(const ThingId &thingId, SampleRate sampleRate)
{
    QSqlQuery query(m_db);
    query.prepare("SELECT MIN(timestamp) AS oldestTimestamp FROM thingPower WHERE thingId = ? AND sampleRate = ?;");
    query.addBindValue(thingId);
    query.addBindValue(sampleRate);
    query.exec();
    if (query.next() && !query.value("oldestTimestamp").isNull()) {
        return QDateTime::fromMSecsSinceEpoch(query.value("oldestTimestamp").toLongLong());
    }
    return QDateTime();
}

QDateTime EnergyLogger::getNewestThingPowerSampleTimestamp(const ThingId &thingId, SampleRate sampleRate)
{
    QSqlQuery query(m_db);
    query.prepare("SELECT MAX(timestamp) AS newestTimestamp FROM thingPower WHERE thingId = ? AND sampleRate = ?;");
    query.addBindValue(thingId);
    query.addBindValue(sampleRate);
    query.exec();
    if (query.next() && !query.value("newestTimestamp").isNull()) {
        return QDateTime::fromMSecsSinceEpoch(query.value("newestTimestamp").toLongLong());
    }
    return QDateTime();
}

void EnergyLogger::scheduleNextSample(SampleRate sampleRate)
{
    QDateTime next = nextSampleTimestamp(sampleRate, QDateTime::currentDateTime());
    m_nextSamples.insert(sampleRate, next);
    qCDebug(dcEnergyExperience()) << "Next sample for" << sampleRate << "scheduled at" << next.toString();
}

QDateTime EnergyLogger::calculateSampleStart(const QDateTime &sampleEnd, SampleRate sampleRate, int sampleCount)
{
    if (sampleRate == SampleRate1Month) {
        return sampleEnd.addMonths(-sampleCount);
    } else if (sampleRate == SampleRate1Year) {
        return sampleEnd.addYears(-sampleCount);
    }
    return sampleEnd.addMSecs(-(quint64)sampleCount * sampleRate * 60 * 1000);
}

void EnergyLogger::rectifySamples(SampleRate sampleRate, SampleRate baseSampleRate)
{
    // Normally we'd need to find the newest available sample of a series and catch up from there.
    // However, it could happen a series does not have any samples at all yet. For example if we're logging since january,
    // and at new years the system was off, we missed the new years yearly sample and don't have any earlier. For those cases
    // we need to start resampling from the oldest timestamp we find in the DB for the base sampleRate.
    QDateTime oldestBaseSample = getOldestPowerBalanceSampleTimestamp(baseSampleRate);
    QDateTime newestSample = getNewestPowerBalanceSampleTimestamp(sampleRate);

    QDateTime startTime = QDateTime::currentDateTime();
    qCDebug(dcEnergyExperience()) << "Checking for missing power balance samples for" << sampleRate;
    qCDebug(dcEnergyExperience()) << "Newest sample:" << newestSample.toString() << "Oldest base sample:" << oldestBaseSample.toString();
    if (newestSample.isNull()) {
        qCDebug(dcEnergyExperience()) << "No sample at all so far. Using base as starting point.";
        newestSample = oldestBaseSample;
    }

//    qCDebug(dcEnergyExperience()) << "next sample after last in series:" << nextSampleTimestamp(sampleRate, newestSample).toString();
//    qCDebug(dcEnergyExperience()) << "next scheduled sample:" << m_nextSamples.value(sampleRate).toString();
    if (!newestSample.isNull() && nextSampleTimestamp(sampleRate, newestSample) < m_nextSamples[sampleRate]) {
        // regularly sample one sample as there may be some valid samples in the base series
        QDateTime nextSample = nextSampleTimestamp(sampleRate, newestSample.addMSecs(1000));
        samplePowerBalance(sampleRate, baseSampleRate, nextSample);
        newestSample = nextSample;
    }
    // Now retrieve the last sample and just carry over totals
    PowerBalanceLogEntry latest = latestLogEntry(sampleRate);
    // We only need to rectify up to maxSamples, so don't bother with older ones
    if (!newestSample.isNull()) {
        newestSample = qMax(newestSample, calculateSampleStart(m_nextSamples[sampleRate], sampleRate, m_configs.value(sampleRate).maxSamples));
    }

    m_db.transaction();
    int count = 0;
    while (!newestSample.isNull() && nextSampleTimestamp(sampleRate, newestSample) < m_nextSamples[sampleRate]) {
        QDateTime nextSample = nextSampleTimestamp(sampleRate, newestSample.addMSecs(1000));
        insertPowerBalance(nextSample, sampleRate, 0, 0, 0, 0, latest.totalConsumption(), latest.totalProduction(), latest.totalAcquisition(), latest.totalReturn());
        newestSample = nextSample;
        count++;
    }
    m_db.commit();
    qCDebug(dcEnergyExperience()) << "Done rectifying" << count << "power balance samples for" << sampleRate << "in" << startTime.msecsTo(QDateTime::currentDateTime()) << "ms";

    foreach (const ThingId &thingId, m_thingsPowerLiveLogs.keys()) {
        QDateTime oldestBaseSample = getOldestThingPowerSampleTimestamp(thingId, baseSampleRate);
        QDateTime newestSample = getNewestThingPowerSampleTimestamp(thingId, sampleRate);

        QDateTime startTime = QDateTime::currentDateTime();
        qCDebug(dcEnergyExperience()) << "Checking for missing thing samples for" << sampleRate << "for thing" << thingId;
        qCDebug(dcEnergyExperience()) << "Newest sample:" << newestSample.toString() << "Oldest base sample:" << oldestBaseSample.toString();
        if (newestSample.isNull()) {
            qCDebug(dcEnergyExperience()) << "No sample at all so far. Using base as starting point.";
            if (oldestBaseSample.isNull()) {
                qCDebug(dcEnergyExperience()) << "Base series doesn't have any samples either. Skipping resampling for" << sampleRate << "for" << thingId;
                continue;
            }
            newestSample = oldestBaseSample;
        }
//        qCDebug(dcEnergyExperience()) << "T next sample after last in series:" << nextSampleTimestamp(sampleRate, newestSample).toString();
//        qCDebug(dcEnergyExperience()) << "T next scheduled sample:" << m_nextSamples.value(sampleRate).toString();
        if (!newestSample.isNull() && nextSampleTimestamp(sampleRate, newestSample) < m_nextSamples[sampleRate]) {
            QDateTime nextSample = nextSampleTimestamp(sampleRate, newestSample.addMSecs(1000));
            sampleThingPower(thingId, sampleRate, baseSampleRate, nextSample);
            newestSample = nextSample;
        }
        // Now retrieve the last sample and just carry over totals
        ThingPowerLogEntry latest = latestLogEntry(sampleRate, thingId);
        // We only need to rectify up to maxSamples, so don't bother with older ones
        newestSample = qMax(newestSample, calculateSampleStart(m_nextSamples[sampleRate], sampleRate, m_configs.value(sampleRate).maxSamples));

        m_db.transaction();
        int count = 0;
        while (!newestSample.isNull() && nextSampleTimestamp(sampleRate, newestSample) < m_nextSamples[sampleRate]) {
            QDateTime nextSample = nextSampleTimestamp(sampleRate, newestSample.addMSecs(1000));
            insertThingPower(nextSample, sampleRate, thingId, 0, latest.totalConsumption(), latest.totalProduction());
            newestSample = nextSample;
            count++;
        }
        m_db.commit();
        qCDebug(dcEnergyExperience()) << "Done rectifying" << count << "thing power samples for" << sampleRate << " for" << thingId << "in" << startTime.msecsTo(QDateTime::currentDateTime()) << "ms";
    }
}

QDateTime EnergyLogger::nextSampleTimestamp(SampleRate sampleRate, const QDateTime &dateTime)
{
    QTime time = dateTime.time();
    QDate date = dateTime.date();
    QDateTime next;
    switch (sampleRate) {
    case SampleRateAny:
        qCWarning(dcEnergyExperience()) << "Cannot calculate next sample timestamp without a sample rate";
        return QDateTime();
    case SampleRate1Min:
        time.setHMS(time.hour(), time.minute(), 0);
        next = QDateTime(date, time).addMSecs(60 * 1000);
        break;
    case SampleRate15Mins:
        time.setHMS(time.hour(), time.minute() - (time.minute() % 15), 0);
        next = QDateTime(date, time).addMSecs(15 * 60 * 1000);
        break;
    case SampleRate1Hour:
        time.setHMS(time.hour(), 0, 0);
        next = QDateTime(date, time).addMSecs(60 * 60 * 1000);
        break;
    case SampleRate3Hours:
        time.setHMS(time.hour() - (time.hour() % 3), 0, 0);
        next = QDateTime(date, time).addMSecs(3 * 60 * 60 * 1000);
        if (next.time().hour() == 2) {
            qCDebug(dcEnergyExperience()) << "DST switch detected!";
            next = next.addMSecs(60 * 60 * 1000);
        }
        break;
    case SampleRate1Day:
        next = QDateTime(date, QTime()).addDays(1);
        break;
    case SampleRate1Week:
        date = date.addDays(-date.dayOfWeek() + 1);
        next = QDateTime(date, QTime()).addDays(7);
        break;
    case SampleRate1Month:
        date = date.addDays(-date.day() + 1);
        next = QDateTime(date, QTime()).addMonths(1);
        break;
    case SampleRate1Year:
        date.setDate(date.year(), 1, 1);
        next = QDateTime(date, QTime()).addYears(1);
        break;
    }

    return next;
}

bool EnergyLogger::samplePowerBalance(SampleRate sampleRate, SampleRate baseSampleRate, const QDateTime &sampleEnd)
{
    QDateTime sampleStart = calculateSampleStart(sampleEnd, sampleRate);

    qCDebug(dcEnergyExperience()) << "Sampling power balance" << sampleRate << "from" << sampleStart << "to" << sampleEnd;

    double medianConsumption = 0;
    double medianProduction = 0;
    double medianAcquisition = 0;
    double medianStorage = 0;
    double totalConsumption = 0;
    double totalProduction = 0;
    double totalAcquisition = 0;
    double totalReturn = 0;

    QSqlQuery query(m_db);
    query.prepare("SELECT * FROM powerBalance WHERE sampleRate = ? AND timestamp > ? AND timestamp <= ?;");
    query.addBindValue(baseSampleRate);
    query.addBindValue(sampleStart.toMSecsSinceEpoch());
    query.addBindValue(sampleEnd.toMSecsSinceEpoch());
    query.exec();

    if (query.lastError().isValid()) {
        qCWarning(dcEnergyExperience()) << "Error fetching power balance samples for" << baseSampleRate << "from" << sampleStart.toString() << "to" << sampleEnd.toString();
        qCWarning(dcEnergyExperience()) << "SQL error was:" << query.lastError() << "executed query:" << query.executedQuery();
        return false;
    }

    int resultCount = 0;
    while (query.next()) {
        resultCount++;
        qCDebug(dcEnergyExperience()) << "Frame:" << QDateTime::fromMSecsSinceEpoch(query.value("timestamp").toLongLong()).toString() << query.value("consumption").toDouble() << query.value("production").toDouble() << query.value("acquisition").toDouble() << query.value("storage").toDouble() << query.value("totalConsumption").toDouble() << query.value("totalProduction").toDouble() << query.value("totalAcquisition").toDouble() << query.value("totalReturn").toDouble();
        medianConsumption += query.value("consumption").toDouble();
        medianProduction += query.value("production").toDouble();
        medianAcquisition += query.value("acquisition").toDouble();
        medianStorage += query.value("storage").toDouble();
        totalConsumption = query.value("totalConsumption").toDouble();
        totalProduction = query.value("totalProduction").toDouble();
        totalAcquisition = query.value("totalAcquisition").toDouble();
        totalReturn = query.value("totalReturn").toDouble();
    }
    if (resultCount > 0) {
        medianConsumption = medianConsumption * baseSampleRate / sampleRate;
        medianProduction = medianProduction * baseSampleRate / sampleRate;
        medianAcquisition = medianAcquisition * baseSampleRate / sampleRate;
        medianStorage = medianStorage * baseSampleRate / sampleRate;

    } else {
        // If there are no base samples for the given time frame at all, let's try to find the last existing one in the base
        // to at least copy the totals from where we left off.

        query = QSqlQuery(m_db);
        query.prepare("SELECT MAX(timestamp), consumption, production, acquisition, storage, totalConsumption, totalProduction, totalAcquisition, totalReturn FROM powerBalance WHERE sampleRate = ?;");
        query.addBindValue(baseSampleRate);
        query.exec();
        if (query.lastError().isValid()) {
            qCWarning(dcEnergyExperience()) << "Error fetching newest power balance sample for" << baseSampleRate;
            qCWarning(dcEnergyExperience()) << "SQL error was:" << query.lastError() << "executed query:" << query.executedQuery();
            return false;
        }

        if (query.next()) {
            totalConsumption = query.value("totalConsumption").toDouble();
            totalProduction = query.value("totalProduction").toDouble();
            totalAcquisition = query.value("totalAcquisition").toDouble();
            totalReturn = query.value("totalReturn").toDouble();
        }
    }


    qCDebug(dcEnergyExperience()) << "Sampled:" << "🔥:" << medianConsumption << "🌞:" << medianProduction << "💵:" << medianAcquisition << "🔋:" << medianStorage << "Totals:" << "🔥:" << totalConsumption << "🌞:" << totalProduction << "💵↓:" << totalAcquisition << "💵↑:" << totalReturn;
    return insertPowerBalance(sampleEnd, sampleRate, medianConsumption, medianProduction, medianAcquisition, medianStorage, totalConsumption, totalProduction, totalAcquisition, totalReturn);
}

bool EnergyLogger::insertPowerBalance(const QDateTime &timestamp, SampleRate sampleRate, double consumption, double production, double acquisition, double storage, double totalConsumption, double totalProduction, double totalAcquisition, double totalReturn)
{
    QSqlQuery query = QSqlQuery(m_db);
    query.prepare("INSERT INTO powerBalance (timestamp, sampleRate, consumption, production, acquisition, storage, totalConsumption, totalProduction, totalAcquisition, totalReturn) values (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);");
    query.addBindValue(timestamp.toMSecsSinceEpoch());
    query.addBindValue(sampleRate);
    query.addBindValue(consumption);
    query.addBindValue(production);
    query.addBindValue(acquisition);
    query.addBindValue(storage);
    query.addBindValue(totalConsumption);
    query.addBindValue(totalProduction);
    query.addBindValue(totalAcquisition);
    query.addBindValue(totalReturn);
    query.exec();
    if (query.lastError().isValid()) {
        qCWarning(dcEnergyExperience()) << "Error logging consumption sample:" << query.lastError() << query.executedQuery();
        return false;
    }
    emit powerBalanceEntryAdded(sampleRate, PowerBalanceLogEntry(timestamp, consumption, production, acquisition, storage, totalConsumption, totalProduction, totalAcquisition, totalReturn));
    return true;
}

bool EnergyLogger::sampleThingsPower(SampleRate sampleRate, SampleRate baseSampleRate, const QDateTime &sampleEnd)
{
    bool ret = true;
    foreach (const ThingId &thingId, m_thingsPowerLiveLogs.keys()) {
        ret &= sampleThingPower(thingId, sampleRate, baseSampleRate, sampleEnd);
    }
    return ret;
}

bool EnergyLogger::sampleThingPower(const ThingId &thingId, SampleRate sampleRate, SampleRate baseSampleRate, const QDateTime &sampleEnd)
{
    QDateTime sampleStart = calculateSampleStart(sampleEnd, sampleRate);
    qCDebug(dcEnergyExperience()) << "Sampling thing power for" << thingId.toString() << sampleRate << "from" << sampleStart.toString() << "to" << sampleEnd.toString();

    double medianCurrentPower = 0;
    double totalConsumption = 0;
    double totalProduction = 0;

    QSqlQuery query(m_db);
    query.prepare("SELECT * FROM thingPower WHERE thingId = ? AND sampleRate = ? AND timestamp > ? AND timestamp <= ?;");
    query.addBindValue(thingId);
    query.addBindValue(baseSampleRate);
    query.addBindValue(sampleStart.toMSecsSinceEpoch());
    query.addBindValue(sampleEnd.toMSecsSinceEpoch());
    query.exec();

    if (query.lastError().isValid()) {
        qCWarning(dcEnergyExperience()) << "Error fetching thing power samples for" << baseSampleRate << "from" << sampleStart.toString() << "to" << sampleEnd.toString();
        qCWarning(dcEnergyExperience()) << "SQL error was:" << query.lastError() << "executed query:" << query.executedQuery();
        return false;
    }

    qCDebug(dcEnergyExperience()) << "Query:" << query.executedQuery();
    qCDebug(dcEnergyExperience()) << "Results:" << query.size();

    int resultCount = 0;
    while (query.next()) {
        resultCount++;
        qCDebug(dcEnergyExperience()) << "Frame:" << query.value("currentPower").toDouble() << QDateTime::fromMSecsSinceEpoch(query.value("timestamp").toLongLong()).toString();
        medianCurrentPower += query.value("currentPower").toDouble();
        totalConsumption = query.value("totalConsumption").toDouble();
        totalProduction = query.value("totalProduction").toDouble();
    }
    if (resultCount > 0) {
        medianCurrentPower = medianCurrentPower * baseSampleRate / sampleRate;

    } else {
        // If there are no base samples for the given time frame at all, let's try to find the last existing one in the base
        // to at least copy the totals from where we left off.

        query = QSqlQuery(m_db);
        query.prepare("SELECT MAX(timestamp), currentPower, totalConsumption, totalProduction FROM thingPower WHERE thingId = ? AND sampleRate = ?;");
        query.addBindValue(thingId);
        query.addBindValue(baseSampleRate);
        query.exec();
        if (query.lastError().isValid()) {
            qCWarning(dcEnergyExperience()) << "Error fetching newest thing power sample for" << thingId.toString() << baseSampleRate;
            qCWarning(dcEnergyExperience()) << "SQL error was:" << query.lastError() << "executed query:" << query.executedQuery();
            return false;
        }

        if (query.next()) {
            totalConsumption = query.value("totalConsumption").toDouble();
            totalProduction = query.value("totalProduction").toDouble();
        }
    }


    qCDebug(dcEnergyExperience()) << "Sampled:" << thingId.toString() << sampleRate << "median currentPower:" << medianCurrentPower << "total consumption:" << totalConsumption << "total production:" << totalProduction;
    return insertThingPower(sampleEnd, sampleRate, thingId, medianCurrentPower, totalConsumption, totalProduction);
}

bool EnergyLogger::insertThingPower(const QDateTime &timestamp, SampleRate sampleRate, const ThingId &thingId, double currentPower, double totalConsumption, double totalProduction)
{
    QSqlQuery query = QSqlQuery(m_db);
    query.prepare("INSERT INTO thingPower (timestamp, sampleRate, thingId, currentPower, totalConsumption, totalProduction) values (?, ?, ?, ?, ?, ?);");
    query.addBindValue(timestamp.toMSecsSinceEpoch());
    query.addBindValue(sampleRate);
    query.addBindValue(thingId);
    query.addBindValue(currentPower);
    query.addBindValue(totalConsumption);
    query.addBindValue(totalProduction);
    query.exec();
    if (query.lastError().isValid()) {
        qCWarning(dcEnergyExperience()) << "Error logging thing power sample:" << query.lastError() << query.executedQuery();
        return false;
    }
    emit thingPowerEntryAdded(sampleRate, ThingPowerLogEntry(timestamp, thingId, currentPower, totalConsumption, totalProduction));
    return true;
}

void EnergyLogger::trimPowerBalance(SampleRate sampleRate, const QDateTime &beforeTime)
{
    QSqlQuery query(m_db);
    query.prepare("DELETE FROM powerBalance WHERE sampleRate = ? AND timestamp < ?;");
    query.addBindValue(sampleRate);
    query.addBindValue(beforeTime.toMSecsSinceEpoch());
    query.exec();
    if (query.numRowsAffected() > 0) {
        qCDebug(dcEnergyExperience()).nospace() << "Trimmed " << query.numRowsAffected() << " from power balance series: " << sampleRate << " (Older than: " << beforeTime.toString() << ")";
    }
}

void EnergyLogger::trimThingPower(const ThingId &thingId, SampleRate sampleRate, const QDateTime &beforeTime)
{
    QSqlQuery query(m_db);
    query.prepare("DELETE FROM thingPower WHERE thingId = ? AND sampleRate = ? AND timestamp < ?;");
    query.addBindValue(thingId);
    query.addBindValue(sampleRate);
    query.addBindValue(beforeTime.toMSecsSinceEpoch());
    query.exec();
    if (query.numRowsAffected() > 0) {
        qCDebug(dcEnergyExperience()).nospace() << "Trimmed " << query.numRowsAffected() << " from thing power series for: " << thingId << sampleRate << " (Older than: " << beforeTime.toString() << ")";
    }
}

PowerBalanceLogEntry EnergyLogger::queryResultToBalanceLogEntry(const QSqlRecord &record) const
{
    return PowerBalanceLogEntry(QDateTime::fromMSecsSinceEpoch(record.value("timestamp").toLongLong()),
                                record.value("consumption").toDouble(),
                                record.value("production").toDouble(),
                                record.value("acquisition").toDouble(),
                                record.value("storage").toDouble(),
                                record.value("totalConsumption").toDouble(),
                                record.value("totalProduction").toDouble(),
                                record.value("totalAcquisition").toDouble(),
                                record.value("totalReturn").toDouble());

}

ThingPowerLogEntry EnergyLogger::queryResultToThingPowerLogEntry(const QSqlRecord &record) const
{
    return ThingPowerLogEntry(QDateTime::fromMSecsSinceEpoch(record.value("timestamp").toULongLong()),
                              record.value("thingId").toUuid(),
                              record.value("currentPower").toDouble(),
                              record.value("totalConsumption").toDouble(),
                              record.value("totalProduction").toDouble());
}
