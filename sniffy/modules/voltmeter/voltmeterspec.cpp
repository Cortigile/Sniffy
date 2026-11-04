#include "voltmeterspec.h"
#include "modules/specutils.h"
#include "modules/adcinputcatalog.h"

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
    AdcInputCatalog catalog;
    QString error;
    if (!catalog.parse(response, error))
        return false;
    for (int slot = 0; slot < MAX_VOLTMETER_CHANNELS; ++slot) {
        availableInputs[slot] = slot < catalog.inputs.size() ? catalog.inputs[slot] : QStringList{};
        currentInputSelectors[slot] = slot < catalog.defaults.size() ? catalog.defaults[slot] : 0;
    }
    availableInputSlots = catalog.inputs.size();
    inputCapabilitiesAvailable = true;
    return true;
}
