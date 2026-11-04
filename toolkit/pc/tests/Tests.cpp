#include "App/MainWindow.h"
#include "App/SamplerCore.h"
#include "App/ChopCore.h"
#include "Common/Dsp.h"
#include "Common/Wavetable.h"
#include "Common/Mangle.h"
#include "Common/Theory.h"
#include "Common/Link.h"
#include "Plugin/LinkEditor.h"
#include "Session.h"

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

// ------------------------------------------------------------ chop station + playback voices

juce::AudioBuffer<float> drumHit (int kind, double sr, double seconds, unsigned seed = 5)
{
    juce::AudioBuffer<float> b (2, (int) (sr * seconds)); b.clear();
    juce::Random rnd ((juce::int64) seed);
    float lp = 0;
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        double t = i / sr, v;
        float n = rnd.nextFloat() * 2 - 1;
        switch (kind)
        {
            case 0: v = 0.9 * std::sin (2 * juce::MathConstants<double>::pi * (45 + 180 * std::exp (-t * 35)) * t) * std::exp (-t * 9); break;            // kick
            case 1: lp += 0.35f * (n - lp); v = 0.5 * lp * std::exp (-t * 16) + 0.45 * std::sin (2 * juce::MathConstants<double>::pi * 185 * t) * std::exp (-t * 22); break;   // snare
            case 2: { float h = n - lp; lp += 0.6f * (n - lp); v = 0.4 * h * std::exp (-t * 55); break; }                                                            // hat
            default: v = 0.3 * (2 * std::fmod (t * 220, 1.0) - 1) * (t < seconds * 0.9 ? 1 : 0);                                                                  // a held saw note
        }
        b.setSample (0, i, (float) v); b.setSample (1, i, (float) v);
    }
    int fade = (int) (0.02 * sr);
    b.applyGainRamp (b.getNumSamples() - fade, fade, 1, 0);   // no click at the end
    return b;
}

