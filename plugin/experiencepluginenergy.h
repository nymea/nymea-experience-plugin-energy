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

#ifndef EXPERIENCEPLUGINENERGY_H
#define EXPERIENCEPLUGINENERGY_H

#include <experiences/experienceplugin.h>

#include "energyplugin.h"

#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(dcEnergyExperience)

class EnergyManagerImpl;

class ExperiencePluginEnergy: public ExperiencePlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "io.nymea.ExperiencePlugin")
    Q_INTERFACES(ExperiencePlugin)

public:
    ExperiencePluginEnergy();
    void init() override;

private:
    QStringList pluginSearchDirs() const;
    void loadPlugins();
    void loadEnergyPlugin(const QString &file);

    QList<EnergyPlugin *> m_plugins;

    EnergyManagerImpl *m_energyManager = nullptr;
};

#endif // EXPERIENCEPLUGINENERGY_H
