#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "License.h"
#include "DarkLookAndFeel.h"

namespace tl
{
    //==============================================================================
    /**
        Painel de licença: mostra o ID desta máquina (com botão de copiar) e recebe
        a chave colada. Sem licença válida o Receiver não transcreve.
    */
    class LicensePanel : public juce::Component
    {
    public:
        std::function<void (const juce::String& licenseText)> onActivate;   // devolve o texto p/ o processor instalar
        std::function<void()> onRemove;
        std::function<void()> onClose;

        LicensePanel()
        {
            auto label = [this] (juce::Label& l, const juce::String& t, float size, juce::Colour c, bool bold = false)
            {
                l.setText (t, juce::dontSendNotification);
                l.setFont (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
                l.setColour (juce::Label::textColourId, c);
                addAndMakeVisible (l);
            };

            label (title,    juce::String (juce::CharPointer_UTF8 ("LICEN\xc3\x87" "A")), 14.0f, col::text, true);
            label (idLabel,  juce::String (juce::CharPointer_UTF8 ("ID desta m\xc3\xa1quina")), 12.0f, col::dim);
            label (keyLabel, juce::String (juce::CharPointer_UTF8 ("Cole aqui a chave que voc\xc3\xaa recebeu")), 12.0f, col::dim);
            label (status,   "", 12.0f, col::dim);
            status.setJustificationType (juce::Justification::topLeft);

            machineId.setReadOnly (true);
            machineId.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 15.0f, juce::Font::bold));
            machineId.setJustification (juce::Justification::centred);
            addAndMakeVisible (machineId);

            copyButton.onClick = [this]
            {
                juce::SystemClipboard::copyTextToClipboard (License::getMachineId());
                setStatus (juce::String (juce::CharPointer_UTF8 ("ID copiado — mande para o desenvolvedor junto com seu nome e e-mail.")), false);
            };
            addAndMakeVisible (copyButton);

            keyEditor.setMultiLine (true, true);
            keyEditor.setReturnKeyStartsNewLine (true);
            keyEditor.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::plain));
            keyEditor.setTextToShowWhenEmpty ("-----TRANSCRIBER LIVE LICENSE----- ...", col::dim);
            addAndMakeVisible (keyEditor);

            activateButton.onClick = [this]
            {
                if (onActivate) onActivate (keyEditor.getText());
            };
            addAndMakeVisible (activateButton);

            pasteButton.onClick = [this]
            {
                const auto clip = juce::SystemClipboard::getTextFromClipboard();
                if (clip.isNotEmpty()) keyEditor.setText (clip, false);
            };
            addAndMakeVisible (pasteButton);

            removeButton.onClick = [this] { if (onRemove) onRemove(); };
            addChildComponent (removeButton);

            closeButton.onClick = [this] { if (onClose) onClose(); };
            addChildComponent (closeButton);
        }

        /** Atualiza o painel com o estado atual da licença. */
        void setInfo (const LicenseInfo& info, bool canClose)
        {
            machineId.setText (License::getMachineId(), juce::dontSendNotification);

            if (info.valid)
            {
                juce::String s;
                s << juce::String (juce::CharPointer_UTF8 ("Ativo. Licenciado para ")) << info.name;
                if (info.email.isNotEmpty()) s << " <" << info.email << ">";
                s << juce::newLine << "serial " << info.serial;
                if (info.issued.isNotEmpty())  s << juce::String (juce::CharPointer_UTF8 ("  \xc2\xb7  emitida em ")) << info.issued;
                s << juce::String (juce::CharPointer_UTF8 ("  \xc2\xb7  ")) << (info.isPerpetual() ? juce::String (juce::CharPointer_UTF8 ("perp\xc3\xa9tua"))
                                                                                                   : juce::String (juce::CharPointer_UTF8 ("v\xc3\xa1lida at\xc3\xa9 ")) + info.expires);
                setStatus (s, false);
            }
            else
            {
                setStatus (info.error, true);
            }

            licensed = info.valid;
            keyLabel.setVisible (! info.valid);
            keyEditor.setVisible (! info.valid);
            activateButton.setVisible (! info.valid);
            pasteButton.setVisible (! info.valid);
            removeButton.setVisible (info.valid);
            closeButton.setVisible (canClose);
            if (info.valid) keyEditor.clear();
            resized();
            repaint();
        }

        void setStatus (const juce::String& text, bool isError)
        {
            status.setText (text, juce::dontSendNotification);
            status.setColour (juce::Label::textColourId, isError ? juce::Colour (0xffffb84d) : col::dim);
        }

        void paint (juce::Graphics& g) override
        {
            g.setColour (col::panel);
            g.fillRoundedRectangle (getLocalBounds().toFloat(), 10.0f);
            g.setColour (col::outline);
            g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 10.0f, 1.0f);
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (16, 12);

            auto top = r.removeFromTop (22);
            title.setBounds (top.removeFromLeft (120));
            closeButton.setBounds (top.removeFromRight (80));
            r.removeFromTop (8);

            auto idRow = r.removeFromTop (30);
            idLabel.setBounds (idRow.removeFromLeft (128));
            copyButton.setBounds (idRow.removeFromRight (80).reduced (0, 2));
            idRow.removeFromRight (8);
            machineId.setBounds (idRow.removeFromLeft (juce::jmin (210, idRow.getWidth())).reduced (0, 2));

            r.removeFromTop (10);
            status.setBounds (r.removeFromTop (licensed ? 34 : 30));

            if (! licensed)
            {
                r.removeFromTop (6);
                keyLabel.setBounds (r.removeFromTop (16));
                auto buttons = r.removeFromBottom (30);
                activateButton.setBounds (buttons.removeFromRight (110).reduced (0, 2));
                buttons.removeFromRight (8);
                pasteButton.setBounds (buttons.removeFromRight (110).reduced (0, 2));
                r.removeFromBottom (8);
                keyEditor.setBounds (r);
            }
            else
            {
                removeButton.setBounds (r.removeFromTop (30).removeFromLeft (150).reduced (0, 2));
            }
        }

    private:
        juce::Label title, idLabel, keyLabel, status;
        juce::TextEditor machineId, keyEditor;
        juce::TextButton copyButton { "Copiar" }, pasteButton { "Colar" },
                         activateButton { "Ativar" },
                         removeButton { juce::String (juce::CharPointer_UTF8 ("Remover licen\xc3\xa7" "a")) },
                         closeButton { "Fechar" };
        bool licensed = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LicensePanel)
    };
}
