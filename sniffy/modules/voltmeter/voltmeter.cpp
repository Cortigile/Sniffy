#include "voltmeter.h"

Voltmeter::Voltmeter(QObject *parent)
{
    Q_UNUSED(parent);
    moduleSpecification = new VoltmeterSpec();
    config = new VoltmeterConfig();
    voltWindow = new VoltmeterWindow(config);
    voltWindow->setObjectName("voltWindow");

    //Set the comm prefix, window name and icon
    //module is not fully initialized - control widget and dock wodget cannot be modified
    moduleCommandPrefix = cmd->SCOPE;
    moduleName = "Voltmeter";
    moduleIconURI = Graphics::getCommonPath()+"icon_voltmeter.png";

    measCalc = new MeasCalculations();
    voltmeterData = new QVector<QVector<QPointF>>;

    for(int i = 0;i<MAX_VOLTMETER_CHANNELS;i++){
        voltMeasList.append(new Measurement(MeasurementType::MEAN,i));
        voltMeasList.append(new Measurement(MeasurementType::RMS,i));
        voltMeasList.append(new Measurement(MeasurementType::PKPK,i));
        voltMeasList.append(new Measurement(MeasurementType::FREQUENCY,i));
    }

    //In case hold button should be shown insert this and connect callback to handle hold/pause
    connect(this, &AbstractModule::moduleCreated, this, &Voltmeter::showHoldButtonCallback);
    connect(this, &AbstractModule::holdClicked, this, &Voltmeter::holdButtonCallback);

    connect(measCalc, &MeasCalculations::measCalculated, this, &Voltmeter::updateMeasurement);

    connect(voltWindow, &VoltmeterWindow::averagingChanged, this,&Voltmeter::setAveraging);
    connect(voltWindow, &VoltmeterWindow::numChannelsEnabledChanged, this,&Voltmeter::setNumChannelsEnabled);
    connect(voltWindow, &VoltmeterWindow::resetMinMax, this,&Voltmeter::resetMinMax);
    connect(voltWindow, &VoltmeterWindow::modeChanged, this,&Voltmeter::setMode);
    connect(voltWindow, &VoltmeterWindow::inputSelectionChanged, this,&Voltmeter::handleInputSelectionChanged);
    connect(voltWindow, &VoltmeterWindow::acquisitionModeChanged, this,&Voltmeter::handleAcquisitionModeChanged);

}

QWidget *Voltmeter::getWidget()
{
    return voltWindow;
}

void Voltmeter::parseData(QByteArray data)
{
    QByteArray dataHeader = data.left(4);

    if(dataHeader==Commands::CONFIG){
        data.remove(0,4);
        moduleSpecification->parseSpecification(data);
        showModuleControl();
        VoltmeterSpec *spec = static_cast<VoltmeterSpec*>(moduleSpecification);
        QList<PinFunctionInfo> pinFuncs;
        for(int i = 0; i < spec->maxADCChannels; ++i){
            const QString &pin = spec->channelPins[i];
            if(!pin.isEmpty() && pin != "-")
                pinFuncs.append({pin, "CH" + QString::number(i + 1), "voltmeter"});
        }
        showModulePinFunctions(moduleName, pinFuncs);
        //TODO parse message from MCU
    }else if(dataHeader=="INP_"){
        VoltmeterSpec *spec = static_cast<VoltmeterSpec*>(moduleSpecification);
        if (spec->parseAvailableInputs(data)) {
            inputCapabilitiesReceived = true;
            voltWindow->setInputCapabilities(spec->availableInputs,
                                             spec->currentInputSelectors,
                                             spec->availableInputSlots);
        } else {
            qWarning() << "Invalid voltmeter input capability response";
        }
    }else if(dataHeader==Commands::SCOPE_INCOME){
        quint8 tmpByte;
        quint16 tmpShort;
        qreal x(0);
        qreal y(0);
        int resolution;
        int length;
        int samplingFreq;
        int currentChannel=0;
        int numChannels=1;

        QDataStream streamBuffLeng(data);

        while (numChannels>currentChannel){
            streamBuffLeng>>samplingFreq;
            streamBuffLeng>>samplingFreq;
            config->realSamplingRate = samplingFreq;
            streamBuffLeng>>tmpByte;
            resolution = tmpByte;
            config->ADCresolution = resolution;
            streamBuffLeng>>length;
            streamBuffLeng>>tmpShort;
            config->rangeMin = (qint16)tmpShort;
            streamBuffLeng>>tmpShort;
            config->rangeMax = (qint16)tmpShort;

            streamBuffLeng>>tmpByte;
            streamBuffLeng>>tmpByte;
            currentChannel = tmpByte;
            streamBuffLeng>>tmpByte;
            numChannels = tmpByte;
            config->numberOfChannels = numChannels;

            QVector<QPointF> points;
            if(length<500000){
                points.reserve(length);
            }else{
                qDebug() << "ERROR Too long signal to be alocated" << length;
            }

            if(resolution>8){
                for (int j(0); j < length/2; j++){
                    streamBuffLeng>>tmpByte;
                    tmpShort = tmpByte;
                    streamBuffLeng>>tmpByte;
                    tmpShort += tmpByte*256;
                    x=j*1.0/samplingFreq;
                    y=(tmpShort*(float)(config->rangeMax-config->rangeMin)/(pow(2,config->ADCresolution)-1)+config->rangeMin)/1000;
                    points.append(QPointF(x,y));
                }
            }else{
                qDebug() << "TODO <8bit data parser";
            }

            while(numChannels<voltmeterData->length()){ //remove signals which will not be filled
                voltmeterData->removeAt(voltmeterData->length()-1);
            }

            if(voltmeterData->length()>=currentChannel){
                voltmeterData->replace(currentChannel-1,points);
            }else{
                voltmeterData->append(points);
            }
        }

        if(currentChannel==numChannels){
            measCalc->calculate(*voltmeterData,voltMeasList,config->realSamplingRate);
        }
    }else{
        //qDebug()<<data;
    }
}

