// VST3Probe: loads each built .vst3 the way a DAW does (through the VST3 interfaces), plays it, and reports.
// Usage: VST3Probe <plugin.vst3> [more.vst3 ...]
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    juce::VST3PluginFormat format;
    int failures = 0;
    for (int a = 1; a < argc; ++a)
    {
        const juce::String path (argv[a]);
        juce::OwnedArray<juce::PluginDescription> types;
        format.findAllTypesForFile (types, path);
        if (types.isEmpty()) { std::printf ("FAIL  %s: no plugin found in it\n", path.toRawUTF8()); ++failures; continue; }
        const auto& d = *types[0];
        juce::String err;
        auto inst = format.createInstanceFromDescription (d, 48000.0, 512, err);
        if (inst == nullptr) { std::printf ("FAIL  %s: couldn't open (%s)\n", d.name.toRawUTF8(), err.toRawUTF8()); ++failures; continue; }
        inst->enableAllBuses();
        inst->prepareToPlay (48000.0, 512);
        const int chans = juce::jmax (inst->getTotalNumInputChannels(), inst->getTotalNumOutputChannels());
        juce::AudioBuffer<float> buf (chans, 512);
        juce::MidiBuffer midi;
        float peak = 0; bool finite = true;
        juce::Random rnd (3);
        for (int b = 0; b < 200; ++b)
        {
            buf.clear(); midi.clear();
            if (d.isInstrument) { if (b == 0) for (int n : { 48, 55, 60 }) midi.addEvent (juce::MidiMessage::noteOn (1, n, (juce::uint8) 100), 0);
                                  if (b == 120) for (int n : { 48, 55, 60 }) midi.addEvent (juce::MidiMessage::noteOff (1, n), 0); }
            else if (b < 120) for (int c = 0; c < juce::jmin (2, chans); ++c) for (int i = 0; i < 512; ++i) buf.setSample (c, i, (rnd.nextFloat() * 2 - 1) * 0.3f);
            inst->processBlock (buf, midi);
            for (int c = 0; c < inst->getTotalNumOutputChannels(); ++c)
                for (int i = 0; i < 512; ++i) { const float v = buf.getSample (c, i); if (! std::isfinite (v)) finite = false; peak = juce::jmax (peak, std::abs (v)); }
        }
        // editor opens
        const bool editor = inst->hasEditor();
        std::unique_ptr<juce::AudioProcessorEditor> ed (editor ? inst->createEditorIfNeeded() : nullptr);
        const bool ok = finite && peak > 1.0e-4f && ed != nullptr;
        std::printf ("%s  %s by %s: %s, %d parameters, %d presets, peak %.3f, window %dx%d\n", ok ? "PASS" : "FAIL",
                     d.name.toRawUTF8(), d.manufacturerName.toRawUTF8(), d.isInstrument ? "instrument" : "effect",
                     (int) inst->getParameters().size(), inst->getNumPrograms(), peak,
                     ed ? ed->getWidth() : 0, ed ? ed->getHeight() : 0);
        if (! ok) ++failures;
        ed.reset();
        inst->releaseResources();
    }
    return failures == 0 ? 0 : 1;
}
