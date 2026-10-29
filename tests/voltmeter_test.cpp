#include <QtTest>
#include <QToolButton>
#include "modules/voltmeter/voltmeter.h"

class VoltmeterTest : public QObject
{
    Q_OBJECT

    static WidgetButtons *buttons(VoltmeterWindow &window, const char *name)
    {
        return window.findChild<WidgetButtons *>(QString::fromLatin1(name));
    }

    static void click(WidgetButtons *control, int index)
    {
        control->setChecked(true, index);
        control->clicked(index);
    }

    static bool hasNumericWrite(const QSignalSpy &writes, const QByteArray &feature, int value)
    {
        QByteArray command = Commands::SCOPE + ":" + feature + " ";
        for (int shift = 0; shift < 32; shift += 8)
            command.append(char((value >> shift) & 0xff));
        command.append(';');
        for (const QList<QVariant> &arguments : writes) {
            if (arguments.at(0).toByteArray() == command)
                return true;
        }
        return false;
    }

private slots:
    void normalConsolidatedChannelColors()
    {
        VoltmeterConfig config;
        VoltmeterWindow window(&config);
        const auto verifyColors = [&window]() {
            const auto series = window.consolidatedChart->getChart()->series();
            for (int i = 0; i < MAX_VOLTMETER_CHANNELS; ++i) {
                const QString color = Graphics::getChannelColor(i);
                auto *trace = qobject_cast<QXYSeries *>(series.at(i));
                QVERIFY(trace);
                QCOMPARE(trace->pen().color(), QColor(color));
                auto *label = window.consolidatedValueLabels.at(i)->findChild<QLabel *>("label_name");
                QVERIFY(label);
                QCOMPARE(label->text(), "CH" + QString::number(i + 1));
                QCOMPARE(label->styleSheet(), "QLabel{color:" + color + ";}");
                auto *value = window.consolidatedValueLabels.at(i)->findChild<QLabel *>("label_value");
                QVERIFY(value);
                QCOMPARE(value->styleSheet(), label->styleSheet());
            }
        };
        window.restoreGUIAfterStartup();
        click(buttons(window, "buttonView"), 1);
        verifyColors();

        QStringList inputs[MAX_VOLTMETER_CHANNELS] = {{"PA0", "PA1"}, {"PB0"}};
        int selectors[MAX_VOLTMETER_CHANNELS] = {};
        window.setInputCapabilities(inputs, selectors, 2);
        click(buttons(window, "voltmeterAcquisitionMode"), 1);
        ChannelData reading;
        reading.voltage = 1.5;
        window.showLogicalReading(1, reading, 0, "PA1", true);
        click(buttons(window, "voltmeterAcquisitionMode"), 0);
        verifyColors();
        click(buttons(window, "buttonView"), 0);
        click(buttons(window, "buttonView"), 1);
        verifyColors();
    }

