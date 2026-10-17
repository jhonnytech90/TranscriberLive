#pragma once

#include "Common/Idioma.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "ReceiverProcessor.h"
#include "Common/DarkLookAndFeel.h"
#include "Common/Branding.h"
#include "Common/LicenseComponent.h"
#include "Common/QrCode.h"
#include "Common/Modelos.h"

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
                                            private juce::Timer,
                                            private juce::ChangeListener
{
public:
    explicit TranscriberLiveAudioProcessorEditor (TranscriberLiveAudioProcessor&);
    ~TranscriberLiveAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void refreshModoUi();
    void applyIdentityFromUi();
    void loadIdentityToUi();
    void showQr();
    juce::String urlDoDisplay() const;
    void showLog();
    void refreshLicenseUi();
    void setControlsVisible (bool);

    /*  Reescreve os textos fixos da janela na lingua atual.

        Chamada no construtor e de novo quando o idioma muda. Sem ela, trocar
        o idioma so valeria na proxima abertura -- e o usuario concluiria que
        o seletor nao funciona.                                            */
    void aplicarTextos();
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    TranscriberLiveAudioProcessor& processor;
    tl::DarkLookAndFeel lnf;
    tl::LogoBadge logo;

    juce::Label        statusLabel, speechLabel, lastLineLabel;
    juce::Label        nameLabel, importanceLabel, modoLabel, gateLabel, vadLabel, holdLabel;
    juce::TextEditor   nameEditor;
    juce::TextButton   colourButton, licenseButton, logButton { "Log" }, qrButton { "QR" };
    juce::ComboBox     importanceBox, modoBox, idiomaBox;
    juce::ToggleButton flashButton { "Flash" }, partialsButton;
    LevelMeter         meter;
    juce::Slider       gateSlider, vadSlider, holdSlider;
    tl::LicensePanel   licensePanel;

    using APVTS = juce::AudioProcessorValueTreeState;
    std::unique_ptr<APVTS::SliderAttachment> gateAttachment, holdAttachment, vadAttachment;
    std::unique_ptr<APVTS::ButtonAttachment> partialsAttachment;

    int  lastLinesVersion = -1;
    /*  Qual dos enderecos desta maquina vai no QR.

        Quase sempre 0 (o melhor da lista). Existe para a maquina com duas
        redes -- cabo e Wi-Fi, ou uma VPN ligada -- em que a escolhida nao e a
        do celular do pessoal. O botao no painel do QR gira entre elas.      */
    int  enderecoEscolhido = 0;
    bool licensePanelOpen = false;
    bool lastLicensed = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TranscriberLiveAudioProcessorEditor)
};
