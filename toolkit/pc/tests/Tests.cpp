#include "App/MainWindow.h"
#include "App/SamplerCore.h"
#include "Common/Dsp.h"
#include "Common/Wavetable.h"
#include "Common/Mangle.h"
#include "Common/Theory.h"
#include "Common/Link.h"
#include "Plugin/LinkEditor.h"

namespace
{
int failures = 0, passes = 0;
void check (bool ok, const juce::String& what, const juce::String& detail = {})
{
    std::printf ("%s  %s%s\n", ok ? "PASS" : "FAIL", what.toRawUTF8(), detail.isNotEmpty() ? ("   (" + detail + ")").toRawUTF8() : "");
    (ok ? passes : failures)++;
}

juce::AudioBuffer<float> sine (double hz, double sr, double seconds, float amp, int ch = 1)
{
    juce::AudioBuffer<float> b (ch, (int) (sr * seconds));
    for (int c = 0; c < ch; ++c)
        for (int i = 0; i < b.getNumSamples(); ++i) b.setSample (c, i, amp * (float) std::sin (2 * juce::MathConstants<double>::pi * hz * i / sr));
    return b;
}

void testPitch()
{
    for (double hz : { 41.2, 110.0, 261.63, 880.0, 2093.0 })
    {
        auto b = sine (hz, 48000, 0.2, 0.5f);
        double got = tk::yinPitch (b.getReadPointer (0), 4096, 48000);
        double cents = 1200 * std::log2 (got / hz);
        check (std::abs (cents) < 2, "YIN pitch " + juce::String (hz) + " Hz", juce::String (got, 2) + " Hz, " + juce::String (cents, 2) + " c");
    }
    // a saw with harmonics still reads the fundamental
    juce::AudioBuffer<float> saw (1, 9600);
    for (int i = 0; i < 9600; ++i) saw.setSample (0, i, (float) (2 * std::fmod (i * 73.42 / 48000, 1.0) - 1) * 0.5f);
    double got = tk::yinPitch (saw.getReadPointer (0), 4096, 48000);
    check (std::abs (1200 * std::log2 (got / 73.42)) < 5, "YIN on a low saw (D2)", juce::String (got, 2) + " Hz");
    auto noise = sine (0, 48000, 0.1, 0); juce::Random r (1);
    for (int i = 0; i < noise.getNumSamples(); ++i) noise.setSample (0, i, r.nextFloat() * 2 - 1);
    check (tk::yinPitch (noise.getReadPointer (0), 4096, 48000) < 0, "YIN says no pitch for white noise");
    check (tk::noteName (60) == "C3" && tk::noteName (36) == "C1", "note names follow Ableton (middle C = C3)", tk::noteName (60));
}

void testLoudness()
{
    tk::Loudness l; l.prepare (48000);
    auto b = sine (997, 48000, 5.0, 0.1f, 2);
    for (int i = 0; i < b.getNumSamples(); i += 480) l.process (b.getReadPointer (0, i), b.getReadPointer (1, i), 480);
    check (std::abs (l.integrated() - (-20.0)) < 0.6, "LUFS of a -20 dBFS stereo 1 kHz sine is about -20", juce::String (l.integrated(), 2));
    check (std::abs (l.truePeakDb() - (-20.0)) < 0.5, "true peak of that sine is about -20 dBTP", juce::String (l.truePeakDb(), 2));
    tk::Loudness q; q.prepare (48000);
    auto s = sine (997, 48000, 3.0, 0.0f, 2);
    for (int i = 0; i < s.getNumSamples(); i += 480) q.process (s.getReadPointer (0, i), s.getReadPointer (1, i), 480);
    check (q.integrated() < -70, "silence is gated out of integrated loudness", juce::String (q.integrated(), 1));
}

void testHum()
{
    auto b = sine (60, 48000, 0.5, 0.05f);
    for (int h = 2; h <= 4; ++h) for (int i = 0; i < b.getNumSamples(); ++i) b.addSample (0, i, 0.02f * (float) std::sin (2 * juce::MathConstants<double>::pi * 60 * h * i / 48000));
    auto r = tk::detectHum (b.getReadPointer (0), b.getNumSamples(), 48000);
    check (r.present && r.mainsHz == 60, "hum finder catches 60 Hz + harmonics", juce::String (r.mainsHz) + " Hz, " + juce::String (r.ratioDb, 1) + " dB");
    auto clean = sine (440, 48000, 0.5, 0.3f);
    check (! tk::detectHum (clean.getReadPointer (0), clean.getNumSamples(), 48000).present, "no hum on a clean A440");
}

juce::AudioBuffer<float> clicks (double bpm, double sr, double seconds, int every = 1)
{
    juce::AudioBuffer<float> b (1, (int) (sr * seconds)); b.clear();
    double spb = 60.0 / bpm * sr;
    juce::Random r (3);
    for (int k = 0; k * spb < b.getNumSamples(); k += every)
        for (int i = 0; i < 2000 && (int) (k * spb) + i < b.getNumSamples(); ++i)
            b.setSample (0, (int) (k * spb) + i, (r.nextFloat() * 2 - 1) * 0.8f * (float) std::exp (-i / 300.0));
    return b;
}

void testOnsetsTempoKey()
{
    auto b = clicks (120, 48000, 4.0);
    auto on = tk::detectOnsets (b, 48000);
    check (on.size() == 8, "onsets: 8 hits in 4 s at 120 BPM", juce::String ((int) on.size()));
    bool aligned = true;
    for (size_t i = 0; i < on.size(); ++i) aligned = aligned && std::abs (on[i] - (int) (i * 24000)) < 600;
    check (aligned, "onsets land within 12 ms of the hits");
    double bpm = tk::estimateBpm (clicks (96, 48000, 10.0), 48000);
    check (std::abs (bpm - 96) < 1.5, "tempo of a 96 BPM pulse", juce::String (bpm, 1));
    double bpm2 = tk::estimateBpm (clicks (140, 48000, 10.0), 48000);
    check (std::abs (bpm2 - 140) < 2, "tempo of a 140 BPM pulse", juce::String (bpm2, 1));
    // A minor triad arpeggio + bass should read as A minor
    juce::AudioBuffer<float> k (1, 48000 * 4); k.clear();
    const int notes[] = { 57, 60, 64, 69, 64, 60, 45, 57 };
    for (int n = 0; n < 8; ++n)
    {
        double hz = tk::hzFromMidi (notes[n]);
        for (int i = 0; i < 24000; ++i) k.addSample (0, n * 24000 + i, 0.3f * (float) std::sin (2 * juce::MathConstants<double>::pi * hz * i / 48000));
    }
    auto key = tk::estimateKey (k, 48000);
    check (key.root == 9 && key.minor, "key of an A minor arpeggio", key.name());
}

void testFiles (const juce::File& dir)
{
    auto b = sine (220, 44100, 0.5, 0.5f, 2);
    auto f = dir.getChildFile ("roundtrip.wav");
    check (tk::writeWav (f, b, 44100, 24), "write 24-bit WAV");
    juce::AudioBuffer<float> r; double sr = 0;
    check (tk::readAudio (f, r, sr) && sr == 44100 && r.getNumSamples() == b.getNumSamples() && r.getNumChannels() == 2, "read it back: same length, rate, channels");
    float err = 0;
    for (int i = 0; i < r.getNumSamples(); ++i) err = juce::jmax (err, std::abs (r.getSample (0, i) - b.getSample (0, i)));
    check (err < 1e-5f, "24-bit round trip error is tiny", juce::String (err, 8));

    std::vector<tk::NoteEvent> notes { { 0, 1, 50, 100, 1 }, { 1, 0.5, 53, 90, 1 } };
    auto mid = dir.getChildFile ("t.mid");
    check (tk::writeMidiFile (mid, { { "Bass", notes } }, 120), "write MIDI file");
    juce::MidiFile mf; juce::FileInputStream in (mid);
    check (mf.readFrom (in) && mf.getNumTracks() >= 1, "MIDI file reads back");
    int ons = 0;
    for (int t = 0; t < mf.getNumTracks(); ++t) for (auto* e : *mf.getTrack (t)) if (e->message.isNoteOn()) ++ons;
    check (ons == 2, "both notes are in it", juce::String (ons));

    std::vector<tk::ZoneSample> zones { { "Pad_C2_v110.wav", 36, 0, 37, 1, 127 }, { "Pad_D#2_v110.wav", 39, 38, 127, 1, 127 } };
    auto xpm = dir.getChildFile ("k.xpm");
    check (tk::writeXpmKeygroup (xpm, "Pad & <Test>", zones), "write keygroup .xpm");
    auto xml = juce::XmlDocument::parse (xpm);
    check (xml != nullptr && xml->hasTagName ("MPCVObject"), "keygroup .xpm is valid XML");
    if (xml != nullptr)
    {
        auto* prog = xml->getChildByName ("Program");
        check (prog != nullptr && prog->getStringAttribute ("type") == "Keygroup", "program type is Keygroup");
        auto* inst = prog != nullptr ? prog->getChildByName ("Instruments") : nullptr;
        check (inst != nullptr && inst->getNumChildElements() == 2, "two keygroups");
        if (inst != nullptr && inst->getNumChildElements() == 2)
        {
            auto* i1 = inst->getChildElement (0);
            auto* l1 = i1->getChildByName ("Layers")->getChildElement (0);
            check (i1->getChildElementAllSubText ("LowNote", {}) == "0" && i1->getChildElementAllSubText ("HighNote", {}) == "37", "keygroup 1 covers notes 0-37");
            check (l1->getChildElementAllSubText ("RootNote", {}) == "37", "RootNote is MIDI note + 1 (MPC quirk)", l1->getChildElementAllSubText ("RootNote", {}));
            check (l1->getChildElementAllSubText ("SampleName", {}) == "Pad_C2_v110", "SampleName has no extension");
        }
        check (prog != nullptr && prog->getChildElementAllSubText ("ProgramName", {}) == "Pad & <Test>", "program name survives XML escaping");
    }
    auto drum = dir.getChildFile ("d.xpm");
    check (tk::writeXpmDrum (drum, "Chop", { "a.wav", "b.wav", "c.wav" }), "write drum .xpm");
    auto dx = juce::XmlDocument::parse (drum);
    check (dx != nullptr && dx->getChildByName ("Program")->getChildByName ("PadNoteMap") != nullptr, "drum .xpm has a pad note map");
    auto sfz = dir.getChildFile ("k.sfz");
    check (tk::writeSfz (sfz, zones) && sfz.loadFileAsString().contains ("pitch_keycenter=36"), "SFZ has the root notes");

    juce::MemoryBlock raw;
    const juce::uint8 bytes[] = { 0x00, 0xF0, 0x41, 0x10, 0xF7, 0x90, 0x40, 0xF0, 0x7E, 0x01, 0x02, 0xF7, 0xF0, 0x33 };
    raw.append (bytes, sizeof (bytes));
    auto msgs = tk::splitSysex (raw);
    check (msgs.size() == 2 && msgs[0].getSize() == 4 && msgs[1].getSize() == 5, "SysEx splitter finds the 2 complete messages and drops junk", juce::String ((int) msgs.size()));
}

void testSampler (const juce::File& dir)
{
    tk::SamplerPlan plan;
    plan.name = "Test Pad"; plan.lo = 36; plan.hi = 48; plan.step = 6; plan.velocities = { 64, 127 };
    plan.outDir = dir.getChildFile ("sampler");
    std::vector<tk::SampledNote> takes;
    for (int n : plan.notes())
        for (int v : plan.velocities)
        {
            juce::AudioBuffer<float> a (2, 48000); a.clear();
            for (int i = 2400; i < 48000; ++i) { float s = (v / 127.0f) * 0.3f * (float) std::sin (2 * juce::MathConstants<double>::pi * tk::hzFromMidi (n) * i / 48000); a.setSample (0, i, s); a.setSample (1, i, s); }
            takes.push_back ({ n, v, a, 48000 });
        }
    auto res = tk::finishSampling (takes, plan);
    check (res.error.isEmpty(), "auto-sampler finishes", res.error);
    check (res.files.size() == 6, "6 WAVs for 3 notes x 2 velocities", juce::String (res.files.size()));
    check (res.xpm.existsAsFile() && res.sfz.existsAsFile(), "writes .xpm and .sfz");
    juce::AudioBuffer<float> lo, hi; double sr;
    bool both = res.files.size() == 6 && tk::readAudio (juce::File (res.files[0]), lo, sr) && tk::readAudio (juce::File (res.files[1]), hi, sr);
    if (both)
    {
        double ratio = tk::peakOf (hi) / juce::jmax (1e-6f, tk::peakOf (lo));
        check (std::abs (ratio - 127.0 / 64.0) < 0.05, "one common gain keeps the velocity dynamics", juce::String (ratio, 3));
        check (lo.getNumSamples() < 48000 - 2000, "leading silence trimmed", juce::String (lo.getNumSamples()));
    }
}

void testWavetable (const juce::File& dir)
{
    std::vector<tk::wt::Frame> keys { tk::wt::shape (0), tk::wt::shape (2) };
    auto frames = tk::wt::render (keys, 64);
    check (frames.size() == 64 && frames[0].size() == (size_t) tk::wt::N, "wavetable renders 64 frames of 2048");
    auto mf = tk::wt::wav (frames, 512, 24);
    check (mf.getSize() == 44 + 64 * 512 * 3, "MiniFreak export: 64 x 512 points, 24-bit, mono", juce::String ((int) mf.getSize()));
    auto f = dir.getChildFile ("mf.wav"); f.replaceWithData (mf.getData(), mf.getSize());
    juce::AudioBuffer<float> b; double sr;
    check (tk::readAudio (f, b, sr) && b.getNumChannels() == 1 && b.getNumSamples() == 64 * 512, "the MiniFreak WAV reads as a normal mono WAV");
    std::vector<float> s ((size_t) b.getNumSamples());
    for (int i = 0; i < b.getNumSamples(); ++i) s[(size_t) i] = b.getSample (0, i);
    auto back = tk::wt::framesFromSamples (s);
    check (back.size() == 64, "imported back as 64 frames", juce::String ((int) back.size()));
    auto rec = sine (110, 48000, 1.0, 0.5f);
    double hz = 0;
    auto cyc = tk::wt::framesFromRecording (rec, 48000, 8, &hz);
    check (cyc.size() == 8 && std::abs (hz - 110) < 1, "pulls 8 cycles from a 110 Hz recording", juce::String (hz, 2) + " Hz");
}

void testMangleTheory()
{
    auto b = sine (220, 48000, 1.0, 0.5f, 2);
    tk::mangle::StretchOptions so; so.factor = 4; so.windowSeconds = 0.2;
    auto st = tk::mangle::stretch (b, 48000, so);
    check (std::abs (st.getNumSamples() - 4 * 48000) < 48000 * 0.3, "stretch x4 is about 4 s long", juce::String (st.getNumSamples() / 48000.0, 2) + " s");
    juce::AudioBuffer<float> mid (1, 8192);
    for (int i = 0; i < 8192; ++i) mid.setSample (0, i, st.getSample (0, 60000 + i));
    double p = tk::yinPitch (mid.getReadPointer (0), 8192, 48000);
    check (std::abs (1200 * std::log2 (p / 220)) < 15, "stretch keeps the pitch", juce::String (p, 1) + " Hz");
    tk::mangle::RotOptions ro;
    auto rot = tk::mangle::tapeRot (b, 48000, ro);
    bool finite = true;
    for (int i = 0; i < rot.getNumSamples(); ++i) finite = finite && std::isfinite (rot.getSample (0, i));
    check (finite && rot.getNumSamples() == b.getNumSamples() && tk::peakOf (rot) > 0.05f, "tape rot output is sane");

    tk::theory::RiffOptions o; o.bars = 4; o.seed = 11;
    auto r = tk::theory::makeRiff (o);
    check (r.chordNames.size() == 4 && ! r.bass.empty() && ! r.lead.empty(), "riff has 4 chords, bass and lead");
    auto& sc = tk::theory::scales()[(size_t) o.scale];
    bool inScale = true;
    for (auto* lane : { &r.chords, &r.bass, &r.lead })
        for (auto& n : *lane)
        {
            int pc = ((n.note - o.root) % 12 + 12) % 12;
            inScale = inScale && std::find (sc.steps.begin(), sc.steps.end(), pc) != sc.steps.end();
        }
    check (inScale, "every riff note is in " + juce::String (sc.name));
    bool noOverlap = true;
    for (size_t i = 1; i < r.bass.size(); ++i) noOverlap = noOverlap && r.bass[i - 1].beat + r.bass[i - 1].lengthBeats <= r.bass[i].beat + 1e-9;
    check (noOverlap, "bass notes never overlap (no legato glide)");
}

void testLink()
{
    tk::link::Packet p; p.name = "Hydra bus"; p.sampleRate = 44100; p.channels = 2; p.frames = 3;
    p.interleaved = { 0.1f, -0.1f, 0.2f, -0.2f, 0.3f, -0.3f };
    tk::link::Packet q;
    check (tk::link::Packet::decode (p.encode(), q) && q.name == "Hydra bus" && q.sampleRate == 44100 && q.frames == 3 && q.interleaved == p.interleaved, "Link packet encodes and decodes");
    juce::MemoryBlock junk ("nope", 4);
    check (! tk::link::Packet::decode (junk, q), "Link rejects junk packets");

    // live: a server and a client on localhost
    std::atomic<int> got { 0 };
    juce::String name;
    juce::CriticalSection nl;
    tk::link::Server server ([&] (int, const tk::link::Packet& pk) { got += pk.frames; const juce::ScopedLock sl (nl); name = pk.name; }, [] (int) {});
    if (! server.start()) { check (false, "Link server starts on port 53317"); return; }
    tk::link::Client client;
    client.setName ("Lead bus");
    client.prepare (48000, 2);
    auto t0 = juce::Time::getMillisecondCounter();
    while (! client.isLinked() && juce::Time::getMillisecondCounter() - t0 < 5000) juce::Thread::sleep (20);
    check (client.isLinked(), "plugin client connects to the app");
    std::vector<float> l (512, 0.25f), r (512, -0.25f);
    for (int k = 0; k < 40; ++k) { client.push (l.data(), r.data(), 512); juce::Thread::sleep (3); }
    t0 = juce::Time::getMillisecondCounter();
    while (got.load() < 40 * 512 && juce::Time::getMillisecondCounter() - t0 < 3000) juce::Thread::sleep (20);
    const juce::ScopedLock sl (nl);
    check (got.load() == 40 * 512 && name == "Lead bus", "audio streams across intact, named after the track", juce::String (got.load()) + " frames from " + name);
    server.stop();
}
}

