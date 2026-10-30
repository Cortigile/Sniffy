#ifndef ARBGENPANELSETTINGS_H
#define ARBGENPANELSETTINGS_H

#include <QObject>
#include <QVBoxLayout>
#include <QScrollArea>

#include "gui/widgetseparator.h"
#include "gui/widgetdial.h"
#include "gui/widgetdialrange.h"
#include "gui/widgetswitch.h"
#include "gui/widgetbuttons.h"
#include "gui/widgettextinput.h"
#include "gui/widgetlabel.h"
#include "gui/widgettab.h"

#include "../labelformator.h"

#include "arbgeneratordefs.h"
#include "arbgeneratorconfig.h"



class ArbGenPanelSettings : public QObject
{
    Q_OBJECT
public:
    explicit ArbGenPanelSettings(QVBoxLayout *destination, bool isPWMbased = false, QWidget *parent = nullptr);

    void setChannelShown(int index, bool isShown);
    void setChannelShapeControl(int index, int shape);
    void restoreGUI();
    void setMaxNumChannels(int numChannels);
    void setDataLengthLabels(int length, int index);
    void setFreqLabel(qreal freq, int index,int precision = 4);
    void setPWMFreqLabel(qreal freq, int index, int precision = 3);
    void setPWMResolutionLabel(QString res, int index);
    void disableGUI();
    void enableGUI();
    bool isSweepActive(int channel) const;
    void prepareSweepFrequencies(int channel = -1);
    void setChannelFrequencySync(int channel, bool enabled);
    int getMaxNumChannels() const { return maxNumChannels; }

signals:

public:

    //common
    WidgetButtons *buttonsGenerate;
    WidgetButtons *buttonsEnable;
    WidgetButtons *buttonsMemory;
    WidgetTextInput *customLengthInput;
    WidgetButtons *buttonSelectFile;
    WidgetButtons *buttonSWSweepEnable[MAX_SWEEP_CHANNELS_NUM];
    WidgetButtons *buttonsSweepScale[MAX_SWEEP_CHANNELS_NUM];
    WidgetButtons *buttonsSweepMode[MAX_SWEEP_CHANNELS_NUM];
    WidgetButtons *buttonSweepSyncWithCH1;
    WidgetLabel *labelArbFileInfo;
    int arbChannelsEnabled = 0;

    WidgetDialRange *dialFreqSweepMin[MAX_SWEEP_CHANNELS_NUM];
    WidgetDialRange *dialFreqSweepMax[MAX_SWEEP_CHANNELS_NUM];
    WidgetDialRange *dialSweepTime[MAX_SWEEP_CHANNELS_NUM];

    int numChannelsEnabled = 1;
    MemoryLength memorySet = MemoryLength::BEST_FIT;
    int customLength = 0;

    //for each channel
    widgetTab *channelTabs;
    WidgetSeparator *separators[MAX_ARB_CHANNELS_NUM];
    QScrollArea *verChannArea[MAX_ARB_CHANNELS_NUM];

    WidgetButtons *buttonsShape[MAX_ARB_CHANNELS_NUM];
    WidgetDialRange *dialFreqCh[MAX_ARB_CHANNELS_NUM];
    WidgetDialRange *dialOffsetCh[MAX_ARB_CHANNELS_NUM];
    WidgetDialRange *dialAmplitudeCh[MAX_ARB_CHANNELS_NUM];
    WidgetDialRange *dialDutyCh[MAX_ARB_CHANNELS_NUM];
    WidgetDialRange *dialPhaseCh[MAX_ARB_CHANNELS_NUM];
    WidgetSwitch *syncwithCh1[MAX_ARB_CHANNELS_NUM];
    WidgetLabel *labelDataLength[MAX_ARB_CHANNELS_NUM];
    WidgetSwitch *swSyncWithCH1[MAX_ARB_CHANNELS_NUM];
    WidgetSwitch *swSyncPWMWithCH1[MAX_ARB_CHANNELS_NUM];

    WidgetDialRange *dialPWMFreqCh[MAX_ARB_CHANNELS_NUM];
    WidgetLabel *labelPWMResolution[MAX_ARB_CHANNELS_NUM];

    SignalShape signalShape[MAX_ARB_CHANNELS_NUM];
    bool channelEnabled[MAX_ARB_CHANNELS_NUM];
    bool channelSyncWithCH1[MAX_ARB_CHANNELS_NUM];
    bool channelSyncPWMWithCH1[MAX_ARB_CHANNELS_NUM];
    bool isSweepEnabled[MAX_SWEEP_CHANNELS_NUM] = {};
    bool isPWMbased = false;
    bool GUIEnabled = true;
    bool isSilentGUIUpdate = false;


private:
    void copyFreq(int fromCh, int toCh);
    void copyPWMFreq(int fromCh, int toCh);
    void createSweepControls(QWidget *parent);
    void updateSweepControls();
    QWidget *sweepChannelArea[MAX_SWEEP_CHANNELS_NUM];
    QWidget *sweepControlsArea[MAX_SWEEP_CHANNELS_NUM];
    QWidget *sweepParametersArea[MAX_SWEEP_CHANNELS_NUM];
    int maxNumChannels = MAX_ARB_CHANNELS_NUM;

signals:
    void signalChanged();
    void syncRequest();

private slots:
    void buttonEnableChannelCallback(int index);
    void buttonShapeCallback(int clicked, int channel);
    void syncWithCH1Callback(int clicked, int channel);
    void syncPWMWithCH1Callback(int clicked, int channel);
    void signalFrequencyCallback(qreal value, int channel);
    void signalPWMFrequencyCallback(qreal value, int channel);
    void signalChangedCallback();
    void memoryCallback(int index);
    void customLenghtCallback(qreal value);
    void sweepMaxCallback(qreal value, int channel);
    void sweepMinCallback(qreal value, int channel);
    void buttonSweepCallback(int index, int channel);
    void sweepSettingsCallback(int channel);
};

#endif // ARBGENPANELSETTINGS_H
