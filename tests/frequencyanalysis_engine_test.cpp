#include <QtTest>
#include "modules/frequencyanalysis/frequencyanalysisengine.h"

#include <cmath>
#include <limits>
#include <random>

using namespace FrequencyAnalysis;

namespace {

constexpr double pi = 3.14159265358979323846;

QVector<double> capture(int samples, double frequency, double rate, double amplitude,
                        double phase, double dc = 1.65)
{
    QVector<double> values(samples);
    for (int n = 0; n < samples; ++n)
        values[n] = dc + amplitude * std::cos(2 * pi * frequency * n / rate + phase);
    return values;
}

double phaseError(double measured, double expected)
{
    return std::abs(std::remainder(measured - expected, 2 * pi)) * 180 / pi;
}

Tone phasor(std::complex<double> value)
{
    return {value, std::abs(value) / std::sqrt(2.0)};
}

} // namespace

class FrequencyAnalysisEngineTest : public QObject
{
    Q_OBJECT

private slots:
    void defaultSweep()
    {
        Settings settings;
        QVector<SweepPoint> points;
        QString error = QStringLiteral("stale");
        QVERIFY2(makeSweep(settings, points, error), qPrintable(error));
        QVERIFY(error.isEmpty());
        QCOMPARE(points.size(), 41);
        QCOMPARE(points.front().frequency, 10.0);
        QCOMPARE(points.back().frequency, 100000.0);
        QCOMPARE(points.front().amplitude, 1.0);
        QCOMPARE(points.back().amplitude, 1.0);
        for (int i = 1; i < points.size(); ++i)
            QVERIFY(points[i].frequency > points[i - 1].frequency);

        settings.startFrequency = 100;
        settings.stopFrequency = 101;
        settings.pointsPerDecade = 1;
        QVERIFY2(makeSweep(settings, points, error), qPrintable(error));
        QCOMPARE(points.size(), 2);
        QCOMPARE(points.front().frequency, 100.0);
        QCOMPARE(points.back().frequency, 101.0);
    }

    void sweepSpacing_data()
    {
        QTest::addColumn<bool>("logFrequency");
        QTest::addColumn<bool>("logAmplitude");
        QTest::newRow("log-linear") << true << false;
        QTest::newRow("linear-linear") << false << false;
        QTest::newRow("log-geometric") << true << true;
        QTest::newRow("linear-geometric") << false << true;
    }

    void sweepSpacing()
    {
        QFETCH(bool, logFrequency);
        QFETCH(bool, logAmplitude);
        Settings settings;
        settings.startFrequency = 10;
        settings.stopFrequency = 1000;
        settings.startAmplitude = 4;
        settings.stopAmplitude = 0.25;
        settings.pointsPerDecade = 2;
        settings.frequencySpacing = logFrequency ? Spacing::Logarithmic : Spacing::Linear;
        settings.amplitudeSpacing = logAmplitude ? Spacing::Logarithmic : Spacing::Linear;
        QVector<SweepPoint> points;
        QString error;
        QVERIFY2(makeSweep(settings, points, error), qPrintable(error));
        QCOMPARE(points.size(), 5);
        QCOMPARE(points.front().frequency, 10.0);
        QCOMPARE(points.back().frequency, 1000.0);
        QCOMPARE(points.front().amplitude, 4.0);
        QCOMPARE(points.back().amplitude, 0.25);
        QVERIFY(std::abs(points[2].frequency - (logFrequency ? 100.0 : 505.0)) < 1e-10);
        QVERIFY(std::abs(points[2].amplitude - (logAmplitude ? 1.0 : 2.125)) < 1e-12);
    }

    void invalidSweeps_data()
    {
        QTest::addColumn<int>("invalid");
        const char *names[] = {"zero-frequency", "negative-frequency", "infinite-start", "nan-stop",
                               "descending-frequency", "equal-frequency", "zero-amplitude",
                               "negative-amplitude", "nan-amplitude", "infinite-amplitude",
                               "zero-density", "excess-density", "zero-averages", "excess-averages",
                               "few-periods", "excess-periods", "negative-settling", "excess-settling",
                               "nan-offset", "infinite-skew", "excess-skew", "unknown-frequency-spacing",
                               "unknown-amplitude-spacing", "overflow-ratio", "excess-total-points"};
        for (int i = 0; i < int(sizeof(names) / sizeof(*names)); ++i)
            QTest::newRow(names[i]) << i;
    }

