// Teste do Receiver completo (processor + hub + editor), como app de janela:
// alimenta processBlock com um WAV em tempo real; o hub sobe UDP+web sozinho.
//   ReceiverTest audio.wav [idioma] [nome]
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "Receiver/ReceiverProcessor.h"
#include <cstdio>

class ReceiverTestApp : public juce::JUCEApplication, private juce::Timer
{
public:
    const juce::String getApplicationName() override    { return "ReceiverTest"; }
    const juce::String getApplicationVersion() override { return "0.3.0"; }

    void initialise (const juce::String& cmd) override
    {
        auto args = juce::StringArray::fromTokens (cmd, true);
        args.removeEmptyStrings();
        if (args.isEmpty()) { std::printf ("uso: ReceiverTest audio.wav [idioma] [nome]\n"); quit(); return; }

        juce::AudioFormatManager fm; fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (juce::File::getCurrentWorkingDirectory().getChildFile (args[0].unquoted())));
        if (reader == nullptr) { std::printf ("nao abriu o wav\n"); quit(); return; }
        sampleRate = reader->sampleRate;
        wav.setSize ((int) reader->numChannels, (int) reader->lengthInSamples);
        reader->read (&wav, 0, (int) reader->lengthInSamples, 0, true, true);

        proc = std::make_unique<TranscriberLiveAudioProcessor>();
        if (args.size() > 1) { auto s = proc->engine.getSettings(); s.language = args[1]; proc->engine.setSettings (s); }
        if (args.size() > 2) { auto id = proc->getIdentity(); id.name = args[2].unquoted(); id.importance = 2; proc->setIdentity (id); }

        std::printf ("modelo: %s | hub UDP %s | web %s\n", proc->getSelectedModel().toRawUTF8(),
                     proc->hub->isBusPrimary() ? "ok" : "NAO", proc->hub->isWebPrimary() ? "ok" : "NAO");

        struct W : juce::DocumentWindow { using DocumentWindow::DocumentWindow; void closeButtonPressed() override { JUCEApplication::getInstance()->systemRequestedQuit(); } };
        window = std::make_unique<W> ("Transcriber Live Receiver", juce::Colour (0xff0e1114), juce::DocumentWindow::allButtons);
        window->setUsingNativeTitleBar (true);
        window->setContentOwned (proc->createEditor(), true);
        window->centreWithSize (window->getWidth(), window->getHeight());
        window->setVisible (true);

        proc->prepareToPlay (sampleRate, block);
        buf.setSize (2, block);
        startTimer ((int) (1000.0 * block / sampleRate));
    }

    void shutdown() override
    {
        stopTimer();
        std::printf ("\nFRASES (engine):\n");
        for (auto& l : proc->engine.getLines()) std::printf ("  %s\n", l.text.toRawUTF8());
        std::printf ("STORE do hub: %d entradas\n", (int) proc->hub->store.getEntries().size());
        window.reset();
        proc.reset();
    }

private:
    void timerCallback() override
    {
        if (! proc->engine.isModelLoaded()) return;

        if (pos + block <= wav.getNumSamples())
        {
            for (int ch = 0; ch < 2; ++ch)
                buf.copyFrom (ch, 0, wav, juce::jmin (ch, wav.getNumChannels() - 1), pos, block);
            pos += block;
        }
        else
        {
            buf.clear();
            if (++silentBlocks > (int) (sampleRate / block) * 40) quit();   // ~40 s de servidor no ar depois do áudio
        }
        juce::MidiBuffer midi;
        proc->processBlock (buf, midi);
    }

    std::unique_ptr<TranscriberLiveAudioProcessor> proc;
    std::unique_ptr<juce::DocumentWindow> window;
    juce::AudioBuffer<float> wav, buf;
    double sampleRate = 48000.0;
    const int block = 512;
    int pos = 0, silentBlocks = 0;
};

START_JUCE_APPLICATION (ReceiverTestApp)
