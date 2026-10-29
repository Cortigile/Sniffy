#include "voltmeterwindow.h"
#include "ui_voltmeterwindow.h"
#include <QGridLayout>
#include <QSet>
#include <QToolButton>
#include <QWidgetAction>
#include <limits>

namespace {
bool isInternalAdcInput(const QString &name)
{
    static const QSet<QString> internalSources = {"temp", "temperature", "vref", "vbat"};
    return internalSources.contains(name.trimmed().toLower());
}
}

VoltmeterWindow::VoltmeterWindow(VoltmeterConfig *config, QWidget *parent) :
    QWidget(parent),
    ui(new Ui::VoltmeterWindow),
    config(config)
{
    ui->setupUi(this);
    //setup displays
    QString styleSheet = "image: url("+Graphics::tintedPath(Graphics::getCommonPath()+"units_v.png")+"); border: none;";
    WidgetDisplay *display;

    for (int i =0;i<MAX_VOLTMETER_CHANNELS ;i++ ) {
        display  = new WidgetDisplay("Voltmeter_CH"+QString::number(i+1), "Channel "+QString::number(i+1), styleSheet, true, 1, HISTORY_SIZE, this);
        display->configLabel(0,"CH"+QString::number(i+1),Graphics::getChannelColor(i),true);
        displays.append(display);
    }

    int index = 0;
    for (WidgetDisplay *dis : displays) {
        // Set contents margins for 1st 0,5,5,5 and others 0,0,5,5
        if(index == 0) {
            dis->setContentsMargins(5, 5, 5, 5);
        } else {
            dis->setContentsMargins(5, 0, 5, 5);
        }
        dis->showAvgDisplay(false);
    dis->configLabel(2,"Voltage",Graphics::palette().textLabel,true);
        ui->verticalLayout_display->addWidget(dis);
        dis->setIndicationFlagColor(Graphics::getChannelColor(index));
        // Switch to vertical progress bar for voltmeter
        dis->useVerticalProgressBar(true);        
        dis->setProgressRange(0,100);
        dis->setProgressValue(25);
        dis->setProgressColor(Graphics::getChannelColor(index));
        dis->showLabel(4);
        dis->setTraceColor(Graphics::getChannelColor(index),0);
        index++;
    }

    consolidatedView = new QWidget(ui->widget_module);
    QVBoxLayout *consolidatedLayout = new QVBoxLayout(consolidatedView);
    consolidatedLayout->setContentsMargins(5, 5, 5, 5);
    consolidatedLayout->setSpacing(5);
    QHBoxLayout *valueHeader = new QHBoxLayout;
    QVBoxLayout *historyControls = new QVBoxLayout;
    historyControls->setSpacing(2);
    QToolButton *historySettings = new QToolButton(consolidatedView);
    historySettings->setObjectName("voltmeterConsolidatedHistorySettings");
    historySettings->setAccessibleName("History samples");
    historySettings->setToolTip("Set history samples (100-1000)");
    historySettings->setIcon(Graphics::tintedIcon(Graphics::getCommonPath() + "icon_history_off.png"));
    historySettings->setFixedSize(26, 26);
    historySettings->setAutoRaise(true);
    historySettings->setPopupMode(QToolButton::InstantPopup);
    QMenu *historyMenu = new QMenu(historySettings);
    QWidgetAction *historyAction = new QWidgetAction(historyMenu);
    dialConsolidatedHistory = new WidgetDialRange(historyMenu, "History samples");
    dialConsolidatedHistory->setObjectName("voltmeterConsolidatedHistorySamples");
    dialConsolidatedHistory->setRange(100, 1000, "", 1, 20, HISTORY_SIZE, false);
    dialConsolidatedHistory->hideUnitSelection();
    dialConsolidatedHistory->setNumOfDecimals(0);
    historyAction->setDefaultWidget(dialConsolidatedHistory);
    historyMenu->addAction(historyAction);
    historySettings->setMenu(historyMenu);
    historyControls->addWidget(historySettings);
    QToolButton *clearHistory = new QToolButton(consolidatedView);
    clearHistory->setObjectName("voltmeterConsolidatedClearHistory");
    clearHistory->setAccessibleName("Clear history");
    clearHistory->setToolTip("Clear history");
    clearHistory->setIcon(Graphics::tintedIcon(Graphics::getCommonPath() + "icon_clear_history.png"));
    clearHistory->setFixedSize(26, 26);
    clearHistory->setAutoRaise(true);
    historyControls->addWidget(clearHistory);
    historyControls->addStretch();
    valueHeader->addLayout(historyControls);
    connect(dialConsolidatedHistory, &WidgetDialRange::valueChanged,
            this, &VoltmeterWindow::setConsolidatedHistorySize);
    connect(clearHistory, &QToolButton::clicked,
            this, &VoltmeterWindow::clearConsolidatedHistory);
    QGridLayout *valueLayout = new QGridLayout;
    valueLayout->setContentsMargins(0, 0, 0, 0);
    for (int i = 0; i < MAX_VOLTMETER_LOGICAL_INPUTS; i++) {
        const int slot = i / MAX_VOLTMETER_INPUTS_PER_SLOT;
        WidgetLabel *valueLabel = new WidgetLabel(consolidatedView,
                                                  "CH" + QString::number(slot + 1), "-- V");
        valueLabel->setColor(Graphics::getChannelColor(slot));
        consolidatedValueLabels.append(valueLabel);
        valueLabel->hide();
        valueLayout->addWidget(valueLabel, i / 4, i % 4);
        consolidatedHistory.append(QVector<QPointF>());
        consolidatedTraceVisible.append(false);
    }
    valueHeader->addLayout(valueLayout, 1);
    consolidatedLayout->addLayout(valueHeader);

    consolidatedChart = new widgetChart(consolidatedView, MAX_VOLTMETER_LOGICAL_INPUTS);
    consolidatedChart->setLabelsSize(8);
    consolidatedChart->formatLabels("%.0f", "%.3g");
    consolidatedChart->setGridDensity(5, 5);
    consolidatedChart->setLabelsVisible(true, true);
    consolidatedChart->setMargins(0, 0, 0, 0);
    consolidatedChart->setRange(0, HISTORY_SIZE, 0, 3.3);
    consolidatedChart->setDataMinMax(0, HISTORY_SIZE);
    for (int i = 0; i < MAX_VOLTMETER_LOGICAL_INPUTS; i++) {
        const int slot = i / MAX_VOLTMETER_INPUTS_PER_SLOT;
        const int input = i % MAX_VOLTMETER_INPUTS_PER_SLOT;
        const QColor color = input == 0 ? QColor(Graphics::getChannelColor(slot))
                                        : QColor::fromHsv((slot * 83 + input * 31) % 360, 190, 230);
        consolidatedChart->setTraceColor(i, color);
    }
    consolidatedLayout->addWidget(consolidatedChart, 1);
    ui->verticalLayout_display->addWidget(consolidatedView, 1);
    consolidatedView->hide();

    //setup control
    tabs = new widgetTab(ui->widget_settings, 3);
    tabs->setText("Control", 0);
    tabs->setText("Data log", 1);
    tabs->setText("Adv", 2);
    ui->verticalLayout_settings->addWidget(tabs);

    WidgetSeparator *separatorSampling = new WidgetSeparator(this, "Mode");
    tabs->getLayout(0)->addWidget(separatorSampling);

    buttonsAcquisitionMode = new WidgetButtons(this, 2, ButtonTypes::RADIO, "Sampling");
    buttonsAcquisitionMode->setObjectName("voltmeterAcquisitionMode");
    tabs->getLayout(0)->addWidget(buttonsAcquisitionMode);
    buttonsAcquisitionMode->setText("Normal", 0);
    buttonsAcquisitionMode->setText("Multimode", 1);
    buttonsAcquisitionMode->setDisabledButton(true, 1);

    WidgetSeparator *separatorChannelEnable = new WidgetSeparator(this,"Channel enable");
    tabs->getLayout(0)->addWidget(separatorChannelEnable);

    buttonsChannelEnable = new WidgetButtons(this,MAX_VOLTMETER_CHANNELS,ButtonTypes::CHECKABLE);
    buttonsChannelEnable->setObjectName("ChannelvoltmeterEnable");
    tabs->getLayout(0)->addWidget(buttonsChannelEnable);
    for (int i =0;i<MAX_VOLTMETER_CHANNELS ;i++ ) {
        buttonsChannelEnable->setText("CH"+QString::number(i+1),i);
        buttonsChannelEnable->setColor(Graphics::getChannelColor(i),i);
        buttonsChannelEnable->setButtonHidden(true,i);
    }

    multimodeSelectionWidget = new QWidget(tabs);
    multimodeSelectionLayout = new QVBoxLayout(multimodeSelectionWidget);
    multimodeSelectionLayout->setContentsMargins(0, 0, 0, 0);
    multimodeSelectionLayout->setSpacing(3);
    multimodeSelectionWidget->hide();
    tabs->getLayout(0)->addWidget(multimodeSelectionWidget);

    WidgetSeparator *separatorAcquisition = new WidgetSeparator(this,"Acquisition");
    tabs->getLayout(0)->addWidget(separatorAcquisition);

    buttonsMode = new WidgetButtons(this,2,ButtonTypes::RADIO,"Speed");
    buttonsMode->setObjectName("ChannelVoltmode");
    tabs->getLayout(0)->addWidget(buttonsMode);
    buttonsMode->setText("Normal",0);
    buttonsMode->setText("Fast",1);

    tabs->getLayout(2)->addWidget(new WidgetSeparator(this, "Mains rejection"));
    buttonsMainsFrequency = new WidgetButtons(this, 2, ButtonTypes::RADIO, "Mains");
    buttonsMainsFrequency->setObjectName("voltmeterMainsFrequency");
    buttonsMainsFrequency->setText("50 Hz", 0);
    buttonsMainsFrequency->setText("60 Hz", 1);
    tabs->getLayout(2)->addWidget(buttonsMainsFrequency);

    buttonsMainsPeriods = new WidgetButtons(this, 4, ButtonTypes::RADIO, "Periods", 1);
    buttonsMainsPeriods->setObjectName("voltmeterMainsPeriods");
    for (int i = 0; i < 4; ++i)
        buttonsMainsPeriods->setText(QString::number(1 << i), i);
    tabs->getLayout(2)->addWidget(buttonsMainsPeriods);

    tabs->getLayout(2)->addWidget(new WidgetSeparator(this, "Sampling"));
    buttonsSamplingRate = new WidgetButtons(this, 2, ButtonTypes::RADIO, "Rate");
    buttonsSamplingRate->setObjectName("voltmeterSamplingRate");
    buttonsSamplingRate->setText("Slow (5 kHz)", 0);
    buttonsSamplingRate->setText("Fast (50 kHz)", 1);
    tabs->getLayout(2)->addWidget(buttonsSamplingRate);
    tabs->getLayout(2)->addStretch();

    labelVdd = new WidgetLabel(this,"Measured Vcc","--V");
    tabs->getLayout(0)->addWidget(labelVdd);

    WidgetSeparator *separatorAvg = new WidgetSeparator(this,"Averaging");
    tabs->getLayout(0)->addWidget(separatorAvg);

    dialAveraging = new WidgetDialRange(tabs ,"Samples");
    dialAveraging->setObjectName("voltAvgSamples");
    dialAveraging->setRange(1,64,"x",1,1,4,false);
    dialAveraging->hideUnitSelection();
    dialAveraging->setNumOfDecimals(0);
    tabs->getLayout(0)->addWidget(dialAveraging);

    labelProgress = new WidgetLabel(this,"Progress","-/-");
    tabs->getLayout(0)->addWidget(labelProgress);

    WidgetSeparator *separatorSet = new WidgetSeparator(this,"Measure");
    tabs->getLayout(0)->addWidget(separatorSet);

    buttonsCalc = new WidgetButtons(this,3,ButtonTypes::RADIO,"",2);
    buttonsCalc->setObjectName("buttonscalc");
    tabs->getLayout(0)->addWidget(buttonsCalc);
    buttonsCalc->setText("Min/Max",0);
    buttonsCalc->setText("Ripple",1);
    buttonsCalc->setText("None",2);
    buttonsCalc->setColor(Graphics::palette().unused,2);

    WidgetSeparator *separatorDisplay = new WidgetSeparator(this, "Display");
    tabs->getLayout(0)->addWidget(separatorDisplay);

    buttonsView = new WidgetButtons(this, 2, ButtonTypes::RADIO, "");
    buttonsView->setObjectName("buttonView");
    tabs->getLayout(0)->addWidget(buttonsView);
    buttonsView->setText("Separated", 0);
    buttonsView->setText("Consolidated", 1);

    // Separator at the end is very important otherwise controls would not be nicely shown when maximized
    QSpacerItem *verticalSpacer;
    verticalSpacer = new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Expanding);
    tabs->getLayout(0)->addItem(verticalSpacer);


    WidgetSeparator *separatorLogger = new WidgetSeparator(this,"Data Logger");
    tabs->getLayout(1)->addWidget(separatorLogger);

    buttonSelectFile = new WidgetButtons(this,1,ButtonTypes::NORMAL,"File");
    buttonSelectFile->setObjectName("buttonselectfile");
    tabs->getLayout(1)->addWidget(buttonSelectFile);
    buttonSelectFile->setText("   Select   ");

    labelFile = new WidgetLabel(this,"","No File selected");
    tabs->getLayout(1)->addWidget(labelFile);

    buttonStartLog = new WidgetButtons(this,1,ButtonTypes::NORMAL);
    buttonStartLog->setObjectName("buttonlogcontrol");
    tabs->getLayout(1)->addWidget(buttonStartLog);
    buttonStartLog->setText("Start",0);
    buttonStartLog->setColor(Graphics::palette().warning,0);
    buttonStartLog->enableAll(false);

    // Separator at the end is very important otherwise controls would not be nicely shown when maximized
    QSpacerItem *verticalSpacerLog;
    verticalSpacerLog = new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Expanding);
    tabs->getLayout(1)->addItem(verticalSpacerLog);

    connect(buttonsChannelEnable,&WidgetButtons::statusChanged,this,&VoltmeterWindow::channelEnableCallback);
    connect(buttonsAcquisitionMode,&WidgetButtons::clicked,this,&VoltmeterWindow::acquisitionModeCallback);
    connect(dialAveraging,&WidgetDialRange::valueChanged,this,&VoltmeterWindow::averagingCallback);
    connect(buttonsCalc,&WidgetButtons::statusChanged,this,&VoltmeterWindow::buttonsCalcsCallback);
    connect(buttonsView,&WidgetButtons::clicked,this,&VoltmeterWindow::viewCallback);
    connect(buttonSelectFile,&WidgetButtons::clicked,this,&VoltmeterWindow::selectFileCallback);
    connect(buttonStartLog,&WidgetButtons::clicked,this,&VoltmeterWindow::datalogCallback);
    connect(buttonsMode,&WidgetButtons::clicked,this,&VoltmeterWindow::modeCallback);
    connect(buttonsMainsFrequency, &WidgetButtons::clicked, this, &VoltmeterWindow::samplingSettingsCallback);
    connect(buttonsMainsPeriods, &WidgetButtons::clicked, this, &VoltmeterWindow::samplingSettingsCallback);
    connect(buttonsSamplingRate, &WidgetButtons::clicked, this, &VoltmeterWindow::samplingSettingsCallback);

}

