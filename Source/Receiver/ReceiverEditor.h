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
/** Área rolável com as frases transcritas em fonte grande. */
class TranscriptView : public juce::Component
{
public:
    void setLines (std::vector<TranscriptionEngine::Line> newLines, int fontSize);
    void paint (juce::Graphics& g) override;
    int  getPreferredHeight (int width) const;

private:
    std::vector<TranscriptionEngine::Line> lines;
    int fontPx = 34;
};

//==============================================================================
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
    void chooseModel (bool vad);
    void refreshTranscript();
    void updateStatus();

    TranscriberLiveAudioProcessor& processor;
    tl::DarkLookAndFeel lnf;

    // topo
    juce::Label        titleLabel, statusLabel, speechLabel;
    juce::ToggleButton listenButton { "TRANSCREVER" };
    LevelMeter         meter;
    juce::TextButton   modelButton  { "Modelo Whisper..." },
                       vadButton    { "Modelo VAD..." },
                       clearButton  { "Limpar" },
                       fontDownButton { "A-" }, fontUpButton { "A+" };

    // identidade do canal
    juce::Label        nameLabel, importanceLabel, targetLabel;
    juce::TextEditor   nameEditor, targetEditor;
    juce::TextButton   colourButton { "Cor" };
    juce::ComboBox     importanceBox;
    juce::ToggleButton flashButton { "Flash no Display" };
    void applyIdentityFromUi();
    void loadIdentityToUi();

    // centro
    juce::Viewport   viewport;
    TranscriptView   transcript;

    // rodapé
    juce::Slider     gateSlider, holdSlider, vadSlider;
    juce::Label      gateLabel, holdLabel, vadLabel;
    juce::ToggleButton partialsButton { "Mostrar parciais" };

    using APVTS = juce::AudioProcessorValueTreeState;
    std::unique_ptr<APVTS::SliderAttachment>   gateAttachment, holdAttachment, vadAttachment;
    std::unique_ptr<APVTS::ButtonAttachment>   partialsAttachment, listenAttachment;

    std::unique_ptr<juce::FileChooser> fileChooser;
    int lastLinesVersion = -1;
    int lastFontSize = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TranscriberLiveAudioProcessorEditor)
};
