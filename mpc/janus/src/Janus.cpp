// Janus.cpp - an AN1x-style virtual analogue for the MPC, by binaryimage.
//
// SPDX-License-Identifier: MIT
//
// Janus is not a Yamaha emulation: it is an original synth built around the ideas that made
// the AN1x special - two complete "Scenes" per patch that you morph between with the mod wheel,
// a sync/FM/ring-mod oscillator pair with feedback from the VCA back into the mixer, a multimode
// filter followed by a separate non-resonant high-pass, and a built-in effects chain.
//
// Signal flow, per voice (10 voices):
//
//   Osc 2 (master) ──┬───────────────────────────────┐
//                    │ FM (linear)        hard sync  │
//                    ▼                       │       ▼
//   Osc 1 (slave) <──┘ <─────────────────────┘     mixer: O1 + O2 + ring (O1*O2) + noise + feedback
//                                                     │
//                    multimode ladder (LPF24/18/12, BPF, HPF, drive)
//                                                     │
//                    12 dB high-pass (no resonance)
//                                                     │
//                    VCA (amp EG * velocity * LFO1 tremolo) ──► feedback back into the mixer
//                                                     │
//   all voices ─► panned stereo sum ─► distortion ─► chorus/flanger ─► ping-pong delay ─► reverb ─► volume
//
// Scenes: every sound-shaping parameter exists twice (S1 and S2). Morph (knob, plus the mod wheel
// when "Wheel Morph" is on) blends them: continuous values glide between the two scenes (cutoff and
// times in the log domain so the sweep sounds even), switches flip at the half-way point.
//
// Common parameters (voice mode, LFO rates, effects) are shared by both scenes.

#include "mpc_plugin.h"
#include "dsp/math.h"
#include "dsp/adsr.h"
#include "dsp/lfo.h"
#include "dsp/smoother.h"
#include "dsp/noise.h"
#include "dsp/tempo.h"
#include "dsp/midi_cc.h"
#include "dsp/voice.h"
#include "janus_dsp.h"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

using namespace mpc::dsp;
using janus::clampf;
using janus::fastExp2;

// ---------------------------------------------------------------------------
// Parameter layout. The order is permanent once projects exist: append only.
// ---------------------------------------------------------------------------
enum Common
{
    kVolume = 0,      // param 0: harmless on purpose (the host nudges it on load)
    kMorph, kWheelMorph, kVoiceMode, kUniVoices, kUniDetune, kSpread, kGlide, kBendRange,
    kVelAmp, kVelFilt, kATFilt,
    kLfo1Wave, kLfo1Rate, kLfo1Sync, kLfo1Div, kLfo2Wave, kLfo2Rate,
    kNumCommon
};

// One scene's parameters. Scene 1 starts at kScene1, Scene 2 at kScene2.
enum SceneParam
{
    sO1Wave = 0, sO1PW, sO1Pitch, sO1Level,
    sO2Wave, sO2PW, sO2Semi, sO2Fine, sO2Level,
    sSync, sFM, sRing, sNoise, sFeedback,
    sFType, sCutoff, sReso, sFEnv, sKeyTrk, sFDrive, sHPF,
    sFA, sFD, sFS, sFR,
    sAA, sAD, sAS, sAR,
    sPegAmt, sPegDec, sPegO1,
    sL1Pitch, sL1Filt, sL1PWM, sL1Amp, sL2Pitch, sL2Filt,
    kSceneSize
};
static constexpr int kScene1 = kNumCommon;
static constexpr int kScene2 = kScene1 + kSceneSize;

enum Fx
{
    kDistDrive = kScene2 + kSceneSize, kDistTone, kDistMix,
    kModType, kModRate, kModDepth, kModMix,
    kDlyDiv, kDlyFdbk, kDlyTone, kDlyMix,
    kRevSize, kRevDamp, kRevMix,
    kCopy12, kCopy21,
    kNumParams
};

// How each scene parameter morphs.
enum class MorphKind { Linear, Log, Discrete };

class Janus : public mpc::Plugin
{
public:
    static mpc::PluginInfo makeInfo()
    {
        mpc::PluginInfo i;
        i.name = "Janus";
        i.vendor = "binaryimage";
        i.uniqueId = MPC_FOURCC('B', '8', '2', 'r');   // must match "uid" in plugin.json
        i.version = 1000;
        i.isInstrument = true;
        i.numInputs = 0;
        i.numOutputs = 2;
        return i;
    }

