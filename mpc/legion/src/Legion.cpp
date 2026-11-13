// Legion.cpp - binaryimage's choir synth for the MPC. "My name is Legion, for we are many."
//
// SPDX-License-Identifier: MIT
//
// Every note is a small crowd of SINGERS (1-8). Each singer has its own slightly-off pitch, slow
// drift, vibrato speed and breath, like real people standing next to each other. Their voices go
// through a bank of five formant filters, which is what makes "ooh", "aah" or "mmm": the vowel
// shapes come from published formant measurements for bass, tenor, alto and soprano singers.
//
//   singers (glottal saw + breath) ─┬─► 5 formant band-passes (SECTION + VOWEL) ─► CHOIR ┐
//                                   └─► resonant low-pass (+ SUB) ───────────► SYNTH ─┤ MORPH
//                                                                                    ▼
//        amp envelope ─► ensemble ─► echo ─► hall ─► tone ─► volume ─► out
//
// The vowel can open over each note (OPEN: "mmm-aah"), wander on an LFO, or ride the mod wheel.
// MORPH crossfades from the choir to a synth made from the same singers, for augmented,
// half-human sounds.

#include "mpc_plugin.h"
#include "dsp/adsr.h"
#include "dsp/tempo.h"
#include "bi_fx.h"
#include <cmath>
#include <cstdio>

using namespace bi;

enum P
{
    kVolume = 0, kSection, kVowel, kOpen, kVowelLfo, kVowelRate, kFormant, kSingers, kSpread, kVibrato,
    kVibRate, kVibDelay, kBreath, kConsonant, kBright, kMorph, kWave, kCutoff, kReso, kFltEnv, kSub,
    kAttack, kDecay, kSustain, kRelease, kVelocity, kGlide, kEnsemble, kEcho, kEchoTime, kEchoFb,
    kHall, kHallSize, kTone, kWidth, kWheelDest, kBendRange, kOctave, kDrift,
    kNumParams
};
enum Section { secBass = 0, secTenor, secAlto, secSoprano, secAuto };
enum WheelDest { wVowel = 0, wMorph, wVibrato, wFormant, wOpen };