VoltmeterWindow::~VoltmeterWindow()
{
    delete ui;
}

void VoltmeterWindow::restoreGUIAfterStartup(int memoryLimit)
{
    restoringSettings = true;
    samplingMemoryLimit = memoryLimit;
    if (config->hasSavedSettings) {
        buttonsChannelEnable->restoreGeometry(QByteArray::number(config->channelMask));
        buttonsAcquisitionMode->setChecked(true, config->multimode ? 1 : 0);
        buttonsMode->setChecked(true, static_cast<int>(config->mode));
        dialAveraging->setRealValue(config->averaging);
        buttonsMainsFrequency->setChecked(true, config->mainsFrequency == 60 ? 1 : 0);
        int periodIndex = 0;
        while ((1 << periodIndex) < config->mainsPeriods)
            ++periodIndex;
        buttonsMainsPeriods->setChecked(true, periodIndex);
        buttonsSamplingRate->setChecked(true, config->samplingRate == 50000 ? 1 : 0);
    }
    samplingSettingsCallback();
    channelEnableCallback(buttonsChannelEnable->getStatus());
    averagingCallback(dialAveraging->getRealValue());
    modeCallback(buttonsMode->getSelectedIndex());
    acquisitionModeCallback(buttonsAcquisitionMode->getSelectedIndex());
    showEmptyCalcs();
    setConsolidatedHistorySize(qRound(dialConsolidatedHistory->getRealValue()));
    restoringSettings = false;
    if (!isMultimode())
        samplingSettingsCallback();
    //buttonStartLog->setChecked(true,1);
    //TODO GUI is loaded to previous state
    //validate the GUI appereance (colors according to selected channel
    //disabled/enabled buttons accordign to selected function
}

