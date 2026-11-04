#include "arbgenpanelsettings.h"

ArbGenPanelSettings::ArbGenPanelSettings(QVBoxLayout *destination, bool isPWMbased, QWidget *parent) :
    QObject(parent),
    isPWMbased(isPWMbased)
{
    // Ensure all channel state flags are initialized deterministically
    for (int i = 0; i < MAX_ARB_CHANNELS_NUM; ++i) {
        channelEnabled[i] = false;
        channelSyncWithCH1[i] = false;
        channelSyncPWMWithCH1[i] = false; // Important for CH1 where no sync switch exists
    }

    WidgetSeparator *separatorCommon = new WidgetSeparator(parent, "Common");
    destination->addWidget(separatorCommon);

    buttonsGenerate = new WidgetButtons(parent,1,ButtonTypes::NORMAL,"");
    buttonsGenerate->setObjectName("arbgenbtnen");
    buttonsGenerate->setText("Start");

    buttonsEnable = new WidgetButtons(parent,4,ButtonTypes::RADIO,"Number of channels");
    buttonsEnable->setObjectName("arbGenBtnEnableChannel");
    buttonsEnable->setText("   One   ",0);
    buttonsEnable->setColor(Graphics::getChannelColor(0),0);
    buttonsEnable->setText("   Two   ",1);
    buttonsEnable->setColor(Graphics::getChannelColor(1),1);
    buttonsEnable->setText("   Three   ",2);
    buttonsEnable->setColor(Graphics::getChannelColor(2),2);
    buttonsEnable->setText("   Four   ",3);
    buttonsEnable->setColor(Graphics::getChannelColor(3),3);

    buttonsMemory = new WidgetButtons(parent,3,ButtonTypes::RADIO,"Memory");
    buttonsMemory->setObjectName("arbgenbtnmem");
    buttonsMemory->setText(" Best fit ",0);
    buttonsMemory->setText(" Long ",1);
    buttonsMemory->setText(" Custom ",2);

    customLengthInput = new WidgetTextInput(parent,"","200",InputTextType::NUMBER);
    customLengthInput->setObjectName("arbgenmemleng");
    customLengthInput->setMaximumSize(120,50);

    buttonSelectFile = new WidgetButtons(parent,1,ButtonTypes::NORMAL,"Arbitrary data");
    buttonSelectFile->setObjectName("buttonselectfileArbGen");
    buttonSelectFile->setText("   Select   ");

    labelArbFileInfo = new WidgetLabel(parent,"No arbitrary data file selected","");
    labelArbFileInfo->setObjectName("labelInfoArbGen");

    QHBoxLayout *commonButtons = new QHBoxLayout();
    destination->addWidget(buttonsGenerate);
    commonButtons->addWidget(buttonsMemory);
    commonButtons->addWidget(customLengthInput);
    destination->addLayout(commonButtons);
    destination->addWidget(buttonsEnable);
    destination->addWidget(buttonSelectFile);
    destination->addWidget(labelArbFileInfo);

    channelTabs = new widgetTab(parent, MAX_ARB_CHANNELS_NUM + 1);
    channelTabs->setObjectName("arbGenChannelTabs");
    destination->addWidget(channelTabs);

    for(int i = 0; i < MAX_ARB_CHANNELS_NUM; i++){
        QString chNStr = QString::number(i + 1);
        channelTabs->setText("CH " + chNStr, i);

        verChannArea[i] = new QScrollArea(parent);
        verChannArea[i]->setWidgetResizable(true);
        verChannArea[i]->setMinimumWidth(150);
        verChannArea[i]->setFrameShape(QFrame::NoFrame);
        channelTabs->getLayout(i)->addWidget(verChannArea[i]);
        QWidget *setScroll = new QWidget(parent);

        verChannArea[i]->setWidget(setScroll);
        QVBoxLayout *verChanBox = new QVBoxLayout();
        setScroll->setLayout(verChanBox);


        separators[i] = new WidgetSeparator(parent, "Channel " + chNStr);
        verChanBox->addWidget(separators[i]);

        buttonsShape[i] = new WidgetButtons(parent,4,ButtonTypes::RADIO,"",0,i);
        buttonsShape[i]->setObjectName("arbGenBtnshape"+chNStr);

        buttonsShape[i]->setColor(Graphics::getChannelColor(i),0);
        buttonsShape[i]->setText("Sine",0);
        buttonsShape[i]->setColor(Graphics::getChannelColor(i),1);
        buttonsShape[i]->setText("Saw",1);
        buttonsShape[i]->setColor(Graphics::getChannelColor(i),2);
        buttonsShape[i]->setText("Rect",2);
        buttonsShape[i]->setColor(Graphics::getChannelColor(i),3);
        buttonsShape[i]->setText("Arb",3);
        verChanBox->addWidget(buttonsShape[i]);
        signalShape[i] = SignalShape::SINE;

        dialFreqCh[i] = new WidgetDialRange(parent,"Frequency",i);
        dialFreqCh[i]->setObjectName("arbGenfreq"+chNStr);
        dialFreqCh[i]->setNumOfDecimals(3);
        dialFreqCh[i]->setRange(0.01,1000000,"Hz",1,0.01,1000,true);
        dialFreqCh[i]->setColor(Graphics::getChannelColor(i));
    dialFreqCh[i]->setAdditionalLabelColor(Graphics::palette().textLabel);
        verChanBox->addWidget(dialFreqCh[i]);

        if(i!=0){
            swSyncWithCH1[i] = new WidgetSwitch(parent,"Off","On","CH1 Freq sync",i);
            swSyncWithCH1[i]->setObjectName("SwFreqSynch"+chNStr);
            swSyncWithCH1[i]->setColor(Graphics::getChannelColor(i));
            verChanBox->addWidget(swSyncWithCH1[i]);
            connect(swSyncWithCH1[i],&WidgetSwitch::clicked,this,&ArbGenPanelSettings::syncWithCH1Callback);
            channelSyncWithCH1[i] = false;
        }

        labelDataLength[i] = new WidgetLabel(parent,"Data length:","NA");
        labelDataLength[i]->setValueAlignment(Qt::AlignLeft);
        verChanBox->addWidget(labelDataLength[i]);

        dialOffsetCh[i] = new WidgetDialRange(parent ,"Offset",i);
        dialOffsetCh[i]->setObjectName("arbGenOffset" + chNStr);
        dialOffsetCh[i]->setRange(-3.3,6.6,"V",0.05,0.01,1.65);
        dialOffsetCh[i]->setColor(Graphics::getChannelColor(i));
        dialOffsetCh[i]->hideUnitSelection();
        verChanBox->addWidget(dialOffsetCh[i]);

        dialAmplitudeCh[i] = new WidgetDialRange(parent ,"Amplitude",i);
        dialAmplitudeCh[i]->setObjectName("arbGenAmpl" + chNStr);
        dialAmplitudeCh[i]->setRange(-3.3,6.6,"V",0.05,0.01,1.65);
        dialAmplitudeCh[i]->setColor(Graphics::getChannelColor(i));
        dialAmplitudeCh[i]->hideUnitSelection();
        verChanBox->addWidget(dialAmplitudeCh[i]);

        dialDutyCh[i] = new WidgetDialRange(parent ,"Duty",i);
        dialDutyCh[i]->setObjectName("arbGenDuty" + chNStr);
        dialDutyCh[i]->setRange(0,100,"%",5,1,50);
        dialDutyCh[i]->setColor(Graphics::getChannelColor(i));
        dialDutyCh[i]->hideUnitSelection();
        verChanBox->addWidget(dialDutyCh[i]);

        dialPhaseCh[i] = new WidgetDialRange(parent ,"Phase",i);
        dialPhaseCh[i]->setObjectName("arbGenPhase" + chNStr);
        dialPhaseCh[i]->setRange(0,360,"°",5,1,0);
        dialPhaseCh[i]->setRealValue(i*90,true);
        dialPhaseCh[i]->setColor(Graphics::getChannelColor(i));
        dialPhaseCh[i]->hideUnitSelection();
        verChanBox->addWidget(dialPhaseCh[i]);

        dialPWMFreqCh[i] = new WidgetDialRange(parent,"PWM Frequency",i);
        dialPWMFreqCh[i]->setObjectName("arbGenpwmfreq"+chNStr);
        dialPWMFreqCh[i]->setRange(1,10000000,"Hz",10,0.01,10000,true);
        dialPWMFreqCh[i]->setColor(Graphics::getChannelColor(i));
    dialPWMFreqCh[i]->setAdditionalLabelColor(Graphics::palette().textLabel);
        verChanBox->addWidget(dialPWMFreqCh[i]);

        if(i!=0){
            swSyncPWMWithCH1[i] = new WidgetSwitch(parent,"Off","On","CH1 PWM sync",i);
            swSyncPWMWithCH1[i]->setObjectName("SwPWMFreqSynch"+chNStr);
            swSyncPWMWithCH1[i]->setColor(Graphics::getChannelColor(i));
            verChanBox->addWidget(swSyncPWMWithCH1[i]);

            channelSyncPWMWithCH1[i] = false;
            if(!isPWMbased){
                swSyncPWMWithCH1[i]->hide();
            }else{
                connect(swSyncPWMWithCH1[i],&WidgetSwitch::clicked,this,&ArbGenPanelSettings::syncPWMWithCH1Callback);
            }
        }

        labelPWMResolution[i] = new WidgetLabel(parent,"PWM Resolution:","NA");
        labelPWMResolution[i]->setValueAlignment(Qt::AlignLeft);
        verChanBox->addWidget(labelPWMResolution[i]);

        if(!isPWMbased){
            dialPWMFreqCh[i]->hide();
            labelPWMResolution[i]->hide();
        }

        QSpacerItem *verticalSpacer = new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Expanding);
        verChanBox->addItem(verticalSpacer);

        connect(buttonsShape[i],&WidgetButtons::clicked,this,&ArbGenPanelSettings::buttonShapeCallback);
        connect(dialFreqCh[i],&WidgetDialRange::valueChanged,this,&ArbGenPanelSettings::signalFrequencyCallback);
        connect(dialOffsetCh[i],&WidgetDialRange::valueChanged,this,&ArbGenPanelSettings::signalChangedCallback);
        connect(dialAmplitudeCh[i],&WidgetDialRange::valueChanged,this,&ArbGenPanelSettings::signalChangedCallback);
        connect(dialDutyCh[i],&WidgetDialRange::valueChanged,this,&ArbGenPanelSettings::signalChangedCallback);
        connect(dialPhaseCh[i],&WidgetDialRange::valueChanged,this,&ArbGenPanelSettings::signalChangedCallback);
        if(isPWMbased){
            connect(dialPWMFreqCh[i],&WidgetDialRange::valueChanged,this,&ArbGenPanelSettings::signalPWMFrequencyCallback);
        }
        channelEnabled[i] = i == 0;
        setChannelShown(i, channelEnabled[i]);
    }
    createSweepControls(parent);
    connect(buttonsEnable,&WidgetButtons::clicked,this,&ArbGenPanelSettings::buttonEnableChannelCallback);
    connect(buttonsMemory,&WidgetButtons::clicked,this,&ArbGenPanelSettings::memoryCallback);
    connect(customLengthInput,&WidgetTextInput::numberChanged,this,&ArbGenPanelSettings::customLenghtCallback);
}

