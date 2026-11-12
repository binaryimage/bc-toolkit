// Maggot.cpp - binaryimage's noise-but-beautiful mangle synth for MPC standalone.
//
// SPDX-License-Identifier: MIT
//
// Original synth. Pretty underneath, filthy on top, and everything can be torn apart live.
//
// VOICE (8 poly or 1 mono)
//   Osc 1  morph sine > tri > saw > square, with a sine wavefolder        ┐
//   Osc 2  same morph, semi/fine, a 3-voice detuned stack, FM into Osc 1  ├─► mixer (+ ring 1x2)
//   Noise  white / pink / crackle / dust with a color tilt                ┘        │
//   Resonator  note-tuned comb string: turns grit into bells (Res Mix blends it)   │
//   Filter  LP / BP / HP / Notch state-variable, drive, audio-rate Filter FM from Osc 1
//   Amp + filter ADSR, an AD mod envelope, 2 LFOs with a destination each, wheel + aftertouch
//
// ARP (Leviasynth-style weirdness)
//   8 modes incl. Converge / Diverge / Chord, 1-4 octaves, swing, a pattern Length that runs
//   against the held notes (polymeter), ratchets with probability, note probability (rests),
//   random octave jumps, Mutate (slowly scrambles the order), Hold, accents.
//
// MANGLE (whole output, stereo)
//   Grind > Ring mod > Bit/rate crush > Wow > ping-pong Delay > Reverb >
//   tape buffer (TAPE STOP, STUTTER, REVERSE, FREEZE buttons + Glitch: random auto-mangles on the
//   16th grid) > Smear (granular cloud over the last few seconds) > Volume
//   WRECK pushes fold, grind, crush, ring, smear, wow and glitch all at once (default on the wheel).

#define MAGGOT_UID "B9fZ"   // must match "uid" in plugin.json
#include "mpc_plugin.h"
#include "dsp/math.h"
#include "dsp/adsr.h"
#include "dsp/tempo.h"
#include "dsp/midi_cc.h"
#include "maggot_dsp.h"
#include "janus_dsp.h"   // binaryimage's own ping-pong delay and reverb (from Janus)

#include <cmath>

using namespace mpc::dsp;
using mg::clampf;

enum P
{
    kVolume = 0, kVoiceMode, kBend, kSpread,
    kO1Shape, kO1Fold, kO1Level, kO2Shape, kO2Semi, kO2Fine, kO2Stack, kO2Level, kFM, kRing,
    kNType, kNColor, kNLevel,
    kResMix, kResDecay, kResTone, kResPitch,
    kFType, kCutoff, kReso, kFDrive, kFEnv, kFKey, kFFM, kFVel,
    kAA, kAD, kAS, kAR, kFA, kFD, kFS, kFR,
    kL1Shape, kL1Rate, kL1Sync, kL1Div, kL1Dest, kL1Amt,
    kL2Shape, kL2Rate, kL2Dest, kL2Amt,
    kMA, kMD, kMDest, kMAmt,
    kWheelDest, kATCut,
    kArpOn, kArpMode, kArpDiv, kArpOct, kArpGate, kArpSwing, kArpLen, kRatchet, kRatchetProb, kNoteProb, kOctJump, kMutate, kArpHold, kAccent,
    kGrind, kBits, kDown, kRingFreq, kRingMix, kSmear, kSmearSize, kSmearPitch, kWow,
    kTapeStop, kStopTime, kStutter, kStutterDiv, kReverse, kReverseDiv, kFreeze, kGlitch, kWreck,
    kDlyDiv, kDlyFb, kDlyMix, kRevSize, kRevMix, kRevDamp,
    kNumParams
};

// LFO / mod-env destinations
enum Dest { dOff, dPitch, dCutoff, dO1Shape, dO1Fold, dO2Shape, dResPitch, dNoise, dAmp, dPan, dSmear, numDests };
static const std::vector<std::string> kDestNames = {"Off", "Pitch", "Cutoff", "O1 Shape", "O1 Fold", "O2 Shape", "Res Pitch", "Noise", "Amp", "Pan", "Smear"};
enum WheelDest { wWreck, wCutoff, wFold, wSmear, wResMix, wVibrato };

class Maggot : public mpc::Plugin
{
public:
    static mpc::PluginInfo makeInfo()
    {
        mpc::PluginInfo i;
        i.name = "Maggot";
        i.vendor = "binaryimage";
        i.uniqueId = MPC_FOURCC(MAGGOT_UID[0], MAGGOT_UID[1], MAGGOT_UID[2], MAGGOT_UID[3]);
        i.version = 1000;
        i.isInstrument = true;
        i.numInputs = 0;
        i.numOutputs = 2;
        return i;
    }