namespace {
float skewFor(float lo, float hi, float centre) { return std::log(0.5f) / std::log((centre - lo) / (hi - lo)); }

const int kMaxVoices = 8, kMaxSingers = 8, kNumVowels = 6, kCtl = 16;
const int kEchoDivs[] = {4, 5, 6, 7, 8, 9, 10, 11, 12, 13};   // 1/16 .. 1/2
const int kNumEchoDivs = 10;

// Formant sets: [section][vowel] = {freq x5, amp dB x5, bandwidth x5}. Vowel order: Mm Uu Oo Ah Eh Ee.
// Uu/Oo/Ah/Eh/Ee are the classic published singer formant measurements; Mm is a closed-mouth hum.
struct Formant { float f[5], a[5], bw[5]; };
const Formant kTable[4][kNumVowels] = {
    {   // bass
        {{250, 900, 2200, 2650, 3000}, {0, -26, -34, -38, -44}, {60, 150, 200, 250, 300}},
        {{350, 600, 2400, 2675, 2950}, {0, -20, -32, -28, -36}, {40, 80, 100, 120, 120}},
        {{400, 750, 2400, 2600, 2900}, {0, -11, -21, -20, -40}, {40, 80, 100, 120, 120}},
        {{600, 1040, 2250, 2450, 2750}, {0, -7, -9, -9, -20}, {60, 70, 110, 120, 130}},
        {{400, 1620, 2400, 2800, 3100}, {0, -12, -9, -12, -18}, {40, 80, 100, 120, 120}},
        {{250, 1750, 2600, 3050, 3340}, {0, -30, -16, -22, -28}, {60, 90, 100, 120, 120}},
    },
    {   // tenor
        {{260, 950, 2400, 2900, 3300}, {0, -26, -32, -36, -44}, {60, 150, 200, 250, 300}},
        {{350, 600, 2700, 2900, 3300}, {0, -20, -17, -14, -26}, {40, 60, 100, 120, 120}},
        {{400, 800, 2600, 2800, 3000}, {0, -10, -12, -12, -26}, {40, 80, 100, 120, 120}},
        {{650, 1080, 2650, 2900, 3250}, {0, -6, -7, -8, -22}, {80, 90, 120, 130, 140}},
        {{400, 1700, 2600, 3200, 3580}, {0, -14, -12, -14, -20}, {70, 80, 100, 120, 120}},
        {{290, 1870, 2800, 3250, 3540}, {0, -15, -18, -20, -30}, {40, 90, 100, 120, 120}},
    },
    {   // alto
        {{280, 1000, 2600, 3300, 4400}, {0, -28, -36, -42, -60}, {60, 150, 200, 250, 300}},
        {{325, 700, 2530, 3500, 4950}, {0, -12, -30, -40, -64}, {50, 60, 170, 180, 200}},
        {{450, 800, 2830, 3500, 4950}, {0, -9, -16, -28, -55}, {70, 80, 100, 130, 135}},
        {{800, 1150, 2800, 3500, 4950}, {0, -4, -20, -36, -60}, {80, 90, 120, 130, 140}},
        {{400, 1600, 2700, 3300, 4950}, {0, -24, -30, -35, -60}, {60, 80, 120, 150, 200}},
        {{350, 1700, 2700, 3700, 4950}, {0, -20, -30, -36, -60}, {50, 100, 120, 150, 200}},
    },
    {   // soprano
        {{300, 1100, 2800, 3600, 4600}, {0, -28, -38, -44, -60}, {60, 150, 200, 250, 300}},
        {{325, 700, 2700, 3800, 4950}, {0, -16, -35, -40, -60}, {50, 60, 170, 180, 200}},
        {{450, 800, 2830, 3800, 4950}, {0, -11, -22, -22, -50}, {40, 80, 100, 120, 120}},
        {{800, 1150, 2900, 3900, 4950}, {0, -6, -32, -20, -50}, {80, 90, 120, 130, 140}},
        {{350, 2000, 2800, 3600, 4950}, {0, -20, -15, -40, -56}, {60, 100, 120, 150, 200}},
        {{270, 2140, 2950, 3900, 4950}, {0, -12, -26, -26, -44}, {60, 90, 100, 120, 120}},
    },
};
const char* kVowelNames[kNumVowels] = {"Mm", "Uu", "Oo", "Ah", "Eh", "Ee"};

inline float polyBlep(float t, float dt)
{
    if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
    if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
    return 0.0f;
}
// cents -> ratio without pow (accurate to a hair within +-200 cents)
inline float centsRatio(float c) { const float x = c * 0.000577623f; return 1.0f + x + 0.5f * x * x + 0.1666667f * x * x * x; }
}

class Legion : public mpc::Plugin
{
public:
    static mpc::PluginInfo makeInfo()
    {
        mpc::PluginInfo i;
        i.name = "Legion";
        i.vendor = "binaryimage";
        i.uniqueId = MPC_FOURCC('B', 'l', 'G', '8');   // must match "uid" in plugin.json
        i.version = 1000;
        i.isInstrument = true;
        i.numInputs = 0;
        i.numOutputs = 2;
        return i;
    }

