#include "MainWindow.h"

class ToolkitApp : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "BC Toolkit"; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise (const juce::String& cmd) override
    {
        engine = std::make_unique<tk::Engine> (true);
        auto args = juce::StringArray::fromTokens (cmd, true);
        int shots = args.indexOf ("--screenshots");
        if (shots >= 0 && shots + 1 < args.size())
        {
            // render every page with demo content, then quit (used by CI)
            juce::LookAndFeel::setDefaultLookAndFeel (&look);
            engine->addDemoSources();
            {
                tk::MainComponent mc (*engine);
                mc.renderAll (juce::File::getCurrentWorkingDirectory().getChildFile (args[shots + 1].unquoted()), 1600, 1000);
            }
            juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
            quit();
            return;
        }
        if (tk::Settings::get().flag ("loopbackOn", false)) engine->startLoopback();
        window = std::make_unique<tk::MainWindow> (*engine);
    }

    void shutdown() override { window.reset(); engine.reset(); }
    void systemRequestedQuit() override { quit(); }
    void anotherInstanceStarted (const juce::String&) override { if (window) window->toFront (true); }

private:
    tk::Look look;
    std::unique_ptr<tk::Engine> engine;
    std::unique_ptr<tk::MainWindow> window;
};

START_JUCE_APPLICATION (ToolkitApp)
