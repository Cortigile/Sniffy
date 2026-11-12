#include "frequencyanalysis.h"
#include "frequencyanalysiswindow.h"
#include "modules/arbgenerator/arbgenerator.h"
#include "modules/arbgenerator/gencommons.h"
#include "modules/arbgenerator/signalcreator.h"
#include "modules/scope/scope.h"
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <QRegularExpression>

namespace {
class AnalysisSpecification : public AbstractSpecification
{
public:
    explicit AnalysisSpecification(QObject *parent) : AbstractSpecification(parent) {}
    void parseSpecification(QByteArray) override
    {
        qWarning() << "Frequency analysis uses the scope and DAC specifications, not a firmware module.";
    }
    void combine(AbstractSpecification *adc, AbstractSpecification *dac, const ResourceSet &extra = {})
    {
        resources = adc->getResources() | dac->getResources();
        setGpioMasks(adc->getGpioMaskA() | dac->getGpioMaskA() | extra.gpioA,
                     adc->getGpioMaskB() | dac->getGpioMaskB() | extra.gpioB,
                     adc->getGpioMaskC() | dac->getGpioMaskC() | extra.gpioC,
                     adc->getGpioMaskD() | dac->getGpioMaskD() | extra.gpioD);
        isSpecificationLoaded = true;
    }
};
}

FrequencyAnalysisModule::FrequencyAnalysisModule(AbstractModule *adc, AbstractModule *dac, QObject *parent)
    : AbstractModule(parent), scope(adc), generator(dac), window(new FrequencyAnalysisWindow)
{
    comm = nullptr;
    cmd = nullptr;
    moduleName = QStringLiteral("Frequency analysis");
    moduleCommandPrefix = "FREQ";
    moduleIconURI = Graphics::getCommonPath() + "icon_freq_anlys.png";
    moduleSpecification = new AnalysisSpecification(this);
    window->setObjectName(QStringLiteral("frequencyAnalysisWindow"));
    watchdog.setSingleShot(true);
    settling.setSingleShot(true);
    inputTimeout.setSingleShot(true);
    connect(window, &FrequencyAnalysisWindow::startRequested, this, &FrequencyAnalysisModule::startSweep);
    connect(window, &FrequencyAnalysisWindow::cancelRequested, this, &FrequencyAnalysisModule::cancelSweep);
    connect(window, &FrequencyAnalysisWindow::inputSelectionChanged, this, [this] {
        if (state != State::Idle || inputCatalog.inputs.isEmpty())
            return;
        QString error;
        if (!updateInputResources(error)) {
            window->setStatus(error, true);
            qWarning() << "Frequency analysis connections:" << error;
        } else {
            window->setStatus(tr("Connections updated. Ready to start a sweep."));
        }
    });
    connect(&inputTimeout, &QTimer::timeout, this, [this] {
        window->setStatus(tr("ADC pin discovery timed out. Reconnect the board to retry."), true);
        qWarning() << "Frequency analysis ADC input catalog timed out.";
    });
    connect(scope, &AbstractModule::modulePinFunctions, this,
            [this](const QString &, const QList<PinFunctionInfo> &pins) { scopePins = pins; });
    connect(generator, &AbstractModule::modulePinFunctions, this,
            [this](const QString &, const QList<PinFunctionInfo> &pins) { generatorPins = pins; });
    connect(&settling, &QTimer::timeout, this, [this] {
        if (state == State::Settling)
            configureCapture();
    });
    connect(&watchdog, &QTimer::timeout, this, [this] {
        stopHardware();
        settling.stop();
        inputTimeout.stop();
        state = State::Fault;
        window->setRunning(false);
        window->setStatus(tr("Instrument timeout. Reconnect the board before starting another sweep."), true);
        qWarning() << "Frequency analysis timed out; reconnect required to discard unacknowledged commands.";
    });
}

FrequencyAnalysisModule::~FrequencyAnalysisModule()
{
    watchdog.stop();
    settling.stop();
    inputTimeout.stop();
    if (state != State::Idle && comm)
        stopHardware();
    delete window;
}

void FrequencyAnalysisModule::setComms(Comms *communication)
{
    disableModule();
    watchdog.stop();
    settling.stop();
    inputTimeout.stop();
    comm = communication;
    state = State::Idle;
    frequencyRequests = 0;
    frequencySeen = false;
    generatorTransport.reset();
    inputCatalog = {};
    uploadSecondsTotal = downloadSecondsTotal = captureOverheadTotal = pointOverheadTotal = 0;
    uploadBlocksTotal = downloadBytesTotal = 0;
    timedCaptures = timedPoints = 0;
    timing.uploadSecondsPerBlock = timing.downloadSecondsPerByte = 0;
    timing.captureOverheadSeconds = timing.pointOverheadSeconds = 0;
    timing.learned = false;
    pointClock.invalidate();
    uploadClock.invalidate();
    captureClock.invalidate();
    settlingClock.invalidate();
    window->setRunning(false);
    if (comm) {
        inputTimeout.start(3000);
        send(Commands::SCOPE, Commands::GET_SCOPE_INPUTS);
    }
}

