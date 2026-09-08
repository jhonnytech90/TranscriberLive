#include "ReceiverEditor.h"

namespace
{
    const juce::Colour kBg    = tl::col::bg;
    const juce::Colour kPanel = tl::col::panel;
    const juce::Colour kText  = tl::col::text;
    const juce::Colour kDim   = tl::col::dim;

    juce::String utf8 (const char* s) { return juce::String (juce::CharPointer_UTF8 (s)); }

    constexpr int kHeaderH = 44;
    constexpr int kLogoH   = 30;
    constexpr int kFooterH = 40;
}

//==============================================================================
void LevelMeter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff0b0e11));
    g.fillRoundedRectangle (r, 3.0f);

    const float minDb = -80.0f, maxDb = 0.0f;
    auto toX = [&] (float db) { return r.getX() + r.getWidth() * juce::jlimit (0.0f, 1.0f, (db - minDb) / (maxDb - minDb)); };

    const float x = toX (levelDb);
    g.setColour (speechActive ? kText : kDim.withAlpha (0.6f));
    g.fillRoundedRectangle (r.getX(), r.getY() + 2.0f, x - r.getX(), r.getHeight() - 4.0f, 2.0f);

    const float gx = toX (gate);
    g.setColour (kText);
    g.fillRect (gx - 1.0f, r.getY(), 2.0f, r.getHeight());
}

//==============================================================================
TranscriberLiveAudioProcessorEditor::TranscriberLiveAudioProcessorEditor (TranscriberLiveAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setLookAndFeel (&lnf);
    setSize (680, 352);

    addAndMakeVisible (logo);

    auto initLabel = [this] (juce::Label& l, const juce::String& text, float size, juce::Colour c, juce::Justification j = juce::Justification::left)
    {
        l.setText (text, juce::dontSendNotification);
        l.setFont (juce::FontOptions (size));
        l.setColour (juce::Label::textColourId, c);
        l.setJustificationType (j);
        addAndMakeVisible (l);
    };

    initLabel (statusLabel,     "", 12.0f, kDim);
    initLabel (speechLabel,     "FALA", 12.0f, kDim, juce::Justification::centred);
    initLabel (lastLineLabel,   "", 15.0f, kDim);
    initLabel (nameLabel,       "Nome", 12.0f, kDim);
    initLabel (importanceLabel, "Import.", 12.0f, kDim);
    initLabel (modelLabel,      "Modelo", 12.0f, kDim);
    initLabel (gateLabel,       "Gate", 12.0f, kDim, juce::Justification::centred);
    initLabel (vadLabel,        "Sensib. VAD", 12.0f, kDim, juce::Justification::centred);
    initLabel (holdLabel,       "Fim de frase", 12.0f, kDim, juce::Justification::centred);

    // ---- identidade ------------------------------------------------------------
    nameEditor.setFont (juce::FontOptions (14.0f));
    nameEditor.setTextToShowWhenEmpty ("ex.: Cantor, Baixo...", kDim);
    nameEditor.onReturnKey = [this] { applyIdentityFromUi(); };
    nameEditor.onFocusLost = [this] { applyIdentityFromUi(); };
    addAndMakeVisible (nameEditor);

    importanceBox.addItemList ({ "Normal", "Importante", "Urgente" }, 1);
    importanceBox.onChange = [this] { applyIdentityFromUi(); };
    addAndMakeVisible (importanceBox);

    flashButton.onClick = [this] { applyIdentityFromUi(); };
    addAndMakeVisible (flashButton);

    colourButton.onClick = [this]
    {
        struct Picker : juce::ColourSelector, juce::ChangeListener
        {
            std::function<void (juce::Colour)> onChange;
            Picker() : juce::ColourSelector (showColourspace | showSliders) { addChangeListener (this); setSize (260, 280); }
            ~Picker() override { removeChangeListener (this); }
            void changeListenerCallback (juce::ChangeBroadcaster*) override { if (onChange) onChange (getCurrentColour()); }
        };
        auto picker = std::make_unique<Picker>();
        picker->setCurrentColour (processor.getIdentity().colour, juce::dontSendNotification);
        juce::Component::SafePointer<TranscriberLiveAudioProcessorEditor> safe (this);
        picker->onChange = [safe] (juce::Colour c)
        {
            if (safe == nullptr) return;
            auto id = safe->processor.getIdentity(); id.colour = c; safe->processor.setIdentity (id); safe->loadIdentityToUi();
        };
        juce::CallOutBox::launchAsynchronously (std::move (picker), colourButton.getScreenBounds(), nullptr);
    };
    addAndMakeVisible (colourButton);

    networkButton.onClick = [this] { showNetwork(); };
    addAndMakeVisible (networkButton);

    licenseButton.onClick = [this]
    {
        licensePanelOpen = ! licensePanelOpen;
        refreshLicenseUi();
    };
    addAndMakeVisible (licenseButton);

    // ---- licença -----------------------------------------------------------------
    licensePanel.onActivate = [this] (const juce::String& text)
    {
        const auto info = processor.activateLicense (text);
        if (info.valid)
        {
            licensePanelOpen = false;
            refreshModelList();
        }
        refreshLicenseUi();
        if (! info.valid)
            licensePanel.setStatus (info.error, true);
    };
    licensePanel.onRemove = [this]
    {
        processor.removeLicense();
        licensePanelOpen = true;
        refreshLicenseUi();
    };
    licensePanel.onClose = [this] { licensePanelOpen = false; refreshLicenseUi(); };
    addChildComponent (licensePanel);

    // ---- modelo ------------------------------------------------------------------
    modelBox.onChange = [this]
    {
        const auto name = modelBox.getText();
        if (name.isNotEmpty() && name != processor.getSelectedModel())
            processor.selectModel (name);
    };
    addAndMakeVisible (modelBox);
    refreshModelList();

    // ---- medidor + knobs ---------------------------------------------------------
    addAndMakeVisible (meter);

    auto initKnob = [this] (juce::Slider& s, const juce::String& suffix, int decimals)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 74, 20);
        s.setTextValueSuffix (suffix);
        s.setNumDecimalPlacesToDisplay (decimals);
        s.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        addAndMakeVisible (s);
    };
    initKnob (gateSlider, " dB", 0);
    initKnob (vadSlider,  "",    2);
    initKnob (holdSlider, " ms", 0);

    addAndMakeVisible (partialsButton);

    auto& apvts = processor.apvts;
    gateAttachment     = std::make_unique<APVTS::SliderAttachment> (apvts, TranscriberLiveAudioProcessor::kParamGate,     gateSlider);
    vadAttachment      = std::make_unique<APVTS::SliderAttachment> (apvts, TranscriberLiveAudioProcessor::kParamVadSens,  vadSlider);
    holdAttachment     = std::make_unique<APVTS::SliderAttachment> (apvts, TranscriberLiveAudioProcessor::kParamHold,     holdSlider);
    partialsAttachment = std::make_unique<APVTS::ButtonAttachment> (apvts, TranscriberLiveAudioProcessor::kParamPartials, partialsButton);

    loadIdentityToUi();
    refreshLicenseUi();
    startTimerHz (15);
}

