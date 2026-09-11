#include "stlinkconnector.h"
#include "communication/serialLine.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QtSerialPort/QSerialPortInfo>
#include <QTextStream>
#include <algorithm>

#include "stlink.h"
extern "C"
{
#include "chipid.h"
#include "common_flash.h"
#include "read_write.h"
#include "option_bytes.h"
}

namespace {

struct PreferredDevice
{
    QString port;
    QString serial;
};

QMutex &preferredPortHintMutex()
{
    static QMutex mutex;
    return mutex;
}

PreferredDevice &preferredPortHintStorage()
{
    static PreferredDevice device;
    return device;
}

QString resolveDebuggerSerialForPort(const QString &portName)
{
    if (portName.isEmpty())
    {
        return QString();
    }

    const QList<QSerialPortInfo> ports = QSerialPortInfo::availablePorts();
    for (const QSerialPortInfo &portInfo : ports)
    {
        if (portInfo.portName().compare(portName, Qt::CaseInsensitive) == 0)
        {
            return portInfo.serialNumber().trimmed();
        }
    }

    return QString();
}

PreferredDevice resolvePreferredDevice()
{
    QMutexLocker locker(&preferredPortHintMutex());
    const PreferredDevice hintedDevice = preferredPortHintStorage();
    preferredPortHintStorage() = {};
    locker.unlock();
    if (!hintedDevice.port.isEmpty()) return hintedDevice;
    const QString port = SerialLine::currentOpenPort();
    return {port, resolveDebuggerSerialForPort(port)};
}

int connectedDebuggerCount()
{
    libusb_context *context = nullptr;
    if (libusb_init(&context) != 0) return -1;
    libusb_device **devices = nullptr;
    const ssize_t count = libusb_get_device_list(context, &devices);
    int debuggerCount = count < 0 ? -1 : 0;
    for (ssize_t index = 0; index < count; ++index) {
        libusb_device_descriptor descriptor = {};
        if (libusb_get_device_descriptor(devices[index], &descriptor) != 0) {
            debuggerCount = -1;
            break;
        }
        if (descriptor.idVendor == STLINK_USB_VID_ST && STLINK_SUPPORTED_USB_PID(descriptor.idProduct)) {
            ++debuggerCount;
        }
    }
    if (devices) libusb_free_device_list(devices, 1);
    libusb_exit(context);
    return debuggerCount;
}

QString resolveChipsDir()
{
    const QString envDir = qgetenv("STLINK_CHIPS_DIR");
    if (!envDir.isEmpty() && QDir(envDir).exists())
    {
        return envDir;
    }

    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QDir(appDir).filePath("config/chips"),
        QDir(appDir).filePath("../share/sniffy/chips"),
        QStringLiteral("/usr/share/sniffy/chips"),
        QStringLiteral("/usr/local/share/sniffy/chips"),
        QStringLiteral("/usr/share/stlink/chips")
    };

    for (const QString &candidate : candidates)
    {
        if (QDir(candidate).exists())
        {
            return candidate;
        }
    }

    return QString();
}

} // namespace

StLinkConnector::StLinkConnector(QObject *parent)
    : QObject(parent), m_stlink(nullptr)
{
}

StLinkConnector::~StLinkConnector()
{
    cleanup();
}

stlink_t* StLinkConnector::handle() const
{
    return m_stlink;
}

bool StLinkConnector::isConnected() const
{
    return m_stlink != nullptr;
}

void StLinkConnector::setPreferredPortHint(const QString &portName)
{
    setPreferredPortHint(portName, resolveDebuggerSerialForPort(portName));
}

void StLinkConnector::setPreferredPortHint(const QString &portName, const QString &serialNumber)
{
    QMutexLocker locker(&preferredPortHintMutex());
    preferredPortHintStorage() = {portName, serialNumber.trimmed()};
}

