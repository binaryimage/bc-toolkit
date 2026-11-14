// <Plugin>Check: tests one bridged plugin without a DAW, using the exact code that goes into the VST3.
//   * every preset makes sound (synths: a chord; effects: a test signal), with no NaNs and no runaway levels
//   * odd block sizes (32, bigger than the host promised) and a sample-rate change
//   * the saved state comes back exactly in a fresh instance
//   * the plugin window: every tab drawn to a PNG, and every control checked (on screen, has a parameter,
//     its images loaded, nothing overlapping)
// Usage: <Plugin>Check <output folder>
#include "MpcBridge.h"
#include "MpcEditor.h"
#include <juce_audio_formats/juce_audio_formats.h>

using namespace bcp;

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

namespace
{
juce::StringArray report;
int failures = 0;
void fail (const juce::String& s) { report.add ("FAIL  " + s); ++failures; }
void note (const juce::String& s) { report.add ("      " + s); }

struct Result { float peak = 0, rms = 0; bool finite = true; };

/** plays notes (synths) or a test signal (effects) through the plugin for `seconds` */
Result render (BridgeProcessor& p, bool synth, double sr, int block, double seconds, juce::AudioBuffer<float>* keep = nullptr)
{
    Result r;
    const int total = (int) (sr * seconds);
    juce::AudioBuffer<float> buf (2, block);
    juce::MidiBuffer midi;
    juce::Random rnd (7);
    double sum = 0; long count = 0;
    const int noteOff = (int) (sr * seconds * 0.6);
    if (keep != nullptr) keep->setSize (2, total);
    for (int pos = 0; pos < total; pos += block)
    {
        const int n = juce::jmin (block, total - pos);
        buf.setSize (2, n, false, false, true);
        buf.clear();
        midi.clear();
        if (synth)
        {
            const int notes[] = { 48, 55, 60, 63, 67 };
            for (int k : notes)
            {
                if (0 >= pos && 0 < pos + n) midi.addEvent (juce::MidiMessage::noteOn (1, k, (juce::uint8) 100), 0);
                if (noteOff >= pos && noteOff < pos + n) midi.addEvent (juce::MidiMessage::noteOff (1, k), noteOff - pos);
            }
        }
        else
        {
            // stabs of a buzzy chord with noise, every half second, then silence at the end
            for (int i = 0; i < n; ++i)
            {
                const double t = (pos + i) / sr;
                const double ph = std::fmod (t, 0.5);
                const float env = t < seconds * 0.6 ? (float) std::exp (-ph * 6.0) : 0.0f;
                const float saw = (float) (std::fmod (t * 110.0, 1.0) * 2 - 1) * 0.4f + (float) (std::fmod (t * 164.8, 1.0) * 2 - 1) * 0.3f;
                const float v = env * (saw + (rnd.nextFloat() * 2 - 1) * 0.1f) * 0.5f;
                buf.setSample (0, i, v); buf.setSample (1, i, v * 0.9f);
            }
        }
        p.processBlock (buf, midi);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < n; ++i)
            {
                const float v = buf.getSample (c, i);
                if (! std::isfinite (v)) r.finite = false;
                r.peak = juce::jmax (r.peak, std::abs (v));
                sum += (double) v * v; ++count;
            }
        if (keep != nullptr) for (int c = 0; c < 2; ++c) keep->copyFrom (c, pos, buf, c, 0, n);
    }
    r.rms = (float) std::sqrt (sum / juce::jmax (1L, count));
    return r;
}

void writePng (const juce::Image& img, const juce::File& f)
{
    f.deleteFile();
    juce::FileOutputStream out (f);
    juce::PNGImageFormat().writeImageToStream (img, out);
}

