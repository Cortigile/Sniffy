#include <QtTest>
#include <QJsonDocument>
#include <QJsonArray>
#include "gui/widgetdialrange.h"
#include "gui/widgetbuttons.h"
#include "gui/widgetselection.h"
#include "gui/widgettab.h"
#include <QLabel>
#include <QTableWidget>
#include <QTabWidget>
#include <QTabBar>
#include <QMouseEvent>
#include <QFontDatabase>
#include <QFileInfo>
#include <QLineEdit>
#include <QPushButton>
#include <QComboBox>
#include <QGraphicsLineItem>
#include <QGraphicsSimpleTextItem>
#include <QtCharts/QLegend>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QLogValueAxis>
#include <QtCharts/QValueAxis>
#include <QtEndian>
#include <cmath>
#include "modules/frequencyanalysis/frequencyanalysis.h"
#include "modules/frequencyanalysis/frequencyanalysiswindow.h"
#include "modules/scope/scopespec.h"
#include "modules/arbgenerator/arbgeneratorspec.h"
#include "modules/arbgenerator/signalcreator.h"
#include "modules/arbgenerator/gencommons.h"
#include "modules/counter/countertablowfreq.h"
#include "modules/voltmeter/voltmeterspec.h"

namespace {
class InstrumentStub final : public AbstractModule
{
public:
    explicit InstrumentStub(AbstractSpecification *spec)
    {
        moduleSpecification = spec;
        moduleName = QStringLiteral("Instrument stub");
    }
    QWidget *getWidget() override { return &widget; }
    void parseData(QByteArray) override {}
    void startModule() override {}
    void stopModule() override {}
    void writeConfiguration() override {}
    void parseConfiguration(QByteArray) override {}
    QByteArray getConfiguration() override { return {}; }
private:
    QWidget widget;
};

QByteArray scopeSpecification(int channels)
{
    QByteArray data;
    QDataStream out(&data, QIODevice::WriteOnly);
    out << quint32(1) << quint32(2000000) << quint32(4000000)
        << quint32(32768) << quint32(channels) << quint32(3300) << quint32(1200);
    for (int i = 0; i < channels; ++i)
        data.append("PA" + QByteArray::number(i) + " ");
    QDataStream masks(&data, QIODevice::Append);
    masks << quint32(3) << quint32(0) << quint32(0) << quint32(0);
    return data;
}

QByteArray generatorSpecification()
{
    QByteArray data;
    QDataStream out(&data, QIODevice::WriteOnly);
    out << quint32(2) << quint32(8400000) << quint32(168000000) << quint32(8192)
        << quint32(12) << quint32(1) << qint32(0) << quint32(3300) << quint32(3300);
    data.append("PA4 ");
    QDataStream masks(&data, QIODevice::Append);
    masks << quint32(16) << quint32(0) << quint32(0) << quint32(0);
    return data;
}

QByteArray inputCatalogResponse()
{
    QByteArray data("INP_");
    data.append(char(0));
    data.append(char(1));
    return data + "/PA0:PA1:PA2:PC2:Temp:Vref:PB0:PF14/PA0:PA1:PA2:PC3:Vref:PF15/";
}

struct Rig
{
    Comms comm;
    ScopeSpec adcSpec;
    ArbGeneratorSpec dacSpec;
    InstrumentStub adc{&adcSpec};
    InstrumentStub dac{&dacSpec};
    WidgetControlModule control;
    ModuleDockWidget dock;
    FrequencyAnalysisModule module{&adc, &dac};

    explicit Rig(int channels = 2, bool loadInputs = true)
    {
        QObject::disconnect(&comm, &Comms::dataWrite, nullptr, nullptr);
        control.setStatus(ModuleStatus::STOP);
        module.setDockWidgetWindow(&dock);
        module.setModuleControlWidget(&control);
        adcSpec.parseSpecification(scopeSpecification(channels));
        dacSpec.parseSpecification(generatorSpecification());
        adc.setModuleConfigured();
        dac.setModuleConfigured();
        module.setComms(&comm);
        if (channels >= 2 && loadInputs)
            module.parseInstrumentData(Commands::SCOPE, inputCatalogResponse());
    }
};

QByteArray readback(const QByteArray &header, quint32 rate)
{
    QByteArray bytes = header;
    const quint32 encoded = qToBigEndian(rate);
    bytes.append(reinterpret_cast<const char *>(&encoded), 4);
    return bytes;
}

bool hasCommand(const QSignalSpy &spy, const QByteArray &command)
{
    for (const auto &write : spy)
        if (write[0].toByteArray() == command)
            return true;
    return false;
}

QByteArray binaryCommand(const QByteArray &command, quint32 value)
{
    const quint32 encoded = qToLittleEndian(value);
    return command + QByteArray(reinterpret_cast<const char *>(&encoded), 4) + ";";
}
}

class FrequencyAnalysisTest : public QObject
{
    Q_OBJECT
    static FrequencyAnalysisWindow *view(Rig &rig)
    {
        return qobject_cast<FrequencyAnalysisWindow *>(rig.module.getWidget());
    }

    static void completeReadback(Rig &rig, quint32 rate)
    {
        rig.module.parseInstrumentData(Commands::SIGNAL_GENERATOR,
                                       readback(Commands::CMD_GEN_SIGNAL_REAL_SAMPLING_FREQ_CH1, rate));
        rig.module.parseInstrumentData(Commands::SIGNAL_GENERATOR,
                                       readback(Commands::CMD_GEN_SIGNAL_REAL_SAMPLING_FREQ_CH2, rate));
    }

    static void setInputs(Rig &rig, int reference, int response)
    {
        auto document = QJsonDocument::fromJson(rig.module.getConfiguration()).object();
        auto configuration = document.value("settings").toObject();
        configuration["referenceInput"] = reference;
        configuration["responseInput"] = response;
        document["settings"] = configuration;
        rig.module.parseConfiguration(QJsonDocument(document).toJson(QJsonDocument::Compact));
    }

    static QByteArray frame(int samples, int rate, double frequency, bool clipped = false)
    {
        constexpr double pi = 3.14159265358979323846;
        QByteArray data;
        for (int ch = 1; ch <= 2; ++ch) {
            QDataStream out(&data, QIODevice::Append);
            out << quint32(0x4f53435f) << qint32(rate) << quint8(12)
                << qint32(samples * 2) << qint16(0) << qint16(3300)
                << quint8(0) << quint8(ch) << quint8(2);
            for (int i = 0; i < samples; ++i) {
                const double angle = 2 * pi * frequency * i / rate;
                const double voltage = 1.65 + (ch == 1 ? 0.5 : 0.25)
                    * std::cos(angle + (ch == 1 ? 0 : -pi / 4));
                const int code = clipped && i == 0 ? 4095 : qRound(voltage / 3.3 * 4095);
                data.append(char(code & 255));
                data.append(char(code >> 8));
            }
        }
        return data + "S_OK";
    }

private slots:
    void initTestCase()
    {
        const auto theme = Graphics::setTheme(0);
        qApp->setStyleSheet(theme->getAppGlobalStyle());
#ifdef Q_OS_WIN
        const QString fontPath = qEnvironmentVariable("WINDIR") + "\\Fonts\\segoeui.ttf";
        if (QFileInfo::exists(fontPath)) {
            const int fontId = QFontDatabase::addApplicationFont(fontPath);
            if (fontId >= 0)
                QApplication::setFont(QFont(QFontDatabase::applicationFontFamilies(fontId).first(), 9));
        }
#endif
    }

    void capabilitiesAndResources()
    {
        Rig ready;
        QCOMPARE(ready.adcSpec.maxADCChannels, 2);
        QCOMPARE(ready.dacSpec.maxDACChannels, 1);
        QVERIFY(ready.adc.isAvailable());
        QVERIFY(ready.dac.isAvailable());
        QVERIFY(ready.module.isAvailable());
        QCOMPARE(ready.module.getResources(), 3);
        QCOMPARE(ready.module.getGpioMaskA(), quint32(19));
        Rig limited(1);
        QVERIFY(!limited.module.isAvailable());
    }