void ArbGenPanelSettings::createSweepControls(QWidget *parent)
{
    channelTabs->setText("Sweep", MAX_ARB_CHANNELS_NUM);
    auto *scroll = new QScrollArea(parent);
    scroll->setObjectName("arbGenSweepScroll");
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    channelTabs->getLayout(MAX_ARB_CHANNELS_NUM)->addWidget(scroll);
    auto *content = new QWidget(scroll);
    auto *layout = new QVBoxLayout(content);
    scroll->setWidget(content);

    for (int i = 0; i < MAX_SWEEP_CHANNELS_NUM; ++i) {
        const QString channelName = QString::number(i + 1);
        // Keep CH1 object names compatible with saved layouts.
        const QString suffix = i == 0 ? QString() : channelName;
        sweepChannelArea[i] = new QWidget(content);
        auto *channelLayout = new QVBoxLayout(sweepChannelArea[i]);
        channelLayout->setContentsMargins(0, 0, 0, 0);
        channelLayout->addWidget(new WidgetSeparator(sweepChannelArea[i], "CH" + channelName + " sweep"));
        layout->addWidget(sweepChannelArea[i]);

        if (i == 1) {
            buttonSweepSyncWithCH1 = new WidgetButtons(sweepChannelArea[i], 2, ButtonTypes::RADIO,
                                                      "Sync with CH1", 0);
            buttonSweepSyncWithCH1->setObjectName("arbGenSweepSyncCH2");
            buttonSweepSyncWithCH1->setText("Off", 0);
            buttonSweepSyncWithCH1->setText("On", 1);
            buttonSweepSyncWithCH1->setToolTip("Follow CH1 frequency and sweep, keeping the configured phase offset.");
            channelLayout->addWidget(buttonSweepSyncWithCH1);
            connect(buttonSweepSyncWithCH1, &WidgetButtons::clicked, this, [this](int index) {
                setChannelFrequencySync(1, index == 1);
            });
        }

        sweepControlsArea[i] = new QWidget(sweepChannelArea[i]);
        auto *controls = new QVBoxLayout(sweepControlsArea[i]);
        controls->setContentsMargins(0, 0, 0, 0);
        channelLayout->addWidget(sweepControlsArea[i]);
        buttonSWSweepEnable[i] = new WidgetButtons(sweepControlsArea[i], 2, ButtonTypes::RADIO,
                                                  "SW sweep", 0, i);
        buttonSWSweepEnable[i]->setObjectName("arbGenSweepbtn" + suffix);
        buttonSWSweepEnable[i]->setText("Off", 0);
        buttonSWSweepEnable[i]->setText("On", 1);
        controls->addWidget(buttonSWSweepEnable[i]);

        sweepParametersArea[i] = new QWidget(sweepControlsArea[i]);
        auto *parameters = new QVBoxLayout(sweepParametersArea[i]);
        parameters->setContentsMargins(0, 0, 0, 0);
        controls->addWidget(sweepParametersArea[i]);
        buttonsSweepScale[i] = new WidgetButtons(sweepParametersArea[i], 2, ButtonTypes::RADIO, "Scale");
        buttonsSweepScale[i]->setObjectName("arbGenSweepScale" + channelName);
        buttonsSweepScale[i]->setText("Linear", 0);
        buttonsSweepScale[i]->setText("Exponential", 1);
        parameters->addWidget(buttonsSweepScale[i]);
        buttonsSweepMode[i] = new WidgetButtons(sweepParametersArea[i], 2, ButtonTypes::RADIO, "Mode");
        buttonsSweepMode[i]->setObjectName("arbGenSweepMode" + channelName);
        buttonsSweepMode[i]->setText("Saw", 0);
        buttonsSweepMode[i]->setText("Up / down", 1);
        parameters->addWidget(buttonsSweepMode[i]);

        dialFreqSweepMin[i] = new WidgetDialRange(sweepParametersArea[i], "Min frequency", i);
        dialFreqSweepMin[i]->setObjectName("arbGenfreqSweepMin" + suffix);
        dialFreqSweepMin[i]->setRange(0.01, 1000000, "Hz", 1, 0.01, 1000, true);
        parameters->addWidget(dialFreqSweepMin[i]);
        dialFreqSweepMax[i] = new WidgetDialRange(sweepParametersArea[i], "Max frequency", i);
        dialFreqSweepMax[i]->setObjectName("arbGenfreqSweepMax" + suffix);
        dialFreqSweepMax[i]->setRange(0.01, 1000000, "Hz", 1, 0.01, 1000, true);
        parameters->addWidget(dialFreqSweepMax[i]);
        dialSweepTime[i] = new WidgetDialRange(sweepParametersArea[i], "Sweep time (per leg)", i);
        dialSweepTime[i]->setObjectName("arbGenfreqSweeptime" + suffix);
        dialSweepTime[i]->setRange(0.1, 60, "s", 1, 0.1, 1, true);
        dialSweepTime[i]->setToolTip("Time from minimum to maximum. Up / down takes twice this time for a complete cycle.");
        parameters->addWidget(dialSweepTime[i]);

        connect(buttonSWSweepEnable[i], &WidgetButtons::clicked, this, &ArbGenPanelSettings::buttonSweepCallback);
        connect(dialFreqSweepMax[i], &WidgetDialRange::valueChanged, this, &ArbGenPanelSettings::sweepMaxCallback);
        connect(dialFreqSweepMin[i], &WidgetDialRange::valueChanged, this, &ArbGenPanelSettings::sweepMinCallback);
        connect(dialSweepTime[i], &WidgetDialRange::valueChanged, this, [this, i]() { sweepSettingsCallback(i); });
        connect(buttonsSweepScale[i], &WidgetButtons::clicked, this, [this, i]() { sweepSettingsCallback(i); });
        connect(buttonsSweepMode[i], &WidgetButtons::clicked, this, [this, i]() { sweepSettingsCallback(i); });
    }
    layout->addStretch();
    updateSweepControls();
}

