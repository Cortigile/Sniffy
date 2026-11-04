#include "frequencyanalysiswindow.h"
#include "graphics/graphics.h"
#include "gui/widgetbuttons.h"
#include "gui/widgetdialrange.h"
#include "gui/widgetselection.h"
#include "gui/widgettab.h"
#include "modules/adcinputcatalog.h"

#include <QtCharts/QAbstractAxis>
#include <QtCharts/QChart>
#include <QtCharts/QChartView>
#include <QtCharts/QLegend>
#include <QtCharts/QLineSeries>
#include <QtCharts/QLogValueAxis>
#include <QtCharts/QValueAxis>
#include <QFileDialog>
#include <QFileInfo>
#include <QGraphicsLineItem>
#include <QGraphicsSimpleTextItem>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTableWidget>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <utility>

namespace {
constexpr float minimumFrequency = 0.000001f;
constexpr float maximumFrequency = 10000000;
constexpr float minimumAmplitude = 0.000001f;
constexpr float defaultMaximumVoltage = 3.3f;
constexpr int maximumPointsPerDecade = 1000;
constexpr int maximumAverages = 128;
constexpr int minimumPeriods = 16;
constexpr int maximumPeriods = FrequencyAnalysis::maximumPeriods;
constexpr int maximumSettlingMs = FrequencyAnalysis::maximumSettlingMs;
constexpr int maximumInputSelector = 255;

QString number(double value)
{
    return QString::number(value, 'g', 17);
}

QString concise(double value)
{
    return QString::number(value, 'g', 6);
}

QString csvField(QString value)
{
    if (!value.contains(QLatin1Char(',')) && !value.contains(QLatin1Char('"'))
        && !value.contains(QLatin1Char('\r')) && !value.contains(QLatin1Char('\n')))
        return value;
    value.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QLatin1Char('"') + value + QLatin1Char('"');
}

QString spacingName(FrequencyAnalysis::Spacing spacing)
{
    return spacing == FrequencyAnalysis::Spacing::Linear
        ? QStringLiteral("linear") : QStringLiteral("logarithmic");
}

void styleAxis(QAbstractAxis *axis)
{
    const auto &palette = Graphics::palette();
    axis->setLabelsColor(QColor(palette.textLabel));
    axis->setTitleBrush(QColor(palette.textAll));
    axis->setLinePenColor(QColor(palette.chartGridlegDefault));
    axis->setGridLineColor(QColor(palette.chartGridlegLowContrast));
    QColor minor(palette.chartGridlegLowContrast);
    minor.setAlpha(65);
    axis->setMinorGridLineColor(minor);
    axis->setMinorGridLineVisible(true);
}

void setFrequencyRange(QAbstractAxis *axis, double minimum, double maximum)
{
    if (auto *log = qobject_cast<QLogValueAxis *>(axis))
        log->setRange(minimum, maximum);
    else if (auto *linear = qobject_cast<QValueAxis *>(axis))
        linear->setRange(minimum, maximum);
}

QLabel *plainLabel(QWidget *parent, const QString &name, const QString &text = {})
{
    auto *label = new QLabel(text, parent);
    label->setObjectName(name);
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return label;
}
}

class FrequencyAnalysisChartView final : public QChartView
{
public:
    FrequencyAnalysisChartView(QChart *chart, QWidget *parent,
                               std::function<void(int, double)> drag)
        : QChartView(chart, parent), m_drag(std::move(drag))
    {
        setRenderHint(QPainter::Antialiasing);
        setMouseTracking(true);
        for (int cursor = 0; cursor < 2; ++cursor) {
            const QColor color(Graphics::getChannelColor(0));
            m_lines[cursor] = new QGraphicsLineItem(chart);
            m_lines[cursor]->setPen(QPen(color, 1.5, Qt::DashLine));
            m_lines[cursor]->setZValue(100);
            m_labels[cursor] = new QGraphicsSimpleTextItem(cursor == 0 ? "A" : "B", chart);
            m_labels[cursor]->setBrush(color);
            m_labels[cursor]->setZValue(101);
        }
        connect(chart, &QChart::plotAreaChanged, this, [this] { redrawCursors(); });
        setCursors(false, 0, 0);
    }

    void setCursors(bool enabled, double a, double b)
    {
        m_enabled = enabled;
        m_frequencies[0] = a;
        m_frequencies[1] = b;
        if (!enabled)
            m_active = -1;
        redrawCursors();
    }

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        const auto point = chartPoint(event);
        if (m_enabled && event->button() == Qt::LeftButton && chart()->plotArea().contains(point)) {
            auto *series = chart()->series().first();
            const double a = chart()->mapToPosition(QPointF(m_frequencies[0], 0), series).x();
            const double b = chart()->mapToPosition(QPointF(m_frequencies[1], 0), series).x();
            m_active = std::abs(point.x() - a) <= std::abs(point.x() - b) ? 0 : 1;
            dragTo(point);
            event->accept();
            return;
        }
        QChartView::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (m_active >= 0 && event->buttons().testFlag(Qt::LeftButton)) {
            dragTo(chartPoint(event));
            event->accept();
            return;
        }
        QChartView::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (m_active >= 0 && event->button() == Qt::LeftButton) {
            dragTo(chartPoint(event));
            m_active = -1;
            event->accept();
            return;
        }
        QChartView::mouseReleaseEvent(event);
    }

private:
    QGraphicsLineItem *m_lines[2];
    QGraphicsSimpleTextItem *m_labels[2];
    std::function<void(int, double)> m_drag;
    double m_frequencies[2] = {0, 0};
    int m_active = -1;
    bool m_enabled = false;

    QPointF chartPoint(QMouseEvent *event) const
    {
        return chart()->mapFromScene(mapToScene(event->position().toPoint()));
    }

    void dragTo(QPointF point)
    {
        const auto area = chart()->plotArea();
        point.setX(std::clamp(point.x(), area.left(), area.right()));
        // Qt Charts performs the logarithmic/linear axis conversion, not the mouse handler.
        const double frequency = chart()->mapToValue(point, chart()->series().first()).x();
        if (std::isfinite(frequency) && frequency > 0)
            m_drag(m_active, frequency);
    }

    void redrawCursors()
    {
        const auto area = chart()->plotArea();
        for (int cursor = 0; cursor < 2; ++cursor) {
            const bool visible = m_enabled && m_frequencies[cursor] > 0
                && !chart()->series().isEmpty() && area.width() > 0;
            m_lines[cursor]->setVisible(visible);
            m_labels[cursor]->setVisible(visible);
            if (!visible)
                continue;
            const double x = chart()->mapToPosition(QPointF(m_frequencies[cursor], 0),
                                                    chart()->series().first()).x();
            m_lines[cursor]->setLine(x, area.top(), x, area.bottom());
            const double labelX = std::clamp(x + 3, area.left(),
                                             std::max(area.left(), area.right() - 15));
            m_labels[cursor]->setPos(labelX, area.top() + cursor * 15);
        }
    }
};

