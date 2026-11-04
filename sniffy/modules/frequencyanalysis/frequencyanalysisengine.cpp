#include "frequencyanalysisengine.h"
#include "modules/arbgenerator/signalcreator.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace FrequencyAnalysis {
namespace {

constexpr double pi = 3.141592653589793238462643383279502884;
constexpr long double twoPi = 6.283185307179586476925286766559005768L;
constexpr int minimumSamples = 128;
constexpr int maximumSamples = 32768;
constexpr double maximumSkewSeconds = 1;

bool fail(QString &error, const char *message)
{
    error = QString::fromLatin1(message);
    return false;
}

bool positiveFinite(double value)
{
    return std::isfinite(value) && value > 0;
}

bool validPeriods(int periods)
{
    return periods >= 16 && periods <= maximumPeriods;
}

bool validSpacing(Spacing spacing)
{
    return spacing == Spacing::Linear || spacing == Spacing::Logarithmic;
}

bool validSkew(double skew)
{
    return std::isfinite(skew) && std::abs(skew) <= maximumSkewSeconds;
}

bool enoughCycles(double alias, double rate, qsizetype samples, int periods)
{
    const double cycles = (alias / rate) * samples;
    return cycles >= periods * (1 - 16 * std::numeric_limits<double>::epsilon());
}

bool tryRate(double frequency, int rate, int sampleLimit, int periods,
             bool allowAliasing, SamplingPlan &plan)
{
    if (rate < 1 || (!allowAliasing && frequency > rate / 8.0))
        return false;
    const double alias = std::abs(std::remainder(frequency, double(rate)));
    if (alias <= 0 || alias > rate / 8.0)
        return false;
    for (int samples = minimumSamples; samples <= sampleLimit; samples *= 2) {
        if (enoughCycles(alias, rate, samples, periods)) {
            plan = {rate, samples, alias};
            return true;
        }
    }
    return false;
}

bool finiteTone(const Tone &tone)
{
    return std::isfinite(tone.value.real()) && std::isfinite(tone.value.imag())
        && std::isfinite(tone.rms) && tone.rms >= 0;
}

} // namespace

