#include "scope.h"

Scope::Scope(QObject *parent) : AbstractModule(parent)
{
    config = new ScopeConfig(this);
    moduleSpecification = new ScopeSpec(); // not parented (inherits non-QObject?) ensure deletion upstream
    specification = static_cast<ScopeSpec*>(moduleSpecification);
    measCalc = new MeasCalculations(this);
    mathCalc = new MathCalculations(this);
    FFTCalc = new FFTengine(this);
    scpWindow = new ScopeWindow(config);
    scpWindow->setObjectName("scpWindow");

    //module is not fully initialized - control widget and dock wodget cannot be modified
    moduleCommandPrefix = cmd->SCOPE;
    moduleName = "Oscilloscope";
    moduleIconURI = Graphics::getCommonPath()+"icon_scope.png";

    scopeData.clear();
    timeAndMemoryHandle = new TimeBaseAndMemory(config,this);

    //connect signals from GUI into scope
    connect(scpWindow, &ScopeWindow::timeBaseChanged,this,&Scope::updateTimebase);
    connect(scpWindow, &ScopeWindow::pretriggerChanged,this,&Scope::updatePretrigger);
    connect(scpWindow, &ScopeWindow::triggerValueChanged,this,&Scope::updateTriggerLevel);
    connect(scpWindow, &ScopeWindow::channelEnableChanged,this,&Scope::updateChannelsEnable);
    connect(scpWindow, &ScopeWindow::triggerModeChanged,this,&Scope::updateTriggerMode);
    connect(scpWindow, &ScopeWindow::triggerEdgeChanged,this,&Scope::updateTriggerEdge);
    connect(scpWindow, &ScopeWindow::triggerChannelChanged,this,&Scope::updateTriggerChannel);
    connect(scpWindow, &ScopeWindow::memoryPolicyChanged,this,&Scope::updateMemoryPolicy);
    connect(scpWindow, &ScopeWindow::memoryCustomLengthChanged,this,&Scope::updateMemoryCustomLength);
    connect(scpWindow, &ScopeWindow::samlingFrequecyCustomInputChanged,this,&Scope::updateCustomSamplingFreq);

    connect(scpWindow, &ScopeWindow::resolutionChanged,this,&Scope::updateResolution);
    connect(scpWindow, &ScopeWindow::historyCaptureSelected, this, &Scope::selectHistoryCapture);
    connect(scpWindow, &ScopeWindow::liveRequested, this, &Scope::resumeLive);
    connect(scpWindow, &ScopeWindow::averageWindowChanged, this, &Scope::setAverageWindow);

    connect(scpWindow, &ScopeWindow::measurementChanged,this, &Scope::addMeasurement);
    connect(scpWindow, &ScopeWindow::measurementClearChanged, this, &Scope::clearMeasurement);
    connect(measCalc, &MeasCalculations::measCalculated, this, &Scope::updateMeasurement);

    connect(scpWindow, &ScopeWindow::mathExpressionChanged,this,&Scope::updateMathExpression);
    connect(scpWindow, &ScopeWindow::fftChanged,this,&Scope::updateFFTConfig);
    connect(mathCalc, &MathCalculations::mathCalculated, this, &Scope::updateMath);
    connect(FFTCalc, &FFTengine::fftCalculated, this, &Scope::updateFFT);

    connect(timeAndMemoryHandle,&TimeBaseAndMemory::updateSamplingFrequency,this, &Scope::setSamplingFrequency);
    connect(timeAndMemoryHandle,&TimeBaseAndMemory::updateMemorySamplesLength,this, &Scope::setDataLength);
    connect(timeAndMemoryHandle,&TimeBaseAndMemory::updateNumChannels,this, &Scope::setNumberOfChannels);
    connect(timeAndMemoryHandle,&TimeBaseAndMemory::updateDataResolution,this, &Scope::setResolution);
}

