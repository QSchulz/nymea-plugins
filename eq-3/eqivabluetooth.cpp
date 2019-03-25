#include "eqivabluetooth.h"

#include "extern-plugininfo.h"

#include <QDataStream>
#include <QDateTime>

const QBluetoothUuid eqivaServiceUuid = QBluetoothUuid(QString("{3e135142-654f-9090-134a-a6ff5bb77046}"));
const QBluetoothUuid commandCharacteristicUuid = QBluetoothUuid(QString("3fa4585a-ce4a-3bad-db4b-b8df8179ea09"));
const QBluetoothUuid notificationCharacteristicUuid = QBluetoothUuid(QString("d0e8434d-cd29-0996-af41-6c90f4e0eb2a"));

const QBluetoothUuid eqivaDeviceInfoServiceUuid = QBluetoothUuid(QString("9e5d1e47-5c13-43a0-8635-82ad38a1386f"));

// Protocol
// Commands:
const quint8 commandSetDate =   0x03;
const quint8 commandSetMode =   0x40;
const quint8 commandSetTemp =   0x41;
const quint8 commandBoost =     0x45;
const quint8 commandLock =      0x80;

// Notifications
const quint8 notifyHeader =     0x02;
const quint8 profileReadReply = 0x21;

const quint8 notifyStatus =     0x01;
const quint8 notifyProfile =    0x02;

// Values:
const quint8 valueOn =          0x01;
const quint8 valueOff =         0x00;

const quint8 modeAuto =         0x08;
const quint8 modeManual =       0x09;
const quint8 modeHoliday =      0x0A;
const quint8 modeBoost1 =       0x0C; // at the end it returns to automatic mode
const quint8 modeBoost2 =       0x0D; // at the end it returns to manual mode
const quint8 modeBoost3 =       0x0E; // at the end it returns to holiday mode

const quint8 lockOff =          0x00;
const quint8 windowLockOn =     0x10;
const quint8 keyLockOn =        0x20;
const quint8 windowAndKeyOn =   0x30;

const quint8 setModeAuto =    0x00;
const quint8 setModeManual =  0x40;
const quint8 setModeHoliday = 0x40; // Same as manual but with a time limit

EqivaBluetooth::EqivaBluetooth(BluetoothLowEnergyManager *bluetoothManager, const QBluetoothAddress &hostAddress, const QString &name, QObject *parent):
    QObject(parent),
    m_bluetoothManager(bluetoothManager),
    m_name(name)
{

    QBluetoothDeviceInfo deviceInfo = QBluetoothDeviceInfo(hostAddress, QString(), 0);
    m_bluetoothDevice = m_bluetoothManager->registerDevice(deviceInfo, QLowEnergyController::PublicAddress);
    connect(m_bluetoothDevice, &BluetoothLowEnergyDevice::stateChanged, this, &EqivaBluetooth::controllerStateChanged);
    m_bluetoothDevice->connectDevice();

    m_refreshTimer.setInterval(30000);
    m_refreshTimer.setSingleShot(true);
    connect(&m_refreshTimer, &QTimer::timeout, this, &EqivaBluetooth::sendDate);
}

EqivaBluetooth::~EqivaBluetooth()
{
    m_bluetoothManager->unregisterDevice(m_bluetoothDevice);
}

void EqivaBluetooth::setName(const QString &name)
{
    m_name = name;
}

bool EqivaBluetooth::available() const
{
    return m_available;
}

bool EqivaBluetooth::enabled() const
{
    return m_enabled;
}

int EqivaBluetooth::setEnabled(bool enabled)
{
    emit enabledChanged();
    return setTargetTemperature(enabled ? m_cachedTargetTemp : 4.5);
}

bool EqivaBluetooth::locked() const
{
    return m_locked;
}

int EqivaBluetooth::setLocked(bool locked)
{
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream << commandLock;
    stream << (locked ? valueOn : valueOff);
    return enqueue("SetLocked", data);
}

bool EqivaBluetooth::boostEnabled() const
{
    return m_boostEnabled;
}

int EqivaBluetooth::setBoostEnabled(bool enabled)
{
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream << commandBoost;
    stream << (enabled ? valueOn : valueOff);
    return enqueue("SetBoostEnabled", data);
}