void FrequencyAnalysisModule::refreshAvailability()
{
    if (!comm || inputCatalog.inputs.size() < 2 || !scope || !generator
        || !scope->isAvailable() || !generator->isAvailable())
        return;
    scopeSpec = dynamic_cast<ScopeSpec *>(scope->getSpecification());
    generatorSpec = dynamic_cast<ArbGeneratorSpec *>(generator->getSpecification());
    if (!scopeSpec || !generatorSpec || scopeSpec->maxADCChannels < 2
        || generatorSpec->maxDACChannels < 1)
        return;
    static_cast<AnalysisSpecification *>(moduleSpecification)->combine(scopeSpec, generatorSpec);
    refreshConnections();
    const bool alreadyAvailable = isAvailable();
    setModuleConfigured();
    if (!alreadyAvailable)
        showModuleControl();
    QList<PinFunctionInfo> pins = scopePins.mid(0, 2) + generatorPins.mid(0, 1);
    if (pins.isEmpty()) {
        pins = {{scopeSpec->channelPins[0], "Reference CH1", "scope"},
                {scopeSpec->channelPins[1], "Response CH2", "scope"},
                {generatorSpec->channelPins[0], "DAC CH1", "signal_generator"}};
    }
    showModulePinFunctions(moduleName, pins);
    QStringList wiring;
    for (const PinFunctionInfo &pin : pins)
        wiring.append(pin.label + ": " + pin.pin);
    window->setWiring(tr("DAC CH1 -> DUT input / scope CH1 reference; DUT output -> scope CH2. %1")
                          .arg(wiring.join(QStringLiteral("; "))));
    showModuleDescription(moduleName,
                          {tr("Measurement"), tr("Inputs"), tr("Resources")},
                          {tr("Gain and relative phase"), tr("Scope CH1 reference, CH2 response"),
                           tr("Oscilloscope + arbitrary DAC generator")});
}

void FrequencyAnalysisModule::setResourceCheck(std::function<QString(const ResourceSet &)> check)
{
    resourceCheck = std::move(check);
}

void FrequencyAnalysisModule::refreshConnections()
{
    const QStringList defaults = {scopeSpec->channelPins[0], scopeSpec->channelPins[1]};
    const QString dacPin = generatorSpec->channelPins[0];
    window->setAcquisitionLimits(scopeSpec->maxSamplingRate12B / 2,
                                 std::min(32768, scopeSpec->memorySize / 4));
    window->setVoltageLimits(generatorSpec->rangeMin, generatorSpec->rangeMax);
    window->setInputCapabilities(inputCatalog.inputs, inputCatalog.defaults, defaults, dacPin);
    publishTiming();
}

void FrequencyAnalysisModule::setTransferRate(double bytesPerSecond)
{
    if (!std::isfinite(bytesPerSecond) || bytesPerSecond < 0) {
        qWarning() << "Invalid frequency analysis transfer rate.";
        return;
    }
    timing.transferBytesPerSecond = bytesPerSecond;
}

void FrequencyAnalysisModule::publishTiming()
{
    if (generatorSpec) {
        timing.generatorBufferBytes = generatorSpec->generatorBufferSize;
        timing.generatorMaximumRate = generatorSpec->maxSamplingRate;
        timing.generatorClock = generatorSpec->periphClockFrequency;
    }
    if (uploadBlocksTotal > 0)
        timing.uploadSecondsPerBlock = uploadSecondsTotal / uploadBlocksTotal;
    if (downloadBytesTotal > 0)
        timing.downloadSecondsPerByte = downloadSecondsTotal / downloadBytesTotal;
    if (timedCaptures > 0)
        timing.captureOverheadSeconds = captureOverheadTotal / timedCaptures;
    if (timedPoints > 0)
        timing.pointOverheadSeconds = pointOverheadTotal / timedPoints;
    timing.learned = timedPoints > 0;
    window->setTimingParameters(timing);
}

void FrequencyAnalysisModule::recordCaptureTiming(int rate)
{
    if (!captureClock.isValid())
        return;
    const double duration = captureClock.nsecsElapsed() / 1e9;
    const double recordSeconds = double(sampling.samples) / rate;
    pointCaptureSeconds += duration;
    const double download = triggerMilliseconds >= 0
        ? std::max(0.0, duration - triggerMilliseconds / 1000.0 - recordSeconds)
        : std::max(0.0, duration - 2 * recordSeconds);
    downloadSecondsTotal += download;
    downloadBytesTotal += 4LL * sampling.samples + 74;
    if (triggerMilliseconds >= 0)
        captureOverheadTotal += std::max(0.0, triggerMilliseconds / 1000.0 - recordSeconds);
    ++timedCaptures;
    captureClock.invalidate();
}