void Voltmeter::writeConfiguration()
{
    //workaround first data was corrupted on channels > 1
    numChannelsEnabled = static_cast<VoltmeterSpec*>(moduleSpecification)->maxADCChannels;
    comm->write(cmd->SCOPE+":"+cmd->SCOPE_TRIG_CHANNEL+":"+cmd->CHANNELS_1+";");
    setNumberOfChannels(numChannelsEnabled);
    comm->write(moduleCommandPrefix+":"+cmd->SCOPE_ADC_CHANNEL_DEAFULT+";");
    comm->write(moduleCommandPrefix + ":" + cmd->GET_SCOPE_INPUTS + ";");

    voltWindow->restoreGUIAfterStartup(static_cast<VoltmeterSpec*>(moduleSpecification)->memorySize);
    if (inputCapabilitiesReceived) {
        VoltmeterSpec *spec = static_cast<VoltmeterSpec*>(moduleSpecification);
        voltWindow->setInputCapabilities(spec->availableInputs, spec->currentInputSelectors,
                                         spec->availableInputSlots);
    }

    voltWindow->setPinsAndNumChannels(static_cast<VoltmeterSpec*>(moduleSpecification)->channelPins,static_cast<VoltmeterSpec*>(moduleSpecification)->maxADCChannels);
    setVDDSampling();
    comm->write(cmd->SCOPE+":"+cmd->SCOPE_TRIG_MODE+":"+cmd->MODE_AUTO_FAST+";");
    comm->write(cmd->SCOPE+":"+cmd->SCOPE_DATA_DEPTH+":"+cmd->DATA_DEPTH_12B+";");
}

void Voltmeter::parseConfiguration(QByteArray config)
{
    this->config->parse(config);
}

QByteArray Voltmeter::getConfiguration()
{
    voltWindow->saveSettingsToConfig();
    return config->serialize();
}

void Voltmeter::startModule()
{
    isStartup = true;
    samplesTaken = 0;
    for(int i=0;i<MAX_VOLTMETER_CHANNELS;i++){
        dataRawVoltage[i].clear();
    }
    for (QList<qreal> &samples : logicalRawVoltage)
        samples.clear();
    setVDDSampling();
    startSampling();
}

void Voltmeter::stopModule()
{
    voltWindow->saveSettingsToConfig();
    voltWindow->stopDatalog();
    stopSampling();
}

//In case hold is needed
void Voltmeter::showHoldButtonCallback(){
    this->showModuleHoldButton(true);
}

void Voltmeter::holdButtonCallback(bool held){
    if(held){
        comm->write(moduleCommandPrefix+":"+cmd->STOP+";");
        setModuleStatus(ModuleStatus::PAUSE);
    }else{
        comm->write(moduleCommandPrefix+":"+cmd->START+";");
        setModuleStatus(ModuleStatus::PLAY);
    }
}
void Voltmeter::updateSamplingChannel(void){
    if(isReferenceMeasured){
        setDefaultSampling();
    }else{
        switch (config->mode){
        case VoltmeterMode::NORMAL:
            setVDDSampling();
            break;
        case VoltmeterMode::FAST:
            setDefaultSampling();
            break;
        }
    }
}

