#include <QtTest>
#include <QTabWidget>
#include "modules/arbgenerator/arbgenerator.h"

class TestArbGenerator : public ArbGenerator
{
public:
    explicit TestArbGenerator(Commands *commands) { cmd = commands; }
};

class ArbGeneratorTest : public QObject
{
    Q_OBJECT

    static void setSpecification(ArbGeneratorSpec &spec)
    {
        spec.maxSamplingRate = 2000000;
        spec.periphClockFrequency = 84000000;
        spec.generatorBufferSize = 8192;
        spec.DACResolution = 12;
        spec.maxDACChannels = 2;
        spec.maxPWMChannels = 2;
        spec.periphPWMClockFrequency = 84000000;
        spec.rangeMin = 0;
        spec.rangeMax = 3.3;
        spec.AVddReal = 3.3;
    }

    static void configureSweep(ArbGenPanelSettings &settings, int channel, float min, float max,
                               float time = 0.1f, int scale = 0, int mode = 0)
    {
        settings.dialFreqSweepMin[channel]->setRealValue(min);
        settings.dialFreqSweepMax[channel]->setRealValue(max);
        settings.dialSweepTime[channel]->setRealValue(time);
        settings.buttonsSweepScale[channel]->clickedInternal(scale);
        settings.buttonsSweepMode[channel]->clickedInternal(mode);
        settings.buttonSWSweepEnable[channel]->clickedInternal(1);
    }

    static bool advance(ArbGenSweepController &controller)
    {
        return QMetaObject::invokeMethod(&controller, "timerCallback", Qt::DirectConnection);
    }

    static QList<QByteArray> samplingWrites(const QSignalSpy &writes)
    {
        QList<QByteArray> result;
        const auto prefix = Commands::SIGNAL_GENERATOR + ":" + Commands::SAMPLING_FREQ + " ";
        for (const auto &arguments : writes) {
            const auto data = arguments.at(0).toByteArray();
            if (data.startsWith(prefix))
                result.append(data.mid(prefix.size(), 4));
        }
        return result;
    }

private slots:
    void initTestCase()
    {
        Graphics::setTheme(0);
    }

    void sweepProfiles_data()
    {
        QTest::addColumn<int>("scale");
        QTest::addColumn<int>("mode");
        QTest::addColumn<qreal>("maxFreq");
        QTest::addColumn<QList<qreal>>("expected");
        QTest::newRow("linear-saw") << 0 << 0 << qreal(30) << QList<qreal>{10, 20, 30, 10, 20};
        QTest::newRow("exponential-saw") << 1 << 0 << qreal(90) << QList<qreal>{10, 30, 90, 10, 30};
        QTest::newRow("linear-symmetric") << 0 << 1 << qreal(30) << QList<qreal>{10, 20, 30, 20, 10};
        QTest::newRow("exponential-symmetric") << 1 << 1 << qreal(90) << QList<qreal>{10, 30, 90, 30, 10};
        QTest::newRow("constant") << 1 << 1 << qreal(10) << QList<qreal>{10, 10, 10, 10, 10};
    }

    void sweepProfiles()
    {
        QFETCH(int, scale);
        QFETCH(int, mode);
        QFETCH(qreal, maxFreq);
        QFETCH(QList<qreal>, expected);
        ArbGenSweepController controller;
        QVERIFY(controller.setParameters(10, maxFreq, 0.1, static_cast<SweepScale>(scale),
                                         static_cast<SweepMode>(mode)));
        QCOMPARE(controller.findChild<QTimer *>()->interval(), 50);
        QSignalSpy frequencies(&controller, &ArbGenSweepController::updateSweepFrequency);
        for (const auto value : expected) {
            QVERIFY(advance(controller));
            QVERIFY(qAbs(frequencies.last().at(0).toDouble() - value) < 1e-10);
        }
    }

    void timingAndUnchangedParameters()
    {
        ArbGenSweepController controller;
        QVERIFY(controller.setParameters(100, 10000, 1, SweepScale::EXPONENTIAL, SweepMode::SYMMETRIC));
        QSignalSpy frequencies(&controller, &ArbGenSweepController::updateSweepFrequency);
        QVERIFY(advance(controller));
        for (int i = 0; i < 10; ++i)
            QVERIFY(advance(controller));
        QVERIFY(qAbs(frequencies.last().at(0).toDouble() - 1000) < 1e-9);
        QVERIFY(controller.setParameters(100, 10000, 1, SweepScale::EXPONENTIAL, SweepMode::SYMMETRIC));
        for (int i = 0; i < 10; ++i)
            QVERIFY(advance(controller));
        QCOMPARE(frequencies.last().at(0).toDouble(), 10000.0);
        for (int i = 0; i < 20; ++i)
            QVERIFY(advance(controller));
        QCOMPARE(frequencies.last().at(0).toDouble(), 100.0);
        QCOMPARE((frequencies.size() - 1) * controller.findChild<QTimer *>()->interval(), 2000);

        controller.startTimer();
        QVERIFY(controller.isActive());
        controller.stopTimer();
        QVERIFY(!controller.isActive());
        controller.startTimer();
        QCOMPARE(frequencies.last().at(0).toDouble(), 100.0);
        controller.stopTimer();
        QVERIFY(controller.setParameters(qreal(0.01f), 1000000, 60));
        QVERIFY(advance(controller));
        QCOMPARE(frequencies.last().at(0).toDouble(), qreal(0.01f));
    }

