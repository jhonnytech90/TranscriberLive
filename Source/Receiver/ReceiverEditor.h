#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "ReceiverProcessor.h"
#include "Common/DarkLookAndFeel.h"

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

    TranscriberLiveAudioProcessor& processor;
    tl::DarkLookAndFeel lnf;

    juce::Label        titleLabel, statusLabel, speechLabel, lastLineLabel;
    juce::Label        nameLabel, importanceLabel, modelLabel, gateLabel, vadLabel, holdLabel;
    juce::TextEditor   nameEditor;
    juce::TextButton   colourButton { "Cor" }, networkButton { "Rede..." };
    juce::ComboBox     importanceBox, modelBox;
    juce::ToggleButton flashButton { "Flash" }, partialsButton { "Parciais" };
    LevelMeter         meter;
    juce::Slider       gateSlider, vadSlider, holdSlider;

    using APVTS = juce::AudioProcessorValueTreeState;
    std::unique_ptr<APVTS::SliderAttachment> gateAttachment, holdAttachment, vadAttachment;
    std::unique_ptr<APVTS::ButtonAttachment> partialsAttachment;

    juce::StringArray lastModelList;
    juce::uint32 lastModelScan = 0;
    int lastLinesVersion = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TranscriberLiveAudioProcessorEditor)
};