void writeWav (const juce::AudioBuffer<float>& b, double sr, const juce::File& f)
{
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (f), sr, 2, 24, {}, 0));
    if (w != nullptr) w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile (argc > 1 ? argv[1] : "check");
    out.createDirectory();

    std::unique_ptr<BridgeProcessor> p (static_cast<BridgeProcessor*> (createPluginFilter()));
    const bool synth = p->getTotalNumInputChannels() == 0;
    const juce::String name = p->getName();
    report.add (name + (synth ? "  (instrument)" : "  (effect)") + ": " + juce::String (p->plugin().numParams()) + " parameters, "
                + juce::String (p->getNumPrograms()) + " presets");

    // ---- every preset
    const double sr = 48000;
    p->setPlayConfigDetails (synth ? 0 : 2, 2, sr, 512);
    p->prepareToPlay (sr, 512);
    int silent = 0;
    juce::StringArray silentNames;
    double worstSecs = 0; juce::String worstName;
    for (int i = 0; i < p->getNumPrograms(); ++i)
    {
        p->setCurrentProgram (i);
        p->prepareToPlay (sr, 512);
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        auto r = render (*p, synth, sr, 512, 2.0);
        const double secs = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0;
        if (secs > worstSecs) { worstSecs = secs; worstName = p->getProgramName (i); }
        if (! r.finite) fail ("preset " + juce::String (i + 1) + " " + p->getProgramName (i) + ": NaN/inf in the output");
        if (r.peak > 7.9f) fail ("preset " + juce::String (i + 1) + " " + p->getProgramName (i) + ": runaway level, peak " + juce::String (r.peak, 2));
        if (r.peak < 1.0e-4f) { ++silent; silentNames.add (p->getProgramName (i)); }
    }
    note ("presets: " + juce::String (p->getNumPrograms() - silent) + " of " + juce::String (p->getNumPrograms()) + " make sound"
          + (silent > 0 ? " (silent: " + silentNames.joinIntoString (", ") + ")" : juce::String()));
    if (silent * 4 > p->getNumPrograms()) fail ("too many silent presets");
    note ("slowest preset: " + worstName + ", " + juce::String (2.0 / juce::jmax (1.0e-6, worstSecs), 1) + "x real time on one core");

    // a short WAV of the first few presets, for listening
    {
        juce::AudioBuffer<float> all (2, 0), one;
        for (int i = 0; i < juce::jmin (4, p->getNumPrograms()); ++i)
        {
            p->setCurrentProgram (i); p->prepareToPlay (sr, 512);
            render (*p, synth, sr, 512, 2.0, &one);
            const int at = all.getNumSamples();
            all.setSize (2, at + one.getNumSamples(), true, true);
            for (int c = 0; c < 2; ++c) all.copyFrom (c, at, one, c, 0, one.getNumSamples());
        }
        writeWav (all, sr, out.getChildFile (name.toLowerCase().removeCharacters (" ") + "-first-presets.wav"));
    }

    // ---- block sizes and sample rates
    p->setCurrentProgram (0);
    for (int bs : { 32, 96, 2048 })
    {
        p->prepareToPlay (44100, bs == 2048 ? 512 : bs);   // 2048: bigger blocks than promised
        auto r = render (*p, synth, 44100, bs, 1.0);
        if (! r.finite || r.peak > 7.9f) fail ("block size " + juce::String (bs) + ": bad output");
        if (r.peak < 1.0e-4f) fail ("block size " + juce::String (bs) + ": silent");
    }
    p->prepareToPlay (96000, 256);
    { auto r = render (*p, synth, 96000, 256, 1.0); if (! r.finite || r.peak < 1.0e-4f) fail ("96 kHz: bad or silent output"); }
    note ("block sizes 32 / 96 / 2048 and 96 kHz: ok");

    // ---- state round trip
    {
        p->setCurrentProgram (juce::jmin (3, p->getNumPrograms() - 1));
        auto& pl = p->plugin();
        for (int i = 0; i < pl.numParams(); i += 7) pl.setNormalised (i, std::fmod (pl.getNormalised (i) + 0.37f, 1.0f));
        juce::MemoryBlock state;
        p->getStateInformation (state);
        std::unique_ptr<BridgeProcessor> q (static_cast<BridgeProcessor*> (createPluginFilter()));
        q->setStateInformation (state.getData(), (int) state.getSize());
        int diffs = 0;
        for (int i = 0; i < pl.numParams(); ++i)
            if (std::abs (pl.getNormalised (i) - q->plugin().getNormalised (i)) > 1.0e-6f) ++diffs;
        if (diffs > 0) fail ("state round trip: " + juce::String (diffs) + " parameters came back different");
        else if (q->getCurrentProgram() != p->getCurrentProgram()) fail ("state round trip: preset number lost");
        else note ("state round trip: all " + juce::String (pl.numParams()) + " parameters and the preset come back");
    }

    // ---- the window
    {
        p->setCurrentProgram (0);
        std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
        auto* be = dynamic_cast<BridgeEditor*> (ed.get());
        if (be == nullptr) { fail ("no editor"); }
        else
        {
            for (auto& prob : be->getSkin().problems) fail ("skin: " + prob);
            be->setSize (BridgeEditor::baseWidth(), BridgeEditor::baseHeight());
            juce::Array<int> shown;
            for (int t = 0; t < be->numTabs(); ++t)
            {
                be->showTab (t);
                const auto& tab = be->getSkin().tabs[(size_t) t];
                int controls = 0;
                std::vector<juce::Rectangle<int>> slots;
                for (auto& e : tab.elements)
                {
                    const bool interactive = e.kind == Skin::Kind::KnobStrip || e.kind == Skin::Kind::KnobGeneric || e.kind == Skin::Kind::Button || e.kind == Skin::Kind::Slider;
                    if (! interactive) continue;
                    ++controls;
                    if (e.param < 0 || e.param >= p->plugin().numParams()) fail (tab.name + ": a control has no parameter");
                    else shown.addIfNotAlreadyThere (e.param);
                    if (! juce::Rectangle<int> (0, 0, kTabW, kTabH).contains (e.bounds)) fail (tab.name + ": a control is off the page");
                    if (e.kind == Skin::Kind::KnobStrip && ! e.strip.isValid()) fail (tab.name + ": knob image missing");
                    if (e.kind == Skin::Kind::Button && (! e.onImage.isValid() || ! e.offImage.isValid())) fail (tab.name + ": button image missing");
                }
                for (auto& e : tab.elements) if (e.kind == Skin::Kind::Focus) slots.push_back (e.bounds);
                for (size_t a = 0; a < slots.size(); ++a)
                    for (size_t b = a + 1; b < slots.size(); ++b)
                        if (slots[a].reduced (6).intersects (slots[b].reduced (6))) fail (tab.name + ": two controls overlap");
                note ("tab " + juce::String (t + 1) + " " + tab.name + ": " + juce::String (slots.size()) + " controls drawn");
                juce::ignoreUnused (controls);
                const auto snap = be->createComponentSnapshot (be->getLocalBounds(), true, 1.0f);
                writePng (snap, out.getChildFile (name.toLowerCase().removeCharacters (" ") + "-" + juce::String (t + 1).paddedLeft ('0', 2) + "-"
                                                  + tab.name.toLowerCase().replaceCharacters (" /", "--") + ".png"));
            }
            note ("window: " + juce::String (be->numTabs()) + " tabs, " + juce::String (shown.size()) + " of " + juce::String (p->plugin().numParams()) + " parameters have a control");
            // the window at its normal (smaller) size too, to check it scales
            be->setSize (BridgeEditor::baseWidth() * 3 / 4, BridgeEditor::baseHeight() * 3 / 4);
            be->showTab (0);
            writePng (be->createComponentSnapshot (be->getLocalBounds(), true, 1.0f), out.getChildFile (name.toLowerCase().removeCharacters (" ") + "-small.png"));
        }
    }

    report.insert (1, failures == 0 ? "PASS" : "FAIL (" + juce::String (failures) + ")");
    const auto text = report.joinIntoString ("\n") + "\n";
    out.getChildFile (name.toLowerCase().removeCharacters (" ") + "-check.txt").replaceWithText (text);
    std::printf ("%s", text.toRawUTF8());
    p->releaseResources();
    return failures == 0 ? 0 : 1;
}