    Maggot() : mpc::Plugin(makeInfo())
    {
        using namespace mpc;
        const std::vector<std::string> lfoShapes = {"Sine", "Triangle", "Saw", "Square", "S&H", "Smooth Rnd"};
        addParam(floatParam("vol", "Volume", -40.0f, 6.0f, -6.0f, "dB", 1));
        addParam(choiceParam("vmode", "Voice Mode", {"Poly", "Mono"}, 0));
        addParam(intParam("bend", "Bend Range", 0, 24, 2, "st"));
        addParam(floatParam("spread", "Spread", 0, 100, 60, "%", 0));

        addParam(floatParam("o1shape", "O1 Shape", 0, 100, 66, "%", 0));
        addParam(floatParam("o1fold", "O1 Fold", 0, 100, 0, "%", 0));
        addParam(floatParam("o1lvl", "O1 Level", 0, 100, 80, "%", 0));
        addParam(floatParam("o2shape", "O2 Shape", 0, 100, 66, "%", 0));
        addParam(intParam("o2semi", "O2 Semi", -24, 24, 0, "st"));
        addParam(floatParam("o2fine", "O2 Fine", -50, 50, 7, "ct", 0));
        addParam(floatParam("o2stack", "O2 Stack", 0, 100, 0, "%", 0));
        addParam(floatParam("o2lvl", "O2 Level", 0, 100, 50, "%", 0));
        addParam(floatParam("fm", "FM", 0, 100, 0, "%", 0));
        addParam(floatParam("ring", "Ring", 0, 100, 0, "%", 0));

        addParam(choiceParam("ntype", "Noise Type", {"White", "Pink", "Crackle", "Dust"}, 1));
        addParam(floatParam("ncolor", "Noise Color", 0, 100, 50, "%", 0));
        addParam(floatParam("nlvl", "Noise", 0, 100, 0, "%", 0));

        addParam(floatParam("resmix", "Res Mix", 0, 100, 0, "%", 0));
        addParam(floatParam("resdecay", "Res Decay", 0, 100, 70, "%", 0));
        addParam(floatParam("restone", "Res Tone", 0, 100, 50, "%", 0));
        addParam(floatParam("respitch", "Res Pitch", -12, 24, 0, "st", 1));

        addParam(choiceParam("ftype", "Flt Type", {"LP", "BP", "HP", "Notch"}, 0));
        addParam(logParam("cutoff", "Cutoff", 20.0f, 18000.0f, 3000.0f, "Hz", 0));
        addParam(floatParam("reso", "Reso", 0, 100, 20, "%", 0));
        addParam(floatParam("fdrive", "Flt Drive", 0, 100, 10, "%", 0));
        addParam(floatParam("fenv", "Flt Env", -100, 100, 30, "%", 0));
        addParam(floatParam("fkey", "Key Track", 0, 100, 50, "%", 0));
        addParam(floatParam("ffm", "Filter FM", 0, 100, 0, "%", 0));
        addParam(floatParam("fvel", "Vel>Filter", 0, 100, 30, "%", 0));

        addParam(logParam("aa", "Amp Attack", 1.0f, 10000.0f, 3.0f, "ms", 0));
        addParam(logParam("ad", "Amp Decay", 1.0f, 10000.0f, 400.0f, "ms", 0));
        addParam(floatParam("as", "Amp Sustain", 0, 100, 80, "%", 0));
        addParam(logParam("ar", "Amp Release", 1.0f, 10000.0f, 300.0f, "ms", 0));
        addParam(logParam("fa", "Flt Attack", 1.0f, 10000.0f, 2.0f, "ms", 0));
        addParam(logParam("fd", "Flt Decay", 1.0f, 10000.0f, 500.0f, "ms", 0));
        addParam(floatParam("fs", "Flt Sustain", 0, 100, 30, "%", 0));
        addParam(logParam("fr", "Flt Release", 1.0f, 10000.0f, 400.0f, "ms", 0));

        addParam(choiceParam("l1shape", "LFO1 Shape", lfoShapes, 0));
        addParam(logParam("l1rate", "LFO1 Rate", 0.03f, 30.0f, 3.0f, "Hz", 2));
        addParam(boolParam("l1sync", "LFO1 Sync", false));
        addParam(choiceParam("l1div", "LFO1 Div", divisionNames(), kDivisionQuarter));
        addParam(choiceParam("l1dest", "LFO1 Dest", kDestNames, dOff));
        addParam(floatParam("l1amt", "LFO1 Amount", -100, 100, 0, "%", 0));
        addParam(choiceParam("l2shape", "LFO2 Shape", lfoShapes, 5));
        addParam(logParam("l2rate", "LFO2 Rate", 0.03f, 30.0f, 0.2f, "Hz", 2));
        addParam(choiceParam("l2dest", "LFO2 Dest", kDestNames, dOff));
        addParam(floatParam("l2amt", "LFO2 Amount", -100, 100, 0, "%", 0));
        addParam(logParam("ma", "Mod Attack", 1.0f, 10000.0f, 1.0f, "ms", 0));
        addParam(logParam("md", "Mod Decay", 1.0f, 10000.0f, 300.0f, "ms", 0));
        addParam(choiceParam("mdest", "MEnv Dest", kDestNames, dOff));
        addParam(floatParam("mamt", "MEnv Amount", -100, 100, 0, "%", 0));
        addParam(choiceParam("wheeldest", "Wheel Dest", {"WRECK", "Cutoff", "O1 Fold", "Smear", "Res Mix", "Vibrato"}, wWreck));
        addParam(floatParam("atcut", "AT>Cutoff", -100, 100, 0, "%", 0));

        addParam(boolParam("arp", "Arp On", false));
        addParam(choiceParam("amode", "Arp Mode", {"Up", "Down", "UpDown", "Converge", "Diverge", "Random", "Order", "Chord"}, 0));
        addParam(choiceParam("adiv", "Arp Rate", divisionNames(), kDivisionSixteenth));
        addParam(intParam("aoct", "Arp Octaves", 1, 4, 1));
        addParam(floatParam("agate", "Arp Gate", 5, 100, 60, "%", 0));
        addParam(floatParam("aswing", "Arp Swing", 0, 75, 0, "%", 0));
        addParam(intParam("alen", "Arp Length", 1, 16, 16, "steps"));
        addParam(intParam("arat", "Ratchet", 1, 4, 2));
        addParam(floatParam("aratp", "Ratchet Prob", 0, 100, 0, "%", 0));
        addParam(floatParam("aprob", "Note Prob", 0, 100, 100, "%", 0));
        addParam(floatParam("ajump", "Oct Jump", 0, 100, 0, "%", 0));
        addParam(floatParam("amut", "Mutate", 0, 100, 0, "%", 0));
        addParam(boolParam("ahold", "Arp Hold", false));
        addParam(floatParam("aacc", "Accent", 0, 100, 30, "%", 0));

        addParam(floatParam("grind", "Grind", 0, 100, 0, "%", 0));
        addParam(floatParam("bits", "Bits", 2, 16, 16, "bit", 1));
        addParam(floatParam("down", "Downsample", 1, 32, 1, "x", 1, 0.5f));
        addParam(logParam("ringf", "Ring Freq", 1.0f, 4000.0f, 30.0f, "Hz", 1));
        addParam(floatParam("ringm", "Ring Mix", 0, 100, 0, "%", 0));
        addParam(floatParam("smear", "Smear", 0, 100, 0, "%", 0));
        addParam(floatParam("smsize", "Smear Size", 0, 100, 40, "%", 0));
        addParam(floatParam("smpitch", "Smear Pitch", 0, 100, 0, "%", 0));
        addParam(floatParam("wow", "Wow", 0, 100, 0, "%", 0));
        addParam(boolParam("tstop", "Tape Stop", false));
        addParam(logParam("tstime", "Stop Time", 0.05f, 4.0f, 0.6f, "s", 2));
        addParam(boolParam("stut", "Stutter", false));
        addParam(choiceParam("stdiv", "Stutter Div", divisionNames(), kDivisionSixteenth));
        addParam(boolParam("rev", "Reverse", false));
        addParam(choiceParam("revdiv", "Reverse Div", divisionNames(), kDivisionQuarter));
        addParam(boolParam("frz", "Freeze", false));
        addParam(floatParam("glitch", "Glitch", 0, 100, 0, "%", 0));
        addParam(floatParam("wreck", "WRECK", 0, 100, 0, "%", 0));

        addParam(choiceParam("dlydiv", "Delay Time", divisionNames(), 9));
        addParam(floatParam("dlyfb", "Delay Fdbk", 0, 95, 40, "%", 0));
        addParam(floatParam("dlymix", "Delay Mix", 0, 100, 0, "%", 0));
        addParam(floatParam("revsize", "Rev Size", 0, 100, 70, "%", 0));
        addParam(floatParam("revmix", "Rev Mix", 0, 100, 0, "%", 0));
        addParam(floatParam("revdamp", "Rev Damp", 0, 100, 40, "%", 0));

        if (numParams() != kNumParams) mpc::log("Maggot: parameter count mismatch (%d vs %d)", numParams(), (int)kNumParams);
        addPrograms();
    }