void VoltmeterWindow::saveSettingsToConfig()
{
    config->channelMask = buttonsChannelEnable->getStatus();
    config->multimode = isMultimode();
    config->averaging = qRound(dialAveraging->getRealValue());
    config->mode = static_cast<VoltmeterMode>(buttonsMode->getSelectedIndex());
    samplingSettingsCallback();
    config->hasSavedSettings = true;
}

void VoltmeterWindow::samplingSettingsCallback()
{
    config->mainsFrequency = buttonsMainsFrequency->getSelectedIndex() == 1 ? 60 : 50;
    config->samplingRate = buttonsSamplingRate->getSelectedIndex() == 1 ? 50000 : 5000;
    config->mainsPeriods = 1 << buttonsMainsPeriods->getSelectedIndex();
    if (restoringSettings)
        return;
    int channelCount = 1;
    const int mask = enabledChannelMask();
    for (int slot = 0; slot < MAX_VOLTMETER_CHANNELS; ++slot) {
        if (mask & (1 << slot))
            channelCount = slot + 1;
    }
    // Muxed ADC groups can pad three active channels to four buffer slots.
    if (channelCount == 3)
        channelCount = 4;
    int largestAvailableIndex = 0;
    for (int i = 0; i < 4; ++i) {
        const bool available = samplingMemoryLimit <= 0 ||
                               config->sampleCountForPeriods(1 << i) * channelCount * 2 <= samplingMemoryLimit;
        buttonsMainsPeriods->setDisabledButton(!available, i);
        if (available)
            largestAvailableIndex = i;
    }
    if (buttonsMainsPeriods->getSelectedIndex() > largestAvailableIndex)
        buttonsMainsPeriods->setChecked(true, largestAvailableIndex);
    config->mainsPeriods = 1 << buttonsMainsPeriods->getSelectedIndex();
}

