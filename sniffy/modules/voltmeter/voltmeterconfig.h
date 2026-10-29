#ifndef VOLTMETERCONFIG_H
#define VOLTMETERCONFIG_H

#include <QObject>
#include <QDataStream>
#include <QDebug>

#include "../abstractconfiguration.h"
#include "voltmeterdefs.h"

enum class VoltmeterMode
{
    NORMAL = 0,
    FAST = 1,
};

class VoltmeterConfig : public AbstractConfiguration
{
public:
    explicit VoltmeterConfig(QObject *parent = nullptr);
    void parse (QByteArray config);
    QByteArray serialize ();

    int realSamplingRate;
    int ADCresolution;

    int rangeMin; //mV
    int rangeMax; //mv
    int numberOfChannels = 1;

    QString datalogFileName;
    QStringList selectedInputNames[MAX_VOLTMETER_CHANNELS];
    bool hasSavedInputSelections = false;
    bool hasSavedSettings = false;
    bool multimode = false;
    int channelMask = 1;
    int averaging = 4;
    int mainsFrequency = 50;
    int mainsPeriods = 2;
    int samplingRate = 5000;

    VoltmeterMode mode = VoltmeterMode::NORMAL;
    int captureSampleCount() const;
    int sampleCountForPeriods(int periods) const;
};

#endif // VOLTMETERCONFIG_H