void testChop()
{
    using namespace tk::chop;
    const double sr = 48000;
    auto t = tidy ({ 900, -5, 300, 300, 310, 5000000, 1200 }, 48000, 32);
    check (t == std::vector<int> ({ 0, 300, 900, 1200, 47998 }), "markers are tidied: sorted, inside the sample, no near-duplicates", juce::String ((int) t.size()));
    auto r = range ({ 0, 100, 300 }, 2, 1000);
    check (r.first == 300 && r.second == 1000 && range ({ 0, 100 }, 7, 1000).second == 0, "slice ranges, and a bad index gives an empty slice");
    auto eq = equal (48000, 16);
    check (eq.size() == 16 && eq[1] == 3000 && eq[15] == 45000, "16 equal slices", juce::String ((int) eq.size()));
    check (equal (48000, 500).size() == (size_t) kMaxSlices, "never more than 64 slices");
    auto gr = grid (4 * 48000, sr, 120, 1, 1000);
    check (gr.size() == 8 && gr[0] == 1000 && gr[1] == 25000, "beat grid at 120 BPM: a marker every 24000 samples from the first hit", juce::String ((int) gr.size()));
    auto sn = sine (100, sr, 0.1, 0.5f);
    int z = nearestZeroCrossing (sn, 400, 400);
    check ((z == 480 || z == 481) && sn.getSample (0, z - 1) <= 0 && sn.getSample (0, z) > 0, "snaps to the next upward zero crossing", juce::String (z));

    check (classify (drumHit (0, sr, 0.4), sr, 0, 19200) == Hit::kick, "a kick reads as a kick", hitName (classify (drumHit (0, sr, 0.4), sr, 0, 19200)));
    check (classify (drumHit (1, sr, 0.3), sr, 0, 14400) == Hit::snare, "a snare reads as a snare", hitName (classify (drumHit (1, sr, 0.3), sr, 0, 14400)));
    check (classify (drumHit (2, sr, 0.15), sr, 0, 7200) == Hit::hat, "a hat reads as a hat", hitName (classify (drumHit (2, sr, 0.15), sr, 0, 7200)));
    check (classify (drumHit (3, sr, 0.6), sr, 0, 28800) == Hit::tone, "a held note reads as a tone", hitName (classify (drumHit (3, sr, 0.6), sr, 0, 28800)));

    auto src = drumHit (3, sr, 0.5);
    SliceFx down; down.semitones = -12;
    auto d = render (src, sr, 0, 24000, down, false, false);
    check (std::abs (d.getNumSamples() - 48000) <= 2, "tuned down an octave = twice as long, like an MPC pad", juce::String (d.getNumSamples()));
    SliceFx rev; rev.reverse = true;
    auto rv = render (src, sr, 1000, 2000, rev, false, false);
    check (rv.getNumSamples() == 1000 && rv.getSample (0, 0) == src.getSample (0, 1999) && rv.getSample (0, 999) == src.getSample (0, 1000), "reverse plays the slice backwards");
    SliceFx quiet; quiet.gainDb = -6;
    auto q = render (src, sr, 0, 4800, quiet, true, false);
    check (std::abs (tk::peakOf (q) - 0.95f * 0.501f) < 0.01f, "normalise then level: -6 dB below 0.95", juce::String (tk::peakOf (q), 3));
    auto f = render (src, sr, 0, 4800, {}, false, true);
    check (f.getSample (0, 0) == 0 && std::abs (f.getSample (0, 4799)) < 0.003f, "tiny fades take the click off both ends");
    check (render (src, sr, 500, 500, {}, false, false).getNumSamples() == 0, "an empty slice renders as nothing instead of crashing");

    // a two-bar break: kick, hat, snare, hat ... at 90 BPM
    const double bpm = 90, spb = 60.0 / bpm * sr / 2;
    juce::AudioBuffer<float> brk (2, (int) (spb * 16)); brk.clear();
    std::vector<int> marks;
    for (int k = 0; k < 16; ++k)
    {
        int kind = k % 4 == 0 ? 0 : (k % 4 == 2 ? 1 : 2);
        auto h = drumHit (kind, sr, 0.25, (unsigned) k + 1);
        int at = (int) (k * spb);
        for (int c = 0; c < 2; ++c) brk.addFrom (c, at, h, c, 0, juce::jmin (h.getNumSamples(), brk.getNumSamples() - at));
        marks.push_back (at);
    }
    std::vector<Hit> types;
    for (int i = 0; i < 16; ++i) { auto rr = range (marks, i, brk.getNumSamples()); types.push_back (classify (brk, sr, rr.first, rr.second)); }
    int right = 0;
    for (int i = 0; i < 16; ++i) right += types[(size_t) i] == (i % 4 == 0 ? Hit::kick : (i % 4 == 2 ? Hit::snare : Hit::hat)) ? 1 : 0;
    check (right >= 14, "hit types in a break: kicks, snares and hats", juce::String (right) + " of 16");
    auto hits = tk::detectOnsets (brk, sr, { 0.55, 60, 64 });
    bool near = hits.size() == 16;
    for (size_t i = 0; near && i < hits.size(); ++i) near = std::abs (hits[i] - marks[i]) < 600;
    check (near, "each one within 12 ms of the hit, kicks included");
    check (hits.size() == 16, "finds all 16 hits in the break", juce::String ((int) hits.size()));

    bool allOk = true;
    for (int style = 0; style < flipStyles().size(); ++style)
        for (int bars : { 1, 2, 4 })
        {
            auto steps = makeFlip (style, marks, brk.getNumSamples(), sr, bpm, types, bars, 7);
            bool ok = ! steps.empty();
            for (auto& s : steps) ok = ok && s.slice >= 0 && s.slice < 16 && s.beat >= 0 && s.beat < bars * 4 && s.length > 0;
            auto audio = renderFlip (steps, brk, sr, marks, std::vector<SliceFx> (16), bpm, bars, true);
            ok = ok && audio.getNumSamples() == (int) std::lround (bars * 4 * 60.0 / bpm * sr) && tk::peakOf (audio) > 0.05f && tk::peakOf (audio) <= 0.99f;
            if (! ok) { allOk = false; check (false, "flip " + flipStyles()[style] + ", " + juce::String (bars) + " bars"); }
        }
    check (allOk, "all 6 flip styles at 1, 2 and 4 bars: hits on the grid, loop-length audio, no clipping");
    auto bb = makeFlip (1, marks, brk.getNumSamples(), sr, bpm, types, 1, 3);
    bool kickOnOne = false, snareOn2 = false, snareOn4 = false;
    for (auto& s : bb)
    {
        if (s.beat == 0 && types[(size_t) s.slice] == Hit::kick) kickOnOne = true;
        if (s.beat == 1 && types[(size_t) s.slice] == Hit::snare) snareOn2 = true;
        if (s.beat == 3 && types[(size_t) s.slice] == Hit::snare) snareOn4 = true;
    }
    check (kickOnOne && snareOn2 && snareOn4, "boom bap flip: a kick pad on the one, snare pads on 2 and 4");
    check (makeFlip (2, marks, brk.getNumSamples(), sr, bpm, types, 2, 1).size() != makeFlip (2, marks, brk.getNumSamples(), sr, bpm, types, 2, 2).size()
           || makeFlip (2, marks, brk.getNumSamples(), sr, bpm, types, 2, 1)[3].slice != makeFlip (2, marks, brk.getNumSamples(), sr, bpm, types, 2, 2)[3].slice,
           "New flip gives a different pattern");
    check (makeFlip (1, {}, 0, sr, bpm, {}, 2, 1).empty(), "a flip of nothing is empty, not a crash");
}