FrequencyAnalysisWindow::FrequencyAnalysisWindow(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("frequencyAnalysisWindow"));
    setWindowTitle(tr("Frequency analysis"));
    const auto &palette = Graphics::palette();
    setStyleSheet(QStringLiteral(
        "QWidget#frequencyAnalysisWindow { background: %1; color: %2; }"
        "QTableWidget { background: %3; alternate-background-color: %1; color: %2; gridline-color: %4; }"
        "QHeaderView::section { background: %1; color: %2; padding: 4px; }")
        .arg(palette.windowWidget, palette.textAll, palette.dataInputArea,
             palette.chartGridlegLowContrast));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(5, 5, 5, 5);
    auto *splitter = new QSplitter(Qt::Horizontal, this);
    layout->addWidget(splitter);
    auto *controls = new QWidget(splitter);
    controls->setObjectName(QStringLiteral("frequencyAnalysisControls"));
    controls->setMinimumWidth(310);
    auto *controlsLayout = new QVBoxLayout(controls);
    controlsLayout->setContentsMargins(0, 0, 0, 0);
    controlsLayout->setSpacing(3);
    m_settingsTabs = new widgetTab(controls, 4);
    m_settingsTabs->setObjectName(QStringLiteral("frequencyAnalysisSettings"));
    m_settingsTabs->setText(tr("Sweep"), 0);
    m_settingsTabs->setText(tr("Acquisition"), 1);
    m_settingsTabs->setText(tr("Connection"), 2);
    m_settingsTabs->setText(tr("View"), 3);
    auto *sweep = m_settingsTabs->getLayout(0);
    auto *acquisition = m_settingsTabs->getLayout(1);
    auto *connection = m_settingsTabs->getLayout(2);
    auto *viewSettings = m_settingsTabs->getLayout(3);
    sweep->parentWidget()->setObjectName(QStringLiteral("sweepSettings"));
    acquisition->parentWidget()->setObjectName(QStringLiteral("acquisitionSettings"));
    connection->parentWidget()->setObjectName(QStringLiteral("connectionSettings"));
    viewSettings->parentWidget()->setObjectName(QStringLiteral("viewSettings"));
    controlsLayout->addWidget(m_settingsTabs, 1);

    const auto dial = [&](QVBoxLayout *destination, const char *name, const QString &title,
                          float minimum, float maximum, float value, const QString &unit,
                          float precision, bool logarithmic, int decimals) {
        auto *input = new WidgetDialRange(m_settingsTabs, title);
        input->setObjectName(QString::fromLatin1(name));
        input->setRange(minimum, maximum, unit, 1, precision, value,
                        logarithmic, decimals, true);
        destination->addWidget(input);
        return input;
    };
    const auto integerDial = [&](QVBoxLayout *destination, const char *name, const QString &title,
                                 int minimum, int maximum, int value, const QString &unit = {}) {
        auto *input = dial(destination, name, title, minimum, maximum, value, unit, 1, false,
                           0);
        input->hideUnitSelection();
        return input;
    };
    const auto buttons = [&](QVBoxLayout *destination, const char *name, const QString &title,
                             const QString &first, const QString &second, int selected) {
        auto *input = new WidgetButtons(m_settingsTabs, 2, ButtonTypes::RADIO, title, selected);
        input->setObjectName(QString::fromLatin1(name));
        input->setText(first, 0);
        input->setText(second, 1);
        destination->addWidget(input);
        return input;
    };
    const FrequencyAnalysis::Settings defaults;
    m_startFrequency = dial(sweep, "startFrequency", tr("Start frequency"), minimumFrequency,
                            maximumFrequency, defaults.startFrequency, tr("Hz"), 0.000001f, true, 3);
    m_stopFrequency = dial(sweep, "stopFrequency", tr("Stop frequency"), minimumFrequency,
                           maximumFrequency, defaults.stopFrequency, tr("Hz"), 0.000001f, true, 3);
    m_startAmplitude = dial(sweep, "startAmplitude", tr("Start amplitude"), minimumAmplitude,
                            defaultMaximumVoltage, defaults.startAmplitude, tr("Vpp"), 0.000001f, true, 3);
    m_stopAmplitude = dial(sweep, "stopAmplitude", tr("Stop amplitude"), minimumAmplitude,
                           defaultMaximumVoltage, defaults.stopAmplitude, tr("Vpp"), 0.000001f, true, 3);
    for (auto *input : {m_startFrequency, m_stopFrequency})
        input->setToolTip(tr("Requested endpoint, not a board bandwidth specification. Actual frequencies are measured."));
    for (auto *input : {m_startAmplitude, m_stopAmplitude})
        input->setToolTip(tr("Peak-to-peak sine amplitude, not RMS. Output spans offset - Vpp/2 to offset + Vpp/2."));
    m_pointsPerDecade = integerDial(sweep, "pointsPerDecade", tr("Points per decade"),
                                    1, maximumPointsPerDecade, defaults.pointsPerDecade);
    m_pointsPerDecade->setToolTip(tr(
        "Both distributions use ceil(log10(stop/start) \u00d7 points per decade) intervals, including both endpoints."));
    sweep->addStretch();

    m_frequencySpacing = buttons(acquisition, "frequencySpacing", tr("Frequency spacing"),
                                  tr("Exponential"), tr("Linear"), 0);
    m_amplitudeSpacing = buttons(acquisition, "amplitudeSpacing", tr("Amplitude spacing"),
                                  tr("Linear"), tr("Logarithmic"), 0);
    m_offset = dial(acquisition, "offset", tr("DC offset"), 0,
                    defaultMaximumVoltage, defaults.offset, tr("V"), 0.000001f, false, 3);
    m_averages = integerDial(acquisition, "averages", tr("Averages"), 1,
                             maximumAverages, defaults.averages);
    m_periods = integerDial(acquisition, "periods", tr("Observed periods"), minimumPeriods,
                            maximumPeriods, defaults.periods);
    m_settlingSeconds = dial(acquisition, "settlingSeconds", tr("Settling time"), 0,
                             maximumSettlingMs / 1000.0f, defaults.settlingMs / 1000.0f,
                             tr("s"), 0.001f, false, 3);
    m_settlingSeconds->setToolTip(tr("Settling delay from 0 to 10 s, with millisecond resolution."));
    m_allowAliasing = new WidgetButtons(m_settingsTabs, 1, ButtonTypes::CHECKABLE);
    m_allowAliasing->setObjectName(QStringLiteral("allowAliasing"));
    m_allowAliasing->setText(tr("Allow undersampling (Aliasing mode)"));
    m_allowAliasing->setChecked(defaults.allowAliasing, 0);
    acquisition->addWidget(m_allowAliasing);
    m_timingLabel = plainLabel(controls, QStringLiteral("frequencyAnalysisTimingEstimate"));
    m_timingLabel->setToolTip(tr(
        "Acquisition and settling estimates only. Waveform upload, USB/UART transfer, processing "
        "and retries add time. The range includes fast-auto trigger-wait uncertainty. "
        "Actual generator and ADC frequencies can differ from their requested values."));
    acquisition->addStretch();

    m_referenceSelection = new WidgetSelection(m_settingsTabs, tr("CH1 reference"));
    m_referenceSelection->setObjectName(QStringLiteral("referenceInput"));
    m_responseSelection = new WidgetSelection(m_settingsTabs, tr("CH2 response"));
    m_responseSelection->setObjectName(QStringLiteral("responseInput"));
    connection->addWidget(m_referenceSelection);
    connection->addWidget(m_responseSelection);
    m_connectionDiagram = plainLabel(m_settingsTabs, QStringLiteral("frequencyAnalysisConnectionDiagram"));
    m_connectionDiagram->setStyleSheet(QStringLiteral(
        "padding: 8px; background: %1; color: %2;").arg(palette.dataInputArea, palette.textAll));
    connection->addWidget(m_connectionDiagram);
    m_wiring = plainLabel(m_settingsTabs, QStringLiteral("frequencyAnalysisWiring"),
                          tr("Board wiring information is not available yet."));
    connection->addWidget(m_wiring);
    m_connectionError = plainLabel(m_settingsTabs, QStringLiteral("frequencyAnalysisConnectionError"));
    m_connectionError->setStyleSheet(QStringLiteral("color: %1;").arg(palette.warning));
    connection->addWidget(m_connectionError);
    connection->addWidget(plainLabel(m_settingsTabs, QStringLiteral("frequencyAnalysisConnectionNotes"), tr(
        "Connect a common ground. Keep offset \u00b1 Vpp/2 and both signals within DAC/ADC limits. "
        "CH1 measures the DUT input; CH2 measures its output.")));
    connection->addStretch();

    auto *actions = new QHBoxLayout;
    actions->setSpacing(3);
    const auto action = [&](const char *name, const QString &text, const QString &color) {
        auto *input = new WidgetButtons(controls, 1, ButtonTypes::NORMAL);
        input->setObjectName(QString::fromLatin1(name));
        input->setText(text);
        input->setColor(color, 0);
        input->setMinimumWidth(0);
        input->setMinimumHeight(36);
        actions->addWidget(input, 1);
        return input;
    };
    m_start = action("startSweep", tr("Start sweep"), palette.running);
    m_cancel = action("cancelSweep", tr("Cancel"), palette.error);
    m_export = action("exportCsv", tr("Export CSV"), palette.controls);
    controlsLayout->addLayout(actions);
    controlsLayout->addWidget(m_timingLabel);
    m_resultCount = plainLabel(controls, QStringLiteral("frequencyAnalysisResultCount"));
    controlsLayout->addWidget(m_resultCount);
    m_status = plainLabel(controls, QStringLiteral("frequencyAnalysisStatus"));
    controlsLayout->addWidget(m_status);

    auto *resultTabs = new QTabWidget(splitter);
    resultTabs->setObjectName(QStringLiteral("frequencyAnalysisResultTabs"));
    auto *plots = new QWidget(resultTabs);
    auto *plotLayout = new QVBoxLayout(plots);
    plotLayout->setContentsMargins(0, 0, 0, 0);
    const auto createPlot = [&](const QString &title, const QString &axisTitle, const char *name,
                                QChart *&chart, QLineSeries *&series,
                                QValueAxis *&axis, FrequencyAnalysisChartView *&view) {
        chart = new QChart;
        chart->setTitle(title);
        chart->setTitleBrush(QColor(palette.textAll));
        chart->setBackgroundBrush(QColor(palette.chart));
        chart->setBackgroundRoundness(0);
        chart->legend()->hide();
        chart->setMargins(QMargins(6, 2, 6, 2));
        series = new QLineSeries(chart);
        series->setPen(QPen(QColor(Graphics::getChannelColor(0)), 2));
        series->setPointsVisible(true);
        chart->addSeries(series);
        axis = new QValueAxis(chart);
        axis->setTitleText(axisTitle);
        axis->setLabelFormat(QStringLiteral("%.4g"));
        axis->setMinorTickCount(4);
        styleAxis(axis);
        chart->addAxis(axis, Qt::AlignLeft);
        series->attachAxis(axis);
        view = new FrequencyAnalysisChartView(chart, plots,
            [this](int cursor, double frequency) { moveCursor(cursor, frequency); });
        view->setObjectName(QString::fromLatin1(name));
        view->setMinimumHeight(190);
        plotLayout->addWidget(view, 1);
    };
    createPlot(tr("Gain: CH2 / CH1"), tr("Gain (dB)"), "frequencyGainPlot",
                m_gainChart, m_gainSeries, m_gainAxis, m_gainView);
    createPlot(tr("Relative phase: CH2 / CH1"), tr("Phase (\u00b0)"), "frequencyPhasePlot",
                m_phaseChart, m_phaseSeries, m_phaseAxis, m_phaseView);
    m_gainSeries->setName(tr("Gain (dB)"));
    m_phaseSeries->setName(tr("Phase (\u00b0)"));
    m_unwrapPhase = new WidgetButtons(m_settingsTabs, 1, ButtonTypes::CHECKABLE);
    m_unwrapPhase->setObjectName(QStringLiteral("unwrapPhase"));
    m_unwrapPhase->setText(tr("Unwrap phase"));
    m_unwrapPhase->setToolTip(tr(
        "Removes artificial +/-180 degree jumps: +170 followed by -170 is displayed as +190. "
        "It changes the display branch, not the measured phase. Sparse sweeps can be ambiguous."));
    viewSettings->addWidget(m_unwrapPhase);
    m_showCursors = new WidgetButtons(m_settingsTabs, 1, ButtonTypes::CHECKABLE);
    m_showCursors->setObjectName(QStringLiteral("showCursors"));
    m_showCursors->setText(tr("Cursors A/B"));
    viewSettings->addWidget(m_showCursors);
    m_combinedPlot = new WidgetButtons(m_settingsTabs, 1, ButtonTypes::CHECKABLE);
    m_combinedPlot->setObjectName(QStringLiteral("combinedPlot"));
    m_combinedPlot->setText(tr("Gain + phase in one plot"));
    m_combinedPlot->setChecked(false, 0);
    m_combinedPlot->setToolTip(tr("Solid gain uses the left dB axis; dashed phase uses the right degree axis."));
    viewSettings->addWidget(m_combinedPlot);
    viewSettings->addStretch();
    m_cursorAReadout = plainLabel(plots, QStringLiteral("frequencyCursorAReadout"));
    m_cursorBReadout = plainLabel(plots, QStringLiteral("frequencyCursorBReadout"));
    m_cursorDifference = plainLabel(plots, QStringLiteral("frequencyCursorDifference"));
    for (auto *label : {m_cursorAReadout, m_cursorBReadout, m_cursorDifference})
        label->setStyleSheet(QStringLiteral("color: %1;").arg(Graphics::getChannelColor(0)));
    plotLayout->addWidget(m_cursorAReadout);
    plotLayout->addWidget(m_cursorBReadout);
    plotLayout->addWidget(m_cursorDifference);
    resultTabs->addTab(plots, tr("Bode plots"));
    m_table = new QTableWidget(0, 15, resultTabs);
    m_table->setObjectName(QStringLiteral("frequencyAnalysisResults"));
    m_table->setHorizontalHeaderLabels({
        tr("Requested f (Hz)"), tr("Actual f (Hz)"), tr("Drive (Vpp)"),
        tr("Actual Fs (Hz)"), tr("N"), tr("Gain (dB)"),
        tr("Wrapped phase (\u00b0)"), tr("Unwrapped phase (\u00b0)"),
        tr("CH1 RMS (V)"), tr("CH2 RMS (V)"), tr("Coherence"),
        tr("Reference pin"), tr("Response pin"), tr("DAC pin"), tr("Observed cycles")});
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setAlternatingRowColors(true);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    resultTabs->addTab(m_table, tr("Data"));
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({350, 750});

    m_estimateTimer = new QTimer(this);
    m_estimateTimer->setSingleShot(true);
    m_estimateTimer->setInterval(150);
    connect(m_estimateTimer, &QTimer::timeout, this, &FrequencyAnalysisWindow::recalculateEstimate);
    for (auto *input : {m_startFrequency, m_stopFrequency, m_startAmplitude, m_stopAmplitude, m_offset})
        connect(input, &WidgetDialRange::valueChanged, this, [this] { scheduleEstimate(); });
    for (auto *input : {m_pointsPerDecade, m_averages, m_periods, m_settlingSeconds})
        connect(input, &WidgetDialRange::valueChanged, this, [this] { scheduleEstimate(); });
    for (auto *input : {m_frequencySpacing, m_amplitudeSpacing, m_allowAliasing})
        connect(input, &WidgetButtons::clicked, this, [this] {
            if (m_results.isEmpty())
                clearResults();
            scheduleEstimate();
        });
    connect(m_unwrapPhase, &WidgetButtons::clicked, this, &FrequencyAnalysisWindow::updatePhasePlot);
    connect(m_showCursors, &WidgetButtons::clicked, this, &FrequencyAnalysisWindow::updateCursors);
    connect(m_combinedPlot, &WidgetButtons::clicked, this, &FrequencyAnalysisWindow::updatePlotLayout);
    connect(m_referenceSelection, &WidgetSelection::selectedIndexChanged, this,
            [this](int index) { acceptInputSelection(0, index); });
    connect(m_responseSelection, &WidgetSelection::selectedIndexChanged, this,
            [this](int index) { acceptInputSelection(1, index); });
    connect(m_start, &WidgetButtons::clicked, this, [this] {
        recalculateEstimate();
        if (!m_running && m_inputsReady && m_inputError.isEmpty() && m_estimateValid)
            emit startRequested();
        else if (!m_inputError.isEmpty())
            setStatus(m_inputError, true);
        else if (!m_estimateValid)
            setStatus(m_timingLabel->text(), true);
    });
    connect(m_cancel, &WidgetButtons::clicked, this, &FrequencyAnalysisWindow::cancelRequested);
    connect(m_export, &WidgetButtons::clicked, this, &FrequencyAnalysisWindow::exportCsv);
    rebuildInputSelections();
    updateConnections();
    clearResults();
    recalculateEstimate();
    setRunning(false);
    setStatus(tr("Waiting for board capabilities and ADC pin metadata. Loading a session never starts output."));
}

