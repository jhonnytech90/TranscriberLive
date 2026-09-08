// Teste de console: alimenta o TranscriptionEngine com um WAV (qualquer taxa,
// mono/estéreo) simulando tempo real e imprime as frases transcritas.
//
//   EngineTest <ggml-model.bin> <ggml-silero.bin|-> <audio.wav> [gateDb]

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>
#include "TranscriptionEngine.h"
#include "MessageBus.h"
#include <cstdio>

int main (int argc, char** argv)
{
    if (argc < 4) { std::printf ("uso: EngineTest model vad|- audio.wav [gateDb]\n"); return 1; }

    juce::MessageManager::getInstance();

    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (juce::File (argv[3])));
    if (reader == nullptr) { std::printf ("nao abriu o wav\n"); return 1; }

    juce::AudioBuffer<float> buf ((int) reader->numChannels, (int) reader->lengthInSamples);
    reader->read (&buf, 0, (int) reader->lengthInSamples, 0, true, true);

    // mono + reamostragem simples p/ 16k (linear) — só para o teste
    const double ratio = reader->sampleRate / 16000.0;
    std::vector<float> mono;
    for (double pos = 0; pos + 1 < buf.getNumSamples(); pos += ratio)
    {
        const int i = (int) pos; const float f = (float) (pos - i);
        float a = 0, b = 0;
        for (int c = 0; c < buf.getNumChannels(); ++c) { a += buf.getSample (c, i); b += buf.getSample (c, i + 1); }
        a /= buf.getNumChannels(); b /= buf.getNumChannels();
        mono.push_back (a + (b - a) * f);
    }

    TranscriptionEngine engine;
    auto s = engine.getSettings();
    if (argc > 4) s.gateThresholdDb = (float) atof (argv[4]);
    if (argc > 5) s.language = argv[5];
    engine.setSettings (s);

    // opcional: argv[6] = nome do canal -> também manda p/ o Display via UDP (127.0.0.1:47800)
    tl::BusSender bus;
    const juce::String chName = argc > 6 ? argv[6] : "";
    const juce::String chId = juce::Uuid().toString();
    auto makeMsg = [&] (const juce::String& type)
    {
        tl::Message m; m.type = type; m.channelId = chId; m.name = chName; m.colour = juce::Colour (0xff4fc3f7);
        m.importance = 2; m.flash = true; m.timeMs = juce::Time::currentTimeMillis(); return m;
    };
    if (chName.isNotEmpty())
    {
        bus.send (makeMsg ("hello"));
        engine.onLine = [&] (const TranscriptionEngine::Line& l)
        {
            if (l.text.isEmpty()) return;
            auto m = makeMsg ("msg"); m.utteranceId = l.utteranceId; m.text = l.text; m.isFinal = l.isFinal; bus.send (m);
        };
    }

    engine.loadModel (juce::File (argv[1]));
    if (juce::String (argv[2]) != "-") engine.loadVadModel (juce::File (argv[2]));

    while (! engine.isModelLoaded()) juce::Thread::sleep (50);
    std::printf ("%s\n", engine.getStatus().toRawUTF8());

    // simula tempo real em blocos de 20 ms
    const int block = 320;
    int lastVersion = engine.getLinesVersion();
    auto t0 = juce::Time::getMillisecondCounterHiRes();

    auto dump = [&]
    {
        if (engine.getLinesVersion() == lastVersion) return;
        lastVersion = engine.getLinesVersion();
        for (auto& l : engine.getLines())
            std::printf ("  [%s] %s\n", l.isFinal ? "FINAL  " : "parcial", l.text.toRawUTF8());
        std::printf ("  ---- (%.1fs)\n", (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0);
    };

    for (size_t pos = 0; pos + block <= mono.size(); pos += block)
    {
        engine.pushAudio (mono.data() + pos, block);
        juce::Thread::sleep (20);
        dump();
    }

    // silêncio final p/ fechar a última frase
    std::vector<float> silence (16000, 0.0f);
    engine.pushAudio (silence.data(), (int) silence.size());
    for (int i = 0; i < 150; ++i) { juce::Thread::sleep (100); dump(); if (! engine.isTranscribing() && ! engine.isSpeechActive() && engine.getPendingSamples() == 0 && i > 20) break; }

    std::printf ("\nRESULTADO FINAL:\n");
    for (auto& l : engine.getLines()) std::printf ("  %s\n", l.text.toRawUTF8());
    return 0;
}