void ArbGenPanelSettings::setChannelShown(int index, bool isShown)
{       
    channelTabs->setTabVisible(index, isShown);
}

void ArbGenPanelSettings::restoreGUI()
{
    isSilentGUIUpdate = true;
    buttonEnableChannelCallback(qBound(0, buttonsEnable->getSelectedIndex(), maxNumChannels - 1));

    memoryCallback(buttonsMemory->getSelectedIndex());
    customLengthInput->processInput();
    customLenghtCallback(customLengthInput->getValue());
    for (int i = 0; i < MAX_SWEEP_CHANNELS_NUM; ++i)
        isSweepEnabled[i] = buttonSWSweepEnable[i]->getSelectedIndex() == 1;

    for(int i =0;i<MAX_ARB_CHANNELS_NUM;i++){
        buttonShapeCallback(buttonsShape[i]->getSelectedIndex(),i);
        if (i > 0)
            syncWithCH1Callback(swSyncWithCH1[i]->isCheckedRight() ? 1 : 0, i);
        if(i>0 && swSyncPWMWithCH1[i]->isCheckedRight()){
            syncPWMWithCH1Callback(1,i);
        }
        signalPWMFrequencyCallback(dialPWMFreqCh[i]->getRealValue(),i);
    }
    for (int i = 0; i < MAX_SWEEP_CHANNELS_NUM; ++i)
        sweepMaxCallback(dialFreqSweepMax[i]->getRealValue(), i);
    updateSweepControls();
    isSilentGUIUpdate = false;
    signalChangedCallback();
}

