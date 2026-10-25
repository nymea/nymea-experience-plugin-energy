// SPDX-License-Identifier: GPL-3.0-or-later

#include <QtCore>
#include <QtSql>
#include <QtTest>
#include <memory>
#include <nymeasettings.h>
#include <typeutils.h>
#include "energylogs.h"

// Expose only EnergyLogger's internals, after loading its dependency headers.
// Including the implementation also exercises the actual maintenance writers.
#define private public
#include "energylogger.h"
#undef private
#include "energylogger.cpp"

Q_LOGGING_CATEGORY(dcEnergyExperience, "EnergyExperience")

class TestEnergyLogger : public QObject
{
    Q_OBJECT

    std::unique_ptr<QTemporaryDir> m_directory;
    std::unique_ptr<EnergyLogger> m_logger;
    QByteArray m_previousConfigPath;
    bool m_hadConfigPath = false;
    // Keep fixtures ahead of startup recovery, regardless of when the suite runs.
    const QDateTime m_timestamp = QDateTime::fromMSecsSinceEpoch(
                (QDateTime::currentDateTimeUtc().addYears(1).toMSecsSinceEpoch() / 60000) * 60000);
    const ThingId m_thing = ThingId::createThingId();

    bool insertMain(double value, const QDateTime &timestamp, EnergyLogs::SampleRate rate = EnergyLogs::SampleRate1Min)
    {
        return m_logger->insertPowerBalance(timestamp, rate, value, 2, 3, 4, value + 10, 6, 7, 8)
                && m_logger->insertThingPower(timestamp, rate, m_thing, value, value + 10, 9);
    }

    bool insertMaintenance(double value, const QDateTime &timestamp, EnergyLogs::SampleRate rate = EnergyLogs::SampleRate1Min)
    {
        return maintenanceInsertPowerBalance(m_logger->m_db, timestamp, rate, value, 2, 3, 4, value + 10, 6, 7, 8)
                && maintenanceInsertThingPower(m_logger->m_db, timestamp, rate, m_thing, value, value + 10, 9);
    }

    QVariant scalar(const QString &sql)
    {
        QSqlQuery query(m_logger->m_db);
        if (!query.exec(sql) || !query.next()) {
            QTest::qFail(qPrintable(query.lastError().text()), __FILE__, __LINE__);
            return QVariant();
        }
        return query.value(0);
    }

    void stopLogger()
    {
        m_logger.reset();
        QSqlDatabase::removeDatabase(QStringLiteral("energylogs"));
    }

    void openLogger()
    {
        // Check the linked libnymea, not just its source, before any constructor I/O.
        QCOMPARE(QDir(NymeaSettings::storagePath()).absolutePath(), m_directory->path());
        m_logger.reset(new EnergyLogger);
        m_logger->m_sampleTimer.stop();
        QVERIFY(m_logger->m_db.isOpen());
        QTRY_VERIFY_WITH_TIMEOUT(!m_logger->m_dbMaintenanceRunning, 5000);
    }

private slots:
    void init()
    {
        m_hadConfigPath = qEnvironmentVariableIsSet("NYMEA_CONFIG_PATH");
        m_previousConfigPath = qgetenv("NYMEA_CONFIG_PATH");
        m_directory.reset(new QTemporaryDir(QDir::tempPath() + "/nymea-test-energylogger-XXXXXX"));
        QVERIFY(m_directory->isValid());
        qputenv("NYMEA_CONFIG_PATH", m_directory->path().toLocal8Bit());
        openLogger();
    }

    void cleanup()
    {
        stopLogger();
        if (m_hadConfigPath)
            qputenv("NYMEA_CONFIG_PATH", m_previousConfigPath);
        else
            qunsetenv("NYMEA_CONFIG_PATH");
        m_directory.reset();
    }