    void consolidatedHistoryControls()
    {
        VoltmeterConfig config;
        VoltmeterWindow window(&config);
        auto *dial = window.findChild<WidgetDialRange *>("voltmeterConsolidatedHistorySamples");
        QVERIFY(dial);
        auto *historySettings = window.findChild<QToolButton *>("voltmeterConsolidatedHistorySettings");
        auto *clearHistory = window.findChild<QToolButton *>("voltmeterConsolidatedClearHistory");
        QVERIFY(historySettings);
        QVERIFY(historySettings->menu());
        QVERIFY(clearHistory);
        QCOMPARE(historySettings->size(), QSize(26, 26));
        QCOMPARE(clearHistory->size(), QSize(26, 26));
        QVERIFY(!historySettings->icon().isNull());
        QVERIFY(!clearHistory->icon().isNull());
        dial->setRealValue(300);
        QCOMPARE(window.consolidatedHistorySize, 300);
        ChannelData readings[MAX_VOLTMETER_CHANNELS];
        readings[0].voltage = 1.5;
        readings[0].percent = 50;
        for (int i = 0; i < 350; ++i)
            window.showData(readings, 1);
        QCOMPARE(window.consolidatedHistory[0].size(), 300);
        QCOMPARE(window.consolidatedHistory[0].first().x(), 51.0);
        dial->setRealValue(100);
        QCOMPARE(window.consolidatedHistory[0].size(), 100);
        QCOMPARE(window.consolidatedHistory[0].first().x(), 251.0);
        const QByteArray savedHistorySize = dial->saveGeometry();
        dial->setRealValue(500);
        dial->restoreGeometry(savedHistorySize);
        window.restoreGUIAfterStartup();
        QCOMPARE(window.consolidatedHistorySize, 100);
        const bool readingHidden = window.consolidatedValueLabels[0]->isHidden();
        clearHistory->click();
        QCOMPARE(window.consolidatedSampleTime, 0.0);
        for (const auto &history : window.consolidatedHistory)
            QVERIFY(history.isEmpty());
        QCOMPARE(window.consolidatedValueLabels[0]->isHidden(), readingHidden);
        window.showData(readings, 1);
        QCOMPARE(window.consolidatedHistory[0].size(), 1);
        QCOMPARE(window.consolidatedHistory[0].first().x(), 1.0);

        QStringList inputs[MAX_VOLTMETER_CHANNELS] = {{"PA0", "PA1"}};
        int selectors[MAX_VOLTMETER_CHANNELS] = {};
        window.setInputCapabilities(inputs, selectors, 1);
        click(buttons(window, "voltmeterAcquisitionMode"), 1);
        dial->setRealValue(300);
        for (int i = 0; i < 350; ++i) {
            window.showLogicalReading(0, readings[0], 0, "PA0", true);
            window.showLogicalReading(1, readings[0], 0, "PA1", false);
        }
        QCOMPARE(window.consolidatedHistory[0].size(), 300);
        QCOMPARE(window.consolidatedHistory[1].size(), 300);
        clearHistory->click();
        QVERIFY(window.consolidatedHistory[0].isEmpty());
        QVERIFY(window.consolidatedHistory[1].isEmpty());
        QVERIFY(!window.consolidatedTraceVisible[0]);
        QVERIFY(!window.consolidatedTraceVisible[1]);
        window.showLogicalReading(1, readings[0], 0, "PA1", true);
        QCOMPARE(window.consolidatedHistory[1].size(), 1);
        QVERIFY(window.consolidatedTraceVisible[1]);
    }

    void consolidatedHistoryClearsAcrossAcquisitionModes()
    {
        VoltmeterConfig config;
        VoltmeterWindow window(&config);
        window.restoreGUIAfterStartup();
        QStringList inputs[MAX_VOLTMETER_CHANNELS] = {{"PA0", "PA1"}};
        int selectors[MAX_VOLTMETER_CHANNELS] = {};
        window.setInputCapabilities(inputs, selectors, 1);
        buttons(window, "ChannelvoltmeterEnable")->restoreGeometry("3");

        ChannelData readings[MAX_VOLTMETER_CHANNELS];
        readings[0].voltage = 1.5;
        readings[1].voltage = 2.5;
        window.showData(readings, 2);
        QVERIFY(!window.consolidatedHistory[0].isEmpty());
        QVERIFY(!window.consolidatedHistory[1].isEmpty());

        click(buttons(window, "voltmeterAcquisitionMode"), 1);
        for (const auto &history : window.consolidatedHistory)
            QVERIFY(history.isEmpty());

        window.showLogicalReading(1, readings[1], 0, "PA1", true);
        QVERIFY(!window.consolidatedHistory[1].isEmpty());
        click(buttons(window, "voltmeterAcquisitionMode"), 0);
        for (const auto &history : window.consolidatedHistory)
            QVERIFY(history.isEmpty());
    }