    Janus() : mpc::Plugin(makeInfo())
    {
        using namespace mpc;
        const std::vector<std::string> waves = {"Saw", "Pulse", "Tri", "Sine"};
        const std::vector<std::string> lfoWaves = LFO::shapeNames();   // Sine, Triangle, Saw, Square, S&H

        // ----- common ---------------------------------------------------------
        addParam(floatParam("vol",      "Volume", -40.0f, 6.0f, -6.0f, "dB", 1));
        addParam(floatParam("morph",    "Morph", 0.0f, 100.0f, 0.0f, "%", 0));
        addParam(boolParam ("wheelmorph","Wheel Morph", true));
        addParam(choiceParam("vmode",   "Voice Mode", {"Poly", "Mono", "Unison"}, 0));
        addParam(intParam  ("univoices","Uni Voices", 2, 6, 4));
        addParam(floatParam("unidet",   "Uni Detune", 0.0f, 50.0f, 12.0f, "ct", 0));
        addParam(floatParam("spread",   "Spread", 0.0f, 100.0f, 50.0f, "%", 0));
        addParam(floatParam("glide",    "Glide", 0.0f, 2000.0f, 0.0f, "ms", 0, 0.4f));
        addParam(intParam  ("bend",     "Bend Range", 0, 24, 2, "st"));
        addParam(floatParam("velamp",   "Vel>Amp", 0.0f, 100.0f, 50.0f, "%", 0));
        addParam(floatParam("velfilt",  "Vel>Filter", 0.0f, 100.0f, 30.0f, "%", 0));
        addParam(floatParam("atfilt",   "AT>Filter", -100.0f, 100.0f, 0.0f, "%", 0));
        addParam(choiceParam("l1wave",  "LFO1 Wave", lfoWaves, 1));
        addParam(logParam  ("l1rate",   "LFO1 Rate", 0.05f, 30.0f, 4.0f, "Hz", 2));
        addParam(boolParam ("l1sync",   "LFO1 Sync", false));
        addParam(choiceParam("l1div",   "LFO1 Div", divisionNames(), kDivisionQuarter));
        addParam(choiceParam("l2wave",  "LFO2 Wave", lfoWaves, 0));
        addParam(logParam  ("l2rate",   "LFO2 Rate", 0.05f, 30.0f, 0.3f, "Hz", 2));

        // ----- two scenes -----------------------------------------------------
        for (int s = 0; s < 2; ++s) addScene(s, waves);

        // ----- effects --------------------------------------------------------
        addParam(floatParam("distdrive","Dist Drive", 0.0f, 100.0f, 30.0f, "%", 0));
        addParam(logParam  ("disttone", "Dist Tone", 400.0f, 16000.0f, 6000.0f, "Hz", 0));
        addParam(floatParam("distmix",  "Dist Mix", 0.0f, 100.0f, 0.0f, "%", 0));
        addParam(choiceParam("modtype", "Mod Type", {"Chorus", "Flanger"}, 0));
        addParam(logParam  ("modrate",  "Mod Rate", 0.05f, 8.0f, 0.6f, "Hz", 2));
        addParam(floatParam("moddepth", "Mod Depth", 0.0f, 100.0f, 50.0f, "%", 0));
        addParam(floatParam("modmix",   "Mod Mix", 0.0f, 100.0f, 0.0f, "%", 0));
        addParam(choiceParam("dlydiv",  "Delay Time", divisionNames(), 9));
        addParam(floatParam("dlyfb",    "Delay Fdbk", 0.0f, 95.0f, 35.0f, "%", 0));
        addParam(logParam  ("dlytone",  "Delay Tone", 400.0f, 16000.0f, 3500.0f, "Hz", 0));
        addParam(floatParam("dlymix",   "Delay Mix", 0.0f, 100.0f, 0.0f, "%", 0));
        addParam(floatParam("revsize",  "Rev Size", 0.0f, 100.0f, 60.0f, "%", 0));
        addParam(floatParam("revdamp",  "Rev Damp", 0.0f, 100.0f, 50.0f, "%", 0));
        addParam(floatParam("revmix",   "Rev Mix", 0.0f, 100.0f, 0.0f, "%", 0));
        addParam(boolParam ("copy12",   "Copy S1>S2", false));
        addParam(boolParam ("copy21",   "Copy S2>S1", false));

        if (numParams() != kNumParams) mpc::log("Janus: parameter count mismatch (%d vs %d)", numParams(), (int)kNumParams);

        addPrograms();

        worker_ = std::thread([this] { workerLoop(); });
    }

    ~Janus() override
    {
        { std::lock_guard<std::mutex> lk(mtx_); quit_ = true; }
        cv_.notify_all();
        if (worker_.joinable()) worker_.join();
    }

    // ----- lifecycle --------------------------------------------------------
    void prepare(double sampleRate, int /*maxBlockSize*/) override
    {
        sr_ = (float)sampleRate;
        for (auto& v : voices_)
        {
            v.filter.setSampleRate(sr_);
            v.fenv.setSampleRate(sampleRate); v.aenv.setSampleRate(sampleRate);
        }
        lfo1_.setSampleRate(sampleRate); lfo2_.setSampleRate(sampleRate);
        controllers_.prepare(sampleRate, 15.0f);
        morph_.setTime(20.0f, sampleRate);
        volume_.setTime(10.0f, sampleRate);
        chorus_.prepare(sr_); delay_.prepare(sr_); reverb_.prepare(sr_);
        dlySmooth_.setTime(80.0f, sampleRate);
        reset();
    }

    void reset() override
    {
        for (auto& v : voices_) v.kill();
        alloc_.reset();
        numHeld_ = 0; monoActive_ = false;
        lfo1_.reset(); lfo2_.reset();
        controllers_.reset();
        chorus_.reset(); delay_.reset(); reverb_.reset();
        distLp_[0] = distLp_[1] = 0.0f;
        volume_.snap(dbToGain(get(kVolume)));
        morph_.snap(morphTarget());
        lastMode_ = getInt(kVoiceMode);
    }

    // Copy buttons are momentary: the worker thread does the copy, then drops the button.
    void parameterChanged(int index, float plain) override
    {
        if ((index == kCopy12 || index == kCopy21) && plain >= 0.5f)
        {
            { std::lock_guard<std::mutex> lk(mtx_); pendingCopy_ = (index == kCopy12) ? 1 : 2; }
            cv_.notify_all();
        }
    }

    bool formatValue(int index, float plain, std::string& out) override
    {
        char buf[32];
        if (index == kVoiceMode || index == kCopy12 || index == kCopy21) return false;
        const int si = sceneIndex(index);
        if (si == sO1Pitch || si == sPegAmt) { std::snprintf(buf, sizeof buf, "%+.1f", (double)plain); out = buf; return true; }
        if (si == sCutoff || si == sHPF || index == kDistTone || index == kDlyTone)
        {
            if (plain >= 1000.0f) std::snprintf(buf, sizeof buf, "%.1fk", (double)(plain * 0.001f));
            else std::snprintf(buf, sizeof buf, "%.0f", (double)plain);
            out = buf; return true;
        }
        return false;
    }