void Voltmeter::updateMeasurement(QList<Measurement *> m)
{
    if(isReferenceMeasured){
        if (m.isEmpty() || m.at(0)->getLabel() == "N/A" ||
            !qIsFinite(m.at(0)->getValue()) || m.at(0)->getValue() <= 0) {
            qWarning() << "Invalid voltmeter reference measurement; retrying calibration";
            setVDDSampling();
            restartSampling();
            return;
        }
        qreal VrefReal = static_cast<VoltmeterSpec*>(moduleSpecification)->VrefCalibration / (pow(2,config->ADCresolution)-1) * static_cast<VoltmeterSpec*>(moduleSpecification)->Vref;
        realVdd = VrefReal / m.at(0)->getValue() * static_cast<VoltmeterSpec*>(moduleSpecification)->Vref/1000;
        voltWindow->showVddValue(qreal(int(realVdd))/1000);
        updateSamplingChannel();

    }else if (activeCaptureIsMultimode == voltWindow->isMultimode()) {
        samplesTaken++;
        if (activeCaptureIsMultimode) {
            int maxScanLength = 1;
            for (int slot = 0; slot < MAX_VOLTMETER_CHANNELS; ++slot)
                maxScanLength = qMax(maxScanLength, voltWindow->selectedInputIndices(slot).size());
            const int calibrationInterval = samplesToTakeTotal * maxScanLength;
            voltWindow->showProgress(samplesTaken, calibrationInterval);
            bool advancedHistoryTime = false;
            for (Measurement *meas : m) {
                if (meas->getLabel() == "N/A")
                    continue;
                const int slot = meas->getChannelIndex();
                if (slot < 0 || slot >= MAX_VOLTMETER_CHANNELS)
                    continue;
                int logicalIndex = -1;
                for (int i = 0; i < logicalChannelNames.size(); ++i) {
                    if (logicalChannelNames.at(i).slot == slot &&
                        logicalChannelNames.at(i).selector == activeCaptureSelectors[slot]) {
                        logicalIndex = i;
                        break;
                    }
                }
                if (logicalIndex < 0)
                    continue;

                if (meas->getType() == MeasurementType::MEAN) {
                    logicalRawVoltage[logicalIndex].append(
                        meas->getValue() * (realVdd / static_cast<VoltmeterSpec*>(moduleSpecification)->Vref));
                    if (logicalRawVoltage[logicalIndex].size() >= samplesToTakeTotal) {
                        ChannelData &reading = logicalData[logicalIndex];
                        reading.voltage = getAverage(&logicalRawVoltage[logicalIndex]);
                        reading.percent = (reading.voltage * 1000 - config->rangeMin) * 100.0 /
                                          (config->rangeMax - config->rangeMin);
                        reading.percent = qBound<qreal>(0, reading.percent, 100);
                        reading.min = qMin(reading.min, reading.voltage);
                        reading.max = qMax(reading.max, reading.voltage);
                        logicalRawVoltage[logicalIndex].clear();
                        voltWindow->showLogicalReading(logicalIndex, reading, slot,
                                                       logicalChannelNames.at(logicalIndex).name,
                                                       !advancedHistoryTime);
                        advancedHistoryTime = true;
                    }
                } else if (meas->getType() == MeasurementType::PKPK) {
                    logicalData[logicalIndex].ripple = meas->getValue();
                } else if (meas->getType() == MeasurementType::FREQUENCY && meas->getLabel() != "N/A") {
                    logicalData[logicalIndex].frequency = meas->getValue();
                }
            }
            if (samplesTaken >= calibrationInterval) {
                samplesTaken = 0;
                updateSamplingChannel();
            }
            if (voltWindow->isLogging()) {
                QStringList names;
                for (const LogicalChannel &logical : logicalChannelNames)
                    names.append("CH" + QString::number(logical.slot + 1) + " " + logical.name);
                voltWindow->appendLogicalDatalog(logicalData, names, logicalChannelNames.size());
            }
        } else {
        voltWindow->showProgress(samplesTaken,samplesToTakeTotal);
        for (Measurement* meas : m) {
            if(meas->getType() == MeasurementType::MEAN && meas->getLabel() != "N/A"){
                dataRawVoltage[meas->getChannelIndex()].append(meas->getValue()*(realVdd/static_cast<VoltmeterSpec*>(moduleSpecification)->Vref));
            }
        }

        if(samplesTaken>=samplesToTakeTotal || isStartup == true){
            updateSamplingChannel();
            samplesTaken = 0;
            isStartup = false;
            for(int i=0;i<MAX_VOLTMETER_CHANNELS;i++){
                data[i].voltage =  getAverage(&dataRawVoltage[i]);
                // Map voltage (in V) to percentage of configured input range.
                // rangeMin / rangeMax are in mV, voltage*1000 converts to mV.
                // Old (incorrect) formula used +rangeMin which produced negative values when rangeMin < 0.
                data[i].percent = (data[i].voltage*1000 - config->rangeMin) * 100.0 / (config->rangeMax - config->rangeMin);
                if(data[i].percent < 0) data[i].percent = 0;
                if(data[i].percent > 100) data[i].percent = 100;
                if(data[i].voltage>data[i].max){
                    data[i].max = data[i].voltage;
                }
                if(data[i].voltage<data[i].min){
                    data[i].min = data[i].voltage;
                }
            }
            for (Measurement* meas : m) {
                if(meas->getType() == MeasurementType::PKPK){
                    data[meas->getChannelIndex()].ripple = meas->getValue();
                }
                if(meas->getType() == MeasurementType::FREQUENCY && meas->getLabel()!="N/A"){
                    data[meas->getChannelIndex()].frequency = meas->getValue();
                }
            }

            for(int i=0;i<MAX_VOLTMETER_CHANNELS;i++){
                dataRawVoltage[i].clear();
            }

            voltWindow->showData(data,numChannelsEnabled);
        }
        }
    }
    if (!isReferenceMeasured && voltWindow->isMultimode())
        applyNextMultimodeCapture();
    else if (!isReferenceMeasured && activeCaptureIsMultimode) {
        setDefaultSampling();
        activeCaptureIsMultimode = false;
        restartSampling();
    }
    else
        restartSampling();
    if (voltWindow->isMultimode()) {
        int maxScanLength = 1;
        for (int slot = 0; slot < MAX_VOLTMETER_CHANNELS; ++slot)
            maxScanLength = qMax(maxScanLength, voltWindow->selectedInputIndices(slot).size());
        voltWindow->showProgress(samplesTaken, samplesToTakeTotal * maxScanLength);
    } else {
        voltWindow->showProgress(samplesTaken,samplesToTakeTotal);
    }
}


