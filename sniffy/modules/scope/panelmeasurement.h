#ifndef PANELMEASUREMENT_H
#define PANELMEASUREMENT_H

#include <QObject>
#include <QVBoxLayout>

#include "gui/widgetseparator.h"
#include "gui/widgetdial.h"
#include "gui/widgetdialrange.h"
#include "gui/widgetbuttons.h"
#include "gui/widgetlabel.h"
#include "gui/widgetselection.h"

#include "measurement.h"
#include "scopedefs.h"

class PanelMeasurement: public QObject
{
    Q_OBJECT
public:
    explicit PanelMeasurement(QVBoxLayout *destination, QWidget *parent = nullptr);

signals:
    void measurementAdded(Measurement *m);
    void measurementClearAll();

private slots:
    void RMSClicked();
    void rmsAcClicked();
    void meanClicked();
    void minClicked();
    void maxClicked();
    void pkpkClicked();

    void freqClicked();
    void periodClicked();
    void dutyClicked();
    void highClicked();
    void lowClicked();
    void phaseClicked();
    void clearClicked();
public slots:
    void setMeasButtonsColor(int index);

public:
    WidgetButtons *channelButtons;
    WidgetButtons *channelButtonPhaseA;
    WidgetButtons *channelButtonPhaseB;
private:   
    QList<WidgetButtons*> measButtons;
};

#endif // PANELMEASUREMENT_H
