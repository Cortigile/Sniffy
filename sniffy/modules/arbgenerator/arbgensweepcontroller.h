#ifndef ARBGENSWEEPCONTROLLER_H
#define ARBGENSWEEPCONTROLLER_H

#include <QObject>
#include <QDebug>
#include <QTimer>
#include <QVector>

#include "arbgeneratordefs.h"

class ArbGenSweepController : public QObject
{
    Q_OBJECT
public:
    explicit ArbGenSweepController(QObject *parent = nullptr);

    bool setParameters(qreal minFreq, qreal maxFreq, qreal totalTime,
                       SweepScale scale = SweepScale::LINEAR, SweepMode mode = SweepMode::SAW);
    void startTimer();
    void stopTimer();
    bool isActive() const;

private:
    qreal minFreq = 0;
    qreal maxFreq = 0;
    qreal totalTime = 0;
    SweepScale scale = SweepScale::LINEAR;
    SweepMode mode = SweepMode::SAW;

    QVector<qreal> freqSamples;
    int actualIndex = 0;

    QTimer *timer;

signals:
    void updateSweepFrequency(qreal frequency);

private slots:
    void timerCallback();

};

#endif // ARBGENSWEEPCONTROLLER_H
