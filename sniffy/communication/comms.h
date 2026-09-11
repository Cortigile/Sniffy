#ifndef COMMS_H
#define COMMS_H

#include <QObject>
#include <QMutex>
#include "connectiontype.h"
#include "serialLine.h"
#include "devicescanner.h"
#include <memory>



class Comms : public QObject
{
    Q_OBJECT

//#define DEBUG_COMMS

public:
    explicit Comms(QObject *parent = nullptr);
    ~Comms();
    quint64 close(bool restartScanner = true);
    void open(DeviceDescriptor device);
    bool getIsOpen() const;
    void scanForDevices();

    void write(QByteArray module, QByteArray feature, QByteArray param);
    void write(QByteArray module, QByteArray feature, int param);
    void write(QByteArray module, QByteArray command);
    void write(QByteArray data);

signals:
    void dataWrite(const QByteArray &data);
    void openLine(DeviceDescriptor desc);
    void closeLine(quint64 requestId);
    void newData(QByteArray message);
    void devicesScaned(QList<DeviceDescriptor> deviceList);
    void communicationError(QByteArray);
    void connectionOpened(bool success);
    void connectionClosed(quint64 requestId);

private slots:
    void parseMessage(QByteArray message);
    void errorReceived(QByteArray error);
    void devicesScanned(QList<DeviceDescriptor> deviceList);

private:
    void finishCloseIfReady(quint64 requestId);
    std::unique_ptr<SerialLine> serial; // owned resource
    QThread *serialThread;              // Qt parented (this)
    DeviceScanner devScanner;           // QThread subclass member
    quint64 connectionRequest = 0;
    bool restartScannerOnClose = false;
    bool serialActivityEnabled = false;
    quint64 serialClosedRequest = 0;
    quint64 scannerPausedRequest = 0;
};

#endif // COMMS_H
