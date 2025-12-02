// SPDX-License-Identifier: GPL-3.0-or-later

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
*
* Copyright (C) 2013 - 2024, nymea GmbH
* Copyright (C) 2024 - 2025, chargebyte austria GmbH
*
* This file is part of nymea-experience-plugin-energy.
*
* nymea-experience-plugin-energy is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* nymea-experience-plugin-energy is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with nymea-experience-plugin-energy. If not, see <https://www.gnu.org/licenses/>.
*
* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#ifndef ENERGYJSONHANDLER_H
#define ENERGYJSONHANDLER_H

#include <QObject>
#include <jsonrpc/jsonhandler.h>

class EnergyManager;

class EnergyJsonHandler : public JsonHandler
{
    Q_OBJECT
public:
    explicit EnergyJsonHandler(EnergyManager *energyManager, QObject *parent = nullptr);

    QString name() const override;

    Q_INVOKABLE JsonReply *GetRootMeter(const QVariantMap &params);
    Q_INVOKABLE JsonReply *SetRootMeter(const QVariantMap &params);
    Q_INVOKABLE JsonReply *GetPowerBalance(const QVariantMap &params);
    Q_INVOKABLE JsonReply *GetPowerBalanceLogs(const QVariantMap &params);
    Q_INVOKABLE JsonReply *GetThingPowerLogs(const QVariantMap &params);

signals:
    void RootMeterChanged(const QVariantMap &params);
    void PowerBalanceChanged(const QVariantMap &params);
    void PowerBalanceLogEntryAdded(const QVariantMap &params);
    void ThingPowerLogEntryAdded(const QVariantMap &params);

private:
    EnergyManager *m_energyManager = nullptr;
};

#endif // ENERGYJSONHANDLER_H
