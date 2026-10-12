#include "ReceiverEditor.h"
#include "Common/Trace.h"
#include "Common/Log.h"

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
    TL_TRACE ("--- editor: construindo ---");
    setLookAndFeel (&lnf);
    /*  Menor do que era: sairam o botao Rede e a lista de modelos deu lugar a
        tres opcoes curtas. Num rack de palco cada pixel da tela touch conta.  */
    setSize (620, 330);

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
    initLabel (modoLabel,       "Modo", 12.0f, kDim);
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

    qrButton.onClick = [this] { showQr(); };
    qrButton.setTooltip (juce::String (juce::CharPointer_UTF8 (
        "Mostra um QR Code para abrir o Display no celular ou tablet")));
    addAndMakeVisible (qrButton);

    logButton.onClick = [this] { showLog(); };
    logButton.setTooltip ("Abre a pasta de logs e copia o diagnostico para colar no suporte");
    addAndMakeVisible (logButton);

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
            refreshModoUi();
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

    // ---- modo --------------------------------------------------------------------
    // Tres comportamentos, nao uma lista de arquivos. Qual .bin atende cada
    // modo e decisao do processador, e muda com o que esta instalado.
    for (int i = 0; i < 3; ++i)
        modoBox.addItem (tl::modelos::rotuloModo (tl::modelos::modoDeIndice (i)), i + 1);

    modoBox.onChange = [this]
    {
        const auto m = tl::modelos::modoDeIndice (modoBox.getSelectedItemIndex());
        processor.setModo (m);
        modoBox.setTooltip (tl::modelos::detalheModo (m));
    };
    addAndMakeVisible (modoBox);
    { TL_TRACE_STEP ("editor: modos"); refreshModoUi(); }

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

    { TL_TRACE_STEP ("editor: identidade"); loadIdentityToUi(); }
    { TL_TRACE_STEP ("editor: painel de licenca"); refreshLicenseUi(); }

    if (processor.getInitError().isNotEmpty())
        statusLabel.setText ("Falha na inicializacao: " + processor.getInitError(), juce::dontSendNotification);

    startTimerHz (15);
    TL_TRACE ("--- editor: pronto ---");
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
    logButton.setBounds (row1.removeFromRight (44).reduced (0, 2));
    row1.removeFromRight (6);
    qrButton.setBounds (row1.removeFromRight (40).reduced (0, 2));
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

    // linha 2: modo
    auto row2 = r.removeFromTop (30);
    modoLabel.setBounds (row2.removeFromLeft (42));
    modoBox.setBounds (row2.reduced (0, 2));

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
             &qrButton, &modoLabel, &modoBox, &meter, &speechLabel,
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

    /*  Barra de status: so o que o tecnico precisa saber.

        O nome do arquivo do modelo e o "VAD ok" sairam daqui. Para quem opera
        uma mesa no meio de um show isso e ruido: nao da para agir sobre nenhum
        dos dois, e ocupavam a linha que precisa ficar livre para o aviso que
        importa. Os dois continuam no Log, que e o que o suporte le.

        O motor so preenche o status quando ha problema; dando tudo certo ele
        vem vazio, e a barra fica limpa.                                       */
    juce::String s = eng.getStatus();
    if (eng.isTranscribing())
        s += s.isEmpty() ? juce::String ("transcrevendo...")
                         : juce::String ("  |  transcrevendo...");

    /*  Quando o Automatico alivia sozinho, o tecnico PRECISA ver -- senao ele
        nota a transcricao mudando de qualidade no meio do show e conclui que
        o plugin esta com defeito. Fica na frente de tudo: naquele momento e
        a informacao mais importante da tela.                                */
    if (const auto troca = processor.getTrocaAutomatica(); troca.isNotEmpty())
        s = utf8 ("Autom\xc3\xa1tico: ") + troca
          + utf8 (" \xe2\x80\x94 esta m\xc3\xa1quina n\xc3\xa3o acompanhava o tempo real.")
          + (s.isEmpty() ? juce::String() : juce::String ("   |   ") + s);

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

}