    Legion() : mpc::Plugin(makeInfo())
    {
        using namespace mpc;
        std::vector<std::string> echoT;
        for (int k = 0; k < kNumEchoDivs; ++k) echoT.push_back(dsp::divisionName(kEchoDivs[k]));
        addParam(floatParam("volume", "Volume", -30.0f, 6.0f, -3.0f, "dB", 1));
        addParam(choiceParam("section", "Section", {"Bass", "Tenor", "Alto", "Soprano", "Auto"}, secAuto));
        addParam(floatParam("vowel", "Vowel", 0.0f, 5.0f, 3.0f, "", 2));
        addParam(floatParam("open", "Open", 0.0f, 100.0f, 0.0f, "%", 0));
        addParam(floatParam("vowel_lfo", "Vowel LFO", 0.0f, 100.0f, 0.0f, "%", 0));
        addParam(floatParam("vowel_rate", "Vowel Rate", 0.03f, 6.0f, 0.25f, "Hz", 2, skewFor(0.03f, 6.0f, 0.4f)));
        addParam(floatParam("formant", "Formant", -12.0f, 12.0f, 0.0f, "", 1));
        addParam(intParam("singers", "Singers", 1, kMaxSingers, 4));
        addParam(floatParam("spread", "Spread", 0.0f, 50.0f, 12.0f, "ct", 0));
        addParam(floatParam("vibrato", "Vibrato", 0.0f, 100.0f, 30.0f, "%", 0));
        addParam(floatParam("vib_rate", "Vib Rate", 3.0f, 8.0f, 5.3f, "Hz", 1));
        addParam(floatParam("vib_delay", "Vib Delay", 0.0f, 2000.0f, 400.0f, "", 0, skewFor(0.0f, 2000.0f, 400.0f)));
        addParam(floatParam("breath", "Breath", 0.0f, 100.0f, 25.0f, "%", 0));
        addParam(floatParam("consonant", "Consonant", 0.0f, 100.0f, 0.0f, "%", 0));
        addParam(floatParam("bright", "Brightness", 0.0f, 100.0f, 50.0f, "%", 0));
        addParam(floatParam("morph", "Morph", 0.0f, 100.0f, 0.0f, "%", 0));
        addParam(choiceParam("wave", "Synth Wave", {"Saw", "Pulse", "Hollow"}, 0));
        addParam(floatParam("cutoff", "Cutoff", 40.0f, 18000.0f, 3000.0f, "", 0, skewFor(40.0f, 18000.0f, 1500.0f)));
        addParam(floatParam("reso", "Reso", 0.0f, 100.0f, 20.0f, "%", 0));
        addParam(floatParam("flt_env", "Flt Env", -100.0f, 100.0f, 30.0f, "", 0));
        addParam(floatParam("sub", "Sub", 0.0f, 100.0f, 0.0f, "%", 0));
        addParam(floatParam("attack", "Attack", 1.0f, 6000.0f, 450.0f, "", 0, skewFor(1.0f, 6000.0f, 500.0f)));
        addParam(floatParam("decay", "Decay", 1.0f, 6000.0f, 600.0f, "", 0, skewFor(1.0f, 6000.0f, 600.0f)));
        addParam(floatParam("sustain", "Sustain", 0.0f, 100.0f, 100.0f, "%", 0));
        addParam(floatParam("release", "Release", 5.0f, 8000.0f, 1400.0f, "", 0, skewFor(5.0f, 8000.0f, 1000.0f)));
        addParam(floatParam("velocity", "Velocity", 0.0f, 100.0f, 40.0f, "%", 0));
        addParam(floatParam("glide", "Glide", 0.0f, 1000.0f, 0.0f, "", 0, skewFor(0.0f, 1000.0f, 150.0f)));
        addParam(floatParam("ensemble", "Ensemble", 0.0f, 100.0f, 30.0f, "%", 0));
        addParam(floatParam("echo", "Echo", 0.0f, 100.0f, 0.0f, "%", 0));
        addParam(choiceParam("echo_time", "Echo Time", echoT, 5));          // 1/8D
        addParam(floatParam("echo_fb", "Echo Fbk", 0.0f, 90.0f, 40.0f, "%", 0));
        addParam(floatParam("hall", "Hall", 0.0f, 100.0f, 35.0f, "%", 0));
        addParam(floatParam("hall_size", "Hall Size", 0.0f, 100.0f, 70.0f, "%", 0));
        addParam(floatParam("tone", "Tone", -100.0f, 100.0f, 0.0f, "", 0));
        addParam(floatParam("width", "Width", 0.0f, 100.0f, 80.0f, "%", 0));
        addParam(choiceParam("wheel_dest", "Wheel Dest", {"Vowel", "Morph", "Vibrato", "Formant", "Open"}, wVowel));
        addParam(intParam("bend_range", "Bend Range", 0, 12, 2, "st"));
        addParam(intParam("octave", "Octave", -2, 2, 0, ""));
        addParam(floatParam("drift", "Drift", 0.0f, 100.0f, 30.0f, "%", 0));
        if (numParams() != kNumParams) mpc::log("Legion: parameter count mismatch");
        addPrograms();
    }

    void prepare(double sampleRate, int) override
    {
        sr_ = (float)sampleRate;
        for (auto& v : voice_) { v.amp.setSampleRate(sampleRate); v.amp.reset(); }
        for (auto& r : ens_) r.prepare((int)(sr_ * 0.04f));
        for (auto& r : echo_) r.prepare((int)(sr_ * 2.2f));
        hall_.prepare(sr_);
        vol_.prepare(30.0f, sr_);
        reset();
    }

    void reset() override
    {
        for (int k = 0; k < kMaxVoices; ++k) { voice_[k] = Voice(); voice_[k].amp.setSampleRate(sr_); voice_[k].amp.reset(); voice_[k].seed(k); }
        for (auto& r : ens_) r.clear();
        for (auto& r : echo_) r.clear();
        hall_.reset();
        for (int c = 0; c < 2; ++c) { tilt_[c].reset(); echoLp_[c].reset(); dc_[c].reset(); }
        wheel_ = 0.0f; bend_ = 0.0f; sustainPedal_ = false; ensPh_ = 0.0f; vowelLfoPh_ = 0.0f; stamp_ = 0; lastNote_ = -1.0f;
        vol_.snap(dbToGain(get(kVolume)));
    }

