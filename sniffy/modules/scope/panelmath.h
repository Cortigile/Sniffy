#ifndef PANELMATH_H
#define PANELMATH_H

#include <QObject>
#include <QVBoxLayout>

#include "gui/widgetbuttons.h"
#include "gui/widgetselection.h"
#include "gui/widgetseparator.h"
#include "gui/widgetlabel.h"
#include "gui/widgettextinput.h"
#include "gui/widgetswitch.h"
#include "gui/widgetdial.h"
#include "gui/widgetdialrange.h"

#include "../labelformator.h"

#include "fftengine.h"

class PanelMath : public QObject
{
    Q_OBJECT
public:
    explicit PanelMath(QVBoxLayout *destination, QWidget *parent = nullptr);
    void symbolicError(int errorPosition);

signals:
    void expressionChanged(QString exp);
    void mathTypeChanged(int index);
    void fftChanged(int length, FFTWindow window, FFTType type, int channelIndex);
    void fftchartChanged(qreal scale, qreal shift, bool isLog = false);


public:
    WidgetButtons *mathType;

    WidgetButtons *btnChannelASel;
    WidgetButtons *btnChannelBSel;
    WidgetSelection *operatorSel;

    WidgetLabel *symbolicTitle;
    WidgetLabel *symbolicDesc;
    WidgetLabel *symbolicExample;
    QString errorExp = "";
    WidgetTextInput *symbolicExpression;

    WidgetButtons *btnChannelFFTSel;
    WidgetSelection *selFFTWindow;
    WidgetButtons *swFFTType;
    WidgetSelection *selFFTLength;
    WidgetDial *dialFFTVertical;
   // WidgetDial *dialFFTHorizontal;
    WidgetDialRange *dialFFTShift;


    int previousMathType = 0;

public slots:
    void typeChanged(int index);
private slots:
    void symbolicExpressionCallback(QString exp);
    void fftCallback();
    void fftTypeCallback();
    void fftChartCallback();

private:
    void hideAll();
    void fillVerticalDials();
    void fillHorizontalDials();
};

#endif // PANELMATH_H