    void invalidSweeps()
    {
        QFETCH(int, invalid);
        const double nan = std::numeric_limits<double>::quiet_NaN();
        const double inf = std::numeric_limits<double>::infinity();
        Settings settings;
        switch (invalid) {
        case 0: settings.startFrequency = 0; break;
        case 1: settings.startFrequency = -1; break;
        case 2: settings.startFrequency = inf; break;
        case 3: settings.stopFrequency = nan; break;
        case 4: settings.stopFrequency = 1; break;
        case 5: settings.stopFrequency = settings.startFrequency; break;
        case 6: settings.startAmplitude = 0; break;
        case 7: settings.stopAmplitude = -1; break;
        case 8: settings.startAmplitude = nan; break;
        case 9: settings.stopAmplitude = inf; break;
        case 10: settings.pointsPerDecade = 0; break;
        case 11: settings.pointsPerDecade = 1001; break;
        case 12: settings.averages = 0; break;
        case 13: settings.averages = 129; break;
        case 14: settings.periods = 15; break;
        case 15: settings.periods = 513; break;
        case 16: settings.settlingMs = -1; break;
        case 17: settings.settlingMs = 10001; break;
        case 18: settings.offset = nan; break;
        case 19: settings.channelSkewSeconds = inf; break;
        case 20: settings.channelSkewSeconds = 1.01; break;
        case 21: settings.frequencySpacing = static_cast<Spacing>(99); break;
        case 22: settings.amplitudeSpacing = static_cast<Spacing>(99); break;
        case 23:
            settings.startFrequency = std::numeric_limits<double>::denorm_min();
            settings.stopFrequency = std::numeric_limits<double>::max();
            settings.pointsPerDecade = 1000;
            break;
        case 24:
            settings.startFrequency = 1;
            settings.stopFrequency = 1e10;
            settings.pointsPerDecade = 1000;
            break;
        }
        QVector<SweepPoint> points{{1, 1}};
        QString error;
        QVERIFY(!makeSweep(settings, points, error));
        QVERIFY(!error.isEmpty());
        QVERIFY(points.isEmpty());
    }

    void sweepLimits()
    {
        Settings settings;
        settings.startFrequency = 1;
        settings.stopFrequency = 1e11;
        settings.pointsPerDecade = 909;
        settings.averages = 128;
        settings.periods = 512;
        settings.settlingMs = 10000;
        settings.channelSkewSeconds = -1;
        QVector<SweepPoint> points;
        QString error;
        QVERIFY2(makeSweep(settings, points, error), qPrintable(error));
        QCOMPARE(points.size(), 10000);
        settings.pointsPerDecade = 910;
        QVERIFY(!makeSweep(settings, points, error));
        settings.pointsPerDecade = 1;
        settings.startFrequency = std::numeric_limits<double>::denorm_min();
        settings.stopFrequency = std::numeric_limits<double>::max();
        QVERIFY2(makeSweep(settings, points, error), qPrintable(error));
        QCOMPARE(points.front().frequency, settings.startFrequency);
        QCOMPARE(points.back().frequency, settings.stopFrequency);
    }

    void tonePrecision_data()
    {
        QTest::addColumn<double>("frequency");
        QTest::addColumn<double>("rate");
        QTest::addColumn<int>("samples");
        QTest::addColumn<double>("phase");
        QTest::addColumn<double>("dc");
        QTest::newRow("direct-off-bin") << (32.417 * 100000 / 4096) << 100000.0 << 4096 << 0.73 << 1.65;
        QTest::newRow("negative-alias") << 1000000.0 << 1010000.0 << 4096 << -1.17 << 2.7;
        QTest::newRow("positive-alias") << 1000000.0 << 990000.0 << 4096 << 2.71 << -3.2;
        QTest::newRow("higher-alias-order") << 4003456.78 << 1000000.0 << 16384 << -2.6 << 1.65;
        QTest::newRow("large-dc") << 1234.567 << 100000.0 << 4096 << 0.41 << 100000.0;
        QTest::newRow("non-power-of-two") << 1234.567 << 100000.0 << 3001 << -0.91 << 1.65;
    }

