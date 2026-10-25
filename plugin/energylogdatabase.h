// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ENERGYLOGDATABASE_H
#define ENERGYLOGDATABASE_H

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QString>
#include <QVariant>

namespace EnergyLogDatabase {

struct DuplicateCleanupResult {
    qint64 powerBalanceRowsRemoved = 0;
    qint64 thingPowerRowsRemoved = 0;
    QString error;
};

// SQLite only. The caller must not have an active transaction. Counts describe
// committed removals only. Freed pages are reused; this deliberately does not VACUUM.
inline bool cleanupDuplicates(QSqlDatabase &db, DuplicateCleanupResult &result)
{
    result = DuplicateCleanupResult();
    if (!db.transaction()) {
        result.error = QStringLiteral("Starting duplicate cleanup transaction: ") + db.lastError().text();
        return false;
    }

    const auto fail = [&db, &result](const QString &error) {
        result.error = error;
        if (!db.rollback())
            result.error += QStringLiteral("; rollback failed: ") + db.lastError().text();
        return false;
    };

    const QString tables[] = {QStringLiteral("powerBalance"), QStringLiteral("thingPower")};
    const QString keys[] = {QStringLiteral("sampleRate, timestamp"), QStringLiteral("thingId, sampleRate, timestamp")};
    const QString names[] = {QStringLiteral("idx_powerBalance_unique_sample"), QStringLiteral("idx_thingPower_unique_sample")};
    qint64 removed[] = {0, 0};
    for (int i = 0; i < 2; ++i) {
        const QString createSql = QStringLiteral("CREATE UNIQUE INDEX %1 ON %2(%3)")
                .arg(names[i], tables[i], keys[i]);
        QSqlQuery query(db);
        query.prepare(QStringLiteral("SELECT sql FROM sqlite_master WHERE type = 'index' AND name = ?"));
        query.addBindValue(names[i]);
        if (!query.exec())
            return fail(QStringLiteral("Inspecting %1: ").arg(names[i]) + query.lastError().text());
        if (query.next()) {
            // These names belong to this migration. Validate the full definition,
            // including uniqueness and absence of a partial-index WHERE clause.
            if (query.value(0).toString() != createSql) {
                query.finish();
                return fail(QStringLiteral("Unexpected definition for index %1").arg(names[i]));
            }
            query.finish();
            continue;
        }
        if (query.lastError().isValid())
            return fail(QStringLiteral("Reading %1: ").arg(names[i]) + query.lastError().text());
        query.finish();

        if (!query.exec(QStringLiteral("DELETE FROM %1 WHERE rowid NOT IN "
                                       "(SELECT MIN(rowid) FROM %1 GROUP BY %2)").arg(tables[i], keys[i])))
            return fail(QStringLiteral("Cleaning %1: ").arg(tables[i]) + query.lastError().text());
        removed[i] = query.numRowsAffected();
        query.finish();
        if (!query.exec(createSql))
            return fail(QStringLiteral("Creating %1: ").arg(names[i]) + query.lastError().text());
        query.finish();
    }

    for (const QString &name : {QStringLiteral("idx_powerBalance"), QStringLiteral("idx_thingPower")}) {
        QSqlQuery query(db);
        if (!query.exec(QStringLiteral("DROP INDEX IF EXISTS %1").arg(name)))
            return fail(QStringLiteral("Dropping redundant index %1: ").arg(name) + query.lastError().text());
        query.finish();
    }

    if (!db.commit())
        return fail(QStringLiteral("Committing duplicate cleanup: ") + db.lastError().text());
    result.powerBalanceRowsRemoved = removed[0];
    result.thingPowerRowsRemoved = removed[1];
    return true;
}

}

#endif // ENERGYLOGDATABASE_H