void ArbGenPanelSettings::setMaxNumChannels(int numChannels)
{
    maxNumChannels = qBound(1, numChannels, MAX_ARB_CHANNELS_NUM);
    bool tmp;
    for (int i = 0; i< MAX_ARB_CHANNELS_NUM; i++) {
        if(numChannels <= i) tmp = true;
        else tmp = false;
        buttonsEnable->setButtonHidden(tmp,i);
    }
    if (numChannelsEnabled > maxNumChannels)
        buttonEnableChannelCallback(maxNumChannels - 1);
    updateSweepControls();
}

void ArbGenPanelSettings::setDataLengthLabels(int length, int index)
{
    labelDataLength[index]->setValue(QString::number(length) +" samples");
}

void ArbGenPanelSettings::setFreqLabel(qreal freq, int index,int precision)
{
    QString color = abs(freq/dialFreqCh[index]->getRealValue()-1)>0.01?Graphics::palette().warning:Graphics::palette().textLabel;
    dialFreqCh[index]->setAdditionalLabelText(LabelFormator::formatOutout(freq,"Hz",precision));
    dialFreqCh[index]->setAdditionalLabelColor(color);
}

void ArbGenPanelSettings::setPWMFreqLabel(qreal freq, int index,int precision )
{
    QString color = abs(freq/dialPWMFreqCh[index]->getRealValue()-1)>0.01?Graphics::palette().warning:Graphics::palette().textLabel;
    dialPWMFreqCh[index]->setAdditionalLabelText(LabelFormator::formatOutout(freq,"Hz",precision));
    dialPWMFreqCh[index]->setAdditionalLabelColor(color);
}

