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

    QHash<Thing*, double> m_totalEnergyConsumedCache;
    QHash<Thing*, double> m_totalEnergyProducedCache;
};

#endif // ENERGYMANAGERIMPL_H
