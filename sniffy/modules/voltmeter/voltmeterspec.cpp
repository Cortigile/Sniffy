#include "voltmeterspec.h"
#include "modules/specutils.h"

VoltmeterSpec::VoltmeterSpec(QObject *parent)
{
    Q_UNUSED(parent);
}

void VoltmeterSpec::parseSpecification(QByteArray spec)
{
    QDataStream stream(spec);

    int tmpVref;
    int tmpVrefInt;

    stream >> resources >> maxSamplingRate12B >> maxSamplingRate8BInterleaved ;
    stream >> memorySize>> maxADCChannels >> tmpVref >> tmpVrefInt;

    Vref = tmpVref;
    VrefCalibration = tmpVrefInt;

    SpecParsing::readPins4(stream, maxADCChannels, [&](int i, const QString &pin){ channelPins[i] = pin; });
    // New: read 4x GPIO masks (A,B,C,D)
    quint32 gpioA = 0, gpioB = 0, gpioC = 0, gpioD = 0;
    stream >> gpioA >> gpioB >> gpioC >> gpioD;
    setGpioMasks(gpioA, gpioB, gpioC, gpioD);
}

bool VoltmeterSpec::parseAvailableInputs(QByteArray response)
{
    static const QByteArray header("INP_");
    if (response.startsWith(header))
        response.remove(0, header.size());
    if (response.endsWith(';'))
        response.chop(1);

    int numSlots = 0;
    while (numSlots < MAX_VOLTMETER_CHANNELS && numSlots < response.size() &&
           response.at(numSlots) != '/') {
        ++numSlots;
    }
    if (numSlots == 0 || numSlots >= response.size() || response.at(numSlots) != '/')
        return false;

    int selectors[MAX_VOLTMETER_CHANNELS] = {};
    QStringList inputs[MAX_VOLTMETER_CHANNELS];
    for (int slot = 0; slot < numSlots; ++slot)
        selectors[slot] = static_cast<quint8>(response.at(slot));

    int position = numSlots;
    for (int slot = 0; slot < numSlots; ++slot) {
        if (position >= response.size() || response.at(position) != '/')
            return false;
        const int listEnd = response.indexOf('/', position + 1);
        if (listEnd < 0)
            return false;

        const QByteArray listBytes = response.mid(position + 1, listEnd - position - 1);
        const QList<QByteArray> names = listBytes.split(':');
        if (names.isEmpty() || names.size() > 256 ||
            (names.size() == 1 && names.first().isEmpty()) ||
            selectors[slot] >= names.size())
            return false;

        for (const QByteArray &name : names) {
            if (name.isEmpty())
                return false;
            inputs[slot].append(QString::fromLatin1(name));
        }
        position = listEnd;
    }

    if (position != response.size() - 1 || response.at(position) != '/')
        return false;

    for (int slot = 0; slot < MAX_VOLTMETER_CHANNELS; ++slot) {
        availableInputs[slot] = inputs[slot];
        currentInputSelectors[slot] = selectors[slot];
    }
    availableInputSlots = numSlots;
    inputCapabilitiesAvailable = true;
    return true;
}
