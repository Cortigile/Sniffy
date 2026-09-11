#include "devicemediator.h"
#include "resourcemanager.h"
#include <QTimer>
#include <QThread>
#include <QStandardPaths>
#include "authenticator.h"
#include "flasher/stlinkconnector.h"
#include "firmwarecompatibility.h"

DeviceMediator::DeviceMediator(Authenticator *auth, QObject *parent) 
    : QObject(parent), authenticator(auth)
{
    communication = new Comms(this);

    connect(communication, SIGNAL(devicesScaned(QList<DeviceDescriptor>)), this, SLOT(newDeviceList(QList<DeviceDescriptor>)), Qt::QueuedConnection);
    connect(communication, &Comms::connectionOpened, this, &DeviceMediator::onConnectionOpened);
    connect(communication, &Comms::connectionClosed, this, &DeviceMediator::onConnectionClosed, Qt::QueuedConnection);
    modules = createModulesList();

    connect(device, &Device::ScanDevices, this, &DeviceMediator::ScanDevices);
    connect(device, &Device::openDevice, this, &DeviceMediator::openDevice);
    connect(device, &Device::closeDevice, this, &DeviceMediator::close);
    connect(device, &Device::deviceSpecificationReady, this, &DeviceMediator::onDeviceSpecificationReady);

    tokenAckTimer = new QTimer(this);
    tokenAckTimer->setSingleShot(true);
    tokenAckTimer->setInterval(3000); // 3 seconds should be more than enough for flash operations
    connect(tokenAckTimer, &QTimer::timeout, this, &DeviceMediator::onTokenAckTimeout);

    deviceSpecificationTimer = new QTimer(this);
    deviceSpecificationTimer->setSingleShot(true);
    deviceSpecificationTimer->setInterval(3000);
    connect(deviceSpecificationTimer, &QTimer::timeout, this, &DeviceMediator::onDeviceSpecificationTimeout);

    // initialize ResourceManager aggregates
    resourceManager.reset();
}

DeviceMediator::~DeviceMediator()
{
    // Modules (Scope, etc.) call comm->write() from their destructors, so they must be
    // torn down before communication - QObject child destruction order isn't guaranteed.
    modules.clear();
}

QList<QSharedPointer<AbstractModule>> DeviceMediator::createModulesList()
{
    QList<QSharedPointer<AbstractModule>> tmpModules;
    tmpModules.append(QSharedPointer<AbstractModule>(device = new Device(this)));
    tmpModules.append(QSharedPointer<AbstractModule>(new Scope(this)));
    tmpModules.append(QSharedPointer<AbstractModule>(new Counter(this)));
    tmpModules.append(QSharedPointer<AbstractModule>(new Voltmeter(this)));
    tmpModules.append(QSharedPointer<AbstractModule>(new SyncPwm(this)));
    tmpModules.append(QSharedPointer<AbstractModule>(new ArbGenerator(this)));
    tmpModules.append(QSharedPointer<AbstractModule>(new ArbGenerator(this, true)));
    tmpModules.append(QSharedPointer<AbstractModule>(new PatternGenerator(this)));
    tmpModules.append(QSharedPointer<AbstractModule>(new VoltageSource(this)));
    //   tmpModules.append(QSharedPointer<AbstractModule> (new TemplateModule(this)));

    for (const QSharedPointer<AbstractModule> &mod : tmpModules)
    {
        connect(mod.data(), &AbstractModule::blockConflictingModules, this, &DeviceMediator::blockConflictingModulesCallback);
        connect(mod.data(), &AbstractModule::releaseConflictingModules, this, &DeviceMediator::releaseConflictingModulesCallback);
        connect(mod.data(), &AbstractModule::moduleDescription, device, &Device::addModuleDescription);
        connect(mod.data(), &AbstractModule::modulePinFunctions, device, &Device::registerModulePinFunctions);
        connect(mod.data(), &AbstractModule::moduleActivePinFunctionsChanged, device, &Device::setModuleActivePinFunctions);
    }

    return tmpModules;
}

QList<QSharedPointer<AbstractModule>> DeviceMediator::getModulesList()
{
    return modules;
}

void DeviceMediator::ScanDevices()
{
    if (firmwareOperationPending) return;
    hasFirmwareTarget = false;
    reconnectFirmwareTarget = false;
    // Explicit Scan button press: never auto-connect.
    // The user wants to see the list and choose. Auto-connect is only
    // for the initial background scan at startup (default true).
    autoConnectOnSingleDevice = false;
    communication->scanForDevices();
}

