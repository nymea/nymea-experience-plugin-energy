#include "energyjsonhandler.h"
#include "energymanagerimpl.h"

Q_DECLARE_LOGGING_CATEGORY(dcEnergyExperience)

EnergyJsonHandler::EnergyJsonHandler(EnergyManager *energyManager, QObject *parent):
    JsonHandler(parent),
    m_energyManager(energyManager)
{
    registerEnum<EnergyManager::EnergyError>();

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

    params.clear();
    description = "Emitted whenever the root meter id changes. If the root meter has been unset, the params will be empty.";
    params.insert("o:rootMeterThingId", enumValueName(Uuid));
    registerNotification("RootMeterChanged", description, params);

    params.clear();
    description = "Emitted whenever the energy balance changes. That is, when the current consumption, production or acquisition changes. Typically they will all change at the same time.";
    params.insert("currentPowerConsumption", enumValueName(Double));
    params.insert("currentPowerProduction", enumValueName(Double));
    params.insert("currentPowerAcquisition", enumValueName(Double));
    registerNotification("PowerBalanceChanged", description, params);

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