void Scope::parseData(QByteArray data){
    // Contract: Accepts raw module data frames. Never throws exceptions (Qt style).
    // Malformed frames are logged and ignored. Side-effects: may update GUI and internal buffers.
    QByteArray dataHeader = data.left(4);

    if(dataHeader==Commands::CONFIG){
        data.remove(0,4);
        moduleSpecification->parseSpecification(data);
        showModuleControl();
        buildModuleDescription(specification);

    }else if(dataHeader==Commands::SAMPLING){
        if(samplingActive && !browsingHistory){
            scpWindow->samplingOngoing();
        }
    }else if(dataHeader==Commands::TRIGGERED){
        if (samplingActive && !browsingHistory)
            scpWindow->triggerCaptured();

    }else if(dataHeader==Commands::SCOPE_INCOME){
        // A frame already in transit after Stop must not replace a historical selection.
        if (!samplingActive || browsingHistory)
            return;
        QString error;
        auto capture = ScopeCapture::decode(data, config->pretriggerPercent, error);
        if (!capture) {
            qWarning() << "[Scope][parseData]" << error;
            return;
        }
        config->realSamplingRate = capture->samplingRate;
        config->ADCresolution = capture->resolution;
        config->numberOfChannelsReceived = capture->channels.size();
        config->rangeMin = capture->rangeMin;
        config->rangeMax = capture->rangeMax;
        config->timeMin = capture->channels.first().first().x();
        config->timeMax = capture->channels.first().last().x();
        capture->timeBase = config->timeBase;
        capture->triggerChannel = config->triggerChannelIndex;
        capture->triggerLevel = config->triggerLevel;
        capture->zoom = qreal(config->requestedSamplingRate) / capture->samplingRate *
                        qreal(capture->channels.first().size()) / config->dataLength *
                        timeAndMemoryHandle->getZoom();
        history.append(std::move(*capture));
        displayCapture(history.size() - 1, true);
        if(config->triggerMode!=ScopeTriggerMode::TRIG_SINGLE){
            restartSampling();
        }else{
            samplingActive = false;
            scpWindow->singleSamplingDone();
            setModuleStatus(ModuleStatus::PAUSE);
        }
    }else{
        qWarning() << "[Scope][parseData] Unknown data header" << dataHeader;
    }
}

void Scope::writeConfiguration(){
    scpWindow->restoreGUIAfterStartup();
    scpWindow->setNumChannels(specification->maxADCChannels);
    scpWindow->setRealSamplingRateAndLlength(config->realSamplingRate,config->dataLength);

    comm->write(moduleCommandPrefix+":"+cmd->SCOPE_ADC_CHANNEL_DEAFULT+";");

    timeAndMemoryHandle->setMaxParams(specification->memorySize,specification->maxADCChannels);

    setResolution(config->ADCresolution);
    setDataLength(config->dataLength);
    setSamplingFrequency(config->requestedSamplingRate);
    updateTriggerLevel(config->triggerLevelPercent);
    updatePretrigger(config->pretriggerPercent);
    updateTriggerEdge(config->triggerEdge);
    setNumberOfChannels(config->numberOfChannels);
    updateTriggerChannel(config->triggerChannelIndex);
    if(config->triggerMode==ScopeTriggerMode::TRIG_STOP)
        config->triggerMode=ScopeTriggerMode::TRIG_SINGLE;
    setTriggerMode(config->triggerMode);
}

void Scope::parseConfiguration(QByteArray config){
    this->config->parse(config);
}
QByteArray Scope::getConfiguration(){
    return config->serialize();
}

Scope::~Scope(){
    stopModule();
}

void Scope::stopModule(){
    stopSampling();
    measCalc->exit();
    mathCalc->exit();
    FFTCalc->exit();
}
void Scope::startModule(){
    if (browsingHistory)
        resumeLive();
    else if(config->triggerMode != ScopeTriggerMode::TRIG_STOP)
        startSampling();
}

QWidget* Scope::getWidget(){
    return scpWindow;
}

void Scope::updateHistoryControls()
{
    scpWindow->setHistoryState(history.size(), historyIndex, browsingHistory,
                              history.averageCount(), history.averageWindow());
}

