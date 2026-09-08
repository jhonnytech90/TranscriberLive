// App Standalone do Display — janela pura, sem dispositivo de áudio.
#include <juce_gui_extra/juce_gui_extra.h>
#include "DisplayProcessor.h"
#include "DisplayEditor.h"
#include "BinaryData.h"

class DisplayApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "Transcriber Live Display"; }
    const juce::String getApplicationVersion() override { return "0.3.0"; }
    bool moreThanOneInstanceAllowed() override          { return true; }

    void initialise (const juce::String&) override
    {
        processor = std::make_unique<DisplayAudioProcessor>();
        window = std::make_unique<MainWindow> (getApplicationName(), *processor);
    }

    void shutdown() override
    {
        window.reset();
        processor.reset();
    }

    void systemRequestedQuit() override { quit(); }

private:
    class MainWindow : public juce::DocumentWindow
    {
    public:
        MainWindow (const juce::String& name, DisplayAudioProcessor& p)
            : DocumentWindow (name, tl::col::bg, DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (p.createEditor(), true);
            setResizable (true, false);
            centreWithSize (1000, 620);
            setVisible (true);
        }

        void closeButtonPressed() override { JUCEApplication::getInstance()->systemRequestedQuit(); }
    };

    std::unique_ptr<DisplayAudioProcessor> processor;
    std::unique_ptr<MainWindow> window;
};

START_JUCE_APPLICATION (DisplayApplication)