bool FrequencyAnalysisModule::resolveInputs(const FrequencyAnalysis::Settings &configuration,
                                            int &reference, int &response, QString &error) const
{
    if (inputCatalog.inputs.size() < 2) {
        error = tr("Wait for the board's ADC pin list before starting a sweep.");
        return false;
    }
    reference = configuration.referenceInput < 0 ? inputCatalog.defaults[0] : configuration.referenceInput;
    response = configuration.responseInput < 0 ? inputCatalog.defaults[1] : configuration.responseInput;
    for (int slot = 0; slot < 2; ++slot) {
        const int index = slot == 0 ? reference : response;
        if (index < 0 || index >= inputCatalog.inputs[slot].size()
            || !AdcInputCatalog::isExternal(inputCatalog.inputs[slot][index])) {
            error = tr("Select an available external input for scope CH%1 in Connection.").arg(slot + 1);
            return false;
        }
    }
    return true;
}

bool FrequencyAnalysisModule::updateInputResources(QString &error)
{
    int reference = -1, response = -1;
    if (!resolveInputs(window->settings(), reference, response, error))
        return false;
    ResourceSet extra;
    QList<PinFunctionInfo> pins;
    for (int slot = 0; slot < 2; ++slot) {
        const QString name = inputCatalog.inputs[slot][slot == 0 ? reference : response].trimmed();
        pins.append({name, slot == 0 ? "Reference CH1" : "Response CH2", "scope"});
        QString physical = name.toUpper();
        // Header aliases not already covered by the scope's default masks.
        if (physical == QLatin1String("D11")) physical = QStringLiteral("PA7");
        if (physical == QLatin1String("D12")) physical = QStringLiteral("PA6");
        if (physical == QLatin1String("A3")) physical = QStringLiteral("PB0");
        static const QRegularExpression portPin(QStringLiteral("^P([A-K])(\\d{1,2})$"));
        const auto match = portPin.match(physical);
        if (match.hasMatch()) {
            const int bit = match.captured(2).toInt();
            if (bit > 15) {
                error = tr("Invalid physical ADC pin name: %1").arg(name);
                return false;
            }
            const quint32 mask = quint32(1) << bit;
            if (match.captured(1) == QLatin1String("A")) extra.gpioA |= mask;
            if (match.captured(1) == QLatin1String("B")) extra.gpioB |= mask;
            if (match.captured(1) == QLatin1String("C")) extra.gpioC |= mask;
            if (match.captured(1) == QLatin1String("D")) extra.gpioD |= mask;
        } else {
            bool isDefaultPin = false;
            for (int ch = 0; ch < std::min(scopeSpec->maxADCChannels, MAX_SCOPE_CHANNELS); ++ch)
                isDefaultPin = isDefaultPin || name.compare(scopeSpec->channelPins[ch].trimmed(),
                                                            Qt::CaseInsensitive) == 0;
            if (!isDefaultPin) {
                error = tr("The board did not provide usable resource information for pin %1.").arg(name);
                return false;
            }
        }
    }
    pins.append({generatorSpec->channelPins[0], "DAC CH1", "signal_generator"});
    ResourceSet requested = ResourceSet::fromModule(scope);
    requested.mergeWith(ResourceSet::fromModule(generator));
    requested.mergeWith(extra);
    requested.pins.clear();
    for (const auto &pin : pins)
        requested.pins.insert(pin.pin.trimmed().toUpper());
    if (resourceCheck) {
        const QString conflict = resourceCheck(requested);
        if (!conflict.isEmpty()) {
            error = conflict;
            return false;
        }
    }
    const bool active = isActive();
    if (active)
        emit releaseConflictingModules(moduleName, getResources());
    static_cast<AnalysisSpecification *>(moduleSpecification)->combine(scopeSpec, generatorSpec, extra);
    showModulePinFunctions(moduleName, pins);
    QStringList wiring;
    for (const auto &pin : pins)
        wiring.append(pin.label + ": " + pin.pin);
    window->setWiring(wiring.join(QStringLiteral("; ")));
    if (active)
        emit blockConflictingModules(moduleName, getResources());
    return true;
}

QWidget *FrequencyAnalysisModule::getWidget() { return window; }
QByteArray FrequencyAnalysisModule::getConfiguration() { return window->serialize(); }
void FrequencyAnalysisModule::parseConfiguration(QByteArray data)
{
    QString error;
    if (!window->restore(data, error)) {
        window->setStatus(error, true);
        qWarning() << "Frequency analysis session:" << error;
    } else if (!inputCatalog.inputs.isEmpty() && !updateInputResources(error)) {
        window->setStatus(error, true);
        qWarning() << "Frequency analysis session inputs:" << error;
    }
}

