#include "ReceiverEditor.h"
#include "Common/Trace.h"
#include "Common/Log.h"
#include "Common/Idioma.h"
#include "Common/Adaptadores.h"
#include "Common/EnvioDeLog.h"
#include "Common/Atualizacao.h"

/*  O ReceiverTest compila este mesmo arquivo como aplicativo, e ali o JUCE
    nao define a versao do plugin. Sem isto o teste nem compila.          */
#ifndef JucePlugin_VersionString
 #define JucePlugin_VersionString "0.0"
#endif

namespace
{
    const juce::Colour kBg    = tl::col::bg;
    const juce::Colour kPanel = tl::col::panel;
    const juce::Colour kText  = tl::col::text;
    const juce::Colour kDim   = tl::col::dim;

    /*  Texto de tela: passa pela traducao. A chave e o proprio
        portugues, entao o codigo continua legivel e um texto sem
        traducao aparece em portugues em vez de virar chave crua.  */
    juce::String tr (const char* s) { return tl::tr (s); }

    /*  Como a linha da rede aparece na lista do QR.

        So o numero nao serve: numa mesa moderna o Wi-Fi da casa, o switch do
        palco e a rede de audio sao todos "192.168.alguma coisa". Quando o
        sistema sabe dizer o nome da placa ("Wi-Fi", "Ethernet"), e esse nome
        que vai para a tela. Quando a faixa do IP indica que o celular nao vai
        alcancar aquela rede, o aviso vale mais que o nome, e ele e que fica. */
    juce::String descreverRede (const juce::String& ip)
    {
        if (! tl::HttpServer::ehRedeLocal (ip))
            return tl::HttpServer::tipoDaRede (ip);

        const auto placa = tl::Adaptadores::doEndereco (ip);

        if (placa.nome.isEmpty())
            return tl::HttpServer::tipoDaRede (ip);

        const auto tipo = tl::Adaptadores::rotuloDoTipo (placa.tipo);

        // No macOS a placa ja se chama "Wi-Fi"; repetir ficaria "Wi-Fi (Wi-Fi)".
        if (tipo.isEmpty() || placa.nome.containsIgnoreCase (tipo))
            return placa.nome;

        return placa.nome + " (" + tipo + ")";
    }

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
    initLabel (speechLabel,     tl::tr ("FALA"), 12.0f, kDim, juce::Justification::centred);
    initLabel (lastLineLabel,   "", 15.0f, kDim);
    initLabel (nameLabel,       tl::tr ("Nome"), 12.0f, kDim);
    initLabel (importanceLabel, tl::tr ("Import."), 12.0f, kDim);
    initLabel (modoLabel,       tl::tr ("Modo"), 12.0f, kDim);
    initLabel (gateLabel,       tl::tr ("Gate"), 12.0f, kDim, juce::Justification::centred);
    initLabel (vadLabel,        tl::tr ("Sensib. VAD"), 12.0f, kDim, juce::Justification::centred);
    initLabel (holdLabel,       tl::tr ("Fim de frase"), 12.0f, kDim, juce::Justification::centred);

    // ---- identidade ------------------------------------------------------------
    nameEditor.setFont (juce::FontOptions (14.0f));
    nameEditor.setTextToShowWhenEmpty (tl::tr ("ex.: Cantor, Baixo..."), kDim);
    nameEditor.onReturnKey = [this] { applyIdentityFromUi(); };
    nameEditor.onFocusLost = [this] { applyIdentityFromUi(); };
    addAndMakeVisible (nameEditor);

    importanceBox.addItemList ({ tl::tr ("Normal"), tl::tr ("Importante"), tl::tr ("Urgente") }, 1);
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
    qrButton.setTooltip (tl::tr ((
        "Mostra um QR Code para abrir o Display no celular ou tablet")));
    addAndMakeVisible (qrButton);

    logButton.onClick = [this] { showLog(); };
    logButton.setTooltip (tl::tr ("Abre a pasta de logs e copia o diagnostico para colar no suporte"));
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

    /*  ---- idioma ----------------------------------------------------------
        O padrao segue a maquina, e esta caixa manda por cima. So o automatico
        seria armadilha: tecnico brasileiro com o sistema em ingles abriria o
        plugin em ingles sem ter pedido.

        Vale para o processo todo (fica no Hub), entao o Display da mesma
        maquina muda junto.                                                  */
    idiomaBox.onChange = [this]
    {
        processor.hub->setIdioma (tl::idiomaDeIndice (idiomaBox.getSelectedItemIndex()));
    };
    addAndMakeVisible (idiomaBox);

