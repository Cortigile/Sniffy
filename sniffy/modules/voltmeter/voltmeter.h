#ifndef VOLTMETER_H
#define VOLTMETER_H

#include <QObject>

#include "../abstractmodule.h"
#include "voltmeterspec.h"
#include "voltmeterconfig.h"
#include "voltmeterwindow.h"
#include <QVector>
#include <QString>

#include "channeldata.h"

#include "../scope/measurement.h"
#include "../scope/meascalculations.h"


class Voltmeter : public AbstractModule
{
    Q_OBJECT
public:
    explicit Voltmeter(QObject *parent = nullptr);
    QWidget* getWidget();
    const ChannelData& channelData(int ch) const { return data[ch]; }
    const ChannelData& logicalChannelData(int index) const { return logicalData[index]; }
    QString logicalChannelName(int index) const;
    int logicalChannelSlot(int index) const;
    int logicalChannelCount() const { return logicalChannelNames.size(); }
    double getVdd() const { return realVdd; }
    int getNumChannelsEnabled() const { return numChannelsEnabled; }


public slots:
    void parseData(QByteArray data);
    void writeConfiguration();
    void parseConfiguration(QByteArray config);
    QByteArray getConfiguration();
    void startModule();
    void stopModule();

private slots:
    //In case hold is needed

    void showHoldButtonCallback();
    void holdButtonCallback(bool held);
    void updateSamplingChannel(void);
    void updateMeasurement(QList<Measurement*> m);
    void setAveraging(int value);
    void setNumChannelsEnabled(int value);
    void resetMinMax();
    void setMode(int index);
    void handleInputSelectionChanged();
    void handleAcquisitionModeChanged(int index);

private:
    friend class VoltmeterTest;
    VoltmeterConfig *config;
    VoltmeterWindow *voltWindow;

    double realVdd = 1;
    bool isReferenceMeasured = false;
    bool isStartup = true;
    int samplesTaken = 0;
    int samplesToTakeTotal = 4;
    int numChannelsEnabled = 1;

    struct LogicalChannel {
        int slot = -1;
        int selector = -1;
        QString name;
    };
    QVector<LogicalChannel> logicalChannelNames;
    ChannelData logicalData[MAX_VOLTMETER_LOGICAL_INPUTS];
    QList<qreal> logicalRawVoltage[MAX_VOLTMETER_LOGICAL_INPUTS];
    int scanPositions[MAX_VOLTMETER_CHANNELS] = {};
    int activeCaptureSelectors[MAX_VOLTMETER_CHANNELS] = {};
    bool activeCaptureIsMultimode = false;
    bool inputCapabilitiesReceived = false;

    QList<qreal> dataRawVoltage[MAX_VOLTMETER_CHANNELS];
    ChannelData data[MAX_VOLTMETER_CHANNELS];

    QList<Measurement *> voltMeasList;
    MeasCalculations *measCalc;

    QVector<QVector<QPointF>> *voltmeterData;
    QList<PinFunctionInfo> activePinFunctions() const override;

    void stopSampling();
    void startSampling();
    void restartSampling();
    void setNumberOfChannels(int num);
    void setVDDSampling();
    void setDefaultSampling();
    void configureMultimodeCapture();
    void applyNextMultimodeCapture();
    void rebuildLogicalChannelMap();

    qreal getAverage(QList<qreal> *list);




};

#endif // VOLTMETER_H
