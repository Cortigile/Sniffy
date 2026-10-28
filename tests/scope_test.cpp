#include <QtTest>
#include <QIODevice>
#include <QSlider>
#include <cmath>
#include "modules/scope/scope.h"

class ScopeTest : public QObject
{
    Q_OBJECT

    static ScopeCapture capture(qreal value, int rate = 1000, int channels = 2, int samples = 4)
    {
        ScopeCapture result;
        result.samplingRate = rate;
        result.resolution = 12;
        result.rangeMax = 3300;
        result.timeBase = 0.001f;
        for (int ch = 0; ch < channels; ++ch) {
            QVector<QPointF> points;
            for (int i = 0; i < samples; ++i)
                points.append(QPointF(qreal(i - samples / 2) / rate, value + ch + i));
            result.channels.append(points);
        }
        return result;
    }

    static QByteArray frame(int value, int rate = 1000, int resolution = 12, int channels = 2)
    {
        QByteArray data;
        QDataStream out(&data, QIODevice::WriteOnly);
        for (int ch = 1; ch <= channels; ++ch) {
            out << quint32(0x4f53435f) << qint32(rate) << quint8(resolution)
                << qint32(resolution > 8 ? 8 : 4) << qint16(0) << qint16(3300)
                << quint8(0) << quint8(ch) << quint8(channels);
            for (int i = 0; i < 4; ++i) {
                out << quint8(value & 255);
                if (resolution > 8)
                    out << quint8(value >> 8);
            }
        }
        data.append("S_OK");
        return data;
    }

private slots:
    void initTestCase()
    {
        Graphics::setTheme(0);
    }

    void boundedRawHistory()
    {
        CaptureHistory history;
        for (int i = 0; i < 140; ++i)
            history.append(capture(i));
        QCOMPARE(history.size(), 128);
        QCOMPARE(history.at(0).channels[0][0].y(), 12.0);
        QCOMPARE(history.at(127).channels[0][0].y(), 139.0);
        QCOMPARE(history.liveData(), history.at(127).channels);
    }

    void averagingControls()
    {
        ScopeConfig config;
        ScopeWindow window(&config);
        auto *toggle = window.findChild<WidgetSwitch *>();
        auto *slider = window.findChild<QSlider *>("scopeAverageWindow");
        QVERIFY(toggle);
        QVERIFY(slider);
        QVERIFY(toggle->isCheckedLeft());
        QCOMPARE(slider->orientation(), Qt::Horizontal);
        QCOMPARE(slider->minimum(), 2);
        QCOMPARE(slider->maximum(), 128);
        QCOMPARE(slider->value(), 16);
        QVERIFY(!slider->isEnabled());
        QSignalSpy changes(&window, &ScopeWindow::averageWindowChanged);
        auto *on = toggle->findChild<QPushButton *>("pushButton_right");
        auto *off = toggle->findChild<QPushButton *>("pushButton_left");
        QVERIFY(on);
        QVERIFY(off);
        on->click();
        QVERIFY(slider->isEnabled());
        QCOMPARE(changes.last().first().toInt(), 16);
        slider->setValue(128);
        QCOMPARE(changes.last().first().toInt(), 128);
        slider->setValue(2);
        QCOMPARE(changes.last().first().toInt(), 2);
        off->click();
        QVERIFY(!slider->isEnabled());
        QCOMPARE(changes.last().first().toInt(), 1);
        on->click();
        QCOMPARE(changes.last().first().toInt(), 2);
    }

    void rollingAverage()
    {
        CaptureHistory history;
        history.setAverageWindow(3);
        history.append(capture(1));
        QCOMPARE(history.averageCount(), 1);
        history.append(capture(3));
        QCOMPARE(history.liveData()[0][0].y(), 2.0);
        history.append(capture(5));
        history.append(capture(7));
        QCOMPARE(history.averageCount(), 3);
        QCOMPARE(history.liveData()[0][0].y(), 5.0);
        QCOMPARE(history.liveData()[1][3].y(), 9.0);
        QCOMPARE(history.at(0).channels[0][0].y(), 1.0);
        QCOMPARE(history.liveData()[0][0].x(), history.at(3).channels[0][0].x());
        history.setAverageWindow(2);
        QCOMPARE(history.liveData()[0][0].y(), 6.0);
        history.setAverageWindow(1);
        QCOMPARE(history.liveData(), history.at(3).channels);
    }