    void pendingCaptureDoesNotCrossAcquisitionModes()
    {
        Comms communication;
        QObject::disconnect(&communication, &Comms::dataWrite, nullptr, nullptr);
        WidgetControlModule control;
        control.setStatus(ModuleStatus::STOP);
        Voltmeter module;
        module.comm = &communication;
        Commands commands;
        module.cmd = &commands;
        ModuleDockWidget dock;
        module.setDockWidgetWindow(&dock);
        module.setModuleControlWidget(&control);

        auto *spec = static_cast<VoltmeterSpec *>(module.getSpecification());
        spec->availableInputs[0] = {"PA0"};
        spec->availableInputSlots = 1;
        module.inputCapabilitiesReceived = true;
        module.voltWindow->setInputCapabilities(spec->availableInputs,
                                                spec->currentInputSelectors, 1);
        click(buttons(*module.voltWindow, "voltmeterAcquisitionMode"), 1);
        ChannelData reading;
        reading.voltage = 1.5;
        module.voltWindow->showLogicalReading(0, reading, 0, "PA0", true);
        QVERIFY(!module.voltWindow->consolidatedHistory[0].isEmpty());

        module.samplesToTakeTotal = 1;
        module.isReferenceMeasured = false;
        module.activeCaptureIsMultimode = true;
        click(buttons(*module.voltWindow, "voltmeterAcquisitionMode"), 0);
        QVERIFY(module.voltWindow->consolidatedHistory[0].isEmpty());

        Measurement staleReading(MeasurementType::MEAN, 0);
        staleReading.setLabel("Mean");
        staleReading.setValue(1.0);
        module.updateMeasurement({&staleReading});

        QVERIFY(module.voltWindow->consolidatedHistory[0].isEmpty());
        QVERIFY(module.logicalRawVoltage[0].isEmpty());

        click(buttons(*module.voltWindow, "voltmeterAcquisitionMode"), 1);
        module.isReferenceMeasured = false;
        module.activeCaptureIsMultimode = false;
        module.updateMeasurement({&staleReading});
        QVERIFY(module.voltWindow->consolidatedHistory[0].isEmpty());
        QVERIFY(module.logicalRawVoltage[0].isEmpty());

        delete module.measCalc;
        delete module.voltmeterData;
        delete module.voltWindow;
        delete module.config;
        delete spec;
        qDeleteAll(module.voltMeasList);
    }

    void initTestCase()
    {
        Graphics::setTheme(0);
    }

    void configurationRoundTrip()
    {
        VoltmeterConfig source;
        source.selectedInputNames[0] = {"PA0", "PA1"};
        source.selectedInputNames[2] = {"PC0"};
        source.multimode = true;
        source.channelMask = 5;
        source.averaging = 8;
        source.mainsFrequency = 60;
        source.mainsPeriods = 8;
        source.samplingRate = 50000;
        source.mode = VoltmeterMode::FAST;

        VoltmeterConfig restored;
        restored.parse(source.serialize());
        QVERIFY(restored.hasSavedSettings);
        QVERIFY(restored.hasSavedInputSelections);
        QCOMPARE(restored.selectedInputNames[0], source.selectedInputNames[0]);
        QVERIFY(restored.selectedInputNames[1].isEmpty());
        QCOMPARE(restored.selectedInputNames[2], source.selectedInputNames[2]);
        QVERIFY(restored.multimode);
        QCOMPARE(restored.channelMask, 5);
        QCOMPARE(restored.averaging, 8);
        QCOMPARE(restored.mainsFrequency, 60);
        QCOMPARE(restored.mainsPeriods, 8);
        QCOMPARE(restored.samplingRate, 50000);
        QCOMPARE(restored.mode, VoltmeterMode::FAST);

        QByteArray truncated = source.serialize();
        truncated.chop(1);
        restored.parse(truncated);
        QVERIFY(!restored.hasSavedSettings);
        QVERIFY(!restored.hasSavedInputSelections);
    }

    void legacyConfiguration()
    {
        QByteArray legacy;
        QDataStream stream(&legacy, QIODevice::WriteOnly);
        stream << quint32(0x564D4346) << quint16(1);
        stream << quint8(1) << QString("PA0") << quint8(0) << quint8(0) << quint8(0);
        VoltmeterConfig config;
        config.parse(legacy);
        QVERIFY(config.hasSavedInputSelections);
        QVERIFY(!config.hasSavedSettings);
        QCOMPARE(config.selectedInputNames[0], QStringList{"PA0"});
        QCOMPARE(config.captureSampleCount(), 200);
    }