qreal EqivaBluetooth::targetTemperature() const
{
    return m_enabled ? m_targetTemp : m_cachedTargetTemp;
}

int EqivaBluetooth::setTargetTemperature(qreal targetTemperature)
{
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream << commandSetTemp;
    if (targetTemperature == 4.5) {
        stream << static_cast<quint8>(4.5 * 2); // 4.5 degrees is off
    } else {
        stream << static_cast<quint8>(targetTemperature * 2); // Temperature *2 (device only supports .5 precision)
        m_cachedTargetTemp = targetTemperature;
    }
    return enqueue("SetTargetTemperature", data);
}

EqivaBluetooth::Mode EqivaBluetooth::mode() const
{
    return m_mode;
}

int EqivaBluetooth::setMode(EqivaBluetooth::Mode mode)
{
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream << commandSetMode;
    switch (mode) {
    case ModeAuto:
        stream << setModeAuto;
        break;
    case ModeManual:
        stream << setModeManual;
        break;
    case ModeHoliday:
        stream << setModeHoliday;

        // Holiday mode would support adding a timestamp in the format:
        // temperature*2 + 128, day, year, hour-and-minute, month
        // Given we can't have params with states and in the nymea context this doesn't make too much sense anyways
        // we're just gonna switch to manual mode here... Any coming-come automatism should be handled by nymea anyways.
        break;
    }
    qCDebug(dcEQ3()) << m_name << "Setting mode to" << data.toHex();
    return enqueue("SetMode", data);
}

bool EqivaBluetooth::windowOpen() const
{
    return m_windowOpen;
}

quint8 EqivaBluetooth::valveOpen() const
{
    return m_valveOpen;
}

void EqivaBluetooth::controllerStateChanged(const QLowEnergyController::ControllerState &state)
{
    qCDebug(dcEQ3()) << m_name << "Bluetooth device state changed:" << state;
    if (state == QLowEnergyController::UnconnectedState) {
        int delay = qMin(m_reconnectAttempt, 30);
        qWarning(dcEQ3()) << m_name << "Eqiva device disconnected. Reconnecting in" << delay << "sec";
        m_available = false;
        emit availableChanged();

        if (m_currentCommand.id != -1) {
            qCDebug(dcEQ3()) << m_name << "Putting command" << m_currentCommand.id << m_currentCommand.name << "back to queue";
            m_commandQueue.prepend(m_currentCommand);
            m_currentCommand = Command();
        }

        QTimer::singleShot(delay * 1000, this, [this](){
            qCDebug(dcEQ3()) << m_name << "Trying to reconnect";
            m_reconnectAttempt++;
            m_bluetoothDevice->connectDevice();
        });
    }

    if (state != QLowEnergyController::DiscoveredState) {
        return;
    }

//    qCDebug(dcEQ3()) << m_name << "Discovered: Service UUIDS:" << m_bluetoothDevice->serviceUuids();
    m_eqivaService = m_bluetoothDevice->controller()->createServiceObject(eqivaServiceUuid, this);
//    m_eqivaService = m_bluetoothDevice->controller()->createServiceObject(QBluetoothUuid(QString("00001800-0000-1000-8000-00805f9b34fb")), this);
//    m_eqivaService = m_bluetoothDevice->controller()->createServiceObject(QBluetoothUuid(QString("00001801-0000-1000-8000-00805f9b34fb")), this);
//    m_eqivaService = m_bluetoothDevice->controller()->createServiceObject(QBluetoothUuid(QString("0000180a-0000-1000-8000-00805f9b34fb")), this);
//    m_eqivaService = m_bluetoothDevice->controller()->createServiceObject(QBluetoothUuid(QString("9e5d1e47-5c13-43a0-8635-82ad38a1386f")), this);
    connect(m_eqivaService, &QLowEnergyService::stateChanged, this, &EqivaBluetooth::serviceStateChanged);
//    connect(m_eqivaService, &QLowEnergyService::characteristicRead, this, [](const QLowEnergyCharacteristic &info, const QByteArray &value){
//        qCDebug(dcEQ3()) << m_name << "Characteristic read:" << info.name() << info.uuid() << value.toHex();
//    });
    connect(m_eqivaService, &QLowEnergyService::characteristicWritten, this, [this](const QLowEnergyCharacteristic &info, const QByteArray &value){
        qCDebug(dcEQ3()) << m_name << "Characteristic written:" << info.name() << info.uuid() << value.toHex();
        emit commandResult(m_currentCommand.id, true);
        m_currentCommand.id = -1;
        processCommandQueue();
    });
    connect(m_eqivaService, &QLowEnergyService::characteristicChanged, this, &EqivaBluetooth::characteristicChanged);
    m_eqivaService->discoverDetails();

}

