#ifndef FREQUENCYANALYSISENGINE_H
#define FREQUENCYANALYSISENGINE_H

#include <QtCore/QString>
#include <QtCore/QVector>
#include <complex>

namespace FrequencyAnalysis {

enum class Spacing { Logarithmic, Linear };

constexpr int maximumPeriods = 512;
constexpr int maximumSettlingMs = 10000;

struct Settings {
    double startFrequency = 100;
    double stopFrequency = 100000;
    double startAmplitude = 1;
    double stopAmplitude = 1;
    double offset = 1.65;
    int pointsPerDecade = 10;
    int averages = 4;
    int periods = 64;
    int settlingMs = 100;
    Spacing frequencySpacing = Spacing::Logarithmic;
    Spacing amplitudeSpacing = Spacing::Linear;
    bool allowAliasing = true;
    double channelSkewSeconds = 0;
    int referenceInput = -1;
    int responseInput = -1;
};

struct SweepPoint {
    double frequency;
    double amplitude;
};

// Amplitudes may increase, decrease, or remain constant. Skew is limited to +/-1 s.
// The same decade-based interval count is used for both frequency spacings.
bool makeSweep(const Settings &, QVector<SweepPoint> &, QString &error);

struct SamplingPlan {
    int requestedRate = 0;
    int samples = 0;
    double aliasFrequency = 0;
};

// Automatically chooses rate and a power-of-two record, targeting 32 samples
// per observed cycle. Requested periods are a planning target, not an ADC
// readback guarantee. Actual captures are checked separately for Fourier safety.
// Revalidate the firmware's actual rate, not just this integer request.
bool planSampling(double actualFrequency, int maxRate, int maxSamples, int periods,
                  bool allowAliasing, SamplingPlan &, QString &error, int samplesPerPeriod = 32);
bool validateSampling(double actualFrequency, int actualRate, int samples, int periods,
                      bool allowAliasing, QString &error);
bool validateCapture(double actualFrequency, int actualRate, int samples,
                     bool allowAliasing, QString &error);

struct TimingParameters {
    int generatorBufferBytes = 0;
    int generatorMaximumRate = 0;
    int generatorClock = 0;
    double transferBytesPerSecond = 0;
    double uploadSecondsPerBlock = 0;
    double downloadSecondsPerByte = 0;
    double captureOverheadSeconds = 0;
    double pointOverheadSeconds = 0;
    bool learned = false;
};

struct TimingEstimate {
    double lowestFrequencySeconds = 0;
    double totalSeconds = 0;
    double lowestFrequencyMaximumSeconds = 0;
    double totalMaximumSeconds = 0;
    int lowestRate = 0;
    int lowestSamples = 0;
    double lowestSamplesPerPeriod = 0;
    bool densityLimited = false;
};

// Includes generator upload, capture transfer and device overhead when known.
// The upper estimate budgets the fast-auto trigger timeout; observed timing
// parameters refine unknown USB/processing/round-trip costs during a run.
// Reuse the optional waveform cache only while settings/generator limits match.
bool estimateSweep(const Settings &, int maxRate, int maxSamples,
                   TimingEstimate &, QString &error, const TimingParameters & = {},
                   QVector<int> *waveformLengths = nullptr);

struct Tone {
    std::complex<double> value;
    double rms = 0;
};

// Cosine convention: v[n] = dc + Re(value * exp(i*2*pi*f*n/rate)).
// One targeted, coherent-gain-corrected, symmetric five-term flat-top DFT;
// negative aliases are conjugated to preserve the physical-frequency phase.
// Requires >=64 samples and >=16 cycles from both DC and Nyquist. Buffers need
// not be powers of two. rms is the tone RMS, not the broadband waveform RMS.
bool extractTone(const QVector<double> &volts, double f, double rate, Tone &, QString &error);

struct Result {
    double gainDb = 0;
    double phaseDegrees = 0;
    double referenceRms = 0;
    double responseRms = 0;
    double coherence = 0;
    int averages = 0;
};

struct Uncertainty {
    double relativeMagnitude = 0;
    double gainDb = 0;
    double phaseDegrees = 0;
};

// Approximate H1 random standard errors (one sigma), assuming independent
// averages. Coherence is magnitude squared; this excludes systematic errors.
bool estimateUncertainty(const Result &, Uncertainty &, QString &error);

// Use one instance per sweep point (clear between frequencies).
// H1 = sum(Y * exp(-i*2*pi*f*skew) * conj(X)) / sum(|X|^2).
// RMS results are derived from peak phasor power, not from Tone::rms.
class Averager {
public:
    void clear();
    bool add(const Tone &reference, const Tone &response, double frequency,
             double skewSeconds, QString &error);
    int count() const;
    bool result(Result &, QString &error) const;

private:
    int m_count = 0;
    double m_scale = 0;
    long double m_sumXX = 0;
    long double m_sumYY = 0;
    std::complex<long double> m_sumYX{0, 0};
};

} // namespace FrequencyAnalysis

#endif // FREQUENCYANALYSISENGINE_H
