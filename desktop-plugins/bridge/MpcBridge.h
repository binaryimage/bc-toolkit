// MpcBridge: runs one of binaryimage's MPC plugins (written against Akai's MPC Plugin SDK) as a desktop
// VST3 through JUCE. The plugin's own source is compiled in unchanged; the bridge talks to it through the
// SDK's VST2-style entry point (VSTPluginMain) and its mpc::Plugin object, the same way the MPC does.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <atomic>
#include <memory>
#include <vector>

#include "mpc_plugin.h"

namespace bcp
{
class BridgeProcessor;

/** One host-facing parameter, backed directly by the MPC plugin's own atomic value. */
class BridgeParam : public juce::AudioProcessorParameterWithID
{
public:
    BridgeParam (BridgeProcessor& owner, int index);
    float getValue() const override;
    void setValue (float newValue) override;
    float getDefaultValue() const override;
    juce::String getText (float normalised, int maximumLength) const override;
    float getValueForText (const juce::String& text) const override;
    int getNumSteps() const override;
    bool isDiscrete() const override;
    bool isBoolean() const override;
    juce::String getLabel() const override;
    bool isAutomatable() const override;
    juce::StringArray getAllValueStrings() const override;

    /** what the host was last told, so changes made inside the plugin can be reported */
    std::atomic<float> lastReported { -1.0f };
    const int index;
private:
    BridgeProcessor& owner;
};

class BridgeProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    BridgeProcessor();
    ~BridgeProcessor() override;

    // ---- the MPC plugin
    mpc::Plugin& plugin() const { return *mpcPlugin; }
    juce::String valueText (int index) const;            // as the MPC shows it (value + unit)
    /** sets a parameter from the plugin window, telling the host (with a gesture) */
    void setFromUi (int index, float normalised, bool beginGesture, bool endGesture);
    BridgeParam* bridgeParam (int index) const { return params[(size_t) index]; }
    std::atomic<int> uiProgramVersion { 0 };              // bumped when a preset loads, so the window refreshes

    // ---- AudioProcessor
    const juce::String getName() const override;
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    bool acceptsMidi() const override;
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // the SDK's host callback (audioMasterGetTime etc.)
    static VstIntPtr hostCallback (AEffect* effect, VstInt32 opcode, VstInt32 index, VstIntPtr value, void* ptr, float opt);

private:
    static BusesProperties busesFor (bool instrument);
    AEffect* effect = nullptr;
    mpc::Plugin* mpcPlugin = nullptr;
    std::vector<BridgeParam*> params;
    VstTimeInfo timeInfo {};
    int maxBlock = 512;
    bool instrument = false;

    // MIDI handed to the plugin each block
    static constexpr int kMaxEvents = 1024;
    struct EventBlock { VstInt32 numEvents; VstIntPtr reserved; VstEvent* events[kMaxEvents]; };
    EventBlock eventBlock {};
    std::vector<VstMidiEvent> midiEvents;

    VstIntPtr dispatch (VstInt32 opcode, VstInt32 index = 0, VstIntPtr value = 0, void* ptr = nullptr, float opt = 0.0f);
    void fillTimeInfo();
    void timerCallback() override;   // reports parameter changes made inside the plugin (presets, MIDI CC) to the host

    friend class BridgeParam;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BridgeProcessor)
};

/** Window size of the MPC tab area the skins are drawn for. */
constexpr int kTabW = 1280, kTabH = 628;
}