bool makeSweep(const Settings &settings, QVector<SweepPoint> &points, QString &error)
{
    points.clear();
    error.clear();
    if (!positiveFinite(settings.startFrequency) || !positiveFinite(settings.stopFrequency)
        || settings.startFrequency >= settings.stopFrequency)
        return fail(error, "Frequencies must be finite, positive, and strictly increasing.");
    if (!positiveFinite(settings.startAmplitude) || !positiveFinite(settings.stopAmplitude))
        return fail(error, "Amplitudes must be finite and positive.");
    if (!std::isfinite(settings.offset) || !validSkew(settings.channelSkewSeconds))
        return fail(error, "Offset must be finite; channel skew must be finite and within +/-1 second.");
    if (settings.pointsPerDecade < 1 || settings.pointsPerDecade > 1000)
        return fail(error, "Points per decade must be in [1, 1000].");
    if (settings.averages < 1 || settings.averages > 128)
        return fail(error, "Averages must be in [1, 128].");
    if (!validPeriods(settings.periods))
        return fail(error, "Periods must be in [16, 512].");
    if (settings.referenceInput < -1 || settings.referenceInput > 255
        || settings.responseInput < -1 || settings.responseInput > 255)
        return fail(error, "ADC input selectors must be defaults (-1) or indices in [0, 255].");
    if (settings.settlingMs < 0 || settings.settlingMs > maximumSettlingMs)
        return fail(error, "Settling time must be in [0, 10] seconds.");
    if (!validSpacing(settings.frequencySpacing) || !validSpacing(settings.amplitudeSpacing))
        return fail(error, "Unknown sweep spacing.");

    const double ratio = settings.stopFrequency / settings.startFrequency;
    double decades;
    if (!std::isfinite(ratio))
        decades = std::log10(settings.stopFrequency) - std::log10(settings.startFrequency);
    else if (ratio == 1)
        decades = std::log1p((settings.stopFrequency - settings.startFrequency)
                            / settings.startFrequency) / std::log(10.0);
    else
        decades = std::log10(ratio);
    const double intervalsValue = std::ceil(decades * settings.pointsPerDecade);
    if (!std::isfinite(intervalsValue) || intervalsValue < 1 || intervalsValue > 9999)
        return fail(error, "The sweep must contain at most 10000 points.");
    const int intervals = int(intervalsValue);
    const double logStart = std::log(settings.startFrequency);
    const double logSpan = std::log(settings.stopFrequency) - logStart;
    const double logAmplitude = std::log(settings.startAmplitude);
    const double amplitudeSpan = std::log(settings.stopAmplitude) - logAmplitude;
    QVector<SweepPoint> generated;
    generated.reserve(intervals + 1);
    for (int i = 0; i <= intervals; ++i) {
        const double progress = double(i) / intervals;
        double frequency = settings.frequencySpacing == Spacing::Linear
            ? settings.startFrequency + progress * (settings.stopFrequency - settings.startFrequency)
            : std::exp(logStart + progress * logSpan);
        double amplitude = settings.amplitudeSpacing == Spacing::Linear
            ? settings.startAmplitude + progress * (settings.stopAmplitude - settings.startAmplitude)
            : std::exp(logAmplitude + progress * amplitudeSpan);
        if (i == 0) {
            frequency = settings.startFrequency;
            amplitude = settings.startAmplitude;
        } else if (i == intervals) {
            frequency = settings.stopFrequency;
            amplitude = settings.stopAmplitude;
        }
        if (!positiveFinite(frequency) || !positiveFinite(amplitude)
            || (i > 0 && frequency <= generated.back().frequency)
            || (i < intervals && frequency >= settings.stopFrequency))
            return fail(error, "Sweep points cannot be represented distinctly at these limits.");
        generated.append({frequency, amplitude});
    }
    points = std::move(generated);
    return true;
}

bool validateSampling(double actualFrequency, int actualRate, int samples, int periods,
                      bool allowAliasing, QString &error)
{
    error.clear();
    if (!positiveFinite(actualFrequency) || actualRate < 1)
        return fail(error, "Frequency and actual sampling rate must be finite and positive.");
    if (!validPeriods(periods))
        return fail(error, "Periods must be in [16, 512].");
    if (samples < minimumSamples || samples > maximumSamples || samples % 8 != 0)
        return fail(error, "Sample count must be a multiple of eight in [128, 32768].");
    if (!allowAliasing && actualFrequency > actualRate / 8.0)
        return fail(error, "The actual sampling rate provides fewer than eight samples per physical cycle.");
    const double alias = std::abs(std::remainder(actualFrequency, double(actualRate)));
    if (alias <= 0 || alias > actualRate / 8.0)
        return fail(error, "The observed alias is at DC or too close to Nyquist.");
    if (!enoughCycles(alias, actualRate, samples, periods))
        return fail(error, "The actual capture contains fewer observed cycles than requested.");
    return true;
}