    bool formatValue(int index, float plain, std::string& out) override
    {
        char b[40];
        if (index == kVowel)
        {
            const int a = (int)std::floor(plain);
            const float f = plain - (float)a;
            if (a >= kNumVowels - 1 || f < 0.05f) out = kVowelNames[std::min(a, kNumVowels - 1)];
            else if (f > 0.95f) out = kVowelNames[a + 1];
            else { std::snprintf(b, sizeof b, "%s>%s %d%%", kVowelNames[a], kVowelNames[a + 1], (int)std::lround(f * 100.0f)); out = b; }
            return true;
        }
        if (index == kAttack || index == kDecay || index == kRelease || index == kVibDelay || index == kGlide)
        {
            if (index == kGlide && plain < 0.5f) { out = "Off"; return true; }
            if (plain >= 1000.0f) std::snprintf(b, sizeof b, "%.2f s", (double)(plain * 0.001f));
            else std::snprintf(b, sizeof b, "%.0f ms", (double)plain);
            out = b; return true;
        }
        if (index == kCutoff)
        {
            if (plain >= 1000.0f) std::snprintf(b, sizeof b, "%.1f kHz", (double)(plain * 0.001f)); else std::snprintf(b, sizeof b, "%.0f Hz", (double)plain);
            out = b; return true;
        }
        if (index == kFormant)
        {
            std::snprintf(b, sizeof b, std::fabs(plain) < 0.05f ? "0 st" : "%+.1f st", (double)plain);
            out = b; return true;
        }
        if (index == kTone)
        {
            const int v = (int)std::lround(plain);
            if (v == 0) out = "Flat"; else { std::snprintf(b, sizeof b, v < 0 ? "Dark %d" : "Bright %d", v < 0 ? -v : v); out = b; }
            return true;
        }
        if (index == kFltEnv) { std::snprintf(b, sizeof b, "%+d", (int)std::lround(plain)); out = b; return true; }
        if (index == kOctave) { const int v = (int)std::lround(plain); std::snprintf(b, sizeof b, v == 0 ? "0" : "%+d", v); out = b; return true; }
        return false;
    }

