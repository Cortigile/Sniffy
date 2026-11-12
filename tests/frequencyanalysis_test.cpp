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
#include <QtCharts/QAreaSeries>
#include <QtCharts/QLegendMarker>
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
#include "app/devicemediator.h"

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

    explicit Rig(int channels = 2, bool loadInputs = true, bool configureInstruments = true)
    {
        QObject::disconnect(&comm, &Comms::dataWrite, nullptr, nullptr);
        control.setStatus(ModuleStatus::STOP);
        module.setDockWidgetWindow(&dock);
        module.setModuleControlWidget(&control);
        adcSpec.parseSpecification(scopeSpecification(channels));
        dacSpec.parseSpecification(generatorSpecification());
        if (configureInstruments) {
            adc.setModuleConfigured();
            dac.setModuleConfigured();
        }
        module.setComms(&comm);
        if (channels >= 2 && loadInputs)
            module.parseInstrumentData(Commands::SCOPE, inputCatalogResponse());
    }
};

struct MediatorRig
{
    QList<QSharedPointer<WidgetControlModule>> controls;
    QList<QSharedPointer<ModuleDockWidget>> docks;
    DeviceMediator mediator{nullptr};
    Voltmeter *voltmeter = nullptr;
    FrequencyAnalysisModule *analysis = nullptr;

    MediatorRig()
    {
        QObject::disconnect(mediator.getComms(), &Comms::dataWrite, nullptr, nullptr);
        const auto modules = mediator.getModulesList();
        for (const auto &module : modules) {
            auto control = QSharedPointer<WidgetControlModule>::create();
            control->setStatus(ModuleStatus::STOP);
            control->hide();
            auto dock = QSharedPointer<ModuleDockWidget>::create();
            module->setDockWidgetWindow(dock.data());
            module->setModuleControlWidget(control.data());
            controls.append(control);
            docks.append(dock);
            if (auto *consumer = qobject_cast<Voltmeter *>(module.data()))
                voltmeter = consumer;
            if (auto *consumer = qobject_cast<FrequencyAnalysisModule *>(module.data()))
                analysis = consumer;
        }
        for (int index = 1; index < modules.size(); ++index)
            modules.at(index)->setComms(mediator.getComms());
        receive(Commands::SCOPE + Commands::CONFIG + scopeSpecification(2));
        receive(Commands::SIGNAL_GENERATOR + Commands::CONFIG + generatorSpecification());
    }

    bool receive(const QByteArray &message)
    {
        return QMetaObject::invokeMethod(&mediator, "parseData", Qt::DirectConnection,
                                         Q_ARG(QByteArray, message));
    }

