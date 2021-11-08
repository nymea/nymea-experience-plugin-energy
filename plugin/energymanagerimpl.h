#ifndef ENERGYMANAGERIMPL_H
#define ENERGYMANAGERIMPL_H

#include <QObject>
#include <QHash>
#include <QTimer>

#include "integrations/thingmanager.h"

#include "energymanager.h"

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
    double m_currentPowerConsumption;
    double m_currentPowerProduction;
    double m_currentPowerAcquisition;
};

#endif // ENERGYMANAGERIMPL_H