    void prepare(double sampleRate, int /*maxBlockSize*/) override
    {
        sr_ = (float)sampleRate;
        for (int i = 0; i < kVoices; ++i)
        {
            Voice& v = voices_[i];
            v.amp.setSampleRate(sampleRate); v.flt.setSampleRate(sampleRate);
            v.res.prepare(sr_);
            v.noise.rng.seed(0x51ED27u + (uint32_t)i * 7919u);
            v.rng.seed(0xC0FFEEu + (uint32_t)i * 104729u);
        }
        tape_.prepare(sr_); smear_.prepare(sr_); wow_.prepare(sr_);
        delay_.prepare(sr_); reverb_.prepare(sr_);
        controllers_.prepare(sampleRate, 15.0f);
        dlySmooth_.setTime(80.0f, sampleRate);
        volume_.setTime(10.0f, sampleRate);
        rng_.seed(0xDEADBEEFu);
        reset();
    }

    void reset() override
    {
        for (auto& v : voices_) v.kill();
        numHeld_ = 0; numLatched_ = 0; arpActiveNotes_ = 0;
        sustain_ = false;
        tape_.clear(); wow_.clear(); delay_.reset(); reverb_.reset();
        controllers_.reset();
        lfoPh_[0] = lfoPh_[1] = 0.0f; lfoHeld_[0] = lfoHeld_[1] = lfoPrev_[0] = lfoPrev_[1] = 0.0f;
        freeBeats_ = 0.0; lastStep_ = -1; stepCounter_ = 0; pendingStep_ = false;
        for (int i = 0; i < 16; ++i) perm_[i] = i;
        autoMode_ = 0; autoLeft_ = 0;
        volume_.snap(dbToGain(get(kVolume)));
        ringPh_ = 0.0f;
    }

    bool formatValue(int index, float plain, std::string& out) override
    {
        char b[32];
        switch (index)
        {
            case kCutoff: case kRingFreq:
                if (plain >= 1000.0f) std::snprintf(b, sizeof b, "%.1fk", (double)(plain * 0.001f));
                else std::snprintf(b, sizeof b, plain < 10.0f ? "%.1f" : "%.0f", (double)plain);
                out = b; return true;
            case kAA: case kAD: case kAR: case kFA: case kFD: case kFR: case kMA: case kMD:
                if (plain >= 1000.0f) { std::snprintf(b, sizeof b, "%.2f s", (double)(plain * 0.001f)); out = b; return true; }
                return false;
            default: return false;
        }
    }

    void process(mpc::ProcessContext& ctx) override
    {
        const int n = ctx.numFrames;
        float* L = ctx.outputs[0];
        float* R = ctx.numOutputs > 1 ? ctx.outputs[1] : scratch_;
        readBlockParams(ctx);

        int pos = 0;
        for (int e = 0; e < ctx.numMidiEvents; ++e)
        {
            const mpc::MidiEvent& ev = ctx.midi[e];
            if (ev.sampleOffset > pos) { renderRange(ctx, L, R, pos, ev.sampleOffset); pos = ev.sampleOffset; }
            handleMidi(ev);
        }
        renderRange(ctx, L, R, pos, n);
        mangle(ctx, L, R, n);
    }

private:
    static constexpr int kVoices = 8;
    static constexpr int kCtrl = 16;