    QPushButton *multimodeButton() const
    {
        return voltmeter->getWidget()->findChild<WidgetButtons *>("voltmeterAcquisitionMode")
            ->findChild<QPushButton *>("pushButton_2");
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

QList<QAreaSeries *> uncertaintyBands(QChart *chart)
{
    QList<QAreaSeries *> bands;
    for (auto *series : chart->series())
        if (auto *band = qobject_cast<QAreaSeries *>(series))
            bands.append(band);
    return bands;
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
        QVERIFY(!rig.module.isAvailable());
        QVERIFY(rig.control.isHidden());
        QSignalSpy writes(&rig.comm, &Comms::dataWrite);
        rig.module.setComms(&rig.comm);
        QVERIFY(hasCommand(writes, "OSCP:INP?;"));
        rig.module.startSweep();
        QVERIFY(!rig.module.isRunning());
        QVERIFY(!hasCommand(writes, "GENS:STRT;"));
        rig.module.parseInstrumentData(Commands::SCOPE, inputCatalogResponse());
        QVERIFY(rig.module.isAvailable());
        QVERIFY(!rig.control.isHidden());
        rig.module.widgetControlClicked(ModuleStatus::STOP);
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

    void sharedDiscoveryGatesBothConsumers_data()
    {
        QTest::addColumn<QByteArray>("response");
        QTest::newRow("NACK") << QByteArray("NACK");
        QTest::newRow("timeout") << QByteArray();
        QTest::newRow("malformed") << QByteArray("INP_/PA0/");
    }

    void sharedDiscoveryGatesBothConsumers()
    {
        QFETCH(QByteArray, response);
        MediatorRig rig;
        QVERIFY(rig.analysis->isDiscoveringInputs());
        QVERIFY(!rig.analysis->isAvailable());
        QVERIFY(rig.controls.last()->isHidden());
        QVERIFY(rig.multimodeButton()->isHidden());
        QVERIFY(rig.voltmeter->isAvailable());

        if (response.isEmpty()) {
            QCOMPARE(rig.analysis->inputTimeout.interval(), 3000);
            QTRY_VERIFY_WITH_TIMEOUT(!rig.analysis->isDiscoveringInputs(), 4500);
        } else {
            QVERIFY(rig.receive(Commands::SCOPE + response));
            QVERIFY(!rig.analysis->isDiscoveringInputs());
        }
        rig.analysis->refreshAvailability();
        QVERIFY(!rig.analysis->isAvailable());
        QVERIFY(rig.controls.last()->isHidden());
        QVERIFY(rig.multimodeButton()->isHidden());

        QSignalSpy writes(rig.mediator.getComms(), &Comms::dataWrite);
        rig.voltmeter->widgetControlClicked(ModuleStatus::STOP);
        QVERIFY(rig.voltmeter->isActive());
        QVERIFY(!qobject_cast<VoltmeterWindow *>(rig.voltmeter->getWidget())->isMultimode());
        QVERIFY(!hasCommand(writes, "OSCP:INP?;"));
        rig.voltmeter->closeModule();
    }

    void sharedDiscoveryAndReconnect()
    {
        MediatorRig rig;
        QVERIFY(rig.receive(Commands::SCOPE + inputCatalogResponse()));
        QVERIFY(rig.analysis->isAvailable());
        QVERIFY(!rig.controls.last()->isHidden());
        QVERIFY(!rig.multimodeButton()->isHidden());
        auto *spec = static_cast<VoltmeterSpec *>(rig.voltmeter->getSpecification());
        QVERIFY(spec->inputCapabilitiesAvailable);
        QCOMPARE(spec->availableInputSlots, 2);

        rig.mediator.disableModules();
        rig.voltmeter->setComms(rig.mediator.getComms());
        rig.analysis->setComms(rig.mediator.getComms());
        QVERIFY(!rig.analysis->isAvailable());
        QVERIFY(rig.controls.last()->isHidden());
        QVERIFY(rig.multimodeButton()->isHidden());
        QVERIFY(!spec->inputCapabilitiesAvailable);
        QCOMPARE(spec->availableInputSlots, 0);
        QVERIFY(rig.receive(Commands::SCOPE + Commands::CONFIG + scopeSpecification(2)));
        QVERIFY(rig.receive(Commands::SIGNAL_GENERATOR + Commands::CONFIG + generatorSpecification()));
        QVERIFY(rig.receive(Commands::SCOPE + QByteArray("NACK")));
        QVERIFY(!rig.analysis->isAvailable());
        QVERIFY(rig.multimodeButton()->isHidden());

        rig.analysis->setComms(rig.mediator.getComms());
        QVERIFY(rig.receive(Commands::SCOPE + inputCatalogResponse()));
        QVERIFY(rig.analysis->isAvailable());
        QVERIFY(!rig.multimodeButton()->isHidden());
    }

    void inputDiscoveryBeforeInstrumentSpecifications()
    {
        Rig rig(2, false, false);
        rig.module.parseInstrumentData(Commands::SCOPE, inputCatalogResponse());
        QVERIFY(!rig.module.isAvailable());
        rig.adc.setModuleConfigured();
        rig.dac.setModuleConfigured();
        rig.module.refreshAvailability();
        QVERIFY(rig.module.isAvailable());
    }

    void singleInputSlotEnablesOnlyVoltmeterMultimode()
    {
        MediatorRig rig;
        QVERIFY(rig.receive(Commands::SCOPE + QByteArray("INP_") + char(0)
                            + "/PA0:PA1:Vref/"));
        QVERIFY(!rig.analysis->isAvailable());
        QVERIFY(rig.controls.last()->isHidden());
        QVERIFY(!rig.multimodeButton()->isHidden());
        QCOMPARE(static_cast<VoltmeterSpec *>(rig.voltmeter->getSpecification())
                     ->availableInputSlots, 1);
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

    void shortGeneratorWaveforms_data()
    {
        QTest::addColumn<double>("frequency");
        QTest::addColumn<QVector<double>>("normalizedSamples");
        QTest::newRow("two-samples-90-degrees")
            << 4200000.0 << QVector<double>{1, -1};
        QTest::newRow("three-samples-15-degrees")
            << 2800000.0 << QVector<double>{0.2588190451025207, 0.7071067811865475,
                                           -0.9659258262890683};
        QTest::newRow("four-samples-unchanged")
            << 2100000.0 << QVector<double>{0, 1, 0, -1};
        QTest::newRow("five-samples-unchanged")
            << 1680000.0 << QVector<double>{0, 0.9510565162951535, 0.5877852522924731,
                                           -0.5877852522924731, -0.9510565162951535};
    }

    void shortGeneratorWaveforms()
    {
        QFETCH(double, frequency);
        QFETCH(QVector<double>, normalizedSamples);
        Rig rig;
        rig.module.settings.offset = 1.2;
        rig.module.points = {{frequency, 0.8}};
        QString error;
        QVERIFY2(rig.module.prepareWaveform(error), qPrintable(error));
        QCOMPARE(rig.module.waveformLength, normalizedSamples.size());
        QCOMPARE(rig.module.generatorRate, quint32(8400000));
        QCOMPARE(rig.module.waveform.size(), 1);
        QCOMPARE(rig.module.waveform[0].size(), normalizedSamples.size());
        for (qsizetype i = 0; i < normalizedSamples.size(); ++i) {
            const double voltage = 1.2 + 0.4 * normalizedSamples[i];
            QCOMPARE(rig.module.waveform[0][i], qRound(voltage / 3.3 * 4095));
        }

        rig.module.widgetControlClicked(ModuleStatus::STOP);
        rig.module.generatorTransport = std::make_unique<GenCommons>(
            Commands::SIGNAL_GENERATOR, &rig.comm, rig.dacSpec.maxSamplingRate);
        QSignalSpy writes(&rig.comm, &Comms::dataWrite);
        rig.module.beginPoint();
        QCOMPARE(rig.module.state, FrequencyAnalysisModule::State::Uploading);
        QVERIFY(hasCommand(writes, binaryCommand("GENS:LCH1 ", rig.module.waveformLength)));
        rig.module.parseInstrumentData(Commands::SIGNAL_GENERATOR, Commands::CMD_GEN_NEXT);
        QCOMPARE(rig.module.state, FrequencyAnalysisModule::State::Frequency);
        completeReadback(rig, 8400000);
        QCOMPARE(rig.module.actualFrequency, frequency);
        QCOMPARE(rig.module.state, FrequencyAnalysisModule::State::Starting);
    }

    void generatorFrequencyLimit_data()
    {
        QTest::addColumn<int>("maximumRate");
        QTest::addColumn<int>("bufferBytes");
        QTest::addColumn<double>("stopFrequency");
        QTest::addColumn<bool>("accepted");
        QTest::newRow("two-sample-limit") << 8400000 << 8192 << 4200000.0 << true;
        QTest::newRow("above-two-sample-limit") << 8400000 << 8192 << 4200001.0 << false;
        QTest::newRow("minimum-buffer") << 8400000 << 4 << 4200000.0 << true;
        QTest::newRow("protocol-limit") << 20000000 << 8192 << 8388607.5 << true;
        QTest::newRow("above-protocol-limit") << 20000000 << 8192 << 8388608.0 << false;
    }

    void generatorFrequencyLimit()
    {
        QFETCH(int, maximumRate);
        QFETCH(int, bufferBytes);
        QFETCH(double, stopFrequency);
        QFETCH(bool, accepted);
        Rig rig;
        rig.dacSpec.maxSamplingRate = maximumRate;
        rig.dacSpec.generatorBufferSize = bufferBytes;
        rig.module.refreshAvailability();
        rig.module.widgetControlClicked(ModuleStatus::STOP);
        view(rig)->findChild<WidgetDialRange *>("startFrequency")
            ->setRealValue(float(stopFrequency * 0.9), true);
        view(rig)->findChild<WidgetDialRange *>("stopFrequency")
            ->setRealValue(float(stopFrequency), true);
        QSignalSpy writes(&rig.comm, &Comms::dataWrite);
        rig.module.startSweep();
        QCOMPARE(rig.module.isRunning(), accepted);
        if (accepted) {
            QCOMPARE(rig.module.state, FrequencyAnalysisModule::State::Barrier);
            QVERIFY(hasCommand(writes, "GENS:FRQ?;"));
        } else {
            QVERIFY(writes.isEmpty());
            QVERIFY(view(rig)->state().value("status_error").toBool());
        }
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
        for (int tab = 0; tab < tabWidget->count(); ++tab) {
            auto *layout = tabs->getLayout(tab);
            QCOMPARE(layout->contentsMargins().top(), 5);
            tabWidget->setCurrentIndex(tab);
            QApplication::processEvents();
            auto *firstControl = layout->itemAt(0)->widget();
            QVERIFY(firstControl && firstControl->isVisible());
            QCOMPARE(firstControl->y(), 5);
        }
        tabWidget->setCurrentIndex(0);
        QApplication::processEvents();
        auto *viewSettings = window->findChild<QWidget *>("viewSettings");
        QVERIFY(viewSettings);
        QVERIFY(tabWidget->widget(3)->isAncestorOf(viewSettings));
        auto *resultTabs = window->findChild<QTabWidget *>("frequencyAnalysisResultTabs");
        QVERIFY(resultTabs);
        for (const auto &name : {"unwrapPhase", "showCursors", "combinedPlot", "showUncertainty"}) {
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

    void uncertaintyBandsFollowDisplay_data()
    {
        QTest::addColumn<bool>("linear");
        QTest::addColumn<bool>("combined");
        QTest::newRow("separate-logarithmic") << false << false;
        QTest::newRow("separate-linear") << true << false;
        QTest::newRow("combined-logarithmic") << false << true;
        QTest::newRow("combined-linear") << true << true;
    }

    void uncertaintyBandsFollowDisplay()
    {
        QFETCH(bool, linear);
        QFETCH(bool, combined);
        FrequencyAnalysisWindow window;
        window.resize(1250, 850);
        window.show();
        if (linear)
            window.findChild<WidgetButtons *>("frequencySpacing")->clickedInternal(1);
        window.clearResults();
        auto *toggle = window.findChild<WidgetButtons *>("showUncertainty");
        QVERIFY(toggle && !toggle->isChecked(0));
        QVERIFY(toggle->getText(0).contains(QStringLiteral("\u00b13\u03c3")));
        QVERIFY(!window.findChild<QLabel *>("frequencyUncertaintyHint"));
        auto *gain = window.findChild<QChartView *>("frequencyGainPlot");
        auto *phase = window.findChild<QChartView *>("frequencyPhasePlot");
        auto *phaseTrace = qobject_cast<QLineSeries *>(phase->chart()->series().first());
        QVERIFY(gain && phase && phaseTrace);
        FrequencyAnalysis::Result result;
        result.coherence = 0.8;
        result.averages = 8;
        result.gainDb = -6;
        result.phaseDegrees = -30;
        result.quantizationRelativeStdDev = 0.2;
        result.dftRelativeErrorBound = 0.03;
        for (const double frequency : {99.5, 999.5, 9999.5})
            window.appendResult(frequency + 0.5, frequency, 1, 320000, 2048, 32, result);
        const auto measurements = window.state().value("results");
        QVERIFY(uncertaintyBands(gain->chart()).isEmpty());
        auto *tabs = window.findChild<widgetTab *>("frequencyAnalysisSettings")->findChild<QTabWidget *>();
        tabs->setCurrentIndex(3);
        window.setRunning(true);
        QApplication::processEvents();
        QVERIFY(toggle->isVisible() && toggle->isEnabled());
        QTest::mouseClick(toggle->findChild<QPushButton *>("pushButton_1"), Qt::LeftButton);
        if (combined)
            window.findChild<WidgetButtons *>("combinedPlot")->findChild<QPushButton *>("pushButton_1")->click();
        const double relative = std::hypot(0.2, 0.03 / std::sqrt(3.0));
        const double gainError = 20 / std::log(10.0) * relative;
        const double phaseError = 180 / std::acos(-1.0) * relative;
        const auto verify = [&] {
            const auto gainBands = uncertaintyBands(gain->chart());
            const auto phaseBands = uncertaintyBands(phase->chart());
            const bool together = phaseTrace->chart() == gain->chart();
            QCOMPARE(gainBands.size(), together ? 2 : 1);
            QCOMPARE(phaseBands.size(), together ? 0 : 1);
            for (auto *band : gainBands + phaseBands) {
                const bool gainBand = band->objectName() == "frequencyGainUncertainty";
                const auto lower = band->lowerSeries()->points();
                const auto upper = band->upperSeries()->points();
                QCOMPARE(lower.size(), 3);
                QCOMPARE(upper.size(), 3);
                QCOMPARE(lower.first().x(), 99.5);
                QCOMPARE(upper.last().x(), 9999.5);
                const double center = gainBand ? -6 : -30;
                const double error = 3 * (gainBand ? gainError : phaseError);
                QVERIFY(qAbs(lower.first().y() - (center - error)) < 1e-10);
                QVERIFY(qAbs(upper.first().y() - (center + error)) < 1e-10);
                auto color = QColor(Graphics::getChannelColor(0));
                color.setAlpha(band->brush().color().alpha());
                QCOMPARE(band->brush().color(), color);
                QVERIFY(color.alpha() > 0 && color.alpha() < 128);
                QCOMPARE(band->pen().style(), Qt::NoPen);
                QCOMPARE(band->attachedAxes().size(), 2);
                for (auto *axis : band->attachedAxes()) {
                    if (axis->orientation() == Qt::Vertical) {
                        auto *valueAxis = qobject_cast<QValueAxis *>(axis);
                        QVERIFY(valueAxis);
                        QVERIFY(valueAxis->min() <= center - error);
                        QVERIFY(valueAxis->max() >= center + error);
                        QCOMPARE(axis->alignment(), !gainBand && together ? Qt::AlignRight : Qt::AlignLeft);
                    } else {
                        QCOMPARE(axis, gainBand || together ? gain->chart()->axes(Qt::Horizontal).first()
                                                           : phase->chart()->axes(Qt::Horizontal).first());
                    }
                }
                for (auto *marker : band->chart()->legend()->markers(band))
                    QVERIFY(!marker->isVisible());
            }
        };
        verify();
        window.setRunning(false);
        window.findChild<WidgetDialRange *>("averages")->setRealValue(128);
        toggle->findChild<QPushButton *>("pushButton_1")->click();
        QVERIFY(uncertaintyBands(gain->chart()).isEmpty());
        QVERIFY(uncertaintyBands(phase->chart()).isEmpty());
        toggle->findChild<QPushButton *>("pushButton_1")->click();
        verify();
        window.findChild<WidgetButtons *>("combinedPlot")->findChild<QPushButton *>("pushButton_1")->click();
        verify();
        QCOMPARE(window.state().value("results"), measurements);
        const auto first = measurements.toArray().first().toObject();
        QCOMPARE(first.value("averages").toInt(), 8);
        const auto estimate = first.value("uncertainty").toObject();
        QVERIFY(estimate.value("available").toBool());
        QCOMPARE(estimate.value("standard_deviations").toInt(), 3);
        QCOMPARE(estimate.value("model").toString(), QString("coherence_quantization_flattop"));
        QVERIFY(qAbs(estimate.value("gain_stddev_db").toDouble() - gainError) < 1e-10);
        QVERIFY(qAbs(estimate.value("phase_stddev_degrees").toDouble() - phaseError) < 1e-10);
        QVERIFY(qAbs(estimate.value("gain_half_width_db").toDouble() - 3 * gainError) < 1e-10);
        QVERIFY(qAbs(estimate.value("phase_half_width_degrees").toDouble() - 3 * phaseError) < 1e-10);
        auto *table = window.findChild<QTableWidget *>("frequencyAnalysisResults");
        QCOMPARE(table->columnCount(), 17);
        QCOMPARE(table->item(0, 15)->text(), QString("8"));
        const auto csv = window.csvData().split('\n');
        QCOMPARE(csv.first().split(',').size(), 20);
        const auto fields = csv.at(1).split(',');
        QCOMPARE(fields.size(), 20);
        QCOMPARE(fields.at(15), QByteArray("8"));
        QVERIFY(qAbs(fields.at(16).toDouble() - gainError) < 1e-10);
        QVERIFY(qAbs(fields.at(17).toDouble() - phaseError) < 1e-10);
        QCOMPARE(fields.at(18), QByteArray("approximate_combined_1_sigma"));
        QApplication::processEvents();
        const QString snapshot = QString::fromLocal8Bit(qgetenv("SNIFFY_FREQ_UNCERTAINTY_UI_SNAPSHOT"));
        if (!snapshot.isEmpty()) {
            QTest::qWait(50);
            QVERIFY(window.grab().save(snapshot));
        }
        window.clearResults();
        QVERIFY(uncertaintyBands(gain->chart()).isEmpty());
        QVERIFY(uncertaintyBands(phase->chart()).isEmpty());
        window.appendResult(100, 100, 1, 3200, 2048, 32, result);
        QVERIFY(uncertaintyBands(gain->chart()).isEmpty());
        window.appendResult(1000, 1000, 1, 32000, 2048, 32, result);
        QCOMPARE(uncertaintyBands(gain->chart()).size()
                 + uncertaintyBands(phase->chart()).size(), 2);
    }

    void uncertaintyGapsAndPhaseWrapping()
    {
        FrequencyAnalysisWindow window;
        auto *gain = window.findChild<QChartView *>("frequencyGainPlot");
        auto *phase = window.findChild<QChartView *>("frequencyPhasePlot");
        auto *toggle = window.findChild<WidgetButtons *>("showUncertainty");
        toggle->findChild<QPushButton *>("pushButton_1")->click();
        FrequencyAnalysis::Result result;
        result.averages = 8;
        result.coherence = 0.8;
        result.phaseDegrees = 179;
        for (int i = 1; i <= 8; ++i) {
            if (i == 3)
                result.averages = 1;
            else
                result.averages = 8;
            result.coherence = i == 6 ? 0 : 0.8;
            result.phaseDegrees = i <= 4 ? 179 : -179;
            window.appendResult(i * 100, i * 100, 1, 32000, 2048, 32, result);
        }
        QCOMPARE(uncertaintyBands(gain->chart()).size(), 3);
        QCOMPARE(uncertaintyBands(phase->chart()).size(), 3);
        const auto original = window.state().value("results");
        const auto missing = original.toArray().at(2).toObject().value("uncertainty").toObject();
        QVERIFY(!missing.value("available").toBool());
        QVERIFY(missing.value("reason").toString().contains("two"));
        const auto zero = original.toArray().at(5).toObject().value("uncertainty").toObject();
        QVERIFY(!zero.value("available").toBool());
        QVERIFY(zero.value("reason").toString().contains("Zero coherence"));
        QVERIFY(!window.findChild<QLabel *>("frequencyUncertaintyHint"));
        auto *unwrap = window.findChild<WidgetButtons *>("unwrapPhase");
        unwrap->findChild<QPushButton *>("pushButton_1")->click();
        QCOMPARE(uncertaintyBands(phase->chart()).size(), 2);
        for (auto *band : uncertaintyBands(phase->chart())) {
            for (const auto &point : band->lowerSeries()->points())
                QVERIFY(point.y() >= -180);
            for (const auto &point : band->upperSeries()->points())
                QVERIFY(point.y() <= 180);
        }
        QCOMPARE(window.state().value("results"), original);
        const auto csv = window.csvData().split('\n');
        const auto missingFields = csv.at(3).split(',');
        QVERIFY(missingFields.at(16).isEmpty() && missingFields.at(17).isEmpty());
        QVERIFY(!missingFields.at(18).isEmpty());
        unwrap->findChild<QPushButton *>("pushButton_1")->click();
        QCOMPARE(uncertaintyBands(phase->chart()).size(), 3);
    }

    void uncertaintyPreferenceIsPersistent()
    {
        FrequencyAnalysisWindow window;
        const auto off = window.serialize();
        auto *toggle = window.findChild<WidgetButtons *>("showUncertainty");
        toggle->findChild<QPushButton *>("pushButton_1")->click();
        const auto on = window.serialize();
        FrequencyAnalysisWindow restored;
        QString error;
        QVERIFY2(restored.restore(on, error), qPrintable(error));
        QVERIFY(restored.findChild<WidgetButtons *>("showUncertainty")->isChecked(0));
        QCOMPARE(restored.serialize(), on);
        auto root = QJsonDocument::fromJson(on).object();
        auto settings = root.value("settings").toObject();
        settings["showUncertainty"] = "invalid";
        root["settings"] = settings;
        QVERIFY(!restored.restore(QJsonDocument(root).toJson(), error));
        QCOMPARE(restored.serialize(), on);
        settings.remove("showUncertainty");
        root["settings"] = settings;
        QVERIFY2(restored.restore(QJsonDocument(root).toJson(), error), qPrintable(error));
        QCOMPARE(restored.serialize(), off);
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
        window->appendResult(100, 100, 1, 3200, 1024, 32, result);
        result.phaseDegrees = -170;
        window->appendResult(1000, 1000, 1, 32000, 1024, 32, result);
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

    void generatorSampleCountsPersistPerPoint()
    {
        FrequencyAnalysisWindow window;
        FrequencyAnalysis::Result result;
        result.coherence = 1;
        result.averages = 2;
        const QList<int> lengths{2, 3, 128};
        for (qsizetype i = 0; i < lengths.size(); ++i)
            window.appendResult(100 * (i + 1), 100 * (i + 1), 1, 32000, 2048, lengths[i], result);

        auto *table = window.findChild<QTableWidget *>("frequencyAnalysisResults");
        QCOMPARE(table->columnCount(), 17);
        QCOMPARE(table->horizontalHeaderItem(16)->text(), QString("DAC samples/period"));
        QCOMPARE(table->rowCount(), lengths.size());
        const auto measurements = window.state().value("results").toArray();
        const auto csv = window.csvData().split('\n');
        QCOMPARE(csv.first().split(',').last(), QByteArray("generator_samples_per_period"));
        for (qsizetype i = 0; i < lengths.size(); ++i) {
            QCOMPARE(table->item(i, 16)->text(), QString::number(lengths[i]));
            QCOMPARE(measurements[i].toObject().value("generator_samples_per_period").toInt(), lengths[i]);
            QCOMPARE(measurements[i].toObject().value("samples").toInt(), 2048);
            const auto fields = csv[i + 1].split(',');
            QCOMPARE(fields.size(), 20);
            QCOMPARE(fields.last().toInt(), lengths[i]);
            QCOMPARE(fields[4].toInt(), 2048);
        }
        for (int invalid : {-1, 0, 1}) {
            window.appendResult(1000, 1000, 1, 32000, 2048, invalid, result);
            QCOMPARE(table->rowCount(), lengths.size());
            QVERIFY(window.state().value("status_error").toBool());
        }
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
        window.appendResult(100, 99.5, 1, 3200, 1024, 32, result);
        result.phaseDegrees = -170;
        window.appendResult(200, 202.5, 1, 6400, 1024, 32, result);
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
        window.appendResult(100, 99.5, 1, 3200, 1024, 32, result);
        result.phaseDegrees = -170;
        window.appendResult(1000, 999.5, 1, 32000, 1024, 32, result);
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

    void cursorFrequenciesUseEngineeringUnits_data()
    {
        QTest::addColumn<double>("frequencyA");
        QTest::addColumn<double>("frequencyB");
        QTest::addColumn<QString>("expectedA");
        QTest::addColumn<QString>("expectedB");
        QTest::addColumn<QString>("expectedDelta");
        QTest::newRow("hertz") << 12.3456 << 987.6544
            << QString("12.346 Hz") << QString("987.654 Hz") << QString("975.309 Hz");
        QTest::newRow("kilohertz") << 1234.56789 << 98765.4321
            << QString("1.235 kHz") << QString("98.765 kHz") << QString("97.531 kHz");
        QTest::newRow("megahertz") << 1234567.89 << 9876543.21
            << QString("1.235 MHz") << QString("9.877 MHz") << QString("8.642 MHz");
        QTest::newRow("millihertz") << 0.001234567 << 0.123456789
            << QString("1.235 mHz") << QString("123.457 mHz") << QString("122.222 mHz");
        QTest::newRow("microhertz") << 0.000001234567 << 0.000987654321
            << QString("1.235 uHz") << QString("987.654 uHz") << QString("986.420 uHz");
        QTest::newRow("kilohertz-boundary") << 999.5 << 1000.0
            << QString("999.500 Hz") << QString("1.000 kHz") << QString("500.000 mHz");
        QTest::newRow("megahertz-boundary") << 999999.0 << 1000000.0
            << QString("999.999 kHz") << QString("1.000 MHz") << QString("1.000 Hz");
        QTest::newRow("negative-delta") << 98765.4321 << 1234.56789
            << QString("98.765 kHz") << QString("1.235 kHz") << QString("-97.531 kHz");
        QTest::newRow("zero-delta") << 125.0 << 125.0
            << QString("125.000 Hz") << QString("125.000 Hz") << QString("0.000 Hz");
        QTest::newRow("tiny-delta") << 1000.0 << 1000.000000000001
            << QString("1.000 kHz") << QString("1.000 kHz") << QString("0.000 Hz");
    }

    void cursorFrequenciesUseEngineeringUnits()
    {
        QFETCH(double, frequencyA);
        QFETCH(double, frequencyB);
        QFETCH(QString, expectedA);
        QFETCH(QString, expectedB);
        QFETCH(QString, expectedDelta);
        FrequencyAnalysisWindow window;
        FrequencyAnalysis::Result result;
        window.appendResult(frequencyA, frequencyA, 1, 1000000, 2048, 32, result);
        window.appendResult(frequencyB, frequencyB, 1, 1000000, 2048, 32, result);
        QCOMPARE(window.findChild<QLabel *>("frequencyCursorAReadout")->text().section(" | ", 0, 0),
                 QString("A: ") + expectedA);
        QCOMPARE(window.findChild<QLabel *>("frequencyCursorBReadout")->text().section(" | ", 0, 0),
                 QString("B: ") + expectedB);
        QCOMPARE(window.findChild<QLabel *>("frequencyCursorDifference")->text().section(" | ", 0, 0),
                 QString("B \u2212 A: ") + expectedDelta);
        const auto cursors = window.state().value("cursors").toObject();
        QCOMPARE(cursors.value("a").toObject().value("frequency_hz").toDouble(), frequencyA);
        QCOMPARE(cursors.value("b").toObject().value("frequency_hz").toDouble(), frequencyB);
        QCOMPARE(cursors.value("delta").toObject().value("frequency_hz").toDouble(), frequencyB - frequencyA);
        const auto csv = window.csvData().split('\n');
        QCOMPARE(csv.at(1).split(',').at(1).toDouble(), frequencyA);
        QCOMPARE(csv.at(2).split(',').at(1).toDouble(), frequencyB);
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
            window.appendResult(frequency, frequency, 1, 1000000, 1056, 32, result);
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
        window->appendResult(1000, 1000, 1, 32000, 1056, 32, result);
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
        QTest::addColumn<int>("averages");
        QTest::addColumn<int>("generatorSamples");
        QTest::newRow("direct") << 1000.0 << 32000 << 1024 << false << 1 << 32;
        QTest::newRow("negative-alias") << 1000000.0 << 1010000 << 4096 << true << 1 << 4;
        QTest::newRow("positive-alias") << 1000000.0 << 990000 << 4096 << true << 1 << 4;
        QTest::newRow("four-averages") << 1000.0 << 32000 << 1024 << false << 4 << 32;
        QTest::newRow("two-generator-samples") << 4200000.0 << 1000000 << 4096 << true << 1 << 2;
        QTest::newRow("three-generator-samples") << 2800000.0 << 1000000 << 4096 << true << 1 << 3;
    }

    void gainAndPhaseFromScope()
    {
        QFETCH(double, frequency);
        QFETCH(int, rate);
        QFETCH(int, samples);
        QFETCH(bool, alias);
        QFETCH(int, averages);
        QFETCH(int, generatorSamples);
        Rig rig;
        rig.module.widgetControlClicked(ModuleStatus::STOP);
        rig.module.settings = FrequencyAnalysis::Settings{};
        rig.module.settings.averages = averages;
        rig.module.settings.periods = 32;
        rig.module.settings.allowAliasing = alias;
        rig.module.points = {{frequency, 1}};
        rig.module.waveformLength = generatorSamples;
        rig.module.actualFrequency = frequency;
        rig.module.sampling.requestedRate = rate;
        rig.module.sampling.samples = samples;
        rig.module.state = FrequencyAnalysisModule::State::Capturing;
        for (int record = 0; record < averages; ++record) {
            rig.module.captureStarted = true;
            rig.module.acceptCapture(frame(samples, rate, frequency));
        }
        const auto results = view(rig)->state().value("results").toArray();
        QCOMPARE(results.size(), 1);
        const auto result = results[0].toObject();
        QVERIFY(qAbs(result.value("gain_db").toDouble() + 6.0206) < 0.03);
        QVERIFY(qAbs(result.value("phase_degrees").toDouble() + 45) < 0.2);
        QCOMPARE(result.value("sampling_rate_hz").toInt(), rate);
        QCOMPARE(result.value("averages").toInt(), averages);
        QCOMPARE(result.value("generator_samples_per_period").toInt(), rig.module.waveformLength);
        QCOMPARE(view(rig)->findChild<QTableWidget *>("frequencyAnalysisResults")->item(0, 16)->text(),
                 QString::number(rig.module.waveformLength));
        QCOMPARE(result.value("uncertainty").toObject().value("available").toBool(), averages >= 2);
        if (averages >= 2) {
            const auto uncertainty = result.value("uncertainty").toObject();
            QVERIFY(uncertainty.value("gain_stddev_db").toDouble() > 0);
            QVERIFY(uncertainty.value("phase_stddev_degrees").toDouble() > 0);
            QCOMPARE(uncertainty.value("model").toString(), QString("coherence_quantization_flattop"));
        }
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
        rig.module.waveformLength = 4;
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
        rig.module.waveformLength = 32;
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
        rig.module.waveformLength = 32;
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
        rig.module.waveformLength = 32;
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
        rig.module.waveformLength = 32;
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
        rig.module.waveformLength = 32;
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
        rig.module.waveformLength = 32;
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