    // ----- audio ------------------------------------------------------------
    void process(mpc::ProcessContext& ctx) override
    {
        updateBlock(ctx);

        int pos = 0;
        for (int e = 0; e < ctx.numMidiEvents; ++e)
        {
            const mpc::MidiEvent& ev = ctx.midi[e];
            if (ev.sampleOffset > pos) { render(ctx, pos, ev.sampleOffset); pos = ev.sampleOffset; }
            handleMidi(ev);
        }
        render(ctx, pos, ctx.numFrames);

        float* L = ctx.outputs[0];
        float* R = ctx.numOutputs > 1 ? ctx.outputs[1] : scratchR_;
        const int n = ctx.numFrames;

        // ----- effects chain --------------------------------------------------
        if (fx_.distMix > 0.001f)
        {
            const float g = 1.0f + fx_.distDrive * fx_.distDrive * 40.0f;
            const float a = 1.0f - std::exp(-6.2831853f * fx_.distTone / sr_);
            const float makeup = 1.0f / (1.0f + fx_.distDrive * 1.5f);
            for (int i = 0; i < n; ++i)
            {
                float* ch[2] = {L, R};
                for (int c = 0; c < 2; ++c)
                {
                    const float x = ch[c][i];
                    // Asymmetric clip gives the even harmonics an overdriven amp has.
                    float y = fastTanh(x * g + 0.15f * fx_.distDrive) - fastTanh(0.15f * fx_.distDrive);
                    distLp_[c] += (y - distLp_[c]) * a;
                    ch[c][i] = x + (distLp_[c] * makeup * 1.4f - x) * fx_.distMix;
                }
            }
        }
        if (fx_.modMix > 0.001f) chorus_.process(L, R, n, fx_.modType, fx_.modRate, fx_.modDepth, fx_.modMix);
        {
            float target = beatsToSamples(divisionBeats(fx_.dlyDiv), ctx.transport.bpm, sr_);
            target = clampf(target, 16.0f, sr_ * 2.5f);
            dlySmooth_.setTarget(target);
            for (int i = 0; i < n; ++i) dlySmooth_.process();   // glide the time block-wise
            delay_.process(L, R, n, dlySmooth_.value(), fx_.dlyFdbk, fx_.dlyMix, fx_.dlyTone);
        }
        reverb_.process(L, R, n, fx_.revSize, fx_.revDamp, fx_.revMix);

        for (int i = 0; i < n; ++i)
        {
            const float g = volume_.process();
            L[i] = sanitize(L[i] * g);
            R[i] = sanitize(R[i] * g);
        }
    }

private:
    static constexpr int kVoices = 10;
    static constexpr int kChunk = 64;
    static constexpr int kCtrl = 16;     // control-rate interval (samples)
    static constexpr int kMaxHeld = 16;

    // ----- parameter helpers ------------------------------------------------
    int sceneIndex(int index) const
    {
        if (index >= kScene1 && index < kScene2) return index - kScene1;
        if (index >= kScene2 && index < kScene2 + kSceneSize) return index - kScene2;
        return -1;
    }