/** Windows CI: JUCE's own ASIO host (the same code Toolkit uses) opens "BC Split" on a test-mode host. */
struct SplitCheck : juce::AudioIODeviceCallback
{
    std::atomic<int> blocks { 0 };
    std::atomic<float> peak0 { 0 }, mean4 { 0 };
    void audioDeviceIOCallbackWithContext (const float* const* in, int nin, float* const* out, int nout, int n, const juce::AudioIODeviceCallbackContext&) override
    {
        if (nin > 4)
        {
            float pk = 0, sum = 0;
            for (int i = 0; i < n; ++i) { pk = juce::jmax (pk, std::abs (in[0][i])); sum += in[4][i]; }
            peak0 = pk; mean4 = sum / n;
        }
        for (int c = 0; c < nout; ++c) if (out[c]) juce::FloatVectorOperations::fill (out[c], c == 0 ? 0.3f : 0.0f, n);
        ++blocks;
    }
    void audioDeviceAboutToStart (juce::AudioIODevice*) override {}
    void audioDeviceStopped() override {}
};

static int splitCheck()
{
#if JUCE_WINDOWS
    std::unique_ptr<juce::AudioIODeviceType> asio (juce::AudioIODeviceType::createAudioIODeviceType_ASIO());
    asio->scanForDevices();
    auto names = asio->getDeviceNames();
    check (names.contains ("BC Split"), "JUCE's ASIO host lists BC Split", names.joinIntoString (", "));
    std::unique_ptr<juce::AudioIODevice> dev (asio->createDevice ("BC Split", "BC Split"));
    if (dev == nullptr) { check (false, "JUCE creates the Split device"); return 1; }
    juce::BigInteger ins, outs; ins.setRange (0, 8, true); outs.setRange (0, 8, true);
    auto err = dev->open (ins, outs, 48000, 256);
    check (err.isEmpty(), "JUCE opens Split at 48k / 256", err);
    SplitCheck cb;
    dev->start (&cb);
    juce::Thread::sleep (1500);
    dev->stop();
    check (cb.blocks > 200, "callbacks arrive through JUCE", juce::String (cb.blocks.load()));
    check (std::abs (cb.peak0 - 0.5f) < 0.02f, "JUCE hears the host's input 1", juce::String (cb.peak0.load()));
    check (std::abs (cb.mean4 - 0.3f) < 0.01f, "what JUCE plays comes back through Split's mixer", juce::String (cb.mean4.load()));
    check (dev->getOutputLatencyInSamples() >= 512, "JUCE sees the extra buffers in the output latency", juce::String (dev->getOutputLatencyInSamples()));
    dev->close();
#else
    check (true, "Split check only runs on Windows");
#endif
    std::printf ("\n%d passed, %d failed\n", passes, failures);
    return failures == 0 ? 0 : 1;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc > 1 && juce::String (argv[1]) == "--split-check") return splitCheck();
    juce::String out = argc > 1 ? argv[1] : "shots";
    auto outDir = juce::File::getCurrentWorkingDirectory().getChildFile (out);
    auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("toolkit-tests");
    tmp.deleteRecursively(); tmp.createDirectory();

    testPitch(); testLoudness(); testHum(); testOnsetsTempoKey();
    testFiles (tmp); testSampler (tmp); testWavetable (tmp); testMangleTheory(); testLink();
    std::printf ("\n%d passed, %d failed\n\n", passes, failures);

    if (argc > 2 && juce::String (argv[2]) == "--no-shots") return failures == 0 ? 0 : 1;
    tk::Look look;
    juce::LookAndFeel::setDefaultLookAndFeel (&look);
    {
        tk::Engine engine (false);
        engine.addDemoSources();
        tk::MainComponent mc (engine);
        mc.renderAll (outDir, 1600, 1000);
    }
    {
        // the VST3's own window, as Ableton would show it, fed a low D so the tuner has something to say
        LinkProcessor proc;
        proc.setPlayConfigDetails (2, 2, 48000, 512);
        proc.prepareToPlay (48000, 512);
        proc.setCustomName ("Hydrasynth");
        juce::AudioBuffer<float> b (2, 512); juce::MidiBuffer m;
        for (int k = 0; k < 20; ++k)
        {
            for (int i = 0; i < 512; ++i) { float v = 0.4f * (float) std::sin (2 * juce::MathConstants<double>::pi * 73.42 * (k * 512 + i) / 48000); b.setSample (0, i, v); b.setSample (1, i, v); }
            proc.processBlock (b, m);
        }
        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
        for (int t = 0; t < 4; ++t) { juce::MessageManager::getInstance()->runDispatchLoopUntil (40); }
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
        outDir.createDirectory();
        juce::FileOutputStream os (outDir.getChildFile ("14-link-plugin.png"));
        if (os.openedOk()) { os.setPosition (0); os.truncate(); juce::PNGImageFormat().writeImageToStream (img, os); }
    }
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    return failures == 0 ? 0 : 1;
}