bool StLinkConnector::init()
{
    // Set STLINK_CHIPS_DIR to help libstlink find chip definitions
    QString chipsDir = resolveChipsDir();

    if (!chipsDir.isEmpty())
    {
        // Force forward slashes for libstlink compatibility.
        chipsDir = chipsDir.replace("\\", "/");
        qputenv("STLINK_CHIPS_DIR", chipsDir.toLocal8Bit());
    }
    else
    {
        emit logMessage("Warning: Chips config dir not found. Tried app-relative and system locations.");
    }

    const PreferredDevice preferredDevice = resolvePreferredDevice();
    const QString preferredPort = preferredDevice.port;
    const QString preferredSerial = preferredDevice.serial;

    if (!preferredPort.isEmpty())
    {
        const QByteArray serial = preferredSerial.toLatin1();
        if (serial.isEmpty() || serial.size() >= STLINK_SERIAL_BUFFER_SIZE)
        {
            emit logMessage(QString("Failed to resolve ST-Link serial for active device port %1.").arg(preferredPort));
            return false;
        }
        char serialFilter[STLINK_SERIAL_BUFFER_SIZE] = {};
        std::copy(serial.cbegin(), serial.cend(), serialFilter);
        emit logMessage(QString("Opening ST-Link for %1 (serial %2)...").arg(preferredPort, preferredSerial));
        m_stlink = stlink_open_usb(UWARN, CONNECT_NORMAL, serialFilter, 0);
    }
    else
    {
        const int connectedCount = connectedDebuggerCount();
        if (connectedCount != 1) {
            emit logMessage(connectedCount < 0 ? "Failed to enumerate USB devices."
                            : connectedCount == 0 ? "No ST-Link devices found."
                            : "Multiple ST-Link devices found. Connect to the target board in the app first, or leave only the target connected.");
            return false;
        }
        stlink_t **stdevs = nullptr;
        const size_t count = stlink_probe_usb(&stdevs, CONNECT_NORMAL, 0);
        if (count != 1)
        {
            emit logMessage(count == 0 ? "No accessible ST-Link devices found."
                                       : "Multiple ST-Link devices found. Connect to the target board in the app first.");
            for (size_t index = 0; index < count; ++index)
            {
                stlink_close(stdevs[index]);
            }
            free(stdevs);
            return false;
        }
        m_stlink = stdevs[0];
        free(stdevs);
    }

    if (!m_stlink)
    {
        emit logMessage(QString("Failed to open selected ST-Link (%1, serial %2). Check the board connection and other debugger tools.")
                    .arg(preferredPort, preferredSerial));
        return false;
    }

    // Enter SWD mode
    if (stlink_enter_swd_mode(m_stlink))
    {
        emit logMessage("Failed to enter SWD mode.");
        cleanup();
        return false;
    }

    // Force debug
    if (stlink_force_debug(m_stlink))
    {
        emit logMessage("Failed to force debug mode.");
        cleanup();
        return false;
    }

    // Try to read core ID first to verify connection
    stlink_core_id(m_stlink);
    // Confirm an MCU is connected
    if (m_stlink->core_id == 0 || m_stlink->core_id == 0xFFFFFFFF)
    {
        emit logMessage("No MCU detected on target.");
        cleanup();
        return false;
    }
    else
    {
        emit logMessage(QString("MCU detected. Core ID: 0x%1").arg(m_stlink->core_id, 0, 16));
    }

    // Load device params (flash size, page size, etc.)
    // This relies on STLINK_CHIPS_DIR being set correctly to find chip definitions.
    if (stlink_load_device_params(m_stlink))
    {
        emit logMessage(QString("Failed to load device parameters automatically. Chip ID: 0x%1").arg(m_stlink->chip_id, 0, 16));

        if (!loadDeviceParamsFallback())
        {
            cleanup();
            return false;
        }
    }

    emit logMessage(QString("Device params loaded: Flash: %1KB, Page: %2B, SRAM: %3KB")
                        .arg(m_stlink->flash_size / 1024)
                        .arg(m_stlink->flash_pgsz)
                        .arg(m_stlink->sram_size / 1024));

    // Check status
    stlink_status(m_stlink);

    return true;
}

void StLinkConnector::cleanup()
{
    if (m_stlink)
    {
        stlink_exit_debug_mode(m_stlink);
        stlink_close(m_stlink);
        m_stlink = nullptr;
    }
}