    void tonePrecision()
    {
        QFETCH(double, frequency);
        QFETCH(double, rate);
        QFETCH(int, samples);
        QFETCH(double, phase);
        QFETCH(double, dc);
        constexpr double amplitude = 0.83;
        Tone tone;
        QString error = QStringLiteral("stale");
        QVERIFY2(extractTone(capture(samples, frequency, rate, amplitude, phase, dc),
                             frequency, rate, tone, error), qPrintable(error));
        QVERIFY(error.isEmpty());
        const double gainError = std::abs(20 * std::log10(std::abs(tone.value) / amplitude));
        QVERIFY2(gainError < 0.02, qPrintable(QString::number(gainError, 'g', 12)));
        const double degreesError = phaseError(std::arg(tone.value), phase);
        QVERIFY2(degreesError < 0.1, qPrintable(QString::number(degreesError, 'g', 12)));
        QVERIFY(std::abs(tone.rms - amplitude / std::sqrt(2.0)) < 0.001);
    }

    void targetedToneNotLargestPeak()
    {
        constexpr int samples = 8192;
        constexpr double rate = 100000;
        const double frequency = 34.71 * rate / samples;
        auto values = capture(samples, frequency, rate, 0.3, -0.8, 2.1);
        for (int n = 0; n < samples; ++n)
            values[n] += 3 * std::cos(2 * pi * 721.3 * n / samples + 1.3);
        Tone tone;
        QString error;
        QVERIFY2(extractTone(values, frequency, rate, tone, error), qPrintable(error));
        QVERIFY(std::abs(20 * std::log10(std::abs(tone.value) / 0.3)) < 0.02);
        QVERIFY(phaseError(std::arg(tone.value), -0.8) < 0.1);
    }

    void invalidTones()
    {
        QString error;
        Tone tone;
        const auto values = capture(4096, 1000, 100000, 1, 0);
        for (double frequency : {0.0, -1.0, std::numeric_limits<double>::quiet_NaN(),
                                  std::numeric_limits<double>::infinity(), 1.0, 49999.0, 50000.0, 100000.0}) {
            tone = phasor({1, 1});
            QVERIFY(!extractTone(values, frequency, 100000, tone, error));
            QVERIFY(!error.isEmpty());
            QCOMPARE(tone.value, std::complex<double>{});
            QCOMPARE(tone.rms, 0.0);
        }
        for (double rate : {0.0, -1.0, std::numeric_limits<double>::infinity(),
                            std::numeric_limits<double>::quiet_NaN()})
            QVERIFY(!extractTone(values, 1000, rate, tone, error));
        QVERIFY(!extractTone(QVector<double>(32, 1), 1000, 100000, tone, error));
        auto corrupted = values;
        corrupted[17] = std::numeric_limits<double>::quiet_NaN();
        QVERIFY(!extractTone(corrupted, 1000, 100000, tone, error));
        corrupted[17] = std::numeric_limits<double>::infinity();
        QVERIFY(!extractTone(corrupted, 1000, 100000, tone, error));
        QVERIFY2(extractTone(QVector<double>(4096, 0), 1000, 100000, tone, error), qPrintable(error));
        QCOMPARE(tone.rms, 0.0);
        QVERIFY2(extractTone(QVector<double>(4096, 1.65), 1000, 100000, tone, error), qPrintable(error));
        QVERIFY(std::abs(tone.value) < 1e-12);
    }

    void directPlans_data()
    {
        QTest::addColumn<double>("frequency");
        QTest::addColumn<int>("maxRate");
        QTest::addColumn<int>("maxSamples");
        QTest::addColumn<int>("periods");
        QTest::newRow("preferred-32") << 10.0 << 2000000 << 32768 << 32;
        QTest::newRow("eight-samples-boundary") << 1000.0 << 8000 << 256 << 32;
        QTest::newRow("memory-bounded-record") << 1000.0 << 8000 << 1000 << 64;
        QTest::newRow("maximum-periods") << 1000.0 << 32000 << 100000 << 512;
        QTest::newRow("slow-rate") << 0.01 << 2000000 << 32768 << 32;
    }

    void directPlans()
    {
        QFETCH(double, frequency);
        QFETCH(int, maxRate);
        QFETCH(int, maxSamples);
        QFETCH(int, periods);
        SamplingPlan plan;
        QString error;
        QVERIFY2(planSampling(frequency, maxRate, maxSamples, periods, false, plan, error), qPrintable(error));
        QVERIFY(error.isEmpty());
        QVERIFY(plan.requestedRate >= 1 && plan.requestedRate <= maxRate);
        QVERIFY(plan.samples >= 128 && plan.samples <= maxSamples && plan.samples <= 32768);
        QCOMPARE(plan.samples & (plan.samples - 1), 0);
        QCOMPARE(plan.aliasFrequency, frequency);
        QVERIFY(plan.aliasFrequency <= plan.requestedRate / 8.0);
        QVERIFY(plan.aliasFrequency * plan.samples / plan.requestedRate >= periods - 1e-10);
        QVERIFY2(validateSampling(frequency, plan.requestedRate, plan.samples, periods, false, error),
                 qPrintable(error));
        SamplingPlan aliasAllowed;
        QVERIFY2(planSampling(frequency, maxRate, maxSamples, periods, true, aliasAllowed, error), qPrintable(error));
        QCOMPARE(aliasAllowed.requestedRate, plan.requestedRate);
        QCOMPARE(aliasAllowed.samples, plan.samples);
        if (frequency == 10) {
            QCOMPARE(plan.requestedRate, 320);
            QCOMPARE(plan.samples, 1024);
        }
    }

