#ifndef ENERGYMANAGERIMPL_H
#define ENERGYMANAGERIMPL_H

#include <QObject>
#include <QHash>
#include <QTimer>

#include "integrations/thingmanager.h"

#include "energymanager.h"

class EnergyLogger;

class EnergyManagerImpl : public EnergyManager
{
    Q_OBJECT
public:
    explicit EnergyManagerImpl(ThingManager *thingManager, QObject *parent = nullptr);

    Thing *rootMeter() const override;
    EnergyError setRootMeter(const ThingId &rootMeterId) override;

    double currentPowerConsumption() const override;
    double currentPowerProduction() const override;
    double currentPowerAcquisition() const override;
    double currentPowerStorage() const override;
    double totalConsumption() const override;
    double totalProduction() const override;
    double totalAcquisition() const override;
    double totalReturn() const override;

    EnergyLogs* logs() const override;

private:
    void watchThing(Thing *thing);
    void unwatchThing(const ThingId &thingId);

    void updatePowerBalance();
    void updateThingPower(Thing *thing);

private slots:
    void logDumpConsumers();

private:
    ThingManager *m_thingManager = nullptr;

    Thing *m_rootMeter = nullptr;

    QTimer m_balanceUpdateTimer;
    double m_currentPowerConsumption = 0;
    double m_currentPowerProduction = 0;
    double m_currentPowerAcquisition = 0;
    double m_currentPowerStorage = 0;
    double m_totalConsumption = 0;
    double m_totalProduction = 0;
    double m_totalAcquisition = 0;
    double m_totalReturn = 0;

    EnergyLogger *m_logger = nullptr;

    // Caching some values so we don't have to look them up on the DB all the time:
    // We use different caches for power balance and thing logs because they are calculated independently
    // and one must not update the others cache for the diffs to be correct

    // For things totals we need to cache 2 values:
    // The last thing state values we've processed
    QHash<Thing*, double> m_powerBalanceTotalEnergyConsumedCache;
    QHash<Thing*, double> m_powerBalanceTotalEnergyProducedCache;

    // - The last thing state value we've read and processed
    // - The last entry in our internal counters we've processed and logged
    // QHash<Thing*, Pair<thingStateValue, internalValue>>
    QHash<Thing*, QPair<double, double>> m_thingsTotalEnergyConsumedCache;
    QHash<Thing*, QPair<double, double>> m_thingsTotalEnergyProducedCache;
};

#endif // ENERGYMANAGERIMPL_H
