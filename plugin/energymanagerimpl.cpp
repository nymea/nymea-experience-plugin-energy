#include "energymanagerimpl.h"
#include "energylogger.h"

#include <nymeasettings.h>

#include <qmath.h>

Q_DECLARE_LOGGING_CATEGORY(dcEnergyExperience)

EnergyManagerImpl::EnergyManagerImpl(ThingManager *thingManager, QObject *parent):
    EnergyManager(parent),
    m_thingManager(thingManager),
    m_logger(new EnergyLogger(this))
{
    // Most of the time we get a bunch of state changes (currentPower, totals, for inverter, battery, rootmeter)
    // at the same time if they're implemented by the same plugin.
    // In order to decrease some load on the system, we'll wait for the event loop pass to finish until we actually
    // update to accumulate those changes and calculate the change in one go.
    m_balanceUpdateTimer.setInterval(0);
    m_balanceUpdateTimer.setSingleShot(true);
    connect(&m_balanceUpdateTimer, &QTimer::timeout, this, &EnergyManagerImpl::updatePowerBalance);

    QSettings settings(NymeaSettings::settingsPath() + "/energy.conf", QSettings::IniFormat);
    ThingId rootMeterThingId = settings.value("rootMeterThingId").toUuid();
    EnergyManagerImpl::setRootMeter(rootMeterThingId);
    qCDebug(dcEnergyExperience()) << "Loaded root meter" << rootMeterThingId;

    PowerBalanceLogEntry latestEntry = m_logger->latestLogEntry(EnergyLogs::SampleRateAny);
    m_totalConsumption = latestEntry.totalConsumption();
    m_totalProduction = latestEntry.totalProduction();
    m_totalAcquisition = latestEntry.totalAcquisition();
    m_totalReturn = latestEntry.totalReturn();
    qCDebug(dcEnergyExperience()) << "Loaded power balance totals. Consumption:" << m_totalConsumption << "Production:" << m_totalProduction << "Acquisition:" << m_totalAcquisition << "Return:" << m_totalReturn;

    foreach (Thing *thing, m_thingManager->configuredThings()) {
        watchThing(thing);
    }
    connect(thingManager, &ThingManager::thingAdded, this, &EnergyManagerImpl::watchThing);
    connect(thingManager, &ThingManager::thingRemoved, this, &EnergyManagerImpl::unwatchThing);

    // Housekeeping on the logger
    foreach (const ThingId &thingId, m_logger->loggedThings()) {
        if (!m_thingManager->findConfiguredThing(thingId)) {
            qCDebug(dcEnergyExperience()) << "Clearing thing logs for unknown thing id" << thingId << "from energy logs.";
            m_logger->removeThingLogs(thingId);
        }
    }

    connect(m_logger, &EnergyLogs::powerBalanceEntryAdded, this, &EnergyManager::powerBalanceSampled);
    connect(m_logger, &EnergyLogs::thingPowerEntryAdded, this, &EnergyManager::thingPowerSampled);
}

Thing *EnergyManagerImpl::rootMeter() const
{
    return m_rootMeter;
}

EnergyManager::EnergyError EnergyManagerImpl::setRootMeter(const ThingId &rootMeterId)
{
    Thing *rootMeter = m_thingManager->findConfiguredThing(rootMeterId);
    if (!rootMeter || !rootMeter->thingClass().interfaces().contains("energymeter")) {
        return EnergyErrorInvalidParameter;
    }

    if (m_rootMeter != rootMeter) {
        qCDebug(dcEnergyExperience()) << "Setting root meter to" << rootMeter->name();
        m_rootMeter = rootMeter;

        QSettings settings(NymeaSettings::settingsPath() + "/energy.conf", QSettings::IniFormat);
        settings.setValue("rootMeterThingId", rootMeter->id().toString());

        emit rootMeterChanged();
    }
    return EnergyErrorNoError;
}

double EnergyManagerImpl::currentPowerConsumption() const
{
    return m_currentPowerConsumption;
}

double EnergyManagerImpl::currentPowerProduction() const
{
    return m_currentPowerProduction;
}

double EnergyManagerImpl::currentPowerAcquisition() const
{
    return m_currentPowerAcquisition;
}

double EnergyManagerImpl::currentPowerStorage() const
{
    return m_currentPowerStorage;
}

double EnergyManagerImpl::totalConsumption() const
{
    return m_totalConsumption;
}

