#ifndef VOLTAGESOURCEWINDOW_H
#define VOLTAGESOURCEWINDOW_H

#include <QWidget>
#include <QScrollArea>
#include <QDebug>
#include <QVBoxLayout>

#include "gui/widgetcontrolmodule.h"
#include "gui/widgetseparator.h"
#include "gui/widgetdial.h"
#include "gui/widgetdialrange.h"
#include "gui/widgetbuttons.h"
#include "gui/widgetlabel.h"
#include "gui/widgetselection.h"
#include "gui/widgetchart.h"
#include "gui/widgettab.h"
#include "gui/widgetdisplay.h"

#include "voltagesourceconfig.h"
#include "voltagesourcedefs.h"
#include "../labelformator.h"

namespace Ui {
class VoltageSourceWindow;
}

class VoltageSourceWindow : public QWidget
{
    Q_OBJECT

public:
    explicit VoltageSourceWindow(VoltageSourceConfig *config, QWidget *parent = nullptr);
    ~VoltageSourceWindow();

    void restoreGUIAfterStartup();
    void setNumberOfChannels(int numChannels);
    void setDisplayValue(qreal value, int channelIndex);
    void setBarValue(qreal value, int channelIndex);
    void setRange(qreal min, qreal max);
    void setPins(QString pins[],int numChann);
    void setRealVdda(qreal value);

private:
    Ui::VoltageSourceWindow *ui;
    VoltageSourceConfig *config;

    QList<WidgetDisplay *> displays;
    QList<WidgetDialRange *> dials;

    WidgetLabel *labelVDDA;
    QVector<qreal> rangeMins;
    QVector<qreal> rangeMaxs;

private slots:
    void dialChangedCallback(qreal value, int channel);

signals:
    void voltageChanged(qreal value, int channel);
};

#endif // VOLTAGESOURCEWINDOW_H