    // ---------------- voice ----------------
    struct Voice
    {
        mg::MorphOsc o1, o2[3];
        mg::NoiseGen noise;
        mg::Resonator res;
        mg::Svf svf;
        ADSR amp, flt;
        mg::Rng rng;
        float mEnv = 0.0f; int mStage = 0;      // AD mod env: 0 idle, 1 attack, 2 decay
        int note = -1; float vel = 1.0f; bool active = false, released = true;
        uint64_t age = 0;
        float panL = 0.707f, panR = 0.707f, fbPm = 0.0f;
        // control-rate cache
        float inc1 = 0, inc2[3] {}, g = 0.1f, k = 1.0f, cutHz = 1000.0f, resPeriod = 100.0f, o1s = 0, o1f = 0, o2s = 0, nl = 0, ampMod = 1.0f;
        int ctrl = 0;
        void kill() { active = false; released = true; amp.reset(); flt.reset(); svf.reset(); res.clear(); mStage = 0; mEnv = 0.0f; note = -1; }
    };
    Voice voices_[kVoices];
    uint64_t ageCounter_ = 0;
    float sr_ = 44100.0f;
    float scratch_[4096] = {0};

    // block parameter snapshot
    struct BP
    {
        float o1Shape, o1Fold, o1Lvl, o2Shape, o2Lvl, o2Stack, fm, ring, nColor, nLvl, resMix, resFb, resDamp, resPitch;
        float cutoff, reso, fdrive, fenv, fkey, ffm, fvel, l1amt, l2amt, mamt, atcut, spread, bend;
        int o2Semi, nType, ftype, l1dest, l2dest, mdest, wheelDest;
        float o2Fine, ma, md;
    } bp_ {};
    float wreck_ = 0.0f, wheel_ = 0.0f;
    float lfoVal_[2] = {0, 0};

    static float dbToGain(float db) { return std::pow(10.0f, db * 0.05f); }

    void readBlockParams(mpc::ProcessContext& ctx)
    {
        controllers_.tick();
        wheel_ = controllers_.modWheel();
        bp_.wheelDest = getInt(kWheelDest);
        wreck_ = clampf(get(kWreck) * 0.01f + (bp_.wheelDest == wWreck ? wheel_ : 0.0f), 0.0f, 1.0f);
        const float W = wreck_;
        bp_.o1Shape = get(kO1Shape) * 0.01f;
        bp_.o1Fold = clampf(get(kO1Fold) * 0.01f + W * 0.6f + (bp_.wheelDest == wFold ? wheel_ : 0.0f), 0.0f, 1.0f);
        bp_.o1Lvl = get(kO1Level) * 0.01f;
        bp_.o2Shape = get(kO2Shape) * 0.01f;
        bp_.o2Semi = getInt(kO2Semi); bp_.o2Fine = get(kO2Fine);
        bp_.o2Stack = get(kO2Stack) * 0.01f;
        bp_.o2Lvl = get(kO2Level) * 0.01f;
        bp_.fm = get(kFM) * 0.01f;
        bp_.ring = get(kRing) * 0.01f;
        bp_.nType = getInt(kNType); bp_.nColor = get(kNColor) * 0.01f; bp_.nLvl = get(kNLevel) * 0.01f;
        bp_.resMix = clampf(get(kResMix) * 0.01f + W * 0.3f + (bp_.wheelDest == wResMix ? wheel_ : 0.0f), 0.0f, 1.0f);
        bp_.resFb = 0.6f + 0.395f * get(kResDecay) * 0.01f;
        bp_.resDamp = 1.0f - get(kResTone) * 0.01f;
        bp_.resPitch = get(kResPitch);
        bp_.ftype = getInt(kFType);
        bp_.cutoff = get(kCutoff); bp_.reso = get(kReso) * 0.01f; bp_.fdrive = get(kFDrive) * 0.01f;
        bp_.fenv = get(kFEnv) * 0.01f; bp_.fkey = get(kFKey) * 0.01f;
        bp_.ffm = clampf(get(kFFM) * 0.01f + W * 0.3f, 0.0f, 1.0f);
        bp_.fvel = get(kFVel) * 0.01f;
        bp_.l1dest = getInt(kL1Dest); bp_.l1amt = get(kL1Amt) * 0.01f;
        bp_.l2dest = getInt(kL2Dest); bp_.l2amt = get(kL2Amt) * 0.01f;
        bp_.mdest = getInt(kMDest); bp_.mamt = get(kMAmt) * 0.01f;
        bp_.ma = get(kMA); bp_.md = get(kMD);
        bp_.atcut = get(kATCut) * 0.01f;
        bp_.spread = get(kSpread) * 0.01f;
        bp_.bend = (float)getInt(kBend);
        for (auto& v : voices_)
        {
            v.amp.set(get(kAA), get(kAD), get(kAS) * 0.01f, get(kAR));
            v.flt.set(get(kFA), get(kFD), get(kFS) * 0.01f, get(kFR));
        }
        bpm_ = ctx.transport.bpm > 20.0 ? ctx.transport.bpm : 120.0;
        playing_ = ctx.transport.playing && ctx.transport.ppqValid;
        ppq_ = ctx.transport.ppqPos;
        volume_.setTarget(dbToGain(get(kVolume)));
    }

    // ---------------- LFOs (global) ----------------
    float lfoPh_[2] {}, lfoHeld_[2] {}, lfoPrev_[2] {};
    float lfoStep(int i, int shape, float rateHz, int samples)
    {
        float& ph = lfoPh_[i];
        float v;
        switch (shape)
        {
            case 0: v = mg::fsin(ph); break;
            case 1: v = ph < 0.5f ? 4.0f * ph - 1.0f : 3.0f - 4.0f * ph; break;
            case 2: v = 1.0f - 2.0f * ph; break;
            case 3: v = ph < 0.5f ? 1.0f : -1.0f; break;
            case 4: v = lfoHeld_[i]; break;
            default: { const float t = ph * ph * (3.0f - 2.0f * ph); v = lfoPrev_[i] + (lfoHeld_[i] - lfoPrev_[i]) * t; break; }
        }
        ph += rateHz * (float)samples / sr_;
        if (ph >= 1.0f) { ph -= std::floor(ph); lfoPrev_[i] = lfoHeld_[i]; lfoHeld_[i] = rng_.bi(); }
        return v;
    }