    void pinCatalogParsingIsTransactional()
    {
        AdcInputCatalog catalog;
        QString error;
        QVERIFY2(catalog.parse(inputCatalogResponse(), error), qPrintable(error));
        QCOMPARE(catalog.inputs.size(), 2);
        QCOMPARE(catalog.defaults, QVector<int>({0, 1}));
        const auto inputs = catalog.inputs;
        const auto defaults = catalog.defaults;
        for (const QByteArray &bad : {QByteArray("INP_/PA0/"), inputCatalogResponse().chopped(1),
                                     inputCatalogResponse() + "extra", QByteArray("INP_\xff/PA0/")}) {
            QVERIFY(!catalog.parse(bad, error));
            QVERIFY(!error.isEmpty());
            QCOMPARE(catalog.inputs, inputs);
            QCOMPARE(catalog.defaults, defaults);
        }
        for (const auto &internal : {"Temp", "temperature", "Vref", " Vbat ", ""})
            QVERIFY(!AdcInputCatalog::isExternal(QString::fromLatin1(internal)));
        QVERIFY(AdcInputCatalog::isExternal("PF14"));
    }

    void pinCatalogIsSharedWithVoltmeter_data()
    {
        QTest::addColumn<QByteArray>("response");
        QTest::newRow("two-slots") << inputCatalogResponse();
        QByteArray fourSlots("INP_");
        for (const char selector : {char(3), char(2), char(1), char(0)})
            fourSlots.append(selector);
        for (int slot = 0; slot < 4; ++slot)
            fourSlots.append("/A2:A3:A4:A5:PC2:Temp:Vref:Vbat");
        fourSlots.append('/');
        QTest::newRow("four-shared-adc-slots") << fourSlots;
        QTest::newRow("without-header") << fourSlots.mid(4);
        QTest::newRow("semicolon-terminated") << fourSlots + ";";
        QTest::newRow("one-slot") << QByteArray("INP_") + char(0) + "/PA0:Temp:Vref/";
    }

    void pinCatalogIsSharedWithVoltmeter()
    {
        QFETCH(QByteArray, response);
        AdcInputCatalog catalog;
        VoltmeterSpec voltmeter;
        QString error;
        QVERIFY2(catalog.parse(response, error), qPrintable(error));
        QVERIFY(voltmeter.parseAvailableInputs(response));
        QVERIFY(voltmeter.inputCapabilitiesAvailable);
        QCOMPARE(voltmeter.availableInputSlots, catalog.inputs.size());
        for (int slot = 0; slot < MAX_VOLTMETER_CHANNELS; ++slot) {
            QCOMPARE(voltmeter.availableInputs[slot],
                     slot < catalog.inputs.size() ? catalog.inputs[slot] : QStringList{});
            QCOMPARE(voltmeter.currentInputSelectors[slot],
                     slot < catalog.defaults.size() ? catalog.defaults[slot] : 0);
        }
        QVERIFY(catalog.inputs.first().contains("Temp"));
        QVERIFY(catalog.inputs.first().contains("Vref"));
        for (const auto &bad : {response.chopped(2), response + "extra",
                               QByteArray("INP_") + char(0) + "/PA0::Temp/",
                               QByteArray("INP_") + QByteArray(5, char(0)) + "/PA0/PA0/PA0/PA0/PA0/"}) {
            QVERIFY(!voltmeter.parseAvailableInputs(bad));
            QCOMPARE(voltmeter.availableInputSlots, catalog.inputs.size());
            QVERIFY(voltmeter.inputCapabilitiesAvailable);
            for (int slot = 0; slot < catalog.inputs.size(); ++slot) {
                QCOMPARE(voltmeter.availableInputs[slot], catalog.inputs[slot]);
                QCOMPARE(voltmeter.currentInputSelectors[slot], catalog.defaults[slot]);
            }
        }
        QVERIFY(voltmeter.parseAvailableInputs(QByteArray("INP_") + char(0) + "/PA0:Temp:Vref/"));
        QCOMPARE(voltmeter.availableInputSlots, 1);
        for (int slot = 1; slot < MAX_VOLTMETER_CHANNELS; ++slot) {
            QVERIFY(voltmeter.availableInputs[slot].isEmpty());
            QCOMPARE(voltmeter.currentInputSelectors[slot], 0);
        }
    }

    void discoveryAndPinChoices()
    {
        Rig rig(2, false);
        QSignalSpy writes(&rig.comm, &Comms::dataWrite);
        rig.module.widgetControlClicked(ModuleStatus::STOP);
        QVERIFY(hasCommand(writes, "OSCP:INP?;"));
        rig.module.startSweep();
        QVERIFY(!rig.module.isRunning());
        QVERIFY(!hasCommand(writes, "GENS:STRT;"));
        rig.module.parseInstrumentData(Commands::SCOPE, inputCatalogResponse());
        auto *reference = view(rig)->findChild<WidgetSelection *>("referenceInput");
        auto *response = view(rig)->findChild<WidgetSelection *>("responseInput");
        QVERIFY(reference && response);
        QCOMPARE(reference->count(), 7);
        QCOMPARE(response->count(), 6);
        QCOMPARE(reference->getSelectedValue(), -1.0);
        QCOMPARE(response->getSelectedValue(), -1.0);
        setInputs(rig, 3, 5);
        QCOMPARE(view(rig)->settings().referenceInput, 3);
        QCOMPARE(view(rig)->settings().responseInput, 5);
        QVERIFY(rig.module.getGpioMaskC() & (1U << 2));
        const auto resources = ResourceSet::fromModule(&rig.module);
        QVERIFY(resources.pins.contains("PC2"));
        QVERIFY(resources.pins.contains("PF15"));
        QVERIFY(!resources.pins.contains("PA0"));
    }

    void selectedPinConflictsBlockOutput()
    {
        Rig rig;
        rig.module.widgetControlClicked(ModuleStatus::STOP);
        rig.module.setResourceCheck([](const ResourceSet &candidate) {
            return candidate.gpioC & (1U << 2) ? QString("PC2 is already in use") : QString{};
        });
        setInputs(rig, 3, -1);
        QCOMPARE(view(rig)->settings().referenceInput, 3);
        QCOMPARE(rig.module.getGpioMaskC(), quint32(0));
        QSignalSpy writes(&rig.comm, &Comms::dataWrite);
        rig.module.startSweep();
        QVERIFY(!rig.module.isRunning());
        QVERIFY(writes.isEmpty());
        QVERIFY(view(rig)->state().value("status_error").toBool());
        view(rig)->findChild<WidgetSelection *>("referenceInput")->setSelected(0);
        QVERIFY(!view(rig)->state().value("status_error").toBool());
    }

    void namedPinResourcesCoverOtherPorts()
    {
        ResourceSet first, second;
        first.pins.insert("PF14");
        second.pins.insert("PF14");
        QVERIFY(ResourceSet::collide(first, second));
        ResourceManager manager;
        manager.reserve(first);
        QVERIFY(manager.conflicts(second));
        const auto restored = ResourceSet::fromJson(first.toJson());
        QCOMPARE(restored.pins, first.pins);
        manager.release(first);
        QVERIFY(!manager.conflicts(second));
    }

    void constrainedBestFitPreservesLegacyBehavior()
    {
        for (const double frequency : {1.0, 123.45, 100000.0, 1000000.0}) {
            const int legacy = SignalCreator::calculateSignalLength(
                MemoryLength::BEST_FIT, 0, 8192, frequency, 8400000, 168000000);
            QCOMPARE(legacy, SignalCreator::calculateSignalLength(
                                MemoryLength::BEST_FIT, 0, 8192, frequency, 8400000, 168000000, 2));
            const int constrained = SignalCreator::calculateSignalLength(
                MemoryLength::BEST_FIT, 0, 8192, frequency, 8400000, 168000000, 4);
            QVERIFY(constrained >= 4);
            QVERIFY(constrained <= 4096);
            QVERIFY(frequency * constrained <= 8400000);
        }
    }

