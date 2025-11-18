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

#include "experiencepluginenergy.h"

#include "energymanagerimpl.h"
#include "energyjsonhandler.h"

#include "energyplugin.h"

#include <jsonrpc/jsonrpcserver.h>
#include <loggingcategories.h>

#include <QDir>
#include <QCoreApplication>
#include <QPluginLoader>

NYMEA_LOGGING_CATEGORY(dcEnergyExperience, "EnergyExperience")

ExperiencePluginEnergy::ExperiencePluginEnergy()
{

}

void ExperiencePluginEnergy::init()
{
    qCDebug(dcEnergyExperience()) << "Initializing energy experience";

    m_energyManager = new EnergyManagerImpl(thingManager(), this);
    jsonRpcServer()->registerExperienceHandler(new EnergyJsonHandler(m_energyManager, this), 1, 0);

    loadPlugins();
}

void ExperiencePluginEnergy::loadPlugins()
{
    foreach (const QString &path, pluginSearchDirs()) {
        QDir dir(path);
        qCDebug(dcEnergyExperience()) << "Loading energy plugins from:" << dir.absolutePath();
        foreach (const QString &entry, dir.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot)) {
            QFileInfo fi(path + "/" + entry);
            if (fi.isFile()) {
                if (entry.startsWith("libnymea_energyplugin") && entry.endsWith(".so")) {
                    loadEnergyPlugin(path + "/" + entry);
                }
            } else if (fi.isDir()) {
                if (QFileInfo::exists(path + "/" + entry + "/libnymea_energyplugin" + entry + ".so")) {
                    loadEnergyPlugin(path + "/" +  entry + "/libnymea_energyplugin" + entry + ".so");
                }
            }
        }
    }
}

QStringList ExperiencePluginEnergy::pluginSearchDirs() const
{
    const char *envDefaultPath = "NYMEA_ENERGY_PLUGINS_PATH";
    const char *envExtraPath = "NYMEA_ENERGY_PLUGINS_EXTRA_PATH";

    QStringList searchDirs;
    QByteArray envExtraPathData = qgetenv(envExtraPath);
    if (!envExtraPathData.isEmpty()) {
        searchDirs << QString::fromUtf8(envExtraPathData).split(':');
    }

    if (qEnvironmentVariableIsSet(envDefaultPath)) {
        QByteArray envDefaultPathData = qgetenv(envDefaultPath);
        if (!envDefaultPathData.isEmpty()) {
            searchDirs << QString::fromUtf8(envDefaultPathData).split(':');
        }
    } else {
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        foreach (QString libraryPath, QCoreApplication::libraryPaths()) {
            searchDirs << libraryPath.replace("qt5", "nymea").replace("plugins", "energy");
        }
#else
        foreach (QString libraryPath, QCoreApplication::libraryPaths()) {
            searchDirs << libraryPath.replace("qt6", "nymea").replace("plugins", "energy");
        }
#endif

        searchDirs << QDir(QCoreApplication::applicationDirPath() + "/../lib/nymea/energy").absolutePath();
        searchDirs << QDir(QCoreApplication::applicationDirPath() + "/../energy/").absolutePath();
        searchDirs << QDir(QCoreApplication::applicationDirPath() + "/../../../energy/").absolutePath();
    }

    searchDirs.removeDuplicates();
    return searchDirs;
}

void ExperiencePluginEnergy::loadEnergyPlugin(const QString &file)
{
    QPluginLoader loader;
    loader.setFileName(file);
    loader.setLoadHints(QLibrary::ResolveAllSymbolsHint);
    if (!loader.load()) {
        qCWarning(dcExperiences()) << loader.errorString();
        return;
    }
    EnergyPlugin *plugin = qobject_cast<EnergyPlugin *>(loader.instance());
    if (!plugin) {
        qCWarning(dcEnergyExperience()) << "Could not get plugin instance of" << loader.fileName();
        loader.unload();
        return;
    }
    qCDebug(dcEnergyExperience()) << "Loaded energy plugin:" << loader.fileName();
    m_plugins.append(plugin);
    plugin->setParent(this);
    plugin->initPlugin(m_energyManager, thingManager(), jsonRpcServer());

}
