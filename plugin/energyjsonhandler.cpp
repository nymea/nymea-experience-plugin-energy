#include "energyjsonhandler.h"
#include "energymanagerimpl.h"

Q_DECLARE_LOGGING_CATEGORY(dcEnergyExperience)

EnergyJsonHandler::EnergyJsonHandler(EnergyManager *energyManager, QObject *parent):
    JsonHandler(parent),
    m_energyManager(energyManager)
{
    registerEnum<EnergyManager::EnergyError>();
    registerEnum<EnergyLogs::SampleRate>();

    registerObject<PowerBalanceLogEntry, PowerBalanceLogEntries>();
    registerObject<ThingPowerLogEntry, ThingPowerLogEntries>();

    QVariantMap params, returns;
    QString description;

    params.clear(); returns.clear();
    description = "Get the root meter ID. If there is no root meter set, the params will be empty.";
    returns.insert("o:rootMeterThingId", enumValueName(Uuid));
    registerMethod("GetRootMeter", description, params, returns);

    params.clear(); returns.clear();
    description = "Set the root meter.";
    params.insert("rootMeterThingId", enumValueName(Uuid));
    returns.insert("energyError", enumRef<EnergyManager::EnergyError>());
    registerMethod("SetRootMeter", description, params, returns);

    params.clear(); returns.clear();
    description = "Get the current power balance. That is, production, consumption and acquisition.";
    returns.insert("currentPowerConsumption", enumValueName(Double));
    returns.insert("currentPowerProduction", enumValueName(Double));
    returns.insert("currentPowerAcquisition", enumValueName(Double));
    registerMethod("GetPowerBalance", description, params, returns);

    params.clear(); returns.clear();
    description = "Get logs for the power balance. If from is not give, the log will start at the beginning of "
                  "recording. If to is not given, the logs will and at the last sample for this sample rate before now.";
    params.insert("sampleRate", enumRef<EnergyLogs::SampleRate>());
    params.insert("o:from", enumValueName(Uint));
    params.insert("o:to", enumValueName(Uint));
    returns.insert("powerBalanceLogEntries", objectRef<PowerBalanceLogEntries>());
    registerMethod("GetPowerBalanceLogs", description, params, returns);

    params.clear(); returns.clear();
    description = "Get logs for one or more things power values. If thingIds is not given, logs for all energy related "
                  "things will be returned. If from is not given, the log will start at the beginning of recording. If "
                  "to is not given, the logs will and at the last sample for this sample rate before now.";
    params.insert("sampleRate", enumRef<EnergyLogs::SampleRate>());
    params.insert("o:thingIds", QVariantList() << enumValueName(Uuid));
    params.insert("o:from", enumValueName(Uint));
    params.insert("o:to", enumValueName(Uint));
    returns.insert("thingPowerLogEntries", objectRef<ThingPowerLogEntries>());
    registerMethod("GetThingPowerLogs", description, params, returns);

    params.clear();
    description = "Emitted whenever the root meter id changes. If the root meter has been unset, the params will be empty.";
    params.insert("o:rootMeterThingId", enumValueName(Uuid));
    registerNotification("RootMeterChanged", description, params);

    params.clear();
    description = "Emitted whenever the energy balance changes. That is, when the current consumption, production or "
                  "acquisition changes. Typically they will all change at the same time.";
    params.insert("currentPowerConsumption", enumValueName(Double));
    params.insert("currentPowerProduction", enumValueName(Double));
    params.insert("currentPowerAcquisition", enumValueName(Double));
    registerNotification("PowerBalanceChanged", description, params);

    params.clear();
    description = "Emitted whenever a entry is added to the power balance log.";
    params.insert("sampleRate", enumRef<EnergyLogs::SampleRate>());
    params.insert("powerBalanceLogEntry", objectRef<PowerBalanceLogEntry>());
    registerNotification("PowerBalanceLogEntryAdded", description, params);

    params.clear();
    description = "Emitted whenever a entry is added to the thing power log.";
    params.insert("sampleRate", enumRef<EnergyLogs::SampleRate>());
    params.insert("thingPowerLogEntry", objectRef<ThingPowerLogEntry>());
    registerNotification("ThingPowerLogEntryAdded", description, params);

    connect(m_energyManager, &EnergyManager::rootMeterChanged, this, [=](){
        QVariantMap params;
        if (m_energyManager->rootMeter()) {
            params.insert("rootMeterThingId", m_energyManager->rootMeter()->id());
        }
        emit RootMeterChanged(params);
    });

    connect(m_energyManager, &EnergyManager::powerBalanceChanged, this, [=](){
        QVariantMap params;
        params.insert("currentPowerConsumption", m_energyManager->currentPowerConsumption());
        params.insert("currentPowerProduction", m_energyManager->currentPowerProduction());
        params.insert("currentPowerAcquisition", m_energyManager->currentPowerAcquisition());
        emit PowerBalanceChanged(params);
    });

    connect(m_energyManager->logs(), &EnergyLogs::powerBalanceEntryAdded, this, [=](EnergyLogs::SampleRate sampleRate, const PowerBalanceLogEntry &entry){
        QVariantMap params;
        params.insert("sampleRate", enumValueName(sampleRate));
        params.insert("powerBalanceLogEntry", pack(entry));
        emit PowerBalanceLogEntryAdded(params);
    });

    connect(m_energyManager->logs(), &EnergyLogs::thingPowerEntryAdded, this, [=](EnergyLogs::SampleRate sampleRate, const ThingPowerLogEntry &entry){
        QVariantMap params;
        params.insert("sampleRate", enumValueName(sampleRate));
        params.insert("thingPowerLogEntry", pack(entry));
        emit ThingPowerLogEntryAdded(params);
    });
}