bool planSampling(double actualFrequency, int maxRate, int maxSamples, int periods,
                  bool allowAliasing, SamplingPlan &plan, QString &error, int samplesPerPeriod)
{
    plan = {};
    error.clear();
    if (!positiveFinite(actualFrequency) || maxRate < 1 || maxSamples < minimumSamples)
        return fail(error, "Frequency and rate must be positive; at least 128 samples are required.");
    if (!validPeriods(periods))
        return fail(error, "Periods must be in [16, 512].");
    if (samplesPerPeriod < 8 || samplesPerPeriod > 2048)
        return fail(error, "Samples per observed period must be in [8, 2048].");
    int sampleLimit = minimumSamples;
    const int limit = std::min(maxSamples, maximumSamples);
    while (sampleLimit <= limit / 2)
        sampleLimit *= 2;
    if (periods > sampleLimit / 8)
        return fail(error, "The buffer cannot hold the requested periods at eight samples per cycle.");

    if (actualFrequency <= maxRate / 8.0) {
        const double lowest = std::max(1.0, std::ceil(8 * actualFrequency));
        const double highest = std::min(double(maxRate),
                                        std::floor(actualFrequency * sampleLimit / periods));
        if (lowest <= highest) {
            const int preferred = int(std::clamp(std::round(samplesPerPeriod * actualFrequency), lowest, highest));
            if (tryRate(actualFrequency, preferred, sampleLimit, periods, false, plan)
                || tryRate(actualFrequency, int(lowest), sampleLimit, periods, false, plan))
                return true;
        }
    }
    if (!allowAliasing)
        return fail(error, "No direct sampling plan meets the rate, buffer, and period limits.");

    const double minimumRatio = double(periods) / sampleLimit;
    const double preferredRatio = std::max(minimumRatio, 1.0 / samplesPerPeriod);
    const double firstOrder = std::max(1.0, std::floor(actualFrequency / maxRate));
    // At alias order k, f/r lies in k +/- [minimumRatio, 1/8].
    // Try integer rates nearest the requested density before the exhaustive search.
    for (int orderOffset = 0; orderOffset < 4; ++orderOffset) {
        const double order = firstOrder + orderOffset;
        for (int sign : {1, -1}) {
            const double lower = actualFrequency / (order + (sign > 0 ? 1 / 8.0 : -minimumRatio));
            const double upper = actualFrequency / (order + (sign > 0 ? minimumRatio : -1 / 8.0));
            const double lo = std::max(1.0, std::ceil(lower));
            const double hi = std::min(double(maxRate), std::floor(upper));
            if (!std::isfinite(lo) || lo > hi)
                continue;
            const int candidate = int(std::clamp(std::round(actualFrequency / (order + sign * preferredRatio)),
                                                 lo, hi));
            if (tryRate(actualFrequency, candidate, sampleLimit, periods, true, plan)
                || (candidate > lo && tryRate(actualFrequency, candidate - 1, sampleLimit, periods, true, plan))
                || (candidate < hi && tryRate(actualFrequency, candidate + 1, sampleLimit, periods, true, plan))
                || tryRate(actualFrequency, int(lo), sampleLimit, periods, true, plan)
                || tryRate(actualFrequency, int(hi), sampleLimit, periods, true, plan))
                return true;
        }
    }

    // Search remaining integer rates without skipping a potentially valid one.
    // Jump over forbidden continuous bands; ceil keeps boundary rounding safe.
    for (int rate = maxRate; rate >= 1;) {
        if (tryRate(actualFrequency, rate, sampleLimit, periods, true, plan))
            return true;
        const double alias = std::remainder(actualFrequency, double(rate));
        const double order = std::round((actualFrequency - alias) / rate);
        double boundary;
        if (std::abs(alias) < minimumRatio * rate)
            boundary = actualFrequency / (order + minimumRatio);
        else if (alias < 0)
            boundary = actualFrequency / (order - 1 / 8.0);
        else
            boundary = actualFrequency / (order + 1 - 1 / 8.0);
        if (boundary < 1)
            break;
        if (std::isfinite(boundary) && boundary < rate - 1.0)
            rate = int(std::ceil(boundary));
        else
            --rate;
    }
    return fail(error, "No safe integer sampling rate meets the alias, buffer, and period limits.");
}

bool validateCapture(double frequency, int rate, int samples, bool allowAliasing, QString &error)
{
    error.clear();
    if (!positiveFinite(frequency) || rate < 1 || samples < minimumSamples
        || samples > maximumSamples || samples % 8 != 0)
        return fail(error, "Invalid actual frequency, ADC rate or record geometry.");
    if (!allowAliasing && frequency > rate / 4.0)
        return fail(error, "The actual ADC rate cannot resolve this physical frequency without aliasing.");
    const double alias = std::abs(std::remainder(frequency, double(rate)));
    if (alias <= 0 || alias > rate / 4.0)
        return fail(error, "The observed tone is at DC or too close to Nyquist.");
    if (!enoughCycles(alias, rate, samples, 16))
        return fail(error, "The record needs at least 16 observed cycles for flat-top Fourier analysis.");
    return true;
}