void DeviceMediator::newDeviceList(QList<DeviceDescriptor> deviceList)
{
    // Ignore scan results while a device is connected (or mid-auth handshake).
    if (isConnected || waitingForTokenAck || pendingReconnectRequest != 0 || firmwareOperationPending)
        return;

    this->deviceList = deviceList;
    if (reconnectFirmwareTarget && hasFirmwareTarget) {
        device->updateGUIDeviceList(deviceList, false);
        for (int index = 0; index < deviceList.size(); ++index) {
            const DeviceDescriptor &candidate = deviceList.at(index);
            const bool matchesTarget = firmwareTargetSerial.isEmpty()
                ? candidate.port == firmwareTarget.port
                : QSerialPortInfo(candidate.port).serialNumber().trimmed().compare(firmwareTargetSerial, Qt::CaseInsensitive) == 0;
            if (matchesTarget && candidate.connType == firmwareTarget.connType) {
                reconnectFirmwareTarget = false;
                device->connectDevice(index);
                return;
            }
        }
        communication->close();
        return;
    }
    device->updateGUIDeviceList(deviceList, autoConnectOnSingleDevice);
}

void DeviceMediator::openDevice(int deviceIndex)
{
    if (firmwareOperationPending) return;
    hasFirmwareTarget = false;
    reconnectFirmwareTarget = false;
    pendingReconnectRequest = 0;
    // Remember which index is currently opened so we can reopen after login
    currentDeviceIndex = deviceIndex;
    communication->open(deviceList.at(deviceIndex));
}

void DeviceMediator::onConnectionOpened(bool success)
{
    const quint64 generation = ++connectionGeneration;
    waitingForDeviceSpecification = false;
    waitingForAuthRecovery = false;
    waitingForTokenAck = false;
    remainingModulesWired = false;
    deviceSpecificationTimer->stop();
    tokenAckTimer->stop();
    isConnected = success;
    isDemoMode = true;
    authenticator->setDemoMode(true);
    if (!isConnected)
    {
        device->errorHandler("Device cannot be opened");
        qDebug() << "ERROR wait for comm to be opened";
        return;
    }

    connect(communication, &Comms::newData, this, &DeviceMediator::parseData);
    connect(communication, &Comms::communicationError, this, &DeviceMediator::handleError);

    pendingDevName = deviceList.at(currentDeviceIndex).deviceName;
    pendingDeviceIndex = currentDeviceIndex;

    // Clear previous right-side specifications before we start receiving CFG_/ACK_ again
    device->clearAllModuleDescriptions();

    // Immediately request MCU reset so it starts in known state.
    // USB-CDC re-enumeration takes ~100ms, give it 150ms total.
    communication->write(Commands::RESET_DEVICE+";");

    QTimer::singleShot(150, this, [this, generation]() {
        if (isConnected && connectionGeneration == generation) {
            requestDeviceSpecification();
        }
    });
}

void DeviceMediator::requestDeviceSpecification()
{
    waitingForDeviceSpecification = true;
    deviceSpecificationTimer->start();
    device->setComms(communication);
}

