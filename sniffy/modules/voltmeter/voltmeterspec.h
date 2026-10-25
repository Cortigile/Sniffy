#ifndef VOLTMETERSPEC_H
#define VOLTMETERSPEC_H

#include <QObject>
#include <QDebug>
#include <QDataStream>
#include <QStringList>
#include "voltmeterdefs.h"
#include "../abstractspecification.h"

class VoltmeterSpec : public AbstractSpecification
{
public:
    explicit VoltmeterSpec(QObject *parent = nullptr);
    void parseSpecification(QByteArray spec);
    bool parseAvailableInputs(QByteArray response);

    int maxSamplingRate12B;
    int maxSamplingRate8BInterleaved;
    int memorySize;
    int maxADCChannels;
    QString channelPins[MAX_VOLTMETER_CHANNELS];
    QStringList availableInputs[MAX_VOLTMETER_CHANNELS];
    int currentInputSelectors[MAX_VOLTMETER_CHANNELS] = {};
    int availableInputSlots = 0;
    bool inputCapabilitiesAvailable = false;
    qreal Vref;
    qreal VrefCalibration;

};

#endif // VOLTMETERSPEC_H