void VoltmeterWindow::setSamplingMemoryLimit(int bytes)
{
    samplingMemoryLimit = bytes;
    samplingSettingsCallback();
}

int VoltmeterWindow::enabledChannelMask() const
{
    if (isMultimode()) {
        int mask = 0;
        for (int slot = 0; slot < availableInputSlots; ++slot) {
            if (!selectedInputIndices(slot).isEmpty())
                mask |= (1 << slot);
        }
        return mask;
    }
    return buttonsChannelEnable ? buttonsChannelEnable->getStatus() : 0;
}

bool VoltmeterWindow::isMultimode() const
{
    return buttonsAcquisitionMode && buttonsAcquisitionMode->getSelectedIndex() == 1;
}

void VoltmeterWindow::setInputCapabilities(const QStringList inputs[], const int selectors[], int numSlots)
{
    availableInputs.clear();
    availableInputSelectors.clear();
    inputButtonRows.clear();
    inputButtonSelectorRows.clear();
    availableInputs.resize(MAX_VOLTMETER_CHANNELS);
    availableInputSelectors.resize(MAX_VOLTMETER_CHANNELS);
    inputButtonRows.resize(MAX_VOLTMETER_CHANNELS);
    inputButtonSelectorRows.resize(MAX_VOLTMETER_CHANNELS);
    availableInputSlots = qBound(0, numSlots, MAX_VOLTMETER_CHANNELS);

    while (QLayoutItem *item = multimodeSelectionLayout->takeAt(0)) {
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }

    for (int slot = 0; slot < availableInputSlots; ++slot) {
        for (int selector = 0; selector < inputs[slot].size(); ++selector) {
            const QString &input = inputs[slot].at(selector);
            if (!isInternalAdcInput(input))
                availableInputs[slot].append(input);
            if (!isInternalAdcInput(input))
                availableInputSelectors[slot].append(selector);
        }
        if (availableInputs[slot].isEmpty())
            continue;

        WidgetSeparator *slotLabel = new WidgetSeparator(multimodeSelectionWidget,
                                                           "CH" + QString::number(slot + 1));
        multimodeSelectionLayout->addWidget(slotLabel);

        const int numberOfRows = (availableInputs[slot].size() + 7) / 8;
        const int baseButtonsPerRow = availableInputs[slot].size() / numberOfRows;
        const int longerRows = availableInputs[slot].size() % numberOfRows;
        int firstInput = 0;
        for (int rowIndex = 0; rowIndex < numberOfRows; ++rowIndex) {
            const int buttonsInRow = baseButtonsPerRow + (rowIndex < longerRows ? 1 : 0);
            WidgetButtons *row = new WidgetButtons(multimodeSelectionWidget, buttonsInRow,
                                                    ButtonTypes::CHECKABLE);
            row->setObjectName("voltmeterInputs_CH" + QString::number(slot + 1) +
                               "_row" + QString::number(rowIndex));
            row->setChecked(false, 0);
            for (int buttonIndex = 0; buttonIndex < buttonsInRow; ++buttonIndex) {
                const int inputIndex = firstInput + buttonIndex;
                row->setText(availableInputs[slot].at(inputIndex), buttonIndex);
                row->setColor(Graphics::getChannelColor(slot), buttonIndex);
                const QStringList &savedNames = config->selectedInputNames[slot];
                const bool restoreSaved = config->hasSavedInputSelections;
                const bool shouldSelect = restoreSaved
                                              ? savedNames.contains(availableInputs[slot].at(inputIndex))
                                              : availableInputs[slot].at(inputIndex) == inputs[slot].value(selectors[slot]);
                int selectedInRow = 0;
                for (int i = 0; i < buttonIndex; ++i)
                    selectedInRow += row->isChecked(i) ? 1 : 0;
                if (shouldSelect && selectedInputIndices(slot).size() + selectedInRow < MAX_VOLTMETER_INPUTS_PER_SLOT)
                    row->setChecked(true, buttonIndex);
            }
            inputButtonRows[slot].append(row);
            QList<int> selectorRow;
            for (int buttonIndex = 0; buttonIndex < buttonsInRow; ++buttonIndex)
                selectorRow.append(availableInputSelectors[slot].at(firstInput + buttonIndex));
            inputButtonSelectorRows[slot].append(selectorRow);
            multimodeSelectionLayout->addWidget(row);
            firstInput += buttonsInRow;
            connect(row, &WidgetButtons::clicked, this,
                    [this, slot, row](int buttonIndex) {
                if (selectedInputIndices(slot).size() > MAX_VOLTMETER_INPUTS_PER_SLOT)
                    row->setChecked(false, buttonIndex);
                inputSelectionCallback();
            });
        }
    }

    hasSelectableInput = false;
    for (const QStringList &slotInputs : availableInputs)
        hasSelectableInput = hasSelectableInput || !slotInputs.isEmpty();
    buttonsAcquisitionMode->setDisabledButton(!hasSelectableInput, 1);
    inputSelectionCallback();
}