    void addScene(int s, const std::vector<std::string>& waves)
    {
        using namespace mpc;
        char id[32], nm[32];
        auto I = [&](const char* base) { std::snprintf(id, sizeof id, "s%d_%s", s + 1, base); return id; };
        auto N = [&](const char* base) { std::snprintf(nm, sizeof nm, "S%d %s", s + 1, base); return nm; };
        const std::vector<std::string> ftypes = {"LPF24", "LPF18", "LPF12", "BPF", "HPF"};

        addParam(choiceParam(I("o1wave"), N("O1 Wave"), waves, 0));                       kind_[s][sO1Wave] = MorphKind::Discrete;
        addParam(floatParam(I("o1pw"),    N("O1 PW"), 5.0f, 95.0f, 50.0f, "%", 0));       kind_[s][sO1PW] = MorphKind::Linear;
        addParam(floatParam(I("o1pitch"), N("O1 Pitch"), -24.0f, 48.0f, 0.0f, "st", 1));  kind_[s][sO1Pitch] = MorphKind::Linear;
        addParam(floatParam(I("o1lvl"),   N("O1 Level"), 0.0f, 100.0f, 100.0f, "%", 0));  kind_[s][sO1Level] = MorphKind::Linear;
        addParam(choiceParam(I("o2wave"), N("O2 Wave"), waves, 0));                       kind_[s][sO2Wave] = MorphKind::Discrete;
        addParam(floatParam(I("o2pw"),    N("O2 PW"), 5.0f, 95.0f, 50.0f, "%", 0));       kind_[s][sO2PW] = MorphKind::Linear;
        addParam(intParam  (I("o2semi"),  N("O2 Semi"), -24, 24, 0, "st"));               kind_[s][sO2Semi] = MorphKind::Linear;
        addParam(floatParam(I("o2fine"),  N("O2 Fine"), -50.0f, 50.0f, 7.0f, "ct", 0));   kind_[s][sO2Fine] = MorphKind::Linear;
        addParam(floatParam(I("o2lvl"),   N("O2 Level"), 0.0f, 100.0f, 80.0f, "%", 0));   kind_[s][sO2Level] = MorphKind::Linear;
        addParam(boolParam (I("sync"),    N("Sync"), false));                             kind_[s][sSync] = MorphKind::Discrete;
        addParam(floatParam(I("fm"),      N("FM"), 0.0f, 100.0f, 0.0f, "%", 0));          kind_[s][sFM] = MorphKind::Linear;
        addParam(floatParam(I("ring"),    N("Ring"), 0.0f, 100.0f, 0.0f, "%", 0));        kind_[s][sRing] = MorphKind::Linear;
        addParam(floatParam(I("noise"),   N("Noise"), 0.0f, 100.0f, 0.0f, "%", 0));       kind_[s][sNoise] = MorphKind::Linear;
        addParam(floatParam(I("fdbk"),    N("Feedback"), 0.0f, 100.0f, 0.0f, "%", 0));    kind_[s][sFeedback] = MorphKind::Linear;
        addParam(choiceParam(I("ftype"),  N("F Type"), ftypes, 0));                       kind_[s][sFType] = MorphKind::Discrete;
        addParam(logParam  (I("cutoff"),  N("Cutoff"), 20.0f, 18000.0f, 2500.0f, "Hz", 0)); kind_[s][sCutoff] = MorphKind::Log;
        addParam(floatParam(I("reso"),    N("Reso"), 0.0f, 100.0f, 20.0f, "%", 0));       kind_[s][sReso] = MorphKind::Linear;
        addParam(floatParam(I("fenv"),    N("F Env Amt"), -100.0f, 100.0f, 40.0f, "%", 0)); kind_[s][sFEnv] = MorphKind::Linear;
        addParam(floatParam(I("keytrk"),  N("Key Track"), 0.0f, 100.0f, 50.0f, "%", 0));  kind_[s][sKeyTrk] = MorphKind::Linear;
        addParam(floatParam(I("fdrive"),  N("F Drive"), 0.0f, 100.0f, 20.0f, "%", 0));    kind_[s][sFDrive] = MorphKind::Linear;
        addParam(logParam  (I("hpf"),     N("HPF"), 10.0f, 4000.0f, 10.0f, "Hz", 0));     kind_[s][sHPF] = MorphKind::Log;
        addParam(logParam  (I("fa"),      N("F Attack"), 1.0f, 10000.0f, 2.0f, "ms", 0));   kind_[s][sFA] = MorphKind::Log;
        addParam(logParam  (I("fd"),      N("F Decay"), 1.0f, 10000.0f, 400.0f, "ms", 0));  kind_[s][sFD] = MorphKind::Log;
        addParam(floatParam(I("fs"),      N("F Sustain"), 0.0f, 100.0f, 30.0f, "%", 0));    kind_[s][sFS] = MorphKind::Linear;
        addParam(logParam  (I("fr"),      N("F Release"), 1.0f, 10000.0f, 300.0f, "ms", 0)); kind_[s][sFR] = MorphKind::Log;
        addParam(logParam  (I("aa"),      N("A Attack"), 1.0f, 10000.0f, 2.0f, "ms", 0));   kind_[s][sAA] = MorphKind::Log;
        addParam(logParam  (I("ad"),      N("A Decay"), 1.0f, 10000.0f, 300.0f, "ms", 0));  kind_[s][sAD] = MorphKind::Log;
        addParam(floatParam(I("as"),      N("A Sustain"), 0.0f, 100.0f, 85.0f, "%", 0));    kind_[s][sAS] = MorphKind::Linear;
        addParam(logParam  (I("ar"),      N("A Release"), 1.0f, 10000.0f, 250.0f, "ms", 0)); kind_[s][sAR] = MorphKind::Log;
        addParam(floatParam(I("pegamt"),  N("PEG Amt"), -48.0f, 48.0f, 0.0f, "st", 1));   kind_[s][sPegAmt] = MorphKind::Linear;
        addParam(logParam  (I("pegdec"),  N("PEG Decay"), 1.0f, 5000.0f, 80.0f, "ms", 0)); kind_[s][sPegDec] = MorphKind::Log;
        addParam(boolParam (I("pego1"),   N("PEG O1 Only"), false));                      kind_[s][sPegO1] = MorphKind::Discrete;
        addParam(floatParam(I("l1pitch"), N("L1>Pitch"), 0.0f, 12.0f, 0.0f, "st", 2, 0.4f)); kind_[s][sL1Pitch] = MorphKind::Linear;
        addParam(floatParam(I("l1filt"),  N("L1>Filter"), -100.0f, 100.0f, 0.0f, "%", 0)); kind_[s][sL1Filt] = MorphKind::Linear;
        addParam(floatParam(I("l1pwm"),   N("L1>PWM"), 0.0f, 100.0f, 0.0f, "%", 0));      kind_[s][sL1PWM] = MorphKind::Linear;
        addParam(floatParam(I("l1amp"),   N("L1>Amp"), 0.0f, 100.0f, 0.0f, "%", 0));      kind_[s][sL1Amp] = MorphKind::Linear;
        addParam(floatParam(I("l2pitch"), N("L2>Pitch"), 0.0f, 12.0f, 0.0f, "st", 2, 0.4f)); kind_[s][sL2Pitch] = MorphKind::Linear;
        addParam(floatParam(I("l2filt"),  N("L2>Filter"), -100.0f, 100.0f, 0.0f, "%", 0)); kind_[s][sL2Filt] = MorphKind::Linear;
    }

