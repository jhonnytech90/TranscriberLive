#include "ReceiverEditor.h"

namespace
{
    const juce::Colour kBg        = tl::col::bg;
    const juce::Colour kPanel     = tl::col::panel;
    const juce::Colour kText      = tl::col::text;
    const juce::Colour kDim       = tl::col::dim;
    const juce::Colour kAccent    = tl::col::text;
    const juce::Colour kWarn      = tl::col::dim;      // linha do gate (neutra)
    const juce::Colour kPartial   { 0xffb0bac6 };
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

    // linha do gate
    const float gx = toX (gate);
    g.setColour (kWarn);
    g.fillRect (gx - 1.0f, r.getY(), 2.0f, r.getHeight());
}

//==============================================================================
void TranscriptView::setLines (std::vector<TranscriptionEngine::Line> newLines, int fontSize)
{
    lines  = std::move (newLines);
    fontPx = fontSize;
    setSize (getWidth(), getPreferredHeight (getWidth()));
    repaint();
}

int TranscriptView::getPreferredHeight (int width) const
{
    const juce::Font f (juce::FontOptions ((float) fontPx));
    int h = 12;
    const int textW = juce::jmax (50, width - 24);

    for (auto& l : lines)
    {
        juce::AttributedString as (l.text);
        as.setFont (f);
        juce::TextLayout tl;
        tl.createLayout (as, (float) textW);
        h += (int) tl.getHeight() + fontPx / 2 + 8;
    }
    return juce::jmax (h, 40);
}

void TranscriptView::paint (juce::Graphics& g)
{
    const juce::Font f (juce::FontOptions ((float) fontPx));
    const juce::Font small (juce::FontOptions (12.0f));
    const int textW = juce::jmax (50, getWidth() - 24);
    int y = 12;

    for (auto& l : lines)
    {
        juce::AttributedString as (l.text);
        as.setFont (l.isFinal ? f : f.italicised());
        as.setColour (l.isFinal ? kText : kPartial);
        juce::TextLayout tl;
        tl.createLayout (as, (float) textW);

        g.setColour (kDim);
        g.setFont (small);
        g.drawText (l.time.formatted ("%H:%M:%S") + (l.isFinal ? "" : "  ..."),
                    12, y, textW, 12, juce::Justification::left);

        tl.draw (g, juce::Rectangle<float> (12.0f, (float) y + 12.0f, (float) textW, tl.getHeight()));
        y += (int) tl.getHeight() + fontPx / 2 + 8;
    }

    if (lines.empty())
    {
        g.setColour (kDim);
        g.setFont (juce::FontOptions (18.0f));
        g.drawText ("Aguardando fala...", getLocalBounds(), juce::Justification::centred);
    }
}

