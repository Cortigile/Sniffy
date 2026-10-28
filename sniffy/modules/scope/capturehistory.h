#ifndef CAPTUREHISTORY_H
#define CAPTUREHISTORY_H

#include <QByteArray>
#include <QPointF>
#include <QString>
#include <QVector>
#include <deque>
#include <optional>

struct ScopeCapture
{
    QVector<QVector<QPointF>> channels;
    int samplingRate = 0;
    int resolution = 0;
    int rangeMin = 0;
    int rangeMax = 0;
    float timeBase = 0;
    qreal zoom = 1;
    int triggerChannel = 0;
    int triggerLevel = 0;

    static std::optional<ScopeCapture> decode(const QByteArray &data, float pretrigger,
                                               QString &error);
    bool compatibleWith(const ScopeCapture &other) const;
};

class CaptureHistory
{
public:
    static constexpr int Capacity = 128;
    void append(ScopeCapture capture);
    void resetAverage();
    void setAverageWindow(int window);
    int size() const { return static_cast<int>(captures.size()); }
    int averageCount() const { return count; }
    int averageWindow() const { return window; }
    const ScopeCapture &at(int index) const { return captures.at(index).capture; }
    QVector<QVector<QPointF>> liveData() const;

private:
    struct Entry {
        ScopeCapture capture;
        quint64 generation;
    };
    std::deque<Entry> captures;
    QVector<QVector<QPointF>> sum;
    quint64 generation = 0;
    int window = 1;
    int count = 0;
    void accumulate(const ScopeCapture &capture, int sign);
};

#endif
