#ifndef ARBGENERATORWINDOW_H
#define ARBGENERATORWINDOW_H

#include <QWidget>
#include <QScrollArea>
#include <QDebug>
#include <QVBoxLayout>
#include <QFileDialog>
#include <QtMath>

#include "gui/widgetcontrolmodule.h"
#include "gui/widgetseparator.h"
#include "gui/widgetdial.h"
#include "gui/widgetdialrange.h"
#include "gui/widgetbuttons.h"
#include "gui/widgetlabel.h"
#include "gui/widgetselection.h"
#include "gui/widgetchart.h"
#include "gui/widgettab.h"
#include "gui/widgetlabelarea.h"
#include "gui/widgettextinput.h"

#include "arbgeneratorconfig.h"
#include "arbgenpanelsettings.h"
#include "arbgeneratorspec.h"
#include "signalcreator.h"
#include "arbgeneratorfileloader.h"
#include "arbgensweepcontroller.h"

namespace Ui {
class ArbGeneratorWindow;
}

class ArbGeneratorWindow : public QWidget
{
    Q_OBJECT

public:
    explicit ArbGeneratorWindow(ArbGeneratorConfig *config, bool isPWMbased = false,QWidget *parent = nullptr);
    ~ArbGeneratorWindow();

    void restoreGUIAfterStartup();
    void setSpecification(ArbGeneratorSpec* spec);

    QList<QList<int> > *getGeneratorDACData() const;
    int getNumChannelsEnabled() const;
    qreal getFrequency(int channel);
    qreal getPWMFrequency(int channel);
    void setProgress(int percent);
    void setGeneratorRuning();
    void setGeneratorStopped();
    void setFrequencyLabels(int channel, qreal freq);
    void setPWMFrequencyLabels(int channel, qreal freq);

private:
    void setGenerateButton (QString text, QString color);

    Ui::ArbGeneratorWindow *ui;
    ArbGeneratorConfig *config;
    ArbGeneratorSpec *spec;
    ArbGenPanelSettings *setting;
    ArbGeneratorFileLoader *fileLoader; //this should be actually in module not in window (TODO in far future)
    ArbGenSweepController *sweepController; //this should be actually in module not in window (TODO in far future)

    widgetChart *chart;
    widgetChart *PWMchart;
    QVector<QVector<QPointF>> *generatorChartData;
    QVector<QVector<QPointF>> *generatorPWMChartData;
    QList<QList<int>> *generatorDACData;

    bool isGenerating = false;
    bool isPWMbased = false;

private slots:
    void runGeneratorCallback();
    void createSignalCallback();
    void openFileCallback();
    void sweepTimerCallback(qreal frequency);
    void syncRequestCallback();

signals:
    void runGenerator();
    void stopGenerator();
    void updateFrequency();
    void restartGenerating();
    void activeChannelsChanged();
};

#endif // ARBGENERATORWINDOW_H