//==============================================================================
TranscriberLiveAudioProcessorEditor::TranscriberLiveAudioProcessorEditor (TranscriberLiveAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setLookAndFeel (&lnf);
    setResizable (true, true);
    setResizeLimits (900, 420, 3000, 2000);
    setSize (960, 600);

    auto initLabel = [this] (juce::Label& l, const juce::String& text, float size, juce::Colour c)
    {
        l.setText (text, juce::dontSendNotification);
        l.setFont (juce::FontOptions (size));
        l.setColour (juce::Label::textColourId, c);
        addAndMakeVisible (l);
    };

    initLabel (titleLabel,  "TRANSCRIBER LIVE", 16.0f, kAccent);
    initLabel (statusLabel, "", 13.0f, kDim);
    initLabel (speechLabel, "FALA", 13.0f, kBg);
    speechLabel.setJustificationType (juce::Justification::centred);

    addAndMakeVisible (listenButton);
    addAndMakeVisible (meter);

    for (auto* b : { &modelButton, &vadButton, &clearButton, &fontDownButton, &fontUpButton })
        addAndMakeVisible (b);

    modelButton.onClick    = [this] { chooseModel (false); };
    vadButton.onClick      = [this] { chooseModel (true); };
    clearButton.onClick    = [this] { processor.engine.clearLines(); processor.sendClearToDisplay(); };
    fontDownButton.onClick = [this] { processor.fontSize = juce::jmax (16, processor.fontSize - 4); };
    fontUpButton.onClick   = [this] { processor.fontSize = juce::jmin (96, processor.fontSize + 4); };

    // ---- identidade do canal --------------------------------------------------
    initLabel (nameLabel,       "Nome",        12.0f, kDim);
    initLabel (importanceLabel, juce::String (juce::CharPointer_UTF8 ("Import\xc3\xa2ncia")), 12.0f, kDim);
    initLabel (targetLabel,     "Display (IP:porta)", 12.0f, kDim);

    for (auto* e : { &nameEditor, &targetEditor })
    {
        e->setFont (juce::FontOptions (14.0f));
            e->onReturnKey = [this] { applyIdentityFromUi(); };
        e->onFocusLost = [this] { applyIdentityFromUi(); };
        addAndMakeVisible (e);
    }
    nameEditor.setTextToShowWhenEmpty ("ex.: Cantor, Baixo, Palco...", kDim);
    targetEditor.setTextToShowWhenEmpty ("127.0.0.1:47800", kDim);

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

    loadIdentityToUi();

    viewport.setViewedComponent (&transcript, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);

    // ---- rodapé ---------------------------------------------------------------
    initLabel (gateLabel,    "Gate",        12.0f, kDim);
    initLabel (holdLabel,    "Fim de frase",12.0f, kDim);
    initLabel (vadLabel,     "Sensib. VAD", 12.0f, kDim);

    for (auto* s : { &gateSlider, &holdSlider, &vadSlider })
    {
        s->setSliderStyle (juce::Slider::LinearHorizontal);
        s->setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 20);
        addAndMakeVisible (s);
    }
    gateSlider.setTextValueSuffix (" dB");
    holdSlider.setTextValueSuffix (" ms");

    addAndMakeVisible (partialsButton);

    auto& apvts = processor.apvts;
    gateAttachment     = std::make_unique<APVTS::SliderAttachment>   (apvts, TranscriberLiveAudioProcessor::kParamGate,     gateSlider);
    holdAttachment     = std::make_unique<APVTS::SliderAttachment>   (apvts, TranscriberLiveAudioProcessor::kParamHold,     holdSlider);
    vadAttachment      = std::make_unique<APVTS::SliderAttachment>   (apvts, TranscriberLiveAudioProcessor::kParamVadSens,  vadSlider);
    partialsAttachment = std::make_unique<APVTS::ButtonAttachment>   (apvts, TranscriberLiveAudioProcessor::kParamPartials, partialsButton);
    listenAttachment   = std::make_unique<APVTS::ButtonAttachment>   (apvts, TranscriberLiveAudioProcessor::kParamListen,   listenButton);

    updateStatus();
    startTimerHz (15);
}

TranscriberLiveAudioProcessorEditor::~TranscriberLiveAudioProcessorEditor() { setLookAndFeel (nullptr); }

//==============================================================================
void TranscriberLiveAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBg);
    g.setColour (kPanel);
    g.fillRect (getLocalBounds().removeFromTop (116));
    g.fillRect (getLocalBounds().removeFromBottom (92));
}

void TranscriberLiveAudioProcessorEditor::resized()
{
    auto r = getLocalBounds();

    // ---- topo ----
    auto top = r.removeFromTop (116).reduced (10, 8);
    auto row1 = top.removeFromTop (28);
    titleLabel.setBounds (row1.removeFromLeft (170));
    listenButton.setBounds (row1.removeFromLeft (140));
    speechLabel.setBounds (row1.removeFromLeft (60).reduced (0, 4));
    row1.removeFromLeft (8);
    fontUpButton.setBounds (row1.removeFromRight (40));
    fontDownButton.setBounds (row1.removeFromRight (40));
    row1.removeFromRight (8);
    clearButton.setBounds (row1.removeFromRight (80));
    row1.removeFromRight (8);
    vadButton.setBounds (row1.removeFromRight (120));
    row1.removeFromRight (6);
    modelButton.setBounds (row1.removeFromRight (150));

    top.removeFromTop (6);
    auto rowId = top.removeFromTop (26);
    nameLabel.setBounds (rowId.removeFromLeft (44));
    nameEditor.setBounds (rowId.removeFromLeft (150).reduced (0, 1));
    rowId.removeFromLeft (6);
    colourButton.setBounds (rowId.removeFromLeft (60));
    rowId.removeFromLeft (10);
    importanceLabel.setBounds (rowId.removeFromLeft (76));
    importanceBox.setBounds (rowId.removeFromLeft (120).reduced (0, 1));
    rowId.removeFromLeft (10);
    flashButton.setBounds (rowId.removeFromLeft (140));
    targetEditor.setBounds (rowId.removeFromRight (150).reduced (0, 1));
    targetLabel.setBounds (rowId.removeFromRight (110));

    top.removeFromTop (6);
    auto row2 = top.removeFromTop (22);
    meter.setBounds (row2.removeFromLeft (300).reduced (0, 4));
    row2.removeFromLeft (12);
    statusLabel.setBounds (row2);

    // ---- rodapé ----
    auto bottom = r.removeFromBottom (92).reduced (10, 8);
    auto brow1 = bottom.removeFromTop (36);
    auto brow2 = bottom.removeFromTop (36);

    const int colW = juce::jmax (150, brow1.getWidth() / 3);

    auto c2 = brow1.removeFromLeft (colW * 2);
    gateLabel.setBounds (c2.removeFromLeft (80));
    gateSlider.setBounds (c2);

    auto c3 = brow1;
    vadLabel.setBounds (c3.removeFromLeft (80));
    vadSlider.setBounds (c3);

    auto d2 = brow2.removeFromLeft (colW * 2);
    holdLabel.setBounds (d2.removeFromLeft (80));
    holdSlider.setBounds (d2);

    auto d1 = brow2;
    d1.removeFromLeft (80);
    partialsButton.setBounds (d1.reduced (0, 4));

    // ---- centro ----
    viewport.setBounds (r.reduced (6, 4));
    transcript.setSize (viewport.getMaximumVisibleWidth(), transcript.getPreferredHeight (viewport.getMaximumVisibleWidth()));
    lastLinesVersion = -1;   // força re-layout
}

