#pragma once
#include "LinkProcessor.h"
#include "Common/Look.h"
#include "Common/Widgets.h"

class LinkEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit LinkEditor (LinkProcessor&);
    ~LinkEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    void timerCallback() override;
    LinkProcessor& proc;
    tk::Look look;
    juce::TextEditor name;
    tk::LedMeter meter;
    tk::TunerDial tuner;
    bool linked = false;
    juce::String shownName;
    std::vector<float> buf;
};