    // ----- presets ------------------------------------------------------------
    // Dark and heavy on purpose. Each one is built from the defaults plus overrides.
    using Ov = std::initializer_list<std::pair<int, float>>;
    void addPrograms()
    {
        auto program = [this](const char* name, Ov common, Ov s1, Ov s2, bool s2FromS1)
        {
            std::vector<float> v((size_t)numParams());
            for (int i = 0; i < numParams(); ++i) v[(size_t)i] = param(i).def;
            for (const auto& o : common) v[(size_t)o.first] = o.second;
            for (const auto& o : s1) v[(size_t)(kScene1 + o.first)] = o.second;
            if (s2FromS1) for (int j = 0; j < kSceneSize; ++j) v[(size_t)(kScene2 + j)] = v[(size_t)(kScene1 + j)];
            for (const auto& o : s2) v[(size_t)(kScene2 + o.first)] = o.second;
            addProgram(name, std::move(v));
        };

        program("Init", {}, {}, {}, true);

        // Mono bass: S1 a fat closed saw/pulse, S2 the same note through sync growl. Wheel = snarl.
        program("Morph Grind Bass",
            {{kVoiceMode, 1}, {kDistDrive, 35}, {kDistMix, 35}, {kDistTone, 4500}},
            {{sO1Wave, 0}, {sO2Wave, 1}, {sO2PW, 35}, {sO2Semi, -12}, {sO2Fine, 4}, {sO2Level, 85},
             {sFType, 0}, {sCutoff, 160}, {sReso, 30}, {sFEnv, 55}, {sFDrive, 55}, {sKeyTrk, 40},
             {sFA, 1}, {sFD, 220}, {sFS, 10}, {sFR, 120}, {sAA, 1}, {sAD, 400}, {sAS, 90}, {sAR, 90}, {sHPF, 28}},
            {{sSync, 1}, {sO1Pitch, 17}, {sCutoff, 420}, {sReso, 45}, {sFType, 1}, {sFDrive, 80}, {sFEnv, 45}, {sFeedback, 25}},
            true);

        // Sync lead with a pitch-EG sweep on osc 1. S2 pushes the sync further and adds ring mod.
        program("Sync Wire",
            {{kVoiceMode, 1}, {kDlyMix, 22}, {kDlyFdbk, 40}, {kRevMix, 18}, {kDistDrive, 25}, {kDistMix, 25}},
            {{sSync, 1}, {sO1Pitch, 7}, {sO1Wave, 0}, {sO2Wave, 0}, {sO2Level, 0}, {sPegAmt, 24}, {sPegDec, 260}, {sPegO1, 1},
             {sFType, 2}, {sCutoff, 3200}, {sReso, 25}, {sFEnv, 20}, {sAA, 3}, {sAD, 500}, {sAS, 80}, {sAR, 200}},
            {{sO1Pitch, 31}, {sRing, 35}, {sCutoff, 1800}, {sReso, 50}, {sFType, 3}},
            true);

        // Poly pad: S1 dark detuned saws, S2 hollow ring-mod/band-pass air. Slow wheel morph.
        program("Cinder Pad",
            {{kVoiceMode, 0}, {kSpread, 80}, {kModType, 0}, {kModMix, 45}, {kModDepth, 60}, {kRevMix, 40}, {kRevSize, 85}, {kLfo2Rate, 0.12f}},
            {{sO1Wave, 0}, {sO2Wave, 0}, {sO2Fine, 11}, {sFType, 2}, {sCutoff, 650}, {sReso, 15}, {sFEnv, 25},
             {sFA, 1800}, {sFD, 3000}, {sFS, 40}, {sFR, 2500}, {sAA, 1500}, {sAD, 2000}, {sAS, 90}, {sAR, 3000}, {sL2Filt, 25}},
            {{sO2Semi, 7}, {sRing, 40}, {sFType, 3}, {sCutoff, 1400}, {sReso, 45}, {sHPF, 180}, {sNoise, 12}, {sL1PWM, 30}, {sO1Wave, 1}},
            true);

        // Six stacked voices, everything driven. S2 opens it up into a wall.
        program("Furnace Unison",
            {{kVoiceMode, 2}, {kUniVoices, 6}, {kUniDetune, 22}, {kSpread, 90}, {kDistDrive, 45}, {kDistMix, 40}},
            {{sO1Wave, 0}, {sO2Wave, 0}, {sO2Semi, -12}, {sFType, 0}, {sCutoff, 700}, {sReso, 20}, {sFDrive, 70},
             {sFEnv, 35}, {sFD, 600}, {sFS, 40}, {sAD, 600}, {sAS, 95}, {sAR, 180}, {sHPF, 40}},
            {{sCutoff, 2600}, {sReso, 30}, {sFeedback, 35}},
            true);

        // Metallic hits for industrial percussion: FM + ring through a band-pass. Short and nasty.
        program("Ring Clank",
            {{kVoiceMode, 0}, {kRevMix, 20}, {kRevSize, 40}, {kVelFilt, 60}},
            {{sO1Wave, 3}, {sO2Wave, 1}, {sO2Semi, 7}, {sO2Fine, 31}, {sFM, 55}, {sRing, 70}, {sO1Level, 40}, {sO2Level, 20},
             {sFType, 3}, {sCutoff, 1900}, {sReso, 55}, {sFEnv, 30}, {sFD, 120}, {sFS, 0}, {sAA, 1}, {sAD, 260}, {sAS, 0}, {sAR, 220},
             {sPegAmt, -12}, {sPegDec, 40}, {sNoise, 10}},
            {{sO2Semi, 13}, {sFM, 85}, {sCutoff, 900}, {sAD, 700}},
            true);

        // VCA feedback screaming through a resonant 18 dB filter, slowly breathing. A drone, hold a note.
        program("Feedback Drone",
            {{kVoiceMode, 1}, {kLfo1Rate, 0.09f}, {kRevMix, 35}, {kRevSize, 90}, {kDlyMix, 15}},
            {{sO1Wave, 0}, {sO2Wave, 1}, {sO2Semi, -5}, {sFeedback, 80}, {sNoise, 18}, {sFType, 1}, {sCutoff, 380}, {sReso, 62},
             {sFDrive, 60}, {sFEnv, 10}, {sAA, 900}, {sAS, 100}, {sAR, 3000}, {sL1Filt, 35}, {sL2Pitch, 0.08f}},
            {{sFType, 4}, {sCutoff, 900}, {sReso, 70}, {sFeedback, 95}},
            true);

        // Felt-not-heard sub: triangle/sine, tiny click from the pitch EG. S2 adds grit.
        program("Sub Pressure",
            {{kVoiceMode, 1}},
            {{sO1Wave, 2}, {sO2Wave, 3}, {sO2Semi, -12}, {sO2Level, 70}, {sFType, 0}, {sCutoff, 220}, {sReso, 5}, {sFEnv, 25},
             {sFD, 140}, {sFS, 0}, {sAA, 1}, {sAD, 500}, {sAS, 85}, {sAR, 80}, {sPegAmt, 12}, {sPegDec, 18}},
            {{sO1Wave, 0}, {sCutoff, 480}, {sFDrive, 75}, {sReso, 25}},
            true);
    }