void TranscriberLiveAudioProcessorEditor::refreshModoUi()
{
    const int idx = (int) processor.getModo();

    if (modoBox.getSelectedItemIndex() != idx)
        modoBox.setSelectedItemIndex (idx, juce::dontSendNotification);

    modoBox.setTooltip (tl::modelos::detalheModo (processor.getModo()));
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

//==============================================================================
/*  Botao "Log".

    O log so vale alguma coisa se o cliente conseguir mandar para nos. Pedir
    "abra a pasta Application Support, entre em logs, ache o arquivo mais novo"
    por telefone, no meio da passagem de som, nao funciona. Entao um clique faz
    as duas coisas: copia o diagnostico para a area de transferencia (basta ele
    colar no WhatsApp) e abre a pasta, para quem preferir mandar o arquivo.  */
void TranscriberLiveAudioProcessorEditor::showLog()
{
    auto& log = tl::Log::get();

    if (! log.isEnabled())
    {
        juce::NativeMessageBox::showMessageBoxAsync (
            juce::MessageBoxIconType::WarningIcon, "Log",
            utf8 ("O log est\xc3\xa1 desligado (existe um arquivo \"sem-log\" na pasta de dados, "
                  "ou a pasta n\xc3\xa3o tem permiss\xc3\xa3o de escrita)."), this);
        return;
    }

    juce::SystemClipboard::copyTextToClipboard (log.diagnostico());
    log.getFolder().revealToUser();

    TL_LOGI ("ui", "diagnostico copiado pelo usuario");

    juce::NativeMessageBox::showMessageBoxAsync (
        juce::MessageBoxIconType::InfoIcon, "Log",
        utf8 ("Diagn\xc3\xb3stico copiado — \xc3\xa9 s\xc3\xb3 colar no WhatsApp ou no e-mail do suporte.\n\n"
              "A pasta com os arquivos completos foi aberta:\n")
            + log.getFolder().getFullPathName(), this);
}

//==============================================================================
juce::String TranscriberLiveAudioProcessorEditor::urlDoDisplay() const
{
    const auto id = processor.getIdentity();

    // Qual maquina serve a pagina:
    //
    //  - Com "Display remoto" preenchido, quem serve e AQUELA maquina. O campo
    //    guarda a porta do barramento (UDP, 47800); a pagina fica na porta web,
    //    que e outra. Como o Display usa a porta web padrao, e ela que entra no
    //    endereco -- e o painel diz isso em texto, para o caso de alguem ter
    //    mudado a porta la.
    //
    //  - Sem o campo, quem serve e esta maquina.
    if (id.remoteHost.isNotEmpty())
        return "http://" + id.remoteHost + ":" + juce::String (tl::kDefaultHttpPort) + "/";

    // 127.0.0.1 nao serve aqui: o celular tentaria falar consigo mesmo. Tem que
    // ser o IP desta maquina na rede local.
    const auto ips = tl::HttpServer::getLocalAddresses();
    if (ips.isEmpty())
        return {};

    juce::String url = "http://" + ips[0] + ":" + juce::String (processor.hub->getHttpPort()) + "/";

    // O codigo de escrita viaja DENTRO do QR. E o que separa quem apontou a
    // camera de quem so descobriu o endereco na rede: o primeiro escreve, o
    // segundo le. Nao atrapalha ninguem, porque ler o QR ja e o caminho
    // normal de abrir a pagina.
    //
    // So entra quando a pagina e desta maquina. Com o Display em outra
    // maquina nao temos como saber o codigo de la -- /token so responde a
    // 127.0.0.1, de proposito -- e o painel explica isso.
    if (const auto k = processor.hub->getTokenEscrita(); k.isNotEmpty())
        url << "?k=" << k;

    return url;
}

void TranscriberLiveAudioProcessorEditor::showQr()
{
    const auto url = urlDoDisplay();

    struct Panel : juce::Component
    {
        juce::Label        titulo, endereco, aviso;
        juce::TextButton   copiar { "Copiar endere" + juce::String (juce::CharPointer_UTF8 ("\xc3\xa7" "o")) };
        juce::ToggleButton escrever { juce::String (juce::CharPointer_UTF8 (
                               "Celulares podem escrever")) };
        juce::TextButton   novoCodigo { juce::String (juce::CharPointer_UTF8 ("Trocar c\xc3\xb3" "digo")) };
        tl::QrCode         qr;
        bool               temQr = false;

        void paint (juce::Graphics& g) override
        {
            if (! temQr) return;
            // Fundo branco sempre, mesmo no tema escuro: leitor de celular
            // espera escuro sobre claro, e QR invertido muitos aparelhos nao
            // leem.
            qr.desenhar (g, areaQr, juce::Colours::white, juce::Colours::black);
        }

        juce::Rectangle<int> areaQr;

        void resized() override
        {
            auto r = getLocalBounds().reduced (10);
            titulo.setBounds (r.removeFromTop (20));
            r.removeFromTop (6);
            areaQr = r.removeFromTop (190);
            r.removeFromTop (8);
            endereco.setBounds (r.removeFromTop (20));
            aviso.setBounds (r.removeFromTop (44));
            r.removeFromTop (4);
            escrever.setBounds (r.removeFromTop (24));
            r.removeFromTop (4);
            auto linha = r.removeFromTop (26);
            copiar.setBounds (linha.removeFromLeft (linha.getWidth() / 2 - 3));
            novoCodigo.setBounds (linha.removeFromRight (linha.getWidth() - 6));
        }
    };

    auto panel = std::make_unique<Panel>();
    auto* p = panel.get();

    for (auto* c : std::initializer_list<juce::Component*> { &p->titulo, &p->endereco, &p->aviso,
                                                            &p->copiar, &p->escrever, &p->novoCodigo })
        p->addAndMakeVisible (c);

    p->titulo.setJustificationType (juce::Justification::centred);
    p->endereco.setJustificationType (juce::Justification::centred);
    p->endereco.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    p->aviso.setJustificationType (juce::Justification::centredTop);
    p->aviso.setFont (juce::FontOptions (11.0f));
    p->aviso.setColour (juce::Label::textColourId, tl::col::dim);
    p->setSize (270, 390);

    // ---- chave geral da escrita --------------------------------------------
    // Fica aqui, junto do QR, porque e a mesma decisao: quem pode escrever no
    // telao. Desligada, nem quem leu o QR escreve.
    p->escrever.setToggleState (processor.hub->isEscritaPermitida(), juce::dontSendNotification);
    p->escrever.setTooltip (juce::String (juce::CharPointer_UTF8 (
        "Desligado, ningu\xc3\xa9m escreve pelo celular, nem quem j\xc3\xa1 leu o QR.")));

    p->novoCodigo.setTooltip (juce::String (juce::CharPointer_UTF8 (
        "Sorteia um c\xc3\xb3" "digo novo: os aparelhos que j\xc3\xa1 entraram param de "
        "escrever at\xc3\xa9 lerem o QR de novo. Serve para cortar o acesso de algu\xc3\xa9m "
        "sem desligar a fun\xc3\xa7\xc3\xa3o para todos.")));

    juce::Component::SafePointer<TranscriberLiveAudioProcessorEditor> safeQr (this);

    p->escrever.onClick = [safeQr, p]
    {
        if (safeQr == nullptr) return;
        safeQr->processor.hub->setEscritaPermitida (p->escrever.getToggleState());
    };

    p->novoCodigo.onClick = [safeQr, p]
    {
        if (safeQr == nullptr) return;
        safeQr->processor.hub->trocarTokenEscrita();

        // Redesenha com o codigo novo: o QR antigo ficou invalido no instante
        // do clique, e deixar ele na tela so faria o tecnico tentar duas vezes.
        if (auto* box = p->findParentComponentOfClass<juce::CallOutBox>()) box->dismiss();
        safeQr->showQr();
    };

    if (url.isEmpty())
    {
        p->titulo.setText ("Sem rede", juce::dontSendNotification);
        p->aviso.setText (juce::String (juce::CharPointer_UTF8 (
            "Esta m\xc3\xa1quina n\xc3\xa3o tem endere\xc3\xa7o de rede local. "
            "Conecte no mesmo Wi-Fi do celular.")), juce::dontSendNotification);
        p->copiar.setEnabled (false);
        p->setSize (260, 150);
    }
    else if (! tl::QrCode::gerar (url, p->qr))
    {
        // Endereco longo demais para as versoes que geramos. Nao acontece com
        // IP de rede local, mas se acontecer o tecnico ainda consegue digitar.
        p->titulo.setText ("Endere" + juce::String (juce::CharPointer_UTF8 ("\xc3\xa7" "o")) + " para o celular",
                           juce::dontSendNotification);
        p->endereco.setText (url, juce::dontSendNotification);
        p->aviso.setText (juce::String (juce::CharPointer_UTF8 (
            "N\xc3\xa3o consegui gerar o QR para este endere\xc3\xa7o. Digite no navegador.")),
            juce::dontSendNotification);
        p->setSize (260, 170);
    }
    else
    {
        p->temQr = true;
        const auto id = processor.getIdentity();
        const bool temCodigo = url.contains ("?k=");

        p->titulo.setText (juce::String (juce::CharPointer_UTF8 (
            "Aponte a c\xc3\xa2mera do celular")), juce::dontSendNotification);

        // O endereco mostrado sai sem o codigo. Ele serve para quem vai
        // digitar a mao, e quem digita a mao entra so para ler -- o codigo
        // tem 8 caracteres e ditar isso no meio de um show nao funciona.
        p->endereco.setText (url.upToFirstOccurrenceOf ("?", false, false), juce::dontSendNotification);

        p->aviso.setText (
            id.remoteHost.isNotEmpty()
                ? juce::String (juce::CharPointer_UTF8 (
                    "Display em outra m\xc3\xa1quina: aqui o QR abre s\xc3\xb3 para leitura. "
                    "Para escrever, use o QR do Receiver daquela m\xc3\xa1quina."))
            : temCodigo
                ? juce::String (juce::CharPointer_UTF8 (
                    "Quem ler o QR pode escrever no tel\xc3\xa3o. Quem s\xc3\xb3 digitar o "
                    "endere\xc3\xa7o v\xc3\xaa a conversa, mas n\xc3\xa3o escreve."))
                : juce::String (juce::CharPointer_UTF8 (
                    "Ainda n\xc3\xa3o peguei o c\xc3\xb3" "digo de escrita do Display. "
                    "Abra de novo em alguns segundos.")),
            juce::dontSendNotification);
    }

    // Copia SEM o codigo: o endereco colado no WhatsApp da produ\xc3\xa7\xc3\xa3o nao
    // deve carregar a permissao de escrever junto.
    p->copiar.onClick = [url] { juce::SystemClipboard::copyTextToClipboard (url.upToFirstOccurrenceOf ("?", false, false)); };

    juce::CallOutBox::launchAsynchronously (std::move (panel), qrButton.getScreenBounds(), nullptr);
}

