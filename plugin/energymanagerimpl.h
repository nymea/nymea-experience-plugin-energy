/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
*
* Copyright 2013 - 2024, nymea GmbH
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

#ifndef ENERGYMANAGERIMPL_H
#define ENERGYMANAGERIMPL_H

#include <QObject>
#include <QHash>
#include <QTimer>

#include "integrations/thingmanager.h"

#include "energymanager.h"

class EnergyLogger;

class EnergyManagerImpl : public EnergyManager
{
    Q_OBJECT
public:
    explicit EnergyManagerImpl(ThingManager *thingManager, QObject *parent = nullptr);

    Thing *rootMeter() const override;
    EnergyError setRootMeter(const ThingId &rootMeterId) override;

    double currentPowerConsumption() const override;
    double currentPowerProduction() const override;
    double currentPowerAcquisition() const override;
    double currentPowerStorage() const override;
    double totalConsumption() const override;
    double totalProduction() const override;
    double totalAcquisition() const override;
    double totalReturn() const override;

    EnergyLogs* logs() const override;

private:
    void watchThing(Thing *thing);
    void unwatchThing(const ThingId &thingId);

    void updatePowerBalance();
    void updateThingPower(Thing *thing);

private slots:
    void logDumpConsumers();

private:
    ThingManager *m_thingManager = nullptr;

    Thing *m_rootMeter = nullptr;

    QTimer m_balanceUpdateTimer;
    double m_currentPowerConsumption = 0;
    double m_currentPowerProduction = 0;
    double m_currentPowerAcquisition = 0;
    double m_currentPowerStorage = 0;
    double m_totalConsumption = 0;
    double m_totalProduction = 0;
    double m_totalAcquisition = 0;
    double m_totalReturn = 0;

    EnergyLogger *m_logger = nullptr;

    // Caching some values so we don't have to look them up on the DB all the time:
    // We use different caches for power balance and thing logs because they are calculated independently
    // and one must not update the others cache for the diffs to be correct

    // For things totals we need to cache 2 values:
    // The last thing state values we've processed
    QHash<Thing*, double> m_powerBalanceTotalEnergyConsumedCache;
    QHash<Thing*, double> m_powerBalanceTotalEnergyProducedCache;

    // - The last thing state value we've read and processed
    // - The last entry in our internal counters we've processed and logged
    // QHash<Thing*, Pair<thingStateValue, internalValue>>
    QHash<Thing*, QPair<double, double>> m_thingsTotalEnergyConsumedCache;
    QHash<Thing*, QPair<double, double>> m_thingsTotalEnergyProducedCache;
};

#endif // ENERGYMANAGERIMPL_H