    // ---------------- notes / voices ----------------
    void startVoice(int note, float vel)
    {
        Voice* v = nullptr;
        if (getInt(kVoiceMode) == 1) v = &voices_[0];
        else
        {
            for (auto& x : voices_) if (!x.active) { v = &x; break; }
            if (!v) for (auto& x : voices_) if (x.released && (!v || x.amp.level() < v->amp.level())) v = &x;
            if (!v) for (auto& x : voices_) if (!v || x.age < v->age) v = &x;
        }
        const bool fresh = !v->active;
        v->note = note; v->vel = vel; v->active = true; v->released = false; v->age = ++ageCounter_;
        if (fresh)
        {
            v->o1.reset(0.0f); for (int k = 0; k < 3; ++k) v->o2[k].reset(v->rng.uni());
            v->svf.reset(); v->res.clear(); v->fbPm = 0.0f;
        }
        // Pan: alternate voices across the field by Spread.
        const int idx = (int)(v - voices_);
        const float pan = ((idx & 1) ? 1.0f : -1.0f) * (0.3f + 0.7f * (float)((idx >> 1) % 4) / 3.0f) * bp_.spread;
        equalPowerPan(clampf(pan, -1.0f, 1.0f), v->panL, v->panR);
        v->amp.noteOn(); v->flt.noteOn();
        v->mStage = 1; v->mEnv = 0.0f;
        v->ctrl = 0;
    }
    void stopVoice(int note)
    {
        for (auto& v : voices_)
            if (v.active && !v.released && v.note == note) { v.released = true; v.amp.noteOff(); v.flt.noteOff(); }
    }

    // ---------------- MIDI ----------------
    int held_[32] {}, heldVel_[32] {}, numHeld_ = 0;          // keys down, in played order
    int latched_[32] {}, latchedVel_[32] {}, numLatched_ = 0;  // what the arp plays
    bool sustain_ = false;
    MidiControllers controllers_;

    void handleMidi(const mpc::MidiEvent& ev)
    {
        const bool arp = getBool(kArpOn);
        if (ev.isNoteOn())
        {
            const int note = ev.note();
            removeFrom(held_, heldVel_, numHeld_, note);
            if (numHeld_ < 32) { held_[numHeld_] = note; heldVel_[numHeld_] = ev.velocity(); ++numHeld_; }
            if (arp)
            {
                // A fresh chord after everything was let go replaces the latched notes.
                if (numHeld_ == 1 && getBool(kArpHold)) numLatched_ = 0;
                removeFrom(latched_, latchedVel_, numLatched_, note);
                if (numLatched_ < 32) { latched_[numLatched_] = note; latchedVel_[numLatched_] = ev.velocity(); ++numLatched_; }
                if (numLatched_ == 1 && !playing_) { pendingStep_ = true; lastStep_ = -1; freeBeats_ = 0.0; }   // free-run restarts on the first key
            }
            else startVoice(note, ev.velocity() / 127.0f);
        }
        else if (ev.isNoteOff())
        {
            const int note = ev.note();
            removeFrom(held_, heldVel_, numHeld_, note);
            if (arp)
            {
                if (!getBool(kArpHold)) removeFrom(latched_, latchedVel_, numLatched_, note);
            }
            else if (!sustain_) stopVoice(note);
        }
        else if (ev.isController() && ev.controller() == cc::Sustain)
        {
            controllers_.handle(ev);
            sustain_ = ev.controllerValue() >= 64;
            if (!sustain_ && !arp)
                for (auto& v : voices_)
                {
                    bool down = false;
                    for (int i = 0; i < numHeld_; ++i) if (held_[i] == v.note) down = true;
                    if (v.active && !v.released && !down) { v.released = true; v.amp.noteOff(); v.flt.noteOff(); }
                }
        }
        else if (ev.isAllNotesOff())
        {
            numHeld_ = 0; numLatched_ = 0;
            for (auto& v : voices_) if (v.active) { v.released = true; v.amp.noteOff(); v.flt.noteOff(); }
        }
        else if (ev.isPitchBend()) bendSemis_ = (float)ev.pitchBend() / 8192.0f;
        else controllers_.handle(ev);
    }
    static void removeFrom(int* a, int* vel, int& n, int note)
    {
        for (int i = 0; i < n; ++i)
            if (a[i] == note) { for (int j = i + 1; j < n; ++j) { a[j - 1] = a[j]; vel[j - 1] = vel[j]; } --n; return; }
    }
    float bendSemis_ = 0.0f;

    // ---------------- arp ----------------
    double bpm_ = 120.0, ppq_ = 0.0, freeBeats_ = 0.0;
    bool playing_ = false, pendingStep_ = false;
    long long lastStep_ = -1;
    int stepCounter_ = 0, perm_[16] {};
    struct ArpNote { int note; int samplesLeft; int startIn; float vel; bool on; };
    ArpNote arpNotes_[32] {};
    int arpActiveNotes_ = 0;