TranscriberLiveAudioProcessorEditor::~TranscriberLiveAudioProcessorEditor() { setLookAndFeel (nullptr); }

//==============================================================================
void TranscriberLiveAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBg);
    g.setColour (kPanel);
    g.fillRect (getLocalBounds().removeFromTop (kHeaderH));
    g.fillRect (getLocalBounds().removeFromBottom (kFooterH));
}

void TranscriberLiveAudioProcessorEditor::resized()
{
    auto r = getLocalBounds();

    // ---- topo: status à esquerda, logo à direita --------------------------------
    auto top = r.removeFromTop (kHeaderH).reduced (12, 6);
    auto logoArea = top.removeFromRight (tl::LogoBadge::widthForHeight (kLogoH));
    logo.setBounds (logoArea.withSizeKeepingCentre (logoArea.getWidth(), kLogoH));
    top.removeFromRight (12);
    statusLabel.setBounds (top);

    auto bottom = r.removeFromBottom (kFooterH).reduced (12, 6);
    lastLineLabel.setBounds (bottom);

    r.reduce (12, 8);

    // painel de licença ocupa toda a área central quando visível
    licensePanel.setBounds (r);

    // linha 1: identidade
    auto row1 = r.removeFromTop (30);
    networkButton.setBounds (row1.removeFromRight (66).reduced (0, 2));
    row1.removeFromRight (6);
    licenseButton.setBounds (row1.removeFromRight (74).reduced (0, 2));
    row1.removeFromRight (6);
    flashButton.setBounds (row1.removeFromRight (62));
    row1.removeFromRight (10);

    nameLabel.setBounds (row1.removeFromLeft (38));
    nameEditor.setBounds (row1.removeFromLeft (146).reduced (0, 2));
    row1.removeFromLeft (6);
    colourButton.setBounds (row1.removeFromLeft (50).reduced (0, 2));
    row1.removeFromLeft (10);
    importanceLabel.setBounds (row1.removeFromLeft (46));
    importanceBox.setBounds (row1.removeFromLeft (juce::jmax (90, row1.getWidth())).reduced (0, 2));

    r.removeFromTop (6);

    // linha 2: modelo
    auto row2 = r.removeFromTop (30);
    modelLabel.setBounds (row2.removeFromLeft (52));
    modelBox.setBounds (row2.reduced (0, 2));

    r.removeFromTop (10);

    // linha 3: medidor + FALA
    auto row3 = r.removeFromTop (18);
    speechLabel.setBounds (row3.removeFromRight (56));
    row3.removeFromRight (8);
    meter.setBounds (row3);

    r.removeFromTop (8);

    // linha 4: knobs
    auto knobs = r;
    const int kw = 132;
    auto placeKnob = [&] (juce::Slider& s, juce::Label& l)
    {
        auto col = knobs.removeFromLeft (kw);
        l.setBounds (col.removeFromBottom (16));
        s.setBounds (col);
    };
    placeKnob (gateSlider, gateLabel);
    placeKnob (vadSlider,  vadLabel);
    placeKnob (holdSlider, holdLabel);
    knobs.removeFromLeft (14);
    partialsButton.setBounds (knobs.removeFromTop (26));
}

