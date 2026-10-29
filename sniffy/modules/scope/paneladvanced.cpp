#include "paneladvanced.h"
#include "scopedefs.h"

PanelAdvanced::PanelAdvanced(QVBoxLayout *destination, QWidget *parent)
{
    destination->addWidget(new WidgetSeparator(parent,"Resolution"));

    resolutionButtons = new WidgetButtons(parent,2,ButtonTypes::RADIO,"",1);
    resolutionButtons->setObjectName("scoperesolution");
    destination->addWidget(resolutionButtons);
    resolutionButtons->setText("8 bit",0);
    resolutionButtons->setText("12 bit",1);

    destination->addWidget(new WidgetSeparator(parent,"Mode"));
    modeButtons = new WidgetButtons(parent, 2, ButtonTypes::RADIO, "", 0);
    modeButtons->setText("Normal", 0);
    modeButtons->setText("XY", 1);
    destination->addWidget(modeButtons);

    channelXButtons = new WidgetButtons(parent, MAX_SCOPE_CHANNELS, ButtonTypes::RADIO, "Ch X", 0);
    for(int i=0; i<MAX_SCOPE_CHANNELS; i++) {
        channelXButtons->setText(QString("CH%1").arg(i+1), i);
        channelXButtons->setColor(Graphics::getChannelColor(i), i);
    }
    destination->addWidget(channelXButtons);

    channelYButtons = new WidgetButtons(parent, MAX_SCOPE_CHANNELS, ButtonTypes::RADIO, "Ch Y", 1);
    for(int i=0; i<MAX_SCOPE_CHANNELS; i++) {
        channelYButtons->setText(QString("CH%1").arg(i+1), i);
        channelYButtons->setColor(Graphics::getChannelColor(i), i);
    }
    destination->addWidget(channelYButtons);

    destination->addWidget(new WidgetSeparator(parent,"Sampling"));

    samplingFrequencyInput = new WidgetTextInput(parent,"Sampling","100k",InputTextType::NUMBER);
    samplingFrequencyInput->setObjectName(QStringLiteral("scopeSamplingFreq"));
    destination->addWidget(samplingFrequencyInput);

    samplingFrequencyReal = new WidgetLabel(parent,"Real Sampling","--");
    destination->addWidget(samplingFrequencyReal);

    dataLengthInput = new WidgetTextInput(parent,"Length","1000",InputTextType::NUMBER);
    dataLengthInput->setObjectName(QStringLiteral("scopeDataLength"));
    destination->addWidget(dataLengthInput);

    destination->addWidget(new WidgetSeparator(parent,"Acquisition history"));
    historyPosition = new WidgetLabel(parent, "Capture", "Live (0/128)");
    destination->addWidget(historyPosition);
    historySlider = new QSlider(Qt::Horizontal, parent);
    historySlider->setObjectName("scopeHistorySlider");
    historySlider->setRange(0, 0);
    historySlider->setEnabled(false);
    historySlider->setToolTip("Oldest to newest raw capture. Selecting a capture pauses acquisition.");
    destination->addWidget(historySlider);
    historyButtons = new WidgetButtons(parent, 3, ButtonTypes::NORMAL);
    historyButtons->setObjectName("scopeHistoryButtons");
    historyButtons->setText("Previous", 0);
    historyButtons->setText("Next", 1);
    historyButtons->setText("Live", 2);
    destination->addWidget(historyButtons);
    historyButtons->setDisabledButton(true, 0);
    historyButtons->setDisabledButton(true, 1);

    destination->addWidget(new WidgetSeparator(parent,"Signal averaging"));
    averagingMode = new WidgetSwitch(parent, "Off", "On", "Averaging");
    // Session-only: restoring widget geometry must not silently enable averaging.
    averagingMode->setObjectName(QString());
    destination->addWidget(averagingMode);
    averageWindowLabel = new WidgetLabel(parent, "Window", "16 captures");
    destination->addWidget(averageWindowLabel);
    averageWindow = new QSlider(Qt::Horizontal, parent);
    averageWindow->setObjectName("scopeAverageWindow");
    averageWindow->setRange(2, 128);
    averageWindow->setValue(16);
    averageWindow->setEnabled(false);
    averageWindow->setToolTip("Average corresponding samples of the latest compatible captures. Use a stable trigger.");
    destination->addWidget(averageWindow);
    averageStatus = new WidgetLabel(parent, "Average", "Off");
    destination->addWidget(averageStatus);

    QSpacerItem *verticalSpacer;
    verticalSpacer = new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Expanding);
    destination->addItem(verticalSpacer);

}