    void aliasPlans()
    {
        QString error;
        for (double frequency : {1000000.0, 1000001.37, 1000000000.0}) {
            SamplingPlan plan;
            QVERIFY2(planSampling(frequency, 1010000, 32768, 32, true, plan, error), qPrintable(error));
            QVERIFY(plan.requestedRate >= 1 && plan.requestedRate <= 1010000);
            QCOMPARE(plan.aliasFrequency, std::abs(std::remainder(frequency, double(plan.requestedRate))));
            QVERIFY2(validateSampling(frequency, plan.requestedRate, plan.samples, 32, true, error), qPrintable(error));
            QVERIFY(!validateSampling(frequency, plan.requestedRate, plan.samples, 32, false, error));
        }
        // Integer-rate aliasing at exactly eight samples/cycle is possible.
        SamplingPlan plan;
        QVERIFY2(planSampling(9, 8, 256, 32, true, plan, error), qPrintable(error));
        QCOMPARE(plan.requestedRate, 8);
        QCOMPARE(plan.aliasFrequency, 1.0);
    }

    void impossiblePlans()
    {
        SamplingPlan plan;
        QString error;
        const auto rejected = [&](double f, int rate, int samples, int periods, bool alias) {
            plan = {1, 256, 1};
            const bool accepted = planSampling(f, rate, samples, periods, alias, plan, error);
            return !accepted && !error.isEmpty() && plan.requestedRate == 0
                && plan.samples == 0 && plan.aliasFrequency == 0;
        };
        QVERIFY(rejected(1000, 7999, 32768, 32, false));
        QVERIFY(rejected(0.000001, 2000000, 32768, 32, false));
        QVERIFY(rejected(1000, 32000, 255, 32, false));
        QVERIFY(rejected(1000, 32000, 1000, 128, true));
        QVERIFY(rejected(1000000, 3, 32768, 32, true));
        QVERIFY(rejected(1, 2, 32768, 32, true));
        QVERIFY(rejected(0, 32000, 32768, 32, true));
        QVERIFY(rejected(std::numeric_limits<double>::quiet_NaN(), 32000, 32768, 32, true));
        QVERIFY(rejected(std::numeric_limits<double>::infinity(), 32000, 32768, 32, true));
        QVERIFY(rejected(1000, 0, 32768, 32, true));
        QVERIFY(rejected(1000, 32000, 32768, 15, true));
        QVERIFY(rejected(1000, 32000, 32768, 1025, true));
    }

    void plannerMatchesExhaustiveIntegerSearch()
    {
        QString error;
        for (double frequency : {0.001, 0.03125, 0.125, 0.25, 0.5, 1.125, 7.99,
                                  8.0, 9.0, 10.03125, 31.875, 100.25, 1000000.0}) {
            for (int maxRate = 1; maxRate <= 64; ++maxRate) {
                for (int limit : {256, 511, 512, 1024}) {
                    for (int periods : {16, 32}) {
                        bool possible = false;
                        for (int rate = 1; rate <= maxRate && !possible; ++rate) {
                            for (int samples = 128; samples <= limit && !possible; samples *= 2)
                                possible = validateSampling(frequency, rate, samples, periods, true, error);
                        }
                        SamplingPlan plan;
                        const bool planned = planSampling(frequency, maxRate, limit, periods, true, plan, error);
                        QVERIFY2(planned == possible, qPrintable(QStringLiteral("f=%1 rate=%2 samples=%3 periods=%4")
                                                                 .arg(frequency).arg(maxRate).arg(limit).arg(periods)));
                        if (planned) {
                            QVERIFY(plan.requestedRate >= 1 && plan.requestedRate <= maxRate);
                            QVERIFY(plan.samples <= limit);
                            QVERIFY2(validateSampling(frequency, plan.requestedRate, plan.samples,
                                                       periods, true, error), qPrintable(error));
                        }
                    }
                }
            }
        }
    }