    void highDacRateUsesUnsignedPacking()
    {
        Comms comm;
        QObject::disconnect(&comm, &Comms::dataWrite, nullptr, nullptr);
        GenCommons transport(Commands::SIGNAL_GENERATOR, &comm, 8400000);
        QSignalSpy writes(&comm, &Comms::dataWrite);
        transport.setSamplingFrequency(0, 8400000);
        QCOMPARE(writes.size(), 1);
        const QByteArray command = writes[0][0].toByteArray();
        QCOMPARE(command.left(10), QByteArray("GENS:FREQ "));
        QCOMPARE(command.size(), 15);
        QCOMPARE(qFromLittleEndian<quint32>(command.constData() + 10), (quint32(8400000) << 8) | 1U);
        QCOMPARE(command.back(), ';');
    }

    void sessionRoundTrip()
    {
        FrequencyAnalysisWindow original;
        const QByteArray saved = original.serialize();
        FrequencyAnalysisWindow restored;
        QString error;
        QVERIFY2(restored.restore(saved, error), qPrintable(error));
        QCOMPARE(restored.serialize(), saved);
        QVERIFY(!restored.restore("{broken", error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(restored.serialize(), saved);
        QVERIFY(!restored.state().value("running").toBool());
    }

    void legacySessionDropsSkewAndAddsAcquisitionDefaults()
    {
        FrequencyAnalysisWindow window;
        auto document = QJsonDocument::fromJson(window.serialize()).object();
        document["version"] = 1;
        auto configuration = document.value("settings").toObject();
        configuration.remove("samplesPerPeriod");
        configuration.remove("referenceInput");
        configuration.remove("responseInput");
        configuration["channelSkewSeconds"] = 37e-9;
        document["settings"] = configuration;
        QString error;
        QVERIFY2(window.restore(QJsonDocument(document).toJson(), error), qPrintable(error));
        QCOMPARE(window.settings().referenceInput, -1);
        QCOMPARE(window.settings().responseInput, -1);
        QCOMPARE(window.settings().channelSkewSeconds, 0.0);
        QCOMPARE(QJsonDocument::fromJson(window.serialize()).object().value("version").toInt(), 3);
        document["version"] = 2;
        configuration["samplesPerPeriod"] = 128;
        configuration["referenceInput"] = -1;
        configuration["responseInput"] = -1;
        document["settings"] = configuration;
        QVERIFY(window.restore(QJsonDocument(document).toJson(), error));
        QVERIFY(!QJsonDocument::fromJson(window.serialize()).object().value("settings").toObject()
                     .contains("samplesPerPeriod"));
    }

    void standardControlsAndVisibleEstimates()
    {
        Rig rig;
        auto *window = view(rig);
        window->findChild<WidgetDialRange *>("startFrequency")->setRealValue(10);
        window->findChild<WidgetDialRange *>("periods")->setRealValue(32);
        window->resize(1250, 850);
        window->show();
        QApplication::processEvents();
        auto *tabs = window->findChild<widgetTab *>("frequencyAnalysisSettings");
        auto *controls = window->findChild<QWidget *>("frequencyAnalysisControls");
        QVERIFY(tabs && controls);
        auto *tabWidget = tabs->findChild<QTabWidget *>();
        QVERIFY(tabWidget);
        QCOMPARE(tabWidget->count(), 4);
        QCOMPARE(tabWidget->tabText(1), QString("Acquisition"));
        QCOMPARE(tabWidget->tabText(2), QString("Connection"));
        QCOMPARE(tabWidget->tabText(3), QString("View"));
        auto *viewSettings = window->findChild<QWidget *>("viewSettings");
        QVERIFY(viewSettings);
        QVERIFY(tabWidget->widget(3)->isAncestorOf(viewSettings));
        auto *resultTabs = window->findChild<QTabWidget *>("frequencyAnalysisResultTabs");
        QVERIFY(resultTabs);
        for (const auto &name : {"unwrapPhase", "showCursors", "combinedPlot"}) {
            auto *control = window->findChild<WidgetButtons *>(name);
            QVERIFY(control);
            QVERIFY(viewSettings->isAncestorOf(control));
            QVERIFY(!resultTabs->isAncestorOf(control));
        }
        for (const auto &name : {"startSweep", "cancelSweep", "exportCsv"}) {
            auto *action = window->findChild<WidgetButtons *>(name);
            QVERIFY(action);
            QVERIFY(controls->isAncestorOf(action));
            QVERIFY(!tabs->isAncestorOf(action));
            QVERIFY(action->minimumHeight() >= 36);
            const QRect bounds(action->mapTo(controls, QPoint(0, 0)), action->size());
            QVERIFY(controls->rect().contains(bounds));
        }
        QTRY_VERIFY(window->state().value("timing_estimate").toObject().value("available").toBool());
        const auto timing = window->state().value("timing_estimate").toObject();
        QVERIFY(qAbs(timing.value("lowest_frequency_seconds").toDouble() - 13.1) < 1e-8);
        QVERIFY(timing.value("total_seconds").toDouble() >= 13.1);
        QVERIFY(!window->findChild<QWidget *>("samplesPerPeriod"));
        auto *estimate = window->findChild<QLabel *>("frequencyAnalysisTimingEstimate");
        QVERIFY(estimate && estimate->isVisible());
        QVERIFY(!tabs->isAncestorOf(estimate));
        QVERIFY(!QPixmap(Graphics::getCommonPath() + "icon_freq_anlys.png").isNull());
        const QString snapshot = QString::fromLocal8Bit(qgetenv("SNIFFY_FREQ_UI_SNAPSHOT"));
        if (!snapshot.isEmpty())
            QVERIFY(window->grab().save(snapshot));
    }

    void viewControlsStayInteractive_data()
    {
        QTest::addColumn<bool>("running");
        QTest::newRow("idle") << false;
        QTest::newRow("sweeping") << true;
    }

    void viewControlsStayInteractive()
    {
        QFETCH(bool, running);
        Rig rig;
        auto *window = view(rig);
        window->resize(1250, 850);
        window->show();
        FrequencyAnalysis::Result result;
        result.gainDb = -6;
        result.phaseDegrees = 170;
        window->appendResult(100, 100, 1, 3200, 1024, result);
        result.phaseDegrees = -170;
        window->appendResult(1000, 1000, 1, 32000, 1024, result);
        const auto measurements = window->state().value("results");
        const auto settings = window->settings();
        QSignalSpy starts(window, &FrequencyAnalysisWindow::startRequested);
        window->setRunning(running);
        auto *settingsTabs = window->findChild<widgetTab *>("frequencyAnalysisSettings");
        auto *tabs = settingsTabs->findChild<QTabWidget *>();
        QVERIFY(tabs && tabs->isEnabled());
        for (int tab = 0; tab < 3; ++tab)
            QCOMPARE(settingsTabs->getLayout(tab)->parentWidget()->isEnabled(), !running);
        QCOMPARE(window->findChild<WidgetDialRange *>("startFrequency")->isEnabled(), !running);
        QCOMPARE(window->findChild<WidgetDialRange *>("periods")->isEnabled(), !running);
        QCOMPARE(window->findChild<WidgetSelection *>("referenceInput")->isEnabled(), !running);
        QApplication::processEvents();
        QTest::mouseClick(tabs->tabBar(), Qt::LeftButton, Qt::NoModifier,
                          tabs->tabBar()->tabRect(3).center());
        QCOMPARE(tabs->currentIndex(), 3);
        auto *viewSettings = window->findChild<QWidget *>("viewSettings");
        QVERIFY(viewSettings && viewSettings->isEnabled());
        for (const auto &name : {"unwrapPhase", "showCursors", "combinedPlot"}) {
            auto *control = window->findChild<WidgetButtons *>(name);
            QVERIFY(control && control->isVisible() && control->isEnabled());
            const QRect bounds(control->mapTo(viewSettings, QPoint(0, 0)), control->size());
            QVERIFY(viewSettings->rect().contains(bounds));
            auto *button = control->findChild<QPushButton *>("pushButton_1");
            QVERIFY(button && button->isVisible() && button->isEnabled());
            const bool checked = control->isChecked(0);
            QTest::mouseClick(button, Qt::LeftButton);
            QCOMPARE(control->isChecked(0), !checked);
        }
        auto *gain = window->findChild<QChartView *>("frequencyGainPlot");
        auto *phase = window->findChild<QChartView *>("frequencyPhasePlot");
        QVERIFY(gain && phase);
        QCOMPARE(gain->chart()->series().size(), 2);
        QVERIFY(phase->isHidden());
        auto *phaseSeries = qobject_cast<QLineSeries *>(gain->chart()->series().at(1));
        QVERIFY(phaseSeries);
        QCOMPARE(phaseSeries->at(1).y(), -170.0);
        QVERIFY(!window->state().value("cursors").toObject().value("enabled").toBool());
        QCOMPARE(window->state().value("results"), measurements);
        QCOMPARE(window->settings().periods, settings.periods);
        QCOMPARE(window->settings().referenceInput, settings.referenceInput);
        QCOMPARE(starts.count(), 0);
        QApplication::processEvents();
        const QString snapshot = QString::fromLocal8Bit(qgetenv("SNIFFY_FREQ_VIEW_UI_SNAPSHOT"));
        if (!snapshot.isEmpty())
            QVERIFY(window->grab().save(snapshot));
        window->setRunning(false);
        for (int tab = 0; tab < 4; ++tab)
            QVERIFY(settingsTabs->getLayout(tab)->parentWidget()->isEnabled());
        FrequencyAnalysisWindow restored;
        QString error;
        const auto saved = window->serialize();
        QVERIFY2(restored.restore(saved, error), qPrintable(error));
        QCOMPARE(restored.serialize(), saved);
    }

    void acquisitionSettingsRoundTrip()
    {
        FrequencyAnalysisWindow original;
        auto *frequency = original.findChild<WidgetButtons *>("frequencySpacing");
        auto *amplitude = original.findChild<WidgetButtons *>("amplitudeSpacing");
        QVERIFY(frequency && amplitude);
        frequency->clickedInternal(frequency->getText(0).startsWith("Lin") ? 0 : 1);
        amplitude->clickedInternal(amplitude->getText(0).startsWith("Log") ? 0 : 1);
        original.findChild<WidgetDialRange *>("startFrequency")->setRealValue(3.25f);
        original.findChild<WidgetDialRange *>("stopFrequency")->setRealValue(9876.5f);
        original.findChild<WidgetDialRange *>("startAmplitude")->setRealValue(0.4f);
        original.findChild<WidgetDialRange *>("stopAmplitude")->setRealValue(1.2f);
        original.findChild<WidgetDialRange *>("averages")->setRealValue(7);
        original.findChild<WidgetDialRange *>("periods")->setRealValue(48);
        original.findChild<WidgetDialRange *>("settlingSeconds")->setRealValue(0.15f);
        original.findChild<WidgetButtons *>("allowAliasing")->setChecked(true, 0);
        QVERIFY(!original.findChild<QWidget *>("channelSkewNs"));
        FrequencyAnalysisWindow restored;
        QString error;
        const auto saved = original.serialize();
        QVERIFY2(restored.restore(saved, error), qPrintable(error));
        QCOMPARE(restored.serialize(), saved);
        const auto settings = restored.settings();
        QCOMPARE(settings.frequencySpacing, FrequencyAnalysis::Spacing::Linear);
        QCOMPARE(settings.amplitudeSpacing, FrequencyAnalysis::Spacing::Logarithmic);
        QCOMPARE(settings.averages, 7);
        QCOMPARE(settings.periods, 48);
        QCOMPARE(settings.settlingMs, 150);
        QVERIFY(settings.allowAliasing);
        QCOMPARE(settings.channelSkewSeconds, 0.0);
        restored.setRunning(true);
        QVERIFY(!restored.restore(saved, error));
        QCOMPARE(restored.serialize(), saved);
    }

    void plotsAndTableUseActualFrequency()
    {
        FrequencyAnalysisWindow window;
        FrequencyAnalysis::Result result;
        result.referenceRms = 0.3;
        result.responseRms = 0.15;
        result.gainDb = -6.0206;
        result.phaseDegrees = 170;
        result.coherence = 0.99;
        window.appendResult(100, 99.5, 1, 3200, 1024, result);
        result.phaseDegrees = -170;
        window.appendResult(200, 202.5, 1, 6400, 1024, result);
        auto *gain = window.findChild<QChartView *>("frequencyGainPlot");
        auto *phase = window.findChild<QChartView *>("frequencyPhasePlot");
        auto *table = window.findChild<QTableWidget *>("frequencyAnalysisResults");
        QVERIFY(gain && phase && table);
        auto *gainSeries = qobject_cast<QLineSeries *>(gain->chart()->series().first());
        auto *phaseSeries = qobject_cast<QLineSeries *>(phase->chart()->series().first());
        QVERIFY(gainSeries && phaseSeries);
        QCOMPARE(gainSeries->at(0).x(), 99.5);
        QCOMPARE(gainSeries->at(1).x(), 202.5);
        QCOMPARE(phaseSeries->at(1).y(), 190.0);
        QCOMPARE(table->rowCount(), 2);
        QCOMPARE(table->item(1, 1)->text().toDouble(), 202.5);
        auto *unwrap = window.findChild<WidgetButtons *>("unwrapPhase");
        QVERIFY(unwrap);
        unwrap->setChecked(false, 0);
        unwrap->clickedInternal(0);
        QCOMPARE(phaseSeries->at(1).y(), -170.0);
        const auto results = window.state().value("results").toArray();
        QCOMPARE(results[1].toObject().value("phase_unwrapped_degrees").toDouble(), 190.0);
    }

    void realisticControlLimitsAndAliasingDefault()
    {
        Rig rig;
        auto *window = view(rig);
        QVERIFY(FrequencyAnalysis::Settings().allowAliasing);
        QVERIFY(window->settings().allowAliasing);
        QVERIFY(window->findChild<WidgetButtons *>("allowAliasing")->isChecked(0));
        QVERIFY(!window->findChild<QLabel *>("aliasHint")->isHidden());
        for (const auto &name : {"startAmplitude", "stopAmplitude", "offset"}) {
            auto *dial = window->findChild<WidgetDialRange *>(name);
            QVERIFY(dial);
            QVERIFY(qAbs(dial->getRangeMax() - rig.dacSpec.rangeMax) < 1e-6);
            dial->setRealValue(100);
            QCOMPARE(dial->getRealValue(), dial->getRangeMax());
        }
        rig.dacSpec.rangeMax = 2.5;
        rig.module.refreshAvailability();
        for (const auto &name : {"startAmplitude", "stopAmplitude", "offset"})
            QCOMPARE(window->findChild<WidgetDialRange *>(name)->getRangeMax(), 2.5f);
        QCOMPARE(window->findChild<WidgetDialRange *>("offset")->getRangeMin(), 0.0f);
        QVERIFY(window->findChild<QLabel *>("frequencyAnalysisStatus")->text().contains("limited"));

        auto *periods = window->findChild<WidgetDialRange *>("periods");
        QCOMPARE(periods->getRangeMax(), 512.0f);
        periods->setRealValue(513);
        QCOMPARE(window->settings().periods, 512);
        auto *settling = window->findChild<WidgetDialRange *>("settlingSeconds");
        QVERIFY(settling);
        QCOMPARE(settling->getRangeMin(), 0.0f);
        QCOMPARE(settling->getRangeMax(), 10.0f);
        settling->setRealValue(11);
        QCOMPARE(window->settings().settlingMs, 10000);
        QCOMPARE(settling->findChild<QLabel *>("label_unit")->text(), QString("s"));
        const auto *units = settling->findChild<QComboBox *>();
        QVERIFY(units);
        for (int i = 0; i < units->count(); ++i)
            QVERIFY(units->itemText(i) == "ms" || units->itemText(i) == "s");
        settling->setRealValue(0.001f);
        QCOMPARE(window->settings().settlingMs, 1);

        window->findChild<WidgetButtons *>("allowAliasing")->findChild<QPushButton *>("pushButton_1")->click();
        QVERIFY(!window->settings().allowAliasing);
        const auto saved = window->serialize();
        FrequencyAnalysisWindow restored;
        QString error;
        QVERIFY2(restored.restore(saved, error), qPrintable(error));
        QVERIFY(!restored.settings().allowAliasing);
        QCOMPARE(restored.settings().settlingMs, 1);
    }

    void rejectsSessionsOutsideNewLimits()
    {
        FrequencyAnalysisWindow window;
        const auto saved = window.serialize();
        for (const auto &entry : QList<QPair<QString, double>>{
                 {"startAmplitude", 3.4}, {"stopAmplitude", 3.4}, {"offset", 3.4},
                 {"offset", -0.1}, {"periods", 513}, {"settlingMs", 10001}}) {
            auto root = QJsonDocument::fromJson(saved).object();
            auto settings = root.value("settings").toObject();
            settings[entry.first] = entry.second;
            root["settings"] = settings;
            QString error;
            QVERIFY(!window.restore(QJsonDocument(root).toJson(), error));
            QVERIFY(!error.isEmpty());
            QCOMPARE(window.serialize(), saved);
        }
    }

    void timingUpdatesKeepLabelsVisible()
    {
        FrequencyAnalysisWindow window;
        window.setAcquisitionLimits(1000000, 512);
        window.setInputCapabilities({{"PA0"}, {"PA1"}}, {0, 0}, {"PA0", "PA1"}, "PA4");
        QTRY_VERIFY(window.state().value("timing_estimate").toObject().value("available").toBool());
        auto *estimate = window.findChild<QLabel *>("frequencyAnalysisTimingEstimate");
        auto *density = window.findChild<QLabel *>("frequencyAnalysisDensityHint");
        auto *start = window.findChild<WidgetButtons *>("startSweep");
        QVERIFY(estimate && density && start);
        QVERIFY(!density->isHidden());
        const QString previous = estimate->text();
        window.setRunning(true);
        FrequencyAnalysis::TimingParameters timing;
        timing.learned = true;
        timing.captureOverheadSeconds = 0.1;
        window.setTimingParameters(timing);
        QCOMPARE(estimate->text(), previous);
        QVERIFY(!density->isHidden());
        QVERIFY(window.state().value("timing_estimate").toObject().value("available").toBool());
        QTRY_VERIFY(estimate->text() != previous);
        QVERIFY(!density->isHidden());
        window.setRunning(false);
        QVERIFY(start->isEnabled());
        window.setAcquisitionLimits(1000000, 512);
        QVERIFY(start->isEnabled());
        window.findChild<WidgetDialRange *>("periods")->setRealValue(80);
        QVERIFY(!start->isEnabled());
        QVERIFY(!estimate->text().contains("Updating"));
        QTRY_VERIFY(window.state().value("timing_estimate").toObject().value("available").toBool() == false
                    && !window.state().value("timing_estimate").toObject().value("pending").toBool());
        QVERIFY(density->isHidden());
        QVERIFY(estimate->text().contains("unavailable"));
    }

    void combinedPlotRetainsResultsAndColors()
    {
        FrequencyAnalysisWindow window;
        window.resize(1250, 850);
        window.show();
        auto *gain = window.findChild<QChartView *>("frequencyGainPlot");
        auto *phase = window.findChild<QChartView *>("frequencyPhasePlot");
        auto *toggle = window.findChild<WidgetButtons *>("combinedPlot");
        QVERIFY(gain && phase && toggle);
        auto *gainSeries = qobject_cast<QLineSeries *>(gain->chart()->series().first());
        auto *phaseSeries = qobject_cast<QLineSeries *>(phase->chart()->series().first());
        QVERIFY(gainSeries && phaseSeries);
        const QColor color(Graphics::getChannelColor(0));
        QCOMPARE(gainSeries->pen().color(), color);
        QCOMPARE(phaseSeries->pen().color(), color);
        FrequencyAnalysis::Result result;
        result.gainDb = -6;
        result.phaseDegrees = 170;
        window.appendResult(100, 99.5, 1, 3200, 1024, result);
        result.phaseDegrees = -170;
        window.appendResult(1000, 999.5, 1, 32000, 1024, result);
        const auto measured = window.state().value("results");
        const auto cursors = window.state().value("cursors");
        for (auto *plot : {gain, phase}) {
            int labels = 0;
            for (auto *item : plot->chart()->childItems()) {
                if (auto *label = dynamic_cast<QGraphicsSimpleTextItem *>(item)) {
                    if (label->text() == "A" || label->text() == "B") {
                        QCOMPARE(label->brush().color(), color);
                        ++labels;
                    }
                } else if (auto *line = dynamic_cast<QGraphicsLineItem *>(item)) {
                    if (line->pen().style() == Qt::DashLine)
                        QCOMPARE(line->pen().color(), color);
                }
            }
            QCOMPARE(labels, 2);
        }
        for (int i = 0; i < 4; ++i) {
            toggle->findChild<QPushButton *>("pushButton_1")->click();
            QApplication::processEvents();
            const bool combined = toggle->isChecked(0);
            QCOMPARE(phase->isHidden(), combined);
            QCOMPARE(gain->chart()->series().size(), combined ? 2 : 1);
            QCOMPARE(phaseSeries->chart(), combined ? gain->chart() : phase->chart());
            QCOMPARE(phaseSeries->pen().style(), combined ? Qt::DashLine : Qt::SolidLine);
            QCOMPARE(gain->chart()->legend()->isVisible(), combined);
            QCOMPARE(phaseSeries->at(1).y(), 190.0);
            QCOMPARE(window.state().value("results"), measured);
            QCOMPARE(window.state().value("cursors"), cursors);
            const auto phaseAxes = phaseSeries->attachedAxes();
            QCOMPARE(phaseAxes.size(), 2);
            for (auto *axis : phaseAxes)
                QCOMPARE(axis->alignment(), axis->orientation() == Qt::Horizontal ? Qt::AlignBottom
                    : (combined ? Qt::AlignRight : Qt::AlignLeft));
        }
        toggle->findChild<QPushButton *>("pushButton_1")->click();
        auto saved = window.serialize();
        FrequencyAnalysisWindow restored;
        QString error;
        QVERIFY2(restored.restore(saved, error), qPrintable(error));
        QCOMPARE(restored.serialize(), saved);
        auto *restoredGain = restored.findChild<QChartView *>("frequencyGainPlot");
        QCOMPARE(restoredGain->chart()->series().size(), 2);
        restored.clearResults();
        QCOMPARE(restoredGain->chart()->series().size(), 2);
        for (auto *series : restoredGain->chart()->series())
            QCOMPARE(series->attachedAxes().size(), 2);
        window.findChild<WidgetButtons *>("unwrapPhase")->findChild<QPushButton *>("pushButton_1")->click();
        QCOMPARE(phaseSeries->at(1).y(), -170.0);
        auto root = QJsonDocument::fromJson(saved).object();
        auto settings = root.value("settings").toObject();
        settings["combinedPlot"] = "invalid";
        root["settings"] = settings;
        QVERIFY(!restored.restore(QJsonDocument(root).toJson(), error));
        QCOMPARE(restored.serialize(), saved);
        settings.remove("combinedPlot");
        root["settings"] = settings;
        QVERIFY2(restored.restore(QJsonDocument(root).toJson(), error), qPrintable(error));
        QCOMPARE(restoredGain->chart()->series().size(), 1);
    }

    void draggingCursors_data()
    {
        QTest::addColumn<bool>("linear");
        QTest::addColumn<bool>("combined");
        QTest::newRow("logarithmic") << false << false;
        QTest::newRow("linear") << true << false;
        QTest::newRow("combined-logarithmic") << false << true;
        QTest::newRow("combined-linear") << true << true;
    }

    void draggingCursors()
    {
        QFETCH(bool, linear);
        QFETCH(bool, combined);
        FrequencyAnalysisWindow window;
        if (linear)
            window.findChild<WidgetButtons *>("frequencySpacing")->clickedInternal(1);
        window.clearResults();
        FrequencyAnalysis::Result result;
        result.referenceRms = 0.3;
        result.responseRms = 0.15;
        result.coherence = 1;
        for (const double frequency : {100.0, 1000.0, 10000.0, 100000.0}) {
            result.gainDb -= 5;
            result.phaseDegrees -= 10;
            window.appendResult(frequency, frequency, 1, 1000000, 1056, result);
        }
        window.resize(1250, 850);
        window.show();
        QApplication::processEvents();
        auto *gain = window.findChild<QChartView *>("frequencyGainPlot");
        auto *phase = window.findChild<QChartView *>("frequencyPhasePlot");
        QVERIFY(gain && phase);
        if (combined) {
            window.findChild<WidgetButtons *>("combinedPlot")->findChild<QPushButton *>("pushButton_1")->click();
            QApplication::processEvents();
            QVERIFY(phase->isHidden());
            QCOMPARE(gain->chart()->series().size(), 2);
        }
        for (auto *plot : {gain, phase}) {
            for (auto *axis : plot->chart()->axes())
                QVERIFY(axis->isMinorGridLineVisible());
            auto *horizontal = plot->chart()->axes(Qt::Horizontal).first();
            if (linear) {
                auto *axis = qobject_cast<QValueAxis *>(horizontal);
                QVERIFY(axis && axis->minorTickCount() > 0);
            } else {
                auto *axis = qobject_cast<QLogValueAxis *>(horizontal);
                QVERIFY(axis && axis->minorTickCount() > 0);
            }
        }
        const auto drag = [](QChartView *view, double startFrequency, double stopFrequency) {
            auto *chart = view->chart();
            const auto position = [&](double frequency) {
                QPointF point = chart->mapToPosition(QPointF(frequency, 0), chart->series().first());
                point.setY(chart->plotArea().center().y());
                return view->mapFromScene(chart->mapToScene(point));
            };
            const QPoint start = position(startFrequency), end = position(stopFrequency);
            QTest::mousePress(view->viewport(), Qt::LeftButton, Qt::NoModifier, start);
            QMouseEvent move(QEvent::MouseMove, QPointF(end),
                             QPointF(view->viewport()->mapToGlobal(end)), Qt::NoButton,
                             Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(view->viewport(), &move);
            QTest::mouseRelease(view->viewport(), Qt::LeftButton, Qt::NoModifier, end);
        };
        drag(gain, 100, linear ? 5500 : std::sqrt(1000.0 * 10000));
        auto cursors = window.state().value("cursors").toObject();
        const auto a = cursors.value("a").toObject();
        const double fa = a.value("frequency_hz").toDouble();
        QVERIFY(fa > 1000 && fa < 10000);
        const double fraction = linear ? (fa - 1000) / 9000 : std::log(fa / 1000) / std::log(10.0);
        QVERIFY(qAbs(a.value("gain_db").toDouble() - (-10 - 5 * fraction)) < 1e-8);
        QVERIFY(qAbs(a.value("phase_degrees").toDouble() - (-20 - 10 * fraction)) < 1e-8);
        QVERIFY(!cursors.value("snapped_to_measurements").toBool());
        drag(combined ? gain : phase, 100000, linear ? 55000 : std::sqrt(10000.0 * 100000));
        cursors = window.state().value("cursors").toObject();
        const double fb = cursors.value("b").toObject().value("frequency_hz").toDouble();
        QVERIFY(fb > 10000 && fb < 100000);
        QVERIFY(qAbs(cursors.value("delta").toObject().value("frequency_hz").toDouble() - (fb - fa)) < 1e-8);
        const QString difference = window.findChild<QLabel *>("frequencyCursorDifference")->text();
        QVERIFY(difference.contains("Hz") && difference.contains("dB") && difference.contains(QChar(0x00b0)));
    }

    void measurementsRetainPinProvenance()
    {
        Rig rig;
        auto *window = view(rig);
        setInputs(rig, 3, 5);
        window->setRunning(true);
        FrequencyAnalysis::Result result;
        result.referenceRms = 0.3;
        result.responseRms = 0.15;
        result.coherence = 1;
        window->appendResult(1000, 1000, 1, 32000, 1056, result);
        window->setRunning(false);
        auto *reference = window->findChild<WidgetSelection *>("referenceInput");
        QVERIFY(reference);
        reference->setSelected(0);
        const auto measurement = window->state().value("results").toArray().first().toObject();
        QCOMPARE(measurement.value("reference_pin").toString(), QString("PC2"));
        QCOMPARE(measurement.value("response_pin").toString(), QString("PF15"));
        QCOMPARE(measurement.value("dac_pin").toString(), QString("PA4"));
        QCOMPARE(window->settings().referenceInput, -1);
    }

    void noAutomaticSweepOnOpen()
    {
        Rig rig;
        QSignalSpy writes(&rig.comm, &Comms::dataWrite);
        rig.module.widgetControlClicked(ModuleStatus::STOP);
        QVERIFY(rig.module.isActive());
        QVERIFY(!rig.module.isRunning());
        QVERIFY(writes.isEmpty());
    }

    void uploadReadbackAndCancel()
    {
        Rig rig;
        rig.module.widgetControlClicked(ModuleStatus::STOP);
        setInputs(rig, 3, 5);
        QSignalSpy writes(&rig.comm, &Comms::dataWrite);
        rig.module.startSweep();
        QVERIFY(rig.module.isRunning());
        QCOMPARE(rig.module.state, FrequencyAnalysisModule::State::Barrier);
        QVERIFY(hasCommand(writes, "GENS:FRQ?;"));
        // Upload acknowledgements from a previous operation cannot pass the barrier.
        rig.module.parseInstrumentData(Commands::SIGNAL_GENERATOR, Commands::CMD_GEN_NEXT);
        QCOMPARE(rig.module.state, FrequencyAnalysisModule::State::Barrier);
        completeReadback(rig, 100000);
        QCOMPARE(rig.module.state, FrequencyAnalysisModule::State::Uploading);
        QVERIFY(rig.module.waveformLength >= 4);
        int blocks = 0;
        while (rig.module.state == FrequencyAnalysisModule::State::Uploading && ++blocks < 2000)
            rig.module.parseInstrumentData(Commands::SIGNAL_GENERATOR, Commands::CMD_GEN_NEXT);
        QCOMPARE(rig.module.state, FrequencyAnalysisModule::State::Frequency);
        writes.clear();
        completeReadback(rig, quint32(rig.module.waveformLength * 10));
        QCOMPARE(rig.module.state, FrequencyAnalysisModule::State::Starting);
        QVERIFY(hasCommand(writes, "GENS:STRT;"));
        rig.module.parseInstrumentData(Commands::SIGNAL_GENERATOR, Commands::CMD_GEN_OK);
        QCOMPARE(rig.module.state, FrequencyAnalysisModule::State::Settling);
        rig.module.configureCapture();
        QCOMPARE(rig.module.state, FrequencyAnalysisModule::State::Capturing);
        QVERIFY(hasCommand(writes, "OSCP:STRT;"));
        QVERIFY(hasCommand(writes, "OSCP:TRIG:F_A_;"));
        QVERIFY(hasCommand(writes, binaryCommand("OSCP:A_CH ", 3)));
        QVERIFY(hasCommand(writes, binaryCommand("OSCP:A_CH ", 0x0105)));
        writes.clear();
        rig.module.cancelSweep();
        QVERIFY(hasCommand(writes, "OSCP:STOP;"));
        QVERIFY(hasCommand(writes, "GENS:STOP;"));
        QCOMPARE(rig.module.state, FrequencyAnalysisModule::State::Draining);
        rig.module.parseInstrumentData(Commands::SIGNAL_GENERATOR, Commands::CMD_GEN_NEXT);
        rig.module.parseInstrumentData(Commands::SCOPE, frame(rig.module.sampling.samples,
                                                            rig.module.sampling.requestedRate, 10));
        QVERIFY(view(rig)->state().value("results").toArray().isEmpty());
        completeReadback(rig, 100000);
        QCOMPARE(rig.module.state, FrequencyAnalysisModule::State::Idle);
        QVERIFY(!view(rig)->state().value("running").toBool());
    }

    void gainAndPhaseFromScope_data()
    {
        QTest::addColumn<double>("frequency");
        QTest::addColumn<int>("rate");
        QTest::addColumn<int>("samples");
        QTest::addColumn<bool>("alias");
        QTest::newRow("direct") << 1000.0 << 32000 << 1024 << false;
        QTest::newRow("negative-alias") << 1000000.0 << 1010000 << 4096 << true;
        QTest::newRow("positive-alias") << 1000000.0 << 990000 << 4096 << true;
    }

    void gainAndPhaseFromScope()
    {
        QFETCH(double, frequency);
        QFETCH(int, rate);
        QFETCH(int, samples);
        QFETCH(bool, alias);
        Rig rig;
        rig.module.widgetControlClicked(ModuleStatus::STOP);
        rig.module.settings = FrequencyAnalysis::Settings{};
        rig.module.settings.averages = 1;
        rig.module.settings.periods = 32;
        rig.module.settings.allowAliasing = alias;
        rig.module.points = {{frequency, 1}};
        rig.module.actualFrequency = frequency;
        rig.module.sampling.requestedRate = rate;
        rig.module.sampling.samples = samples;
        rig.module.state = FrequencyAnalysisModule::State::Capturing;
        rig.module.captureStarted = true;
        rig.module.acceptCapture(frame(samples, rate, frequency));
        const auto results = view(rig)->state().value("results").toArray();
        QCOMPARE(results.size(), 1);
        const auto result = results[0].toObject();
        QVERIFY(qAbs(result.value("gain_db").toDouble() + 6.0206) < 0.03);
        QVERIFY(qAbs(result.value("phase_degrees").toDouble() + 45) < 0.2);
        QCOMPARE(result.value("sampling_rate_hz").toInt(), rate);
        QCOMPARE(rig.module.state, FrequencyAnalysisModule::State::Draining);
        completeReadback(rig, 32000);
        QCOMPARE(rig.module.state, FrequencyAnalysisModule::State::Idle);
    }

    void rejectsMalformedAndClippedCaptures_data()
    {
        QTest::addColumn<bool>("clipped");
        QTest::newRow("truncated") << false;
        QTest::newRow("clipped") << true;
    }

    void retriesAnUnusableQuantizedAlias()
    {
        Rig rig;
        rig.module.widgetControlClicked(ModuleStatus::STOP);
        rig.module.settings = FrequencyAnalysis::Settings{};
        rig.module.settings.allowAliasing = true;
        rig.module.points = {{1000000, 1}};
        rig.module.actualFrequency = 1000000;
        rig.module.sampling.requestedRate = 1010000;
        rig.module.sampling.samples = 4096;
        rig.module.state = FrequencyAnalysisModule::State::Capturing;
        rig.module.captureStarted = true;
        QSignalSpy writes(&rig.comm, &Comms::dataWrite);
        // A shared-clock timer can turn the requested beat into a DC alias.
        rig.module.acceptCapture(frame(4096, 1000000, 1000000));
        QCOMPARE(rig.module.rateAttempts, 1);
        QVERIFY(rig.module.sampling.requestedRate < 1010000);
        QCOMPARE(rig.module.state, FrequencyAnalysisModule::State::Capturing);
        QVERIFY(hasCommand(writes, "OSCP:STOP;"));
        QVERIFY(hasCommand(writes, "OSCP:STRT;"));
        QVERIFY(view(rig)->state().value("results").toArray().isEmpty());
        rig.module.cancelSweep();
        completeReadback(rig, 1000000);
    }

    void growsRecordForQuantizedDirectSampling()
    {
        Rig rig;
        rig.module.widgetControlClicked(ModuleStatus::STOP);
        rig.module.settings = FrequencyAnalysis::Settings{};
        rig.module.settings.averages = 1;
        rig.module.points = {{1000, 1}};
        rig.module.actualFrequency = 1000;
        rig.module.sampling.requestedRate = 32000;
        rig.module.sampling.samples = 1056;
        rig.module.state = FrequencyAnalysisModule::State::Capturing;
        rig.module.captureStarted = true;
        rig.module.acceptCapture(frame(1056, 100000, 1000));
        QCOMPARE(rig.module.rateAttempts, 1);
        QVERIFY(rig.module.sampling.samples > 1056);
        QVERIFY(view(rig)->state().value("results").toArray().isEmpty());
        rig.module.captureStarted = true;
        rig.module.acceptCapture(frame(rig.module.sampling.samples, 100000, 1000));
        QCOMPARE(view(rig)->state().value("results").toArray().size(), 1);
        completeReadback(rig, 32000);
        QCOMPARE(rig.module.state, FrequencyAnalysisModule::State::Idle);
    }

    void manualTypingIsNotReformattedPerDigit_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<QString>("text");
        QTest::addColumn<double>("value");
        QTest::newRow("point-count") << QString("pointsPerDecade") << QString("128") << 128.0;
        QTest::newRow("settling") << QString("settlingSeconds") << QString("3.500") << 3.5;
        QTest::newRow("periods") << QString("periods") << QString("128") << 128.0;
        QTest::newRow("frequency") << QString("startFrequency") << QString("123.456") << 123.456;
        QTest::newRow("amplitude") << QString("startAmplitude") << QString("1.234") << 1.234;
    }

    void manualTypingIsNotReformattedPerDigit()
    {
        QFETCH(QString, name);
        QFETCH(QString, text);
        QFETCH(double, value);
        FrequencyAnalysisWindow window;
        window.resize(1250, 850);
        window.show();
        auto *tabs = window.findChild<widgetTab *>("frequencyAnalysisSettings")->findChild<QTabWidget *>();
        tabs->setCurrentIndex(name == "settlingSeconds" || name == "periods" ? 1 : 0);
        auto *dial = window.findChild<WidgetDialRange *>(name);
        QVERIFY(dial);
        if (name == "settlingSeconds")
            dial->setRealValue(1.0f);
        auto *edit = dial->findChild<QLineEdit *>("lineEdit");
        QVERIFY(edit);
        edit->setFocus();
        edit->selectAll();
        QTest::keyClicks(edit, text);
        QCOMPARE(edit->text(), text);
        QTest::keyClick(edit, Qt::Key_Return);
        QVERIFY(qAbs(dial->getRealValue() - value) < 0.00002);
        const int decimal = edit->text().indexOf('.');
        if (name == "pointsPerDecade" || name == "periods") {
            QCOMPARE(decimal, -1);
            QCOMPARE(edit->text(), QString::number(qRound(value)));
        } else {
            QVERIFY(decimal >= 0);
            QCOMPARE(edit->text().size() - decimal - 1, 3);
        }
    }

    void integerControlsMatchCounterSamples_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<float>("initial");
        QTest::newRow("points-one") << QString("pointsPerDecade") << 1.0f;
        QTest::newRow("points-ten") << QString("pointsPerDecade") << 10.0f;
        QTest::newRow("periods") << QString("periods") << 32.0f;
        QTest::newRow("averages") << QString("averages") << 4.0f;
    }

    void integerControlsMatchCounterSamples()
    {
        QFETCH(QString, name);
        QFETCH(float, initial);
        FrequencyAnalysisWindow window;
        QWidget counterWidget;
        QVBoxLayout counterLayout(&counterWidget);
        CounterTabLowFreq counter(&counterLayout, &counterWidget);
        auto *samples = counter.dialSampleCountCh1;
        auto *dial = window.findChild<WidgetDialRange *>(name);
        QVERIFY(dial);
        dial->setRealValue(initial);
        samples->setRealValue(initial);
        auto *edit = dial->findChild<QLineEdit *>("lineEdit");
        QVERIFY(edit);
        QCOMPARE(edit->text(), samples->findChild<QLineEdit *>("lineEdit")->text());
        dial->findChild<QPushButton *>("pushButton_plus")->click();
        samples->findChild<QPushButton *>("pushButton_plus")->click();
        QCOMPARE(dial->getRealValue(), samples->getRealValue());
        QCOMPARE(edit->text(), QString::number(qRound(initial + 1)));
        dial->findChild<QPushButton *>("pushButton_minus")->click();
        samples->findChild<QPushButton *>("pushButton_minus")->click();
        QCOMPARE(dial->getRealValue(), samples->getRealValue());
        QCOMPARE(edit->text(), QString::number(qRound(initial)));
    }

    void periodTargetDoesNotAbortAValidCapture()
    {
        Rig rig;
        rig.module.widgetControlClicked(ModuleStatus::STOP);
        rig.module.settings = FrequencyAnalysis::Settings{};
        rig.module.settings.averages = 1;
        rig.module.points = {{1000, 1}};
        rig.module.actualFrequency = 1000;
        rig.module.sampling.requestedRate = 32000;
        rig.module.sampling.samples = 1024;
        rig.module.state = FrequencyAnalysisModule::State::Capturing;
        rig.module.captureStarted = true;
        rig.module.acceptCapture(frame(1024, 64000, 1000));
        const auto measurements = view(rig)->state().value("results").toArray();
        QCOMPARE(measurements.size(), 1);
        QCOMPARE(measurements.first().toObject().value("observed_cycles").toDouble(), 16.0);
        QVERIFY(!rig.module.drainError);
        QVERIFY(rig.module.periodTargetLimited);
        QVERIFY(qAbs(measurements.first().toObject().value("phase_degrees").toDouble() + 45) < 0.2);
        completeReadback(rig, 32000);
    }

    void shortBufferRetunesTheActualAdcRate_data()
    {
        QTest::addColumn<bool>("alias");
        QTest::newRow("direct-only") << false;
        QTest::newRow("alias-enabled") << true;
    }

    void shortBufferRetunesTheActualAdcRate()
    {
        QFETCH(bool, alias);
        Rig rig;
        rig.module.widgetControlClicked(ModuleStatus::STOP);
        rig.adcSpec.memorySize = 512;
        rig.module.settings = FrequencyAnalysis::Settings{};
        rig.module.settings.averages = 1;
        rig.module.settings.allowAliasing = alias;
        rig.module.points = {{1000, 1}};
        rig.module.actualFrequency = 1000;
        rig.module.sampling.requestedRate = 32000;
        rig.module.sampling.samples = 128;
        rig.module.state = FrequencyAnalysisModule::State::Capturing;
        rig.module.captureStarted = true;
        rig.module.acceptCapture(frame(128, 64000, 1000));
        QCOMPARE(rig.module.rateAttempts, 1);
        QVERIFY(rig.module.sampling.requestedRate < 32000);
        QVERIFY(view(rig)->state().value("results").toArray().isEmpty());
        rig.module.captureStarted = true;
        rig.module.acceptCapture(frame(128, rig.module.sampling.requestedRate * 2, 1000));
        QCOMPARE(view(rig)->state().value("results").toArray().size(), 1);
        QVERIFY(!rig.module.drainError);
        completeReadback(rig, 32000);
    }

    void completedPointsPublishObservedTiming()
    {
        Rig rig;
        rig.module.widgetControlClicked(ModuleStatus::STOP);
        rig.module.setTransferRate(11520);
        rig.module.settings = FrequencyAnalysis::Settings{};
        rig.module.settings.averages = 1;
        rig.module.points = {{10000, 1}};
        rig.module.actualFrequency = 10000;
        rig.module.sampling.requestedRate = 80000;
        rig.module.sampling.samples = 128;
        rig.module.pointClock.start();
        QTest::qWait(10);
        rig.module.requestCapture(true);
        rig.module.parseInstrumentData(Commands::SCOPE, Commands::SAMPLING);
        rig.module.parseInstrumentData(Commands::SCOPE, Commands::TRIGGERED);
        QTest::qWait(8);
        rig.module.parseInstrumentData(Commands::SCOPE, frame(128, 80000, 10000));
        QCOMPARE(rig.module.timedCaptures, 1);
        QCOMPARE(rig.module.timedPoints, 1);
        QCOMPARE(rig.module.downloadBytesTotal, qint64(4 * 128 + 74));
        QTRY_VERIFY(view(rig)->state().value("timing_estimate").toObject().value("learned_overhead").toBool());
        QCOMPARE(view(rig)->state().value("timing_estimate").toObject()
                     .value("reported_transfer_bytes_per_second").toDouble(), 11520.0);
        completeReadback(rig, 32000);
    }

    void slowActualRateIsRaisedInsteadOfReduced_data()
    {
        shortBufferRetunesTheActualAdcRate_data();
    }

    void slowActualRateIsRaisedInsteadOfReduced()
    {
        QFETCH(bool, alias);
        Rig rig;
        rig.module.widgetControlClicked(ModuleStatus::STOP);
        rig.module.settings = FrequencyAnalysis::Settings{};
        rig.module.settings.averages = 1;
        rig.module.settings.allowAliasing = alias;
        rig.module.points = {{1000, 1}};
        rig.module.actualFrequency = 1000;
        rig.module.sampling.requestedRate = 8000;
        rig.module.sampling.samples = 128;
        rig.module.state = FrequencyAnalysisModule::State::Capturing;
        rig.module.captureStarted = true;
        rig.module.acceptCapture(frame(128, 3000, 1000));
        QCOMPARE(rig.module.rateAttempts, 1);
        QVERIFY(rig.module.sampling.requestedRate > 8000);
        rig.module.captureStarted = true;
        rig.module.acceptCapture(frame(128, 8000, 1000));
        QCOMPARE(view(rig)->state().value("results").toArray().size(), 1);
        QVERIFY(!rig.module.drainError);
        completeReadback(rig, 32000);
    }

    void rejectsMalformedAndClippedCaptures()
    {
        QFETCH(bool, clipped);
        Rig rig;
        rig.module.widgetControlClicked(ModuleStatus::STOP);
        rig.module.settings = FrequencyAnalysis::Settings{};
        rig.module.settings.averages = 1;
        rig.module.points = {{1000, 1}};
        rig.module.actualFrequency = 1000;
        rig.module.sampling.requestedRate = 32000;
        rig.module.sampling.samples = 1024;
        rig.module.state = FrequencyAnalysisModule::State::Capturing;
        rig.module.captureStarted = true;
        QByteArray data = frame(1024, 32000, 1000, clipped);
        if (!clipped)
            data.chop(17);
        rig.module.acceptCapture(data);
        QVERIFY(view(rig)->state().value("results").toArray().isEmpty());
        QVERIFY(rig.module.drainError);
        QCOMPARE(rig.module.state, FrequencyAnalysisModule::State::Draining);
        completeReadback(rig, 32000);
    }

    void timeoutRequiresReconnect()
    {
        Rig rig;
        rig.module.widgetControlClicked(ModuleStatus::STOP);
        QSignalSpy writes(&rig.comm, &Comms::dataWrite);
        rig.module.startSweep();
        QVERIFY(QMetaObject::invokeMethod(&rig.module.watchdog, "timeout", Qt::DirectConnection));
        QCOMPARE(rig.module.state, FrequencyAnalysisModule::State::Fault);
        QVERIFY(hasCommand(writes, "GENS:DINI;"));
        QVERIFY(!rig.module.isRunning());
        writes.clear();
        rig.module.startSweep();
        QVERIFY(!rig.module.isRunning());
        QVERIFY(writes.isEmpty());
    }
};

QTEST_MAIN(FrequencyAnalysisTest)
#include "frequencyanalysis_test.moc"
