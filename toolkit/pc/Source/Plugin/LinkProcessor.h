#pragma once
#include "Common/JuceHeader.h"
#include "Common/Link.h"

/** BC Link: passes audio through untouched and streams a copy to the Toolkit app. */
class LinkProcessor : public juce::AudioProcessor
{
public:
    LinkProcessor();
    ~LinkProcessor() override = default;

    void prepareToPlay (double sampleRate, int) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "BC Link"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;
    void updateTrackProperties (const TrackProperties&) override;

    /** the name Toolkit shows: your own if you typed one, else the Ableton track name */
    juce::String displayName() const;
    void setCustomName (const juce::String&);
    juce::String customName() const { const juce::ScopedLock sl (nameLock); return custom; }
    juce::String hostTrackName() const { const juce::ScopedLock sl (nameLock); return trackName; }

    bool isLinked() const { return client.isLinked(); }
    std::atomic<float> peak { 0 };
    /** last ~85 ms of mono audio, for the editor's tuner */
    void copyRecent (float* dest, int n) const;
    double sampleRate() const { return sr.load(); }

private:
    tk::link::Client client;
    mutable juce::CriticalSection nameLock;
    juce::String custom, trackName;
    std::atomic<double> sr { 48000 };
    static constexpr int R = 4096;
    std::array<float, R> ring {};
    std::atomic<int> w { 0 };
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LinkProcessor)
};