    void sampleCounts()
    {
        VoltmeterConfig config;
        const int expected[2][2][4] = {
            {{100, 200, 400, 800}, {1000, 2000, 4000, 8000}},
            {{83, 167, 333, 667}, {833, 1667, 3333, 6667}}
        };
        int frequencyIndex = 0;
        for (int frequency : {50, 60}) {
            int rateIndex = 0;
            for (int rate : {5000, 50000}) {
                int periodIndex = 0;
                for (int periods : {1, 2, 4, 8}) {
                    config.mainsFrequency = frequency;
                    config.samplingRate = rate;
                    config.mainsPeriods = periods;
                    QCOMPARE(config.captureSampleCount(), expected[frequencyIndex][rateIndex][periodIndex++]);
                    const double duration = double(config.captureSampleCount()) / rate;
                    QVERIFY(qAbs(duration - double(periods) / frequency) <= 0.5 / rate);
                }
                ++rateIndex;
            }
            ++frequencyIndex;
        }
    }

    void selectionsSurviveStartupModeSwitchAndSession()
    {
        VoltmeterConfig config;
        config.hasSavedInputSelections = true;
        config.selectedInputNames[0] = {"PA1"};
        config.selectedInputNames[2] = {"PC1"};
        VoltmeterWindow window(&config);
        window.restoreGUIAfterStartup();
        QCOMPARE(config.selectedInputNames[0], QStringList{"PA1"});
        QCOMPARE(config.selectedInputNames[2], QStringList{"PC1"});

        QStringList inputs[MAX_VOLTMETER_CHANNELS] = {
            {"PA0", "PA1", "Vref"}, {"PB0", "PB1"}, {"PC0", "PC1"}, {"PD0", "PD1"}
        };
        int selectors[MAX_VOLTMETER_CHANNELS] = {};
        window.setInputCapabilities(inputs, selectors, MAX_VOLTMETER_CHANNELS);
        QCOMPARE(window.selectedInputNames(0), QStringList{"PA1"});
        QVERIFY(window.selectedInputNames(1).isEmpty());
        QCOMPARE(window.selectedInputNames(2), QStringList{"PC1"});
        QVERIFY(window.selectedInputNames(3).isEmpty());

        buttons(window, "ChannelvoltmeterEnable")->restoreGeometry("9");
        click(buttons(window, "voltmeterAcquisitionMode"), 1);
        QCOMPARE(window.enabledChannelMask(), 5);
        click(buttons(window, "voltmeterAcquisitionMode"), 0);
        QCOMPARE(window.enabledChannelMask(), 9);
        click(buttons(window, "voltmeterAcquisitionMode"), 1);
        QCOMPARE(window.selectedInputNames(0), QStringList{"PA1"});
        QVERIFY(window.selectedInputNames(1).isEmpty());

        click(buttons(window, "voltmeterMainsFrequency"), 1);
        click(buttons(window, "voltmeterMainsPeriods"), 3);
        click(buttons(window, "voltmeterSamplingRate"), 1);
        window.saveSettingsToConfig();
        window.restoreGUIAfterStartup();
        window.setInputCapabilities(inputs, selectors, MAX_VOLTMETER_CHANNELS);
        QCOMPARE(window.selectedInputNames(0), QStringList{"PA1"});
        QVERIFY(window.selectedInputNames(1).isEmpty());

        VoltmeterConfig restored;
        restored.parse(config.serialize());
        VoltmeterWindow restoredWindow(&restored);
        restoredWindow.restoreGUIAfterStartup();
        restoredWindow.setInputCapabilities(inputs, selectors, MAX_VOLTMETER_CHANNELS);
        QVERIFY(restoredWindow.isMultimode());
        QCOMPARE(restoredWindow.selectedInputNames(0), QStringList{"PA1"});
        QVERIFY(restoredWindow.selectedInputNames(1).isEmpty());
        QCOMPARE(restoredWindow.selectedInputNames(2), QStringList{"PC1"});
        QCOMPARE(buttons(restoredWindow, "voltmeterMainsFrequency")->getSelectedIndex(), 1);
        QCOMPARE(buttons(restoredWindow, "voltmeterMainsPeriods")->getSelectedIndex(), 3);
        QCOMPARE(buttons(restoredWindow, "voltmeterSamplingRate")->getSelectedIndex(), 1);
        click(buttons(restoredWindow, "voltmeterAcquisitionMode"), 0);
        QCOMPARE(restoredWindow.enabledChannelMask(), 9);

        auto *inputRow = buttons(restoredWindow, "voltmeterInputs_CH1_row0");
        click(inputRow, 0);
        inputRow->setChecked(false, 1);
        inputRow->clicked(1);
        QCOMPARE(restored.selectedInputNames[0], QStringList{"PA0"});
        QVERIFY(restored.hasSavedInputSelections);
        restoredWindow.saveSettingsToConfig();
        restoredWindow.restoreGUIAfterStartup();
        restoredWindow.setInputCapabilities(inputs, selectors, MAX_VOLTMETER_CHANNELS);
        QCOMPARE(restoredWindow.selectedInputNames(0), QStringList{"PA0"});
        QVERIFY(restoredWindow.selectedInputNames(1).isEmpty());
    }