void FrequencyAnalysisModule::parseData(QByteArray)
{
    qWarning() << "Unexpected host-side frequency analysis protocol message.";
}

void FrequencyAnalysisModule::writeConfiguration() {}

void FrequencyAnalysisModule::startModule()
{
    setModuleStatus(ModuleStatus::PAUSE);
    window->setStatus(state == State::Fault
                          ? tr("Reconnect before starting another sweep after an interrupted operation.")
                          : tr("Select reference and response pins in Connection, then start a sweep."),
                      state == State::Fault);
}

void FrequencyAnalysisModule::stopHardware()
{
    if (!comm)
        return;
    send(Commands::SCOPE, Commands::STOP);
    send(Commands::SIGNAL_GENERATOR, Commands::CMD_GEN_STOP);
    send(Commands::SIGNAL_GENERATOR, Commands::DEINIT);
}

void FrequencyAnalysisModule::stopModule()
{
    watchdog.stop();
    settling.stop();
    inputTimeout.stop();
    stopHardware();
    // Without transaction IDs, an interrupted upload must be fenced on reconnect.
    if (state != State::Idle)
        state = State::Fault;
    window->setRunning(false);
    window->setStatus(state == State::Fault
                          ? tr("Stopped. Reconnect before reopening an interrupted sweep.")
                          : tr("Stopped."));
}

bool FrequencyAnalysisModule::isRunning() const
{
    return state != State::Idle && state != State::Fault && state != State::Draining;
}

void FrequencyAnalysisModule::send(const QByteArray &prefix, const QByteArray &command)
{
    comm->write(prefix, command);
}

void FrequencyAnalysisModule::sendValue(const QByteArray &prefix, const QByteArray &command, quint32 value)
{
    comm->write(prefix, command, static_cast<int>(value));
}

void FrequencyAnalysisModule::armWatchdog(int milliseconds)
{
    watchdog.start(milliseconds);
}

void FrequencyAnalysisModule::requestFrequency()
{
    ++frequencyRequests;
    send(Commands::SIGNAL_GENERATOR, Commands::GET_REAL_SMP_FREQ);
    armWatchdog();
}

void FrequencyAnalysisModule::startSweep()
{
    if (state == State::Fault) {
        window->setStatus(tr("Reconnect the board before starting another sweep after an interrupted operation."), true);
        return;
    }
    if (state != State::Idle)
        return;
    if (!comm || !isAvailable() || !isActive()) {
        window->setStatus(tr("Open the Frequency analysis module on a board with a DAC and two scope inputs first."), true);
        return;
    }
    QString error;
    settings = window->settings();
    if (!FrequencyAnalysis::makeSweep(settings, points, error)) {
        window->setStatus(error, true);
        qWarning() << "Frequency analysis settings:" << error;
        return;
    }
    if (!resolveInputs(settings, referenceSelector, responseSelector, error)
        || !updateInputResources(error)) {
        window->setStatus(error, true);
        qWarning() << "Frequency analysis inputs:" << error;
        return;
    }
    if (generatorSpec->maxDACChannels < 1 || generatorSpec->DACResolution < 1
        || generatorSpec->DACResolution > 16 || generatorSpec->rangeMax <= generatorSpec->rangeMin
        || generatorSpec->generatorBufferSize < 4 || generatorSpec->maxSamplingRate < 2
        || generatorSpec->periphClockFrequency <= 0) {
        window->setStatus(tr("The board returned unsupported DAC capabilities."), true);
        return;
    }
    const double amplitude = std::max(settings.startAmplitude, settings.stopAmplitude);
    if (settings.offset - amplitude / 2 < generatorSpec->rangeMin
        || settings.offset + amplitude / 2 > generatorSpec->rangeMax) {
        window->setStatus(tr("The entire waveform must fit the DAC range %1 to %2 V. Reduce Vpp or change offset.")
                              .arg(generatorSpec->rangeMin).arg(generatorSpec->rangeMax), true);
        return;
    }
    if (settings.stopFrequency * 2 > std::min(generatorSpec->maxSamplingRate, 0x00ffffff)) {
        window->setStatus(tr("Stop frequency exceeds the board's two-sample waveform limit (%1 Hz).")
                              .arg(std::min(generatorSpec->maxSamplingRate, 0x00ffffff) / 2.0), true);
        return;
    }
    if (settings.startFrequency < settings.periods / 1800.0) {
        window->setStatus(tr("The requested periods need more than 30 minutes at the starting frequency."), true);
        return;
    }
    FrequencyAnalysis::TimingEstimate estimate;
    if (!FrequencyAnalysis::estimateSweep(settings, scopeSpec->maxSamplingRate12B / 2,
                                          std::min(32768, scopeSpec->memorySize / 4), estimate, error, timing)) {
        window->setStatus(tr("The sweep cannot be acquired: %1").arg(error), true);
        qWarning() << "Frequency analysis acquisition plan:" << error;
        return;
    }
    generatorTransport = std::make_unique<GenCommons>(
        Commands::SIGNAL_GENERATOR, comm, generatorSpec->maxSamplingRate);
    pointIndex = 0;
    lowCoherence = false;
    periodTargetLimited = false;
    pointClock.start();
    averaging.clear();
    window->clearResults();
    window->setRunning(true);
    setModuleStatus(ModuleStatus::WAIT_EVENT);
    window->setStatus(tr("Preparing generator; waiting for the instrument command barrier."));
    state = State::Barrier;
    send(Commands::SCOPE, Commands::STOP);
    send(Commands::SIGNAL_GENERATOR, Commands::CMD_GEN_STOP);
    send(Commands::SIGNAL_GENERATOR, Commands::DEINIT);
    send(Commands::SIGNAL_GENERATOR, Commands::CMD_GEN_MODE + ":" + Commands::CMD_MODE_DAC);
    requestFrequency();
}