QList<int> VoltmeterWindow::selectedInputIndices(int slot) const
{
    QList<int> selected;
    if (slot < 0 || slot >= inputButtonRows.size())
        return selected;

    for (int rowIndex = 0; rowIndex < inputButtonRows.at(slot).size(); ++rowIndex) {
        WidgetButtons *row = inputButtonRows.at(slot).at(rowIndex);
        for (int buttonIndex = 0; buttonIndex < inputButtonSelectorRows.at(slot).at(rowIndex).size(); ++buttonIndex) {
                if (row->isChecked(buttonIndex))
                    selected.append(inputButtonSelectorRows.at(slot).at(rowIndex).at(buttonIndex));
        }
    }
    return selected;
}

QStringList VoltmeterWindow::selectedInputNames(int slot) const
{
    QStringList names;
    if (slot < 0 || slot >= availableInputSelectors.size())
        return names;
    for (int selector : selectedInputIndices(slot)) {
        const int displayIndex = availableInputSelectors.at(slot).indexOf(selector);
        if (displayIndex >= 0 && displayIndex < availableInputs.at(slot).size())
            names.append(availableInputs.at(slot).at(displayIndex));
    }
    return names;
}

void VoltmeterWindow::inputSelectionCallback()
{
    int channelMask = enabledChannelMask();
    if (isMultimode() && channelMask == 0 && availableInputSlots > 0) {
        for (int slot = 0; slot < availableInputSlots; ++slot) {
            if (!inputButtonRows.at(slot).isEmpty()) {
                inputButtonRows[slot].first()->setChecked(true, 0);
                break;
            }
        }
        channelMask = enabledChannelMask();
    }
    if (availableInputSlots > 0 && !restoringSettings) {
        for (int slot = 0; slot < MAX_VOLTMETER_CHANNELS; ++slot)
            config->selectedInputNames[slot] = selectedInputNames(slot);
        config->hasSavedInputSelections = true;
    }
    int enabledCount = 0;
    for (int logicalIndex = 0; logicalIndex < MAX_VOLTMETER_LOGICAL_INPUTS; ++logicalIndex) {
        bool selected = false;
        const int slot = logicalIndex / MAX_VOLTMETER_INPUTS_PER_SLOT;
        const int inputOrdinal = logicalIndex % MAX_VOLTMETER_INPUTS_PER_SLOT;
        const QList<int> selectedIndices = selectedInputIndices(slot);
        selected = inputOrdinal < selectedIndices.size();
        if (isMultimode()) {
            consolidatedValueLabels.at(logicalIndex)->setVisible(selected);
            if (!selected)
                consolidatedTraceVisible[logicalIndex] = false;
        }
    }
    for (int slot = 0; slot < MAX_VOLTMETER_CHANNELS; ++slot) {
        if (channelMask & (1 << slot))
            enabledCount = slot + 1;
        if (slot < consolidatedValueLabels.size())
            consolidatedValueLabels.at(slot)->setVisible(isMultimode() && (channelMask & (1 << slot)));
    }
    updateConsolidatedChart();
    samplingSettingsCallback();
    emit numChannelsEnabledChanged(enabledCount);
    emit inputSelectionChanged();
}

void VoltmeterWindow::acquisitionModeCallback(int index)
{
    const bool multimode = index == 1;
    if (multimode && !wasMultimode) {
        const int selectedView = buttonsView->getSelectedIndex();
        if (selectedView >= 0)
            previousViewIndex = selectedView;
    }
    wasMultimode = multimode;

    multimodeSelectionWidget->setVisible(multimode);
    buttonsChannelEnable->setVisible(!multimode);
    for (int slot = 0; slot < MAX_VOLTMETER_CHANNELS; ++slot) {
        displays.at(slot)->setVisible(!multimode && (buttonsChannelEnable->getStatus() & (1 << slot)));
    }

    if (multimode) {
        buttonsView->setChecked(true, 1);
        buttonsView->hide();
        consolidatedView->show();
    } else {
        buttonsView->show();
        buttonsView->setChecked(true, previousViewIndex);
        viewCallback(previousViewIndex);
    }
    emit acquisitionModeChanged(index);
    inputSelectionCallback();
}

