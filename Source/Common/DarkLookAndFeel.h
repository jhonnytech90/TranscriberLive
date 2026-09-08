#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace tl
{
    // Paleta neutra (dark). Cor só aparece nos balões das pessoas.
    namespace col
    {
        const juce::Colour bg      { 0xff0e1114 };
        const juce::Colour panel   { 0xff161a1f };
        const juce::Colour side    { 0xff121519 };
        const juce::Colour widget  { 0xff1f252c };
        const juce::Colour outline { 0xff2b333c };
        const juce::Colour text    { 0xffe8ebee };
        const juce::Colour dim     { 0xff8a95a3 };
        const juce::Colour highlight { 0xff3a4450 };
    }

    /** LookAndFeel escuro e neutro para os dois plugins. */
    class DarkLookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        DarkLookAndFeel()
            : juce::LookAndFeel_V4 (ColourScheme (col::bg,            // windowBackground
                                                  col::widget,        // widgetBackground
                                                  col::panel,         // menuBackground
                                                  col::outline,       // outline
                                                  col::text,          // defaultText
                                                  col::widget,        // defaultFill
                                                  col::text,          // highlightedText
                                                  col::highlight,     // highlightedFill
                                                  col::text))         // menuText
        {
            setColour (juce::TextButton::buttonColourId,     col::widget);
            setColour (juce::TextButton::buttonOnColourId,   col::highlight);
            setColour (juce::TextButton::textColourOffId,    col::text);
            setColour (juce::TextButton::textColourOnId,     col::text);
            setColour (juce::ComboBox::backgroundColourId,   col::widget);
            setColour (juce::ComboBox::outlineColourId,      col::outline);
            setColour (juce::ComboBox::textColourId,         col::text);
            setColour (juce::ComboBox::arrowColourId,        col::dim);
            setColour (juce::PopupMenu::backgroundColourId,  col::panel);
            setColour (juce::PopupMenu::highlightedBackgroundColourId, col::highlight);
            setColour (juce::TextEditor::backgroundColourId, col::widget);
            setColour (juce::TextEditor::outlineColourId,    col::outline);
            setColour (juce::TextEditor::focusedOutlineColourId, col::dim);
            setColour (juce::TextEditor::textColourId,       col::text);
            setColour (juce::TextEditor::highlightColourId,  col::highlight);
            setColour (juce::Slider::backgroundColourId,     col::widget);
            setColour (juce::Slider::trackColourId,          col::dim);
            setColour (juce::Slider::thumbColourId,          col::text);
            setColour (juce::Slider::textBoxBackgroundColourId, col::widget);
            setColour (juce::Slider::textBoxOutlineColourId, col::outline);
            setColour (juce::Slider::textBoxTextColourId,    col::text);
            setColour (juce::ToggleButton::textColourId,     col::text);
            setColour (juce::ToggleButton::tickColourId,     col::text);
            setColour (juce::ToggleButton::tickDisabledColourId, col::dim);
            setColour (juce::Label::textColourId,            col::text);
            setColour (juce::ScrollBar::thumbColourId,       col::outline);
            setColour (juce::ColourSelector::backgroundColourId, col::panel);
            setColour (juce::ColourSelector::labelTextColourId,  col::text);
            setColour (juce::FileBrowserComponent::currentPathBoxBackgroundColourId, col::widget);
            setColour (juce::FileBrowserComponent::filenameBoxBackgroundColourId,    col::widget);
            setColour (juce::ListBox::backgroundColourId,    col::panel);
            setColour (juce::DirectoryContentsDisplayComponent::highlightColourId, col::highlight);
        }
    };

    /** Cor de texto legível sobre a cor do balão. */
    inline juce::Colour textOn (juce::Colour bubble)
    {
        return bubble.getPerceivedBrightness() > 0.55f ? juce::Colour (0xff0e1114) : juce::Colours::white;
    }
}
