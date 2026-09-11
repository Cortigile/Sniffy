#ifndef PANELADVANCED_H
#define PANELADVANCED_H

#include <QObject>
#include <QVBoxLayout>

#include "gui/widgetseparator.h"
#include "gui/widgetdial.h"
#include "gui/widgetdialrange.h"
#include "gui/widgetbuttons.h"
#include "gui/widgetlabel.h"
#include "gui/widgetselection.h"
#include "gui/widgettextinput.h"

class PanelAdvanced  : public QObject
{
public:
    PanelAdvanced(QVBoxLayout *destination, QWidget *parent = nullptr);

    WidgetButtons *resolutionButtons;
    WidgetButtons *modeButtons;
    WidgetButtons *channelXButtons;
    WidgetButtons *channelYButtons;
    WidgetTextInput *samplingFrequencyInput;
    WidgetLabel *samplingFrequencyReal;
    WidgetTextInput *dataLengthInput;
};

#endif // PANELADVANCED_H
