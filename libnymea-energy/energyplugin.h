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

#ifndef ENERGYPLUGIN_H
#define ENERGYPLUGIN_H

#include <QObject>

#include <jsonrpc/jsonrpcserver.h>
#include <integrations/thingmanager.h>

#include "energymanager.h"

class EnergyPlugin : public QObject
{
    Q_OBJECT
public:
    explicit EnergyPlugin(QObject *parent = nullptr);
    virtual ~EnergyPlugin() = default;

    virtual void init() = 0;

protected:
    EnergyManager *energyManager() const;
    ThingManager *thingManager() const;
    JsonRPCServer *jsonRpcServer() const;

private:
    friend class ExperiencePluginEnergy;
    void initPlugin(EnergyManager *energyManager, ThingManager *thingManager, JsonRPCServer *jsonRPCServer);

    EnergyManager *m_energyManager = nullptr;
    ThingManager *m_thingManager = nullptr;
    JsonRPCServer *m_jsonRpcServer = nullptr;
};

Q_DECLARE_INTERFACE(EnergyPlugin, "io.nymea.EnergyPlugin")

#endif // ENERGYPLUGIN_H