qreal Voltmeter::getAverage(QList<qreal> *list)
{
    qreal out = 0;
    if (list->length()>=1){
        for (qreal num : *list) {
            out += num;
        }
        out = out/list->length();
    }
    return out;
}

void Voltmeter::setAveraging(int value)
{
    samplesToTakeTotal = value;
}

void Voltmeter::setNumChannelsEnabled(int value)
{
    numChannelsEnabled = value;
    notifyActivePinFunctionsChanged();
}

void Voltmeter::handleInputSelectionChanged()
{
    if (!inputCapabilitiesReceived)
        return;
    rebuildLogicalChannelMap();

    int activeSlots = 0;
    for (int slot = 0; slot < MAX_VOLTMETER_CHANNELS; ++slot) {
        const QList<int> selected = voltWindow->selectedInputIndices(slot);
        if (!selected.isEmpty())
            activeSlots = slot + 1;
        scanPositions[slot] = 0;
        const int currentSelector = static_cast<VoltmeterSpec*>(moduleSpecification)->currentInputSelectors[slot];
        const int currentPosition = selected.indexOf(currentSelector);
        if (currentPosition >= 0)
            scanPositions[slot] = currentPosition;
        if (!activeCaptureIsMultimode)
            activeCaptureSelectors[slot] = currentSelector;
    }
    if (voltWindow->isMultimode())
        numChannelsEnabled = qMax(1, activeSlots);
    notifyActivePinFunctionsChanged();
}

void Voltmeter::handleAcquisitionModeChanged(int index)
{
    Q_UNUSED(index);
    samplesTaken = 0;
    for (QList<qreal> &samples : dataRawVoltage)
        samples.clear();
    for (QList<qreal> &samples : logicalRawVoltage)
        samples.clear();
}

