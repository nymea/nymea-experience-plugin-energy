// SPDX-License-Identifier: LGPL-3.0-or-later

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
*
* Copyright (C) 2013 - 2024, nymea GmbH
* Copyright (C) 2024 - 2025, chargebyte austria GmbH
*
* This file is part of libnymea-energy.
*
* libnymea-energy is free software: you can redistribute it and/or
* modify it under the terms of the GNU Lesser General Public License
* as published by the Free Software Foundation, either version 3
* of the License, or (at your option) any later version.
*
* libnymea-energy is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU Lesser General Public License for more details.
*
* You should have received a copy of the GNU Lesser General Public License
* along with libnymea-energy. If not, see <https://www.gnu.org/licenses/>.
*
* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#ifndef ENERGYMANAGER_H
#define ENERGYMANAGER_H

#include "energylogs.h"

#include <QObject>

#include <integrations/thing.h>


class EnergyManager : public QObject
{
    Q_OBJECT
public:
    enum EnergyError {
        EnergyErrorNoError,
        EnergyErrorMissingParameter,
        EnergyErrorInvalidParameter,
    };
    Q_ENUM(EnergyError)

    explicit EnergyManager(QObject *parent = nullptr);
    virtual ~EnergyManager() = default;

    virtual EnergyError setRootMeter(const ThingId &rootMeterId) = 0;
    virtual Thing *rootMeter() const = 0;

    virtual double currentPowerConsumption() const = 0;
    virtual double currentPowerProduction() const = 0;
    virtual double currentPowerAcquisition() const = 0;
    virtual double currentPowerStorage() const = 0;
    virtual double totalConsumption() const = 0;
    virtual double totalProduction() const = 0;
    virtual double totalAcquisition() const = 0;
    virtual double totalReturn() const = 0;

    virtual EnergyLogs *logs() const = 0;

signals:
    void rootMeterChanged();
    void powerBalanceChanged();
};

#endif // ENERGYMANAGER_H
