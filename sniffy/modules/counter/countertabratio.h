#ifndef COUNTERTABRATIO_H
#define COUNTERTABRATIO_H

#include <QObject>
#include <QVBoxLayout>

#include "gui/widgetseparator.h"
#include "gui/widgetdial.h"
#include "gui/widgetdialrange.h"
#include "gui/widgetbuttons.h"

#include "counterdefs.h"

class CounterTabRatio : public QObject
{
    Q_OBJECT
public:
    explicit CounterTabRatio(QVBoxLayout *destination, QWidget *parent = nullptr);

    WidgetDialRange *dialSampleCount;
    WidgetButtons *buttonStart;

signals:

};

#endif // COUNTERTABRATIO_H