    // ----- copy worker (never on the audio thread) ---------------------------
    void workerLoop()
    {
        std::unique_lock<std::mutex> lk(mtx_);
        while (true)
        {
            cv_.wait(lk, [this] { return quit_ || pendingCopy_ != 0; });
            if (quit_) return;
            const int which = pendingCopy_; pendingCopy_ = 0;
            lk.unlock();
            std::this_thread::sleep_for(std::chrono::milliseconds(30));   // let the button-down settle
            const int from = which == 1 ? kScene1 : kScene2, to = which == 1 ? kScene2 : kScene1;
            for (int j = 0; j < kSceneSize; ++j)
            {
                setNormalised(to + j, getNormalised(from + j));
                notifyHostParameterChanged(to + j);
            }
            const int btn = which == 1 ? kCopy12 : kCopy21;
            setNormalised(btn, 0.0f);
            notifyHostParameterChanged(btn);
            requestDisplayUpdate();
            lk.lock();
        }
    }

    // ----- per block ------------------------------------------------------------
    float morphTarget() const
    {
        float m = get(kMorph) * 0.01f;
        if (getBool(kWheelMorph)) m += controllers_.modWheel();
        return clampf(m, 0.0f, 1.0f);
    }

    struct Scene { float v[kSceneSize]; };
    Scene cur_{};

    void computeScene(float m)
    {
        for (int j = 0; j < kSceneSize; ++j)
        {
            const float a = get(kScene1 + j), b = get(kScene2 + j);
            switch (kind_[0][j])
            {
                case MorphKind::Discrete: cur_.v[j] = m < 0.5f ? a : b; break;
                case MorphKind::Log:
                {
                    const float la = std::log(a > 0.001f ? a : 0.001f), lb = std::log(b > 0.001f ? b : 0.001f);
                    cur_.v[j] = std::exp(la + (lb - la) * m);
                    break;
                }
                default: cur_.v[j] = a + (b - a) * m; break;
            }
        }
    }

    struct FxSettings
    {
        float distDrive, distTone, distMix, modRate, modDepth, modMix, dlyFdbk, dlyTone, dlyMix, revSize, revDamp, revMix;
        int modType, dlyDiv;
    } fx_{};

    void updateBlock(mpc::ProcessContext& ctx)
    {
        // Voice-mode change: release everything so nothing hangs.
        const int mode = getInt(kVoiceMode);
        if (mode != lastMode_)
        {
            for (auto& v : voices_) v.release();
            alloc_.reset(); numHeld_ = 0; monoActive_ = false;
            lastMode_ = mode;
        }

        alloc_.setBendRange((float)getInt(kBendRange));
        volume_.setTarget(dbToGain(get(kVolume)));
        morph_.setTarget(morphTarget());

        lfo1_.setShape(getInt(kLfo1Wave));
        if (getBool(kLfo1Sync))
        {
            lfo1_.setTempoSync(ctx.transport.bpm, divisionBeats(getInt(kLfo1Div)));
            if (ctx.transport.playing && ctx.transport.ppqValid) lfo1_.syncToPpq(ctx.transport.ppqPos);
        }
        else lfo1_.setFrequency(get(kLfo1Rate));
        lfo2_.setShape(getInt(kLfo2Wave));
        lfo2_.setFrequency(get(kLfo2Rate));

        glideMs_ = get(kGlide);
        velAmp_ = get(kVelAmp) * 0.01f; velFilt_ = get(kVelFilt) * 0.01f; atFilt_ = get(kATFilt) * 0.01f;
        uniVoices_ = getInt(kUniVoices); uniDetune_ = get(kUniDetune); spread_ = get(kSpread) * 0.01f;

        fx_.distDrive = get(kDistDrive) * 0.01f; fx_.distTone = get(kDistTone); fx_.distMix = get(kDistMix) * 0.01f;
        fx_.modType = getInt(kModType); fx_.modRate = get(kModRate); fx_.modDepth = get(kModDepth) * 0.01f; fx_.modMix = get(kModMix) * 0.01f;
        fx_.dlyDiv = getInt(kDlyDiv); fx_.dlyFdbk = get(kDlyFdbk) * 0.01f; fx_.dlyTone = get(kDlyTone); fx_.dlyMix = get(kDlyMix) * 0.01f;
        fx_.revSize = get(kRevSize) * 0.01f; fx_.revDamp = get(kRevDamp) * 0.01f; fx_.revMix = get(kRevMix) * 0.01f;
    }

    // ----- voices -------------------------------------------------------------
    struct Voice
    {
        janus::Osc o1, o2;
        janus::Ladder filter;
        janus::HP12 hp;
        janus::PitchEG peg;
        ADSR fenv, aenv;
        Xorshift32 rng{0x1234567u};
        float note = 60.0f, target = 60.0f, glideCoef = 1.0f;
        float vel = 1.0f, detuneCt = 0.0f, panL = 0.707f, panR = 0.707f;
        float fb = 0.0f;
        int ctrl = 0;
        float inc1 = 0.0f, inc2 = 0.0f, pw1 = 0.5f, pw2 = 0.5f;
        bool active = false;

        void kill() { active = false; fenv.reset(); aenv.reset(); filter.reset(); hp.reset(); peg.reset(); fb = 0.0f; }
        void release() { if (active) { fenv.noteOff(); aenv.noteOff(); } }
    };