//==============================================================================
void TranscriberLiveAudioProcessorEditor::timerCallback()
{
    auto& eng = processor.engine;

    meter.setLevel (processor.getInputLevelDb(),
                    processor.apvts.getRawParameterValue (TranscriberLiveAudioProcessor::kParamGate)->load(),
                    eng.isSpeechActive());

    const bool speech = eng.isSpeechActive();
    speechLabel.setColour (juce::Label::backgroundColourId, speech ? kText : juce::Colours::transparentBlack);
    speechLabel.setColour (juce::Label::textColourId, speech ? kBg : kDim);

    if (eng.getLinesVersion() != lastLinesVersion || processor.fontSize != lastFontSize)
        refreshTranscript();

    updateStatus();
}

void TranscriberLiveAudioProcessorEditor::refreshTranscript()
{
    auto& eng = processor.engine;
    lastLinesVersion = eng.getLinesVersion();
    lastFontSize     = processor.fontSize;

    const int w = viewport.getMaximumVisibleWidth();
    transcript.setSize (w, 10);
    transcript.setLines (eng.getLines(), processor.fontSize);

    // rola para o fim (última frase sempre visível)
    viewport.setViewPosition (0, juce::jmax (0, transcript.getHeight() - viewport.getViewHeight()));
}

void TranscriberLiveAudioProcessorEditor::updateStatus()
{
    auto& eng = processor.engine;
    juce::String s = eng.getStatus();

    if (eng.isModelLoaded())
        s += eng.isVadLoaded() ? juce::String ("   |   VAD: Silero") : juce::String (juce::CharPointer_UTF8 ("   |   VAD: s\xc3\xb3 por n\xc3\xadvel (carregue o Silero p/ melhor rejei\xc3\xa7\xc3\xa3o)"));
    if (eng.isTranscribing())
        s += "   |   transcrevendo...";

    if (statusLabel.getText() != s)
        statusLabel.setText (s, juce::dontSendNotification);
}

void TranscriberLiveAudioProcessorEditor::loadIdentityToUi()
{
    const auto id = processor.getIdentity();
    nameEditor.setText (id.name, juce::dontSendNotification);
    targetEditor.setText (id.busHost + ":" + juce::String (id.busPort), juce::dontSendNotification);
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

    auto target = targetEditor.getText().trim();
    if (target.isNotEmpty())
    {
        id.busHost = target.upToFirstOccurrenceOf (":", false, false).trim();
        const auto port = target.fromFirstOccurrenceOf (":", false, false).getIntValue();
        id.busPort = port > 0 ? port : tl::kDefaultBusPort;
    }
    processor.setIdentity (id);
    loadIdentityToUi();
}

void TranscriberLiveAudioProcessorEditor::chooseModel (bool vad)
{
    auto start = (vad ? processor.getVadFile() : processor.getModelFile()).getParentDirectory();
    if (! start.isDirectory())
        start = juce::File::getSpecialLocation (juce::File::userHomeDirectory);

    fileChooser = std::make_unique<juce::FileChooser> (
        vad ? "Escolha o modelo Silero VAD (ggml-silero-*.bin)" : "Escolha o modelo Whisper (ggml-*.bin)",
        start, "*.bin");

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this, vad] (const juce::FileChooser& fc)
        {
            const auto f = fc.getResult();
            if (f.existsAsFile())
            {
                if (vad) processor.setVadFile (f);
                else     processor.setModelFile (f);
            }
        });
}