void VoltmeterWindow::showData(ChannelData data[], int numChannels){
    consolidatedSampleTime += 1;
    for(int i = 0;i<numChannels;i++){
        displays.at(i)->displayString(displays.at(i)->formatNumber(data[i].voltage,'f',4));
        displays.at(i)->appendNewHistorySample("",data[i].voltage , "V", 1);
        int pctInt = static_cast<int>(data[i].percent + 0.5);
        if(pctInt < 0) pctInt = 0; else if(pctInt > 100) pctInt = 100;
        displays.at(i)->setProgressValue(pctInt);     
        displays.at(i)->drawIndicationFlag(4);

        if(buttonsCalc->getSelectedIndex()==0){
            displays.at(i)->displayQerrString(displays.at(i)->formatNumber(data[i].min,'f',4));
            displays.at(i)->displayTerrString(displays.at(i)->formatNumber(data[i].max,'f',4));
            QString styleSheet = "image: url("+Graphics::tintedPath(Graphics::getCommonPath()+"units_Vmax.png")+"); border: none;";
            displays[i]->setErrStyle(styleSheet);
            styleSheet = "image: url("+Graphics::tintedPath(Graphics::getCommonPath()+"units_Vmin.png")+"); border: none;";
            displays[i]->setTerrStyle(styleSheet);
            displays[i]->showErrDisplay(true);
            displays[i]->showTerrStyle(true);
        }else if(buttonsCalc->getSelectedIndex()==1){
            displays.at(i)->displayQerrString(displays.at(i)->formatNumber(data[i].frequency,'f',4));
            displays.at(i)->displayTerrString(displays.at(i)->formatNumber(data[i].ripple,'f',4));
            QString styleSheet = "image: url("+Graphics::tintedPath(Graphics::getCommonPath()+"units_Vripple.png")+"); border: none;";
            displays[i]->setErrStyle(styleSheet);
            styleSheet = "image: url("+Graphics::tintedPath(Graphics::getCommonPath()+"units_hz.png")+"); border: none;";
            displays[i]->setTerrStyle(styleSheet);
            displays[i]->showErrDisplay(true);
            displays[i]->showTerrStyle(true);
        }else{
            displays[i]->showErrDisplay(false);
            displays[i]->showTerrStyle(false);
            displays.at(i)->displayQerrString("");
            displays.at(i)->displayTerrString("");
        }
    }
    const int channelMask = enabledChannelMask();
    for (int i = 0; i < MAX_VOLTMETER_CHANNELS; i++) {
        const bool channelEnabled = (channelMask & (1 << i)) != 0;
        const bool hasData = i < numChannels;
        consolidatedValueLabels.at(i)->setVisible(channelEnabled);
        if (channelEnabled && hasData) {
            consolidatedValueLabels.at(i)->setValue(
                displays.at(i)->formatNumber(data[i].voltage, 'f', 4) + " V");
            consolidatedHistory[i].append(QPointF(consolidatedSampleTime, data[i].voltage));
            if (consolidatedHistory[i].size() > consolidatedHistorySize)
                consolidatedHistory[i].removeFirst();
            consolidatedTraceVisible[i] = true;
        }
    }
    if (!isMultimode()) {
        for (int i = MAX_VOLTMETER_CHANNELS; i < MAX_VOLTMETER_LOGICAL_INPUTS; ++i) {
            consolidatedValueLabels.at(i)->hide();
            consolidatedTraceVisible[i] = false;
        }
    }
    updateConsolidatedChart();
    if(isDataLogRunning){
        appendDatalog(data, numChannels);
    }
}

void VoltmeterWindow::showLogicalReading(int logicalIndex, const ChannelData &data,
                                         int slot, const QString &inputName, bool advanceTime)
{
    if (logicalIndex < 0 || logicalIndex >= MAX_VOLTMETER_LOGICAL_INPUTS)
        return;
    if (advanceTime)
        consolidatedSampleTime += 1;

    const int inputOffset = logicalIndex % MAX_VOLTMETER_INPUTS_PER_SLOT;
    const QColor color = inputOffset == 0 ? QColor(Graphics::getChannelColor(slot))
                                          : QColor::fromHsv((slot * 83 + inputOffset * 31) % 360, 190, 230);
    WidgetLabel *label = consolidatedValueLabels.at(logicalIndex);
    label->setName("CH" + QString::number(slot + 1) + " " + inputName);
    label->setValue(QLocale().toString(data.voltage, 'f', 4) + " V");
    label->setColor(color.name());
    label->show();

    consolidatedHistory[logicalIndex].append(QPointF(consolidatedSampleTime, data.voltage));
    if (consolidatedHistory[logicalIndex].size() > consolidatedHistorySize)
        consolidatedHistory[logicalIndex].removeFirst();
    consolidatedTraceVisible[logicalIndex] = true;
    consolidatedChart->setTraceColor(logicalIndex, color);
    updateConsolidatedChart();
}

void VoltmeterWindow::setLogicalInputs(const QStringList &keys, const QStringList &labels)
{
    QHash<QString, QVector<QPointF>> previousHistory;
    QHash<QString, bool> previousVisibility;
    for (int i = 0; i < logicalInputKeys.size(); ++i) {
        previousHistory.insert(logicalInputKeys.at(i), consolidatedHistory.at(i));
        previousVisibility.insert(logicalInputKeys.at(i), consolidatedTraceVisible.at(i));
    }

    logicalInputKeys = keys;
    for (int i = 0; i < MAX_VOLTMETER_LOGICAL_INPUTS; ++i) {
        consolidatedHistory[i].clear();
        consolidatedTraceVisible[i] = false;
        consolidatedValueLabels.at(i)->hide();
        if (i >= keys.size())
            continue;
        consolidatedHistory[i] = previousHistory.value(keys.at(i));
        consolidatedTraceVisible[i] = previousVisibility.value(keys.at(i), false);
        consolidatedValueLabels.at(i)->setName(labels.value(i));
        if (!consolidatedHistory[i].isEmpty())
            consolidatedValueLabels.at(i)->setValue(
                QLocale().toString(consolidatedHistory[i].last().y(), 'f', 4) + " V");
        if (consolidatedTraceVisible[i])
            consolidatedValueLabels.at(i)->show();
    }
    updateConsolidatedChart();
}

