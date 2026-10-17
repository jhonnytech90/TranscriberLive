#include "DisplayEditor.h"
#include "Common/Idioma.h"

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
        if (e.importance == 3) head += juce::String ("  !!  ") + tl::tr ("URGENTE");
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
        g.drawText (tl::tr ("Nenhuma mensagem ainda. Os Receivers aparecem na barra lateral quando conectarem."),
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

    drawRow (0, kText, tl::tr ("Todos"), juce::String (participants.size()) + tl::tr (" canais"), selectedId.isEmpty(), true);

    int y = 44;
    for (auto& p : participants)
    {
        juce::String sub = juce::String (p.messageCount) + " msg";
        if (p.importance == 3) sub += tl::tr (" - urgente");
        else if (p.importance == 2) sub += tl::tr (" - importante");
        if (! p.isOnline (now)) sub += tl::tr (" - offline");
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

    tl::AvisoDeIdioma::get().addChangeListener (this);
    aplicarTextos();

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

DisplayAudioProcessorEditor::~DisplayAudioProcessorEditor()
{
    tl::AvisoDeIdioma::get().removeChangeListener (this);
    setLookAndFeel (nullptr);
}

void DisplayAudioProcessorEditor::aplicarTextos()
{
    flashButton   .setButtonText (tl::tr ("Flash"));
    clearButton   .setButtonText (tl::tr ("Limpar tudo"));
    settingsButton.setButtonText (tl::tr ("Rede..."));
    refresh (false);          // os baloes e a lista tambem tem texto nosso
}

void DisplayAudioProcessorEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    aplicarTextos();
    resized();
    repaint();
}

void DisplayAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBg);
    g.setColour (kPanel);
    g.fillRect (getLocalBounds().removeFromTop (56));
    g.setColour (kSide);
    g.fillRect (getLocalBounds().withTrimmedTop (56).removeFromLeft (220));
    g.setColour (kDim);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText (tl::tr ("PARTICIPANTES"), 16, 62, 200, 14, juce::Justification::left);
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
    else if (hub.isMirroring())  s = tl::tr ("espelhando o Display principal desta maquina");
    else                         s = "UDP " + juce::String (hub.getBusPort()) + tl::tr (" em uso por outro programa - tentando...");

    if (hub.isWebPrimary())
        s += juce::String ("   |   ") + tl::tr ("Celular/tablet: ") + hub.getWebAddressHint() + "   (" + juce::String (hub.getWebClients()) + tl::tr (" conectados)");
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
        juce::Label l1 { {}, tl::tr ("Porta UDP (Receivers):") }, l2 { {}, tl::tr ("Porta web (celular):") };
        juce::Label l3 { {}, tl::tr ("Outro computador (IP):") }, l4, ajuda, situacao;
        juce::ComboBox idioma;
        juce::TextEditor bus, http, parceiro;
        juce::ToggleButton webOn { tl::tr ("Servidor web ligado") };
        juce::TextButton apply { tl::tr ("Aplicar") };
        Panel()
        {
            for (auto* c : std::initializer_list<juce::Component*> {
                     &l1, &l2, &l3, &l4, &ajuda, &situacao, &bus, &http, &parceiro,
                     &idioma, &webOn, &apply })
                addAndMakeVisible (c);

            ajuda.setFont (juce::FontOptions (11.0f));
            ajuda.setColour (juce::Label::textColourId, tl::col::dim);
            ajuda.setJustificationType (juce::Justification::topLeft);
            ajuda.setText (tl::tr ((
                "Mostra aqui tamb\xc3\xa9m os Receivers daquele computador. Serve para o "
                "t\xc3\xa9" "cnico de P.A. e o de monitor conversarem entre dois PCs: cada "
                "um aponta para o outro. Vale tanto neste plugin dentro do host "
                "quanto no app Display. Se a outra m\xc3\xa1quina estiver em outra porta "
                "web, escreva ip:porta. Vazio = s\xc3\xb3 esta m\xc3\xa1quina.")),
                juce::dontSendNotification);

            situacao.setFont (juce::FontOptions (11.0f));
            situacao.setJustificationType (juce::Justification::centredLeft);

            setSize (340, 326);
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
            auto c = r.removeFromTop (26); l3.setBounds (c.removeFromLeft (170)); parceiro.setBounds (c);
            situacao.setBounds (r.removeFromTop (16));
            ajuda.setBounds (r.removeFromTop (72));
            r.removeFromTop (6);
            r.removeFromTop (4);
            auto d = r.removeFromTop (26); l4.setBounds (d.removeFromLeft (170)); idioma.setBounds (d);
            r.removeFromTop (8);
            apply.setBounds (r.removeFromTop (28));
        }
    };

    auto panel = std::make_unique<Panel>();
    panel->bus.setText (juce::String (processor.hub->getBusPort()));
    panel->http.setText (juce::String (processor.hub->getHttpPort()));
    panel->webOn.setVisible (false);

    /*  Idioma tambem aqui: quem usa so o aplicativo do Display, sem o
        Receiver aberto, nao teria por onde trocar.                       */
    panel->l4.setText (tl::tr ("Idioma"), juce::dontSendNotification);
    for (int i = 0; i < 3; ++i)
        panel->idioma.addItem (tl::rotuloIdioma (tl::idiomaDeIndice (i)), i + 1);
    panel->idioma.setSelectedItemIndex (tl::indiceDeIdioma (processor.hub->getIdioma()),
                                        juce::dontSendNotification);
    {
        juce::Component::SafePointer<DisplayAudioProcessorEditor> eu (this);
        auto* cx = panel.get();
        cx->idioma.onChange = [eu, cx]
        {
            if (eu != nullptr)
                eu->processor.hub->setIdioma (tl::idiomaDeIndice (cx->idioma.getSelectedItemIndex()));
        };
    }

    panel->parceiro.setText (processor.hub->getParceiro());
    panel->parceiro.setTextToShowWhenEmpty (tl::tr ("ex.: 192.168.0.20 ou 192.168.0.20:47801"), tl::col::dim);

    {
        const auto host = processor.hub->getParceiro();
        const bool online = processor.hub->isParceiroOnline();
        panel->situacao.setText (
            host.isEmpty() ? juce::String()
                           : (online ? tl::tr (("conectado a ")) + host
                                     : tl::tr (("sem resposta de ")) + host),
            juce::dontSendNotification);
        panel->situacao.setColour (juce::Label::textColourId,
                                   online ? juce::Colour (0xff3ddc84) : juce::Colour (0xffff6b6b));
    }

    auto* raw = panel.get();
    juce::Component::SafePointer<DisplayAudioProcessorEditor> safe (this);
    raw->apply.onClick = [safe, raw]
    {
        if (safe == nullptr) return;
        safe->processor.hub->setPorts (raw->bus.getText().getIntValue(), raw->http.getText().getIntValue());

        /*  O cliente cola o que estiver na mao, entao limpamos o que daria
            para limpar em vez de recusar: tira "http://" e qualquer caminho
            depois do endereco. A PORTA fica, se ele digitou uma -- as duas
            maquinas costumam usar a mesma, mas quem achou a porta ocupada
            subiu para a seguinte, e so ele sabe disso.                      */
        const auto host = tl::limparEnderecoParceiro (raw->parceiro.getText());
        safe->processor.hub->setParceiro (host);
        if (auto* box = raw->findParentComponentOfClass<juce::CallOutBox>()) box->dismiss();
    };

    juce::CallOutBox::launchAsynchronously (std::move (panel), settingsButton.getScreenBounds(), nullptr);
}
