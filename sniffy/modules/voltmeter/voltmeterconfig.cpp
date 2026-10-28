#include "voltmeterconfig.h"
#include <QDataStream>
#include <QIODevice>

VoltmeterConfig::VoltmeterConfig(QObject *parent)
{
    Q_UNUSED(parent);
}

void VoltmeterConfig::parse(QByteArray config)
{
    hasSavedSettings = false;
    hasSavedInputSelections = false;
    for (QStringList &names : selectedInputNames)
        names.clear();

    if (config.isEmpty()) {
        hasSavedInputSelections = false;
        isConfigurationLoaded = true;
        return;
    }

    QDataStream stream(&config, QIODevice::ReadOnly);
    quint32 magic = 0;
    quint16 version = 0;
    stream >> magic >> version;
    if (magic == 0x564D4346 && (version == 1 || version == 2)) {
        for (int slot = 0; slot < MAX_VOLTMETER_CHANNELS; ++slot) {
            quint8 count = 0;
            stream >> count;
            if (stream.status() != QDataStream::Ok || count > MAX_VOLTMETER_INPUTS_PER_SLOT) {
                qWarning() << "Invalid voltmeter input selection count";
                for (QStringList &names : selectedInputNames)
                    names.clear();
                isConfigurationLoaded = true;
                return;
            }
            for (quint8 i = 0; i < count; ++i) {
                QString name;
                stream >> name;
                if (stream.status() != QDataStream::Ok) {
                    qWarning() << "Truncated voltmeter input selections";
                    for (QStringList &names : selectedInputNames)
                        names.clear();
                    isConfigurationLoaded = true;
                    return;
                }
                selectedInputNames[slot].append(name);
            }
        }
        if (version == 2) {
            bool savedMultimode;
            qint32 mask, average, frequency, periods, rate, calibrationMode;
            stream >> savedMultimode >> mask >> average >> frequency >> periods >> rate >> calibrationMode;
            hasSavedSettings = stream.status() == QDataStream::Ok && stream.atEnd() &&
                               mask >= 0 && mask < (1 << MAX_VOLTMETER_CHANNELS) &&
                               average >= 1 && average <= 64 &&
                               (frequency == 50 || frequency == 60) &&
                               (periods == 1 || periods == 2 || periods == 4 || periods == 8) &&
                               (rate == 5000 || rate == 50000) &&
                               (calibrationMode == 0 || calibrationMode == 1);
            if (hasSavedSettings) {
                multimode = savedMultimode;
                channelMask = mask;
                averaging = average;
                mainsFrequency = frequency;
                mainsPeriods = periods;
                samplingRate = rate;
                mode = static_cast<VoltmeterMode>(calibrationMode);
            }
        }
        hasSavedInputSelections = stream.status() == QDataStream::Ok && stream.atEnd() &&
                                  (version == 1 || hasSavedSettings);
        if (!hasSavedInputSelections) {
            qWarning() << "Invalid voltmeter configuration";
            for (QStringList &names : selectedInputNames)
                names.clear();
        }
    } else {
        qWarning() << "Unsupported voltmeter configuration";
        hasSavedInputSelections = false;
    }

    isConfigurationLoaded = true;

}

QByteArray VoltmeterConfig::serialize()
{
    QByteArray data; // stack allocated
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream << quint32(0x564D4346) << quint16(2);
    for (const QStringList &names : selectedInputNames) {
        const quint8 count = static_cast<quint8>(qMin(names.size(), MAX_VOLTMETER_INPUTS_PER_SLOT));
        stream << count;
        for (int i = 0; i < count; ++i)
            stream << names.at(i);
    }
    stream << multimode << qint32(channelMask) << qint32(averaging)
           << qint32(mainsFrequency) << qint32(mainsPeriods) << qint32(samplingRate)
           << qint32(mode);
    hasSavedSettings = true;

    return data;
}

int VoltmeterConfig::captureSampleCount() const
{
    return sampleCountForPeriods(mainsPeriods);
}

int VoltmeterConfig::sampleCountForPeriods(int periods) const
{
    return qRound(double(samplingRate) * periods / mainsFrequency);
}
