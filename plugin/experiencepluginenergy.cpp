/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
*
* Copyright 2013 - 2025, nymea GmbH
* Contact: contact@nymea.io
*
* This file is part of nymea.
* This project including source code and documentation is protected by
* copyright law, and remains the property of nymea GmbH. All rights, including
* reproduction, publication, editing and translation, are reserved. The use of
* this project is subject to the terms of a license agreement to be concluded
* with nymea GmbH in accordance with the terms of use of nymea GmbH, available
* under https://nymea.io/license
*
* GNU General Public License Usage
* Alternatively, this project may be redistributed and/or modified under the
* terms of the GNU General Public License as published by the Free Software
* Foundation, GNU version 3. This project is distributed in the hope that it
* will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty
* of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
* Public License for more details.
*
* You should have received a copy of the GNU General Public License along with
* this project. If not, see <https://www.gnu.org/licenses/>.
*
* For any further details and any questions please contact us under
* contact@nymea.io or see our FAQ/Licensing Information on
* https://nymea.io/license/faq
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