FrequencyAnalysis::Settings FrequencyAnalysisWindow::settings() const
{
    FrequencyAnalysis::Settings result;
    result.startFrequency = m_startFrequency->getRealValue();
    result.stopFrequency = m_stopFrequency->getRealValue();
    result.startAmplitude = m_startAmplitude->getRealValue();
    result.stopAmplitude = m_stopAmplitude->getRealValue();
    result.offset = m_offset->getRealValue();
    result.pointsPerDecade = qRound(m_pointsPerDecade->getRealValue());
    result.averages = qRound(m_averages->getRealValue());
    result.periods = qRound(m_periods->getRealValue());
    result.settlingMs = qRound(m_settlingSeconds->getRealValue() * 1000.0);
    result.frequencySpacing = m_frequencySpacing->isChecked(1)
        ? FrequencyAnalysis::Spacing::Linear : FrequencyAnalysis::Spacing::Logarithmic;
    result.amplitudeSpacing = m_amplitudeSpacing->isChecked(1)
        ? FrequencyAnalysis::Spacing::Logarithmic : FrequencyAnalysis::Spacing::Linear;
    result.allowAliasing = m_allowAliasing->isChecked(0);
    result.channelSkewSeconds = 0;
    result.referenceInput = m_referenceInput;
    result.responseInput = m_responseInput;
    return result;
}

