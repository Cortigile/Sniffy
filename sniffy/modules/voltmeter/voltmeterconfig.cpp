#include "voltmeterconfig.h"
#include <QDataStream>
#include <QIODevice>

VoltmeterConfig::VoltmeterConfig(QObject *parent)
{
    Q_UNUSED(parent);
}

void VoltmeterConfig::parse(QByteArray config)
{
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
    if (magic == 0x564D4346 && version == 1) {
        for (int slot = 0; slot < MAX_VOLTMETER_CHANNELS; ++slot) {
            quint8 count = 0;
            stream >> count;
            if (stream.status() != QDataStream::Ok || count > MAX_VOLTMETER_INPUTS_PER_SLOT) {
                for (QStringList &names : selectedInputNames)
                    names.clear();
                isConfigurationLoaded = true;
                return;
            }
            for (quint8 i = 0; i < count; ++i) {
                QString name;
                stream >> name;
                if (stream.status() != QDataStream::Ok) {
                    for (QStringList &names : selectedInputNames)
                        names.clear();
                    isConfigurationLoaded = true;
                    return;
                }
                selectedInputNames[slot].append(name);
            }
        }
        hasSavedInputSelections = stream.status() == QDataStream::Ok && stream.atEnd();
        if (!hasSavedInputSelections) {
            for (QStringList &names : selectedInputNames)
                names.clear();
        }
    } else {
        hasSavedInputSelections = false;
    }

    isConfigurationLoaded = true;

}

QByteArray VoltmeterConfig::serialize()
{
    QByteArray data; // stack allocated
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream << quint32(0x564D4346) << quint16(1);
    for (const QStringList &names : selectedInputNames) {
        const quint8 count = static_cast<quint8>(qMin(names.size(), MAX_VOLTMETER_INPUTS_PER_SLOT));
        stream << count;
        for (int i = 0; i < count; ++i)
            stream << names.at(i);
    }
    hasSavedInputSelections = true;

    return data;
}
