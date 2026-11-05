#pragma once
#include "Page.h"

namespace tk
{
    /** Sidebar of 3D keys + header + the current page. */
    class MainComponent : public juce::Component, private juce::Timer
    {
    public:
        explicit MainComponent (Engine&);
        ~MainComponent() override;
        void paint (juce::Graphics&) override;
        void resized() override;
        void show (const juce::String& pageId);
        Page* page (const juce::String& pageId);
        void sendAudio (const juce::String& pageId, const juce::AudioBuffer<float>&, double sr, const juce::String& name);
        juce::OwnedArray<Page> pages;
        Page* current = nullptr;
        /** render every page to PNGs (screenshots), with demo content */
        void renderAll (const juce::File& dir, int w, int h);
        /** Every page, every tab on it, at the window sizes the app allows (wide, the minimum, tall like a vertical
            monitor): screenshots plus the button check. Returns one line per problem (empty = all good) and writes
            the full report to dir/layout-check.txt. */
        juce::StringArray checkAll (const juce::File& dir);
    private:
        void timerCallback() override;
        Engine& engine;
        juce::OwnedArray<juce::TextButton> nav;
        juce::String status;
    };

    class MainWindow : public juce::DocumentWindow
    {
    public:
        MainWindow (Engine&);
        ~MainWindow() override;
        void closeButtonPressed() override;
    private:
        Look look;
    };
}
