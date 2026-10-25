// SPDX-License-Identifier: GPL-3.0-or-later

#include <QtTest>
#include <QTimeZone>

#include <ctime>

#include "energysampletimestamps.h"

class TestSampleTimestamps : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // Match production fromMSecsSinceEpoch(), which uses Qt::LocalTime.
        qputenv("TZ", "Europe/Vienna");
#ifdef Q_OS_UNIX
        tzset();
#endif
    }

    void dstProgression_data()
    {
        QTest::addColumn<int>("rate");
        QTest::addColumn<QString>("startUtc");
        QTest::addColumn<QString>("endUtc");
        QTest::addColumn<int>("count");

        QTest::newRow("autumn-minute") << 1 << QString("2026-10-24T23:00:00Z") << QString("2026-10-25T03:00:00Z") << 240;
        QTest::newRow("autumn-quarter-hour") << 15 << QString("2026-10-24T23:00:00Z") << QString("2026-10-25T03:00:00Z") << 16;
        QTest::newRow("autumn-hour") << 60 << QString("2026-10-24T23:00:00Z") << QString("2026-10-25T03:00:00Z") << 4;
        QTest::newRow("spring-minute") << 1 << QString("2026-03-29T00:00:00Z") << QString("2026-03-29T03:00:00Z") << 180;
        QTest::newRow("spring-quarter-hour") << 15 << QString("2026-03-29T00:00:00Z") << QString("2026-03-29T03:00:00Z") << 12;
        QTest::newRow("spring-hour") << 60 << QString("2026-03-29T00:00:00Z") << QString("2026-03-29T03:00:00Z") << 3;
    }

    void dstProgression()
    {
        QFETCH(int, rate);
        QFETCH(QString, startUtc);
        QFETCH(QString, endUtc);
        QFETCH(int, count);
        const QTimeZone zone("Europe/Vienna");
        QVERIFY(zone.isValid());
        const auto sampleRate = EnergyLogs::SampleRate(rate);
        QDateTime timestamp = QDateTime::fromString(startUtc, Qt::ISODate).toTimeZone(zone);
        const QDateTime end = QDateTime::fromString(endUtc, Qt::ISODate);
        for (int i = 0; i < count; ++i) {
            const QDateTime next = EnergySampleTimestamps::nextSampleTimestamp(sampleRate, timestamp);
            const QDateTime localTimestamp = timestamp.toLocalTime();
            QCOMPARE(localTimestamp.time(), timestamp.time());
            const QDateTime localNext = EnergySampleTimestamps::nextSampleTimestamp(sampleRate, localTimestamp);
            QCOMPARE(localNext.toMSecsSinceEpoch(), next.toMSecsSinceEpoch());
            QCOMPARE(EnergySampleTimestamps::nextSampleTimestamp(sampleRate, localTimestamp.addMSecs(1000)).toMSecsSinceEpoch(),
                     next.toMSecsSinceEpoch());
            QVERIFY(next.isValid());
            QVERIFY(next > timestamp);
            QCOMPARE(timestamp.msecsTo(next), qint64(rate) * 60000);
            // Repair passes a timestamp one second after the last sample.
            QCOMPARE(EnergySampleTimestamps::nextSampleTimestamp(sampleRate, timestamp.addMSecs(1000)), next);
            // Fractional timestamps must still align to the same boundary.
            QCOMPARE(EnergySampleTimestamps::nextSampleTimestamp(sampleRate, timestamp.addMSecs(1234)), next);
            timestamp = next;
        }
        QCOMPARE(timestamp.toMSecsSinceEpoch(), end.toMSecsSinceEpoch());
    }

    void invalidInput()
    {
        QVERIFY(!EnergySampleTimestamps::nextSampleTimestamp(EnergyLogs::SampleRate1Hour, QDateTime()).isValid());
        QVERIFY(!EnergySampleTimestamps::nextSampleTimestamp(EnergyLogs::SampleRateAny, QDateTime::currentDateTime()).isValid());
    }
};

QTEST_GUILESS_MAIN(TestSampleTimestamps)
#include "tst_sampletimestamps.moc"
