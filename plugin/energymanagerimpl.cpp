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
    // Most of the time we get a bunch of signals at the same time (root meter, producers, consumers etc)
    // In order to decrease some load on the system, we'll wait for wee bit until we actually update to
    // accumulate those changes and calculate the change in one go.
    m_balanceUpdateTimer.setInterval(50);
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
    qCDebug(dcEnergyExperience()) << "Loader power balance totals. Consumption:" << m_totalConsumption << "Production:" << m_totalProduction << "Acquisition:" << m_totalAcquisition << "Return:" << m_totalReturn;

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
        settings.setValue("rootMeterThingId", rootMeter->id());

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

    qCDebug(dcEnergyExperience()) << "Wathing thing:" << thing->name();

    // React on things that requie us updating the power balance
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

        m_totalEnergyConsumedCache[thing] = thing->stateValue("totalEnergyConsumed").toDouble();
        m_totalEnergyProducedCache[thing] = thing->stateValue("totalEnergyProduced").toDouble();

        connect(thing, &Thing::stateValueChanged, this, [=](const StateTypeId &stateTypeId, const QVariant &value){
            if (thing->thingClass().getStateType(stateTypeId).name() == "currentPower") {                
                m_logger->logThingPower(thing->id(), value.toDouble(), thing->state("totalEnergyConsumed").value().toDouble(), thing->state("totalEnergyProduced").value().toDouble());
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

        double oldAcquisition = m_totalEnergyConsumedCache.value(m_rootMeter);
        double newAcquisition = m_rootMeter->stateValue("totalEnergyConsumed").toDouble();
        qCDebug(dcEnergyExperience()) << "Root meteter total consumption diff" << "old" << oldAcquisition << " new" << newAcquisition << (newAcquisition -oldAcquisition);
        m_totalAcquisition += newAcquisition - oldAcquisition;
        m_totalEnergyConsumedCache[m_rootMeter] = newAcquisition;

        double oldReturn = m_totalEnergyProducedCache.value(m_rootMeter);
        double newReturn = m_rootMeter->stateValue("totalEnergyProduced").toDouble();
        qCDebug(dcEnergyExperience()) << "Root meteter total return diff" << "old" << oldReturn << " new" << newReturn << (newReturn - oldReturn);
        m_totalReturn += newReturn - oldReturn;
        m_totalEnergyProducedCache[m_rootMeter] = newReturn;
    }

    double currentPowerProduction = 0;
    foreach (Thing* thing, m_thingManager->configuredThings().filterByInterface("smartmeterproducer")) {
        currentPowerProduction += thing->stateValue("currentPower").toDouble();
        double oldProduction = m_totalEnergyProducedCache.value(thing);
        double newProduction = thing->stateValue("totalEnergyProduced").toDouble();
        qCDebug(dcEnergyExperience()) << "inverter total production diff" << "old" << oldProduction << " new" << newProduction << (newProduction - oldProduction);
        m_totalProduction += newProduction - oldProduction;
        m_totalEnergyProducedCache[thing] = newProduction;
    }

    double currentPowerStorage = 0;
    double totalFromStorage = 0;
    foreach (Thing *thing, m_thingManager->configuredThings().filterByInterface("energystorage")) {
        currentPowerStorage += thing->stateValue("currentPower").toDouble();
        double oldProduction = m_totalEnergyProducedCache.value(thing);
        double newProduction = thing->stateValue("totalEnergyProduced").toDouble();
        totalFromStorage += newProduction - oldProduction;
        m_totalEnergyProducedCache[thing] = newProduction;
    }

    double currentPowerConsumption = currentPowerAcquisition + qAbs(qMin(0.0, currentPowerProduction)) - currentPowerStorage;
    m_totalConsumption = m_totalAcquisition + m_totalProduction + totalFromStorage;

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


