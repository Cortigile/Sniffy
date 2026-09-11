#ifndef DEVICESCANNER_H
#define DEVICESCANNER_H

#include <QObject>
#include <QThread>
#include <QDateTime>
#include <QMutex>
#include <QWaitCondition>

#include "devicedescriptor.h"
#include "serialLine.h"

class DeviceScanner : public QThread
{
    Q_OBJECT
public:
    explicit DeviceScanner(QObject *parent = nullptr);
    ~DeviceScanner();
    void searchForDevices(bool isSearchEnaled);

signals:
    void newDevicesScanned(QList<DeviceDescriptor> deviceList);


private:
    void run() override;
    bool deviceListsEqual(QList<DeviceDescriptor> &listA, QList<DeviceDescriptor> &listB);
    QMutex searchMutex;
    QWaitCondition searchChanged;
    bool isSearchEnaled = false;
    bool isRunning = true;
    bool shouldClearList = false;
    quint64 searchGeneration = 0;
    QList<DeviceDescriptor> currentDeviceList;
    SerialLine serLine;

public slots:
    void quit();
};

#endif // DEVICESCANNER_H