    void maximumWindowEviction()
    {
        CaptureHistory history;
        history.setAverageWindow(128);
        for (int i = 0; i < 256; ++i)
            history.append(capture(i));
        QCOMPARE(history.size(), 128);
        QCOMPARE(history.averageCount(), 128);
        QCOMPARE(history.liveData()[0][0].y(), 191.5);
        QCOMPARE(history.at(0).channels[0][0].y(), 128.0);
    }

    void averagingReducesNoise()
    {
        CaptureHistory history;
        history.setAverageWindow(128);
        const auto clean = capture(1, 1000, 2, 100);
        qreal rawSquaredError = 0;
        for (int acquisition = 0; acquisition < 128; ++acquisition) {
            auto noisy = clean;
            for (auto &channel : noisy.channels) {
                for (int i = 0; i < channel.size(); ++i) {
                    const qreal noise = 0.1 * std::sin(acquisition * 1.7 + i * 0.3);
                    channel[i].ry() += noise;
                    rawSquaredError += noise * noise;
                }
            }
            history.append(noisy);
        }
        qreal averagedSquaredError = 0;
        const auto averaged = history.liveData();
        for (int ch = 0; ch < averaged.size(); ++ch)
            for (int i = 0; i < averaged[ch].size(); ++i)
                averagedSquaredError += std::pow(averaged[ch][i].y() - clean.channels[ch][i].y(), 2);
        const qreal rawRms = std::sqrt(rawSquaredError / (128 * 2 * 100));
        const qreal averagedRms = std::sqrt(averagedSquaredError / (2 * 100));
        QVERIFY(averagedRms < rawRms * 0.1);
    }

    void resetPreservesHistory()
    {
        CaptureHistory history;
        history.setAverageWindow(16);
        history.append(capture(1));
        history.append(capture(3));
        history.resetAverage();
        QCOMPARE(history.averageCount(), 0);
        history.setAverageWindow(128);
        QCOMPARE(history.averageCount(), 0);
        history.append(capture(10));
        QCOMPARE(history.size(), 3);
        QCOMPARE(history.averageCount(), 1);
        QCOMPARE(history.liveData()[0][0].y(), 10.0);
    }