double EnergyManagerImpl::totalProduction() const
{
    return m_totalProduction;
}

double EnergyManagerImpl::totalAcquisition() const
{
    return m_totalAcquisition;
}

double EnergyManagerImpl::totalReturn() const
{
    return m_totalReturn;
}

EnergyLogs *EnergyManagerImpl::logs() const
{
    return m_logger;
}

void EnergyManagerImpl::watchThing(Thing *thing)
{
    // If we don't have a root meter yet, we'll be auto-setting the first energymeter that appears.
    // It may be changed by the user through an API call later.
    if (!m_rootMeter && thing->thingClass().interfaces().contains("energymeter")) {
        setRootMeter(thing->id());
    }

    qCDebug(dcEnergyExperience()) << "Watching thing:" << thing->name();

    // React on things that require us updating the power balance
    if (thing->thingClass().interfaces().contains("energymeter")
            || thing->thingClass().interfaces().contains("smartmeterproducer")
            || thing->thingClass().interfaces().contains("energystorage")) {
        connect(thing, &Thing::stateValueChanged, this, [=](const StateTypeId &stateTypeId){
            if (thing->thingClass().getStateType(stateTypeId).name() == "currentPower") {
                m_balanceUpdateTimer.start();
            }
        });
    }

    // React on things that need to be logged
    if (thing->thingClass().interfaces().contains("energymeter")
            || thing->thingClass().interfaces().contains("smartmeterconsumer")
            || thing->thingClass().interfaces().contains("smartmeterproducer")
            || thing->thingClass().interfaces().contains("energystorage")) {

        // Initialize caches used to calculate diffs
        ThingPowerLogEntry entry = m_logger->latestLogEntry(EnergyLogs::SampleRateAny, {thing->id()});
        ThingPowerLogEntry stateEntry = m_logger->cachedThingEntry(thing->id());

        m_powerBalanceTotalEnergyConsumedCache[thing] = stateEntry.totalConsumption();
        m_powerBalanceTotalEnergyProducedCache[thing] = stateEntry.totalProduction();

        m_thingsTotalEnergyConsumedCache[thing] = qMakePair<double, double>(stateEntry.totalConsumption(), entry.totalConsumption());
        m_thingsTotalEnergyProducedCache[thing] = qMakePair<double, double>(stateEntry.totalProduction(), entry.totalProduction());
        qCDebug(dcEnergyExperience()) << "Loaded thing power totals for" << thing->name() << "Consumption:" << entry.totalConsumption() << "Production:" << entry.totalProduction() << "Last thing state consumption:" << stateEntry.totalConsumption() << "production:" << stateEntry.totalProduction();

        connect(thing, &Thing::stateValueChanged, this, [=](const StateTypeId &stateTypeId, const QVariant &/*value*/){
            if (QStringList({"currentPower", "totalEnergyConsumed", "totalEnergyProduced"}).contains(thing->thingClass().getStateType(stateTypeId).name())) {

                // We'll be keeping our own counters, starting from 0 at the time they're added to nymea and increasing with the things counters.
                // This way we'll have proper logs even if the thing counter is reset (some things may reset their counter on power loss, factory reset etc)
                // and also won't start with huge values if the thing has been counting for a while and only added to nymea later on


                // Consumption
                double oldThingConsumptionState = m_thingsTotalEnergyConsumedCache.value(thing).first;
                double oldThingConsumptionInternal = m_thingsTotalEnergyConsumedCache.value(thing).second;
                double newThingConsumptionState = thing->stateValue("totalEnergyConsumed").toDouble();
                // For the very first cycle (oldConsumption is 0) we'll sync up on the meter, without actually adding it to our diff
                if (oldThingConsumptionState == 0 && newThingConsumptionState != 0) {
                    qInfo(dcEnergyExperience()) << "Don't have a consumption counter for" << thing->name() << "Synching internal counters to initial value:" << newThingConsumptionState;
                    oldThingConsumptionState = newThingConsumptionState;
                }
                // If the thing's meter has been reset in the meantime (newConsumption < oldConsumption) we'll sync down, taking the whole diff from 0 to new value
                if (newThingConsumptionState < oldThingConsumptionState) {
                    qCInfo(dcEnergyExperience()) << "Thing meter for" << thing->name() << "seems to have been reset. Re-synching internal consumption counter.";
                    oldThingConsumptionState = newThingConsumptionState;
                }
                double consumptionDiff = newThingConsumptionState - oldThingConsumptionState;
                double newThingConsumptionInternal = oldThingConsumptionInternal + consumptionDiff;
                m_thingsTotalEnergyConsumedCache[thing] = qMakePair<double, double>(newThingConsumptionState, newThingConsumptionInternal);


                // Production
                double oldThingProductionState = m_thingsTotalEnergyProducedCache.value(thing).first;
                double oldThingProductionInternal = m_thingsTotalEnergyProducedCache.value(thing).second;
                double newThingProductionState = thing->stateValue("totalEnergyProduced").toDouble();
                // For the very first cycle (oldProductino is 0) we'll sync up on the meter, without actually adding it to our diff
                if (oldThingProductionState == 0 && newThingProductionState != 0) {
                    qInfo(dcEnergyExperience()) << "Don't have a production counter for" << thing->name() << "Synching internal counter to initial value:" << newThingProductionState;
                    oldThingProductionState = newThingProductionState;
                }
                // If the thing's meter has been reset in the meantime (newProduction < oldProduction) we'll sync down, taking the whole diff from 0 to new value
                if (newThingProductionState < oldThingProductionState) {
                    qCInfo(dcEnergyExperience()) << "Thing meter for" << thing->name() << "seems to have been reset. Re-synching internal production counter.";
                    oldThingProductionState = newThingProductionState;
                }
                double productionDiff = newThingProductionState - oldThingProductionState;
                double newThingProductionInternal = oldThingProductionInternal + productionDiff;
                m_thingsTotalEnergyProducedCache[thing] = qMakePair<double, double>(newThingProductionState, newThingProductionInternal);


                // Write to log
                qCDebug(dcEnergyExperience()) << "Logging thing" << thing->name() << "total consumption:" << newThingConsumptionInternal << "production:" << newThingProductionInternal;
                m_logger->logThingPower(thing->id(), thing->state("currentPower").value().toDouble(), newThingConsumptionInternal, newThingProductionInternal);

                // Cache the thing state values in case nymea is restarted
                m_logger->cacheThingEntry(thing->id(), newThingConsumptionState, newThingProductionState);
            }
        });
    }
}