void FrequencyAnalysisModule::cancelSweep()
{
    if (state == State::Idle || state == State::Fault || state == State::Draining)
        return;
    finish(tr("Sweep cancelled; output stopped."), false);
}

void FrequencyAnalysisModule::beginPoint()
{
    if (pointIndex > 0 || !pointClock.isValid())
        pointClock.start();
    pointCaptureSeconds = lastUploadSeconds = lastSettlingSeconds = 0;
    send(Commands::SCOPE, Commands::STOP);
    send(Commands::SIGNAL_GENERATOR, Commands::CMD_GEN_STOP);
    QString error;
    if (!prepareWaveform(error)) {
        finish(error, true);
        return;
    }
    uploadClock.start();
    generatorTransport->setSignalToSend(&waveform);
    generatorTransport->setSamplingFrequency(0, double(generatorRate));
    send(Commands::SIGNAL_GENERATOR, Commands::CMD_GEN_OUTBUFF_ON);
    state = State::Uploading;
    uploadNext();
}

bool FrequencyAnalysisModule::prepareWaveform(QString &error)
{
    const auto &point = points.at(pointIndex);
    const int maxRate = std::min(generatorSpec->maxSamplingRate, 0x00ffffff);
    const int memory = std::min(generatorSpec->generatorBufferSize, 131070);
    waveformLength = SignalCreator::calculateSignalLength(
        MemoryLength::BEST_FIT, 0, memory, point.frequency, maxRate,
        generatorSpec->periphClockFrequency, 2);
    if (waveformLength < 2 || waveformLength > memory / 2) {
        error = tr("No best-fit waveform with at least two DAC samples is available at %1 Hz.").arg(point.frequency);
        return false;
    }
    const double requestedRate = point.frequency * waveformLength;
    if (!std::isfinite(requestedRate) || requestedRate < 1 || requestedRate > maxRate) {
        error = tr("The best-fit waveform exceeds the DAC sampling limits at %1 Hz.").arg(point.frequency);
        return false;
    }
    generatorRate = quint32(std::llround(requestedRate));
    if (generatorRate == 0 || generatorRate > quint32(maxRate)) {
        error = tr("The requested DAC sampling rate cannot be represented.");
        return false;
    }
    const int phase = waveformLength == 2 ? 90 : waveformLength == 3 ? 15 : 0;
    const auto volts = SignalCreator::createSignal(
        SignalShape::SINE, waveformLength, point.amplitude / 2, settings.offset,
        50, phase, generatorSpec->rangeMin, generatorSpec->rangeMax);
    const int maximumCode = int((quint32(1) << generatorSpec->DACResolution) - 1);
    QList<int> codes;
    codes.reserve(volts.size());
    for (qreal value : volts) {
        const int code = qRound((value - generatorSpec->rangeMin)
                               / (generatorSpec->rangeMax - generatorSpec->rangeMin) * maximumCode);
        if (code < 0 || code > maximumCode) {
            error = tr("Generated waveform contains an out-of-range DAC code.");
            return false;
        }
        codes.append(code);
    }
    waveform = {codes};
    return true;
}

void FrequencyAnalysisModule::instrumentError(const QString &message)
{
    if (isRunning())
        finish(tr("Measurement stopped: %1").arg(message), true);
}

void FrequencyAnalysisModule::finish(const QString &message, bool error)
{
    watchdog.stop();
    settling.stop();
    send(Commands::SCOPE, Commands::STOP);
    send(Commands::SIGNAL_GENERATOR, Commands::CMD_GEN_STOP);
    captureStarted = false;
    drainMessage = message;
    drainError = error;
    state = State::Draining;
    window->setStatus(message, error);
    // Drain every outstanding readback before accepting a new upload's acknowledgements.
    requestFrequency();
    if (error)
        qWarning() << "Frequency analysis:" << message;
}