bool StLinkConnector::loadDeviceParamsFallback()
{
    emit logMessage("Attempting manual parameter fallback...");

    QString chipsDir = qgetenv("STLINK_CHIPS_DIR");
    if (chipsDir.isEmpty())
    {
        chipsDir = resolveChipsDir();
    }

    if (chipsDir.isEmpty())
    {
        emit logMessage("No chips directory available for fallback parser.");
        return false;
    }

    QDir dir(chipsDir);
    QStringList filters;
    filters << "*.chip";
    dir.setNameFilters(filters);

    QFileInfoList list = dir.entryInfoList();
    for (const QFileInfo &fileInfo : list)
    {
        QFile file(fileInfo.absoluteFilePath());
        if (file.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            QTextStream in(&file);
            uint32_t fileChipId = 0;

            // Simple parser variables
            uint32_t p_flash_size_reg = 0;
            uint32_t p_flash_pagesize = 0;
            uint32_t p_sram_size = 0;
            uint32_t p_bootrom_base = 0;
            uint32_t p_bootrom_size = 0;
            uint32_t p_option_base = 0;
            uint32_t p_option_size = 0;
            uint32_t p_flags = 0;
            decltype(m_stlink->flash_type) p_flash_type = STM32_FLASH_TYPE_UNKNOWN;

            while (!in.atEnd())
            {
                QString line = in.readLine().trimmed();
                if (line.startsWith("#") || line.isEmpty())
                    continue;

                // Remove comments
                int commentIdx = line.indexOf("//");
                if (commentIdx != -1)
                    line = line.left(commentIdx).trimmed();

                QStringList parts = line.split(QRegularExpression("\\s+"));
                if (parts.size() < 2)
                    continue;

                QString key = parts[0];
                QString value = parts[1];

                bool ok;
                if (key == "chip_id")
                {
                    fileChipId = value.toUInt(&ok, 0); // Auto-detect base (hex 0x...)
                }
                else if (key == "flash_size_reg")
                {
                    p_flash_size_reg = value.toUInt(&ok, 0);
                }
                else if (key == "flash_pagesize")
                {
                    p_flash_pagesize = value.toUInt(&ok, 0);
                }
                else if (key == "sram_size")
                {
                    p_sram_size = value.toUInt(&ok, 0);
                }
                else if (key == "bootrom_base")
                {
                    p_bootrom_base = value.toUInt(&ok, 0);
                }
                else if (key == "bootrom_size")
                {
                    p_bootrom_size = value.toUInt(&ok, 0);
                }
                else if (key == "option_base")
                {
                    p_option_base = value.toUInt(&ok, 0);
                }
                else if (key == "option_size")
                {
                    p_option_size = value.toUInt(&ok, 0);
                }
                else if (key == "flash_type")
                {
                    if (value == "F0_F1_F3")
                        p_flash_type = STM32_FLASH_TYPE_F0_F1_F3;
                    else if (value == "F1_XL")
                        p_flash_type = STM32_FLASH_TYPE_F1_XL;
                    else if (value == "F2_F4")
                        p_flash_type = STM32_FLASH_TYPE_F2_F4;
                    else if (value == "F7")
                        p_flash_type = STM32_FLASH_TYPE_F7;
                    else if (value == "G0")
                        p_flash_type = STM32_FLASH_TYPE_G0;
                    else if (value == "G4")
                        p_flash_type = STM32_FLASH_TYPE_G4;
                    else if (value == "H7")
                        p_flash_type = STM32_FLASH_TYPE_H7;
                    else if (value == "L0_L1")
                        p_flash_type = STM32_FLASH_TYPE_L0_L1;
                    else if (value == "L4")
                        p_flash_type = STM32_FLASH_TYPE_L4;
                    else if (value == "L5_U5")
                        p_flash_type = STM32_FLASH_TYPE_L5_U5;
                    else if (value == "L5_U5_H5")
                        p_flash_type = STM32_FLASH_TYPE_L5_U5;
                    else if (value == "WB_WL")
                        p_flash_type = STM32_FLASH_TYPE_WB_WL;
                    else if (value == "WB0")
                        p_flash_type = STM32_FLASH_TYPE_WB0;
                    else if (value == "H5")
                        p_flash_type = STM32_FLASH_TYPE_H5;
                    else if (value == "C0")
                        p_flash_type = STM32_FLASH_TYPE_C0;
                    else if (value == "C5")
                        p_flash_type = STM32_FLASH_TYPE_C5;
                }
            }

            if (fileChipId == m_stlink->chip_id)
            {
                emit logMessage("Found matching config file: " + fileInfo.fileName());

                m_stlink->flash_pgsz = p_flash_pagesize;
                m_stlink->sram_size = p_sram_size;
                m_stlink->sys_base = p_bootrom_base;
                m_stlink->sys_size = p_bootrom_size;
                m_stlink->option_base = p_option_base;
                m_stlink->option_size = p_option_size;
                m_stlink->flash_type = p_flash_type;
                m_stlink->chip_flags = p_flags;

                uint32_t flash_size_kb = 0;
                if (p_flash_size_reg != 0)
                {
                    if (!stlink_read_mem32(m_stlink, p_flash_size_reg, 4))
                    {
                        const uint32_t val = *(uint32_t *)m_stlink->q_buf;
                        flash_size_kb = val & 0xFFFF;
                    }
                }

                if (flash_size_kb > 0)
                {
                    m_stlink->flash_size = flash_size_kb * 1024;
                }
                else
                {
                    emit logMessage(QString("Failed to read flash size from chip definition register 0x%1.").arg(p_flash_size_reg, 0, 16));
                    return false;
                }

                m_stlink->flash_base = 0x08000000; // Standard base
                m_stlink->sram_base = 0x20000000;  // Standard base

                return true;
            }
        }
    }

    emit logMessage("No matching config file found for this chip.");
    return false;
}
