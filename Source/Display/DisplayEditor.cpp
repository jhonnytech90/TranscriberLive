#include "DisplayEditor.h"

namespace
{
    const juce::Colour kBg      = tl::col::bg;
    const juce::Colour kPanel   = tl::col::panel;
    const juce::Colour kSide    = tl::col::side;
    const juce::Colour kText    = tl::col::text;
    const juce::Colour kDim     = tl::col::dim;
    const juce::Colour kAccent  = tl::col::text;
    const juce::Colour kUrgent  { 0xffff5252 };

    juce::String timeString (juce::int64 ms) { return juce::Time (ms).formatted ("%H:%M:%S"); }
}

//==============================================================================
void ChatView::setEntries (std::vector<tl::MessageStore::Entry> e, int px)
{
    entries = std::move (e);
    fontPx = px;
    setSize (getWidth(), getPreferredHeight (getWidth()));
    repaint();
}

void ChatView::layoutEntry (const tl::MessageStore::Entry& e, int textW, juce::TextLayout& tl) const
{
    juce::Font f (juce::FontOptions ((float) (e.importance == 3 ? fontPx + 4 : fontPx)));
    if (! e.isFinal) f = f.italicised();
    if (e.importance >= 2) f = f.boldened();

    const auto onColour = tl::textOn (e.colour);
    juce::AttributedString as (e.text);
    as.setFont (f);
    as.setColour (e.isFinal ? onColour : onColour.withAlpha (0.65f));
    tl.createLayout (as, (float) textW);
}

int ChatView::getPreferredHeight (int width) const
{
    const int bubbleW = juce::jmax (120, width - 32);
    int h = 10;
    for (auto& e : entries)
    {
        juce::TextLayout tl;
        layoutEntry (e, bubbleW - 28, tl);
        h += 20 + (int) tl.getHeight() + 14 + 10;
    }
    return juce::jmax (h, 60);
}

void ChatView::paint (juce::Graphics& g)
{
    const int bubbleW = juce::jmax (120, getWidth() - 32);
    const juce::Font small (juce::FontOptions (13.0f, juce::Font::bold));
    int y = 10;

    for (auto& e : entries)
    {
        juce::TextLayout tl;
        layoutEntry (e, bubbleW - 28, tl);
        const int bh = 20 + (int) tl.getHeight() + 14;
        juce::Rectangle<int> bubble (16, y, bubbleW, bh);

        // balão: a cor da pessoa é o próprio balão (sólido); parcial um pouco mais apagada
        const auto fill = e.isFinal ? e.colour : e.colour.withAlpha (0.55f);
        const auto on   = tl::textOn (e.colour);
        g.setColour (fill);
        g.fillRoundedRectangle (bubble.toFloat(), 12.0f);

        if (e.importance == 3)
        {
            g.setColour (kUrgent);
            g.drawRoundedRectangle (bubble.toFloat().reduced (1.5f), 12.0f, 3.0f);
        }

        // cabeçalho: nome + hora (+ marcador de importância), em cima da cor do balão
        g.setFont (small);
        g.setColour (on.withAlpha (0.85f));
        juce::String head = e.name;
        if (e.importance == 2) head += "  !";
        if (e.importance == 3) head += "  !!  URGENTE";
        g.drawText (head, 30, y + 6, bubbleW - 120, 14, juce::Justification::left);
        g.setColour (on.withAlpha (0.6f));
        g.drawText (timeString (e.timeMs) + (e.isFinal ? "" : "  ..."), bubble.getRight() - 110, y + 6, 100, 14, juce::Justification::right);

        tl.draw (g, juce::Rectangle<float> (30.0f, (float) y + 22.0f, (float) (bubbleW - 28), tl.getHeight()));
        y += bh + 10;
    }

    if (entries.empty())
    {
        g.setColour (kDim);
        g.setFont (juce::FontOptions (18.0f));
        g.drawText ("Nenhuma mensagem ainda. Os Receivers aparecem na barra lateral quando conectarem.",
                    getLocalBounds().reduced (20), juce::Justification::centred);
    }
}

//==============================================================================
void ParticipantList::setParticipants (std::vector<tl::MessageStore::Participant> p, const juce::String& selected)
{
    participants = std::move (p);
    selectedId = selected;
    setSize (getWidth(), getPreferredHeight());
    repaint();
}

void ParticipantList::paint (juce::Graphics& g)
{
    const auto now = juce::Time::currentTimeMillis();
    const int w = getWidth();

    auto drawRow = [&] (int y, juce::Colour dot, const juce::String& name, const juce::String& sub, bool selected, bool online)
    {
        if (selected)
        {
            g.setColour (kPanel.brighter (0.15f));
            g.fillRoundedRectangle (juce::Rectangle<int> (6, y + 3, w - 12, rowH - 6).toFloat(), 6.0f);
        }
        juce::ignoreUnused (dot);
        g.setColour (online ? kText.withAlpha (0.8f) : kDim.withAlpha (0.4f));   // indicador neutro de online
        g.fillEllipse (18.0f, (float) y + rowH / 2.0f - 4.0f, 8.0f, 8.0f);
        g.setColour (online ? kText : kDim);
        g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        g.drawText (name, 40, y + 6, w - 48, 18, juce::Justification::left);
        g.setColour (kDim);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (sub, 40, y + 24, w - 48, 14, juce::Justification::left);
    };

    drawRow (0, kText, "Todos", juce::String (participants.size()) + " canais", selectedId.isEmpty(), true);

    int y = 44;
    for (auto& p : participants)
    {
        juce::String sub = juce::String (p.messageCount) + " msg";
        if (p.importance == 3) sub += " - urgente";
        else if (p.importance == 2) sub += " - importante";
        if (! p.isOnline (now)) sub += " - offline";
        drawRow (y, p.colour, p.name, sub, selectedId == p.id, p.isOnline (now));
        y += rowH;
    }
}

