#ifndef FREQUENCYANALYSISWINDOW_H
#define FREQUENCYANALYSISWINDOW_H

#include "frequencyanalysisengine.h"

#include <QByteArray>
#include <QJsonObject>
#include <QStringList>
#include <QVector>
#include <QWidget>
#include <optional>

class QAbstractAxis;
class QChart;
class QLabel;
class QLineSeries;
class QTableWidget;
class QTimer;
class QValueAxis;
class WidgetButtons;
class WidgetDialRange;
class WidgetSelection;
class widgetTab;
class FrequencyAnalysisChartView;

class FrequencyAnalysisWindow : public QWidget
{
    Q_OBJECT

public:
    explicit FrequencyAnalysisWindow(QWidget *parent = nullptr);

    FrequencyAnalysis::Settings settings() const;
    QByteArray serialize() const;
    bool restore(const QByteArray &data, QString &error);
    void setRunning(bool running);
    void setStatus(const QString &status, bool error = false);
    void setWiring(const QString &wiring);
    void setAcquisitionLimits(int maxRate, int maxSamples);
    void setVoltageLimits(double minimum, double maximum);
    void setTimingParameters(const FrequencyAnalysis::TimingParameters &parameters);
    void setInputCapabilities(const QVector<QStringList> &inputs, const QVector<int> &defaults,
                              const QStringList &defaultPins, const QString &dacPin);
    void clearResults();
    void appendResult(double requestedFrequency, double actualFrequency,
                      double programmedAmplitude, int actualRate, int samples,
                      const FrequencyAnalysis::Result &result);
    QJsonObject state() const;

signals:
    void startRequested();
    void cancelRequested();
    void inputSelectionChanged();

private:
    struct Measurement {
        double requestedFrequency;
        double actualFrequency;
        double programmedAmplitude;
        int actualRate;
        int samples;
        FrequencyAnalysis::Result result;
        double unwrappedPhase;
        QString referencePin;
        QString responsePin;
        QString dacPin;
    };
    struct CursorReading {
        double frequency, gain, phase;
        int lower, upper;
        double fraction;
    };

    WidgetDialRange *m_startFrequency;
    WidgetDialRange *m_stopFrequency;
    WidgetDialRange *m_startAmplitude;
    WidgetDialRange *m_stopAmplitude;
    WidgetDialRange *m_offset;
    WidgetDialRange *m_pointsPerDecade;
    WidgetDialRange *m_averages;
    WidgetDialRange *m_periods;
    WidgetDialRange *m_settlingSeconds;
    WidgetButtons *m_frequencySpacing;
    WidgetButtons *m_amplitudeSpacing;
    WidgetButtons *m_allowAliasing;
    WidgetButtons *m_unwrapPhase;
    WidgetButtons *m_showCursors;
    WidgetButtons *m_combinedPlot;
    widgetTab *m_settingsTabs;
    WidgetSelection *m_referenceSelection;
    WidgetSelection *m_responseSelection;
    QLabel *m_wiring;
    QLabel *m_connectionDiagram;
    QLabel *m_connectionError;
    QLabel *m_timingLabel;
    QLabel *m_status;
    QLabel *m_resultCount;
    QLabel *m_cursorAReadout;
    QLabel *m_cursorBReadout;
    QLabel *m_cursorDifference;
    WidgetButtons *m_start;
    WidgetButtons *m_cancel;
    WidgetButtons *m_export;
    QChart *m_gainChart;
    QChart *m_phaseChart;
    FrequencyAnalysisChartView *m_gainView;
    FrequencyAnalysisChartView *m_phaseView;
    QLineSeries *m_gainSeries;
    QLineSeries *m_phaseSeries;
    QAbstractAxis *m_gainFrequencyAxis = nullptr;
    QAbstractAxis *m_phaseFrequencyAxis = nullptr;
    QValueAxis *m_gainAxis;
    QValueAxis *m_phaseAxis;
    QTableWidget *m_table;
    QVector<Measurement> m_results;
    QTimer *m_estimateTimer;
    QJsonObject m_timingState;
    FrequencyAnalysis::TimingParameters m_timingParameters;
    QByteArray m_timingGeometryKey;
    QVector<int> m_timingWaveformLengths;
    QVector<QStringList> m_inputs;
    QVector<int> m_defaultInputs;
    QStringList m_defaultPins;
    QString m_dacPin;
    QString m_runReferencePin;
    QString m_runResponsePin;
    QString m_runDacPin;
    QVector<int> m_selectorValues[2];
    QString m_inputError;
    int m_referenceInput = -1;
    int m_responseInput = -1;
    int m_maxRate = 0;
    int m_maxSamples = 0;
    double m_cursorFrequency[2] = {0, 0};
    bool m_running = false;
    bool m_statusError = false;
    bool m_logFrequency = true;
    bool m_inputsReady = false;
    bool m_estimateValid = false;
    bool m_cursorBTracksEnd = true;
    bool m_runPinsCaptured = false;

    QJsonObject settingsJson() const;
    QJsonObject cursorsJson() const;
    std::optional<CursorReading> cursorReading(int cursor) const;
    QString resolvedPin(int slot, int selector) const;
    void rebuildInputSelections();
    void acceptInputSelection(int slot, int visibleIndex);
    void updateConnections();
    void updateActionState();
    void scheduleEstimate(bool invalidate = true);
    void recalculateEstimate();
    void createFrequencyAxes();
    void updatePlotRanges();
    void updatePhasePlot();
    void updatePlotLayout();
    void moveCursor(int cursor, double frequency);
    void updateCursors();
    void exportCsv();
};

#endif // FREQUENCYANALYSISWINDOW_H