    void invalidParametersStopSweep()
    {
        ArbGenSweepController controller;
        QVERIFY(controller.setParameters(10, 30, 0.1));
        controller.startTimer();
        QTest::ignoreMessage(QtWarningMsg, "Invalid generator sweep parameters: 30 10 0.1");
        QVERIFY(!controller.setParameters(30, 10, 0.1));
        QVERIFY(!controller.isActive());
        QTest::ignoreMessage(QtWarningMsg, "Cannot start generator sweep without valid parameters");
        controller.startTimer();
        QVERIFY(!controller.isActive());
        QTest::ignoreMessage(QtWarningMsg, "Invalid generator sweep parameters: 0 10 0.1");
        QVERIFY(!controller.setParameters(0, 10, 0.1));
    }

    void verticalSweepTabAndSyncControls_data()
    {
        QTest::addColumn<bool>("pwm");
        QTest::newRow("DAC") << false;
        QTest::newRow("PWM") << true;
    }

    void verticalSweepTabAndSyncControls()
    {
        QFETCH(bool, pwm);
        ArbGeneratorConfig config;
        ArbGeneratorSpec spec;
        setSpecification(spec);
        ArbGeneratorWindow window(&config, pwm);
        window.setSpecification(&spec);
        window.restoreGUIAfterStartup();
        auto *settings = window.findChild<ArbGenPanelSettings *>();
        QVERIFY(settings);
        settings->buttonsEnable->clickedInternal(1);
        auto *tabs = settings->channelTabs->findChild<QTabWidget *>();
        QVERIFY(tabs);
        QCOMPARE(tabs->tabText(0), "CH 1");
        QCOMPARE(tabs->tabText(1), "CH 2");
        QCOMPARE(tabs->tabText(MAX_ARB_CHANNELS_NUM), "Sweep");
        QCOMPARE(tabs->currentIndex(), 0);
        QVERIFY(tabs->isTabVisible(MAX_ARB_CHANNELS_NUM));
        QVERIFY(!tabs->isTabVisible(2));
        QVERIFY(!tabs->isTabVisible(3));
        auto *scroll = window.findChild<QScrollArea *>("arbGenSweepScroll");
        QVERIFY(scroll);
        for (int channel = 0; channel < 2; ++channel) {
            auto *min = settings->dialFreqSweepMin[channel];
            auto *max = settings->dialFreqSweepMax[channel];
            auto *time = settings->dialSweepTime[channel];
            QVERIFY(scroll->isAncestorOf(min));
            auto *layout = qobject_cast<QVBoxLayout *>(min->parentWidget()->layout());
            QVERIFY(layout);
            QCOMPARE(layout->direction(), QBoxLayout::TopToBottom);
            QVERIFY(layout->indexOf(min) < layout->indexOf(max));
            QVERIFY(layout->indexOf(max) < layout->indexOf(time));
        }
        configureSweep(*settings, 0, 100, 1000);
        configureSweep(*settings, 1, 200, 2000, 0.2f, 1, 1);
        settings->buttonSweepSyncWithCH1->clickedInternal(1);
        QVERIFY(settings->swSyncWithCH1[1]->isCheckedRight());
        QVERIFY(settings->channelSyncWithCH1[1]);
        QVERIFY(!settings->dialFreqSweepMin[1]->isEnabled());
        QCOMPARE(settings->dialFreqCh[1]->getRealValue(), settings->dialFreqCh[0]->getRealValue());
        QMetaObject::invokeMethod(settings->swSyncWithCH1[1], "setLeft", Qt::DirectConnection);
        QCOMPARE(settings->buttonSweepSyncWithCH1->getSelectedIndex(), 0);
        QVERIFY(settings->dialFreqSweepMin[1]->isEnabled());
        QCOMPARE(settings->dialFreqSweepMin[1]->getRealValue(), 200.0f);
        QCOMPARE(settings->dialFreqSweepMax[1]->getRealValue(), 2000.0f);
        QCOMPARE(settings->buttonsSweepScale[1]->getSelectedIndex(), 1);
        settings->dialFreqSweepMin[0]->setRealValue(5000);
        QCOMPARE(settings->dialFreqSweepMax[0]->getRealValue(), 5000.0f);
        QCOMPARE(settings->dialFreqSweepMax[1]->getRealValue(), 2000.0f);
        settings->dialFreqSweepMax[0]->setRealValue(10);
        QCOMPARE(settings->dialFreqSweepMin[0]->getRealValue(), 10.0f);
    }

