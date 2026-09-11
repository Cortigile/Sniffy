#ifndef PANELSETTINGS_H
#define PANELSETTINGS_H

#include <QObject>
#include <QVBoxLayout>

#include "gui/widgetseparator.h"
#include "gui/widgetdial.h"
#include "gui/widgetdialrange.h"
#include "gui/widgetswitch.h"
#include "gui/widgetbuttons.h"

class PanelSettings : public QObject
{
    Q_OBJECT
public:
    explicit PanelSettings(QVBoxLayout *destination, QWidget *parent = nullptr);

signals:

public:
    WidgetDial *dialTimeBase;
    WidgetButtons *buttonsChannelEnable;
    WidgetButtons *buttonsTriggerMode;
    WidgetButtons *buttonsTriggerChannel;
    WidgetButtons *buttonsTriggerEdge;

    WidgetDialRange *dialPretrigger;
    WidgetDialRange *dialTriggerValue;

    WidgetButtons *buttonsMemorySet;

    WidgetButtons *buttonsChannelVertical;
    WidgetDial *dialVerticalScale;
    WidgetDialRange *dialVerticalShift;

private:
    void fillTimeBase();

    const QString verticalControlColor = "color:"+Graphics::getChannelColor(0);
    const QString verticalControlBcgrColor = "background-color:"+Graphics::getChannelColor(0);

};

#endif // PANELSETTINGS_H
