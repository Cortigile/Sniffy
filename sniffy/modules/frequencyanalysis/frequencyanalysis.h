#ifndef FREQUENCYANALYSIS_H
#define FREQUENCYANALYSIS_H

#include "modules/abstractmodule.h"
#include "frequencyanalysisengine.h"
#include "modules/adcinputcatalog.h"
#include "modules/resourcemanager.h"
#include <QTimer>
#include <QElapsedTimer>
#include <functional>
#include <memory>

class ScopeSpec;
class ArbGeneratorSpec;
class FrequencyAnalysisWindow;
class FrequencyAnalysisTest;
class GenCommons;

class FrequencyAnalysisModule : public AbstractModule
{
    Q_OBJECT
public:
    FrequencyAnalysisModule(AbstractModule *scope, AbstractModule *generator, QObject *parent = nullptr);
    ~FrequencyAnalysisModule() override;

    void setComms(Comms *communication) override;
    void refreshAvailability();
    void parseInstrumentData(const QByteArray &prefix, const QByteArray &data);
    void instrumentError(const QString &message);
    bool isRunning() const;
    void setResourceCheck(std::function<QString(const ResourceSet &)> check);
    void setTransferRate(double bytesPerSecond);

    void parseData(QByteArray data) override;
    void startModule() override;
    void stopModule() override;
    void writeConfiguration() override;
    void parseConfiguration(QByteArray data) override;
    QByteArray getConfiguration() override;
    QWidget *getWidget() override;

public slots:
    void startSweep();
    void cancelSweep();

private:
    friend class FrequencyAnalysisTest;
    enum class State { Idle, Barrier, Uploading, Frequency, Starting, Settling, Capturing, Draining, Fault };
    State state = State::Idle;
    AbstractModule *scope;
    AbstractModule *generator;
    ScopeSpec *scopeSpec = nullptr;
    ArbGeneratorSpec *generatorSpec = nullptr;
    FrequencyAnalysisWindow *window;
    QTimer watchdog;
    QTimer settling;
    QTimer inputTimeout;
    AdcInputCatalog inputCatalog;
    std::function<QString(const ResourceSet &)> resourceCheck;
    int referenceSelector = -1;
    int responseSelector = -1;
    FrequencyAnalysis::Settings settings;
    QVector<FrequencyAnalysis::SweepPoint> points;
    FrequencyAnalysis::SamplingPlan sampling;
    FrequencyAnalysis::Averager averaging;
    int pointIndex = 0;
    int waveformLength = 0;
    int frequencyRequests = 0;
    int rateAttempts = 0;
    int actualRate = 0;
    double actualFrequency = 0;
    quint32 generatorRate = 0;
    QList<QList<int>> waveform;
    std::unique_ptr<GenCommons> generatorTransport;
    QList<PinFunctionInfo> scopePins;
    QList<PinFunctionInfo> generatorPins;
    QString drainMessage;
    bool drainError = false;
    bool captureStarted = false;
    bool frequencySeen = false;
    bool lowCoherence = false;
    bool periodTargetLimited = false;
    FrequencyAnalysis::TimingParameters timing;
    QElapsedTimer pointClock, uploadClock, captureClock, settlingClock;
    double triggerMilliseconds = -1;
    double pointCaptureSeconds = 0, lastUploadSeconds = 0, lastSettlingSeconds = 0;
    double uploadSecondsTotal = 0, downloadSecondsTotal = 0;
    double captureOverheadTotal = 0, pointOverheadTotal = 0;
    qint64 uploadBlocksTotal = 0, downloadBytesTotal = 0;
    int timedCaptures = 0, timedPoints = 0;

    void publishTiming();
    void recordCaptureTiming(int rate);
    void refreshConnections();
    bool resolveInputs(const FrequencyAnalysis::Settings &, int &reference, int &response, QString &error) const;
    bool updateInputResources(QString &error);
    void send(const QByteArray &prefix, const QByteArray &command);
    void sendValue(const QByteArray &prefix, const QByteArray &command, quint32 value);
    void requestFrequency();
    void beginPoint();
    bool prepareWaveform(QString &error);
    void uploadNext();
    void configureCapture();
    void requestCapture(bool first);
    void acceptCapture(const QByteArray &data);
    void finish(const QString &message, bool error);
    void stopHardware();
    void armWatchdog(int milliseconds = 5000);
    bool decodeCapture(const QByteArray &data, QVector<double> &reference,
                       QVector<double> &response, int &rate, double &adcStep, QString &error) const;
};

#endif // FREQUENCYANALYSIS_H