void ArbGenPanelSettings::setPWMResolutionLabel(QString res, int index)
{
    labelPWMResolution[index]->setValue(res);
}

void ArbGenPanelSettings::disableGUI()
{
    GUIEnabled = false;
    for(int i = 0; i < MAX_ARB_CHANNELS_NUM; i++){
        buttonsShape[i]->setEnabled(false);
        dialOffsetCh[i]->setEnabled(false);
        dialAmplitudeCh[i]->setEnabled(false);
        dialDutyCh[i]->setEnabled(false);
        dialPhaseCh[i]->setEnabled(false);
        dialPWMFreqCh[i]->setEnabled(false);
    }
    buttonsEnable->setEnabled(false);
    buttonsMemory->setEnabled(false);
    customLengthInput->setEnabled(false);
}

void ArbGenPanelSettings::enableGUI()
{
    GUIEnabled = true;
    for(int i = 0; i < MAX_ARB_CHANNELS_NUM; i++){
        buttonsShape[i]->setEnabled(true);
        dialOffsetCh[i]->setEnabled(true);
        dialAmplitudeCh[i]->setEnabled(true);
        dialDutyCh[i]->setEnabled(true);
        dialPhaseCh[i]->setEnabled(true);
        if(!channelSyncPWMWithCH1[i])
            dialPWMFreqCh[i]->setEnabled(true);
    }
    buttonsEnable->setEnabled(true);
    buttonsMemory->setEnabled(true);
    customLengthInput->setEnabled(true);
    updateSweepControls();
}