QJsonObject FrequencyAnalysisWindow::settingsJson() const
{
    const auto current = settings();
    return {
        {QStringLiteral("startFrequency"), current.startFrequency},
        {QStringLiteral("stopFrequency"), current.stopFrequency},
        {QStringLiteral("startAmplitude"), current.startAmplitude},
        {QStringLiteral("stopAmplitude"), current.stopAmplitude},
        {QStringLiteral("offset"), current.offset},
        {QStringLiteral("pointsPerDecade"), current.pointsPerDecade},
        {QStringLiteral("averages"), current.averages},
        {QStringLiteral("periods"), current.periods},
        {QStringLiteral("settlingMs"), current.settlingMs},
        {QStringLiteral("frequencySpacing"), spacingName(current.frequencySpacing)},
        {QStringLiteral("amplitudeSpacing"), spacingName(current.amplitudeSpacing)},
        {QStringLiteral("allowAliasing"), current.allowAliasing},
        {QStringLiteral("referenceInput"), current.referenceInput},
        {QStringLiteral("responseInput"), current.responseInput},
        {QStringLiteral("unwrapPhase"), m_unwrapPhase->isChecked(0)},
        {QStringLiteral("showCursors"), m_showCursors->isChecked(0)},
        {QStringLiteral("combinedPlot"), m_combinedPlot->isChecked(0)}
    };
}

QByteArray FrequencyAnalysisWindow::serialize() const
{
    return QJsonDocument(QJsonObject{
        {QStringLiteral("version"), 3},
        {QStringLiteral("settings"), settingsJson()}
    }).toJson(QJsonDocument::Compact);
}

bool FrequencyAnalysisWindow::restore(const QByteArray &data, QString &error)
{
    error.clear();
    const auto reject = [&](const QString &message) {
        error = message;
        setStatus(error, true);
        return false;
    };
    if (m_running)
        return reject(tr("Cannot restore frequency analysis settings while a sweep is running or draining."));
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return reject(tr("Invalid frequency analysis session JSON: %1")
                          .arg(parseError.error != QJsonParseError::NoError
                                   ? parseError.errorString() : tr("expected an object")));
    const auto root = document.object();
    const auto version = root.value(QStringLiteral("version"));
    if (!version.isDouble() || (version.toDouble() != 1 && version.toDouble() != 2 && version.toDouble() != 3))
        return reject(tr("Unsupported or missing frequency analysis session version (expected 1, 2 or 3)."));
    if (!root.value(QStringLiteral("settings")).isObject())
        return reject(tr("Frequency analysis session is missing its settings object."));
    const bool legacy = version.toInt() == 1;
    const auto object = root.value(QStringLiteral("settings")).toObject();
    const auto readNumber = [&](const char *name, double minimum, double maximum, double &target) {
        const QString key = QString::fromLatin1(name);
        const auto value = object.value(key);
        if (!value.isDouble() || !std::isfinite(value.toDouble())
            || value.toDouble() < minimum || value.toDouble() > maximum) {
            error = tr("Invalid or missing '%1': expected a finite number in [%2, %3].")
                        .arg(key, number(minimum), number(maximum));
            return false;
        }
        target = value.toDouble();
        return true;
    };
    const auto readInteger = [&](const char *name, int minimum, int maximum, int &target) {
        double value = 0;
        if (!readNumber(name, minimum, maximum, value))
            return false;
        if (std::floor(value) != value) {
            error = tr("Invalid '%1': expected a whole number.").arg(QString::fromLatin1(name));
            return false;
        }
        target = static_cast<int>(value);
        return true;
    };
    const auto readSpacing = [&](const char *name, FrequencyAnalysis::Spacing &target) {
        const QString key = QString::fromLatin1(name);
        const auto value = object.value(key);
        if (value.isString() && value.toString() == QLatin1String("linear"))
            target = FrequencyAnalysis::Spacing::Linear;
        else if (value.isString() && value.toString() == QLatin1String("logarithmic"))
            target = FrequencyAnalysis::Spacing::Logarithmic;
        else {
            error = tr("Invalid or missing '%1': expected 'linear' or 'logarithmic'.").arg(key);
            return false;
        }
        return true;
    };
    FrequencyAnalysis::Settings candidate;
    if (!readNumber("startFrequency", minimumFrequency, maximumFrequency, candidate.startFrequency)
        || !readNumber("stopFrequency", minimumFrequency, maximumFrequency, candidate.stopFrequency)
        || !readNumber("startAmplitude", minimumAmplitude, m_startAmplitude->getRangeMax(), candidate.startAmplitude)
        || !readNumber("stopAmplitude", minimumAmplitude, m_stopAmplitude->getRangeMax(), candidate.stopAmplitude)
        || !readNumber("offset", m_offset->getRangeMin(), m_offset->getRangeMax(), candidate.offset)
        || !readInteger("pointsPerDecade", 1, maximumPointsPerDecade, candidate.pointsPerDecade)
        || !readInteger("averages", 1, maximumAverages, candidate.averages)
        || !readInteger("periods", minimumPeriods, maximumPeriods, candidate.periods)
        || !readInteger("settlingMs", 0, maximumSettlingMs, candidate.settlingMs)
        || !readSpacing("frequencySpacing", candidate.frequencySpacing)
        || !readSpacing("amplitudeSpacing", candidate.amplitudeSpacing))
        return reject(error);
    if (!legacy
        && (!readInteger("referenceInput", -1, maximumInputSelector, candidate.referenceInput)
            || !readInteger("responseInput", -1, maximumInputSelector, candidate.responseInput)))
        return reject(error);
    for (const auto &key : {QStringLiteral("allowAliasing"), QStringLiteral("unwrapPhase")}) {
        if (!object.value(key).isBool())
            return reject(tr("Invalid or missing '%1': expected a boolean.").arg(key));
    }
    for (const auto &key : {QStringLiteral("showCursors"), QStringLiteral("combinedPlot")}) {
        if (object.contains(key) && !object.value(key).isBool())
            return reject(tr("Invalid '%1': expected a boolean.").arg(key));
    }
    candidate.allowAliasing = object.value(QStringLiteral("allowAliasing")).toBool();
    // WidgetDialRange owns float values. Normalize once, then validate the exact values it will retain.
    const auto normalizeFloat = [&](const char *name, WidgetDialRange *input, double &value) {
        const float converted = static_cast<float>(value);
        const double tolerance = 2 * std::numeric_limits<float>::epsilon()
            * std::max(std::abs(value), double(std::numeric_limits<float>::min()));
        if (!std::isfinite(converted) || (value != 0 && converted == 0) || converted < input->getRangeMin()
            || converted > input->getRangeMax() || std::abs(double(converted) - value) > tolerance) {
            error = tr("Invalid '%1': outside the numeric precision or range of the control.")
                        .arg(QString::fromLatin1(name));
            return false;
        }
        value = converted;
        return true;
    };
    if (!normalizeFloat("startFrequency", m_startFrequency, candidate.startFrequency)
        || !normalizeFloat("stopFrequency", m_stopFrequency, candidate.stopFrequency)
        || !normalizeFloat("startAmplitude", m_startAmplitude, candidate.startAmplitude)
        || !normalizeFloat("stopAmplitude", m_stopAmplitude, candidate.stopAmplitude)
        || !normalizeFloat("offset", m_offset, candidate.offset))
        return reject(error);
    candidate.channelSkewSeconds = 0; // Ignore the removed legacy phase-skew setting, including in v1.
    QVector<FrequencyAnalysis::SweepPoint> points;
    if (!FrequencyAnalysis::makeSweep(candidate, points, error))
        return reject(tr("Invalid frequency analysis sweep: %1").arg(error));

    m_startFrequency->setRealValue(candidate.startFrequency, true);
    m_stopFrequency->setRealValue(candidate.stopFrequency, true);
    m_startAmplitude->setRealValue(candidate.startAmplitude, true);
    m_stopAmplitude->setRealValue(candidate.stopAmplitude, true);
    m_offset->setRealValue(candidate.offset, true);
    m_pointsPerDecade->setRealValue(candidate.pointsPerDecade, true);
    m_averages->setRealValue(candidate.averages, true);
    m_periods->setRealValue(candidate.periods, true);
    m_settlingSeconds->setRealValue(candidate.settlingMs / 1000.0f, true);
    m_frequencySpacing->setChecked(true, candidate.frequencySpacing == FrequencyAnalysis::Spacing::Linear ? 1 : 0);
    m_amplitudeSpacing->setChecked(true, candidate.amplitudeSpacing == FrequencyAnalysis::Spacing::Linear ? 0 : 1);
    m_allowAliasing->setChecked(candidate.allowAliasing, 0);
    m_unwrapPhase->setChecked(object.value(QStringLiteral("unwrapPhase")).toBool(), 0);
    m_showCursors->setChecked(object.value(QStringLiteral("showCursors")).toBool(true), 0);
    m_combinedPlot->setChecked(object.value(QStringLiteral("combinedPlot")).toBool(false), 0);
    updatePlotLayout();
    const bool inputsChanged = m_referenceInput != candidate.referenceInput || m_responseInput != candidate.responseInput;
    m_referenceInput = candidate.referenceInput;
    m_responseInput = candidate.responseInput;
    rebuildInputSelections();
    updateConnections();
    clearResults();
    recalculateEstimate();
    setRunning(false);
    if (m_inputsReady && !m_inputError.isEmpty())
        setStatus(m_inputError, true);
    else
        setStatus(tr("Settings restored. Output is off; press Start sweep when the connections are ready."));
    if (inputsChanged)
        emit inputSelectionChanged();
    return true;
}