    int buildSequence(int* out)   // returns count (max 64)
    {
        int notes[32], vels[32];
        const int n = numLatched_;
        for (int i = 0; i < n; ++i) { notes[i] = latched_[i]; vels[i] = latchedVel_[i]; }
        const int mode = getInt(kArpMode);
        int sorted[32];
        for (int i = 0; i < n; ++i) sorted[i] = notes[i];
        std::sort(sorted, sorted + n);
        int base[32]; int bn = 0;
        switch (mode)
        {
            case 1: for (int i = n - 1; i >= 0; --i) base[bn++] = sorted[i]; break;                      // Down
            case 2: for (int i = 0; i < n; ++i) base[bn++] = sorted[i];                                   // UpDown
                    for (int i = n - 2; i >= 1; --i) base[bn++] = sorted[i]; break;
            case 3: { int lo = 0, hi = n - 1; while (lo <= hi) { base[bn++] = sorted[lo++]; if (lo <= hi) base[bn++] = sorted[hi--]; } break; }  // Converge
            case 4: { int mid = n / 2, a = mid, b = mid + 1;                                              // Diverge
                      while (a >= 0 || b < n) { if (a >= 0) base[bn++] = sorted[a--]; if (b < n) base[bn++] = sorted[b++]; } break; }
            case 6: for (int i = 0; i < n; ++i) base[bn++] = notes[i]; break;                             // Order
            default: for (int i = 0; i < n; ++i) base[bn++] = sorted[i]; break;                           // Up / Random / Chord
        }
        const int oct = getInt(kArpOct);
        int c = 0;
        for (int o = 0; o < oct && c < 64; ++o)
            for (int i = 0; i < bn && c < 64; ++i) out[c++] = base[i] + 12 * o;
        (void)vels;
        return c;
    }

    void arpTrigger(int sampleInStep, int stepSamples)
    {
        if (numLatched_ == 0) return;
        int seq[64];
        const int count = buildSequence(seq);
        if (count == 0) return;
        const int len = getInt(kArpLen);
        const int step = stepCounter_ % len;
        // Mutate: at the top of each pattern cycle, maybe swap two steps of the order.
        if (step == 0 && rng_.uni() < get(kMutate) * 0.01f)
        {
            const int a = (int)(rng_.uni() * 16.0f) & 15, b = (int)(rng_.uni() * 16.0f) & 15;
            std::swap(perm_[a], perm_[b]);
        }
        ++stepCounter_;
        if (rng_.uni() * 100.0f >= get(kNoteProb)) return;    // a rest
        const int mode = getInt(kArpMode);
        float vel = 0.85f;
        if (step % 4 == 0) vel = 1.0f; else vel = 1.0f - get(kAccent) * 0.006f;
        int ratchet = 1;
        if (rng_.uni() * 100.0f < get(kRatchetProb)) ratchet = getInt(kRatchet);
        const float gate = get(kArpGate) * 0.01f;
        const int sub = std::max(1, stepSamples / ratchet);
        auto push = [&](int note, int startIn)
        {
            if (rng_.uni() * 100.0f < get(kOctJump)) note += (rng_.uni() < 0.5f ? 12 : (rng_.uni() < 0.6f ? 24 : -12));
            note = std::max(0, std::min(127, note));
            for (auto& a : arpNotes_)
                if (!a.on && a.startIn < 0) { a = {note, std::max(1, (int)(sub * gate)), startIn, vel, false}; return; }
        };
        for (int r = 0; r < ratchet; ++r)
        {
            const int startIn = sampleInStep + r * sub;
            if (mode == 7) { for (int i = 0; i < numLatched_; ++i) push(latched_[i], startIn); }
            else if (mode == 5) push(seq[(int)(rng_.uni() * (float)count) % count], startIn);
            else push(seq[perm_[step % 16] % std::max(1, std::min(count, len)) % count], startIn);
        }
    }

    // advance the arp by n samples starting at `offset` in the block; fires note-ons/offs
    void arpAdvance(int offset, int n)
    {
        const double divBeats = divisionBeats(getInt(kArpDiv));
        const double beatsPerSample = bpm_ / 60.0 / (double)sr_;
        const int stepSamples = std::max(1, (int)(divBeats / beatsPerSample));
        const double swing = get(kArpSwing) * 0.01 * 0.5;
        for (int i = 0; i < n; ++i)
        {
            // note scheduling
            for (auto& a : arpNotes_)
            {
                if (a.startIn >= 0)
                {
                    if (a.startIn == 0) { startVoice(a.note, a.vel); a.on = true; a.startIn = -1; }
                    else --a.startIn;
                }
                else if (a.on)
                {
                    if (--a.samplesLeft <= 0) { a.on = false; stopVoice(a.note); }
                }
            }
            if (numLatched_ == 0 && !getBool(kArpHold)) continue;
            const double beat = playing_ ? ppq_ + (double)(offset + i) * beatsPerSample : freeBeats_;
            if (!playing_) freeBeats_ += beatsPerSample;
            const double s = beat / divBeats;
            const long long k = (long long)std::floor(s);
            const double frac = s - (double)k;
            if (k != lastStep_) { lastStep_ = k; pendingStep_ = true; }
            if (pendingStep_ && frac >= ((k & 1) ? swing : 0.0)) { pendingStep_ = false; arpTrigger(0, stepSamples); }
        }
    }

    // ---------------- render ----------------
    void renderRange(mpc::ProcessContext& ctx, float* L, float* R, int from, int to)
    {
        if (to <= from) return;
        const bool arp = getBool(kArpOn);
        int pos = from;
        while (pos < to)
        {
            const int n = std::min(kCtrl, to - pos);
            if (arp) arpAdvance(pos, n);
            // global LFOs at control rate
            const float l1rate = getBool(kL1Sync) ? (float)(bpm_ / 60.0 / divisionBeats(getInt(kL1Div))) : get(kL1Rate);
            lfoVal_[0] = lfoStep(0, getInt(kL1Shape), l1rate, n);
            lfoVal_[1] = lfoStep(1, getInt(kL2Shape), get(kL2Rate), n);
            for (auto& v : voices_) if (v.active) renderVoice(v, L + pos, R + pos, n);
            pos += n;
        }
        (void)ctx;
    }