void testVoices()
{
    tk::Engine e (false);
    const int n = 48000;
    auto ramp = std::make_shared<juce::AudioBuffer<float>> (2, n);
    for (int i = 0; i < n; ++i) { ramp->setSample (0, i, i / (float) n); ramp->setSample (1, i, -i / (float) n); }
    std::vector<float> L (512), R (512);
    auto block = [&] { std::fill (L.begin(), L.end(), 0.0f); std::fill (R.begin(), R.end(), 0.0f); e.renderVoices (L.data(), R.data(), 512, 48000); };

    tk::PlayOptions o; o.start = 1000; o.length = 2000; o.fadeInMs = 0;
    e.play (ramp, 48000, o);
    check (ramp.use_count() == 2, "playing shares the audio, it doesn't copy it", juce::String ((int) ramp.use_count()));
    block();
    check (std::abs (L[10] - 1010 / 48000.0f) < 1e-5f && std::abs (R[10] + 1010 / 48000.0f) < 1e-5f, "a voice plays its slice, both channels");
    for (int k = 0; k < 4; ++k) block();
    check (! e.isPlaying() && L[511] == 0, "and stops at the slice end");

    // a 10-minute stereo file: starting it must be instant (the old engine copied it on every pad hit)
    auto big = std::make_shared<juce::AudioBuffer<float>> (2, 48000 * 600);
    big->clear();
    auto t0 = juce::Time::getMillisecondCounterHiRes();
    for (int k = 0; k < 64; ++k) { tk::PlayOptions p; p.start = k * 1000; p.length = 1000; e.play (big, 48000, p); }
    double ms = juce::Time::getMillisecondCounterHiRes() - t0;
    check (ms < 20, "64 pad hits on a 10-minute sample take no time", juce::String (ms, 2) + " ms");

    tk::PlayOptions a; a.choke = false; a.fadeInMs = 0;
    e.play (ramp, 48000, a); e.play (ramp, 48000, a);
    block();
    check (e.voicesPlaying() >= 2, "poly: two hits ring together", juce::String (e.voicesPlaying()));
    tk::PlayOptions m; m.fadeInMs = 0;
    e.play (ramp, 48000, m);
    for (int k = 0; k < 2; ++k) block();
    check (e.voicesPlaying() == 1, "mono: a new hit cuts the others (with a fade, not a click)", juce::String (e.voicesPlaying()));

    e.stopPlayback();
    block();
    float tail = 0; for (int i = 300; i < 512; ++i) tail = juce::jmax (tail, std::abs (L[(size_t) i]));
    check (! e.isPlaying() && tail == 0, "Stop fades out within a few milliseconds");
    float jump = 0; for (int i = 1; i < 300; ++i) jump = juce::jmax (jump, std::abs (L[(size_t) i] - L[(size_t) i - 1]));
    check (jump < 0.01f, "and the stop doesn't click", juce::String (jump, 4));

    tk::PlayOptions rv; rv.reverse = true; rv.start = 2000; rv.length = 1000; rv.fadeInMs = 0;
    e.play (ramp, 48000, rv);
    block();
    check (std::abs (L[0] - 2999 / 48000.0f) < 1e-5f && L[10] < L[0], "reverse plays backwards from the slice end");
    tk::PlayOptions up; up.semitones = 12; up.length = 4800; up.fadeInMs = 0;
    e.play (ramp, 48000, up);
    int len = 0; for (int k = 0; k < 10 && e.isPlaying(); ++k) { block(); len += 512; }
    check (len <= 2560 && len >= 2048, "tuned up an octave finishes in half the time", juce::String (len));
    tk::PlayOptions lp; lp.loop = true; lp.length = 1000;
    e.play (ramp, 48000, lp);
    for (int k = 0; k < 10; ++k) block();
    check (e.isPlaying(), "loop keeps going past the slice end");
    e.stopPlayback(); block();
    juce::AudioBuffer<float> legacy (1, 1000); legacy.clear();
    e.play (legacy, 44100);
    check (e.isPlaying() && e.playPosition() == 0, "the old copy-and-play call still works");
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

void collect (juce::Component* c, juce::Array<juce::Component*>& out)
{
    for (auto* ch : c->getChildren()) { out.add (ch); collect (ch, out); }
}

/** Bash on the chop station like an impatient producer: random keys, buttons, combos and sliders, new audio mid-way. */
void testChopFuzz()
{
    tk::Engine e (false);
    tk::MainComponent mc (e);
    mc.setSize (1600, 1000);
    mc.show ("chop");
    auto* page = mc.current;
    page->demo();
    juce::Array<juce::Component*> kids;
    collect (page, kids);
    juce::Random rnd (42);
    std::vector<float> L (256), R (256);
    const juce::String keys = "zxcvasdfqwer1234t";
    const int special[] = { juce::KeyPress::spaceKey, juce::KeyPress::escapeKey, juce::KeyPress::deleteKey, juce::KeyPress::leftKey,
                            juce::KeyPress::rightKey, juce::KeyPress::returnKey };
    int actions = 0;
    for (int step = 0; step < 3000; ++step)
    {
        int what = rnd.nextInt (10);
        if (what < 4)
        {
            if (rnd.nextBool()) { auto ch = keys[rnd.nextInt (keys.length())]; page->keyPressed (juce::KeyPress ((int) ch, {}, ch)); }
            else page->keyPressed (juce::KeyPress (special[rnd.nextInt (6)]));
            if (rnd.nextInt (20) == 0) page->keyPressed (juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 'z'));
        }
        else if (what < 8)
        {
            auto* c = kids[rnd.nextInt (kids.size())];
            if (auto* b = dynamic_cast<juce::Button*> (c))
            {
                auto t = b->getButtonText();
                if (t.contains ("Open") || t.contains ("Export")) continue;   // file dialogs / writing to the Music folder
                if (b->onClick) b->onClick();
            }
            else if (auto* cb = dynamic_cast<juce::ComboBox*> (c)) { if (cb->getNumItems() > 0) cb->setSelectedItemIndex (rnd.nextInt (cb->getNumItems()), juce::sendNotificationSync); }
            else if (auto* sl = dynamic_cast<juce::Slider*> (c))
            {
                if (sl->onDragStart) sl->onDragStart();
                sl->setValue (sl->getMinimum() + rnd.nextDouble() * (sl->getMaximum() - sl->getMinimum()), juce::sendNotificationSync);
                if (sl->onDragEnd) sl->onDragEnd();
            }
            else if (rnd.nextInt (8) == 0)   // click somewhere on the waveform, the pads, anything
            {
                auto pt = juce::Point<float> ((float) rnd.nextInt (juce::jmax (1, c->getWidth())), (float) rnd.nextInt (juce::jmax (1, c->getHeight())));
                auto now = juce::Time::getCurrentTime();
                juce::MouseEvent me (juce::Desktop::getInstance().getMainMouseSource(), pt, rnd.nextInt (6) == 0 ? juce::ModifierKeys::altModifier : juce::ModifierKeys(),
                                     0, 0, 0, 0, 0, c, c, now, pt, now, 1, false);
                c->mouseDown (me); c->mouseDrag (me.withNewPosition (pt.translated ((float) rnd.nextInt (200) - 100, 0))); c->mouseUp (me);
                if (rnd.nextInt (5) == 0) c->mouseDoubleClick (me);
            }
        }
        else if (what == 8 && rnd.nextInt (60) == 0)
        {
            juce::AudioBuffer<float> b (1 + rnd.nextInt (2), 2000 + rnd.nextInt (200000));
            for (int ch = 0; ch < b.getNumChannels(); ++ch) for (int i = 0; i < b.getNumSamples(); ++i) b.setSample (ch, i, (rnd.nextFloat() * 2 - 1) * (i % 9000 < 400 ? 0.8f : 0.02f));
            mc.sendAudio ("chop", b, rnd.nextBool() ? 44100 : 48000, "fuzz");
        }
        std::fill (L.begin(), L.end(), 0.0f); std::fill (R.begin(), R.end(), 0.0f);
        e.renderVoices (L.data(), R.data(), 256, 48000);
        for (float v : L) if (! std::isfinite (v)) { check (false, "chop fuzz: output went NaN"); return; }
        page->tick();
        if (step % 50 == 0) juce::MessageManager::getInstance()->runDispatchLoopUntil (1);   // let background analysis land
        ++actions;
    }
    check (true, "3000 random chop station actions without a crash", juce::String (actions));
}