void FrequencyAnalysisWindow::setRunning(bool running)
{
    if (running && !m_running) {
        m_runReferencePin = m_inputsReady ? resolvedPin(0, m_referenceInput) : QString();
        m_runResponsePin = m_inputsReady ? resolvedPin(1, m_responseInput) : QString();
        m_runDacPin = m_inputsReady ? m_dacPin : QString();
        m_runPinsCaptured = true;
    }
    m_running = running;
    for (int tab = 0; tab < 3; ++tab)
        m_settingsTabs->getLayout(tab)->parentWidget()->setEnabled(!running);
    m_referenceSelection->setEnabled(m_inputsReady && !running);
    m_responseSelection->setEnabled(m_inputsReady && !running);
    updateActionState();
}

void FrequencyAnalysisWindow::setStatus(const QString &status, bool error)
{
    m_statusError = error;
    m_status->setText(status);
    m_status->setStyleSheet(QStringLiteral("color: %1;")
        .arg(error ? Graphics::palette().error : Graphics::palette().textAll));
}

void FrequencyAnalysisWindow::setWiring(const QString &wiring)
{
    m_wiring->setText(wiring);
}

void FrequencyAnalysisWindow::setAcquisitionLimits(int maxRate, int maxSamples)
{
    maxRate = std::max(0, maxRate);
    maxSamples = std::max(0, maxSamples);
    if (m_maxRate == maxRate && m_maxSamples == maxSamples)
        return;
    m_maxRate = maxRate;
    m_maxSamples = maxSamples;
    if (m_maxRate == 0 || m_maxSamples == 0)
        recalculateEstimate();
    else
        scheduleEstimate();
}

void FrequencyAnalysisWindow::setVoltageLimits(double minimum, double maximum)
{
    const float lower = static_cast<float>(minimum);
    const float upper = static_cast<float>(maximum);
    const float span = static_cast<float>(maximum - minimum);
    if (!std::isfinite(lower) || !std::isfinite(upper) || !std::isfinite(span)
        || lower >= upper || span <= minimumAmplitude) {
        setStatus(tr("The board returned unsupported DAC voltage limits."), true);
        return;
    }
    if (m_offset->getRangeMin() == lower && m_offset->getRangeMax() == upper
        && m_startAmplitude->getRangeMax() == span)
        return;
    const bool adjusted = m_offset->getRealValue() < lower || m_offset->getRealValue() > upper
        || m_startAmplitude->getRealValue() > span || m_stopAmplitude->getRealValue() > span;
    m_offset->updateRange(lower, upper, true);
    m_startAmplitude->updateRange(minimumAmplitude, span, true);
    m_stopAmplitude->updateRange(minimumAmplitude, span, true);
    if (adjusted)
        setStatus(tr("Voltage settings were limited to the board's DAC range. Check amplitude and offset before starting."));
    scheduleEstimate();
}

void FrequencyAnalysisWindow::setTimingParameters(const FrequencyAnalysis::TimingParameters &parameters)
{
    m_timingParameters = parameters;
    scheduleEstimate(false);
}

void FrequencyAnalysisWindow::setInputCapabilities(const QVector<QStringList> &inputs,
                                                   const QVector<int> &defaults,
                                                   const QStringList &defaultPins,
                                                   const QString &dacPin)
{
    m_inputs = inputs;
    m_defaultInputs = defaults;
    m_defaultPins = defaultPins;
    m_dacPin = dacPin.trimmed();
    m_inputsReady = inputs.size() >= 2 && defaults.size() >= 2
        && !inputs.at(0).isEmpty() && !inputs.at(1).isEmpty();
    if (m_inputsReady) {
        for (int slot = 0; slot < 2; ++slot)
            m_inputsReady = m_inputsReady && defaults.at(slot) >= 0
                && defaults.at(slot) < inputs.at(slot).size();
    }
    rebuildInputSelections();
    updateConnections();
    if (m_inputsReady && !m_inputError.isEmpty())
        setStatus(m_inputError, true);
    updateActionState();
}

QString FrequencyAnalysisWindow::resolvedPin(int slot, int selector) const
{
    if (!m_inputsReady) {
        if (selector == -1 && slot < m_defaultPins.size() && AdcInputCatalog::isExternal(m_defaultPins.at(slot)))
            return m_defaultPins.at(slot).trimmed();
        return {};
    }
    const int resolved = selector == -1 ? m_defaultInputs.at(slot) : selector;
    if (resolved < 0 || resolved >= m_inputs.at(slot).size())
        return {};
    const QString name = m_inputs.at(slot).at(resolved).trimmed();
    return AdcInputCatalog::isExternal(name) ? name : QString();
}

void FrequencyAnalysisWindow::rebuildInputSelections()
{
    for (int slot = 0; slot < 2; ++slot) {
        auto *selection = slot == 0 ? m_referenceSelection : m_responseSelection;
        const QSignalBlocker blocker(selection);
        selection->clear();
        auto &values = m_selectorValues[slot];
        values.clear();
        const QString defaultPin = resolvedPin(slot, -1);
        selection->addOption(defaultPin.isEmpty() ? tr("Default (not resolved)")
                                                  : tr("Default (%1)").arg(defaultPin), -1);
        values.append(-1);
        if (slot < m_inputs.size()) {
            const auto &names = m_inputs.at(slot);
            for (int index = 0; index < names.size(); ++index) {
                if (!AdcInputCatalog::isExternal(names.at(index)))
                    continue;
                selection->addOption(names.at(index).trimmed(), index);
                values.append(index);
            }
        }
        const int stored = slot == 0 ? m_referenceInput : m_responseInput;
        int selected = values.indexOf(stored);
        if (selected < 0) {
            selection->addOption(m_inputsReady ? tr("Unavailable selector %1").arg(stored)
                                                : tr("Stored selector %1 (waiting for metadata)").arg(stored), stored);
            values.append(stored);
            selected = values.size() - 1;
        }
        selection->setSelected(selected, true);
        selection->setEnabled(m_inputsReady && !m_running);
    }
}

void FrequencyAnalysisWindow::acceptInputSelection(int slot, int visibleIndex)
{
    if (m_running || !m_inputsReady || visibleIndex < 0 || visibleIndex >= m_selectorValues[slot].size())
        return;
    const int selector = m_selectorValues[slot].at(visibleIndex);
    if (resolvedPin(slot, selector).isEmpty()) {
        setStatus(tr("The selected CH%1 input is unavailable or is an internal ADC source.").arg(slot + 1), true);
        rebuildInputSelections();
        return;
    }
    int &stored = slot == 0 ? m_referenceInput : m_responseInput;
    if (stored == selector)
        return;
    const QString previousError = m_inputError;
    stored = selector;
    rebuildInputSelections();
    updateConnections();
    updateActionState();
    if (!m_inputError.isEmpty())
        setStatus(m_inputError, true);
    else if (!previousError.isEmpty() && m_status->text() == previousError)
        setStatus(tr("Connections updated. Ready to start a sweep."));
    emit inputSelectionChanged();
}