    void actualSamplingMustBeRevalidated()
    {
        SamplingPlan plan;
        QString error;
        QVERIFY2(planSampling(1000, 32000, 32768, 32, false, plan, error), qPrintable(error));
        QCOMPARE(plan.requestedRate, 32000);
        QVERIFY(!validateSampling(1000, 34000, plan.samples, 32, false, error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!validateSampling(1000, 7999, 32768, 32, false, error));
        QVERIFY(validateSampling(1000000, 1010000, 4096, 32, true, error));
        QVERIFY(validateSampling(1000000, 990000, 4096, 32, true, error));
        QVERIFY(!validateSampling(1000000, 1010000, 4096, 32, false, error));
        QVERIFY(!validateSampling(1000000, 1000000, 4096, 32, true, error));
        QVERIFY(!validateSampling(1000000, 666667, 4096, 32, true, error));
        QVERIFY(!validateSampling(1000000, 1000001, 4096, 32, true, error));
        QVERIFY(!validateSampling(1000, 32000, 1000, 32, false, error));
        QVERIFY(!validateSampling(1000, 32000, 128, 32, false, error));
        QVERIFY(!validateSampling(1000, 32000, 65536, 32, false, error));
        QVERIFY(!validateSampling(1000, 0, 1024, 32, false, error));
        QVERIFY(!validateSampling(-1, 32000, 1024, 32, false, error));
        QVERIFY(!validateSampling(1000, 32000, 1024, 15, false, error));
    }

    void requestedSamplesPerPeriod()
    {
        QString error;
        for (const int density : {8, 16, 32, 128}) {
            SamplingPlan plan;
            QVERIFY2(planSampling(1000, 200000, 8192, 32, false, plan, error, density), qPrintable(error));
            QCOMPARE(plan.requestedRate, 1000 * density);
            QVERIFY(plan.samples >= 32 * density);
            QVERIFY(plan.samples <= 33 * density + 8);
            QCOMPARE(plan.samples % 8, 0);
        }
        SamplingPlan limited;
        QVERIFY2(planSampling(1000, 200000, 1000, 32, false, limited, error, 2048), qPrintable(error));
        QVERIFY(limited.requestedRate / limited.aliasFrequency < 32);
        QVERIFY(limited.samples <= 1000);
        QVERIFY2(validateSampling(1000, limited.requestedRate, limited.samples, 32, false, error), qPrintable(error));
        QVERIFY(!planSampling(1000, 200000, 8192, 32, false, limited, error, 7));
        QVERIFY(!error.isEmpty());
        QVERIFY(!planSampling(1000, 200000, 8192, 32, false, limited, error, 2049));
        Settings invalid;
        QVector<SweepPoint> points;
        invalid.referenceInput = -2;
        QVERIFY(!makeSweep(invalid, points, error));
        invalid.referenceInput = 0;
        invalid.responseInput = 256;
        QVERIFY(!makeSweep(invalid, points, error));
    }

    void timingEstimatesFollowTheActualPlan()
    {
        Settings settings;
        settings.stopFrequency = 100;
        TimingEstimate estimate;
        QString error;
        QVERIFY2(estimateSweep(settings, 2000000, 8192, estimate, error), qPrintable(error));
        QCOMPARE(estimate.lowestRate, 320);
        QCOMPARE(estimate.lowestSamples, 1024);
        QCOMPARE(estimate.lowestSamplesPerPeriod, 32.0);
        QVERIFY(std::abs(estimate.lowestFrequencySeconds - 13.1) < 1e-9);
        QVERIFY(std::abs(estimate.lowestFrequencyMaximumSeconds - 25.9) < 1e-9);
        QVERIFY(!estimate.densityLimited);
        QVector<SweepPoint> points;
        QVERIFY(makeSweep(settings, points, error));
        double total = 0, maximum = 0;
        for (const auto &point : points) {
            SamplingPlan plan;
            QVERIFY(planSampling(point.frequency, 2000000, 8192, settings.periods,
                                 settings.allowAliasing, plan, error));
            const double settling = std::max(settings.settlingMs / 1000.0, 3 / point.frequency);
            total += settling + settings.averages * double(plan.samples) / plan.requestedRate;
            maximum += settling + 2 * settings.averages * double(plan.samples) / plan.requestedRate;
        }
        QVERIFY(std::abs(total - estimate.totalSeconds) < 1e-9);
        QVERIFY(std::abs(maximum - estimate.totalMaximumSeconds) < 1e-9);
        settings.frequencySpacing = Spacing::Linear;
        TimingEstimate linear;
        QVERIFY(estimateSweep(settings, 2000000, 8192, linear, error));
        QVERIFY(linear.totalSeconds < estimate.totalSeconds);
        QVERIFY(estimateSweep(settings, 2000000, 512, estimate, error));
        QVERIFY(estimate.densityLimited);
        settings.stopFrequency = 1000000;
        settings.allowAliasing = false;
        QVERIFY(!estimateSweep(settings, 1000000, 8192, estimate, error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(estimate.totalSeconds, 0.0);
    }

    void captureSafetyIsNotThePeriodTarget()
    {
        QString error;
        QVERIFY(!validateSampling(1000, 64000, 1024, 32, false, error));
        QVERIFY2(validateCapture(1000, 64000, 1024, false, error), qPrintable(error));
        QVERIFY(!validateCapture(1000, 64000, 512, false, error));
        QVERIFY(!validateCapture(1000000, 1010000, 1024, true, error));
        QVERIFY(validateCapture(1000000, 1010000, 2048, true, error));
        QVERIFY(!validateCapture(1000000, 1000000, 4096, true, error));
        QVERIFY(!validateCapture(1000, 3999, 128, false, error));
        QVERIFY(validateCapture(1000, 4000, 128, false, error));
    }

    void timingIncludesTransportAndObservedOverheads()
    {
        Settings settings;
        settings.stopFrequency = 100;
        QString error;
        TimingEstimate acquisition, wire, observed;
        QVERIFY(estimateSweep(settings, 2000000, 8192, acquisition, error));
        TimingParameters parameters;
        parameters.generatorBufferBytes = 8192;
        parameters.generatorMaximumRate = 8400000;
        parameters.generatorClock = 168000000;
        parameters.transferBytesPerSecond = 11520;
        QVector<int> waveforms;
        QVERIFY2(estimateSweep(settings, 2000000, 8192, wire, error, parameters, &waveforms),
                 qPrintable(error));
        QVector<SweepPoint> points;
        QVERIFY(makeSweep(settings, points, error));
        QCOMPARE(waveforms.size(), points.size());
        const double firstUploadBytes = 2.0 * waveforms[0] + 24.0 * ((waveforms[0] + 63) / 64);
        const double firstDownloadBytes = 4.0 * wire.lowestSamples + 74;
        const double expected = acquisition.lowestFrequencyMaximumSeconds
            + (firstUploadBytes + settings.averages * firstDownloadBytes) / 11520;
        QVERIFY(std::abs(expected - wire.lowestFrequencyMaximumSeconds) < 1e-9);
        QVERIFY(wire.totalMaximumSeconds > acquisition.totalMaximumSeconds);
        parameters.uploadSecondsPerBlock = 0.05;
        parameters.downloadSecondsPerByte = 1.0 / 4000;
        parameters.captureOverheadSeconds = 0.03;
        parameters.pointOverheadSeconds = 0.05;
        parameters.learned = true;
        QVERIFY(estimateSweep(settings, 2000000, 8192, observed, error, parameters, &waveforms));
        QVERIFY(observed.lowestFrequencyMaximumSeconds > wire.lowestFrequencyMaximumSeconds);
        QVERIFY(observed.totalMaximumSeconds > wire.totalMaximumSeconds);
        waveforms[0] = 1;
        QVERIFY(!estimateSweep(settings, 2000000, 8192, observed, error, parameters, &waveforms));
        QVERIFY(!error.isEmpty());
        parameters.transferBytesPerSecond = -1;
        QVERIFY(!estimateSweep(settings, 2000000, 8192, observed, error, parameters));
    }

    void phaseRotatedCapturesAndPhysicalSkew()
    {
        constexpr double frequency = 1000000;
        constexpr double rate = 1010000;
        constexpr double skew = 37e-9;
        constexpr double gain = 0.2;
        constexpr double phase = -0.8;
        Averager average;
        QString error;
        std::complex<double> rawReferenceSum{0, 0};
        for (int i = 0; i < 8; ++i) {
            const double triggerPhase = 0.17 + i * 2 * pi / 8;
            Tone reference;
            Tone response;
            QVERIFY2(extractTone(capture(4096, frequency, rate, 0.8, triggerPhase),
                                 frequency, rate, reference, error), qPrintable(error));
            QVERIFY2(extractTone(capture(4096, frequency, rate, 0.8 * gain,
                                         triggerPhase + phase + 2 * pi * frequency * skew, 1.4),
                                 frequency, rate, response, error), qPrintable(error));
            rawReferenceSum += reference.value;
            QVERIFY2(average.add(reference, response, frequency, skew, error), qPrintable(error));
        }
        QVERIFY(std::abs(rawReferenceSum) < 1e-8);
        QCOMPARE(average.count(), 8);
        Result result;
        QVERIFY2(average.result(result, error), qPrintable(error));
        QVERIFY(std::abs(result.gainDb - 20 * std::log10(gain)) < 0.02);
        QVERIFY(phaseError(result.phaseDegrees * pi / 180, phase) < 0.1);
        QVERIFY(std::abs(result.referenceRms - 0.8 / std::sqrt(2.0)) < 0.001);
        QVERIFY(std::abs(result.responseRms - 0.8 * gain / std::sqrt(2.0)) < 0.001);
        QVERIFY(result.coherence > 0.99999 && result.coherence <= 1);
        average.clear();
        QCOMPARE(average.count(), 0);
        QVERIFY(!average.result(result, error));
    }

    void crossSpectrumCoherenceAndWeighting()
    {
        Averager average;
        QString error;
        Result result;
        for (int i = 0; i < 8; ++i) {
            const auto x = std::polar(1.0, i * 0.73);
            const std::complex<double> h(1, i % 2 ? 0.5 : -0.5);
            QVERIFY2(average.add(phasor(x), phasor(h * x), 1000, 0, error), qPrintable(error));
            if (i == 0) {
                QVERIFY2(average.result(result, error), qPrintable(error));
                QCOMPARE(result.coherence, 1.0);
            }
        }
        QVERIFY2(average.result(result, error), qPrintable(error));
        QVERIFY(std::abs(result.gainDb) < 1e-12);
        QVERIFY(std::abs(result.phaseDegrees) < 1e-12);
        QVERIFY(std::abs(result.coherence - 0.8) < 1e-12);
        QCOMPARE(result.averages, 8);
        QVERIFY(std::abs(result.referenceRms - std::sqrt(0.5)) < 1e-12);
        QVERIFY(std::abs(result.responseRms - std::sqrt(1.25 / 2)) < 1e-12);

        average.clear();
        QVERIFY(average.add(phasor({1, 0}), phasor({1, 0}), 1000, 0, error));
        QVERIFY(average.add(phasor({2, 0}), phasor({6, 0}), 1000, 0, error));
        QVERIFY2(average.result(result, error), qPrintable(error));
        // H1 is power weighted: (1*1 + 6*2)/(1 + 4), not mean(1, 3).
        QVERIFY(std::abs(result.gainDb - 20 * std::log10(13.0 / 5)) < 1e-12);
        QCOMPARE(result.averages, 2);
    }

    void coherenceUncertainty_data()
    {
        QTest::addColumn<double>("coherence");
        QTest::addColumn<int>("averages");
        QTest::addColumn<double>("relative");
        QTest::newRow("perfect") << 1.0 << 2 << 0.0;
        QTest::newRow("eighty-percent") << 0.8 << 8 << 0.125;
        QTest::newRow("four-times-the-averages") << 0.8 << 32 << 0.0625;
        QTest::newRow("lower-coherence") << 0.5 << 4 << std::sqrt(0.125);
    }

    void coherenceUncertainty()
    {
        QFETCH(double, coherence);
        QFETCH(int, averages);
        QFETCH(double, relative);
        Result result;
        result.coherence = coherence;
        result.averages = averages;
        Uncertainty uncertainty;
        QString error;
        QVERIFY2(estimateUncertainty(result, uncertainty, error), qPrintable(error));
        QVERIFY(error.isEmpty());
        QVERIFY(std::abs(uncertainty.relativeMagnitude - relative) < 1e-14);
        QVERIFY(std::abs(uncertainty.gainDb - 20 / std::log(10.0) * relative) < 1e-12);
        QVERIFY(std::abs(uncertainty.phaseDegrees - 180 / pi * relative) < 1e-12);
        result.coherence = std::numeric_limits<double>::denorm_min();
        QVERIFY2(estimateUncertainty(result, uncertainty, error), qPrintable(error));
        QVERIFY(std::isfinite(uncertainty.gainDb) && std::isfinite(uncertainty.phaseDegrees));
        QVERIFY(uncertainty.relativeMagnitude > 1e150);
    }

    void uncertaintyUnavailableCases()
    {
        for (const auto &entry : QList<QPair<double, int>>{
                 {1, 0}, {1, 1}, {0.8, -1}, {0, 8}, {-0.1, 8}, {1.1, 8},
                 {std::numeric_limits<double>::quiet_NaN(), 8},
                 {std::numeric_limits<double>::infinity(), 8}}) {
            Result result;
            result.coherence = entry.first;
            result.averages = entry.second;
            Uncertainty uncertainty{1, 1, 1};
            QString error;
            QVERIFY(!estimateUncertainty(result, uncertainty, error));
            QVERIFY(!error.isEmpty());
            QCOMPARE(uncertainty.relativeMagnitude, 0.0);
            QCOMPARE(uncertainty.gainDb, 0.0);
            QCOMPARE(uncertainty.phaseDegrees, 0.0);
        }
        Averager average;
        QString error;
        QVERIFY(average.add(phasor({1, 0}), phasor({1, 0}), 1000, 0, error));
        Result result;
        QVERIFY(average.result(result, error));
        QCOMPARE(result.coherence, 1.0);
        QCOMPARE(result.averages, 1);
        Uncertainty uncertainty;
        QVERIFY(!estimateUncertainty(result, uncertainty, error));
    }

    void uncertaintyMatchesIndependentNoiseScatter()
    {
        std::mt19937 random(45678);
        std::normal_distribution<double> noise(0, std::sqrt(0.125));
        constexpr int trials = 600;
        double gainSquared = 0, phaseSquared = 0, predictedGainSquared = 0, predictedPhaseSquared = 0;
        for (int trial = 0; trial < trials; ++trial) {
            Averager average;
            QString error;
            for (int record = 0; record < 64; ++record) {
                const auto x = std::polar(1.0, record * 0.73);
                const auto y = x + std::complex<double>(noise(random), noise(random));
                QVERIFY(average.add(phasor(x), phasor(y), 1000, 0, error));
            }
            Result result;
            Uncertainty uncertainty;
            QVERIFY(average.result(result, error));
            QVERIFY(estimateUncertainty(result, uncertainty, error));
            QCOMPARE(result.averages, 64);
            gainSquared += result.gainDb * result.gainDb;
            phaseSquared += result.phaseDegrees * result.phaseDegrees;
            predictedGainSquared += uncertainty.gainDb * uncertainty.gainDb;
            predictedPhaseSquared += uncertainty.phaseDegrees * uncertainty.phaseDegrees;
        }
        QVERIFY(std::abs(std::sqrt(gainSquared / predictedGainSquared) - 1) < 0.15);
        QVERIFY(std::abs(std::sqrt(phaseSquared / predictedPhaseSquared) - 1) < 0.15);
    }

    void averagerRangeAndInvalids()
    {
        Averager average;
        QString error;
        Result result;
        QVERIFY(!average.result(result, error));
        QVERIFY(!average.add(Tone{}, phasor({1, 0}), 1000, 0, error));
        const double nan = std::numeric_limits<double>::quiet_NaN();
        QVERIFY(!average.add(phasor({nan, 0}), phasor({1, 0}), 1000, 0, error));
        QVERIFY(!average.add(phasor({1, 0}), phasor({1, 0}), nan, 0, error));
        QVERIFY(!average.add(phasor({1, 0}), phasor({1, 0}), 1000, nan, error));
        QVERIFY(!average.add(phasor({1, 0}), phasor({1, 0}), 1000, 1.01, error));
        QVERIFY(!average.add(Tone{{1, 0}, -1}, phasor({1, 0}), 1000, 0, error));
        QCOMPARE(average.count(), 0);
        for (double amplitude : {1e-200, 1e200}) {
            average.clear();
            QVERIFY2(average.add(phasor({amplitude, 0}), phasor({-2 * amplitude, 0}),
                                 1000, 0, error), qPrintable(error));
            QVERIFY2(average.result(result, error), qPrintable(error));
            QVERIFY(std::abs(result.gainDb - 20 * std::log10(2.0)) < 1e-12);
            QCOMPARE(result.phaseDegrees, 180.0);
            QCOMPARE(result.coherence, 1.0);
            QVERIFY(std::abs(result.referenceRms / amplitude - 1 / std::sqrt(2.0)) < 1e-12);
        }
        const int before = average.count();
        QVERIFY(!average.add(Tone{}, phasor({1, 0}), 1000, 0, error));
        QCOMPARE(average.count(), before);
        QVERIFY2(average.result(result, error), qPrintable(error));
        QVERIFY(error.isEmpty());
    }
};

QTEST_APPLESS_MAIN(FrequencyAnalysisEngineTest)
#include "frequencyanalysis_engine_test.moc"