void testVideo (const juce::File& dir)
{
    // the Video page uses the BC Visualizer's own styles and MP4 writer: make a short clip through them from inside the Toolkit
    auto styles = bcv::makeAll();
    check (styles.size() >= 80, "the Video page has all 80 of the visualizer's styles", juce::String ((int) styles.size()));
    bcv::Track t; bcv::setDemoTrack (t, 2.0);
    bcv::Look look; look.style = 5; look.title = "Toolkit video test";
    auto out = dir.getChildFile ("toolkit-video.mp4");
    bcv::ExportJob job (t, look, out, 640, 360, 30);
    bool ok = job.runNow();
    check (ok && out.getSize() > 10000, "exports an MP4 from inside the Toolkit", ok ? juce::String (out.getSize()) + " bytes" : job.error);

    tk::Engine e (false);
    tk::MainComponent mc (e);
    mc.setSize (1600, 1000);
    check (mc.page ("video") != nullptr, "the Video page is in the Toolkit");
    juce::AudioBuffer<float> take (1, 44100); take.clear();
    for (int i = 0; i < take.getNumSamples(); ++i) take.setSample (0, i, 0.3f * (float) std::sin (i * 0.05));
    mc.sendAudio ("video", take, 44100, "Recorded take");
    check (mc.current != nullptr && mc.current->id == "video", "a take sent from Record opens the Video page");
    for (int k = 0; k < 5; ++k) mc.current->tick();
    mc.sendAudio ("chop", take, 44100, "Recorded take");
    check (mc.current->id == "chop", "and the chop station still takes audio the same way");
    for (int k = 0; k < 5; ++k) mc.current->tick();
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
    testFiles (tmp); testSampler (tmp); testWavetable (tmp); testMangleTheory(); testLink(); testChop(); testVoices(); testVideo (tmp); testChopFuzz();
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
        juce::FileOutputStream os (outDir.getChildFile ("15-link-plugin.png"));
        if (os.openedOk()) { os.setPosition (0); os.truncate(); juce::PNGImageFormat().writeImageToStream (img, os); }
    }
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    return failures == 0 ? 0 : 1;
}