void Voltmeter::rebuildLogicalChannelMap()
{
    struct ReadingSnapshot {
        qreal voltage;
        qreal ripple;
        qreal frequency;
        qreal min;
        qreal max;
        qreal percent;
    };
    const QVector<LogicalChannel> previousChannels = logicalChannelNames;
    QVector<ReadingSnapshot> previousData;
    QVector<QList<qreal>> previousRawVoltage;
    previousData.reserve(previousChannels.size());
    previousRawVoltage.reserve(previousChannels.size());
    for (int i = 0; i < previousChannels.size(); ++i) {
        const ChannelData &source = logicalData[i];
        previousData.append({source.voltage, source.ripple, source.frequency,
                             source.min, source.max, source.percent});
        previousRawVoltage.append(logicalRawVoltage[i]);
    }

    logicalChannelNames.clear();
    VoltmeterSpec *spec = static_cast<VoltmeterSpec*>(moduleSpecification);
    for (int slot = 0; slot < MAX_VOLTMETER_CHANNELS; ++slot) {
        const QList<int> selected = voltWindow->selectedInputIndices(slot);
        for (int selector : selected) {
            if (selector < 0 || selector >= spec->availableInputs[slot].size() ||
                logicalChannelNames.size() >= MAX_VOLTMETER_LOGICAL_INPUTS)
                continue;
            LogicalChannel logical;
            logical.slot = slot;
            logical.selector = selector;
            logical.name = spec->availableInputs[slot].at(selector);
            logicalChannelNames.append(logical);
        }
    }

    QStringList keys;
    QStringList labels;
    for (int i = 0; i < logicalChannelNames.size(); ++i) {
        const LogicalChannel &logical = logicalChannelNames.at(i);
        const QString key = QString::number(logical.slot) + ":" + QString::number(logical.selector);
        keys.append(key);
        labels.append("CH" + QString::number(logical.slot + 1) + " " + logical.name);
        logicalData[i].voltage = 0;
        logicalData[i].ripple = 0;
        logicalData[i].frequency = 0;
        logicalData[i].min = 10000;
        logicalData[i].max = -10000;
        logicalData[i].percent = 0;
        logicalRawVoltage[i].clear();
        for (int oldIndex = 0; oldIndex < previousChannels.size(); ++oldIndex) {
            if (previousChannels.at(oldIndex).slot == logical.slot &&
                previousChannels.at(oldIndex).selector == logical.selector) {
                const ReadingSnapshot &source = previousData.at(oldIndex);
                logicalData[i].voltage = source.voltage;
                logicalData[i].ripple = source.ripple;
                logicalData[i].frequency = source.frequency;
                logicalData[i].min = source.min;
                logicalData[i].max = source.max;
                logicalData[i].percent = source.percent;
                logicalRawVoltage[i] = previousRawVoltage.at(oldIndex);
                break;
            }
        }
    }
    voltWindow->setLogicalInputs(keys, labels);
}

void Voltmeter::applyNextMultimodeCapture()
{
    if (!inputCapabilitiesReceived || !voltWindow->isMultimode()) {
        restartSampling();
        return;
    }

    configureMultimodeCapture();
    restartSampling();

    const int activeSlots = qBound(1, numChannelsEnabled, MAX_VOLTMETER_CHANNELS);
    for (int slot = 0; slot < activeSlots; ++slot) {
        const QList<int> selected = voltWindow->selectedInputIndices(slot);
        if (!selected.isEmpty())
            scanPositions[slot] = (scanPositions[slot] + 1) % selected.size();
    }
}

void Voltmeter::configureMultimodeCapture()
{
    VoltmeterSpec *spec = static_cast<VoltmeterSpec*>(moduleSpecification);
    const int activeSlots = qBound(1, numChannelsEnabled, MAX_VOLTMETER_CHANNELS);
    setNumberOfChannels(activeSlots);
    comm->write(cmd->SCOPE, cmd->SAMPLING_FREQ, config->samplingRate);
    comm->write(cmd->SCOPE, cmd->DATA_LENGTH, config->captureSampleCount());
    for (int slot = 0; slot < activeSlots; ++slot) {
        const QList<int> selected = voltWindow->selectedInputIndices(slot);
        int selector = spec->currentInputSelectors[slot];
        if (!selected.isEmpty()) {
            scanPositions[slot] %= selected.size();
            selector = selected.at(scanPositions[slot]);
        }
        activeCaptureSelectors[slot] = selector;
        spec->currentInputSelectors[slot] = selector;
        comm->write(cmd->SCOPE, cmd->SCOPE_ADC_CHANNEL, (slot << 8) | selector);
    }
    activeCaptureIsMultimode = true;
}