    void process(mpc::ProcessContext& ctx) override
    {
        const int n = ctx.numFrames;
        float* L = ctx.outputs[0];
        float* R = ctx.outputs[1];

        // ---- block settings
        const int nSing = std::max(1, std::min(kMaxSingers, getInt(kSingers)));
        const float spread = get(kSpread), drift = get(kDrift) * 0.01f;
        const float vibDepth = get(kVibrato) * 0.01f, vibRate = get(kVibRate);
        const float vibDelay = get(kVibDelay) * 0.001f * sr_;
        const float breath = get(kBreath) * 0.01f, cons = get(kConsonant) * 0.01f, bright = get(kBright) * 0.01f;
        const int wheelDest = getInt(kWheelDest);
        const float wheel = wheel_;
        float morph = get(kMorph) * 0.01f + (wheelDest == wMorph ? wheel : 0.0f);
        morph = clampf(morph, 0.0f, 1.0f);
        const int wave = getInt(kWave);
        const float cutoff = get(kCutoff), reso = get(kReso) * 0.01f, fltEnv = get(kFltEnv) * 0.01f, sub = get(kSub) * 0.01f;
        const float glideMs = get(kGlide);
        const float glideCoef = glideMs < 0.5f ? 1.0f : 1.0f - std::exp(-1.0f / (glideMs * 0.001f * sr_ / (float)kCtl));
        const float bendSt = bend_ * (float)getInt(kBendRange);
        const float octave = 12.0f * (float)getInt(kOctave);
        const float formantSt = get(kFormant) + (wheelDest == wFormant ? 12.0f * wheel : 0.0f);
        const float vibExtra = wheelDest == wVibrato ? wheel : 0.0f;
        const float open = clampf(get(kOpen) * 0.01f + (wheelDest == wOpen ? wheel : 0.0f), 0.0f, 1.0f);
        vowelLfoPh_ += get(kVowelRate) * (float)n / sr_; vowelLfoPh_ -= std::floor(vowelLfoPh_);
        const float vowelLfo = get(kVowelLfo) * 0.01f * 1.5f * std::sin(kTwoPi * vowelLfoPh_);
        const float vowelBase = get(kVowel) + vowelLfo + (wheelDest == wVowel ? 2.0f * wheel : 0.0f);
        const int section = getInt(kSection);
        const float a = get(kAttack), d = get(kDecay), s = get(kSustain) * 0.01f, r = get(kRelease);
        const float choirGain = 1.0f - morph, synthGain = morph;
        const float singNorm = 1.0f / std::sqrt((float)nSing);
        const float singNormNoise = std::sqrt((float)nSing) / (float)nSing * 1.2f;   // independent singers' noise adds like power
        const float srcCut = 600.0f + 9000.0f * bright * bright;
        const float width = get(kWidth) * 0.01f;

        handleMidi(ctx, a, d, s, r);

        // per-voice control-rate settings (filters, formants)
        for (auto& v : voice_)
        {
            if (!v.amp.isActive()) continue;
            v.amp.set(a, d, s, r);
            v.srcLp[0].set(srcCut, sr_); v.srcLp[1].set(srcCut * 1.07f, sr_);
            const float note = v.notePos + octave;
            // vowel: OPEN glides from "Mm" to the chosen vowel over the attack
            const float lvl = v.amp.level();
            const float vw = clampf(vowelBase * (1.0f - open * (1.0f - std::min(1.0f, lvl * 1.4f))), 0.0f, 5.0f);
            setFormants(v, section, note, vw, formantSt);
            const float fe = clampf(cutoff * std::pow(2.0f, fltEnv * 6.0f * lvl) , 30.0f, 18000.0f);
            v.lp[0].set(fe, 0.6f + reso * 9.0f, sr_); v.lp[1].set(fe * 1.01f, 0.6f + reso * 9.0f, sr_);
        }

        // master FX settings
        const float ens = get(kEnsemble) * 0.01f;
        const double bpm = (ctx.transport.bpm > 1.0 && ctx.transport.bpm < 999.0) ? ctx.transport.bpm : 120.0;
        int et = getInt(kEchoTime); if (et < 0) et = 0; if (et >= kNumEchoDivs) et = kNumEchoDivs - 1;
        const float echoD = clampf((float)(mpc::dsp::divisionBeats(kEchoDivs[et]) * 60.0 / bpm) * sr_, 64.0f, sr_ * 2.1f);
        const float echo = get(kEcho) * 0.01f, echoFb = get(kEchoFb) * 0.01f;
        const float hall = get(kHall) * 0.01f;
        hall_.set(0.5f + 0.5f * get(kHallSize) * 0.01f, 1.2f + 7.0f * get(kHallSize) * 0.01f, 0.45f, 0.5f, 18.0f, false);
        for (int c = 0; c < 2; ++c) { tilt_[c].set(1200.0f, sr_); echoLp_[c].set(4500.0f, sr_); }
        const float tone = get(kTone) * 0.01f;
        const float tLo = tone < 0 ? 1.0f - 0.5f * tone : 1.0f - 0.4f * tone, tHi = tone < 0 ? 1.0f + 0.9f * tone : 1.0f + 1.2f * tone;
        vol_.target = dbToGain(get(kVolume));

        for (int i = 0; i < n; ++i)
        {
            float mixL = 0.0f, mixR = 0.0f;
            for (auto& v : voice_)
            {
                if (!v.amp.isActive()) continue;
                // control rate: singer pitches (vibrato, drift, detune) ramp linearly over kCtl samples
                if (v.ctl-- <= 0)
                {
                    v.ctl = kCtl - 1;
                    v.notePos += glideCoef * (v.target - v.notePos);
                    const float baseHz = 440.0f * std::pow(2.0f, (v.notePos + octave + bendSt - 69.0f) / 12.0f);
                    const float baseInc = baseHz / sr_;
                    v.age += kCtl;
                    const float vibOn = clampf(((float)v.age - vibDelay) / (0.4f * sr_ + 1.0f), 0.0f, 1.0f);
                    const float vibC = (vibDepth + vibExtra) * 45.0f * vibOn;
                    for (int k = 0; k < nSing; ++k)
                    {
                        Singer& sg = v.sing[k];
                        sg.vibPh += sg.vibRate * vibRate * (float)kCtl / sr_; sg.vibPh -= std::floor(sg.vibPh);
                        if (--sg.driftCount <= 0) { sg.driftT = v.rng.bi(); sg.driftCount = 40 + (int)(v.rng.uni() * 200.0f); }
                        sg.drift += 0.02f * (sg.driftT - sg.drift);
                        const float c = sg.detune * spread + vibC * std::sin(kTwoPi * sg.vibPh) + drift * 18.0f * sg.drift;
                        sg.incTarget = baseInc * centsRatio(c);
                        sg.incStep = (sg.incTarget - sg.inc) * (1.0f / (float)kCtl);
                        if (sg.inc <= 0.0f) { sg.inc = sg.incTarget; sg.incStep = 0.0f; }
                    }
                    v.subInc = baseInc * 0.5f;
                }

                const float env = v.amp.process();
                const float g = env * v.velGain;
                // singers
                float srcL = 0.0f, srcR = 0.0f, rawL = 0.0f, rawR = 0.0f, puffL = 0.0f, puffR = 0.0f;
                for (int k = 0; k < nSing; ++k)
                {
                    Singer& sg = v.sing[k];
                    sg.inc += sg.incStep;
                    const float dt = sg.inc;
                    sg.ph += dt; if (sg.ph >= 1.0f) sg.ph -= 1.0f;
                    float saw = 2.0f * sg.ph - 1.0f - polyBlep(sg.ph, dt);
                    float raw = saw;
                    if (morph > 0.001f && wave != 0)
                    {
                        float p2 = sg.ph + 0.5f; if (p2 >= 1.0f) p2 -= 1.0f;
                        const float saw2 = 2.0f * p2 - 1.0f - polyBlep(p2, dt);
                        raw = wave == 1 ? (saw - saw2) * 0.6f : (saw + saw2) * 0.7f;   // pulse / hollow (odd-less)
                    }
                    // breath: noise that puffs with each glottal cycle (one noise per side, gated by every singer's cycle)
                    const float puff = sg.ph < 0.35f ? 1.0f : 0.35f;
                    if (k & 1) { srcR += saw; rawR += raw; puffR += puff; } else { srcL += saw; rawL += raw; puffL += puff; }
                }
                if (breath > 0.001f)
                {
                    const float vo = 1.0f - 0.85f * breath, bn = breath * 1.4f * singNormNoise;
                    srcL = srcL * vo + v.rng.bi() * bn * puffL;
                    srcR = srcR * vo + v.rng.bi() * bn * puffR;
                }
                if (nSing == 1) { srcR = srcL; rawR = rawL; }
                srcL *= singNorm; srcR *= singNorm; rawL *= singNorm; rawR *= singNorm;
                // consonant: a hiss / breath burst at the start of the note
                float cn = 0.0f;
                if (v.consLeft > 0) { --v.consLeft; cn = v.consHp.hp(v.rng.bi()) * cons * ((float)v.consLeft / v.consLen) * 0.8f; }

                float oL = 0.0f, oR = 0.0f;
                if (choirGain > 0.001f)
                {
                    const float xl = v.srcLp[0].lp(srcL), xr = v.srcLp[1].lp(srcR);
                    float fl = 0.0f, fr = 0.0f;
                    for (int f = 0; f < 5; ++f)
                    {
                        v.fm[0][f].process(xl); v.fm[1][f].process(xr);
                        fl += v.fm[0][f].bp * v.fGain[f];
                        fr += v.fm[1][f].bp * v.fGain[f];
                    }
                    oL += fl * choirGain * 2.2f; oR += fr * choirGain * 2.2f;
                }
                if (synthGain > 0.001f)
                {
                    oL += v.lp[0].processLP(rawL) * synthGain * 0.7f;
                    oR += v.lp[1].processLP(rawR) * synthGain * 0.7f;
                }
                if (sub > 0.001f)
                {
                    v.subPh += v.subInc; if (v.subPh >= 1.0f) v.subPh -= 1.0f;
                    const float sb = std::sin(kTwoPi * v.subPh) * sub * 0.5f;
                    oL += sb; oR += sb;
                }
                oL += cn; oR += cn;
                // per-voice placement across the stereo field
                mixL += g * (oL * (1.0f - v.pan * width) );
                mixR += g * (oR * (1.0f + v.pan * width) );
            }

            // ensemble: three slow modulated taps
            float yL = mixL, yR = mixR;
            if (ens > 0.001f)
            {
                ens_[0].write(mixL); ens_[1].write(mixR);
                ensPh_ += 0.33f / sr_; if (ensPh_ >= 1.0f) ensPh_ -= 1.0f;
                float el = 0.0f, er = 0.0f;
                for (int t = 0; t < 3; ++t)
                {
                    const float ph = ensPh_ + (float)t / 3.0f;
                    const float tri = 2.0f * std::fabs(ph - std::floor(ph) - 0.5f);
                    const float dly = (0.006f + 0.006f * tri) * sr_;
                    el += ens_[t & 1].readDelayLin(dly);
                    er += ens_[(t + 1) & 1].readDelayLin(dly * 1.11f);
                }
                yL += ens * 0.5f * el; yR += ens * 0.5f * er;
            }
            // echo (ping-pong, darkened)
            if (echo > 0.001f || echoActive_ > 0)
            {
                const float eL = echo_[0].readDelayLin(echoD), eR = echo_[1].readDelayLin(echoD);
                echo_[0].write(undenorm(0.5f * (yL + yR) * echo + echoLp_[1].lp(eR) * echoFb));
                echo_[1].write(undenorm(echoLp_[0].lp(eL) * echoFb));
                yL += eL; yR += eR;
                echoActive_ = echo > 0.001f ? (int)(sr_ * 8.0f) : echoActive_ - 1;
            }
            // hall
            if (hall > 0.001f)
            {
                float hl, hr;
                hall_.process(yL, yR, hl, hr);
                yL += hall * 0.9f * hl; yR += hall * 0.9f * hr;
            }
            // tone tilt + volume
            const float vg = vol_.next();
            const float lo0 = tilt_[0].lp(yL), lo1 = tilt_[1].lp(yR);
            yL = dc_[0].process(lo0 * tLo + (yL - lo0) * tHi) * vg;
            yR = dc_[1].process(lo1 * tLo + (yR - lo1) * tHi) * vg;
            if (!std::isfinite(yL) || !std::isfinite(yR)) { yL = yR = 0.0f; }
            L[i] = clampf(yL, -4.0f, 4.0f); R[i] = clampf(yR, -4.0f, 4.0f);
        }
    }

private:
    struct Singer
    {
        float ph = 0, inc = 0, incTarget = 0, incStep = 0, detune = 0, vibPh = 0, vibRate = 1, drift = 0, driftT = 0;
        int driftCount = 0;
    };
    struct Voice
    {
        mpc::dsp::ADSR amp;
        Singer sing[kMaxSingers];
        Svf fm[2][5], lp[2];
        float fGain[5] = {0, 0, 0, 0, 0};
        OnePole srcLp[2], consHp;
        Rng rng;
        int note = -1, ctl = 0, age = 0, consLeft = 0;
        float consLen = 1.0f;
        float notePos = 60.0f, target = 60.0f, velGain = 1.0f, pan = 0.0f, subPh = 0.0f, subInc = 0.0f;
        bool held = false, sustained = false;
        uint32_t stamp = 0;
        void seed(int k) { rng.seed(0xC0FFEEu + 7919u * (uint32_t)k); }
    };

