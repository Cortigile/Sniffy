#ifndef COUNTERTABHIGHFREQ_H
#define COUNTERTABHIGHFREQ_H

#include <QObject>
#include <QVBoxLayout>
#include <QLabel>

#include "gui/widgetseparator.h"
#include "gui/widgetdial.h"
#include "gui/widgetdialrange.h"
#include "gui/widgetswitch.h"
#include "gui/widgetbuttons.h"

#include "counterdefs.h"

class CounterTabHighFreq : public QObject
{
    Q_OBJECT
public:
    explicit CounterTabHighFreq(QVBoxLayout *destination, QWidget *parent = nullptr);

    WidgetDialRange *dialAveraging;
    WidgetButtons *buttonsQuantitySwitch;
    WidgetButtons *buttonsErrorSwitch;
    WidgetButtons *buttonsGateTime;
    QLabel *labelAutoInputPrescaler;

private:


signals:    

private:

};

#endif // COUNTERTABHIGHFREQ_H
