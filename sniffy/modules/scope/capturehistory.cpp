#include "capturehistory.h"
#include "scopedefs.h"

#include <QDataStream>
#include <QDebug>
#include <QIODevice>
#include <cmath>
#include <utility>

std::optional<ScopeCapture> ScopeCapture::decode(const QByteArray &data, float pretrigger,
                                                QString &error)
{
    ScopeCapture capture;
    if (!std::isfinite(pretrigger) || pretrigger < 0 || pretrigger > 100) {
        error = QStringLiteral("Invalid OSC_ pretrigger percentage");
        return std::nullopt;
    }
    QDataStream stream(data);
    int expectedChannels = 1;
    for (int channel = 1; channel <= expectedChannels; ++channel) {
        quint32 header = 0;
        qint32 rate = 0, length = 0;
        quint8 resolution = 0, unused = 0, index = 0, channels = 0;
        qint16 low = 0, high = 0;
        stream >> header >> rate >> resolution >> length >> low >> high
               >> unused >> index >> channels;
        const int bytesPerSample = resolution > 8 ? 2 : 1;
        if (stream.status() != QDataStream::Ok || header != 0x4f53435f ||
            rate <= 0 || resolution < 1 || resolution > 16 ||
            length <= 0 || length > 500000 || length % bytesPerSample != 0 ||
            low >= high || index != channel || channels < 1 || channels > MAX_SCOPE_CHANNELS ||
            (channel > 1 && channels != expectedChannels) ||
            stream.device()->bytesAvailable() < length) {
            error = QStringLiteral("Invalid or incomplete OSC_ channel %1").arg(channel);
            return std::nullopt;
        }
        expectedChannels = channels;
        if (channel == 1) {
            capture.samplingRate = rate;
            capture.resolution = resolution;
            capture.rangeMin = low;
            capture.rangeMax = high;
        } else if (rate != capture.samplingRate || resolution != capture.resolution ||
                   low != capture.rangeMin || high != capture.rangeMax ||
                   length / bytesPerSample != capture.channels.first().size()) {
            error = QStringLiteral("Inconsistent OSC_ channel geometry");
            return std::nullopt;
        }
        const int samples = length / bytesPerSample;
        const qreal minX = -qreal(samples) / rate * pretrigger / 100.0;
        QVector<QPointF> points;
        points.reserve(samples);
        for (int sample = 0; sample < samples; ++sample) {
            quint8 lo = 0, hi = 0;
            stream >> lo;
            if (bytesPerSample == 2)
                stream >> hi;
            const quint16 value = lo + (quint16(hi) << 8);
            const qreal voltage = (value * qreal(high - low) / ((1u << resolution) - 1) + low) / 1000;
            points.append(QPointF(minX + qreal(sample) / rate, voltage));
        }
        capture.channels.append(points);
    }
    // Firmware appends S_OK after the final channel, before the serial delimiter.
    const QByteArray trailer = stream.device()->readAll();
    if (!trailer.isEmpty() && trailer != QByteArrayLiteral("S_OK")) {
        error = QStringLiteral("Unexpected trailing OSC_ data");
        return std::nullopt;
    }
    error.clear();
    return capture;
}

bool ScopeCapture::compatibleWith(const ScopeCapture &other) const
{
    if (samplingRate != other.samplingRate || resolution != other.resolution ||
        rangeMin != other.rangeMin || rangeMax != other.rangeMax ||
        channels.size() != other.channels.size())
        return false;
    for (int ch = 0; ch < channels.size(); ++ch) {
        if (channels[ch].size() != other.channels[ch].size() || channels[ch].isEmpty() ||
            channels[ch].first().x() != other.channels[ch].first().x() ||
            channels[ch].last().x() != other.channels[ch].last().x())
            return false;
    }
    return true;
}

void CaptureHistory::accumulate(const ScopeCapture &capture, int sign)
{
    for (int ch = 0; ch < sum.size(); ++ch) {
        auto &points = sum[ch];
        const auto &source = capture.channels[ch];
        for (int i = 0; i < points.size(); ++i)
            points[i].ry() += sign * source[i].y();
    }
}

void CaptureHistory::resetAverage()
{
    ++generation;
    count = 0;
    sum.clear();
}

void CaptureHistory::append(ScopeCapture capture)
{
    Q_ASSERT(!capture.channels.isEmpty() && !capture.channels.first().isEmpty());
    if (count > 0 && !captures.back().capture.compatibleWith(capture))
        resetAverage();
    if (window == 1 || count == 0) {
        sum = capture.channels;
        count = 1;
    } else {
        if (count == window) {
            accumulate(captures.at(captures.size() - count).capture, -1);
            --count;
        }
        accumulate(capture, 1);
        ++count;
    }
    captures.push_back({std::move(capture), generation});
    if (size() > Capacity)
        captures.pop_front();
}

void CaptureHistory::setAverageWindow(int value)
{
    if (value < 1 || value > Capacity) {
        qWarning() << "Invalid scope averaging window" << value;
        return;
    }
    window = value;
    sum.clear();
    count = 0;
    for (auto it = captures.rbegin(); it != captures.rend() && count < window; ++it) {
        if (it->generation != generation)
            break;
        if (sum.isEmpty())
            sum = it->capture.channels;
        else
            accumulate(it->capture, 1);
        ++count;
    }
}

QVector<QVector<QPointF>> CaptureHistory::liveData() const
{
    if (captures.empty())
        return {};
    if (count <= 1)
        return captures.back().capture.channels;
    auto result = sum;
    for (auto &channel : result)
        for (auto &point : channel)
            point.ry() /= count;
    return result;
}