    void setFormants(Voice& v, int section, float note, float vowel, float shiftSt)
    {
        // section blend: fixed, or by pitch (Auto)
        float sp;
        if (section >= secBass && section <= secSoprano) sp = (float)section;
        else sp = clampf((note - 43.0f) / 8.0f, 0.0f, 3.0f);    // ~G2 bass .. G4 soprano
        const int s0 = std::min((int)sp, 2), v0 = std::min((int)vowel, kNumVowels - 2);
        const float sf = sp - (float)s0, vf = vowel - (float)v0;
        const float shift = std::pow(2.0f, shiftSt / 12.0f);
        for (int f = 0; f < 5; ++f)
        {
            auto blend = [&](const float (Formant::*m)[5]) {
                const float a0 = (kTable[s0][v0].*m)[f] + vf * ((kTable[s0][v0 + 1].*m)[f] - (kTable[s0][v0].*m)[f]);
                const float a1 = (kTable[s0 + 1][v0].*m)[f] + vf * ((kTable[s0 + 1][v0 + 1].*m)[f] - (kTable[s0 + 1][v0].*m)[f]);
                return a0 + sf * (a1 - a0);
            };
            const float fq = clampf(blend(&Formant::f) * shift, 80.0f, sr_ * 0.42f);
            const float bw = std::max(blend(&Formant::bw) * (0.7f + 0.3f * shift), 30.0f);
            const float amp = dbToGain(blend(&Formant::a));
            v.fm[0][f].set(fq, fq / bw, sr_);
            v.fm[1][f].set(fq * 1.012f, fq / bw, sr_);
            v.fGain[f] = amp * v.fm[0][f].k;          // k = 1/Q normalises the band-pass peak to unity
        }
    }

