#ifndef VOLTMETERWINDOW_H
#define VOLTMETERWINDOW_H

#include <QWidget>
#include <QScrollArea>
#include <QDebug>
#include <QVBoxLayout>
#include <QFileDialog>
#include <QDateTime>

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
#include "gui/widgetdisplay.h"
#include "gui/widgetswitch.h"
#include "../../graphics/graphics.h"

#include "voltmeterconfig.h"
#include "voltmeterdefs.h"
#include "channeldata.h"
#include "../labelformator.h"

namespace Ui {
class VoltmeterWindow;
}

class VoltmeterWindow : public QWidget
{
    Q_OBJECT

public:
    explicit VoltmeterWindow(VoltmeterConfig *config, QWidget *parent = nullptr);
    ~VoltmeterWindow();

    void restoreGUIAfterStartup();
    void showData(ChannelData data[], int numChannels);
    void showLogicalReading(int logicalIndex, const ChannelData &data,
                            int slot, const QString &inputName, bool advanceTime);
    void setLogicalInputs(const QStringList &keys, const QStringList &labels);
    void showEmptyCalcs();
    void showVddValue(qreal value);
    void showProgress(int current, int max);
    void setPinsAndNumChannels(QString pins[], int numOfCh);
    void setInputCapabilities(const QStringList inputs[], const int selectors[], int numSlots);
    QList<int> selectedInputIndices(int slot) const;
    QStringList selectedInputNames(int slot) const;
    bool isMultimode() const;
    void appendDatalog(ChannelData data[], int numChannels);
    void appendLogicalDatalog(const ChannelData data[], const QStringList &names, int logicalCount);
    void stopDatalog();
    void startDatalog();
    void setDatalogFilePath(const QString &path);
    bool isLogging() const { return isDataLogRunning; }
    int enabledChannelMask() const;

private:
    Ui::VoltmeterWindow *ui;
    VoltmeterConfig *config;

    QList<WidgetDisplay *> displays;
    QWidget *multimodeSelectionWidget;
    QVBoxLayout *multimodeSelectionLayout;
    QVector<QStringList> availableInputs;
    QVector<QList<int>> availableInputSelectors;
    QVector<QList<WidgetButtons *>> inputButtonRows;
    QVector<QList<QList<int>>> inputButtonSelectorRows;
    int availableInputSlots = 0;
    bool hasSelectableInput = false;
    int previousViewIndex = 0;
    bool wasMultimode = false;
    QWidget *consolidatedView;
    widgetChart *consolidatedChart;
    QList<WidgetLabel *> consolidatedValueLabels;
    QVector<QVector<QPointF>> consolidatedHistory;
    QVector<bool> consolidatedTraceVisible;
    QStringList logicalInputKeys;
    qreal consolidatedSampleTime = 0;

    widgetTab *tabs;
    WidgetButtons *buttonsChannelEnable;
    WidgetButtons *buttonsView;
    WidgetButtons *buttonsAcquisitionMode;
    WidgetButtons *buttonsMode;
    WidgetLabel *labelVdd;
    WidgetDialRange *dialAveraging;
    WidgetLabel *labelProgress;
    WidgetButtons *buttonsCalc;

    WidgetButtons *buttonSelectFile;
    WidgetLabel *labelFile;
    WidgetButtons *buttonStartLog;

    bool isDataLogRunning = false;
    bool datalogMultimode = false;
    QStringList datalogLogicalNames;
    qreal currentVdd;
    QFile *logFile;
    int logSampleIndex;
    QTextStream *logStream;


private slots:
    void channelEnableCallback(int status);
    void inputSelectionCallback();
    void acquisitionModeCallback(int index);
    void averagingCallback(int value);
    void buttonsCalcsCallback(int clicked);
    void viewCallback(int index);
    void updateConsolidatedChart();
    void selectFileCallback();
    void datalogCallback(int index);
    void modeCallback(int index);

signals:
    void averagingChanged(int value);
    void numChannelsEnabledChanged(int numChEnabled);
    void resetMinMax();
    void modeChanged(int index);
    void acquisitionModeChanged(int index);
    void inputSelectionChanged();
};

#endif // VOLTMETERWINDOW_H