void VoltmeterWindow::showEmptyCalcs(){
    for(int i = 0;i<MAX_VOLTMETER_CHANNELS;i++){
        if(buttonsCalc->getSelectedIndex()==0){
            displays.at(i)->displayQerrString("--");
            displays.at(i)->displayTerrString("--");
            QString styleSheet = "image: url("+Graphics::tintedPath(Graphics::getCommonPath()+"units_Vmax.png")+"); border: none;";
            displays[i]->setErrStyle(styleSheet);
            styleSheet =  "image: url("+Graphics::tintedPath(Graphics::getCommonPath()+"units_Vmin.png")+"); border: none;";
            displays[i]->setTerrStyle(styleSheet);
            displays[i]->showErrDisplay(true);
            displays[i]->showTerrStyle(true);
        }else if(buttonsCalc->getSelectedIndex()==1){
            displays.at(i)->displayQerrString("--");
            displays.at(i)->displayTerrString("--");
            QString styleSheet = "image: url("+Graphics::tintedPath(Graphics::getCommonPath()+"units_Vripple.png")+"); border: none;";
            displays[i]->setErrStyle(styleSheet);
            styleSheet = "image: url("+Graphics::tintedPath(Graphics::getCommonPath()+"units_hz.png")+"); border: none;";
            displays[i]->setTerrStyle(styleSheet);
            displays[i]->showErrDisplay(true);
            displays[i]->showTerrStyle(true);
        }else{
            displays[i]->showErrDisplay(false);
            displays[i]->showTerrStyle(false);
            displays.at(i)->displayQerrString("");
            displays.at(i)->displayTerrString("");
        }
    }
}

void VoltmeterWindow::showVddValue(qreal value){
    labelVdd->setValue(QString::number(value)+" V");
    currentVdd = value;
}

void VoltmeterWindow::showProgress(int current, int max){
    labelProgress->setValue(QString::number(current)+"/"+QString::number(max));
}

void VoltmeterWindow::setPinsAndNumChannels(QString pins[], int numOfCh)
{
    bool tmp;
    for (int i = 0; i< MAX_VOLTMETER_CHANNELS; i++) {
        if(numOfCh <= i){
            tmp = true;
            displays.at(i)->configLabel(1,"pin "+pins[i],Graphics::palette().textAll,true);
        }else {
            tmp = false;
        }
        buttonsChannelEnable->setButtonHidden(tmp,i);
    }
}

void VoltmeterWindow::appendDatalog(ChannelData data[], int numChannels)
{
    Q_UNUSED(numChannels);
    QDateTime now;
    *logStream << QString::number(logSampleIndex)+","+now.currentDateTime().toString("dd.MM.yyyy") + "," +now.currentDateTime().toString("hh:mm:ss.zzz") + ","+ QString::number(currentVdd) ;

    for(int i = 0; i < MAX_VOLTMETER_CHANNELS; i++){
        *logStream << ","+QString::number(data[i].voltage);
       // *logStream << ","+QString::number(data[i].min);
       // *logStream << ","+QString::number(data[i].max);
       // *logStream << ","+QString::number(data[i].ripple);
       // *logStream << ","+QString::number(data[i].frequency);
    }
    *logStream << "\n";

    logSampleIndex++;
    labelFile->setValue("Logging active (" + QString::number(logSampleIndex) + " smpl)");
}

void VoltmeterWindow::appendLogicalDatalog(const ChannelData data[], const QStringList &names,
                                           int logicalCount)
{
    if (!isDataLogRunning)
        return;
    QDateTime now;
    *logStream << QString::number(logSampleIndex) + "," +
                  now.currentDateTime().toString("dd.MM.yyyy") + "," +
                  now.currentDateTime().toString("hh:mm:ss.zzz") + "," + QString::number(currentVdd);
    for (const QString &name : datalogLogicalNames) {
        const int index = names.indexOf(name);
        if (index >= 0 && index < logicalCount)
            *logStream << "," + QString::number(data[index].voltage);
        else
            *logStream << ",";
    }
    *logStream << "\n";
    ++logSampleIndex;
    labelFile->setValue("Logging active (" + QString::number(logSampleIndex) + " smpl)");
}

void VoltmeterWindow::stopDatalog()
{
    if(isDataLogRunning){
        logFile->close();
        isDataLogRunning = false;
        buttonStartLog->setText("Start",0);
    buttonStartLog->setColor(Graphics::palette().warning,0);
        buttonSelectFile->enableAll(true);
        buttonsAcquisitionMode->enableAll(true);
        buttonsAcquisitionMode->setDisabledButton(!hasSelectableInput, 1);
        buttonsChannelEnable->enableAll(!isMultimode());
        buttonsView->enableAll(!isMultimode());
        for (const QList<WidgetButtons *> &rows : inputButtonRows) {
            for (WidgetButtons *row : rows)
                row->enableAll(isMultimode());
        }
        datalogLogicalNames.clear();
        labelFile->setValue("Log stopped (" + QString::number(logSampleIndex) + " smpl)");
    }
}

void VoltmeterWindow::startDatalog()
{
    if(!isDataLogRunning){
        logSampleIndex = 0;
        logFile = new QFile(config->datalogFileName);
        if(logFile->exists()){
            logFile->remove();
        }
        if(logFile->open(QFile::WriteOnly | QFile::Truncate)){
            logStream = new QTextStream(logFile);
            *logStream << "sample ID,date,time,Vdd";
            datalogMultimode = isMultimode();
            datalogLogicalNames.clear();
            if (datalogMultimode) {
                for (int slot = 0; slot < availableInputSlots; ++slot) {
                    for (const QString &name : selectedInputNames(slot)) {
                        const QString column = "CH" + QString::number(slot + 1) + " " + name;
                        datalogLogicalNames.append(column);
                        *logStream << ",Voltage " + column;
                    }
                }
            } else {
                for(int i = 0; i < MAX_VOLTMETER_CHANNELS; i++)
                    *logStream << ",Voltage CH"+QString::number(i+1);
            }
            *logStream << "\n";
            isDataLogRunning = true;
            buttonsAcquisitionMode->disableAll();
            buttonsChannelEnable->enableAll(!datalogMultimode);
            buttonsView->enableAll(!datalogMultimode);
            for (const QList<WidgetButtons *> &rows : inputButtonRows) {
                for (WidgetButtons *row : rows)
                    row->enableAll(datalogMultimode);
            }
            buttonStartLog->setText("Stop",0);
            buttonStartLog->setColor(Graphics::palette().running,0);
            buttonSelectFile->enableAll(false);
        }else{
            labelFile->setValue("Error opening file");
            qDebug ()<< "ERROR log file could not be opened";
        }
    }
}