void FrequencyAnalysisWindow::updateConnections()
{
    const QString reference = resolvedPin(0, m_referenceInput);
    const QString response = resolvedPin(1, m_responseInput);
    m_inputError.clear();
    if (!m_inputsReady)
        m_inputError = tr("Waiting for two-channel ADC pin metadata. Start is disabled until discovery completes.");
    else if (reference.isEmpty())
        m_inputError = tr("Stored CH1 reference selector %1 is unavailable or internal. Choose an external pin.")
                           .arg(m_referenceInput);
    else if (response.isEmpty())
        m_inputError = tr("Stored CH2 response selector %1 is unavailable or internal. Choose an external pin.")
                           .arg(m_responseInput);
    m_connectionError->setText(m_inputError);
    m_connectionError->setVisible(!m_inputError.isEmpty());
    m_connectionDiagram->setText(tr(
        "DAC CH1 (%1) \u2192 [ DUT ] \u2192 CH2 (%2)\n"
        "DUT input \u2192 CH1 reference (%3)\n"
        "Common ground: board GND \u2194 DUT GND")
        .arg(m_dacPin.isEmpty() ? tr("pin pending") : m_dacPin,
             response.isEmpty() ? tr("unresolved") : response,
             reference.isEmpty() ? tr("unresolved") : reference));
}

void FrequencyAnalysisWindow::updateActionState()
{
    const bool canStart = !m_running && m_inputsReady && m_inputError.isEmpty() && m_estimateValid;
    m_start->setEnabled(canStart);
    m_start->setEnabledButton(canStart, 0);
    m_cancel->setEnabled(m_running);
    m_cancel->setEnabledButton(m_running, 0);
    m_export->setEnabled(!m_results.isEmpty());
    m_export->setEnabledButton(!m_results.isEmpty(), 0);
}

void FrequencyAnalysisWindow::scheduleEstimate(bool invalidate)
{
    if (invalidate) {
        m_estimateValid = false;
        m_timingState = {{QStringLiteral("available"), false}, {QStringLiteral("pending"), true}};
    }
    // Keep the last estimate and warning visible while learned timing is refreshed.
    m_estimateTimer->start();
    updateActionState();
}

void FrequencyAnalysisWindow::recalculateEstimate()
{
    m_estimateTimer->stop();
    m_estimateValid = false;
    if (m_maxRate <= 0 || m_maxSamples <= 0) {
        m_timingLabel->setText(tr("Acquisition estimate unavailable until board rate and buffer limits are known."));
        m_timingState = {{QStringLiteral("available"), false},
                        {QStringLiteral("error"), tr("Board acquisition limits unavailable.")}};
        updateActionState();
        return;
    }
    FrequencyAnalysis::TimingEstimate estimate;
    QString error;
    const QByteArray geometryKey = QJsonDocument(settingsJson()).toJson(QJsonDocument::Compact)
        + QByteArray::number(m_maxRate) + ":" + QByteArray::number(m_maxSamples) + ":"
        + QByteArray::number(m_timingParameters.generatorBufferBytes) + ":"
        + QByteArray::number(m_timingParameters.generatorMaximumRate) + ":"
        + QByteArray::number(m_timingParameters.generatorClock);
    if (geometryKey != m_timingGeometryKey) {
        m_timingGeometryKey = geometryKey;
        m_timingWaveformLengths.clear();
    }
    if (!FrequencyAnalysis::estimateSweep(settings(), m_maxRate, m_maxSamples, estimate, error,
                                          m_timingParameters, &m_timingWaveformLengths)) {
        m_timingLabel->setText(tr("Acquisition estimate unavailable: %1").arg(error));
        m_timingState = {{QStringLiteral("available"), false}, {QStringLiteral("error"), error}};
        updateActionState();
        return;
    }
    m_estimateValid = true;
    m_timingLabel->setText(tr(
        "Lowest point (%1 Hz): ~%2 s\n"
        "Total sweep: ~%3 s\n")
        .arg(concise(settings().startFrequency), concise(estimate.lowestFrequencyMaximumSeconds),
             concise(estimate.totalMaximumSeconds)));
    m_timingLabel->setToolTip(tr(
        "Uses the fast-auto trigger timeout budget, averaging and settling. Includes generator "
        "upload and capture transfer at the reported UART rate, when applicable. USB latency, "
        "processing and device round-trip overhead are refined from completed sweep points."));
    m_timingState = {
        {QStringLiteral("available"), true},
        {QStringLiteral("lowest_frequency_seconds"), estimate.lowestFrequencySeconds},
        {QStringLiteral("lowest_frequency_maximum_seconds"), estimate.lowestFrequencyMaximumSeconds},
        {QStringLiteral("total_seconds"), estimate.totalSeconds},
        {QStringLiteral("total_maximum_seconds"), estimate.totalMaximumSeconds},
        {QStringLiteral("lowest_rate_hz"), estimate.lowestRate},
        {QStringLiteral("lowest_samples"), estimate.lowestSamples},
        {QStringLiteral("lowest_samples_per_period"), estimate.lowestSamplesPerPeriod},
        {QStringLiteral("density_limited"), estimate.densityLimited},
        {QStringLiteral("learned_overhead"), m_timingParameters.learned},
        {QStringLiteral("reported_transfer_bytes_per_second"), m_timingParameters.transferBytesPerSecond}
    };
    updateActionState();
}

void FrequencyAnalysisWindow::createFrequencyAxes()
{
    const auto create = [&](QChart *chart, QAbstractAxis *&axis) {
        if (axis) {
            chart->removeAxis(axis);
            delete axis;
        }
        if (m_logFrequency) {
            auto *log = new QLogValueAxis(chart);
            log->setBase(10);
            log->setMinorTickCount(8);
            log->setLabelFormat(QStringLiteral("%.4g"));
            axis = log;
        } else {
            auto *linear = new QValueAxis(chart);
            linear->setMinorTickCount(4);
            linear->setLabelFormat(QStringLiteral("%.4g"));
            axis = linear;
        }
        axis->setTitleText(tr("Actual generator frequency (Hz)"));
        styleAxis(axis);
        chart->addAxis(axis, Qt::AlignBottom);
    };
    create(m_gainChart, m_gainFrequencyAxis);
    create(m_phaseChart, m_phaseFrequencyAxis);
    m_gainSeries->attachAxis(m_gainFrequencyAxis);
    m_phaseSeries->attachAxis(m_combinedPlot->isChecked(0) ? m_gainFrequencyAxis : m_phaseFrequencyAxis);
}

void FrequencyAnalysisWindow::clearResults()
{
    m_gainView->setCursors(false, 0, 0);
    m_phaseView->setCursors(false, 0, 0);
    m_results.clear();
    if (!m_running) {
        m_runPinsCaptured = false;
        m_runReferencePin.clear();
        m_runResponsePin.clear();
        m_runDacPin.clear();
    }
    m_gainSeries->clear();
    m_phaseSeries->clear();
    m_table->setRowCount(0);
    m_cursorFrequency[0] = m_cursorFrequency[1] = 0;
    m_cursorBTracksEnd = true;
    m_logFrequency = settings().frequencySpacing == FrequencyAnalysis::Spacing::Logarithmic;
    createFrequencyAxes();
    m_resultCount->setText(tr("0 measured points"));
    updatePlotRanges();
    updateActionState();
}