bool estimateSweep(const Settings &settings, int maxRate, int maxSamples,
                   TimingEstimate &estimate, QString &error, const TimingParameters &timing,
                   QVector<int> *waveformLengths)
{
    estimate = {};
    QVector<SweepPoint> points;
    if (!makeSweep(settings, points, error))
        return false;
    for (double value : {timing.transferBytesPerSecond, timing.uploadSecondsPerBlock,
                         timing.downloadSecondsPerByte, timing.captureOverheadSeconds,
                         timing.pointOverheadSeconds}) {
        if (!std::isfinite(value) || value < 0)
            return fail(error, "Timing observations must be finite and nonnegative.");
    }
    const bool generatorKnown = timing.generatorBufferBytes != 0
        || timing.generatorMaximumRate != 0 || timing.generatorClock != 0;
    if (generatorKnown && (timing.generatorBufferBytes < 8 || timing.generatorMaximumRate < 4
                           || timing.generatorClock <= 0))
        return fail(error, "Incomplete generator timing capabilities.");
    const double secondsPerByte = std::max(timing.downloadSecondsPerByte,
        timing.transferBytesPerSecond > 0 ? 1 / timing.transferBytesPerSecond : 0);
    TimingEstimate calculated;
    QVector<int> calculatedLengths;
    const bool cachedLengths = waveformLengths && waveformLengths->size() == points.size();
    if (generatorKnown && !cachedLengths)
        calculatedLengths.reserve(points.size());
    for (int i = 0; i < points.size(); ++i) {
        SamplingPlan plan;
        if (!planSampling(points[i].frequency, maxRate, maxSamples, settings.periods,
                          settings.allowAliasing, plan, error)) {
            error = QStringLiteral("At %1 Hz: %2").arg(points[i].frequency, 0, 'g', 8).arg(error);
            return false;
        }
        const double captureSeconds = double(plan.samples) / plan.requestedRate;
        if (captureSeconds > 1800)
            return fail(error, "A capture would exceed 30 minutes.");
        const double settlingSeconds = std::max(settings.settlingMs / 1000.0, 3 / points[i].frequency);
        double uploadSeconds = 0;
        if (generatorKnown) {
            const int samples = cachedLengths ? waveformLengths->at(i)
                : SignalCreator::calculateSignalLength(MemoryLength::BEST_FIT, 0,
                    std::min(timing.generatorBufferBytes, 131070), points[i].frequency,
                    std::min(timing.generatorMaximumRate, 0x00ffffff), timing.generatorClock, 4);
            if (samples < 4 || samples > std::min(timing.generatorBufferBytes, 131070) / 2
                || samples * points[i].frequency > timing.generatorMaximumRate)
                return fail(error, "A sweep point has no feasible sine waveform for the timing estimate.");
            if (!cachedLengths)
                calculatedLengths.append(samples);
            const int blocks = (samples + 63) / 64;
            const double wireSeconds = timing.transferBytesPerSecond > 0
                ? (2.0 * samples + 24.0 * blocks) / timing.transferBytesPerSecond : 0;
            uploadSeconds = std::max(wireSeconds, blocks * timing.uploadSecondsPerBlock);
        }
        const double overhead = uploadSeconds + timing.pointOverheadSeconds
            + settings.averages * ((4.0 * plan.samples + 74) * secondsPerByte
                                   + timing.captureOverheadSeconds);
        const double pointSeconds = settlingSeconds + settings.averages * captureSeconds + overhead;
        const double maximumSeconds = settlingSeconds + settings.averages * 2 * captureSeconds + overhead;
        const double density = plan.requestedRate / plan.aliasFrequency;
        calculated.totalSeconds += pointSeconds;
        calculated.totalMaximumSeconds += maximumSeconds;
        calculated.densityLimited = calculated.densityLimited
            || std::abs(density - 32) > 0.64;
        if (i == 0) {
            calculated.lowestFrequencySeconds = pointSeconds;
            calculated.lowestFrequencyMaximumSeconds = maximumSeconds;
            calculated.lowestRate = plan.requestedRate;
            calculated.lowestSamples = plan.samples;
            calculated.lowestSamplesPerPeriod = density;
        }
    }
    estimate = calculated;
    if (waveformLengths && generatorKnown && !cachedLengths)
        *waveformLengths = std::move(calculatedLengths);
    error.clear();
    return true;
}