    tl::AvisoDeIdioma::get().addChangeListener (this);
    aplicarTextos();

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
        statusLabel.setText (tl::tr ("Falha na inicializa\xc3\xa7\xc3\xa3o: ") + processor.getInitError(), juce::dontSendNotification);

    /*  Aviso de versao nova. Os dois botoes nascem escondidos: so aparecem
        se o site responder que existe uma, e nunca atrapalham quem ja esta
        atualizado.                                                        */
    for (auto* b : { &btBaixar, &btIgnorar })
    {
        b->setVisible (false);
        addChildComponent (b);
    }

    btBaixar.onClick = [this]
    {
        TL_LOGI ("update", "usuario foi baixar a " + versaoNova);
        juce::URL (urlDaVersaoNova).launchInDefaultBrowser();
    };

    btIgnorar.onClick = [this]
    {
        processor.hub->ignorarUpdate (versaoNova, tl::Atualizacao::hoje());
        btBaixar.setVisible (false);
        btIgnorar.setVisible (false);
        resized();
    };

    {
        juce::Component::SafePointer<TranscriberLiveAudioProcessorEditor> seguro (this);
        tl::Atualizacao::consultar (JucePlugin_VersionString,
                                    [seguro] (tl::Atualizacao::Resultado r)
                                    {
                                        if (seguro != nullptr)
                                            seguro->mostrarAvisoDeUpdate (r.versao, r.url);
                                    });
    }

    startTimerHz (15);
    TL_TRACE ("--- editor: pronto ---");
}

TranscriberLiveAudioProcessorEditor::~TranscriberLiveAudioProcessorEditor()
{
    tl::AvisoDeIdioma::get().removeChangeListener (this);
    setLookAndFeel (nullptr);
}

/*  Textos fixos da janela, na lingua atual.

    Tudo que e texto escrito por nos passa por aqui -- inclusive o que o
    construtor ja escreveu. Repetir e barato e garante que nada fique para
    tras quando o idioma muda com a janela aberta.                          */
void TranscriberLiveAudioProcessorEditor::aplicarTextos()
{
    speechLabel    .setText (tl::tr ("FALA"),         juce::dontSendNotification);
    nameLabel      .setText (tl::tr ("Nome"),         juce::dontSendNotification);
    importanceLabel.setText (tl::tr ("Import."),      juce::dontSendNotification);
    modoLabel      .setText (tl::tr ("Modo"),         juce::dontSendNotification);
    gateLabel      .setText (tl::tr ("Gate"),         juce::dontSendNotification);
    vadLabel       .setText (tl::tr ("Sensib. VAD"),  juce::dontSendNotification);
    holdLabel      .setText (tl::tr ("Fim de frase"), juce::dontSendNotification);

    nameEditor.setTextToShowWhenEmpty (tl::tr ("ex.: Cantor, Baixo..."), kDim);

    colourButton  .setButtonText (tl::tr ("Cor"));
    licenseButton .setButtonText (tl::tr ("Licen\xc3\xa7" "a"));
    flashButton   .setButtonText (tl::tr ("Flash"));
    partialsButton.setButtonText (tl::tr ("Parciais"));

    qrButton .setTooltip (tl::tr ("Mostra um QR Code para abrir o Display no celular ou tablet"));
    logButton.setTooltip (tl::tr ("Abre a pasta de logs e copia o diagnostico para colar no suporte"));

    // Listas: a escolha do usuario tem que sobreviver a troca de lingua.
    {
        const int sel = importanceBox.getSelectedId();
        importanceBox.clear (juce::dontSendNotification);
        importanceBox.addItemList ({ tl::tr ("Normal"), tl::tr ("Importante"), tl::tr ("Urgente") }, 1);
        if (sel > 0) importanceBox.setSelectedId (sel, juce::dontSendNotification);
    }
    {
        const int sel = modoBox.getSelectedItemIndex();
        modoBox.clear (juce::dontSendNotification);
        for (int i = 0; i < 3; ++i)
            modoBox.addItem (tl::modelos::rotuloModo (tl::modelos::modoDeIndice (i)), i + 1);
        if (sel >= 0) modoBox.setSelectedItemIndex (sel, juce::dontSendNotification);
        modoBox.setTooltip (tl::modelos::detalheModo (processor.getModo()));
    }
    {
        idiomaBox.clear (juce::dontSendNotification);
        for (int i = 0; i < 3; ++i)
            idiomaBox.addItem (tl::rotuloIdioma (tl::idiomaDeIndice (i)), i + 1);
        idiomaBox.setSelectedItemIndex (tl::indiceDeIdioma (processor.hub->getIdioma()),
                                        juce::dontSendNotification);
        idiomaBox.setTooltip (tl::tr ("Idioma da interface \xe2\x80\x94 Interface language"));
    }
}

void TranscriberLiveAudioProcessorEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    aplicarTextos();
    resized();
    repaint();
}

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

    if (btIgnorar.isVisible())
    {
        btIgnorar.setBounds (top.removeFromRight (62).reduced (0, 2));
        top.removeFromRight (6);
        btBaixar .setBounds (top.removeFromRight (62).reduced (0, 2));
        top.removeFromRight (10);
    }

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
    // O seletor de idioma rouba a direita desta linha: a caixa do modo tem
    // tres itens curtos e sobrava largura demais para ela.
    idiomaBox.setBounds (row2.removeFromRight (150).reduced (0, 2));
    row2.removeFromRight (8);
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
             &qrButton, &modoLabel, &modoBox, &idiomaBox, &meter, &speechLabel,
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
/*  Versao nova no site.

    Fica na barra preta de cima, que e para onde o tecnico ja olha quando
    alguma coisa esta diferente. Dois botoes, nada de janela no meio da tela:
    abrir um modal na abertura do host seria a maneira mais rapida de o
    plugin virar "aquele que incomoda".                                    */
void TranscriberLiveAudioProcessorEditor::mostrarAvisoDeUpdate (const juce::String& versao,
                                                                const juce::String& url)
{
    if (versao.isEmpty())
        return;

    auto& hub = processor.hub.get();

    // Adiou hoje: respeita o dia. Amanha o aviso volta.
    if (hub.getUpdateIgnorado() == versao && hub.getUpdateIgnoradoEm() == tl::Atualizacao::hoje())
        return;

    versaoNova      = versao;
    urlDaVersaoNova = url;

    btBaixar .setButtonText (tr ("Baixar"));
    btIgnorar.setButtonText (tr ("Ignorar"));
    btBaixar .setVisible (true);
    btIgnorar.setVisible (true);

    resized();
}