void EqivaBluetooth::serviceStateChanged(QLowEnergyService::ServiceState newState)
{
    qCDebug(dcEQ3()) << m_name << "Service state changed:" << newState;
    if (newState != QLowEnergyService::ServiceDiscovered) {
        return;
    }

    m_available = true;
    m_reconnectAttempt = 0;
    emit availableChanged();

//    // Debug...
//    foreach (const QLowEnergyCharacteristic &characteristic, m_eqivaService->characteristics()) {
//        qCDebug(dcEQ3()).nospace().noquote() << "C:    --> " << characteristic.uuid().toString() << " (Handle 0x" << QString("%1").arg(characteristic.handle(), 0, 16) << " Name: " << characteristic.name() << "): " << characteristic.value().toHex() << characteristic.value();
//        foreach (const QLowEnergyDescriptor &descriptor, characteristic.descriptors()) {
//            qCDebug(dcEQ3()).nospace().noquote() << "D:        --> " << descriptor.uuid().toString() << " (Handle 0x" << QString("%2").arg(descriptor.handle(), 0, 16) << " Name: " << descriptor.name() << "): " << descriptor.value().toHex() << characteristic.value();
//        }
//    }

    sendDate();
}

void EqivaBluetooth::characteristicChanged(const QLowEnergyCharacteristic &info, const QByteArray &value)
{
    qCDebug(dcEQ3()) << m_name << "Notification received" << info.uuid() << value.toHex();
    QByteArray data(value);
    QDataStream stream(&data, QIODevice::ReadOnly);
    quint8 header;
    stream >> header;
    if (header == notifyHeader) {
        quint8 notificationType;
        stream >> notificationType;

        if (notificationType == notifyStatus) {
            quint8 lockAndMode;
            stream >> lockAndMode;
            stream >> m_valveOpen;
            quint8 undefined;
            stream >> undefined;
            quint8 rawTemp;
            stream >> rawTemp;

            quint8 lock = (lockAndMode & 0xF0);
            m_locked = (lock == keyLockOn) || (lock == windowAndKeyOn);
            m_windowOpen = (lock == windowLockOn) || (lock == windowAndKeyOn);
            quint8 mode = (lockAndMode & 0x0F);
            m_targetTemp = 1.0 * rawTemp / 2;
            m_enabled = m_targetTemp >= 5;
            if (m_targetTemp < 5) {
                m_targetTemp = 5;
            }
            qCDebug(dcEQ3()) << m_name << "Status notification received: Enabled:" << m_enabled << "Temp:" << m_targetTemp << "Keylock:" << m_locked << "Window open:" << m_windowOpen << "Mode:" << mode << "Valve open:" << m_valveOpen << "Boost:" << m_boostEnabled;

            m_boostEnabled = false;
            switch (mode) {
            case modeManual:
                m_mode = ModeManual;
                break;
            case modeAuto:
                m_mode = ModeAuto;
                break;
            case modeBoost1:
                m_boostEnabled = true;
                m_mode = ModeAuto;
                break;
            case modeBoost2:
                m_boostEnabled = true;
                m_mode = ModeManual;
                break;
            case modeBoost3:
                m_boostEnabled = true;
                m_mode = ModeHoliday;
                break;
            case modeHoliday:
                m_mode = ModeHoliday;
                break;
            }

            emit enabledChanged();
            emit lockedChanged();
            emit boostEnabledChanged();
            emit modeChanged();
            emit windowOpenChanged();
            emit targetTemperatureChanged();
            emit valveOpenChanged();

            m_refreshTimer.start();
        } else if (notificationType == notifyProfile) {
            // Profile updates not implemented
        } else {
            qCWarning(dcEQ3()) << m_name << "Unknown notification type" << notificationType;
        }

    } else if (header == profileReadReply) {
        // Profile read not implemented...
    } else {
        qCWarning(dcEQ3()) << m_name << "Unhandled notification from device:" << value.toHex();
    }

}

