#include "arbgensweepcontroller.h"
#include <cmath>

ArbGenSweepController::ArbGenSweepController(QObject *parent):
    QObject(parent)
{
   timer = new QTimer(this);
   connect(timer, &QTimer::timeout, this, &ArbGenSweepController::timerCallback);
}

bool ArbGenSweepController::setParameters(qreal minFreq, qreal maxFreq, qreal totalTime,
                                         SweepScale scale, SweepMode mode)
{
    if (!std::isfinite(minFreq) || !std::isfinite(maxFreq) || !std::isfinite(totalTime) ||
        minFreq < qreal(0.01f) || maxFreq > 1000000 || minFreq > maxFreq ||
        totalTime < 0.1 || totalTime > 60 ||
        (scale != SweepScale::LINEAR && scale != SweepScale::EXPONENTIAL) ||
        (mode != SweepMode::SAW && mode != SweepMode::SYMMETRIC)) {
        qWarning() << "Invalid generator sweep parameters:" << minFreq << maxFreq << totalTime;
        stopTimer();
        freqSamples.clear();
        return false;
    }

    if (!freqSamples.isEmpty() && this->minFreq == minFreq && this->maxFreq == maxFreq &&
        this->totalTime == totalTime && this->scale == scale && this->mode == mode)
        return true;

    actualIndex = 0;
    this->minFreq = minFreq;
    this->maxFreq = maxFreq;
    this->totalTime = totalTime;
    this->scale = scale;
    this->mode = mode;

    freqSamples.clear();
    const int intervals = qMax(2, qRound(totalTime / SWEEP_TIME_STEP));
    for (int i = 0; i <= intervals; ++i) {
        const qreal fraction = qreal(i) / intervals;
        const qreal frequency = scale == SweepScale::EXPONENTIAL
            ? minFreq * std::pow(maxFreq / minFreq, fraction)
            : minFreq + fraction * (maxFreq - minFreq);
        freqSamples.append(frequency);
    }
    freqSamples.first() = minFreq;
    freqSamples.last() = maxFreq;
    if (mode == SweepMode::SYMMETRIC) {
        for (int i = intervals - 1; i > 0; --i)
            freqSamples.append(freqSamples.at(i));
    }

    timer->setInterval(qRound(totalTime / intervals * 1000));
    timer->setTimerType(Qt::PreciseTimer);
    return true;
}

void ArbGenSweepController::startTimer()
{
    if (freqSamples.isEmpty()) {
        qWarning() << "Cannot start generator sweep without valid parameters";
        return;
    }
    if (timer->isActive())
        return;
    timer->start();
    timerCallback();
}

void ArbGenSweepController::stopTimer()
{
    timer->stop();
    actualIndex = 0;
}

bool ArbGenSweepController::isActive() const
{
    return timer->isActive();
}

void ArbGenSweepController::timerCallback()
{
    const qreal frequency = freqSamples.at(actualIndex);
    actualIndex = (actualIndex + 1) % freqSamples.size();
    emit updateSweepFrequency(frequency);
}