void ArbGenPanelSettings::copyFreq(int fromCh, int toCh)
{
    qreal value = dialFreqCh[fromCh]->getRealValue();
    dialFreqCh[toCh]->setRealValue(value,true);
}

void ArbGenPanelSettings::copyPWMFreq(int fromCh, int toCh)
{
    qreal value = dialPWMFreqCh[fromCh]->getRealValue();
    dialPWMFreqCh[toCh]->setRealValue(value,true);
}

void ArbGenPanelSettings::buttonEnableChannelCallback(int index)
{
    index = qBound(0, index, maxNumChannels - 1);
    buttonsEnable->restoreGeometry(QByteArray::number(1 << index));
    for(int i = 0;i<index+1;i++){
        setChannelShown(i,true);
        channelEnabled[i] = true;
    }
    for(int i = index+1;i<MAX_ARB_CHANNELS_NUM;i++){
        setChannelShown(i,false);
        channelEnabled[i] = false;
    }
    numChannelsEnabled = index+1;
    updateSweepControls();
    if (!isSilentGUIUpdate)
        prepareSweepFrequencies();
    signalChangedCallback();
}

void ArbGenPanelSettings::buttonShapeCallback(int clicked, int channel)
{
    if(signalShape[channel] == SignalShape::ARB && clicked !=3){
        arbChannelsEnabled--;
    }

    switch (clicked) {
    case 0:
        signalShape[channel] = SignalShape::SINE;
        dialDutyCh[channel]->hide();
        break;
    case 1:
        signalShape[channel] = SignalShape::SAW;
        dialDutyCh[channel]->show();
        break;
    case 2:
        signalShape[channel] = SignalShape::RECT;
        dialDutyCh[channel]->show();
        break;
    case 3:
        signalShape[channel] = SignalShape::ARB;
        arbChannelsEnabled++;
        dialDutyCh[channel]->hide();

        break;
    }

    if(arbChannelsEnabled>0){
        buttonSelectFile->show();
        labelArbFileInfo->show();
    }else{
        labelArbFileInfo->hide();
        buttonSelectFile->hide();
    }
    signalChangedCallback();
}

void ArbGenPanelSettings::syncWithCH1Callback(int clicked, int channel)
{
    if(clicked==0){
        channelSyncWithCH1[channel] = false;
    }else{
        channelSyncWithCH1[channel] = true;
        copyFreq(0,channel);
    }
    if (channel == 1)
        buttonSweepSyncWithCH1->restoreGeometry(clicked == 0 ? "1" : "2");
    updateSweepControls();
    if (clicked == 0)
        prepareSweepFrequencies(channel);
    signalChangedCallback();

    if(clicked != 0 && !isSilentGUIUpdate) emit syncRequest();
}

void ArbGenPanelSettings::setChannelFrequencySync(int channel, bool enabled)
{
    Q_ASSERT(channel > 0 && channel < MAX_ARB_CHANNELS_NUM);
    swSyncWithCH1[channel]->restoreGeometry(enabled ? "R" : "L");
    syncWithCH1Callback(enabled ? 1 : 0, channel);
}

void ArbGenPanelSettings::syncPWMWithCH1Callback(int clicked, int channel)
{
    if(clicked==0){
        channelSyncPWMWithCH1[channel] = false;
        if(GUIEnabled)
            dialPWMFreqCh[channel]->setEnabled(true);
    }else{
        channelSyncPWMWithCH1[channel] = true;
        copyPWMFreq(0,channel);
        if (GUIEnabled)
            dialPWMFreqCh[channel]->setEnabled(false);
    }
    signalChangedCallback();
}