    void independentSweepsAndStopping_data()
    {
        verticalSweepTabAndSyncControls_data();
    }

    void independentSweepsAndStopping()
    {
        QFETCH(bool, pwm);
        ArbGeneratorConfig config;
        ArbGeneratorSpec spec;
        setSpecification(spec);
        ArbGeneratorWindow window(&config, pwm);
        window.setSpecification(&spec);
        window.restoreGUIAfterStartup();
        auto *settings = window.findChild<ArbGenPanelSettings *>();
        settings->buttonsEnable->clickedInternal(1);
        configureSweep(*settings, 0, 10, 30);
        configureSweep(*settings, 1, 100, 900, 0.1f, 1, 1);
        auto *ch1 = window.findChild<ArbGenSweepController *>("arbGenSweepController1");
        auto *ch2 = window.findChild<ArbGenSweepController *>("arbGenSweepController2");
        QVERIFY(ch1);
        QVERIFY(ch2);
        window.setGeneratorRuning();
        QCOMPARE(window.getFrequency(0), 10.0);
        QCOMPARE(window.getFrequency(1), 100.0);
        QVERIFY(advance(*ch1));
        QCOMPARE(window.getFrequency(0), 20.0);
        QCOMPARE(window.getFrequency(1), 100.0);
        QVERIFY(advance(*ch2));
        QCOMPARE(window.getFrequency(1), 300.0);
        settings->dialSweepTime[1]->setRealValue(0.2f);
        QCOMPARE(window.getFrequency(0), 20.0);
        QVERIFY(advance(*ch1));
        QCOMPARE(window.getFrequency(0), 30.0);

        settings->buttonSweepSyncWithCH1->clickedInternal(1);
        QVERIFY(!ch2->isActive());
        QVERIFY(advance(*ch1));
        QCOMPARE(window.getFrequency(0), window.getFrequency(1));
        QVERIFY(settings->isSweepEnabled[1]);
        settings->buttonSWSweepEnable[0]->clickedInternal(0);
        QVERIFY(!ch1->isActive());
        QVERIFY(!ch2->isActive());
        settings->dialFreqCh[0]->setRealValue(15);
        QCOMPARE(window.getFrequency(1), 15.0);
        QCOMPARE(settings->dialFreqSweepMax[1]->getRealValue(), 900.0f);
        settings->buttonSWSweepEnable[0]->clickedInternal(1);
        QVERIFY(ch1->isActive());
        QVERIFY(!ch2->isActive());
        settings->buttonSweepSyncWithCH1->clickedInternal(0);
        QVERIFY(ch2->isActive());
        settings->buttonSWSweepEnable[0]->clickedInternal(0);
        QVERIFY(!ch1->isActive());
        QVERIFY(ch2->isActive());
        window.setGeneratorStopped();
        QVERIFY(!ch1->isActive());
        QVERIFY(!ch2->isActive());
        QSignalSpy updates(&window, &ArbGeneratorWindow::updateFrequency);
        QTest::qWait(120);
        QCOMPARE(updates.size(), 0);
    }

    void sweepOffDoesNotOverwriteManualFrequencies()
    {
        ArbGeneratorConfig config;
        ArbGeneratorSpec spec;
        setSpecification(spec);
        spec.maxDACChannels = 1;
        ArbGeneratorWindow window(&config);
        window.setSpecification(&spec);
        window.restoreGUIAfterStartup();
        auto *settings = window.findChild<ArbGenPanelSettings *>();
        QCOMPARE(settings->getMaxNumChannels(), 1);
        settings->dialFreqCh[0]->setRealValue(1234);
        settings->dialFreqSweepMax[0]->setRealValue(9999);
        settings->restoreGUI();
        QCOMPARE(window.getFrequency(0), 1234.0);
        QVERIFY(settings->dialFreqCh[0]->isEnabled());
        QVERIFY(!settings->isSweepActive(1));
        configureSweep(*settings, 0, 100, 1000);
        settings->buttonSWSweepEnable[0]->restoreGeometry("1");
        settings->dialFreqCh[0]->setRealValue(1234, true);
        settings->dialFreqSweepMax[0]->setRealValue(9999, true);
        settings->restoreGUI();
        QCOMPARE(window.getFrequency(0), 1234.0);
        QVERIFY(settings->dialFreqCh[0]->isEnabled());
    }