    void duplicateInsertsAndSignals()
    {
        QSignalSpy balance(m_logger.get(), &EnergyLogs::powerBalanceEntryAdded);
        QSignalSpy things(m_logger.get(), &EnergyLogs::thingPowerEntryAdded);
        QVERIFY(balance.isValid());
        QVERIFY(things.isValid());
        QVERIFY(insertMain(11, m_timestamp));
        QVERIFY(insertMain(99, m_timestamp));
        QVERIFY(insertMaintenance(98, m_timestamp));
        QCOMPARE(balance.count(), 1);
        QCOMPARE(things.count(), 1);
        QCOMPARE(scalar("SELECT consumption FROM powerBalance").toDouble(), 11.0);
        QCOMPARE(scalar("SELECT currentPower FROM thingPower").toDouble(), 11.0);
        QCOMPARE(scalar("SELECT totalConsumption FROM thingPower").toDouble(), 21.0);

        // Maintenance first, then a main-thread duplicate: no spurious signals.
        QVERIFY(insertMaintenance(22, m_timestamp.addSecs(60)));
        QVERIFY(insertMaintenance(99, m_timestamp.addSecs(60)));
        QVERIFY(insertMain(99, m_timestamp.addSecs(60)));
        QCOMPARE(balance.count(), 1);
        QCOMPARE(things.count(), 1);
        QCOMPARE(scalar("SELECT COUNT(*) FROM powerBalance").toInt(), 2);
        QCOMPARE(scalar("SELECT COUNT(*) FROM thingPower").toInt(), 2);

        // Same timestamp in another rate or for another thing is a distinct key.
        QVERIFY(insertMain(33, m_timestamp, EnergyLogs::SampleRate15Mins));
        QVERIFY(m_logger->insertThingPower(m_timestamp, EnergyLogs::SampleRate1Min,
                                          ThingId::createThingId(), 44, 54, 9));
        QCOMPARE(balance.count(), 2);
        QCOMPARE(things.count(), 3);
        QCOMPARE(scalar("SELECT COUNT(*) FROM powerBalance").toInt(), 3);
        QCOMPARE(scalar("SELECT COUNT(*) FROM thingPower").toInt(), 4);
    }

    void queuedFlush()
    {
        QSignalSpy balance(m_logger.get(), &EnergyLogs::powerBalanceEntryAdded);
        QSignalSpy things(m_logger.get(), &EnergyLogs::thingPowerEntryAdded);
        // Hold writes deterministically, without racing the startup worker.
        m_logger->m_dbMaintenanceRunning = true;
        QVERIFY(insertMain(99, m_timestamp));
        QVERIFY(insertMain(22, m_timestamp.addSecs(60)));
        QVERIFY(insertMain(98, m_timestamp.addSecs(60)));
        m_logger->flushPendingDbWrites();
        QCOMPARE(m_logger->m_pendingPowerBalanceSamples.count(), 3);
        QCOMPARE(m_logger->m_pendingThingPowerSamples.count(), 3);
        QCOMPARE(scalar("SELECT COUNT(*) FROM powerBalance").toInt(), 0);
        QCOMPARE(balance.count(), 0);
        QCOMPARE(things.count(), 0);

        // Recovery writes the first key while its live sample is queued.
        QVERIFY(insertMaintenance(11, m_timestamp));
        m_logger->m_dbMaintenanceRunning = false;
        m_logger->flushPendingDbWrites();
        QVERIFY(m_logger->m_pendingPowerBalanceSamples.isEmpty());
        QVERIFY(m_logger->m_pendingThingPowerSamples.isEmpty());
        QCOMPARE(scalar("SELECT COUNT(*) FROM powerBalance").toInt(), 2);
        QCOMPARE(scalar("SELECT COUNT(*) FROM thingPower").toInt(), 2);
        QCOMPARE(scalar("SELECT consumption FROM powerBalance ORDER BY timestamp LIMIT 1").toDouble(), 11.0);
        QCOMPARE(scalar("SELECT currentPower FROM thingPower ORDER BY timestamp DESC LIMIT 1").toDouble(), 22.0);
        QCOMPARE(balance.count(), 1);
        QCOMPARE(things.count(), 1);
        m_logger->flushPendingDbWrites();
        QCOMPARE(balance.count(), 1);
        QCOMPARE(things.count(), 1);
    }