void FrequencyAnalysisWindow::appendResult(double requestedFrequency, double actualFrequency,
                                           double programmedAmplitude, int actualRate, int samples,
                                           const FrequencyAnalysis::Result &result)
{
    for (double value : {requestedFrequency, actualFrequency, programmedAmplitude,
                         result.gainDb, result.phaseDegrees, result.referenceRms,
                         result.responseRms, result.coherence}) {
        if (!std::isfinite(value)) {
            setStatus(tr("Rejected a non-finite frequency analysis result."), true);
            return;
        }
    }
    if (requestedFrequency <= 0 || actualFrequency <= 0 || programmedAmplitude <= 0
        || actualRate <= 0 || samples <= 0 || result.referenceRms < 0
        || result.responseRms < 0 || result.coherence < 0 || result.coherence > 1) {
        setStatus(tr("Rejected an out-of-range frequency analysis result."), true);
        return;
    }
    auto wrapped = result;
    wrapped.phaseDegrees = std::remainder(result.phaseDegrees, 360.0);
    double unwrapped = wrapped.phaseDegrees;
    if (!m_results.isEmpty()) {
        const auto &previous = m_results.constLast();
        unwrapped = previous.unwrappedPhase
            + std::remainder(wrapped.phaseDegrees - previous.result.phaseDegrees, 360.0);
    }
    const QString referencePin = m_runPinsCaptured ? m_runReferencePin
        : (m_inputsReady ? resolvedPin(0, m_referenceInput) : QString());
    const QString responsePin = m_runPinsCaptured ? m_runResponsePin
        : (m_inputsReady ? resolvedPin(1, m_responseInput) : QString());
    const QString dacPin = m_runPinsCaptured ? m_runDacPin : (m_inputsReady ? m_dacPin : QString());
    m_results.append({requestedFrequency, actualFrequency, programmedAmplitude,
                      actualRate, samples, wrapped, unwrapped, referencePin, responsePin, dacPin});
    m_gainSeries->append(actualFrequency, wrapped.gainDb);
    m_phaseSeries->append(actualFrequency, m_unwrapPhase->isChecked(0) ? unwrapped : wrapped.phaseDegrees);
    if (m_cursorFrequency[0] <= 0)
        m_cursorFrequency[0] = actualFrequency;
    if (m_cursorBTracksEnd)
        m_cursorFrequency[1] = actualFrequency;
    const int row = m_table->rowCount();
    m_table->insertRow(row);
    const QStringList values = {
        number(requestedFrequency), number(actualFrequency), number(programmedAmplitude),
        QString::number(actualRate), QString::number(samples), number(wrapped.gainDb),
        number(wrapped.phaseDegrees), number(unwrapped), number(wrapped.referenceRms),
        number(wrapped.responseRms), number(wrapped.coherence),
        referencePin, responsePin, dacPin,
        number(samples * std::abs(std::remainder(actualFrequency, double(actualRate))) / actualRate)};
    for (int column = 0; column < values.size(); ++column) {
        auto *item = new QTableWidgetItem(values.at(column));
        item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_table->setItem(row, column, item);
    }
    m_resultCount->setText(tr("%1 measured points").arg(m_results.size()));
    updatePlotRanges();
    updateActionState();
}

void FrequencyAnalysisWindow::updatePhasePlot()
{
    m_phaseSeries->clear();
    for (const auto &measurement : m_results)
        m_phaseSeries->append(measurement.actualFrequency, m_unwrapPhase->isChecked(0)
            ? measurement.unwrappedPhase : measurement.result.phaseDegrees);
    updatePlotRanges();
}

void FrequencyAnalysisWindow::updatePlotLayout()
{
    const bool combined = m_combinedPlot->isChecked(0);
    QChart *destination = combined ? m_gainChart : m_phaseChart;
    QChart *source = m_phaseSeries->chart();
    m_gainView->setCursors(false, 0, 0);
    m_phaseView->setCursors(false, 0, 0);
    if (source != destination) {
        source->removeSeries(m_phaseSeries);
        source->removeAxis(m_phaseAxis);
        destination->addSeries(m_phaseSeries);
        destination->addAxis(m_phaseAxis, combined ? Qt::AlignRight : Qt::AlignLeft);
        m_phaseSeries->attachAxis(m_phaseAxis);
        m_phaseSeries->attachAxis(combined ? m_gainFrequencyAxis : m_phaseFrequencyAxis);
    }
    m_phaseSeries->setPen(QPen(QColor(Graphics::getChannelColor(0)), 2,
                               combined ? Qt::DashLine : Qt::SolidLine));
    m_gainChart->setTitle(combined ? tr("Gain and relative phase: CH2 / CH1") : tr("Gain: CH2 / CH1"));
    m_gainChart->legend()->setVisible(combined);
    m_gainChart->legend()->setLabelColor(QColor(Graphics::palette().textAll));
    m_phaseView->setVisible(!combined);
    updatePlotRanges();
}

void FrequencyAnalysisWindow::updatePlotRanges()
{
    double minimum = std::min(m_startFrequency->getRealValue(), m_stopFrequency->getRealValue());
    double maximum = std::max(m_startFrequency->getRealValue(), m_stopFrequency->getRealValue());
    double gainMinimum = 0, gainMaximum = 0, phaseMinimum = 0, phaseMaximum = 0;
    if (!m_results.isEmpty()) {
        minimum = maximum = m_results.constFirst().actualFrequency;
        gainMinimum = gainMaximum = m_results.constFirst().result.gainDb;
        phaseMinimum = phaseMaximum = m_unwrapPhase->isChecked(0)
            ? m_results.constFirst().unwrappedPhase : m_results.constFirst().result.phaseDegrees;
        for (const auto &measurement : m_results) {
            minimum = std::min(minimum, measurement.actualFrequency);
            maximum = std::max(maximum, measurement.actualFrequency);
            gainMinimum = std::min(gainMinimum, measurement.result.gainDb);
            gainMaximum = std::max(gainMaximum, measurement.result.gainDb);
            const double phase = m_unwrapPhase->isChecked(0)
                ? measurement.unwrappedPhase : measurement.result.phaseDegrees;
            phaseMinimum = std::min(phaseMinimum, phase);
            phaseMaximum = std::max(phaseMaximum, phase);
        }
    }
    if (m_logFrequency) {
        const double factor = minimum == maximum ? 1.2 : 1.05;
        minimum /= factor;
        maximum *= factor;
    } else {
        const double padding = std::max((maximum - minimum) * 0.05, maximum * 0.05);
        minimum = std::max(0.0, minimum - padding);
        maximum += padding;
    }
    setFrequencyRange(m_gainFrequencyAxis, minimum, maximum);
    setFrequencyRange(m_phaseFrequencyAxis, minimum, maximum);
    if (m_results.isEmpty()) {
        m_gainAxis->setRange(-20, 20);
        m_phaseAxis->setRange(-180, 180);
    } else {
        const double gainPadding = std::max(1.0, (gainMaximum - gainMinimum) * 0.1);
        const double phasePadding = std::max(10.0, (phaseMaximum - phaseMinimum) * 0.1);
        m_gainAxis->setRange(gainMinimum - gainPadding, gainMaximum + gainPadding);
        m_phaseAxis->setRange(phaseMinimum - phasePadding, phaseMaximum + phasePadding);
    }
    m_phaseAxis->setTitleText(m_unwrapPhase->isChecked(0)
        ? tr("Unwrapped phase (\u00b0)") : tr("Wrapped phase (\u00b0)"));
    updateCursors();
}

void FrequencyAnalysisWindow::moveCursor(int cursor, double frequency)
{
    if (cursor < 0 || cursor > 1 || m_results.isEmpty() || !std::isfinite(frequency) || frequency <= 0)
        return;
    m_cursorFrequency[cursor] = frequency;
    if (const auto reading = cursorReading(cursor))
        m_cursorFrequency[cursor] = reading->frequency;
    if (cursor == 1)
        m_cursorBTracksEnd = false;
    updateCursors();
}

std::optional<FrequencyAnalysisWindow::CursorReading> FrequencyAnalysisWindow::cursorReading(int cursor) const
{
    if (cursor < 0 || cursor > 1 || m_results.isEmpty() || m_cursorFrequency[cursor] <= 0)
        return std::nullopt;
    const double frequency = m_cursorFrequency[cursor];
    int lower = -1, upper = -1;
    for (int i = 0; i < m_results.size(); ++i) {
        const double f = m_results[i].actualFrequency;
        if (f <= frequency && (lower < 0 || f >= m_results[lower].actualFrequency))
            lower = i;
        if (f >= frequency && (upper < 0 || f < m_results[upper].actualFrequency))
            upper = i;
    }
    if (lower < 0) lower = upper;
    if (upper < 0) upper = lower;
    const auto &a = m_results[lower];
    const auto &b = m_results[upper];
    const double f = std::clamp(frequency, a.actualFrequency, b.actualFrequency);
    const double fraction = a.actualFrequency == b.actualFrequency ? 0
        : (m_logFrequency ? std::log(f / a.actualFrequency) / std::log(b.actualFrequency / a.actualFrequency)
                          : (f - a.actualFrequency) / (b.actualFrequency - a.actualFrequency));
    const double phaseA = m_unwrapPhase->isChecked(0) ? a.unwrappedPhase : a.result.phaseDegrees;
    const double phaseB = m_unwrapPhase->isChecked(0) ? b.unwrappedPhase : b.result.phaseDegrees;
    return CursorReading{f, a.result.gainDb + fraction * (b.result.gainDb - a.result.gainDb),
                         phaseA + fraction * (phaseB - phaseA), lower, upper, fraction};
}