//==============================================================================
void TranscriberLiveAudioProcessorEditor::timerCallback()
{
    auto& eng = processor.engine;

    if (processor.isLicensed() != lastLicensed)
        refreshLicenseUi();

    if (! lastLicensed)
    {
        if (statusLabel.getText().isEmpty() && ! btBaixar.isVisible())
            statusLabel.setText (tr ("Sem licen\xc3\xa7" "a \xe2\x80\x94 o \xc3\xa1udio passa intacto, mas nada \xc3\xa9 transcrito."), juce::dontSendNotification);
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
        s += s.isEmpty() ? tl::tr ("transcrevendo...")
                         : juce::String ("  |  ") + tl::tr ("transcrevendo...");

    /*  Quando o Automatico alivia sozinho, o tecnico PRECISA ver -- senao ele
        nota a transcricao mudando de qualidade no meio do show e conclui que
        o plugin esta com defeito. Fica na frente de tudo: naquele momento e
        a informacao mais importante da tela.                                */
    if (const auto troca = processor.getTrocaAutomatica(); troca.isNotEmpty())
        s = tr ("Autom\xc3\xa1tico: ") + troca
          + tr (" \xe2\x80\x94 esta m\xc3\xa1quina n\xc3\xa3o acompanhava o tempo real.")
          + (s.isEmpty() ? juce::String() : juce::String ("   |   ") + s);

    /*  O aviso de versao nova vem na frente do resto: se a maquina esta
        atrasando, pode ser exatamente a correcao que ja esta publicada.  */
    if (btBaixar.isVisible())
        s = tr ("Vers\xc3\xa3o ") + versaoNova + tr (" dispon\xc3\xadvel.")
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
    if (id.name.isEmpty()) id.name = tl::tr ("Canal");
    id.importance = juce::jmax (1, importanceBox.getSelectedId());
    id.flash = flashButton.getToggleState();
    processor.setIdentity (id);
    loadIdentityToUi();
}

//==============================================================================
/*  Botao "Log".

    Antes isto copiava o diagnostico e abria a pasta. Nao funcionava: a pasta e
    oculta no Windows, o cliente esta no meio da passagem de som, e o arquivo
    quase nunca chegava ate nos. Agora o mesmo clique manda os logs direto para
    o site, identificados pela licenca -- nome e e-mail de quem comprou.

    O cliente ve antes, por escrito, exatamente o que vai subir. Nada sai da
    maquina dele sem ele ler essa lista e clicar em Enviar. Quem preferir o
    caminho antigo ainda tem o botao de abrir a pasta.                       */
void TranscriberLiveAudioProcessorEditor::showLog()
{
    auto& log = tl::Log::get();

    if (! log.isEnabled())
    {
        juce::NativeMessageBox::showMessageBoxAsync (
            juce::MessageBoxIconType::WarningIcon, "Log",
            tr ("O log est\xc3\xa1 desligado (existe um arquivo \"sem-log\" na pasta de dados, "
                  "ou a pasta n\xc3\xa3o tem permiss\xc3\xa3o de escrita)."), this);
        return;
    }

    const auto pacote = tl::EnvioDeLog::montar (processor.getLicenseInfo(),
                                                juce::PluginHostType().getHostDescription());

    if (pacote.nomesDeArquivo.isEmpty())
    {
        juce::NativeMessageBox::showMessageBoxAsync (
            juce::MessageBoxIconType::InfoIcon, "Log",
            tr ("Ainda n\xc3\xa3o h\xc3\xa1 nada gravado para enviar."), this);
        return;
    }

    juce::String resumo;
    resumo << tr ("Isto \xc3\xa9 o que vai para o suporte:") << "\n\n"
           << tr ("Nome: ")  << (pacote.nome.isNotEmpty()  ? pacote.nome  : tr ("(sem licen\xc3\xa7" "a instalada)")) << "\n"
           << tr ("E-mail: ")<< (pacote.email.isNotEmpty() ? pacote.email : tr ("(sem licen\xc3\xa7" "a instalada)")) << "\n"
           << tr ("M\xc3\xa1quina: ") << pacote.maquina << "\n"
           << tr ("Vers\xc3\xa3o: ") << pacote.versao << "  |  " << pacote.so << "\n"
           << tr ("Programa: ") << pacote.host << "\n"
           << tr ("Arquivos: ") << pacote.nomesDeArquivo.joinIntoString (", ")
           << "  (" << juce::File::descriptionOfSizeInBytes (pacote.bytes) << ")\n\n"
           << tr ("Os logs guardam o que a ferramenta fez: hor\xc3\xa1rios, tempos de "
                  "transcri\xc3\xa7\xc3\xa3o e erros. N\xc3\xa3o vai \xc3\xa1udio nem o que foi falado.");

    auto* janela = new juce::AlertWindow (tr ("Enviar log para o suporte"),
                                          resumo, juce::MessageBoxIconType::QuestionIcon, this);

    janela->addTextEditor ("recado", {}, tr ("O que aconteceu? (opcional)"));
    janela->addButton (tr ("Enviar"),      1, juce::KeyPress (juce::KeyPress::returnKey));
    janela->addButton (tr ("Abrir pasta"), 2);
    janela->addButton (tr ("Cancelar"),    0, juce::KeyPress (juce::KeyPress::escapeKey));

    janela->setVisible (true);
    janela->toFront (true);

    juce::Component::SafePointer<TranscriberLiveAudioProcessorEditor> seguro (this);

    janela->enterModalState (true, juce::ModalCallbackFunction::create (
        [seguro, janela, pacote] (int escolha)
        {
            const auto recado = janela->getTextEditorContents ("recado");
            delete janela;

            if (escolha == 2)
            {
                tl::Log::get().getFolder().revealToUser();
                return;
            }

            if (escolha != 1)
                return;

            TL_LOGI ("ui", "enviando log para o suporte");

            /*  Fora da thread da interface: a rede de uma casa de show pode
                levar 20 s para responder, e o host nao pode congelar junto. */
            juce::Thread::launch ([seguro, pacote, recado]
            {
                const auto erro = tl::EnvioDeLog::enviar (pacote, recado);

                juce::MessageManager::callAsync ([seguro, erro]
                {
                    if (seguro == nullptr)
                        return;

                    if (erro.isEmpty())
                    {
                        TL_LOGI ("ui", "log enviado");
                        juce::NativeMessageBox::showMessageBoxAsync (
                            juce::MessageBoxIconType::InfoIcon, "Log",
                            tl::tr ("Log enviado. Obrigado \xe2\x80\x94 j\xc3\xa1 estamos com ele aqui."),
                            seguro.getComponent());
                    }
                    else
                    {
                        TL_LOGW ("ui", "falha ao enviar log: " + erro);
                        juce::NativeMessageBox::showMessageBoxAsync (
                            juce::MessageBoxIconType::WarningIcon, "Log",
                            erro + "\n\n"
                              + tl::tr ("Voc\xc3\xaa ainda pode mandar os arquivos \xc3\xa0 m\xc3\xa3o: use Abrir pasta."),
                            seguro.getComponent());
                    }
                });
            });
        }), false);
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

    // A lista ja vem com o melhor endereco primeiro (rede local antes de VPN e
    // de maquina virtual). O indice so sai de zero se o tecnico trocar na mao.
    const int i = juce::jlimit (0, ips.size() - 1, enderecoEscolhido);
    juce::String url = "http://" + ips[i] + ":" + juce::String (processor.hub->getHttpPort()) + "/";

    return url;
}

void TranscriberLiveAudioProcessorEditor::showQr()
{
    const auto url = urlDoDisplay();

    struct Panel : juce::Component
    {
        juce::Label      titulo, endereco, aviso, redeLabel, redeDetalhe;
        juce::TextButton copiar { tl::tr ("Copiar endere\xc3\xa7" "o") };
        juce::ComboBox   redes;
        bool             temEscolhaDeRede = false;
        tl::QrCode       qr;
        bool             temQr = false;
        juce::Rectangle<int> areaQr;

        void paint (juce::Graphics& g) override
        {
            if (! temQr) return;
            // Fundo branco sempre, mesmo no tema escuro: leitor de celular
            // espera escuro sobre claro, e QR invertido muitos aparelhos nao
            // leem.
            qr.desenhar (g, areaQr, juce::Colours::white, juce::Colours::black);
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (10);
            titulo.setBounds (r.removeFromTop (20));
            r.removeFromTop (6);
            areaQr = r.removeFromTop (190);
            r.removeFromTop (8);
            endereco.setBounds (r.removeFromTop (20));
            r.removeFromTop (2);
            aviso.setBounds (r.removeFromTop (34));

            if (temEscolhaDeRede)
            {
                r.removeFromTop (6);
                redeLabel.setBounds (r.removeFromTop (16));
                redes.setBounds (r.removeFromTop (26));
                redeDetalhe.setBounds (r.removeFromTop (30));
            }

            r.removeFromTop (6);
            copiar.setBounds (r.removeFromTop (26));
        }
    };

    auto panel = std::make_unique<Panel>();
    auto* p = panel.get();

    for (auto* c : std::initializer_list<juce::Component*> { &p->titulo, &p->endereco,
                                                             &p->aviso, &p->copiar })
        p->addAndMakeVisible (c);

    p->titulo.setJustificationType (juce::Justification::centred);
    p->endereco.setJustificationType (juce::Justification::centred);
    p->endereco.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    p->aviso.setJustificationType (juce::Justification::centredTop);
    p->aviso.setFont (juce::FontOptions (11.0f));
    p->aviso.setColour (juce::Label::textColourId, tl::col::dim);

    /*  Escolha da rede.

        Chamado real: a maquina do cliente tinha VPN ligada e a lista comecava
        por um IP PUBLICO da Amazon. O QR apontava para la e o celular ficava na
        tela preta. A ordenacao poe a rede local na frente, mas quem tem cabo E
        Wi-Fi ainda pode ter a escolhida errada -- e so a pessoa na sala sabe em
        qual rede o celular esta.

        Antes isto era um botao que girava entre os enderecos as cegas: o
        tecnico clicava e torcia. Agora ele ve a lista inteira, com o IP e que
        tipo de rede e cada um, e escolhe sabendo.                           */
    const auto todosIps = tl::HttpServer::getLocalAddresses();
    p->temEscolhaDeRede = todosIps.size() > 1;

    if (p->temEscolhaDeRede)
    {
        for (auto* c : std::initializer_list<juce::Component*> { &p->redeLabel, &p->redes,
                                                                 &p->redeDetalhe })
            p->addAndMakeVisible (c);

        p->redeLabel.setText (tl::tr ("Rede desta m\xc3\xa1quina"), juce::dontSendNotification);
        p->redeLabel.setFont (juce::FontOptions (11.0f));
        p->redeLabel.setColour (juce::Label::textColourId, tl::col::dim);

        for (int i = 0; i < todosIps.size(); ++i)
            p->redes.addItem (todosIps[i] + "   \xe2\x80\xa2   " + descreverRede (todosIps[i]),
                              i + 1);

        const int atual = juce::jlimit (0, todosIps.size() - 1, enderecoEscolhido);
        p->redes.setSelectedItemIndex (atual, juce::dontSendNotification);

        p->redeDetalhe.setFont (juce::FontOptions (11.0f));
        p->redeDetalhe.setColour (juce::Label::textColourId, tl::col::dim);
        p->redeDetalhe.setJustificationType (juce::Justification::topLeft);

        auto explicar = [p, todosIps] (int i)
        {
            const auto ip = todosIps[juce::jlimit (0, todosIps.size() - 1, i)];
            juce::String s = tl::tr ("Computador: ") + juce::SystemStats::getComputerName();

            if (const auto placa = tl::Adaptadores::doEndereco (ip); placa.sistema.isNotEmpty())
                s << "\n" << tl::tr ("Placa: ") << (placa.nome.isNotEmpty() ? placa.nome : placa.sistema);

            if (! tl::HttpServer::ehRedeLocal (ip))
                s << "\n" << tl::tr ("O celular provavelmente nao alcanca esta. Prefira uma rede local.");
            p->redeDetalhe.setText (s, juce::dontSendNotification);
        };
        explicar (atual);

        juce::Component::SafePointer<TranscriberLiveAudioProcessorEditor> safeRede (this);
        p->redes.onChange = [safeRede, p, explicar]
        {
            if (safeRede == nullptr) return;
            safeRede->enderecoEscolhido = p->redes.getSelectedItemIndex();
            explicar (safeRede->enderecoEscolhido);

            // Redesenha: o QR e o endereco escrito mudaram.
            if (auto* box = p->findParentComponentOfClass<juce::CallOutBox>()) box->dismiss();
            safeRede->showQr();
        };
    }

    p->setSize (300, p->temEscolhaDeRede ? 430 : 352);

    juce::Component::SafePointer<TranscriberLiveAudioProcessorEditor> safeQr (this);

    if (url.isEmpty())
    {
        p->titulo.setText (tr ("Sem rede"), juce::dontSendNotification);
        p->aviso.setText (tl::tr ((
            "Esta m\xc3\xa1quina n\xc3\xa3o tem endere\xc3\xa7o de rede local. "
            "Conecte no mesmo Wi-Fi do celular.")), juce::dontSendNotification);
        p->copiar.setEnabled (false);
    }
    else
    {
        p->titulo.setText (tl::tr ("Endere\xc3\xa7" "o para o celular"), juce::dontSendNotification);
        p->endereco.setText (url, juce::dontSendNotification);
        p->temQr = tl::QrCode::gerar (url, p->qr);   // estatica: texto -> destino

        if (! p->temQr)
            p->aviso.setText (tl::tr ((
                "N\xc3\xa3o consegui gerar o QR para este endere\xc3\xa7o. Digite no navegador.")),
                juce::dontSendNotification);
        else if (processor.getIdentity().remoteHost.isNotEmpty())
            p->aviso.setText (tl::tr ((
                "Display em outra m\xc3\xa1quina: o QR abre a p\xc3\xa1gina de l\xc3\xa1.")),
                juce::dontSendNotification);
        else
            p->aviso.setText (tl::tr ((
                "Aponte a c\xc3\xa2mera do celular. Quem abrir a p\xc3\xa1gina, "
                "pela c\xc3\xa2mera ou digitando o endere\xc3\xa7o, j\xc3\xa1 pode escrever.")),
                juce::dontSendNotification);

        p->copiar.onClick = [url] { juce::SystemClipboard::copyTextToClipboard (url); };
    }

    juce::CallOutBox::launchAsynchronously (std::move (panel), qrButton.getScreenBounds(), nullptr);
}
