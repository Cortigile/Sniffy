#ifndef PANELADVANCED_H
#define PANELADVANCED_H

#include <QObject>
#include <QVBoxLayout>
#include <QSlider>

#include "capturehistory.h"
#include "gui/widgetseparator.h"
#include "gui/widgetdial.h"
#include "gui/widgetdialrange.h"
#include "gui/widgetbuttons.h"
#include "gui/widgetlabel.h"
#include "gui/widgetswitch.h"
#include "gui/widgettextinput.h"

class PanelAdvanced  : public QObject
{
public:
    PanelAdvanced(QVBoxLayout *destination, QWidget *parent = nullptr);
    std::optional<ScopeVoltageRange> voltageRange() const { return customVoltageRange; }

    WidgetButtons *resolutionButtons;
    WidgetButtons *modeButtons;
    WidgetButtons *channelXButtons;
    WidgetButtons *channelYButtons;
    WidgetTextInput *samplingFrequencyInput;
    WidgetLabel *samplingFrequencyReal;
    WidgetTextInput *dataLengthInput;
    WidgetLabel *historyPosition;
    QSlider *historySlider;
    WidgetButtons *historyButtons;
    WidgetSwitch *averagingMode;
    WidgetLabel *averageWindowLabel;
    QSlider *averageWindow;
    WidgetLabel *averageStatus;

private:
    std::optional<ScopeVoltageRange> customVoltageRange;
};

#endif // PANELADVANCED_H
