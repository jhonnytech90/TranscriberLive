#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "ReceiverProcessor.h"
#include "Common/DarkLookAndFeel.h"
#include "Common/Branding.h"
#include "Common/LicenseComponent.h"

//==============================================================================
/** Medidor de nível com a linha do gate desenhada por cima. */
class LevelMeter : public juce::Component
{
public:
    void setLevel (float db, float gateDb, bool speech)
    {
        levelDb = db; gate = gateDb; speechActive = speech; repaint();
    }
    void paint (juce::Graphics& g) override;

private:
    float levelDb = -100.0f, gate = -45.0f;
    bool  speechActive = false;
};

//==============================================================================
/** Editor compacto do Receiver. */
class TranscriberLiveAudioProcessorEditor : public juce::AudioProcessorEditor,
                                            private juce::Timer
{
public:
    explicit TranscriberLiveAudioProcessorEditor (TranscriberLiveAudioProcessor&);
    ~TranscriberLiveAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void refreshModelList();
    void applyIdentityFromUi();
    void loadIdentityToUi();
    void showNetwork();
    void showLog();
    void refreshLicenseUi();
    void setControlsVisible (bool);

    TranscriberLiveAudioProcessor& processor;
    tl::DarkLookAndFeel lnf;
    tl::LogoBadge logo;

    juce::Label        statusLabel, speechLabel, lastLineLabel;
    juce::Label        nameLabel, importanceLabel, modelLabel, gateLabel, vadLabel, holdLabel;
    juce::TextEditor   nameEditor;
    juce::TextButton   colourButton { "Cor" }, networkButton { "Rede..." },
                       licenseButton { juce::String (juce::CharPointer_UTF8 ("Licen\xc3\xa7" "a")) },
                       logButton { "Log" };
    juce::ComboBox     importanceBox, modelBox;
    juce::ToggleButton flashButton { "Flash" }, partialsButton { "Parciais" };
    LevelMeter         meter;
    juce::Slider       gateSlider, vadSlider, holdSlider;
    tl::LicensePanel   licensePanel;

    using APVTS = juce::AudioProcessorValueTreeState;
    std::unique_ptr<APVTS::SliderAttachment> gateAttachment, holdAttachment, vadAttachment;
    std::unique_ptr<APVTS::ButtonAttachment> partialsAttachment;

    juce::StringArray lastModelList;
    juce::uint32 lastModelScan = 0;
    int  lastLinesVersion = -1;
    bool licensePanelOpen = false;
    bool lastLicensed = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TranscriberLiveAudioProcessorEditor)
};