QString EnergyJsonHandler::name() const
{
    return "Energy";
}

JsonReply *EnergyJsonHandler::GetRootMeter(const QVariantMap &params)
{
    Q_UNUSED(params)
    QVariantMap ret;
    if (m_energyManager->rootMeter()) {
        ret.insert("rootMeterThingId", m_energyManager->rootMeter()->id());
    }
    return createReply(ret);
}

JsonReply *EnergyJsonHandler::SetRootMeter(const QVariantMap &params)
{
    QVariantMap returns;

    if (!params.contains("rootMeterThingId")) {
        returns.insert("energyError", enumValueName(EnergyManager::EnergyErrorMissingParameter));
        return createReply(returns);
    }
    EnergyManager::EnergyError status = m_energyManager->setRootMeter(params.value("rootMeterThingId").toUuid());
    returns.insert("energyError", enumValueName(status));
    return createReply(returns);
}

JsonReply *EnergyJsonHandler::GetPowerBalance(const QVariantMap &params)
{
    Q_UNUSED(params)
    QVariantMap ret;
    ret.insert("currentPowerConsumption", m_energyManager->currentPowerConsumption());
    ret.insert("currentPowerProduction", m_energyManager->currentPowerProduction());
    ret.insert("currentPowerAcquisition", m_energyManager->currentPowerAcquisition());
    return createReply(ret);
}

JsonReply *EnergyJsonHandler::GetPowerBalanceLogs(const QVariantMap &params)
{
    qCDebug(dcEnergyExperience()) << "params" << params;
    qCDebug(dcEnergyExperience()) << "from" << params.value("from");
    EnergyLogs::SampleRate sampleRate = enumNameToValue<EnergyLogs::SampleRate>(params.value("sampleRate").toString());
    QDateTime from = params.contains("from") ? QDateTime::fromMSecsSinceEpoch(params.value("from").toLongLong() * 1000) : QDateTime();
    qCDebug(dcEnergyExperience()) << "from2" << from;
    QDateTime to = params.contains("to") ? QDateTime::fromMSecsSinceEpoch(params.value("to").toLongLong() * 1000) : QDateTime();
    QVariantMap returns;
    returns.insert("powerBalanceLogEntries", pack(m_energyManager->logs()->powerBalanceLogs(sampleRate, from, to)));
    return createReply(returns);
}

JsonReply *EnergyJsonHandler::GetThingPowerLogs(const QVariantMap &params)
{
    EnergyLogs::SampleRate sampleRate = enumNameToValue<EnergyLogs::SampleRate>(params.value("sampleRate").toString());
    QList<ThingId> thingIds;
    foreach (const QVariant &thingId, params.value("thingIds").toList()) {
        thingIds.append(thingId.toUuid());
    }
    QDateTime from = params.contains("from") ? QDateTime::fromMSecsSinceEpoch(params.value("from").toLongLong() * 1000) : QDateTime();
    QDateTime to = params.contains("to") ? QDateTime::fromMSecsSinceEpoch(params.value("to").toLongLong() * 1000) : QDateTime();
    QVariantMap returns;
    returns.insert("thingPowerLogEntries",pack(m_energyManager->logs()->thingPowerLogs(sampleRate, thingIds, from, to)));
    return createReply(returns);
}