void FrequencyAnalysisWindow::updateCursors()
{
    const auto readingA = cursorReading(0), readingB = cursorReading(1);
    const bool havePoints = readingA && readingB;
    const bool enabled = m_showCursors->isChecked(0);
    if (!havePoints) {
        m_gainView->setCursors(false, 0, 0);
        m_phaseView->setCursors(false, 0, 0);
        m_cursorAReadout->setText(tr("A: no measured points"));
        m_cursorBReadout->setText(tr("B: no measured points"));
        m_cursorDifference->setText(tr("Drag A/B in either plot."));
        return;
    }
    const auto &a = *readingA;
    const auto &b = *readingB;
    m_gainView->setCursors(enabled, a.frequency, b.frequency);
    m_phaseView->setCursors(enabled, a.frequency, b.frequency);
    m_cursorAReadout->setText(tr("A: %1 Hz | %2 dB | %3\u00b0")
        .arg(concise(a.frequency), concise(a.gain), concise(a.phase)));
    m_cursorBReadout->setText(tr("B: %1 Hz | %2 dB | %3\u00b0")
        .arg(concise(b.frequency), concise(b.gain), concise(b.phase)));
    m_cursorDifference->setText(tr("B \u2212 A: %1 Hz | %2 dB | %3\u00b0")
        .arg(concise(b.frequency - a.frequency), concise(b.gain - a.gain), concise(b.phase - a.phase)));
}

QJsonObject FrequencyAnalysisWindow::cursorsJson() const
{
    QJsonObject result{{QStringLiteral("enabled"), m_showCursors->isChecked(0)},
                       {QStringLiteral("snapped_to_measurements"), false},
                       {QStringLiteral("interpolation"), m_logFrequency ? "logarithmic" : "linear"}};
    const auto aReading = cursorReading(0), bReading = cursorReading(1);
    if (!aReading || !bReading)
        return result;
    const auto cursor = [&](const CursorReading &reading) {
        return QJsonObject{
            {QStringLiteral("index"), reading.lower},
            {QStringLiteral("upper_index"), reading.upper},
            {QStringLiteral("interpolation_fraction"), reading.fraction},
            {QStringLiteral("frequency_hz"), reading.frequency},
            {QStringLiteral("gain_db"), reading.gain},
            {QStringLiteral("phase_degrees"), reading.phase}
        };
    };
    const auto a = cursor(*aReading);
    const auto b = cursor(*bReading);
    result.insert(QStringLiteral("a"), a);
    result.insert(QStringLiteral("b"), b);
    result.insert(QStringLiteral("delta"), QJsonObject{
        {QStringLiteral("frequency_hz"), b.value(QStringLiteral("frequency_hz")).toDouble()
            - a.value(QStringLiteral("frequency_hz")).toDouble()},
        {QStringLiteral("gain_db"), b.value(QStringLiteral("gain_db")).toDouble()
            - a.value(QStringLiteral("gain_db")).toDouble()},
        {QStringLiteral("phase_degrees"), b.value(QStringLiteral("phase_degrees")).toDouble()
            - a.value(QStringLiteral("phase_degrees")).toDouble()}
    });
    return result;
}

QJsonObject FrequencyAnalysisWindow::state() const
{
    QJsonArray results;
    for (const auto &measurement : m_results) {
        results.append(QJsonObject{
            {QStringLiteral("requested_frequency_hz"), measurement.requestedFrequency},
            {QStringLiteral("actual_frequency_hz"), measurement.actualFrequency},
            {QStringLiteral("drive_vpp"), measurement.programmedAmplitude},
            {QStringLiteral("sampling_rate_hz"), measurement.actualRate},
            {QStringLiteral("samples"), measurement.samples},
            {QStringLiteral("gain_db"), measurement.result.gainDb},
            {QStringLiteral("phase_degrees"), measurement.result.phaseDegrees},
            {QStringLiteral("phase_unwrapped_degrees"), measurement.unwrappedPhase},
            {QStringLiteral("reference_rms"), measurement.result.referenceRms},
            {QStringLiteral("response_rms"), measurement.result.responseRms},
            {QStringLiteral("coherence"), measurement.result.coherence},
            {QStringLiteral("reference_pin"), measurement.referencePin},
            {QStringLiteral("response_pin"), measurement.responsePin},
            {QStringLiteral("dac_pin"), measurement.dacPin},
            {QStringLiteral("observed_cycles"), measurement.samples
                * std::abs(std::remainder(measurement.actualFrequency, double(measurement.actualRate)))
                / measurement.actualRate}
        });
    }
    return {
        {QStringLiteral("version"), 3},
        {QStringLiteral("settings"), settingsJson()},
        {QStringLiteral("running"), m_running},
        {QStringLiteral("status"), m_status->text()},
        {QStringLiteral("status_error"), m_statusError},
        {QStringLiteral("wiring"), m_wiring->text()},
        {QStringLiteral("inputs_ready"), m_inputsReady},
        {QStringLiteral("input_error"), m_inputError},
        {QStringLiteral("reference_pin"), resolvedPin(0, m_referenceInput)},
        {QStringLiteral("response_pin"), resolvedPin(1, m_responseInput)},
        {QStringLiteral("dac_pin"), m_dacPin},
        {QStringLiteral("timing_estimate"), m_timingState},
        {QStringLiteral("cursors"), cursorsJson()},
        {QStringLiteral("result_count"), m_results.size()},
        {QStringLiteral("results"), results}
    };
}

void FrequencyAnalysisWindow::exportCsv()
{
    if (m_results.isEmpty())
        return;
    QString path = QFileDialog::getSaveFileName(this, tr("Export frequency analysis"),
                                               QStringLiteral("frequency-analysis.csv"),
                                               tr("CSV files (*.csv)"));
    if (path.isEmpty())
        return;
    if (QFileInfo(path).suffix().isEmpty())
        path += QStringLiteral(".csv");
    QByteArray csv("requested_frequency_hz,actual_frequency_hz,drive_vpp,actual_sample_rate_hz,"
                   "samples,gain_db,phase_wrapped_deg,phase_unwrapped_deg,reference_rms_v,"
                   "response_rms_v,coherence,reference_pin,response_pin,dac_pin,observed_cycles\n");
    for (const auto &measurement : m_results) {
        csv += QStringList{
            number(measurement.requestedFrequency), number(measurement.actualFrequency),
            number(measurement.programmedAmplitude), QString::number(measurement.actualRate),
            QString::number(measurement.samples), number(measurement.result.gainDb),
            number(measurement.result.phaseDegrees), number(measurement.unwrappedPhase),
            number(measurement.result.referenceRms), number(measurement.result.responseRms),
            number(measurement.result.coherence), csvField(measurement.referencePin),
            csvField(measurement.responsePin), csvField(measurement.dacPin),
            number(measurement.samples * std::abs(std::remainder(measurement.actualFrequency,
                                                                 double(measurement.actualRate)))
                   / measurement.actualRate)
        }.join(QLatin1Char(',')).toUtf8();
        csv += '\n';
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::critical(this, tr("CSV export failed"),
                             tr("Cannot open '%1': %2").arg(path, file.errorString()));
        return;
    }
    if (file.write(csv) != csv.size()) {
        const QString error = file.errorString();
        file.cancelWriting();
        QMessageBox::critical(this, tr("CSV export failed"),
                             tr("Cannot write '%1': %2").arg(path, error));
        return;
    }
    if (!file.commit()) {
        QMessageBox::critical(this, tr("CSV export failed"),
                             tr("Cannot save '%1': %2").arg(path, file.errorString()));
        return;
    }
    setStatus(tr("Exported %1 measured points to %2.").arg(m_results.size()).arg(path));
}