QString Voltmeter::logicalChannelName(int index) const
{
    return index >= 0 && index < logicalChannelNames.size() ? logicalChannelNames.at(index).name : QString();
}

int Voltmeter::logicalChannelSlot(int index) const
{
    return index >= 0 && index < logicalChannelNames.size() ? logicalChannelNames.at(index).slot : -1;
}

QList<PinFunctionInfo> Voltmeter::activePinFunctions() const
{
    QList<PinFunctionInfo> activeFunctions;
    if (voltWindow && voltWindow->isMultimode()) {
        static const QSet<QString> internalSources = {"Temp", "Vref", "Vbat"};
        for (int slot = 0; slot < MAX_VOLTMETER_CHANNELS; ++slot) {
            for (const QString &name : voltWindow->selectedInputNames(slot)) {
                if (internalSources.contains(name))
                    continue;
                activeFunctions.append({name, "CH" + QString::number(slot + 1), "voltmeter"});
            }
        }
        return activeFunctions;
    }

    const int channelMask = voltWindow ? voltWindow->enabledChannelMask() : 0;
    const QList<PinFunctionInfo> &functions = reportedPinFunctions();
    for(int channelIndex = 0; channelIndex < functions.size() && channelIndex < MAX_VOLTMETER_CHANNELS; ++channelIndex){
        if(channelMask & (1 << channelIndex))
            activeFunctions.append(functions.at(channelIndex));
    }
    return activeFunctions;
}

void Voltmeter::resetMinMax()
{
    for(int i=0;i<MAX_VOLTMETER_CHANNELS;i++){
        data[i].max = -10000;
        data[i].min = 10000;
    }
    for (int i = 0; i < logicalChannelNames.size(); ++i) {
        logicalData[i].max = -10000;
        logicalData[i].min = 10000;
    }
}

void Voltmeter::setMode(int index)
{
    switch (index){
    case 0:
        config->mode = VoltmeterMode::NORMAL;
        break;
    case 1:
        config->mode = VoltmeterMode::FAST;
        break;
    default:
        config->mode = VoltmeterMode::NORMAL;
        break;
    }
}


// ******************* Private functions - not important and no logic inside *******
void Voltmeter::stopSampling(){
    setModuleStatus(ModuleStatus::PAUSE);
    comm->write(cmd->SCOPE+":"+cmd->STOP+";");
}

void Voltmeter::startSampling(){
    setModuleStatus(ModuleStatus::PLAY);
    comm->write(cmd->SCOPE+":"+cmd->START+";");
}

void Voltmeter::restartSampling(){
    comm->write(cmd->SCOPE+":"+cmd->NEXT+";");
}

void Voltmeter::setNumberOfChannels(int num){
    switch (num){
    case 1:
        comm->write(cmd->SCOPE, cmd->CHANNELS, cmd->CHANNELS_1);
        break;
    case 2:
        comm->write(cmd->SCOPE+":"+cmd->CHANNELS+":"+cmd->CHANNELS_2+";");
        break;
    case 3:
        comm->write(cmd->SCOPE+":"+cmd->CHANNELS+":"+cmd->CHANNELS_3+";");
        break;
    case 4:
        comm->write(cmd->SCOPE+":"+cmd->CHANNELS+":"+cmd->CHANNELS_4+";");
        break;
    }
}

void Voltmeter::setVDDSampling(){
    setNumberOfChannels(1);
    comm->write(cmd->SCOPE, cmd->SAMPLING_FREQ, 5000);
    comm->write(moduleCommandPrefix+":"+cmd->SCOPE_ADC_CHANNEL_VREF+";");
    int length = 400 + samplesToTakeTotal*100;
    if(length>2000){
        length = 2000;
    }
    comm->write(cmd->SCOPE,cmd->DATA_LENGTH,length);
    isReferenceMeasured = true;
    activeCaptureIsMultimode = false;
}

void Voltmeter::setDefaultSampling()
{
    comm->write(moduleCommandPrefix+":"+cmd->SCOPE_ADC_CHANNEL_DEAFULT+";");
    setNumberOfChannels(qMax(1, numChannelsEnabled));
    comm->write(cmd->SCOPE, cmd->SAMPLING_FREQ, config->samplingRate);
    comm->write(cmd->SCOPE,cmd->DATA_LENGTH,config->captureSampleCount());

    isReferenceMeasured = false;
    activeCaptureIsMultimode = false;
}