//==============================================================================
void TranscriberLiveAudioProcessorEditor::setControlsVisible (bool v)
{
    for (auto* c : std::initializer_list<juce::Component*> {
             &nameLabel, &nameEditor, &colourButton, &importanceLabel, &importanceBox, &flashButton,
             &networkButton, &modelLabel, &modelBox, &meter, &speechLabel,
             &gateSlider, &vadSlider, &holdSlider, &gateLabel, &vadLabel, &holdLabel,
             &partialsButton, &lastLineLabel })
        c->setVisible (v);
}

void TranscriberLiveAudioProcessorEditor::refreshLicenseUi()
{
    const auto info = processor.getLicenseInfo();
    lastLicensed = info.valid;

    const bool showPanel = licensePanelOpen || ! info.valid;
    licensePanel.setInfo (info, info.valid);      // só pode fechar se estiver licenciado
    licensePanel.setVisible (showPanel);
    setControlsVisible (! showPanel);
    licenseButton.setVisible (info.valid);       // sem licença o painel já está aberto
    resized();
    repaint();
}

//==============================================================================
void TranscriberLiveAudioProcessorEditor::timerCallback()
{
    auto& eng = processor.engine;

    if (processor.isLicensed() != lastLicensed)
        refreshLicenseUi();

    if (! lastLicensed)
    {
        if (statusLabel.getText().isEmpty())
            statusLabel.setText (utf8 ("Sem licen\xc3\xa7" "a — o \xc3\xa1udio passa intacto, mas nada \xc3\xa9 transcrito."), juce::dontSendNotification);
        return;
    }

    meter.setLevel (processor.getInputLevelDb(),
                    processor.apvts.getRawParameterValue (TranscriberLiveAudioProcessor::kParamGate)->load(),
                    eng.isSpeechActive());

    const bool speech = eng.isSpeechActive();
    speechLabel.setColour (juce::Label::backgroundColourId, speech ? kText : juce::Colours::transparentBlack);
    speechLabel.setColour (juce::Label::textColourId, speech ? kBg : kDim);

    // status: modelo + VAD
    juce::String s = eng.getStatus();
    if (eng.isModelLoaded())
        s += processor.hasVadModel() ? "  |  VAD ok" : utf8 ("  |  VAD n\xc3\xa3o encontrado (ggml-silero*.bin)");
    if (eng.isTranscribing()) s += "  |  transcrevendo...";
    if (statusLabel.getText() != s) statusLabel.setText (s, juce::dontSendNotification);

    // última frase reconhecida (só conferência; o acompanhamento é no Display / celular)
    if (eng.getLinesVersion() != lastLinesVersion)
    {
        lastLinesVersion = eng.getLinesVersion();
        auto lines = eng.getLines();
        juce::String t = lines.empty() ? juce::String() : lines.back().text;
        if (! lines.empty() && ! lines.back().isFinal) t += " ...";
        lastLineLabel.setText (t, juce::dontSendNotification);
    }

    // lista de modelos (re-escaneia a pasta a cada 5 s — barato)
    const auto now = juce::Time::getMillisecondCounter();
    if (now - lastModelScan > 5000) refreshModelList();
}