    void startVoice(Voice& v, int note, int velocity, float detuneCt, float pan, bool retrigger, bool slideFromLast)
    {
        const bool wasActive = v.active && v.aenv.isActive();
        v.target = (float)note;
        if (glideMs_ > 0.5f && slideFromLast && lastPlayed_ >= 0) { if (!wasActive) v.note = (float)lastPlayed_; }
        else v.note = (float)note;
        const float glideSamples = glideMs_ * 0.001f * sr_;
        v.glideCoef = glideSamples > 1.0f ? 1.0f - std::exp(-1.0f / (glideSamples / 4.6f)) : 1.0f;
        v.vel = velocity / 127.0f;
        v.detuneCt = detuneCt;
        equalPowerPan(clampf(pan, -1.0f, 1.0f), v.panL, v.panR);
        if (!wasActive)
        {
            v.o1.reset(0.0f); v.o2.reset(0.37f); v.filter.reset(); v.hp.reset(); v.fb = 0.0f;
        }
        if (retrigger || !wasActive) { v.fenv.noteOn(); v.aenv.noteOn(); v.peg.trigger(); }
        v.active = true;
        v.ctrl = 0;
    }

    void handleMidi(const mpc::MidiEvent& ev)
    {
        const int mode = lastMode_;
        if (ev.isNoteOn())
        {
            const int note = ev.note(), vel = ev.velocity();
            if (mode == 0)
            {
                bool stolen = false;
                const int vi = alloc_.noteOn(note, vel, &stolen);
                // Alternate voices left/right by the spread amount.
                const float pan = ((vi & 1) ? 1.0f : -1.0f) * spread_ * 0.6f;
                startVoice(voices_[vi], note, vel, 0.0f, pan, true, true);
            }
            else
            {
                pushHeld(note, vel);
                playMono(note, vel, true);
            }
            lastPlayed_ = note;
        }
        else if (ev.isNoteOff())
        {
            if (mode == 0)
            {
                const int vi = alloc_.noteOff(ev.note());
                if (vi >= 0) voices_[vi].release();
            }
            else
            {
                removeHeld(ev.note());
                if (sustainDown_) return;
                if (numHeld_ > 0) playMono(held_[numHeld_ - 1].note, held_[numHeld_ - 1].vel, false);   // legato back
                else releaseMono();
            }
        }
        else if (ev.isAllNotesOff())
        {
            alloc_.allNotesOff([this](int vi) { voices_[vi].release(); });
            numHeld_ = 0; releaseMono();
        }
        else if (ev.isController() && ev.controller() == cc::Sustain)
        {
            controllers_.handle(ev);
            sustainDown_ = ev.controllerValue() >= 64;
            if (mode == 0) alloc_.setSustain(sustainDown_, [this](int vi) { voices_[vi].release(); });
            else if (!sustainDown_ && numHeld_ == 0) releaseMono();
        }
        else if (ev.isPitchBend()) alloc_.setPitchBend(ev.pitchBend());
        else controllers_.handle(ev);
    }

    // Mono / unison: voices 0..n-1 all play the newest held note.
    void playMono(int note, int vel, bool retrigger)
    {
        const int n = lastMode_ == 2 ? uniVoices_ : 1;
        for (int k = 0; k < n; ++k)
        {
            float det = 0.0f, pan = 0.0f;
            if (n > 1)
            {
                const float t = (float)k / (float)(n - 1) * 2.0f - 1.0f;   // -1..1 across the stack
                det = t * uniDetune_;
                pan = t * spread_;
            }
            startVoice(voices_[k], note, vel, det, pan, retrigger, true);
        }
        for (int k = n; k < kVoices; ++k) voices_[k].release();
        monoActive_ = true;
    }
    void releaseMono() { for (auto& v : voices_) v.release(); monoActive_ = false; }

    struct Held { int note, vel; };
    void pushHeld(int note, int vel)
    {
        removeHeld(note);
        if (numHeld_ == kMaxHeld) { for (int i = 1; i < kMaxHeld; ++i) held_[i - 1] = held_[i]; --numHeld_; }
        held_[numHeld_++] = {note, vel};
    }
    void removeHeld(int note)
    {
        for (int i = 0; i < numHeld_; ++i)
            if (held_[i].note == note) { for (int j = i + 1; j < numHeld_; ++j) held_[j - 1] = held_[j]; --numHeld_; return; }
    }

    void render(mpc::ProcessContext& ctx, int from, int to)
    {
        float* L = ctx.outputs[0];
        float* R = ctx.numOutputs > 1 ? ctx.outputs[1] : scratchR_;
        while (from < to)
        {
            const int n = (to - from) < kChunk ? (to - from) : kChunk;
            for (int i = 0; i < n; ++i)
            {
                controllers_.tick();
                lfo1Buf_[i] = lfo1_.process();
                lfo2Buf_[i] = lfo2_.process();
            }
            morph_.setTarget(morphTarget());
            for (int i = 0; i < n; ++i) morph_.process();
            computeScene(morph_.value());
            // Envelopes follow the morphed scene.
            const Scene& s = cur_;
            for (auto& v : voices_)
            {
                if (!v.active) continue;
                v.fenv.set(s.v[sFA], s.v[sFD], s.v[sFS] * 0.01f, s.v[sFR]);
                v.aenv.set(s.v[sAA], s.v[sAD], s.v[sAS] * 0.01f, s.v[sAR]);
                v.peg.set(s.v[sPegDec], sr_);
                v.hp.set(s.v[sHPF], sr_);
                v.o1.setWave((int)s.v[sO1Wave]); v.o2.setWave((int)s.v[sO2Wave]);
            }
            for (int vi = 0; vi < kVoices; ++vi)
            {
                Voice& v = voices_[vi];
                if (!v.active) continue;
                renderVoice(v, L + from, R + from, n);
                if (!v.aenv.isActive()) { v.kill(); if (lastMode_ == 0) alloc_.voiceFinished(vi); }
            }
            from += n;
        }
    }

