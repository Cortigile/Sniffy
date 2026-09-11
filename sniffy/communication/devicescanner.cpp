#include "devicescanner.h"

DeviceScanner::DeviceScanner(QObject *parent) : QThread(parent)
{
}

DeviceScanner::~DeviceScanner()
{
    quit();
    wait();
}

void DeviceScanner::searchForDevices(bool isSearchEnaled)
{
    QMutexLocker locker(&searchMutex);
    if (isSearchEnaled)
    {
        shouldClearList = true;
    }
    this->isSearchEnaled = isSearchEnaled;
    ++searchGeneration;
    searchChanged.wakeAll();
}

void DeviceScanner::run()
{
    QMutexLocker locker(&searchMutex);
    QList<DeviceDescriptor> tempDeviceList;
    while (isRunning) {
        while (isRunning && !isSearchEnaled) {
            searchChanged.wait(&searchMutex);
        }
        if (!isRunning) {
            break;
        }
        if (shouldClearList) {
            currentDeviceList.clear();
            shouldClearList = false;
        }
        const quint64 generation = searchGeneration;
        tempDeviceList.clear();
        locker.unlock();
        SerialLine::getAvailableDevices(&tempDeviceList, 0);
        locker.relock();
        if (!isRunning || !isSearchEnaled || generation != searchGeneration) {
            continue;
        }
        if (!deviceListsEqual(tempDeviceList, currentDeviceList)) {
            currentDeviceList = tempDeviceList;
            isSearchEnaled = false;
            locker.unlock();
            emit newDevicesScanned(tempDeviceList);
            locker.relock();
        }
        if (isRunning && isSearchEnaled && generation == searchGeneration) {
            searchChanged.wait(&searchMutex, 500);
        }
    }
}

bool DeviceScanner::deviceListsEqual(QList<DeviceDescriptor> &listA, QList<DeviceDescriptor> &listB)
{
    bool equal = false;
    if(listB.length() == listA.length()){
        equal = true;
        for(int i = 0;i<listA.length();i++){
            if(listA.at(i).deviceName != listB.at(i).deviceName || listA.at(i).port != listB.at(i).port){
                equal = false;
                break;
            }
        }
    }
    return equal;
}

void DeviceScanner::quit()
{
    QMutexLocker locker(&searchMutex);
    isRunning = false;
    searchChanged.wakeAll();
}