    void multirowSelectionLimit()
    {
        VoltmeterConfig config;
        config.hasSavedInputSelections = true;
        QStringList inputs[MAX_VOLTMETER_CHANNELS];
        for (int i = 0; i < 16; ++i)
            inputs[0].append("PA" + QString::number(i));
        config.selectedInputNames[0] = inputs[0].mid(8, 8);
        int selectors[MAX_VOLTMETER_CHANNELS] = {};
        VoltmeterWindow window(&config);
        window.setInputCapabilities(inputs, selectors, 1);
        QCOMPARE(window.selectedInputNames(0), inputs[0].mid(8, 8));
        QCOMPARE(window.selectedInputIndices(0).size(), MAX_VOLTMETER_INPUTS_PER_SLOT);
    }

    void memoryLimitsPeriodOptions()
    {
        VoltmeterConfig config;
        VoltmeterWindow window(&config);
        window.setSamplingMemoryLimit(40000);
        buttons(window, "ChannelvoltmeterEnable")->restoreGeometry("15");
        click(buttons(window, "voltmeterSamplingRate"), 1);
        auto *periods = buttons(window, "voltmeterMainsPeriods");
        click(periods, 3);
        QCOMPARE(periods->getSelectedIndex(), 2);
        QCOMPARE(config.mainsPeriods, 4);
        QVERIFY(!periods->findChild<QPushButton *>("pushButton_4")->isEnabled());
        QCOMPARE(config.captureSampleCount(), 4000);

        buttons(window, "ChannelvoltmeterEnable")->restoreGeometry("1");
        click(buttons(window, "voltmeterMainsFrequency"), 1);
        QVERIFY(periods->findChild<QPushButton *>("pushButton_4")->isEnabled());
        click(periods, 3);
        QCOMPARE(config.captureSampleCount(), 6667);

        buttons(window, "ChannelvoltmeterEnable")->restoreGeometry("15");
        click(buttons(window, "voltmeterMainsFrequency"), 0);
        QCOMPARE(config.mainsPeriods, 4);
        click(buttons(window, "voltmeterSamplingRate"), 0);
        QVERIFY(periods->findChild<QPushButton *>("pushButton_4")->isEnabled());
    }