void Scope::displayCapture(int index, bool averaged)
{
    if (index < 0 || index >= history.size()) {
        qWarning() << "[Scope] Invalid history index" << index;
        return;
    }
    historyIndex = index;
    const auto &capture = history.at(index);
    displayedSamplingRate = capture.samplingRate;
    scopeData = averaged ? history.liveData() : capture.channels;
    scpWindow->setCaptureMetadata(capture);
    updateHistoryControls();
    scpWindow->showDataTraces(scopeData, capture.timeBase, capture.triggerChannel);
    scpWindow->setDataMinMaxTimeAndZoom(capture.channels.first().first().x(),
                                      capture.channels.first().last().x(), capture.zoom);
    measCalc->calculate(scopeData, config->scopeMeasList, displayedSamplingRate);
    mathCalc->calculate(scopeData, displayedSamplingRate, mathExpression);
    if (FFTlength != 0 && FFTChannelIndex >= 0 && FFTChannelIndex < scopeData.size())
        FFTCalc->calculate(scopeData.at(FFTChannelIndex), FFTwindow, FFTtype, FFTlength, true, displayedSamplingRate);
    else if (FFTlength != 0)
        scpWindow->updateFFTchart({}, displayedSamplingRate / 2.0);
    updateHistoryControls();
}

void Scope::selectHistoryCapture(int index)
{
    if (index < 0 || index >= history.size()) {
        qWarning() << "[Scope] Invalid history selection" << index;
        return;
    }
    browsingHistory = true;
    stopSampling();
    scpWindow->singleSamplingDone();
    displayCapture(index, false);
}

void Scope::resumeLive()
{
    browsingHistory = false;
    if (history.size() > 0)
        displayCapture(history.size() - 1, true);
    if (config->triggerMode == ScopeTriggerMode::TRIG_STOP)
        config->triggerMode = ScopeTriggerMode::TRIG_SINGLE;
    scpWindow->setAcquisitionRunning(config->triggerMode);
    setTriggerMode(config->triggerMode);
    startSampling();
    updateHistoryControls();
}

void Scope::setAverageWindow(int window)
{
    history.setAverageWindow(window);
    if (!browsingHistory && history.size() > 0)
        displayCapture(history.size() - 1, true);
    updateHistoryControls();
}

void Scope::resetAverage()
{
    history.resetAverage();
    updateHistoryControls();
}

void Scope::updateTimebase(float div){
    timeAndMemoryHandle->setTimeBase(div);
}

void Scope::updateCustomSamplingFreq(int freq)
{
    timeAndMemoryHandle->overWriteSamplingFreq(freq);
}

void Scope::updatePretrigger(float percentage){
    if (config->pretriggerPercent != percentage)
        resetAverage();
    config->pretriggerPercent=percentage;
    config->pretrigger = 65535*percentage/100;
    comm->write(cmd->SCOPE, cmd->SCOPE_PRETRIGGER, config->pretrigger);
}

void Scope::updateTriggerLevel(float percentage){
    if (config->triggerLevelPercent != percentage)
        resetAverage();
    config->triggerLevelPercent=percentage;
    config->triggerLevel = 65535*percentage/100;
    comm->write(cmd->SCOPE, cmd->SCOPE_TRIG_LEVEL, config->triggerLevel);
}

void Scope::updateTriggerMode(ScopeTriggerMode mode){
    if (config->triggerMode != mode)
        resetAverage();
    config->triggerMode=mode;
    setTriggerMode(mode);

    if(mode==ScopeTriggerMode::TRIG_STOP){
        stopSampling();
    }else{
        browsingHistory = false;
        if (history.size() > 0)
            displayCapture(history.size() - 1, true);
        startSampling();
    }
}

void Scope::updateTriggerEdge(ScopeTriggerEdge edge){
    if (config->triggerEdge != edge)
        resetAverage();
    setTriggerEdge(edge);
    config->triggerEdge=edge;
}

void Scope::updateTriggerChannel(int index){
    if (config->triggerChannelIndex != index)
        resetAverage();
    config->triggerChannelIndex=index;
    setTriggerChannel(index);
}
void Scope::updateMemoryPolicy(int memPolicyIndex){
    timeAndMemoryHandle->setMemoryPolicy((MemoryPolicy)memPolicyIndex);
}