void FrequencyAnalysisModule::uploadNext()
{
    generatorTransport->sendNext();
    if (generatorTransport->isSentAll()) {
        if (uploadClock.isValid()) {
            lastUploadSeconds = uploadClock.nsecsElapsed() / 1e9;
            uploadSecondsTotal += lastUploadSeconds;
            uploadBlocksTotal += (waveformLength + SEND_BLOCK_SIZE - 1) / SEND_BLOCK_SIZE;
            uploadClock.invalidate();
        }
        state = State::Frequency;
        requestFrequency();
        return;
    }
    armWatchdog();
}

void FrequencyAnalysisModule::configureCapture()
{
    if (settlingClock.isValid()) {
        lastSettlingSeconds = settlingClock.nsecsElapsed() / 1e9;
        settlingClock.invalidate();
    }
    averaging.clear();
    actualRate = 0;
    rateAttempts = 0;
    send(Commands::SCOPE, Commands::STOP);
    send(Commands::SCOPE, Commands::CHANNELS + ":" + Commands::CHANNELS_2);
    send(Commands::SCOPE, Commands::SCOPE_DATA_DEPTH + ":" + Commands::DATA_DEPTH_12B);
    send(Commands::SCOPE, Commands::SCOPE_ADC_CHANNEL_DEAFULT);
    sendValue(Commands::SCOPE, Commands::SCOPE_ADC_CHANNEL, quint32(referenceSelector));
    sendValue(Commands::SCOPE, Commands::SCOPE_ADC_CHANNEL, (1U << 8) | quint32(responseSelector));
    send(Commands::SCOPE, Commands::SCOPE_TRIG_CHANNEL + ":" + Commands::CHANNELS_1);
    send(Commands::SCOPE, Commands::SCOPE_TRIG_EDGE + ":" + Commands::EDGE_RISING);
    sendValue(Commands::SCOPE, Commands::SCOPE_TRIG_LEVEL, 32768);
    send(Commands::SCOPE, Commands::SCOPE_TRIG_MODE + ":" + Commands::MODE_AUTO_FAST);
    sendValue(Commands::SCOPE, Commands::SCOPE_PRETRIGGER, 0);
    sendValue(Commands::SCOPE, Commands::DATA_LENGTH, quint32(sampling.samples));
    sendValue(Commands::SCOPE, Commands::SAMPLING_FREQ, quint32(sampling.requestedRate));
    requestCapture(true);
}

void FrequencyAnalysisModule::requestCapture(bool first)
{
    state = State::Capturing;
    captureStarted = false;
    triggerMilliseconds = -1;
    captureClock.start();
    send(Commands::SCOPE, first ? Commands::START : Commands::NEXT);
    const double durationMs = 1000.0 * sampling.samples / sampling.requestedRate;
    armWatchdog(int(std::min(3600000.0, std::max(5000.0, durationMs * 2 + 3000))));
    window->setStatus(tr("Point %1/%2: %3 Hz; capture %4/%5")
                          .arg(pointIndex + 1).arg(points.size()).arg(actualFrequency, 0, 'g', 9)
                          .arg(averaging.count() + 1).arg(settings.averages));
}

bool FrequencyAnalysisModule::decodeCapture(const QByteArray &data, QVector<double> &reference,
                                          QVector<double> &response, int &rate, double &adcStep, QString &error) const
{
    const auto capture = ScopeCapture::decode(data, 0, error);
    if (!capture)
        return false;
    if (capture->channels.size() != 2 || capture->resolution != 12
        || capture->channels[0].size() != sampling.samples
        || capture->channels[1].size() != sampling.samples) {
        error = tr("Unexpected scope channel count, resolution or capture length.");
        return false;
    }
    rate = capture->samplingRate;
    const double low = capture->rangeMin / 1000.0;
    const double high = capture->rangeMax / 1000.0;
    const double margin = (high - low) / 4095.0;
    adcStep = margin;
    QVector<double> *channels[] = {&reference, &response};
    for (int ch = 0; ch < 2; ++ch) {
        channels[ch]->reserve(sampling.samples);
        int clipped = 0;
        for (const QPointF &point : capture->channels[ch]) {
            const double value = point.y();
            if (value <= low + margin || value >= high - margin)
                ++clipped;
            channels[ch]->append(value);
        }
        if (clipped > 0) {
            error = tr("CH%1 reaches the ADC rails. Reduce amplitude or adjust the offset.").arg(ch + 1);
            return false;
        }
    }
    return true;
}

