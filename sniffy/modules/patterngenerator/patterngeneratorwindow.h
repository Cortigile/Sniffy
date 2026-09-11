#ifndef PATTERNGENERATORWINDOW_H
#define PATTERNGENERATORWINDOW_H

#include <QWidget>
#include <QScrollArea>
#include <QDebug>
#include <QVBoxLayout>
#include <QFileDialog>
#include <QPushButton>

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
#include "gui/qcustomplot.h"

#include "patterngeneratorconfig.h"
#include "patterngeneratorspec.h"
#include "patterngeneratorsettings.h"
#include "patterngeneratorpatterns.h"
#include "patterngeneratorpainter.h"

namespace Ui {
class PatternGeneratorWindow;
}

class PatternGeneratorWindow : public QWidget
{
    Q_OBJECT

public:
    explicit PatternGeneratorWindow(PatternGeneratorConfig *config, QWidget *parent = nullptr);
    ~PatternGeneratorWindow();

    PatternGeneratorSettings *settings;    
    PatternGeneratorPatterns *patterns;
    QPushButton *buttonGridAlpha = nullptr; // top-right grid transparency stepper

    void restoreGUIAfterStartup();
    void setSpecification(PatternGeneratorSpec* spec);    

    void setProgress(int percent);
    void setGenerateButton(QString text, QString color);
    void setGeneratorState(bool onClick);

    QList<patttype> *getPatternData();

private:
    Ui::PatternGeneratorWindow *ui;
    PatternGeneratorConfig *config;
    PatternGeneratorSpec *spec;    
    PatternGeneratorPainter *painter;

    QList<patttype> *patternData;
    // Track last edited cell to avoid toggling repeatedly while dragging over same point
    int lastEditedChannel = -1;
    int lastEditedPosition = -1;

private slots:
    void patternSelectionChangedCallback(int index);

    void runGeneratorCallback();
    void openFileCallback();
    void resetPatternCallback();   
    void freqChangedDialsCallback(float val, int patternIndex);
    void freqChangedCombosCallback(int index, float realVal);
    void dataLenChangedDialsCallback(float val);

    void quadratureSequenceChangedCallback(int index);

    void chartEditDataOnLeftClickCallback(QGraphicsSceneMouseEvent* event);

signals:
    void runGenerator();
    void stopGenerator();
    // Emitted when user switches to I2C protocol pattern in the dropdown
    void i2cSelected();
    // Emitted when user leaves the I2C pattern (previous pattern was I2C, new is not)
    void i2cDeselected();
};

#endif // PATTERNGENERATORWINDOW_H
