// SPDX-License-Identifier: GPL-3.0-or-later

#include <QtTest>
#include <energylogdatabase.h>

class TestEnergyLogDatabase : public QObject
{
    Q_OBJECT
    QSqlDatabase db;

    bool exec(const QString &sql) { return QSqlQuery(db).exec(sql); }
    QVariant scalar(const QString &sql)
    {
        QSqlQuery query(db);
        if (!query.exec(sql) || !query.next())
            return QVariant();
        return query.value(0);
    }

private slots:
    void init()
    {
        db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("cleanup-test"));
        db.setDatabaseName(QStringLiteral(":memory:"));
        QVERIFY2(db.open(), qPrintable(db.lastError().text()));
        QVERIFY(exec("CREATE TABLE powerBalance (sampleRate INT, timestamp BIGINT, consumption FLOAT)"));
        QVERIFY(exec("CREATE TABLE thingPower (thingId TEXT, sampleRate INT, timestamp BIGINT, currentPower FLOAT)"));
        QVERIFY(exec("CREATE INDEX idx_powerBalance ON powerBalance(sampleRate, timestamp)"));
        QVERIFY(exec("CREATE INDEX idx_thingPower ON thingPower(thingId, sampleRate, timestamp)"));
        QVERIFY(exec("INSERT INTO powerBalance VALUES (1,100,10),(1,100,20),(1,100,30),(15,100,40),(1,101,50)"));
        QVERIFY(exec("INSERT INTO thingPower VALUES ('a',1,100,10),('a',1,100,20),('b',1,100,30),('a',15,100,40),('a',1,101,50)"));
    }

    void cleanup()
    {
        db.close();
        db = QSqlDatabase();
        QSqlDatabase::removeDatabase(QStringLiteral("cleanup-test"));
    }

    void preservesFirstAndPreventsDuplicates()
    {
        EnergyLogDatabase::DuplicateCleanupResult result;
        QVERIFY2(EnergyLogDatabase::cleanupDuplicates(db, result), qPrintable(result.error));
        QCOMPARE(result.powerBalanceRowsRemoved, qint64(2));
        QCOMPARE(result.thingPowerRowsRemoved, qint64(1));
        QVERIFY(result.error.isEmpty());
        QCOMPARE(scalar("SELECT group_concat(rowid) FROM (SELECT rowid FROM powerBalance ORDER BY rowid)").toString(), QString("1,4,5"));
        QCOMPARE(scalar("SELECT group_concat(rowid) FROM (SELECT rowid FROM thingPower ORDER BY rowid)").toString(), QString("1,3,4,5"));
        QCOMPARE(scalar("SELECT consumption FROM powerBalance WHERE sampleRate=1 AND timestamp=100").toInt(), 10);
        QCOMPARE(scalar("SELECT currentPower FROM thingPower WHERE thingId='a' AND sampleRate=1 AND timestamp=100").toInt(), 10);
        QCOMPARE(scalar("SELECT count(*) FROM sqlite_master WHERE name IN ('idx_powerBalance','idx_thingPower')").toInt(), 0);
        QVERIFY(!exec("INSERT INTO powerBalance VALUES (1,100,99)"));
        QVERIFY(!exec("INSERT INTO thingPower VALUES ('a',1,100,99)"));
        QVERIFY(exec("INSERT INTO powerBalance VALUES (60,100,99)"));
        QVERIFY(exec("INSERT INTO thingPower VALUES ('c',1,100,99)"));
        // Abort any DELETE to prove the second run skips scanning/deleting rows.
        QVERIFY(exec("CREATE TRIGGER no_balance_delete BEFORE DELETE ON powerBalance BEGIN SELECT RAISE(ABORT,'unexpected delete'); END"));
        QVERIFY(exec("CREATE TRIGGER no_thing_delete BEFORE DELETE ON thingPower BEGIN SELECT RAISE(ABORT,'unexpected delete'); END"));
        result.error = "stale";
        QVERIFY2(EnergyLogDatabase::cleanupDuplicates(db, result), qPrintable(result.error));
        QCOMPARE(result.powerBalanceRowsRemoved, qint64(0));
        QCOMPARE(result.thingPowerRowsRemoved, qint64(0));
        QVERIFY(result.error.isEmpty());
    }

    void statementFailureRollsBack()
    {
        QVERIFY(exec("CREATE TRIGGER fail_delete BEFORE DELETE ON thingPower BEGIN SELECT RAISE(ABORT,'injected failure'); END"));
        EnergyLogDatabase::DuplicateCleanupResult result;
        QVERIFY(!EnergyLogDatabase::cleanupDuplicates(db, result));
        QVERIFY(result.error.contains("injected failure"));
        verifyRollback(result);
        QVERIFY(exec("DROP TRIGGER fail_delete"));
        QVERIFY2(EnergyLogDatabase::cleanupDuplicates(db, result), qPrintable(result.error));
    }

    void commitFailureRollsBack()
    {
        QVERIFY(exec("PRAGMA foreign_keys=ON"));
        QVERIFY(exec("CREATE TABLE parent (id INTEGER PRIMARY KEY)"));
        QVERIFY(exec("CREATE TABLE child (parentId INTEGER REFERENCES parent(id) DEFERRABLE INITIALLY DEFERRED)"));
        QVERIFY(exec("CREATE TRIGGER fail_commit AFTER DELETE ON powerBalance BEGIN INSERT INTO child VALUES (999); END"));
        EnergyLogDatabase::DuplicateCleanupResult result;
        QVERIFY(!EnergyLogDatabase::cleanupDuplicates(db, result));
        QVERIFY(result.error.contains("Committing"));
        verifyRollback(result);
        QCOMPARE(scalar("SELECT count(*) FROM child").toInt(), 0);
        QVERIFY(db.transaction());
        QVERIFY(db.rollback());
    }

    void existingTransactionIsNotRolledBack()
    {
        QVERIFY(db.transaction());
        QVERIFY(exec("INSERT INTO powerBalance VALUES (60,100,99)"));
        EnergyLogDatabase::DuplicateCleanupResult result;
        QVERIFY(!EnergyLogDatabase::cleanupDuplicates(db, result));
        QVERIFY(result.error.contains("Starting"));
        QCOMPARE(scalar("SELECT count(*) FROM powerBalance").toInt(), 6);
        QVERIFY(db.rollback());
        QCOMPARE(scalar("SELECT count(*) FROM powerBalance").toInt(), 5);
    }

    void incorrectIndexDefinitionIsRejected()
    {
        QVERIFY(exec("CREATE INDEX idx_thingPower_unique_sample ON thingPower(thingId,sampleRate,timestamp)"));
        EnergyLogDatabase::DuplicateCleanupResult result;
        QVERIFY(!EnergyLogDatabase::cleanupDuplicates(db, result));
        QVERIFY(result.error.contains("Unexpected definition"));
        QCOMPARE(scalar("SELECT count(*) FROM powerBalance").toInt(), 5);
        QCOMPARE(scalar("SELECT count(*) FROM sqlite_master WHERE name='idx_powerBalance_unique_sample'").toInt(), 0);
    }

private:
    void verifyRollback(const EnergyLogDatabase::DuplicateCleanupResult &result)
    {
        QCOMPARE(result.powerBalanceRowsRemoved, qint64(0));
        QCOMPARE(result.thingPowerRowsRemoved, qint64(0));
        QCOMPARE(scalar("SELECT count(*) FROM powerBalance").toInt(), 5);
        QCOMPARE(scalar("SELECT count(*) FROM thingPower").toInt(), 5);
        QCOMPARE(scalar("SELECT count(*) FROM sqlite_master WHERE name IN ('idx_powerBalance_unique_sample','idx_thingPower_unique_sample')").toInt(), 0);
        QCOMPARE(scalar("SELECT count(*) FROM sqlite_master WHERE name IN ('idx_powerBalance','idx_thingPower')").toInt(), 2);
    }
};

QTEST_GUILESS_MAIN(TestEnergyLogDatabase)
#include "tst_energylogdatabase.moc"