void Scope::updateMemoryCustomLength(int length)
{
    timeAndMemoryHandle->overWriteMemoryLength(length);
}

void Scope::updateResolution(int resolution)
{
    timeAndMemoryHandle->setDataResolution(resolution);
}

void Scope::updateChannelsEnable(int buttonStatus){
    if (history.size() > 0)
        displayCapture(historyIndex, !browsingHistory);
    timeAndMemoryHandle->setNumOfChannels(fmax(log2(buttonStatus),0)+1);
    notifyActivePinFunctionsChanged();
}

void Scope::addMeasurement(Measurement *m){
    config->scopeMeasList.append(m);
    if(config->scopeMeasList.length()>12){
        config->scopeMeasList.removeFirst();
    }
    config->measCount= config->scopeMeasList.length();
    measCalc->calculate(scopeData,config->scopeMeasList,displayedSamplingRate);
}

void Scope::clearMeasurement(){
    config->scopeMeasList.clear();
    config->measCount = 0;
    updateMeasurement(config->scopeMeasList);
}

void Scope::buildModuleDescription(ScopeSpec *spec)
{
    QString name = moduleName;
    QList<QString> labels ,values;

    labels.append("ADC Channels");
    values.append(QString::number(spec->maxADCChannels));

    labels.append("Memory size");
    values.append(LabelFormator::formatOutout(spec->memorySize,"B",1));

    labels.append("Max sampling rate");
    values.append(LabelFormator::formatOutout(spec->maxSamplingRate12B,"sps",2));

    labels.append("Pins");
    QString pins;
    for(int i = 0;i<spec->maxADCChannels;i++){
        pins += spec->channelPins[i] + ", ";
    }
    values.append(pins.left(pins.length()-2));

    showModuleDescription(name, labels, values);

    // Pinout overlay: emit one entry per active ADC channel
    QList<PinFunctionInfo> pinFuncs;
    for(int i = 0; i < spec->maxADCChannels; i++){
        if(!spec->channelPins[i].isEmpty() && spec->channelPins[i] != "-"){
            pinFuncs.append({spec->channelPins[i], "CH" + QString::number(i + 1), "scope"});
        }
    }
    showModulePinFunctions(name, pinFuncs);
}

QList<PinFunctionInfo> Scope::activePinFunctions() const
{
    QList<PinFunctionInfo> activeFunctions;
    const int channelMask = scpWindow ? scpWindow->enabledChannelMask() : 0;
    const QList<PinFunctionInfo> &functions = reportedPinFunctions();
    for(int channelIndex = 0; channelIndex < functions.size() && channelIndex < MAX_SCOPE_CHANNELS; ++channelIndex){
        if(channelMask & (1 << channelIndex))
            activeFunctions.append(functions.at(channelIndex));
    }
    return activeFunctions;
}

void Scope::updateMeasurement(QList<Measurement*> m){
    scpWindow->updateMeasurement(m);
}

void Scope::updateMath(int errorPosition)
{
    scpWindow->updateMath(mathCalc->getCalculatedMathTrace());
    scpWindow->mathError(errorPosition);
}

void Scope::updateMathExpression(QString exp)
{
    mathExpression = exp;
    mathCalc->calculate(scopeData,displayedSamplingRate,mathExpression);
}

void Scope::updateFFTConfig(int length, FFTWindow window, FFTType type, int channelIndex)
{
    FFTlength = length;
    FFTwindow = window;
    FFTtype = type;
    FFTChannelIndex = channelIndex;

    if(FFTlength !=0 && FFTChannelIndex >= 0 && scopeData.size()>FFTChannelIndex){
    FFTCalc->calculate(scopeData.at(FFTChannelIndex),FFTwindow,FFTtype,FFTlength,true,displayedSamplingRate);
    }
}

void Scope::updateFFT()
{
    scpWindow->updateFFTchart(FFTCalc->getProcessedData(), FFTCalc->getMaxFrequency());
}

void Scope::stopSampling(){
    samplingActive = false;
    comm->write(cmd->SCOPE+":"+cmd->STOP+";");
    setModuleStatus(ModuleStatus::PAUSE);
}