bool extractTone(const QVector<double> &volts, double f, double rate, Tone &tone, QString &error)
{
    tone = {};
    error.clear();
    if (!positiveFinite(f) || !positiveFinite(rate))
        return fail(error, "Tone frequency and sampling rate must be finite and positive.");
    const auto size = volts.size();
    if (size < 64)
        return fail(error, "Tone extraction requires at least 64 samples.");
    const double signedAlias = std::remainder(f, rate);
    const double normalized = std::abs(signedAlias) / rate;
    if (!enoughCycles(std::abs(signedAlias), rate, size, 16)
        || (0.5 - normalized) * size < 16)
        return fail(error, "Tone extraction requires 16 cycles of separation from DC and Nyquist.");
    double scale = 0;
    for (double value : volts) {
        if (!std::isfinite(value))
            return fail(error, "The capture contains nonfinite samples.");
        scale = std::max(scale, std::abs(value));
    }
    if (scale == 0)
        return true;
    long double mean = 0;
    for (double value : volts)
        mean += static_cast<long double>(value) / scale;
    mean /= size;
    long double real = 0;
    long double imaginary = 0;
    long double coherentGain = 0;
    for (decltype(volts.size()) n = 0; n < size; ++n) {
        const double windowAngle = 2 * pi * double(n) / double(size - 1);
        const double window = 0.21557895 - 0.41663158 * std::cos(windowAngle)
            + 0.277263158 * std::cos(2 * windowAngle)
            - 0.083578947 * std::cos(3 * windowAngle)
            + 0.006947368 * std::cos(4 * windowAngle);
        const double angle = 2 * pi * normalized * double(n);
        const long double centered = static_cast<long double>(volts[n]) / scale - mean;
        real += window * centered * std::cos(angle);
        imaginary -= window * centered * std::sin(angle);
        coherentGain += window;
    }
    // A real sinusoid has half its peak phasor at each signed frequency.
    // Dividing by sum(w), then multiplying by two, corrects coherent gain.
    const long double realPeak = (2 * real / coherentGain) * scale;
    const long double imaginaryPeak = (2 * imaginary / coherentGain) * scale;
    const std::complex<double> phasor{double(realPeak),
                                     double(signedAlias < 0 ? -imaginaryPeak : imaginaryPeak)};
    const double rms = double(std::hypot(realPeak / std::sqrt(2.0L),
                                         imaginaryPeak / std::sqrt(2.0L)));
    if (!std::isfinite(phasor.real()) || !std::isfinite(phasor.imag()) || !std::isfinite(rms))
        return fail(error, "The extracted tone exceeds the finite numerical range.");
    tone = {phasor, rms};
    return true;
}

bool estimateUncertainty(const Result &result, Uncertainty &uncertainty, QString &error)
{
    uncertainty = {};
    error.clear();
    if (!std::isfinite(result.coherence) || result.coherence < 0 || result.coherence > 1)
        return fail(error, "Coherence must be finite and in [0, 1].");
    if (result.averages < 2)
        return fail(error, "At least two independent averages are required.");
    if (result.coherence == 0)
        return fail(error, "Zero coherence has no finite uncertainty estimate.");
    // sqrt((1-C)/(2*N*C)), arranged to avoid overflowing 1/C near zero.
    const double relative = std::sqrt(1 - result.coherence) / std::sqrt(result.coherence)
        / std::sqrt(2.0 * result.averages);
    uncertainty = {relative, 20 / std::log(10.0) * relative, 180 / pi * relative};
    return true;
}

void Averager::clear()
{
    m_count = 0;
    m_scale = 0;
    m_sumXX = 0;
    m_sumYY = 0;
    m_sumYX = {0, 0};
}

