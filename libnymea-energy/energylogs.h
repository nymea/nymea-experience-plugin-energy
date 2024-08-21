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
* GNU Lesser General Public License Usage
* Alternatively, this project may be redistributed and/or modified under the
* terms of the GNU Lesser General Public License as published by the Free
* Software Foundation; version 3. This project is distributed in the hope that
* it will be useful, but WITHOUT ANY WARRANTY; without even the implied
* warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
* Lesser General Public License for more details.
*
* You should have received a copy of the GNU Lesser General Public License
* along with this project. If not, see <https://www.gnu.org/licenses/>.
*
* For any further details and any questions please contact us under
* contact@nymea.io or see our FAQ/Licensing Information on
* https://nymea.io/license/faq
*
* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#ifndef ENERGYLOGS_H
#define ENERGYLOGS_H

#include <QObject>
#include <QDateTime>
#include <QVariant>

#include <typeutils.h>

class PowerBalanceLogEntry;
class PowerBalanceLogEntries;
class ThingPowerLogEntry;
class ThingPowerLogEntries;

class EnergyLogs: public QObject
{
    Q_OBJECT
public:
    EnergyLogs(QObject *parent = nullptr);
    virtual ~EnergyLogs() = default;

    enum SampleRate {
        SampleRateAny = 0,
        SampleRate1Min = 1,
        SampleRate15Mins = 15,
        SampleRate1Hour = 60,
        SampleRate3Hours = 180,
        SampleRate1Day = 1440,
        SampleRate1Week = 10080,
        SampleRate1Month = 43200,
        SampleRate1Year = 525600
    };
    Q_ENUM(SampleRate)

    /*! Returns logs for the given sample rate for total household consumption, production, acquisition and storage balance.
     *  From and to may be given to limit results to a time span.
    */
    virtual PowerBalanceLogEntries powerBalanceLogs(SampleRate sampleRate, const QDateTime &from = QDateTime(), const QDateTime &to = QDateTime()) const = 0;

    /*! Returns logs for the given sample rate for currentPower, totalEnergyConsumed and totalEnergyProduced for the given things.
     *  From and to may be given to limit results to a time span.
     *  If thingIds is empty, all things will be returned.
     */
    virtual ThingPowerLogEntries thingPowerLogs(SampleRate sampleRate, const QList<ThingId> &thingIds, const QDateTime &from = QDateTime(), const QDateTime &to = QDateTime()) const = 0;

signals:
    void powerBalanceEntryAdded(SampleRate sampleRate, const PowerBalanceLogEntry &entry);
    void thingPowerEntryAdded(SampleRate sampleRate, const ThingPowerLogEntry &entry);

};


class PowerBalanceLogEntry
{
    Q_GADGET
    Q_PROPERTY(QDateTime timestamp READ timestamp)
    Q_PROPERTY(double consumption READ consumption)
    Q_PROPERTY(double production READ production)
    Q_PROPERTY(double acquisition READ acquisition)
    Q_PROPERTY(double storage READ storage)
    Q_PROPERTY(double totalConsumption READ totalConsumption)
    Q_PROPERTY(double totalProduction READ totalProduction)
    Q_PROPERTY(double totalAcquisition READ totalAcquisition)
    Q_PROPERTY(double totalReturn READ totalReturn)
public:
    PowerBalanceLogEntry();
    PowerBalanceLogEntry(const QDateTime &timestamp, double consumption, double production, double acquisition, double storage, double totalConsumption, double totalProduction, double totalAcquisition, double totalReturn);
    QDateTime timestamp() const;
    double consumption() const;
    double production() const;
    double acquisition() const;
    double storage() const;
    double totalConsumption() const;
    double totalProduction() const;
    double totalAcquisition() const;
    double totalReturn() const;

private:
    QDateTime m_timestamp;
    double m_consumption = 0;
    double m_production = 0;
    double m_acquisition = 0;
    double m_storage = 0;
    double m_totalConsumption = 0;
    double m_totalProduction = 0;
    double m_totalAcquisition = 0;
    double m_totalReturn = 0;
};
Q_DECLARE_METATYPE(PowerBalanceLogEntry)

class PowerBalanceLogEntries: public QList<PowerBalanceLogEntry>
{
    Q_GADGET
    Q_PROPERTY(int count READ count)
public:
    PowerBalanceLogEntries() = default;
    PowerBalanceLogEntries(const QList<PowerBalanceLogEntry> &other);
    Q_INVOKABLE QVariant get(int index) const;
    Q_INVOKABLE void put(const QVariant &variant);
};
Q_DECLARE_METATYPE(PowerBalanceLogEntries)

class ThingPowerLogEntry {
    Q_GADGET
    Q_PROPERTY(QDateTime timestamp READ timestamp)
    Q_PROPERTY(QUuid thingId READ thingId)
    Q_PROPERTY(double currentPower READ currentPower)
    Q_PROPERTY(double totalConsumption READ totalConsumption)
    Q_PROPERTY(double totalProduction READ totalProduction)
public:
    ThingPowerLogEntry();
    ThingPowerLogEntry(const QDateTime &timestamp, const ThingId &thingId, double currentPower, double totalConsumption, double totalProuction);
    QDateTime timestamp() const;
    ThingId thingId() const;
    double currentPower() const;
    double totalConsumption() const;
    double totalProduction() const;
private:
    QDateTime m_timestamp;
    ThingId m_thingId;
    double m_currentPower = 0;
    double m_totalConsumption = 0;
    double m_totalProduction = 0;
};
Q_DECLARE_METATYPE(ThingPowerLogEntry)

class ThingPowerLogEntries: public QList<ThingPowerLogEntry>
{
    Q_GADGET
    Q_PROPERTY(int count READ count)
public:
    ThingPowerLogEntries() = default;
    ThingPowerLogEntries(const QList<ThingPowerLogEntry> &other): QList<ThingPowerLogEntry>(other) {}
    Q_INVOKABLE QVariant get(int index) const;
    Q_INVOKABLE void put(const QVariant &variant);
};
Q_DECLARE_METATYPE(ThingPowerLogEntries)

#endif // ENERGYLOGS_H