void TranscriberLiveAudioProcessorEditor::refreshModelList()
{
    lastModelScan = juce::Time::getMillisecondCounter();
    const auto models = processor.getAvailableModels();
    const auto selected = processor.getSelectedModel();

    if (models != lastModelList || modelBox.getText() != selected)
    {
        lastModelList = models;
        modelBox.clear (juce::dontSendNotification);
        if (models.isEmpty())
        {
            modelBox.setTextWhenNothingSelected (utf8 ("nenhum ggml-*.bin em ") + tl::Hub::getModelsDir().getFullPathName());
        }
        else
        {
            modelBox.addItemList (models, 1);
            const int idx = models.indexOf (selected);
            modelBox.setSelectedItemIndex (idx >= 0 ? idx : 0, juce::dontSendNotification);
        }
    }
}

void TranscriberLiveAudioProcessorEditor::loadIdentityToUi()
{
    const auto id = processor.getIdentity();
    nameEditor.setText (id.name, juce::dontSendNotification);
    importanceBox.setSelectedId (id.importance, juce::dontSendNotification);
    flashButton.setToggleState (id.flash, juce::dontSendNotification);
    colourButton.setColour (juce::TextButton::buttonColourId, id.colour);
    colourButton.setColour (juce::TextButton::textColourOffId, tl::textOn (id.colour));
}

void TranscriberLiveAudioProcessorEditor::applyIdentityFromUi()
{
    auto id = processor.getIdentity();
    id.name = nameEditor.getText().trim();
    if (id.name.isEmpty()) id.name = "Canal";
    id.importance = juce::jmax (1, importanceBox.getSelectedId());
    id.flash = flashButton.getToggleState();
    processor.setIdentity (id);
    loadIdentityToUi();
}

void TranscriberLiveAudioProcessorEditor::showNetwork()
{
    struct Panel : juce::Component
    {
        juce::Label info, l1;
        juce::TextEditor remote;
        juce::TextButton apply { "Aplicar" };
        Panel()
        {
            for (auto* c : std::initializer_list<juce::Component*> { &info, &l1, &remote, &apply }) addAndMakeVisible (c);
            info.setFont (juce::FontOptions (12.0f));
            info.setColour (juce::Label::textColourId, tl::col::dim);
            info.setJustificationType (juce::Justification::topLeft);
            l1.setText ("Display remoto (IP:porta):", juce::dontSendNotification);
            setSize (340, 150);
        }
        void resized() override
        {
            auto r = getLocalBounds().reduced (10);
            info.setBounds (r.removeFromTop (52));
            r.removeFromTop (4);
            auto a = r.removeFromTop (26); l1.setBounds (a.removeFromLeft (170)); remote.setBounds (a);
            r.removeFromTop (10);
            apply.setBounds (r.removeFromTop (28));
        }
    };

    auto panel = std::make_unique<Panel>();
    const auto id = processor.getIdentity();
    auto& hub = *processor.hub;
    panel->info.setText (juce::String ("Nesta m") + utf8 ("\xc3\xa1") + "quina: UDP " + juce::String (hub.getBusPort())
                         + (hub.isBusPrimary() ? " (ativo aqui)" : " (em outro programa)")
                         + "\nCelular/tablet: " + hub.getWebAddressHint()
                         + "\nDeixe vazio se o Display roda neste computador.", juce::dontSendNotification);
    panel->remote.setText (id.remoteHost.isEmpty() ? juce::String() : id.remoteHost + ":" + juce::String (id.remotePort));
    panel->remote.setTextToShowWhenEmpty ("ex.: 192.168.0.20:47800", tl::col::dim);

    auto* raw = panel.get();
    juce::Component::SafePointer<TranscriberLiveAudioProcessorEditor> safe (this);
    raw->apply.onClick = [safe, raw]
    {
        if (safe == nullptr) return;
        auto id2 = safe->processor.getIdentity();
        const auto t = raw->remote.getText().trim();
        id2.remoteHost = t.upToFirstOccurrenceOf (":", false, false).trim();
        const int port = t.fromFirstOccurrenceOf (":", false, false).getIntValue();
        id2.remotePort = port > 0 ? port : tl::kDefaultBusPort;
        safe->processor.setIdentity (id2);
        if (auto* box = raw->findParentComponentOfClass<juce::CallOutBox>()) box->dismiss();
    };

    juce::CallOutBox::launchAsynchronously (std::move (panel), networkButton.getScreenBounds(), nullptr);
}