    void startupAndPeriodicReferenceStaySeparateFromMultimode()
    {
        Comms communication;
        QObject::disconnect(&communication, &Comms::dataWrite, nullptr, nullptr);
        QSignalSpy writes(&communication, &Comms::dataWrite);
        WidgetControlModule control;
        control.setStatus(ModuleStatus::STOP);
        Voltmeter module;
        module.comm = &communication;
        Commands commands;
        module.cmd = &commands;
        ModuleDockWidget dock;
        module.setDockWidgetWindow(&dock);
        module.setModuleControlWidget(&control);
        auto *spec = static_cast<VoltmeterSpec *>(module.getSpecification());
        spec->maxADCChannels = 2;
        spec->memorySize = 40000;
        spec->Vref = 3300;
        spec->VrefCalibration = 1500;
        spec->availableInputSlots = 2;
        spec->availableInputs[0] = {"PA0", "PA1"};
        spec->availableInputs[1] = {"PB0", "PB1"};
        module.config->ADCresolution = 12;
        module.config->rangeMin = 0;
        module.config->rangeMax = 3300;
        module.config->hasSavedInputSelections = true;
        module.config->selectedInputNames[0] = {"PA1"};
        module.config->selectedInputNames[1] = {"PB1"};
        module.inputCapabilitiesReceived = true;
        module.voltWindow->setInputCapabilities(spec->availableInputs, spec->currentInputSelectors, 2);
        click(buttons(*module.voltWindow, "voltmeterAcquisitionMode"), 1);
        module.writeConfiguration();
        module.startModule();
        QVERIFY(module.isReferenceMeasured);
        QVERIFY(!module.activeCaptureIsMultimode);

        Measurement reference(MeasurementType::MEAN, 0);
        reference.setLabel("Mean");
        reference.setValue(1500.0 / 4095 * 3.3);
        module.updateMeasurement({&reference});
        QVERIFY(qAbs(module.getVdd() - 3300) < 0.001);
        QVERIFY(!module.isReferenceMeasured);
        QVERIFY(module.activeCaptureIsMultimode);
        QCOMPARE(module.activeCaptureSelectors[0], 1);
        QCOMPARE(module.activeCaptureSelectors[1], 1);

        module.samplesToTakeTotal = 1;
        Measurement channel0(MeasurementType::MEAN, 0);
        channel0.setLabel("Mean");
        channel0.setValue(3.0);
        Measurement channel1(MeasurementType::MEAN, 1);
        channel1.setLabel("Mean");
        channel1.setValue(1.0);
        module.updateMeasurement({&channel0, &channel1});
        QCOMPARE(module.logicalChannelData(0).voltage, 3.0);
        QCOMPARE(module.logicalChannelData(1).voltage, 1.0);
        QVERIFY(module.isReferenceMeasured);
        QVERIFY(!module.activeCaptureIsMultimode);
        module.updateMeasurement({&reference});
        QVERIFY(module.activeCaptureIsMultimode);
        QVERIFY(qAbs(module.getVdd() - 3300) < 0.001);

        reference.setValue(0);
        module.setVDDSampling();
        module.updateMeasurement({&reference});
        QVERIFY(module.isReferenceMeasured);
        QVERIFY(qIsFinite(module.getVdd()));
        QVERIFY(!writes.isEmpty());

        VoltmeterConfig newSession;
        newSession.multimode = true;
        newSession.selectedInputNames[0] = {"PA0"};
        newSession.samplingRate = 50000;
        newSession.mainsFrequency = 60;
        newSession.mainsPeriods = 8;
        newSession.mode = VoltmeterMode::FAST;
        module.parseConfiguration(newSession.serialize());
        module.writeConfiguration();
        QCOMPARE(module.voltWindow->selectedInputNames(0), QStringList{"PA0"});
        QVERIFY(module.voltWindow->selectedInputNames(1).isEmpty());
        QCOMPARE(module.config->mainsPeriods, 8);
        QCOMPARE(module.config->mainsFrequency, 60);
        QCOMPARE(module.config->samplingRate, 50000);
        module.startModule();
        QVERIFY(module.isReferenceMeasured);
        reference.setValue(1500.0 / 4095 * 3.3);
        writes.clear();
        module.updateMeasurement({&reference});
        QVERIFY(hasNumericWrite(writes, Commands::SAMPLING_FREQ, 50000));
        QVERIFY(hasNumericWrite(writes, Commands::DATA_LENGTH, 6667));

        for (int frequency : {50, 60}) {
            for (int rate : {5000, 50000}) {
                for (int periods : {1, 2, 4, 8}) {
                    module.config->mainsFrequency = frequency;
                    module.config->samplingRate = rate;
                    module.config->mainsPeriods = periods;
                    for (bool multimode : {false, true}) {
                        writes.clear();
                        if (multimode)
                            module.configureMultimodeCapture();
                        else
                            module.setDefaultSampling();
                        QVERIFY(hasNumericWrite(writes, Commands::SAMPLING_FREQ, rate));
                        QVERIFY(hasNumericWrite(writes, Commands::DATA_LENGTH, module.config->captureSampleCount()));
                    }
                }
            }
        }
        writes.clear();
        module.setVDDSampling();
        QVERIFY(hasNumericWrite(writes, Commands::SAMPLING_FREQ, 5000));
        module.stopModule();
        delete module.measCalc;
        delete module.voltmeterData;
        delete module.voltWindow;
        delete module.config;
        delete spec;
        qDeleteAll(module.voltMeasList);
    }
};

QTEST_MAIN(VoltmeterTest)
#include "voltmeter_test.moc"