void DeviceMediator::beginTokenAuthentication()
{
    if (CustomSettings::getLoginToken() == "none") {
        finalizeDeviceOpen(pendingDeviceIndex, pendingDevName);
        return;
    }

    QByteArray token = CustomSettings::getLoginToken();
    if (token.size() == 384) {
        token = QByteArray::fromHex(token);
    }

    waitingForTokenAck = true;
    tokenAckTimer->start();
    communication->write("SYST:MAIL:" + CustomSettings::getUserEmail().toUtf8() + ";");
    communication->write("SYST:TIME:" + QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss").toUtf8() + ";");
    communication->write("TKN_:DATA:" + token + ";");
    qDebug() << "[Auth] Protocol accepted, token sent; waiting for FW ACK before loading modules";
}

void DeviceMediator::reopenDeviceAfterLogin()
{
    // Only reopen if a device was actually connected before this login attempt.
    // Otherwise there's nothing to reconnect - trying to open a stale/absent
    // device just spams "wait for serport to be opened" and fails.
    if (!isConnected || currentDeviceIndex < 0 || deviceList.isEmpty()) return;

    autoConnectOnSingleDevice = false;
    pendingReconnectRequest = shutdownConnection(false);
    ShowDeviceModule();
}

void DeviceMediator::onConnectionClosed(quint64 requestId)
{
    if (firmwareOperationPending && pendingFirmwareCloseRequest == requestId) {
        pendingFirmwareCloseRequest = 0;
        emit firmwareOperationReady();
        return;
    }
    if (pendingReconnectRequest == 0 || pendingReconnectRequest != requestId) {
        return;
    }
    pendingReconnectRequest = 0;
    if (!isConnected && currentDeviceIndex >= 0 && currentDeviceIndex < deviceList.size()) {
        device->connectDevice(currentDeviceIndex);
    }
}

void DeviceMediator::prepareFirmwareOperation()
{
    if (firmwareOperationPending) return;
    firmwareOperationPending = true;
    reconnectFirmwareTarget = false;
    if (isConnected && currentDeviceIndex >= 0 && currentDeviceIndex < deviceList.size()) {
        firmwareTarget = deviceList.at(currentDeviceIndex);
        firmwareTargetSerial = QSerialPortInfo(firmwareTarget.port).serialNumber().trimmed();
        hasFirmwareTarget = true;
    }
    StLinkConnector::setPreferredPortHint(hasFirmwareTarget ? firmwareTarget.port : QString(),
                                        hasFirmwareTarget ? firmwareTargetSerial : QString());
    device->disconnectDevice();
    pendingFirmwareCloseRequest = communication->close(false);
}

void DeviceMediator::finishFirmwareOperation(bool success, bool flashed)
{
    if (!firmwareOperationPending) return;
    firmwareOperationPending = false;
    pendingFirmwareCloseRequest = 0;
    if (success && !flashed && hasFirmwareTarget) {
        for (int index = deviceList.size() - 1; index >= 0; --index) {
            if (deviceList.at(index).port == firmwareTarget.port && deviceList.at(index).connType == firmwareTarget.connType) {
                deviceList.removeAt(index);
            }
        }
    }
    currentDeviceIndex = -1;
    device->updateGUIDeviceList(deviceList, false);
    autoConnectOnSingleDevice = success && flashed;
    reconnectFirmwareTarget = success && flashed && hasFirmwareTarget;
    communication->close();
}

void DeviceMediator::disableModules()
{
    for (const QSharedPointer<AbstractModule> &mod : modules)
    {
        mod->disableModule();
    }
}

void DeviceMediator::closeModules()
{
    for (const QSharedPointer<AbstractModule> &mod : modules)
    {
        mod->closeModule();
    }
}


quint64 DeviceMediator::shutdownConnection(bool restartScanner)
{
    pendingReconnectRequest = 0;
    ++connectionGeneration;
    waitingForDeviceSpecification = false;
    waitingForAuthRecovery = false;
    deviceSpecificationTimer->stop();
    waitingForTokenAck = false;
    tokenAckTimer->stop();

    disconnect(communication, &Comms::newData, this, &DeviceMediator::parseData);
    disconnect(communication, &Comms::communicationError, this, &DeviceMediator::handleError);
    return disconnectDevice(restartScanner);
}

quint64 DeviceMediator::disconnectDevice(bool restartScanner)
{
    if (isConnected)
    {
        if (authenticator) authenticator->setConnectedDevice(QString(), QString());
        emit saveLayoutUponExit();
        disableModules();
        const quint64 requestId = communication->close(restartScanner);
        isConnected = false;
        return requestId;
    }
    return 0;
}

void DeviceMediator::blockConflictingModulesCallback(QString moduleName, int resources)
{
    // Find the module that is starting
    QSharedPointer<AbstractModule> starter;
    for (const QSharedPointer<AbstractModule> &m : modules) {
        if (m->getModuleName() == moduleName) {
            starter = m;
            break;
        }
    }
    // Prepare aggregate set including the starter's masks
    ResourceSet starterSet = ResourceSet::fromModule(starter, resources);
    for (const QSharedPointer<AbstractModule> &mod : modules) {
        if (mod->getModuleName() == moduleName)
            continue;
        ResourceSet set = ResourceSet::fromModule(mod);
        bool lock = ResourceSet::collide(set, starterSet);
        if (lock)
            mod->setModuleStatus(ModuleStatus::LOCKED);
    }
    resourceManager.reserve(starterSet);
}

void DeviceMediator::releaseConflictingModulesCallback(QString moduleName, int resources)
{
    // release masks of the stopping module
    for (const QSharedPointer<AbstractModule> &m : modules) {
        if (m->getModuleName() == moduleName) {
            ResourceSet set = ResourceSet::fromModule(m, resources);
            resourceManager.release(set);
            break;
        }
    }
    // Check all other modules if they can be unlocked now
    for (const QSharedPointer<AbstractModule> &mod : modules) {
        // Skip the module that is being released
        if (mod->getModuleName() == moduleName)
            continue;
        if (mod->getModuleStatus() == ModuleStatus::LOCKED) {
            ResourceSet reservedNow = resourceManager.reserved();
            ResourceSet modSet = ResourceSet::fromModule(mod, mod->getResources());
            const bool stillConflicts = ResourceSet::collide(modSet, reservedNow);
            if (!stillConflicts) {
                // Transition back to STOP to indicate "unlocked"; previous code used STOP for unlock state
                mod->setModuleStatus(ModuleStatus::STOP);
            }
        }
    }
}

void DeviceMediator::close()
{
    if (!firmwareOperationPending) hasFirmwareTarget = false;
    reconnectFirmwareTarget = false;
    // Suppress auto-connect after manual disconnect so the background scanner
    // doesn't immediately reconnect to the device the user just left.
    autoConnectOnSingleDevice = false;

    shutdownConnection(!firmwareOperationPending);
    ShowDeviceModule();
}

void DeviceMediator::closeApp()
{
    shutdownConnection(true);

}

void DeviceMediator::handleError(QByteArray error)
{
    device->errorHandler(error);
}

void DeviceMediator::parseData(QByteArray data)
{
    bool isDataPassed = false;
    QByteArray dataHeader = data.left(4);
    QByteArray dataToPass = data.right(data.length() - 4);

    for (const QSharedPointer<AbstractModule> &module : modules)
    {
        if (dataHeader == module->getCommandPrefix() && (module->isActive() || dataToPass.left(4) == Commands::CONFIG || dataToPass.left(4) == Commands::ACK || dataToPass.left(4) == Commands::INACTIVE))
        {
            module->parseData(dataToPass);
            isDataPassed = true;
            if(dataToPass.left(4) == Commands::INACTIVE){
                qDebug() << "Module" << module->getModuleName() << "is inactive.";
                module->closeModule();
            }
            if(dataToPass.left(4) == Commands::CONFIG){
                module->setModuleConfigured();
            }
        }
    }
    if(dataHeader == Commands::TOKEN && dataToPass.left(4) == Commands::ACK){
        isDataPassed = true;
        if (!isConnected || (!waitingForTokenAck && !waitingForAuthRecovery)) {
            return;
        }
        if (dataToPass.contains("DEMO")) {
            emit popupMessageRequested("Running in demo mode");
            isDemoMode = true;
            authenticator->setDemoMode(true);
        } else {
            isDemoMode = false;
            authenticator->setDemoMode(false);
        }

        // Deterministic auth: FW has finished all flash operations and verified
        // the token. NOW it's safe to wire modules — they will get correct
        // auth-required responses instead of IACT.
        if (waitingForTokenAck) {
            waitingForTokenAck = false;
            tokenAckTimer->stop();
            qDebug() << "[Auth] FW ACK received, proceeding to load modules (demo=" << isDemoMode << ")";
            finalizeDeviceOpen(pendingDeviceIndex, pendingDevName);
        }
    }
    if(dataHeader == Commands::ERROR){
        qDebug() << "ERROR " << dataToPass.toHex();
        //emit popupMessageRequested("Device Error: " + QString::fromUtf8(dataToPass.toHex()));
        isDataPassed = true;

        if (waitingForTokenAck) {
            waitingForTokenAck = false;
            tokenAckTimer->stop();
            waitingForAuthRecovery = true;
            requestDeviceSpecification();
        } else if (waitingForDeviceSpecification && !waitingForAuthRecovery) {
            failDeviceOpen(QStringLiteral("Device setup failed (firmware error %1). Please reconnect the device.")
                               .arg(QString::fromLatin1(dataToPass.toHex())));
        }
    }
    if(dataHeader == Commands::DEBUG){
        qDebug() << "DEVICE DEBUG " << dataToPass;
        isDataPassed = true;
    }
    if (!isDataPassed)
    {
        if (data.length() < 30)
        {
            qDebug() << "ERROR: this data was not passed to any module" << data;
        }
        else
        {
            qDebug() << "ERROR: this data was not passed to any module" << data.left(15) << " ... " << data.right(10);
        }       
    }
}

void DeviceMediator::finalizeDeviceOpen(int deviceIndex, QString devName)
{
    if (!isConnected || remainingModulesWired) {
        return;
    }
    const quint64 generation = connectionGeneration;
    // Check for session file before setting comms, so layout can be loaded
    // and modules can access JSON data when they show their controls
    if (devName.isEmpty())
    {
        CustomSettings::setNoSessionfound();
    }
    else
    {
        QString sessionFile = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/sessions/" + devName + ".json";
        QFile file(sessionFile);
        if (file.exists())
        {
            CustomSettings::askForSessionRestore(devName, qobject_cast<QWidget*>(this->parent()));
        }
        else
        {
            CustomSettings::setNoSessionfound();
        }
    }

    if (!isConnected || connectionGeneration != generation) {
        return;
    }

    // Emit loadLayoutUponOpen BEFORE setComms so MainWindow can load the session
    // data (pendingSessionData) before modules start showing their controls
    if (CustomSettings::isSessionRestoreRequest() && deviceIndex >= 0 && deviceIndex < deviceList.size()) {
        emit loadLayoutUponOpen(deviceList.at(deviceIndex).deviceName);
    } else {
        // No session to restore, load default layout instead
        emit loadLayoutUponOpen("layoutOnly");
    }

    if (!isConnected || connectionGeneration != generation) {
        return;
    }
    wireRemainingModules();
    refreshDeviceToken();
}

void DeviceMediator::wireRemainingModules()
{
    if (remainingModulesWired) {
        return;
    }

    remainingModulesWired = true;
    for (int index = 1; index < modules.size(); ++index)
    {
        modules.at(index)->setComms(communication);
    }
}

void DeviceMediator::ShowDeviceModule()
{
    device->showModuleWindow();
    device->showModuleControl();
    device->hideModuleStatus();
}

bool DeviceMediator::getIsConnected() const
{
    return isConnected;
}

bool DeviceMediator::getIsDemoMode() const
{
    return isDemoMode;
}

QString DeviceMediator::getDeviceName()
{
    return device->getName();
}

QString DeviceMediator::getMcuId()
{
    return device->getMcuId();
}

ResourceSet DeviceMediator::getResourcesInUse() const
{
    return resourceManager.reserved();
}

void DeviceMediator::setResourcesInUse(ResourceSet resources)
{
    resourceManager.reserve(resources);
}

void DeviceMediator::onTokenAckTimeout()
{
    if (!waitingForTokenAck) return; // already handled
    failDeviceOpen(QStringLiteral("Device authentication timed out. Please reconnect the device."));
}

void DeviceMediator::onDeviceSpecificationTimeout()
{
    if (!waitingForDeviceSpecification) return;
    failDeviceOpen(QStringLiteral("Device did not provide its specification. Please reconnect the device."));
}

void DeviceMediator::failDeviceOpen(const QString &message)
{
    device->disconnectDevice();
    emit popupMessageRequested(message);
}

void DeviceMediator::onDeviceSpecificationReady()
{
    if (!isConnected || !waitingForDeviceSpecification) {
        return;
    }
    waitingForDeviceSpecification = false;
    const bool completingAuthRecovery = waitingForAuthRecovery;
    waitingForAuthRecovery = false;
    deviceSpecificationTimer->stop();
    DeviceSpec *spec = device->getDeviceSpec();
    if (spec != nullptr && spec->HasCompatibilityMetadata) {
        spec->IsProtocolCompatible = FirmwareCompatibility::isSupportedProtocolVersion(spec->Protocol_Version);
        if (!spec->IsProtocolCompatible) {
            spec->CompatibilityMessage = QStringLiteral(
                                            "Firmware protocol version %1 is not supported by desktop version %2.")
                                            .arg(spec->Protocol_Version_Text,
                                                 FirmwareCompatibility::applicationVersionText());
            emit popupMessageRequested(spec->CompatibilityMessage);
        } else {
            spec->CompatibilityMessage.clear();
        }
    } else if (spec != nullptr) {
        spec->IsProtocolCompatible = true;
        spec->CompatibilityMessage.clear();
    }

    if (spec == nullptr || spec->IsProtocolCompatible) {
        if (completingAuthRecovery) {
            finalizeDeviceOpen(pendingDeviceIndex, pendingDevName);
        } else {
            beginTokenAuthentication();
        }
    }
}

void DeviceMediator::refreshDeviceToken()
{
    if (authenticator) {
        authenticator->setConnectedDevice(device->getName(), device->getMcuId());
        
        if (lastTokenRefresh.isValid() && lastTokenRefresh.secsTo(QDateTime::currentDateTime()) < 30 && !isDemoMode) {
            qDebug() << "Skipping automatic token refresh (last refresh < 30s ago)";
        } else {
            lastTokenRefresh = QDateTime::currentDateTime();
            authenticator->tokenRefresh(device->getName(), device->getMcuId(), isDemoMode);
        }
    }

}