void FrequencyAnalysisModule::acceptCapture(const QByteArray &data)
{
    watchdog.stop();
    QVector<double> reference, response;
    QString error;
    int rate = 0;
    double adcStep = 0;
    if (!decodeCapture(data, reference, response, rate, adcStep, error)) {
        finish(error, true);
        return;
    }
    if (!FrequencyAnalysis::validateCapture(actualFrequency, rate, sampling.samples,
                                           settings.allowAliasing, error)) {
        if (averaging.count() == 0 && rateAttempts < 8) {
            const double alias = std::abs(std::remainder(actualFrequency, double(rate)));
            const int limit = std::min(32768, scopeSpec->memorySize / 4) / 8 * 8;
            bool retry = false;
            if (alias > 0 && alias <= rate / 4.0
                && (settings.allowAliasing || actualFrequency <= rate / 4.0)) {
                int nextLength = 128;
                const double required = 20.0 * rate / alias;
                while (nextLength < required && nextLength <= limit / 2)
                    nextLength *= 2;
                if (nextLength >= required && nextLength > sampling.samples && nextLength <= limit
                    && double(nextLength) / sampling.requestedRate <= 1800) {
                    sampling.samples = nextLength;
                    retry = true;
                }
            }
            // Alias mode still prefers a direct-rate correction for a tone below Nyquist.
            if (!retry && (!settings.allowAliasing || actualFrequency < rate / 2.0)
                && sampling.requestedRate > 1) {
                const bool tooSlow = actualFrequency > rate / 4.0;
                const double desiredActualRate = tooSlow ? actualFrequency * 8
                    : std::min(rate * 0.8, actualFrequency * limit / 20);
                const double proposed = sampling.requestedRate * desiredActualRate / rate;
                const int maximum = std::max(1, scopeSpec->maxSamplingRate12B / 2);
                const int request = int(std::clamp(tooSlow ? std::ceil(proposed) : std::floor(proposed),
                                                  1.0, double(maximum)));
                if ((tooSlow && request > sampling.requestedRate)
                    || (!tooSlow && request < sampling.requestedRate)) {
                    sampling.requestedRate = request;
                    retry = true;
                }
            }
            if (!retry && settings.allowAliasing && sampling.requestedRate > 100) {
                sampling.requestedRate -= std::max(1, sampling.requestedRate / 137);
                retry = true;
            }
            if (retry) {
                ++rateAttempts;
                send(Commands::SCOPE, Commands::STOP);
                sendValue(Commands::SCOPE, Commands::DATA_LENGTH, quint32(sampling.samples));
                sendValue(Commands::SCOPE, Commands::SAMPLING_FREQ, quint32(sampling.requestedRate));
                requestCapture(true);
                return;
            }
        }
        finish(tr("ADC capture is not resolvable: %1 (f=%2 Hz, Fs=%3 Hz, N=%4).")
                   .arg(error).arg(actualFrequency, 0, 'g', 9).arg(rate).arg(sampling.samples), true);
        return;
    }
    if (actualRate != 0 && rate != actualRate) {
        finish(tr("ADC sampling rate changed during averaging."), true);
        return;
    }
    actualRate = rate;
    const double observedCycles = sampling.samples
        * std::abs(std::remainder(actualFrequency, double(rate))) / rate;
    periodTargetLimited = periodTargetLimited || observedCycles < settings.periods * 0.95;
    const auto referenceRange = std::minmax_element(reference.cbegin(), reference.cend());
    if (*referenceRange.second - *referenceRange.first < 8 * adcStep) {
        finish(tr("Reference signal is missing or below eight ADC counts peak-to-peak."), true);
        return;
    }
    FrequencyAnalysis::Tone input, output;
    if (!FrequencyAnalysis::extractTone(reference, actualFrequency, rate, input, error, adcStep)
        || !FrequencyAnalysis::extractTone(response, actualFrequency, rate, output, error, adcStep)) {
        finish(error, true);
        return;
    }
    if (!averaging.add(input, output, actualFrequency, 0, error)) {
        finish(error, true);
        return;
    }
    recordCaptureTiming(rate);
    if (averaging.count() < settings.averages) {
        requestCapture(false);
        return;
    }
    FrequencyAnalysis::Result result;
    if (!averaging.result(result, error)) {
        finish(error, true);
        return;
    }
    lowCoherence = lowCoherence || (settings.averages > 1 && result.coherence < 0.9);
    window->appendResult(points[pointIndex].frequency, actualFrequency, points[pointIndex].amplitude,
                         rate, sampling.samples, waveformLength, result);
    if (pointClock.isValid()) {
        const double duration = pointClock.nsecsElapsed() / 1e9;
        pointOverheadTotal += std::max(0.0, duration - lastUploadSeconds
                                            - pointCaptureSeconds - lastSettlingSeconds);
        ++timedPoints;
        publishTiming();
    }
    ++pointIndex;
    if (pointIndex == points.size()) {
        finish(lowCoherence
                   ? tr("Sweep complete with low-coherence points (<0.9). Check noise, wiring and settling.")
                   : (periodTargetLimited
                          ? tr("Sweep complete. Some records met the Fourier minimum, but not the requested period target; see observed cycles in Data.")
                          : tr("Sweep complete.")),
               false);
    } else {
        beginPoint();
    }
}