void VoltmeterWindow::channelEnableCallback(int status)
{
    int numChEnabled=0;
    for(int i =0;i<MAX_VOLTMETER_CHANNELS;i++){
        if((int)(pow(2,i))&(status)){
            displays.at(i)->setVisible(buttonsView->getSelectedIndex() == 0);
            consolidatedValueLabels.at(i)->setVisible(buttonsView->getSelectedIndex() == 1);
            numChEnabled = i+1;
        }else{
            displays.at(i)->hide();
            consolidatedValueLabels.at(i)->hide();
        }
    }
    updateConsolidatedChart();
    samplingSettingsCallback();
    emit numChannelsEnabledChanged(numChEnabled);
}

void VoltmeterWindow::viewCallback(int index)
{
    const bool consolidated = index == 1;
    consolidatedView->setVisible(consolidated);
    const int channelMask = enabledChannelMask();
    for (int i = 0; i < MAX_VOLTMETER_LOGICAL_INPUTS; i++) {
        if (i >= MAX_VOLTMETER_CHANNELS) {
            consolidatedValueLabels.at(i)->hide();
            continue;
        }
        const bool channelEnabled = (channelMask & (1 << i)) != 0;
        displays.at(i)->setVisible(!consolidated && channelEnabled);
        consolidatedValueLabels.at(i)->setVisible(consolidated && channelEnabled);
    }
    updateConsolidatedChart();
}

void VoltmeterWindow::setConsolidatedHistorySize(int size)
{
    consolidatedHistorySize = qBound(100, size, 1000);
    for (QVector<QPointF> &history : consolidatedHistory) {
        if (history.size() > consolidatedHistorySize)
            history.remove(0, history.size() - consolidatedHistorySize);
    }
    updateConsolidatedChart();
}

void VoltmeterWindow::clearConsolidatedHistory()
{
    for (QVector<QPointF> &history : consolidatedHistory)
        history.clear();
    consolidatedTraceVisible.fill(false);
    consolidatedSampleTime = 0;
    updateConsolidatedChart();
}

void VoltmeterWindow::updateConsolidatedChart()
{
    const int channelMask = enabledChannelMask();
    qreal minValue = std::numeric_limits<qreal>::max();
    qreal maxValue = std::numeric_limits<qreal>::lowest();
    for (int i = 0; i < MAX_VOLTMETER_LOGICAL_INPUTS; i++) {
        if (!isMultimode() && i < MAX_VOLTMETER_CHANNELS) {
            const QString color = Graphics::getChannelColor(i);
            consolidatedValueLabels.at(i)->setName("CH" + QString::number(i + 1));
            consolidatedValueLabels.at(i)->setColor(color);
            consolidatedChart->setTraceColor(i, QColor(color));
        }
        const bool visible = isMultimode()
                                 ? consolidatedTraceVisible.at(i)
                                 : i < MAX_VOLTMETER_CHANNELS && (channelMask & (1 << i));
        if (visible) {
            consolidatedChart->updateTrace(&consolidatedHistory[i], i);
            for (const QPointF &point : consolidatedHistory.at(i)) {
                minValue = qMin(minValue, point.y());
                maxValue = qMax(maxValue, point.y());
            }
        } else {
            QVector<QPointF> emptyTrace;
            consolidatedChart->updateTrace(&emptyTrace, i);
        }
    }

    const qreal xMax = qMax<qreal>(consolidatedHistorySize, consolidatedSampleTime);
    const qreal xMin = qMax<qreal>(0, xMax - consolidatedHistorySize);
    consolidatedChart->setDataMinMax(xMin, xMax);
    consolidatedChart->setRangeX(xMin, xMax);
    if (minValue == std::numeric_limits<qreal>::max()) {
        consolidatedChart->setRangeY(0, 3.3);
    } else {
        const qreal span = maxValue - minValue;
        const qreal margin = span > 0 ? span * 0.1 : qMax<qreal>(0.1, qAbs(maxValue) * 0.1);
        consolidatedChart->setRangeY(minValue - margin, maxValue + margin);
    }
}

void VoltmeterWindow::averagingCallback(int value)
{
    emit averagingChanged(value);
}

void VoltmeterWindow::buttonsCalcsCallback(int clicked)
{
    showEmptyCalcs();
    if(clicked!=0){
        emit resetMinMax();
    }
}

void VoltmeterWindow::setDatalogFilePath(const QString &path)
{
    config->datalogFileName = path;
    QString display = path;
    if (display.length() >= 25)
        display = display.left(8) + " ... " + display.right(12);
    if (path.length() > 3) {
        labelFile->setValue(display);
        buttonStartLog->enableAll(true);
    }
}

void VoltmeterWindow::selectFileCallback()
{
    QString fileName;
    fileName = QFileDialog::getSaveFileName(this,"Select output file","","Text files (*.csv *.txt)");
    config->datalogFileName = fileName;
    qDebug() << fileName;
    if(fileName.length()>=25){
        fileName = fileName.left(8)+" ... "+fileName.right(12);
    }
    if(fileName.length()>3){
        labelFile->setValue(fileName);
        buttonStartLog->enableAll(true);
    }

}

void VoltmeterWindow::datalogCallback(int index)
{
    Q_UNUSED(index);
    if(buttonStartLog->getText(0)=="Start"){
        startDatalog();
    }else if(buttonStartLog->getText(0)=="Stop"){
        stopDatalog();
    }
}

void VoltmeterWindow::modeCallback(int index)
{
    emit modeChanged(index);
}
