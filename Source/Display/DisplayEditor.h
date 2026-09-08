#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "DisplayProcessor.h"
#include "Common/DarkLookAndFeel.h"

//==============================================================================
/** Conversa em balões. */
class ChatView : public juce::Component
{
public:
    void setEntries (std::vector<tl::MessageStore::Entry> e, int fontPx);
    void paint (juce::Graphics& g) override;
    int  getPreferredHeight (int width) const;

private:
    struct Layout { juce::TextLayout text; int height = 0; };
    void layoutEntry (const tl::MessageStore::Entry& e, int textW, juce::TextLayout& tl) const;

    std::vector<tl::MessageStore::Entry> entries;
    int fontPx = 26;
};

//==============================================================================
/** Barra lateral de participantes (clique = filtra). */
class ParticipantList : public juce::Component
{
public:
    std::function<void (const juce::String&)> onSelect;   // "" = todos

    void setParticipants (std::vector<tl::MessageStore::Participant> p, const juce::String& selected);
    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    int  getPreferredHeight() const { return 44 + (int) participants.size() * rowH; }

private:
    static constexpr int rowH = 46;
    std::vector<tl::MessageStore::Participant> participants;
    juce::String selectedId;
};

//==============================================================================
/** Camada de flash por cima de tudo. */
class FlashOverlay : public juce::Component
{
public:
    FlashOverlay() { setInterceptsMouseClicks (false, false); }
    void trigger (juce::Colour c, int importance);
    void tick();          // chamado pelo timer do editor (30 Hz)
    void paint (juce::Graphics& g) override;

private:
    juce::Colour colour;
    float alpha = 0.0f;
    int pulsesLeft = 0;
    float decay = 0.06f;
};

//==============================================================================
class DisplayAudioProcessorEditor : public juce::AudioProcessorEditor,
                                    private juce::Timer
{
public:
    explicit DisplayAudioProcessorEditor (DisplayAudioProcessor&);
    ~DisplayAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void refresh (bool force);
    void showSettings();

    DisplayAudioProcessor& processor;
    tl::DarkLookAndFeel lnf;

    juce::Label        titleLabel, statusLabel;
    juce::ToggleButton flashButton { "Flash" };
    juce::TextButton   clearButton { "Limpar tudo" }, fontDownButton { "A-" }, fontUpButton { "A+" },
                       settingsButton { "Rede..." };

    juce::Viewport      sideViewport, chatViewport;
    ParticipantList     participants;
    ChatView            chat;
    FlashOverlay        flash;

    juce::String selectedChannel;
    int  lastVersion = -1;
    int  lastFontSize = 0;
    juce::int64 lastFlashSeq = 0;
    juce::uint32 lastParticipantRefresh = 0;
    bool userScrolledUp = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DisplayAudioProcessorEditor)
};