    void sessionRestoresSweepSettingsAndLegacyNames()
    {
        Commands commands;
        TestArbGenerator module(&commands);
        WidgetControlModule control;
        control.setStatus(ModuleStatus::STOP);
        module.setModuleControlWidget(&control);
        auto *spec = static_cast<ArbGeneratorSpec *>(module.getSpecification());
        setSpecification(*spec);
        auto *window = qobject_cast<ArbGeneratorWindow *>(module.getWidget());
        window->setSpecification(spec);
        window->restoreGUIAfterStartup();
        auto *settings = window->findChild<ArbGenPanelSettings *>();
        settings->buttonsEnable->clickedInternal(1);
        configureSweep(*settings, 0, 10, 100, 2, 1, 1);
        configureSweep(*settings, 1, 30, 300, 3, 0, 0);
        settings->setChannelFrequencySync(1, true);
        auto *tabs = settings->channelTabs->findChild<QTabWidget *>();
        tabs->setCurrentIndex(MAX_ARB_CHANNELS_NUM);
        QMap<QString, QByteArray> saved;
        module.saveGeometry(saved);
        const auto prefix = module.getModuleName();
        QVERIFY(saved.contains(prefix + "arbGenSweepbtn"));
        QVERIFY(saved.contains(prefix + "arbGenfreqSweepMin"));
        QVERIFY(saved.contains(prefix + "arbGenfreqSweepMax"));
        QVERIFY(saved.contains(prefix + "arbGenfreqSweeptime"));
        settings->setChannelFrequencySync(1, false);
        configureSweep(*settings, 0, 500, 1000);
        configureSweep(*settings, 1, 600, 900);
        module.restoreGeometry(saved);
        module.writeConfiguration();
        QCOMPARE(settings->dialFreqSweepMin[0]->getRealValue(), 10.0f);
        QCOMPARE(settings->dialFreqSweepMax[1]->getRealValue(), 300.0f);
        QCOMPARE(settings->buttonsSweepScale[0]->getSelectedIndex(), 1);
        QCOMPARE(settings->buttonsSweepMode[0]->getSelectedIndex(), 1);
        QCOMPARE(tabs->currentIndex(), MAX_ARB_CHANNELS_NUM);
        QVERIFY(settings->channelSyncWithCH1[1]);
        QVERIFY(!settings->dialFreqSweepMin[1]->isEnabled());
        delete window;
    }

    void synchronizedSweepUsesSharedFirmwareUpdates()
    {
        Comms communication;
        QObject::disconnect(&communication, &Comms::dataWrite, nullptr, nullptr);
        QSignalSpy writes(&communication, &Comms::dataWrite);
        Commands commands;
        TestArbGenerator module(&commands);
        WidgetControlModule control;
        control.setStatus(ModuleStatus::STOP);
        module.setModuleControlWidget(&control);
        module.setComms(&communication);
        QByteArray config = Commands::CONFIG;
        QDataStream out(&config, QIODevice::Append);
        for (int value : {0, 2000000, 84000000, 8192, 12, 2, 0, 3300, 3300})
            out << value;
        out.writeRawData("PA4_PA5_", 8);
        out << quint32(0) << quint32(0) << quint32(0) << quint32(0);
        module.parseData(config);
        auto *window = qobject_cast<ArbGeneratorWindow *>(module.getWidget());
        window->restoreGUIAfterStartup();
        auto *settings = window->findChild<ArbGenPanelSettings *>();
        settings->buttonsEnable->clickedInternal(1);
        configureSweep(*settings, 0, 100, 1000);
        settings->setChannelFrequencySync(1, true);
        const auto phase = settings->dialPhaseCh[1]->getRealValue();
        settings->buttonsGenerate->clickedInternal(0);
        QCOMPARE(window->getGeneratorDACData()->at(0).size(), window->getGeneratorDACData()->at(1).size());
        module.parseData(Commands::CMD_GEN_OK);
        auto *controller = window->findChild<ArbGenSweepController *>("arbGenSweepController1");
        for (int i = 0; i < 5; ++i) {
            writes.clear();
            QVERIFY(advance(*controller));
            const auto updates = samplingWrites(writes);
            QCOMPARE(updates.size(), 1);
            QCOMPARE(quint8(updates.first().at(0)), quint8(100));
            QCOMPARE(window->getFrequency(0), window->getFrequency(1));
            QCOMPARE(settings->dialPhaseCh[1]->getRealValue(), phase);
        }
        window->setGeneratorStopped();
        delete window;
    }
};

QTEST_MAIN(ArbGeneratorTest)
#include "arbgenerator_test.moc"