    void cleanupOnOpen()
    {
        {
            QSqlQuery query(m_logger->m_db);
            QVERIFY(query.exec("DROP INDEX idx_powerBalance_unique_sample"));
            QVERIFY(query.exec("DROP INDEX idx_thingPower_unique_sample"));
        }
        QVERIFY(insertMain(11, m_timestamp));
        QVERIFY(insertMain(99, m_timestamp));
        QVERIFY(insertMaintenance(98, m_timestamp));
        QVERIFY(insertMain(22, m_timestamp, EnergyLogs::SampleRate15Mins));
        QCOMPARE(scalar("SELECT COUNT(*) FROM powerBalance").toInt(), 4);
        stopLogger();
        openLogger();
        QCOMPARE(scalar("SELECT COUNT(*) FROM powerBalance").toInt(), 2);
        QCOMPARE(scalar("SELECT COUNT(*) FROM thingPower").toInt(), 2);
        QCOMPARE(scalar("SELECT consumption FROM powerBalance WHERE sampleRate = 1").toDouble(), 11.0);
        QCOMPARE(scalar("SELECT totalConsumption FROM thingPower WHERE sampleRate = 1").toDouble(), 21.0);
        QVERIFY(insertMain(97, m_timestamp));
        QVERIFY(insertMaintenance(96, m_timestamp));
        QCOMPARE(scalar("SELECT COUNT(*) FROM powerBalance").toInt(), 2);
        QCOMPARE(scalar("SELECT COUNT(*) FROM thingPower").toInt(), 2);
        EnergyLogDatabase::DuplicateCleanupResult result;
        QVERIFY(EnergyLogDatabase::cleanupDuplicates(m_logger->m_db, result));
        QCOMPARE(result.powerBalanceRowsRemoved, 0);
        QCOMPARE(result.thingPowerRowsRemoved, 0);
    }

    void failedMigrationClosesDatabase()
    {
        {
            QSqlQuery query(m_logger->m_db);
            QVERIFY(query.exec("DROP INDEX idx_powerBalance_unique_sample"));
            QVERIFY(query.exec("DROP INDEX idx_thingPower_unique_sample"));
            QVERIFY(query.exec("CREATE INDEX idx_thingPower_unique_sample ON thingPower(timestamp)"));
        }
        QVERIFY(insertMain(11, m_timestamp));
        QVERIFY(insertMain(99, m_timestamp));
        EnergyLogDatabase::DuplicateCleanupResult result;
        QVERIFY(!EnergyLogDatabase::cleanupDuplicates(m_logger->m_db, result));
        QVERIFY(!result.error.isEmpty());
        QCOMPARE(result.powerBalanceRowsRemoved, 0);
        QCOMPARE(result.thingPowerRowsRemoved, 0);
        QCOMPARE(scalar("SELECT COUNT(*) FROM powerBalance").toInt(), 2);
        QCOMPARE(scalar("SELECT COUNT(*) FROM thingPower").toInt(), 2);
        QCOMPARE(scalar("SELECT COUNT(*) FROM sqlite_master WHERE name = 'idx_powerBalance_unique_sample'").toInt(), 0);
        stopLogger();
        m_logger.reset(new EnergyLogger);
        QVERIFY(!m_logger->m_db.isOpen());
        QVERIFY(!m_logger->m_sampleTimer.isActive());
        QVERIFY(!m_logger->m_dbMaintenanceRunning);
    }
};

QTEST_GUILESS_MAIN(TestEnergyLogger)
#include "tst_energylogger.moc"