void ParticipantList::mouseDown (const juce::MouseEvent& e)
{
    if (e.y < 44) { if (onSelect) onSelect ({}); return; }
    const int idx = (e.y - 44) / rowH;
    if (idx >= 0 && idx < (int) participants.size() && onSelect)
        onSelect (participants[(size_t) idx].id);
}

//==============================================================================
void FlashOverlay::trigger (juce::Colour c, int importance)
{
    colour = c;
    alpha = importance >= 2 ? 0.85f : 0.6f;
    pulsesLeft = importance == 3 ? 3 : (importance == 2 ? 2 : 1);
    decay = importance == 3 ? 0.08f : 0.05f;
    setVisible (true);
    repaint();
}

void FlashOverlay::tick()
{
    if (alpha <= 0.0f && pulsesLeft <= 0) { if (isVisible()) setVisible (false); return; }

    alpha -= decay;
    if (alpha <= 0.0f)
    {
        --pulsesLeft;
        alpha = pulsesLeft > 0 ? 0.7f : 0.0f;
    }
    repaint();
}

void FlashOverlay::paint (juce::Graphics& g)
{
    if (alpha <= 0.0f) return;
    g.fillAll (colour.withAlpha (juce::jlimit (0.0f, 1.0f, alpha)));
}

//==============================================================================
DisplayAudioProcessorEditor::DisplayAudioProcessorEditor (DisplayAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setLookAndFeel (&lnf);
    setResizable (true, true);
    setResizeLimits (640, 400, 4000, 3000);
    setSize (1000, 620);

    auto initLabel = [this] (juce::Label& l, const juce::String& text, float size, juce::Colour c)
    {
        l.setText (text, juce::dontSendNotification);
        l.setFont (juce::FontOptions (size));
        l.setColour (juce::Label::textColourId, c);
        addAndMakeVisible (l);
    };
    initLabel (statusLabel, "", 13.0f, kDim);
    addAndMakeVisible (lnfLogo);

    flashButton.setToggleState (processor.flashEnabled, juce::dontSendNotification);
    flashButton.onClick = [this] { processor.flashEnabled = flashButton.getToggleState(); };
    addAndMakeVisible (flashButton);

    for (auto* b : { &clearButton, &fontDownButton, &fontUpButton, &settingsButton })
        addAndMakeVisible (b);
    clearButton.onClick    = [this] { processor.hub->clearEverywhere(); };
    fontDownButton.onClick = [this] { processor.fontSize = juce::jmax (14, processor.fontSize - 3); };
    fontUpButton.onClick   = [this] { processor.fontSize = juce::jmin (72, processor.fontSize + 3); };
    settingsButton.onClick = [this] { showSettings(); };

    participants.onSelect = [this] (const juce::String& id) { selectedChannel = id; refresh (true); };
    sideViewport.setViewedComponent (&participants, false);
    sideViewport.setScrollBarsShown (true, false);
    addAndMakeVisible (sideViewport);

    chatViewport.setViewedComponent (&chat, false);
    chatViewport.setScrollBarsShown (true, false);
    addAndMakeVisible (chatViewport);

    addChildComponent (flash);

    startTimerHz (30);
}

DisplayAudioProcessorEditor::~DisplayAudioProcessorEditor() { setLookAndFeel (nullptr); }

void DisplayAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBg);
    g.setColour (kPanel);
    g.fillRect (getLocalBounds().removeFromTop (56));
    g.setColour (kSide);
    g.fillRect (getLocalBounds().withTrimmedTop (56).removeFromLeft (220));
    g.setColour (kDim);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText ("PARTICIPANTES", 16, 62, 200, 14, juce::Justification::left);
}

void DisplayAudioProcessorEditor::resized()
{
    auto r = getLocalBounds();
    auto top = r.removeFromTop (56).reduced (10, 6);

    // logo no canto direito (ocupa a altura das duas linhas do cabeçalho)
    auto logoArea = top.removeFromRight (tl::LogoBadge::widthForHeight (38));
    lnfLogo.setBounds (logoArea.withSizeKeepingCentre (logoArea.getWidth(), 38));
    top.removeFromRight (16);

    auto row1 = top.removeFromTop (26);
    flashButton.setBounds (row1.removeFromLeft (80));
    fontUpButton.setBounds (row1.removeFromRight (40));
    fontDownButton.setBounds (row1.removeFromRight (40));
    row1.removeFromRight (8);
    clearButton.setBounds (row1.removeFromRight (100));
    row1.removeFromRight (8);
    settingsButton.setBounds (row1.removeFromRight (80));
    top.removeFromTop (2);
    statusLabel.setBounds (top.removeFromTop (18));

    auto side = r.removeFromLeft (220);
    side.removeFromTop (26);
    sideViewport.setBounds (side);
    participants.setSize (sideViewport.getMaximumVisibleWidth(), participants.getPreferredHeight());

    chatViewport.setBounds (r.reduced (4, 4));
    flash.setBounds (getLocalBounds());
    lastVersion = -1;
}