void FrequencyAnalysisModule::parseInstrumentData(const QByteArray &prefix, const QByteArray &data)
{
    const QByteArray header = data.left(4);
    if (prefix == Commands::SCOPE && header == "NACK" && isDiscoveringInputs()) {
        inputTimeout.stop();
        window->setStatus(tr("ADC pin discovery was denied by the firmware (check licensing)."), true);
        qWarning() << "ADC input capability request was denied by the firmware.";
        return;
    }
    if (prefix == Commands::SCOPE && header == "INP_") {
        inputTimeout.stop();
        if (state != State::Idle) {
            instrumentError(tr("Unexpected ADC input configuration response during acquisition."));
            return;
        }
        QString error;
        AdcInputCatalog catalog;
        if (!catalog.parse(data, error)) {
            window->setStatus(tr("Invalid ADC pin list: %1").arg(error), true);
            qWarning() << "Frequency analysis ADC input catalog:" << error;
            return;
        }
        inputCatalog = std::move(catalog);
        refreshAvailability();
        if (isAvailable() && !updateInputResources(error)) {
            window->setStatus(error, true);
            qWarning() << "Frequency analysis ADC inputs:" << error;
        }
        return;
    }
    if (header == Commands::INACTIVE) {
        instrumentError(tr("Firmware disabled an instrument (check licensing and board capabilities)."));
        return;
    }
    if (prefix == Commands::SIGNAL_GENERATOR) {
        if (header == Commands::CMD_GEN_SIGNAL_REAL_SAMPLING_FREQ_CH1 && frequencyRequests > 0) {
            if (data.size() != 8) {
                instrumentError(tr("Malformed generator frequency readback."));
                return;
            }
            generatorRate = qFromBigEndian<quint32>(data.constData() + 4);
            frequencySeen = true;
        } else if (header == Commands::CMD_GEN_SIGNAL_REAL_SAMPLING_FREQ_CH2 && frequencyRequests > 0) {
            if (data.size() != 8 || !frequencySeen) {
                instrumentError(tr("Incomplete generator frequency readback."));
                return;
            }
            frequencySeen = false;
            --frequencyRequests;
            if (frequencyRequests != 0)
                return;
            watchdog.stop();
            if (state == State::Barrier) {
                beginPoint();
            } else if (state == State::Frequency) {
                actualFrequency = double(generatorRate) / waveformLength;
                QString error;
                if (!std::isfinite(actualFrequency) || actualFrequency <= 0) {
                    finish(tr("Generator returned an invalid sampling rate."), true);
                    return;
                }
                // Every board can sustain this conservative two-channel, 12-bit limit.
                const int maxRate = int(scopeSpec->maxSamplingRate12B / 2);
                const int maxSamples = std::min(32768, int(scopeSpec->memorySize / 4));
                if (!FrequencyAnalysis::planSampling(actualFrequency, maxRate, maxSamples,
                                                    settings.periods, settings.allowAliasing, sampling, error,
                                                    32)) {
                    finish(error, true);
                    return;
                }
                if (1000.0 * sampling.samples / sampling.requestedRate > 1800000.0) {
                    finish(tr("A capture would exceed 30 minutes. Increase the starting frequency or reduce the period count."), true);
                    return;
                }
                state = State::Starting;
                send(Commands::SIGNAL_GENERATOR, Commands::CMD_GEN_START);
                armWatchdog();
            } else if (state == State::Draining) {
                send(Commands::SIGNAL_GENERATOR, Commands::DEINIT);
                state = State::Idle;
                setModuleStatus(ModuleStatus::PAUSE);
                window->setRunning(false);
                window->setStatus(drainMessage, drainError);
            }
        } else if (header == Commands::CMD_GEN_NEXT && state == State::Uploading) {
            uploadNext();
        } else if (header == Commands::CMD_GEN_OK && state == State::Starting) {
            watchdog.stop();
            state = State::Settling;
            settlingClock.start();
            const double settleMs = std::max(double(settings.settlingMs), 3000.0 / actualFrequency);
            settling.start(int(std::min(3600000.0, std::ceil(settleMs))));
        }
    } else if (prefix == Commands::SCOPE && state == State::Capturing) {
        if (header == Commands::SAMPLING)
            captureStarted = true;
        else if (header == Commands::TRIGGERED && captureClock.isValid())
            triggerMilliseconds = captureClock.nsecsElapsed() / 1e6;
        else if (header == Commands::SCOPE_INCOME && captureStarted)
            acceptCapture(data);
    }
}