void EnergyManagerImpl::unwatchThing(const ThingId &thingId)
{
    if (m_rootMeter && m_rootMeter->id() == thingId) {
        m_rootMeter = nullptr;
        emit rootMeterChanged();
    }

    m_logger->removeThingLogs(thingId);
}

void EnergyManagerImpl::updatePowerBalance()
{
    double currentPowerAcquisition = 0;
    if (m_rootMeter) {
        currentPowerAcquisition = m_rootMeter->stateValue("currentPower").toDouble();

        double oldAcquisition = m_powerBalanceTotalEnergyConsumedCache.value(m_rootMeter);
        double newAcquisition = m_rootMeter->stateValue("totalEnergyConsumed").toDouble();
        // For the very first cycle (oldAcquisition is 0) we'll sync up on the meter values without actually adding them to our balance.
        if (oldAcquisition == 0) {
            oldAcquisition = newAcquisition;
        }
        // If the root meter has been reset in the meantime (newConsumption < oldConsumption) we'll sync down, taking the whole diff from 0 to new value
        if (newAcquisition < oldAcquisition) {
            qCInfo(dcEnergyExperience()) << "Root meter seems to have been reset. Re-synching internal consumption counter.";
            oldAcquisition = newAcquisition;
        }
        qCDebug(dcEnergyExperience()) << "Root meter total consumption: Previous value:" << oldAcquisition << "New value:" << newAcquisition << "Diff:" << (newAcquisition -oldAcquisition);
        m_totalAcquisition += newAcquisition - oldAcquisition;
        m_powerBalanceTotalEnergyConsumedCache[m_rootMeter] = newAcquisition;

        double oldReturn = m_powerBalanceTotalEnergyProducedCache.value(m_rootMeter);
        double newReturn = m_rootMeter->stateValue("totalEnergyProduced").toDouble();
        // For the very first cycle (oldReturn is 0) we'll sync up on the meter values without actually adding them to our balance.
        if (oldReturn == 0) {
            oldReturn = newReturn;
        }
        if (newReturn < oldReturn) {
            qCInfo(dcEnergyExperience()) << "Root meter seems to have been reset. Re-synching internal production counter.";
            oldReturn = newReturn;
        }
        qCDebug(dcEnergyExperience()) << "Root meter total production: Previous value:" << oldReturn << "New value:" << newReturn << "Diff:" << (newReturn - oldReturn);
        m_totalReturn += newReturn - oldReturn;
        m_powerBalanceTotalEnergyProducedCache[m_rootMeter] = newReturn;
    }

    double currentPowerProduction = 0;
    foreach (Thing* thing, m_thingManager->configuredThings().filterByInterface("smartmeterproducer")) {
        currentPowerProduction += thing->stateValue("currentPower").toDouble();
        double oldProduction = m_powerBalanceTotalEnergyProducedCache.value(thing);
        double newProduction = thing->stateValue("totalEnergyProduced").toDouble();
        // For the very first cycle (oldProduction is 0) we'll sync up on the producer values without actually adding them to our balance.
        if (oldProduction == 0) {
            oldProduction = newProduction;
        }
        if (newProduction < oldProduction) {
            oldProduction = newProduction;
        }
        qCDebug(dcEnergyExperience()) << "Producer" << thing->name() << "total production: Previous value:" << oldProduction << "New value:" << newProduction << "Diff:" << (newProduction - oldProduction);
        m_totalProduction += newProduction - oldProduction;
        m_powerBalanceTotalEnergyProducedCache[thing] = newProduction;
    }

    double currentPowerStorage = 0;
    double totalFromStorage = 0;
    foreach (Thing *thing, m_thingManager->configuredThings().filterByInterface("energystorage")) {
        currentPowerStorage += thing->stateValue("currentPower").toDouble();
        double oldProduction = m_powerBalanceTotalEnergyProducedCache.value(thing);
        double newProduction = thing->stateValue("totalEnergyProduced").toDouble();
        // For the very first cycle (oldProdction is 0) we'll sync up on the meter values without actually adding them to our balance.
        if (oldProduction == 0) {
            oldProduction = newProduction;
        }
        if (newProduction < oldProduction) {
            oldProduction = newProduction;
        }
        qCDebug(dcEnergyExperience()) << "Storage" << thing->name() << "total storage: Previous value:" << oldProduction << "New value:" << newProduction << "Diff:" << (newProduction - oldProduction);
        totalFromStorage += newProduction - oldProduction;
        m_powerBalanceTotalEnergyProducedCache[thing] = newProduction;
    }

    double currentPowerConsumption = currentPowerAcquisition + qAbs(qMin(0.0, currentPowerProduction)) - currentPowerStorage;
    m_totalConsumption = m_totalAcquisition + m_totalProduction + totalFromStorage - m_totalReturn;

    qCDebug(dcEnergyExperience()).noquote().nospace() << "Power balance: " << "🔥: " << currentPowerConsumption << " W, 🌞: " << currentPowerProduction << " W, 💵: " << currentPowerAcquisition << " W, 🔋: " << currentPowerStorage << " W. Totals: 🔥: " << m_totalConsumption << " kWh, 🌞: " << m_totalProduction << " kWh, 💵↓: " << m_totalAcquisition << " kWh, 💵↑: " << m_totalReturn << " kWh";
    if (currentPowerAcquisition != m_currentPowerAcquisition
            || currentPowerConsumption != m_currentPowerConsumption
            || currentPowerProduction != m_currentPowerProduction
            || currentPowerStorage != m_currentPowerStorage) {
        m_currentPowerAcquisition = currentPowerAcquisition;
        m_currentPowerProduction = currentPowerProduction;
        m_currentPowerConsumption = currentPowerConsumption;
        m_currentPowerStorage = currentPowerStorage;
        emit powerBalanceChanged();
        m_logger->logPowerBalance(m_currentPowerConsumption, m_currentPowerProduction, m_currentPowerAcquisition, m_currentPowerStorage, m_totalConsumption, m_totalProduction, m_totalAcquisition, m_totalReturn);
    }
}

void EnergyManagerImpl::logDumpConsumers()
{
    foreach (Thing *consumer, m_thingManager->configuredThings().filterByInterface("smartmeterconsumer")) {
        qCDebug(dcEnergyExperience()).nospace().noquote() << consumer->name() << ": " << (consumer->stateValue("currentPower").toDouble() / 230) << "A (" << consumer->stateValue("currentPower").toDouble() << "W)";
    }
}