bool Averager::add(const Tone &reference, const Tone &response, double frequency,
                   double skewSeconds, QString &error)
{
    error.clear();
    if (!finiteTone(reference) || !finiteTone(response))
        return fail(error, "Tone phasors and RMS values must be finite; RMS must be nonnegative.");
    if (!positiveFinite(frequency) || !validSkew(skewSeconds))
        return fail(error, "Frequency must be positive and finite; skew must be within +/-1 second.");
    if (reference.value == std::complex<double>{0, 0})
        return fail(error, "The reference tone has zero amplitude.");
    if (m_count == std::numeric_limits<int>::max())
        return fail(error, "Too many captures in this average.");
    const double scale = std::max({m_scale, std::abs(reference.value.real()),
                                   std::abs(reference.value.imag()), std::abs(response.value.real()),
                                   std::abs(response.value.imag())});
    // A shared dynamic amplitude scale keeps powers finite even for very large
    // or very small phasors. Rescale earlier spectra, not the measured phases.
    const long double rescale = static_cast<long double>(m_scale) / scale;
    const std::complex<long double> x(static_cast<long double>(reference.value.real()) / scale,
                                      static_cast<long double>(reference.value.imag()) / scale);
    std::complex<long double> y(static_cast<long double>(response.value.real()) / scale,
                                static_cast<long double>(response.value.imag()) / scale);
    const long double skewPhase = -twoPi * std::remainder(static_cast<long double>(frequency)
                                                         * skewSeconds, 1.0L);
    y *= std::complex<long double>(std::cos(skewPhase), std::sin(skewPhase));
    const long double xx = m_sumXX * rescale * rescale + std::norm(x);
    const long double yy = m_sumYY * rescale * rescale + std::norm(y);
    const auto yx = m_sumYX * rescale * rescale + y * std::conj(x);
    if (!std::isfinite(xx) || xx <= 0 || !std::isfinite(yy)
        || !std::isfinite(yx.real()) || !std::isfinite(yx.imag()))
        return fail(error, "The capture spectra exceed the numerical range.");
    m_scale = scale;
    m_sumXX = xx;
    m_sumYY = yy;
    m_sumYX = yx;
    ++m_count;
    return true;
}

int Averager::count() const
{
    return m_count;
}

bool Averager::result(Result &result, QString &error) const
{
    result = {};
    error.clear();
    if (m_count == 0 || m_sumXX <= 0 || m_sumYY <= 0)
        return fail(error, "A nonzero reference and response capture are required.");
    const long double crossMagnitude = std::abs(m_sumYX);
    if (crossMagnitude <= 0)
        return fail(error, "The cross spectrum is zero; finite gain and phase are undefined.");
    Result calculated;
    calculated.gainDb = double(20 * (std::log10(crossMagnitude) - std::log10(m_sumXX)));
    calculated.phaseDegrees = double(std::arg(m_sumYX) * 180 / (twoPi / 2));
    if (calculated.phaseDegrees <= -180)
        calculated.phaseDegrees += 360;
    calculated.referenceRms = double(m_scale * std::sqrt(m_sumXX / (2 * static_cast<long double>(m_count))));
    calculated.responseRms = double(m_scale * std::sqrt(m_sumYY / (2 * static_cast<long double>(m_count))));
    const long double correlation = crossMagnitude / std::sqrt(m_sumXX) / std::sqrt(m_sumYY);
    calculated.coherence = m_count == 1 ? 1 : double(std::clamp(correlation * correlation, 0.0L, 1.0L));
    calculated.averages = m_count;
    if (!std::isfinite(calculated.gainDb) || !std::isfinite(calculated.phaseDegrees)
        || !std::isfinite(calculated.referenceRms) || !std::isfinite(calculated.responseRms)
        || !std::isfinite(calculated.coherence))
        return fail(error, "The averaged result exceeds the finite numerical range.");
    result = calculated;
    return true;
}

} // namespace FrequencyAnalysis
