#include "stlinkwriter.h"
#include <QFile>
#include <QFileInfo>
#include <QThread>

#include "stlink.h"
extern "C"
{
#include "common_flash.h"
#include "read_write.h"
#include "stm32_flash.h"
}

namespace
{
/**
 * Program the C5 BOOT_SEL option byte so the boot source comes from the
 * BOOT0 option byte (0 => main flash) instead of the BOOT0 pin, which the
 * ST-Link leaves high after flashing and would send the core to BootROM.
 * Idempotent. The core must be halted; the caller resets the target
 * afterwards. Returns 0 on success.
 */
int32_t ensureC5BootFromMainFlash(stlink_t *stlink)
{
    const uint32_t bootSelBit = 1u << STM32_FLASH_C5_OPTSR_BOOT_SEL;

    uint32_t cur = 0;
    if (stlink_read_debug32(stlink, STM32_FLASH_C5_OPTSR_PRG, &cur) != 0)
        return -1;
    if (cur & bootSelBit)
        return 0; // already configured

    const auto wr = [&](uint32_t addr, uint32_t val)
    {
        return stlink_write_debug32(stlink, addr, val) == 0;
    };

    // Unlock, program, start option byte programming.
    if (!wr(STM32_FLASH_C5_OPTKEYR, STM32_FLASH_C5_OPTKEY1) ||
        !wr(STM32_FLASH_C5_OPTKEYR, STM32_FLASH_C5_OPTKEY2) ||
        !wr(STM32_FLASH_C5_OPTSR_PRG, cur | bootSelBit) ||
        !wr(STM32_FLASH_C5_OPTCR, 1u << STM32_FLASH_C5_OPTCR_OPTSTRT))
        return -1;

    // Wait for OPT_BUSY to clear (option byte programming takes a few ms).
    for (int i = 0; i < 100; ++i)
    {
        uint32_t sr = 0;
        if (stlink_read_debug32(stlink, STM32_FLASH_C5_OPTSR_CUR, &sr) == 0 &&
            !(sr & (1u << STM32_FLASH_C5_OPTSR_OPT_BUSY)))
            break;
        QThread::msleep(1);
    }

    // Verify the change took effect.
    return (stlink_read_debug32(stlink, STM32_FLASH_C5_OPTSR_PRG, &cur) == 0 &&
            (cur & bootSelBit)) ? 0 : -1;
}
} // namespace

StLinkWriter::StLinkWriter(QObject *parent) : QObject(parent)
{
}

void StLinkWriter::flash(stlink_t* stlink, const QString &filePath)
{
    if (!stlink)
    {
        emit operationFinished(false, "Device not connected");
        return;
    }

    emit operationStarted("Flashing");
    emit logMessage("Flashing file: " + QFileInfo(filePath).fileName());

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
    {
        emit operationFinished(false, "Could not open firmware file");
        return;
    }

    QByteArray data = file.readAll();
    file.close();

    if (data.isEmpty())
    {
        emit operationFinished(false, "Firmware file is empty");
        return;
    }

    // Ensure we are in the right mode
    stlink_reset(stlink, RESET_HARD);
    stlink_force_debug(stlink);
    stlink_status(stlink);

    // C5: program the boot option while the core is reliably halted.
    // Option bytes live in a separate memory region, so this does not
    // affect the flash that is programmed below. The boot source is
    // latched at the final reset.
    if (stlink->flash_type == STM32_FLASH_TYPE_C5)
    {
        emit logMessage("Configuring C5 boot option (BOOT_SEL=1)...");
        if (ensureC5BootFromMainFlash(stlink) != 0)
        {
            emit operationFinished(false, "The C5 boot option could not be configured.");
            return;
        }
        emit logMessage("C5 boot option configured.");
    }

    // Unlock flash if necessary
    unlock_flash_if(stlink);

    stm32_addr_t flash_base = stlink->flash_base;
    uint32_t size = data.size();

    emit logMessage("Erasing flash...");
    emit progressChanged(0, 100);

    // Use page erase instead of mass erase to avoid timeouts and provide progress
    uint32_t page_size = stlink->flash_pgsz;
    if (page_size == 0)
        page_size = 2048; // Safety fallback

    uint32_t addr = flash_base;
    uint32_t end_addr = flash_base + size;

    int total_pages = (size + page_size - 1) / page_size;
    int erased_pages = 0;

    while (addr < end_addr)
    {
        int res = stlink_erase_flash_page(stlink, addr);
        if (res != 0)
        {
            emit operationFinished(false, QString("Failed to erase page at 0x%1").arg(addr, 0, 16));
            return;
        }
        addr += page_size;
        erased_pages++;

        // Progress 0-50%
        int percent = (int)((double)erased_pages / total_pages * 50.0);
        emit progressChanged(percent, 100);

        // Keep GUI responsive
        QThread::msleep(10);
    }

    emit logMessage("Flash erased. Writing firmware...");
    emit progressChanged(50, 100);

    // Ensure flash is unlocked before writing
    unlock_flash_if(stlink);

    emit progressChanged(75, 100);

    // Let libstlink handle the MCU-specific programming granularity internally.
    // Repeated short stlink_write_flash() calls can fail on some F4 targets
    // after the first successful block.
    QByteArray writeBuffer = data;
    const int res = stlink_write_flash(stlink,
                                       flash_base,
                                       reinterpret_cast<uint8_t *>(writeBuffer.data()),
                                       size,
                                       0,
                                       NO_ERASE);

    if (res != 0)
    {
        emit operationFinished(false, QString("Failed to write firmware. Error: %1").arg(res));
        return;
    }

    emit progressChanged(100, 100);
    emit logMessage("Flashing complete. Resetting device...");

    if (stlink_reset(stlink, RESET_HARD) != 0)
    {
        emit operationFinished(false, "Firmware was written, but the target reset failed.");
        return;
    }

    if (stlink->flash_type != STM32_FLASH_TYPE_C5 && stlink_run(stlink, RUN_NORMAL) != 0)
    {
        emit operationFinished(false, "Firmware was written, but the target could not resume.");
        return;
    }

    emit operationFinished(true, "Firmware flashed successfully");
}
