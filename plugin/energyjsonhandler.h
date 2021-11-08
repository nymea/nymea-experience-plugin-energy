#ifndef ENERGYJSONHANDLER_H
#define ENERGYJSONHANDLER_H

#include <QObject>
#include "jsonrpc/jsonhandler.h"

class EnergyManager;

class EnergyJsonHandler : public JsonHandler
{
    Q_OBJECT
public:
    explicit EnergyJsonHandler(EnergyManager *energyManager, QObject *parent = nullptr);

    QString name() const override;

    Q_INVOKABLE JsonReply* GetRootMeter(const QVariantMap &params);
    Q_INVOKABLE JsonReply* SetRootMeter(const QVariantMap &params);
    Q_INVOKABLE JsonReply* GetPowerBalance(const QVariantMap &params);

signals:
    void RootMeterChanged(const QVariantMap &params);

    void PowerBalanceChanged(const QVariantMap &params);

private:
    EnergyManager *m_energyManager = nullptr;

};

#endif // ENERGYJSONHANDLER_H