    void renderVoice(Voice& v, float* L, float* R, int n)
    {
        const Scene& s = cur_;
        const int ftype = (int)s.v[sFType];
        const bool sync = s.v[sSync] >= 0.5f, pegO1 = s.v[sPegO1] >= 0.5f;
        const float l1 = s.v[sO1Level] * 0.01f, l2 = s.v[sO2Level] * 0.01f;
        const float ring = s.v[sRing] * 0.01f, noise = s.v[sNoise] * 0.01f;
        const float fm = s.v[sFM] * 0.01f;
        const float fbk = s.v[sFeedback] * 0.01f * 1.6f;
        const float drive = 1.0f + s.v[sFDrive] * 0.01f * 6.0f;
        const float tremDepth = s.v[sL1Amp] * 0.01f;
        const float velGain = 1.0f - velAmp_ + velAmp_ * v.vel;
        const float voiceGain = lastMode_ == 2 ? 0.9f / std::sqrt((float)uniVoices_) : 0.6f;
        // Keep the mixer from slamming the filter when everything is up.
        const float mixNorm = 0.6f / (0.4f + l1 + l2 + ring + noise * 0.5f);

        for (int i = 0; i < n; ++i)
        {
            const float fe = v.fenv.process();
            const float ae = v.aenv.process();
            const float pe = v.peg.process();

            if ((v.ctrl++ % kCtrl) == 0)
            {
                // Glide (one-pole in note space), advanced kCtrl samples at a time.
                if (v.glideCoef >= 1.0f) v.note = v.target;
                else { const float c = 1.0f - std::pow(1.0f - v.glideCoef, (float)kCtrl); v.note += (v.target - v.note) * c; }

                const float lfo1 = lfo1Buf_[i], lfo2 = lfo2Buf_[i];
                float vib = s.v[sL1Pitch] * lfo1 + s.v[sL2Pitch] * lfo2;
                if (!getBool(kWheelMorph)) vib += 0.5f * controllers_.modWheel() * lfo1;   // wheel = vibrato when not morphing
                const float base = v.note + alloc_.bendSemitones() + vib + v.detuneCt * 0.01f;
                const float peg = s.v[sPegAmt] * pe;
                const float n2 = base + s.v[sO2Semi] + s.v[sO2Fine] * 0.01f + (pegO1 ? 0.0f : peg);
                const float n1 = base + s.v[sO1Pitch] + peg;
                v.inc2 = 440.0f * fastExp2((n2 - 69.0f) / 12.0f) / sr_;
                v.inc1 = 440.0f * fastExp2((n1 - 69.0f) / 12.0f) / sr_;

                const float pwm = s.v[sL1PWM] * 0.01f * 0.45f * lfo1;
                v.o1.setPulseWidth(s.v[sO1PW] * 0.01f + pwm);
                v.o2.setPulseWidth(s.v[sO2PW] * 0.01f - pwm);

                const float oct = s.v[sFEnv] * 0.01f * fe * 7.0f
                                + s.v[sKeyTrk] * 0.01f * (v.note - 60.0f) / 12.0f
                                + s.v[sL1Filt] * 0.01f * lfo1 * 3.0f
                                + s.v[sL2Filt] * 0.01f * lfo2 * 3.0f
                                + velFilt_ * (v.vel - 0.5f) * 3.0f
                                + atFilt_ * controllers_.aftertouch() * 3.0f;
                v.filter.set(s.v[sCutoff] * fastExp2(clampf(oct, -10.0f, 10.0f)), s.v[sReso] * 0.01f);
            }

            bool w2 = false; float f2 = 0.0f;
            const float x2 = v.o2.process(v.inc2, w2, f2);
            const float inc1 = v.inc1 * (1.0f + fm * 3.0f * x2);
            float x1;
            if (sync) x1 = v.o1.processSynced(inc1, w2, f2);
            else { bool w1; float f1; x1 = v.o1.process(inc1, w1, f1); }

            const float nz = v.rng.nextBipolar();
            float mix = (x1 * l1 + x2 * l2 + x1 * x2 * ring + nz * noise * 0.7f) * mixNorm + v.fb * fbk;
            float y = v.filter.process(mix, ftype, drive);
            y = v.hp.process(y);
            const float trem = 1.0f - tremDepth * 0.5f * (1.0f - lfo1Buf_[i]);
            y *= ae * velGain * trem;
            v.fb = y * 2.0f;
            y *= voiceGain;
            L[i] += y * v.panL;
            R[i] += y * v.panR;
        }
    }

    // ----- state ----------------------------------------------------------------
    float sr_ = 44100.0f;
    Voice voices_[kVoices];
    VoiceAllocator<kVoices> alloc_;
    Held held_[kMaxHeld];
    int numHeld_ = 0;
    bool monoActive_ = false, sustainDown_ = false;
    int lastMode_ = 0, lastPlayed_ = -1;
    float glideMs_ = 0.0f, velAmp_ = 0.5f, velFilt_ = 0.3f, atFilt_ = 0.0f, uniDetune_ = 12.0f, spread_ = 0.5f;
    int uniVoices_ = 4;

    MorphKind kind_[2][kSceneSize];
    LFO lfo1_, lfo2_;
    MidiControllers controllers_;
    Smoother morph_, volume_, dlySmooth_;
    float lfo1Buf_[kChunk] = {0}, lfo2Buf_[kChunk] = {0};
    float scratchR_[4096] = {0};

    janus::Chorus chorus_;
    janus::PingPongDelay delay_;
    janus::Reverb reverb_;
    float distLp_[2] = {0, 0};

    std::thread worker_;
    std::mutex mtx_;
    std::condition_variable cv_;
    bool quit_ = false;
    int pendingCopy_ = 0;
};

MPC_PLUGIN_ENTRY(Janus)