    void renderVoice(Voice& v, float* L, float* R, int n)
    {
        const BP& b = bp_;
        // ---- control rate ----
        // mod env (AD)
        float d[numDests] {};
        auto addMod = [&](int dest, float amt) { if (dest > 0 && dest < numDests) d[dest] += amt; };
        addMod(b.l1dest, lfoVal_[0] * b.l1amt);
        addMod(b.l2dest, lfoVal_[1] * b.l2amt);
        addMod(b.mdest, v.mEnv * b.mamt);
        if (b.wheelDest == wVibrato) d[dPitch] += lfoVal_[0] * wheel_ * 0.04f;
        const float note = (float)v.note + bendSemis_ * b.bend + d[dPitch] * 12.0f;
        const float f1 = 440.0f * std::exp2((note - 69.0f) / 12.0f);
        v.inc1 = f1 / sr_;
        const float f2 = 440.0f * std::exp2((note + (float)b.o2Semi + b.o2Fine * 0.01f - 69.0f) / 12.0f);
        for (int k = 0; k < 3; ++k)
        {
            const float det = (k - 1) * b.o2Stack * 0.35f;   // up to +/-35 cents
            v.inc2[k] = f2 * std::exp2(det / 12.0f) / sr_;
        }
        v.o1s = clampf(b.o1Shape + d[dO1Shape], 0.0f, 1.0f);
        v.o1f = clampf(b.o1Fold + d[dO1Fold], 0.0f, 1.0f);
        v.o2s = clampf(b.o2Shape + d[dO2Shape], 0.0f, 1.0f);
        v.nl = clampf(b.nLvl + d[dNoise], 0.0f, 1.0f);
        v.ampMod = clampf(1.0f + d[dAmp], 0.0f, 2.0f);
        const float fe = v.flt.level();
        const float oct = b.fenv * fe * 6.0f + b.fkey * (note - 60.0f) / 12.0f + b.fvel * (v.vel - 0.6f) * 3.0f
                        + d[dCutoff] * 4.0f + b.atcut * controllers_.aftertouch() * 4.0f
                        + (b.wheelDest == wCutoff ? wheel_ * 4.0f : 0.0f);
        v.cutHz = clampf(b.cutoff * std::exp2(clampf(oct, -10.0f, 10.0f)), 20.0f, sr_ * 0.45f);
        v.g = mg::svfG(v.cutHz, sr_);
        v.k = 2.0f - 1.97f * b.reso;
        const float resNote = note + b.resPitch + d[dResPitch] * 12.0f;
        v.resPeriod = sr_ / (440.0f * std::exp2((resNote - 69.0f) / 12.0f));
        const float drive = 1.0f + b.fdrive * 6.0f;
        const bool stack = b.o2Stack > 0.001f;
        const float pan = clampf(d[dPan], -1.0f, 1.0f);
        float pl = v.panL, pr = v.panR;
        if (pan != 0.0f) { pl *= 1.0f - std::max(0.0f, pan); pr *= 1.0f + std::min(0.0f, pan); }
        const float smearMod = d[dSmear];
        smearModAccum_ += smearMod; smearModCount_ += 1;
        const float gain = 0.35f;

        // mod env advance (control rate)
        const float nS = (float)n;
        if (v.mStage == 1) { v.mEnv += nS / std::max(1.0f, b.ma * 0.001f * sr_); if (v.mEnv >= 1.0f) { v.mEnv = 1.0f; v.mStage = 2; } }
        else if (v.mStage == 2) { v.mEnv *= std::exp(-nS * 6.9f / std::max(1.0f, b.md * 0.001f * sr_)); if (v.mEnv < 1e-4f) { v.mEnv = 0.0f; v.mStage = 0; } }

        for (int i = 0; i < n; ++i)
        {
            const float a = v.amp.process();
            v.flt.process();
            // Osc 2 (stack) first: it FMs osc 1.
            float x2 = v.o2[1].process(v.inc2[1], v.o2s, 0.0f);
            if (stack) x2 = (x2 + v.o2[0].process(v.inc2[0], v.o2s, 0.0f) + v.o2[2].process(v.inc2[2], v.o2s, 0.0f)) * 0.5f;
            const float x1 = v.o1.process(v.inc1, v.o1s, v.o1f, b.fm * 0.5f * x2);
            float mix = x1 * b.o1Lvl + x2 * b.o2Lvl + x1 * x2 * b.ring;
            if (v.nl > 0.0f) mix += v.noise.process(b.nType, b.nColor) * v.nl;
            if (b.resMix > 0.001f)
            {
                const float r = v.res.process(mix * 0.25f, v.resPeriod, b.resFb, b.resDamp);
                mix = mix + (r * 2.0f - mix) * b.resMix;
            }
            float g = v.g;
            if (b.ffm > 0.001f) g = mg::svfG(v.cutHz * std::exp2(x1 * b.ffm * 3.0f), sr_);
            float y = v.svf.process(mg::fastTanh(mix * 0.5f * drive), g, v.k, b.ftype);
            y *= a * (0.4f + 0.6f * v.vel) * v.ampMod * gain;
            L[i] += y * pl;
            R[i] += y * pr;
        }
        if (!v.amp.isActive()) v.kill();
    }

    // ---------------- mangle chain ----------------
    mg::MangleBuffer tape_;
    mg::Smear smear_;
    mg::Crusher crush_;
    mg::Wow wow_;
    janus::PingPongDelay delay_;
    janus::Reverb reverb_;
    Smoother dlySmooth_, volume_;
    mg::Rng rng_;
    float ringPh_ = 0.0f;
    float smearModAccum_ = 0.0f; int smearModCount_ = 0;
    int autoMode_ = 0, autoLeft_ = 0;     // Glitch: automatic stutter/reverse/stop on the 16th grid
    long long lastSixteenth_ = -1;