    void incompatibleGeometry_data()
    {
        QTest::addColumn<int>("change");
        for (int i = 0; i < 6; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }

    void incompatibleGeometry()
    {
        QFETCH(int, change);
        CaptureHistory history;
        history.setAverageWindow(16);
        history.append(capture(1));
        auto next = capture(9);
        switch (change) {
        case 0: next = capture(9, 2000); break;
        case 1: next.resolution = 8; break;
        case 2: next.rangeMax = 2500; break;
        case 3: next = capture(9, 1000, 1); break;
        case 4: next = capture(9, 1000, 2, 8); break;
        case 5:
            for (auto &channel : next.channels)
                for (auto &point : channel)
                    point.rx() += 0.001;
            break;
        }
        history.append(next);
        QCOMPARE(history.averageCount(), 1);
        QCOMPARE(history.liveData(), next.channels);
    }

    void decodeFrames()
    {
        QString error;
        const auto decoded = ScopeCapture::decode(frame(2048), 50, error);
        QVERIFY2(decoded.has_value(), qPrintable(error));
        QCOMPARE(decoded->channels.size(), 2);
        QCOMPARE(decoded->channels[0].size(), 4);
        QCOMPARE(decoded->channels[0][0].x(), -0.002);
        QVERIFY(qAbs(decoded->channels[0][0].y() - 2048.0 / 4095 * 3.3) < 1e-12);
        const auto eightBit = ScopeCapture::decode(frame(255, 2000, 8, 1), 25, error);
        QVERIFY(eightBit);
        QCOMPARE(eightBit->channels[0][0].y(), 3.3);
        QCOMPARE(eightBit->channels[0][0].x(), -0.0005);
    }

    void rejectIncompleteFrames()
    {
        const QByteArray complete = frame(2048);
        QString error;
        for (int length = 0; length < complete.size() - 4; ++length) {
            QVERIFY(!ScopeCapture::decode(complete.left(length), 50, error));
            QVERIFY(!error.isEmpty());
        }
        QVERIFY(ScopeCapture::decode(complete.chopped(4), 50, error));
        for (int trailerLength = 1; trailerLength < 4; ++trailerLength)
            QVERIFY(!ScopeCapture::decode(complete.chopped(4 - trailerLength), 50, error));
        QVERIFY(!ScopeCapture::decode(complete + "extra", 50, error));
        QVERIFY(!ScopeCapture::decode(complete.chopped(4) + "FAIL", 50, error));
        QVERIFY(!ScopeCapture::decode(frame(1, 0), 50, error));
        QVERIFY(!ScopeCapture::decode(complete, -1, error));
        QVERIFY(!ScopeCapture::decode(frame(1, 1000, 12, 5), 50, error));
        auto invalid = complete;
        invalid[18] = 0;
        QVERIFY(!ScopeCapture::decode(invalid, 50, error));
        invalid = complete;
        invalid[12] = 7; // Odd 16-bit payload length.
        QVERIFY(!ScopeCapture::decode(invalid, 50, error));
    }

    void defaultLiveAcquisition()
    {
        Comms communication;
        QObject::disconnect(&communication, &Comms::dataWrite, nullptr, nullptr);
        QSignalSpy writes(&communication, &Comms::dataWrite);
        WidgetControlModule control;
        control.setStatus(ModuleStatus::STOP);
        ModuleDockWidget dock;
        Scope module;
        Commands commands;
        module.cmd = &commands;
        module.setComms(&communication);
        module.setModuleControlWidget(&control);
        module.setDockWidgetWindow(&dock);
        module.config->ADCresolution = 12;
        module.config->realSamplingRate = 100000;
        module.config->rangeMin = 0;
        module.config->rangeMax = 3300;
        auto *spec = static_cast<ScopeSpec *>(module.getSpecification());
        spec->memorySize = 40000;
        spec->maxADCChannels = 2;
        module.writeConfiguration();
        module.startModule();
        QCOMPARE(module.config->triggerMode, ScopeTriggerMode::TRIG_AUTO);
        QCOMPARE(module.history.averageWindow(), 1);
        QVERIFY(!module.browsingHistory);
        const QByteArray nextCommand = Commands::SCOPE + ":" + Commands::NEXT + ";";
        for (const int value : {1000, 3000, 2000}) {
            writes.clear();
            module.parseData(frame(value));
            QCOMPARE(module.scopeData.size(), 2);
            QCOMPARE(module.scopeData[0].size(), 4);
            QVERIFY(qAbs(module.scopeData[0][0].y() - value / 4095.0 * 3.3) < 1e-12);
            QVERIFY(module.samplingActive);
            QCOMPARE(module.getModuleStatus(), ModuleStatus::PLAY);
            QCOMPARE(writes.size(), 1);
            QCOMPARE(writes.at(0).at(0).toByteArray(), nextCommand);
        }
        QCOMPARE(module.history.size(), 3);
        module.stopModule();
        delete module.scpWindow;
        module.scpWindow = nullptr;
    }

    void browsingAndSamplingReset()
    {
        Comms communication;
        QObject::disconnect(&communication, &Comms::dataWrite, nullptr, nullptr);
        QSignalSpy writes(&communication, &Comms::dataWrite);
        WidgetControlModule control;
        control.setStatus(ModuleStatus::STOP);
        ModuleDockWidget dock;
        Scope module;
        Commands commands;
        module.cmd = &commands;
        module.setComms(&communication);
        module.setModuleControlWidget(&control);
        module.setDockWidgetWindow(&dock);
        module.config->ADCresolution = 12;
        module.config->rangeMin = 0;
        module.config->rangeMax = 3300;
        module.config->numberOfChannels = 2;
        auto *spec = static_cast<ScopeSpec *>(module.getSpecification());
        spec->memorySize = 40000;
        spec->maxADCChannels = 2;
        module.timeAndMemoryHandle->setMaxParams(40000, 2);
        module.scpWindow->setNumChannels(2);
        module.setAverageWindow(2);
        module.startModule();
        module.parseData(frame(1000));
        module.parseData(frame(3000));
        QCOMPARE(module.history.size(), 2);
        QVERIFY(qAbs(module.scopeData[0][0].y() - 2000.0 / 4095 * 3.3) < 1e-12);
        auto *slider = module.getWidget()->findChild<QSlider *>("scopeHistorySlider");
        QVERIFY(slider);
        slider->setValue(0);
        QVERIFY(module.browsingHistory);
        QVERIFY(!module.samplingActive);
        QCOMPARE(module.scopeData, module.history.at(0).channels);
        const int pausedWrites = writes.size();
        module.parseData(frame(4000));
        QCOMPARE(writes.size(), pausedWrites);
        QCOMPARE(module.history.size(), 2);
        auto *navigation = module.getWidget()->findChild<WidgetButtons *>("scopeHistoryButtons");
        QVERIFY(navigation);
        navigation->clicked(1);
        QCOMPARE(module.historyIndex, 1);
        QCOMPARE(module.scopeData, module.history.at(1).channels);
        QVERIFY(module.browsingHistory);
        navigation->clicked(0);
        QCOMPARE(module.historyIndex, 0);
        navigation->clicked(2);
        QVERIFY(module.samplingActive);
        QVERIFY(!module.browsingHistory);
        QCOMPARE(module.history.averageCount(), 2);
        module.updateCustomSamplingFreq(2000);
        QCOMPARE(module.history.averageCount(), 0);
        module.parseData(frame(4000, 2000));
        QCOMPARE(module.history.averageCount(), 1);
        module.selectHistoryCapture(0);
        QCOMPARE(module.displayedSamplingRate, 1000);
        QCOMPARE(module.config->requestedSamplingRate, 2000);
        QCOMPARE(module.config->realSamplingRate, 2000);
        module.resumeLive();
        QCOMPARE(module.displayedSamplingRate, 2000);
        module.updatePretrigger(25);
        QCOMPARE(module.history.averageCount(), 0);
        module.parseData(frame(1000, 2000));
        module.updateMemoryCustomLength(200);
        QCOMPARE(module.history.averageCount(), 0);
        module.parseData(frame(1000, 2000));
        module.updateResolution(8);
        QCOMPARE(module.history.averageCount(), 0);
        module.parseData(frame(100, 2000, 8));
        module.updateChannelsEnable(1);
        QCOMPARE(module.history.averageCount(), 0);
        module.parseData(frame(100, 2000, 8, 1));
        module.updateTimebase(0.002f);
        QCOMPARE(module.history.averageCount(), 0);
        module.updateTriggerMode(ScopeTriggerMode::TRIG_SINGLE);
        module.parseData(frame(100, 2000, 8, 1));
        QVERIFY(!module.samplingActive);
        const int singleCaptureCount = module.history.size();
        module.parseData(frame(200, 2000, 8, 1));
        QCOMPARE(module.history.size(), singleCaptureCount);
        module.resumeLive();
        QVERIFY(module.samplingActive);
        QCOMPARE(module.config->triggerMode, ScopeTriggerMode::TRIG_SINGLE);
        module.parseData(frame(200, 2000, 8, 1));
        QCOMPARE(module.history.size(), singleCaptureCount + 1);
        QVERIFY(!module.samplingActive);
        module.selectHistoryCapture(0);
        module.startModule();
        QVERIFY(!module.browsingHistory);
        QVERIFY(module.samplingActive);
        module.stopModule();
        delete module.scpWindow;
        module.scpWindow = nullptr;
    }
};

QTEST_MAIN(ScopeTest)
#include "scope_test.moc"
