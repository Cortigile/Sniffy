#ifndef SYNCPWMWINDOW_H
#define SYNCPWMWINDOW_H

#include <QWidget>
#include <QSplitter>

#include "gui/widgetcontrolmodule.h"
#include "gui/widgetseparator.h"
#include "gui/widgetdial.h"
#include "gui/widgetdialrange.h"
#include "gui/widgetchart.h"
#include "gui/widgetswitch.h"
#include "gui/widgetbuttons.h"

#include "syncpwmspec.h"
#include "syncpwmconfig.h"
#include "syncpwmsettings.h"
#include "syncpwmdefs.h"
#include "syncpwmpainter.h"

namespace Ui {
class SyncPwmWindow;
}

class SyncPwmWindow : public QWidget
{
    Q_OBJECT

public:
    explicit SyncPwmWindow(SyncPwmConfig *config, QWidget *parent = nullptr);
    ~SyncPwmWindow();

    SyncPwmSettings *settings;
    SyncPwmPainter *painter;

    void connectDependentChannels();
    void disconnectDependentChannels();
    void setSpecification(SyncPwmSpec *spec);
    void restoreGUIAfterStartup();
    void setStartTxt();
    void setStopTxt();
    void uncheckStartButton();
    void uncheckEquidistantButton();

    void setFreqDial(float val, int chanIndex);
    void setPhaseDial(float val, int chanIndex);
    void setDutyDial(float val, int chanIndex);

    void enableChannel(bool enable, int chanIndex);
    void repaint();

private slots:
    void dialFreqCallback(float val, int chanIndex);

private:
    Ui::SyncPwmWindow *ui;
    SyncPwmConfig *config;
    SyncPwmSpec *spec;

    widgetChart *chart;
};

#endif // SYNCPWMWINDOW_H