    void mangle(mpc::ProcessContext& ctx, float* L, float* R, int n)
    {
        const float W = wreck_;
        const float grind = clampf(get(kGrind) * 0.01f + W * 0.7f, 0.0f, 1.0f);
        const float bits = clampf(get(kBits) - W * 10.0f, 2.0f, 16.0f);
        const float down = clampf(get(kDown) + W * 8.0f, 1.0f, 32.0f);
        const float ringMix = clampf(get(kRingMix) * 0.01f + W * 0.3f, 0.0f, 1.0f);
        const float ringInc = get(kRingFreq) / sr_;
        const float wow = clampf(get(kWow) * 0.01f + W * 0.5f, 0.0f, 1.0f);
        float smearAmt = get(kSmear) * 0.01f + W * 0.5f + (bp_.wheelDest == wSmear ? wheel_ : 0.0f);
        if (smearModCount_ > 0) { smearAmt += smearModAccum_ / (float)smearModCount_; smearModAccum_ = 0.0f; smearModCount_ = 0; }
        smearAmt = clampf(smearAmt, 0.0f, 1.0f);
        const float glitch = clampf(get(kGlitch) * 0.01f + W * 0.4f, 0.0f, 1.0f);

        // Glitch: roll the dice on each 16th.
        const double beatsPerSample = bpm_ / 60.0 / (double)sr_;
        const double startBeat = playing_ ? ppq_ : freeBeatsFx_;
        if (!playing_) freeBeatsFx_ += beatsPerSample * n;
        const long long sixteenth = (long long)std::floor((startBeat + beatsPerSample * n) * 4.0);
        if (sixteenth != lastSixteenth_)
        {
            lastSixteenth_ = sixteenth;
            if (autoLeft_ > 0) { if (--autoLeft_ == 0) autoMode_ = 0; }
            else if (glitch > 0.0f && rng_.uni() < glitch * 0.3f)
            {
                const float r = rng_.uni();
                autoMode_ = r < 0.55f ? 2 : (r < 0.85f ? 3 : 1);   // mostly stutter, some reverse, rare tape stop
                autoLeft_ = 1 + (int)(rng_.uni() * 4.0f);
            }
        }

        const double sixteenthSamples = 0.25 / beatsPerSample;
        const float stutLen = (float)(divisionBeats(getInt(kStutterDiv)) / beatsPerSample);
        const float revLen = (float)(divisionBeats(getInt(kReverseDiv)) / beatsPerSample);
        const bool tStop = getBool(kTapeStop) || autoMode_ == 1;
        const bool stut = getBool(kStutter) || autoMode_ == 2;
        const bool rev = getBool(kReverse) || autoMode_ == 3;
        const bool frz = getBool(kFreeze);
        const float loop = frz ? (float)(sixteenthSamples * 2.0) : (rev && !stut ? revLen : stutLen);
        tape_.setModes(tStop, stut, rev, frz, loop, autoMode_ == 1 ? 0.25f : get(kStopTime));

        float target = (float)(divisionBeats(getInt(kDlyDiv)) / beatsPerSample);
        target = clampf(target, 16.0f, sr_ * 2.5f);
        dlySmooth_.setTarget(target);
        for (int i = 0; i < n; ++i) dlySmooth_.process();

        const float smSize = get(kSmearSize) * 0.01f, smPitch = get(kSmearPitch) * 0.01f;
        const float gGain = 1.0f + grind * 12.0f;
        const float gMake = 1.0f / (1.0f + grind * 2.5f);
        for (int i = 0; i < n; ++i)
        {
            float l = L[i], r = R[i];
            if (grind > 0.001f)
            {
                l = (mg::fastTanh(l * gGain + 0.3f * grind) - mg::fastTanh(0.3f * grind)) * gMake * 1.5f;
                r = (mg::fastTanh(r * gGain + 0.3f * grind) - mg::fastTanh(0.3f * grind)) * gMake * 1.5f;
            }
            if (ringMix > 0.001f)
            {
                ringPh_ += ringInc; if (ringPh_ >= 1.0f) ringPh_ -= 1.0f;
                const float m = mg::fsin(ringPh_);
                l += (l * m - l) * ringMix;
                r += (r * m - r) * ringMix;
            }
            if (bits < 15.9f || down > 1.01f) crush_.process(l, r, bits, down);
            wow_.process(l, r, wow);
            L[i] = l; R[i] = r;
        }
        delay_.process(L, R, n, dlySmooth_.value(), get(kDlyFb) * 0.01f, get(kDlyMix) * 0.01f, 3500.0f);
        reverb_.process(L, R, n, get(kRevSize) * 0.01f, get(kRevDamp) * 0.01f, get(kRevMix) * 0.01f);
        for (int i = 0; i < n; ++i)
        {
            float l = L[i], r = R[i];
            tape_.process(l, r);
            smear_.process(tape_, l, r, smearAmt, smSize, smPitch);
            const float g = volume_.process();
            l *= g; r *= g;
            L[i] = std::isfinite(l) ? l : 0.0f;
            R[i] = std::isfinite(r) ? r : 0.0f;
        }
        (void)ctx;
    }
    double freeBeatsFx_ = 0.0;

    // ---------------- presets ----------------
    using Ov = std::initializer_list<std::pair<int, float>>;
    void addPrograms()
    {
        auto program = [this](const char* name, Ov o)
        {
            std::vector<float> v((size_t)numParams());
            for (int i = 0; i < numParams(); ++i) v[(size_t)i] = param(i).def;
            for (const auto& p : o) v[(size_t)p.first] = p.second;
            addProgram(name, std::move(v));
        };
        #include "maggot_presets.inc"
    }
};

MPC_PLUGIN_ENTRY(Maggot)