void ArbGenPanelSettings::signalFrequencyCallback(qreal value, int channel)
{
    if(channel==0){
        for(int i = 1;i<MAX_ARB_CHANNELS_NUM;i++){
            if(channelSyncWithCH1[i])
                copyFreq(0,i);
        }
    }else{
        if(channelSyncWithCH1[channel])
            dialFreqCh[0]->setRealValue(value,false);
    }

    if (channel < MAX_SWEEP_CHANNELS_NUM && !channelSyncWithCH1[channel]) {
        dialFreqSweepMax[channel]->setRealValue(value, true);
        if (value < dialFreqSweepMin[channel]->getRealValue())
            dialFreqSweepMin[channel]->setRealValue(value, true);
    }
    if (channel == 0 || !channelSyncWithCH1[channel])
        signalChangedCallback();
}

void ArbGenPanelSettings::signalPWMFrequencyCallback(qreal value, int channel)
{
    if(channel==0){
        for(int i = 1;i<MAX_ARB_CHANNELS_NUM;i++){
            if(channelSyncPWMWithCH1[i])
                copyPWMFreq(0,i);
        }
    }else{
        if(channelSyncPWMWithCH1[channel])
            dialPWMFreqCh[0]->setRealValue(value,false);
    }

    if (channel == 0 || !channelSyncPWMWithCH1[channel])
        signalChangedCallback();
}

void ArbGenPanelSettings::memoryCallback(int index)
{
    if(index==0){
        memorySet = MemoryLength::BEST_FIT;
        customLengthInput->hide();
    }else if(index==1){
        memorySet = MemoryLength::LONG;
        customLengthInput->hide();
    }else if(index==2){
        memorySet = MemoryLength::CUSTOM;
        customLengthInput->show();
        customLength = round(customLengthInput->getValue());
    }
    signalChangedCallback();
}

void ArbGenPanelSettings::customLenghtCallback(qreal value)
{
    customLength = round(value);
    signalChangedCallback();
}

void ArbGenPanelSettings::buttonSweepCallback(int index, int channel)
{
    isSweepEnabled[channel] = index == 1;
    updateSweepControls();
    prepareSweepFrequencies(channel);
    signalChangedCallback();
}

void ArbGenPanelSettings::sweepMaxCallback(qreal value, int channel)
{
    if(value<dialFreqSweepMin[channel]->getRealValue())
        dialFreqSweepMin[channel]->setRealValue(value,true);
    sweepSettingsCallback(channel);
}

void ArbGenPanelSettings::sweepMinCallback(qreal value, int channel)
{
    if(value>dialFreqSweepMax[channel]->getRealValue())
        dialFreqSweepMax[channel]->setRealValue(value,true);
    sweepSettingsCallback(channel);
}

void ArbGenPanelSettings::sweepSettingsCallback(int channel)
{
    prepareSweepFrequencies(channel);
    signalChangedCallback();
}

bool ArbGenPanelSettings::isSweepActive(int channel) const
{
    Q_ASSERT(channel >= 0 && channel < MAX_ARB_CHANNELS_NUM);
    return channel < MAX_SWEEP_CHANNELS_NUM && channelEnabled[channel] &&
           isSweepEnabled[channel] && !channelSyncWithCH1[channel];
}

void ArbGenPanelSettings::prepareSweepFrequencies(int channel)
{
    for (int i = 0; i < MAX_SWEEP_CHANNELS_NUM; ++i) {
        if ((channel < 0 || channel == i) && isSweepActive(i))
            dialFreqCh[i]->setRealValue(dialFreqSweepMax[i]->getRealValue(), true);
    }
    for (int i = 1; i < MAX_ARB_CHANNELS_NUM; ++i) {
        if (channelSyncWithCH1[i])
            copyFreq(0, i);
    }
}

void ArbGenPanelSettings::updateSweepControls()
{
    for (int i = 0; i < MAX_SWEEP_CHANNELS_NUM; ++i) {
        sweepChannelArea[i]->setVisible(i < maxNumChannels && channelEnabled[i]);
        sweepControlsArea[i]->setEnabled(!channelSyncWithCH1[i]);
        sweepParametersArea[i]->setEnabled(isSweepEnabled[i]);
    }
    for (int i = 0; i < MAX_ARB_CHANNELS_NUM; ++i)
        dialFreqCh[i]->setEnabled(!channelSyncWithCH1[i] && !isSweepActive(i));
}

void ArbGenPanelSettings::signalChangedCallback()
{
    if(!isSilentGUIUpdate)
        emit signalChanged();
}