    void handleMidi(mpc::ProcessContext& ctx, float a, float d, float s, float r)
    {
        for (int e = 0; e < ctx.numMidiEvents; ++e)
        {
            const mpc::MidiEvent& m = ctx.midi[e];
            if (m.isNoteOn()) noteOn(m.data[1], m.data[2], a, d, s, r);
            else if (m.isNoteOff()) noteOff(m.data[1]);
            else if (m.isPitchBend()) bend_ = (float)m.pitchBend() / 8192.0f;
            else if (m.isController())
            {
                if (m.controller() == 1) wheel_ = (float)m.data[2] / 127.0f;
                else if (m.controller() == 64)
                {
                    sustainPedal_ = m.data[2] >= 64;
                    if (!sustainPedal_) for (auto& v : voice_) if (v.sustained) { v.sustained = false; v.amp.noteOff(); }
                }
                else if (m.controller() == 123 || m.controller() == 120) for (auto& v : voice_) { v.amp.noteOff(); v.held = false; v.sustained = false; }
            }
        }
    }

    void noteOn(int note, int vel, float a, float d, float s, float r)
    {
        // reuse a voice already on this note, else a free one, else the oldest released, else the oldest
        Voice* pick = nullptr;
        for (auto& v : voice_) if (v.note == note && v.amp.isActive()) { pick = &v; break; }
        if (!pick) for (auto& v : voice_) if (!v.amp.isActive()) { pick = &v; break; }
        if (!pick) { for (auto& v : voice_) if (!v.held && (!pick || v.stamp < pick->stamp)) pick = &v; }
        if (!pick) { for (auto& v : voice_) if (!pick || v.stamp < pick->stamp) pick = &v; }
        Voice& v = *pick;
        const bool fresh = !v.amp.isActive();
        v.note = note; v.held = true; v.sustained = false; v.stamp = ++stamp_;
        v.target = (float)note;
        if (fresh)
        {
            v.notePos = (get(kGlide) >= 0.5f && lastNote_ >= 0.0f) ? lastNote_ : (float)note;
            v.age = 0; v.ctl = 0;
            for (int k = 0; k < kMaxSingers; ++k)
            {
                Singer& sg = v.sing[k];
                sg.ph = v.rng.uni(); sg.inc = 0.0f; sg.detune = v.rng.bi(); sg.vibPh = v.rng.uni();
                sg.vibRate = 0.85f + 0.3f * v.rng.uni(); sg.drift = v.rng.bi() * 0.5f; sg.driftCount = 0;
            }
            for (auto& ch : v.fm) for (auto& f : ch) f.reset();
            v.lp[0].reset(); v.lp[1].reset(); v.srcLp[0].reset(); v.srcLp[1].reset();
            v.pan = v.rng.bi() * 0.35f;
        }
        lastNote_ = (float)note;
        const float vs = get(kVelocity) * 0.01f;
        v.velGain = (1.0f - vs) + vs * ((float)vel / 127.0f) * ((float)vel / 127.0f) * 1.3f;
        v.consLen = sr_ * (0.03f + 0.04f * (1.0f - (float)vel / 127.0f));
        v.consLeft = (int)v.consLen;
        v.consHp.set(3500.0f, sr_);
        v.amp.set(a, d, s, r);
        v.amp.noteOn();
    }

    void noteOff(int note)
    {
        for (auto& v : voice_)
            if (v.note == note && v.held)
            {
                v.held = false;
                if (sustainPedal_) v.sustained = true; else v.amp.noteOff();
            }
    }

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
        #include "legion_presets.inc"
    }

    float sr_ = 44100.0f;
    Voice voice_[kMaxVoices];
    Ring ens_[2], echo_[2];
    Reverb hall_;
    OnePole tilt_[2], echoLp_[2];
    DcBlock dc_[2];
    Smooth vol_;
    float wheel_ = 0.0f, bend_ = 0.0f, ensPh_ = 0.0f, vowelLfoPh_ = 0.0f, lastNote_ = -1.0f;
    bool sustainPedal_ = false;
    uint32_t stamp_ = 0;
    int echoActive_ = 0;
};

MPC_PLUGIN_ENTRY(Legion)