void EqivaBluetooth::writeCharacteristic(const QBluetoothUuid &characteristicUuid, const QByteArray &data)
{
    QLowEnergyCharacteristic characteristic = m_eqivaService->characteristic(characteristicUuid);
    m_eqivaService->writeCharacteristic(characteristic, data);
}

void EqivaBluetooth::sendDate()
{
    QDateTime now = QDateTime::currentDateTime();

    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream << commandSetDate;
    stream << static_cast<quint8>(now.date().year() - 2000);
    stream << static_cast<quint8>(now.date().month());
    stream << static_cast<quint8>(now.date().day());
    stream << static_cast<quint8>(now.time().hour());
    stream << static_cast<quint8>(now.time().minute());
    stream << static_cast<quint8>(now.time().second());

    // Example: 03130117172315 -> 03YYMMDDHHMMSS
    enqueue("SetDate", data);
}

int EqivaBluetooth::enqueue(const QString &name, const QByteArray &data)
{
    static int nextId = 0;

    Command cmd;
    cmd.name = name;
    cmd.id = nextId++;
    cmd.data = data;
    m_commandQueue.append(cmd);
    processCommandQueue();
    return cmd.id;
}

void EqivaBluetooth::processCommandQueue()
{
    if (m_currentCommand.id != -1) {
        qCDebug(dcEQ3()) << m_name << "Busy sending a command" << m_currentCommand.id << m_currentCommand.name;
        return;
    }

    if (m_commandQueue.isEmpty()) {
        qCDebug(dcEQ3()) << m_name << "Command queue is empty. Nothing to do...";
        return;
    }

    if (!m_available) {
        qCWarning(dcEQ3()) << m_name << "Not connected. Trying to reconnect before sending commands...";
        m_bluetoothDevice->connectDevice();
        return;
    }

    m_currentCommand = m_commandQueue.takeFirst();
    qCDebug(dcEQ3()) << m_name << "Sending command" << m_currentCommand.id << m_currentCommand.name << m_currentCommand.data.toHex();
    writeCharacteristic(commandCharacteristicUuid, m_currentCommand.data);
}


EqivaBluetoothDiscovery::EqivaBluetoothDiscovery(BluetoothLowEnergyManager *bluetoothManager, QObject *parent):
    QObject(parent),
    m_bluetoothManager(bluetoothManager)
{

}

bool EqivaBluetoothDiscovery::startDiscovery()
{
    if (!m_bluetoothManager->available()) {
        qCWarning(dcEQ3()) << "Bluetooth is not available.";
        return false;
    }

    if (!m_bluetoothManager->enabled()) {
        qCWarning(dcEQ3()) << "Bluetooth is not available.";
        return false;
    }

    qCDebug(dcEQ3()) << "Starting Bluetooth discovery";
    BluetoothDiscoveryReply *reply = m_bluetoothManager->discoverDevices();
    connect(reply, &BluetoothDiscoveryReply::finished, this, &EqivaBluetoothDiscovery::deviceDiscoveryDone);

    return true;
}

void EqivaBluetoothDiscovery::deviceDiscoveryDone()
{
    BluetoothDiscoveryReply *reply = static_cast<BluetoothDiscoveryReply *>(sender());
    reply->deleteLater();

    if (reply->error() != BluetoothDiscoveryReply::BluetoothDiscoveryReplyErrorNoError) {
        qCWarning(dcEQ3()) << "Bluetooth discovery error:" << reply->error();
        return;
    }

    QStringList results;

    qCDebug(dcEQ3()) << "Discovery finished";
    foreach (const QBluetoothDeviceInfo &deviceInfo, reply->discoveredDevices()) {
        qCDebug(dcEQ3()) << "Discovered device" << deviceInfo.name();
        if (deviceInfo.name().contains("CC-RT-BLE")) {
            results.append(deviceInfo.address().toString());
        }
    }

    emit finished(results);
}