void Scope::startSampling(){
    samplingActive = true;
    comm->write(cmd->SCOPE+":"+cmd->START+";");
    if(config->triggerMode == ScopeTriggerMode::TRIG_SINGLE)
        setModuleStatus(ModuleStatus::WAIT_EVENT);
    else
        setModuleStatus(ModuleStatus::PLAY);
}

// ******************* Private functions - not important and no logic inside *******
void Scope::restartSampling(){
    comm->write(cmd->SCOPE+":"+cmd->NEXT+";");
}

void Scope::setNumberOfChannels(int num){
    resetAverage();
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

void Scope::setResolution(int res)
{
    resetAverage();
    if(res==8){
        comm->write(cmd->SCOPE+":"+cmd->SCOPE_DATA_DEPTH+":"+cmd->DATA_DEPTH_8B+";");
    }else{
        comm->write(cmd->SCOPE+":"+cmd->SCOPE_DATA_DEPTH+":"+cmd->DATA_DEPTH_12B+";");
    }
}


void Scope::setTriggerChannel(int chan){
    if(chan==0)
        comm->write(cmd->SCOPE+":"+cmd->SCOPE_TRIG_CHANNEL+":"+cmd->CHANNELS_1+";");
    if(chan==1)
        comm->write(cmd->SCOPE+":"+cmd->SCOPE_TRIG_CHANNEL+":"+cmd->CHANNELS_2+";");
    if(chan==2)
        comm->write(cmd->SCOPE+":"+cmd->SCOPE_TRIG_CHANNEL+":"+cmd->CHANNELS_3+";");
    if(chan==3)
        comm->write(cmd->SCOPE+":"+cmd->SCOPE_TRIG_CHANNEL+":"+cmd->CHANNELS_4+";");
}

void Scope::setTriggerMode(ScopeTriggerMode mode){
    if (mode == ScopeTriggerMode::TRIG_AUTO && (float)(config->dataLength)/config->requestedSamplingRate>0.5 ){
        mode = ScopeTriggerMode::TRIG_AUTO_FAST;
    }

    switch(mode){
    case ScopeTriggerMode::TRIG_SINGLE:
        comm->write(cmd->SCOPE+":"+cmd->SCOPE_TRIG_MODE+":"+cmd->MODE_SINGLE+";");
        break;
    case ScopeTriggerMode::TRIG_AUTO:
        comm->write(cmd->SCOPE+":"+cmd->SCOPE_TRIG_MODE+":"+cmd->MODE_AUTO+";");
        break;
    case ScopeTriggerMode::TRIG_NORMAL:
        comm->write(cmd->SCOPE+":"+cmd->SCOPE_TRIG_MODE+":"+cmd->MODE_NORMAL+";");
        break;
    case ScopeTriggerMode::TRIG_STOP:
        comm->write(cmd->SCOPE+":"+cmd->SCOPE_TRIG_MODE+":"+cmd->MODE_SINGLE+";");
        break;
    case ScopeTriggerMode::TRIG_AUTO_FAST:
        comm->write(cmd->SCOPE+":"+cmd->SCOPE_TRIG_MODE+":"+cmd->MODE_AUTO_FAST+";");
        break;
    }
}

void Scope::setTriggerEdge(ScopeTriggerEdge edge){
    switch (edge) {
    case ScopeTriggerEdge::EDGE_RISING:
        comm->write(cmd->SCOPE+":"+cmd->SCOPE_TRIG_EDGE+":"+cmd->EDGE_RISING+";");
        break;
    case ScopeTriggerEdge::EDGE_FALLING:
        comm->write(cmd->SCOPE+":"+cmd->SCOPE_TRIG_EDGE+":"+cmd->EDGE_FALLING+";");
        break;
    }
}

void Scope::setSamplingFrequency(int samplingRate){
    resetAverage();
    comm->write(cmd->SCOPE,cmd->SAMPLING_FREQ,samplingRate);
}

void Scope::setDataLength(int dataLength){
    resetAverage();
    comm->write(cmd->SCOPE,cmd->DATA_LENGTH,dataLength);
}