//==============================================================================
void DisplayAudioProcessorEditor::timerCallback()
{
    flash.tick();

    auto& store = processor.store();
    const int v = store.getVersion();
    const auto now = juce::Time::getMillisecondCounter();

    if (v != lastVersion || processor.fontSize != lastFontSize)
        refresh (false);
    else if (now - lastParticipantRefresh > 1000)   // atualiza online/offline
    {
        lastParticipantRefresh = now;
        participants.setSize (sideViewport.getMaximumVisibleWidth(), participants.getPreferredHeight());
        participants.setParticipants (store.getParticipants(), selectedChannel);
    }

    const auto f = store.getFlashSince (lastFlashSeq);
    if (f.seq > 0)
    {
        lastFlashSeq = f.seq;
        if (processor.flashEnabled) flash.trigger (f.colour, f.importance);
    }

    // status
    auto& hub = *processor.hub;
    juce::String s;
    if (hub.isBusPrimary())      s = "UDP " + juce::String (hub.getBusPort()) + " ok";
    else if (hub.isMirroring())  s = "espelhando o Display principal desta maquina";
    else                         s = "UDP " + juce::String (hub.getBusPort()) + " em uso por outro programa - tentando...";

    if (hub.isWebPrimary())
        s += "   |   Celular/tablet: " + hub.getWebAddressHint() + "   (" + juce::String (hub.getWebClients()) + " conectados)";
    else if (hub.isMirroring())
        s += "   |   Celular/tablet: " + hub.getWebAddressHint();
    if (statusLabel.getText() != s) statusLabel.setText (s, juce::dontSendNotification);
}

void DisplayAudioProcessorEditor::refresh (bool force)
{
    auto& store = processor.store();
    lastVersion  = store.getVersion();
    lastFontSize = processor.fontSize;
    lastParticipantRefresh = juce::Time::getMillisecondCounter();

    participants.setSize (sideViewport.getMaximumVisibleWidth(), participants.getPreferredHeight());
    participants.setParticipants (store.getParticipants(), selectedChannel);

    // se o usuário rolou para cima, não puxa para o fim (a menos que force)
    const bool atBottom = chatViewport.getViewPositionY() + chatViewport.getViewHeight() >= chat.getHeight() - 40;

    const int w = chatViewport.getMaximumVisibleWidth();
    chat.setSize (w, 10);
    chat.setEntries (store.getEntries (selectedChannel), processor.fontSize);

    if (atBottom || force)
        chatViewport.setViewPosition (0, juce::jmax (0, chat.getHeight() - chatViewport.getViewHeight()));
}

void DisplayAudioProcessorEditor::showSettings()
{
    struct Panel : juce::Component
    {
        juce::Label l1 { {}, "Porta UDP (Receivers):" }, l2 { {}, "Porta web (celular):" };
        juce::TextEditor bus, http;
        juce::ToggleButton webOn { "Servidor web ligado" };
        juce::TextButton apply { "Aplicar" };
        Panel()
        {
            for (auto* c : std::initializer_list<juce::Component*> { &l1, &l2, &bus, &http, &webOn, &apply }) addAndMakeVisible (c);
            setSize (300, 170);
        }
        void resized() override
        {
            auto r = getLocalBounds().reduced (10);
            auto a = r.removeFromTop (26); l1.setBounds (a.removeFromLeft (170)); bus.setBounds (a);
            r.removeFromTop (6);
            auto b = r.removeFromTop (26); l2.setBounds (b.removeFromLeft (170)); http.setBounds (b);
            r.removeFromTop (6);
            webOn.setBounds (r.removeFromTop (26));
            r.removeFromTop (10);
            apply.setBounds (r.removeFromTop (28));
        }
    };

    auto panel = std::make_unique<Panel>();
    panel->bus.setText (juce::String (processor.hub->getBusPort()));
    panel->http.setText (juce::String (processor.hub->getHttpPort()));
    panel->webOn.setVisible (false);

    auto* raw = panel.get();
    juce::Component::SafePointer<DisplayAudioProcessorEditor> safe (this);
    raw->apply.onClick = [safe, raw]
    {
        if (safe == nullptr) return;
        safe->processor.hub->setPorts (raw->bus.getText().getIntValue(), raw->http.getText().getIntValue());
        if (auto* box = raw->findParentComponentOfClass<juce::CallOutBox>()) box->dismiss();
    };

    juce::CallOutBox::launchAsynchronously (std::move (panel), settingsButton.getScreenBounds(), nullptr);
}
