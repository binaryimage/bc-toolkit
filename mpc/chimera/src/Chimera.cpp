// Chimera.cpp - binaryimage's Chimera effect pedal, ported to MPC standalone.
//
// SPDX-License-Identifier: MIT
//
// The DSP is the desktop Chimera's own code (DspCommon.h, JunoChorus.h, TripleDelay.h,
// MultiReverb.h, copied unchanged). Only the master EQ is new: the desktop version used JUCE's
// filters, so here it is five plain RBJ biquads with the same curves.
//
// Signal flow (same as the desktop plugin):
//
//   in ──┬──────────────────────────────────────────────── DRY ─────┐
//        ├─► Juno chorus ──────────────────────────────── CHORUS ───┤
//        │        │ (Serial: chorus feeds the delay and reverb)     │
//        ├─► Triple delay A/B/C (serial or parallel) ───── DELAY ───┤──► mixer (level, pan,
//        │        │ (Serial: delay feeds the reverb)                │    mute, solo per channel)
//        └─► Multi reverb (8 types, freeze) ────────────── REVERB ──┘          │
//                                                                  5-band master EQ ─► Output
//
// Chain Routing = Serial: each effect also feeds the next (chorus -> delay -> reverb), like
// pedals in a row. Parallel: all three only hear the input.

#include "mpc_plugin.h"
#include "dsp/math.h"
#include "DspCommon.h"
#include "JunoChorus.h"
#include "TripleDelay.h"
#include "MultiReverb.h"

#include <cmath>

using namespace chimera;

namespace {

// Same skew as JUCE's setSkewForCentre(), so the knobs feel like the desktop plugin.
float skewFor(float lo, float hi, float centre)
{
    return std::log(0.5f) / std::log((centre - lo) / (hi - lo));
}

// Sync division length in quarter notes ("Free" = 0), from the desktop Parameters.h.
double syncBeats(int index)
{
    static const double beats[] = { 0.0, 0.125, 1.0 / 6.0, 0.25, 0.375, 1.0 / 3.0, 0.5, 0.75,
                                    2.0 / 3.0, 1.0, 1.5, 2.0, 3.0, 4.0 };
    return beats[index < 0 ? 0 : (index > 13 ? 13 : index)];
}

// RBJ Audio EQ Cookbook biquad (transposed direct form II).
struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void reset() { z1 = z2 = 0.0f; }
    float process(float x)
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        z1 = undenormal(z1); z2 = undenormal(z2);
        return y;
    }
    void set(double nb0, double nb1, double nb2, double na0, double na1, double na2)
    {
        b0 = (float)(nb0 / na0); b1 = (float)(nb1 / na0); b2 = (float)(nb2 / na0);
        a1 = (float)(na1 / na0); a2 = (float)(na2 / na0);
    }
    void highPass(double sr, double f, double q)
    {
        const double w = 2.0 * M_PI * f / sr, c = std::cos(w), al = std::sin(w) / (2.0 * q);
        set((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
    }
    void lowPass(double sr, double f, double q)
    {
        const double w = 2.0 * M_PI * f / sr, c = std::cos(w), al = std::sin(w) / (2.0 * q);
        set((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
    }
    void peak(double sr, double f, double q, double gainDb)
    {
        const double A = std::pow(10.0, gainDb / 40.0), w = 2.0 * M_PI * f / sr, c = std::cos(w), al = std::sin(w) / (2.0 * q);
        set(1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
    }
    void lowShelf(double sr, double f, double q, double gainDb)
    {
        const double A = std::pow(10.0, gainDb / 40.0), w = 2.0 * M_PI * f / sr, c = std::cos(w), s = std::sin(w);
        const double al = s / (2.0 * q), sq = 2.0 * std::sqrt(A) * al;
        set(A * ((A + 1) - (A - 1) * c + sq), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - sq),
            (A + 1) + (A - 1) * c + sq, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - sq);
    }
    void highShelf(double sr, double f, double q, double gainDb)
    {
        const double A = std::pow(10.0, gainDb / 40.0), w = 2.0 * M_PI * f / sr, c = std::cos(w), s = std::sin(w);
        const double al = s / (2.0 * q), sq = 2.0 * std::sqrt(A) * al;
        set(A * ((A + 1) + (A - 1) * c + sq), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sq),
            (A + 1) - (A - 1) * c + sq, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sq);
    }
};

struct EQSettings
{
    float hpf = 20, loF = 120, loG = 0, midF = 1000, midG = 0, midQ = 0.9f, hiF = 6000, hiG = 0, lpf = 20000;
    bool operator!=(const EQSettings& o) const
    {
        return hpf != o.hpf || loF != o.loF || loG != o.loG || midF != o.midF || midG != o.midG || midQ != o.midQ
            || hiF != o.hiF || hiG != o.hiG || lpf != o.lpf;
    }
};

// Five-band master EQ: low cut, low shelf, mid bell, high shelf, high cut.
class MasterEQ
{
public:
    void prepare(double sampleRate) { sr = sampleRate; current = EQSettings(); current.hpf = -1; apply(EQSettings()); reset(); }
    void reset() { for (auto& ch : f) for (auto& b : ch) b.reset(); }
    void setSettings(const EQSettings& s) { if (s != current) apply(s); }
    float process(int ch, float x) { for (auto& b : f[ch]) x = b.process(x); return x; }
private:
    void apply(const EQSettings& s)
    {
        current = s;
        const double nyq = sr * 0.45;
        auto lim = [nyq](double v) { return v < 10.0 ? 10.0 : (v > nyq ? nyq : v); };
        for (auto& ch : f)
        {
            ch[0].highPass(sr, lim(s.hpf), 0.707);
            ch[1].lowShelf(sr, lim(s.loF), 0.7, s.loG);
            ch[2].peak(sr, lim(s.midF), s.midQ, s.midG);
            ch[3].highShelf(sr, lim(s.hiF), 0.7, s.hiG);
            ch[4].lowPass(sr, lim(s.lpf), 0.707);
        }
    }
    double sr = 44100.0;
    EQSettings current;
    Biquad f[2][5];
};

} // namespace

// ---------------------------------------------------------------------------
// Parameter order is permanent once projects exist: append only.
// ---------------------------------------------------------------------------
enum P
{
    kOut = 0,                          // param 0: harmless on purpose (host nudges it on load)
    kChOn, kChMode, kChRate, kChDepth, kChWidth, kChTone, kChHiss,
    kDlOn, kDlRouting,
    kDlA,                              // 8 per unit: on, type, sync, time, fb, tone, mod, level
    kDlB = kDlA + 8,
    kDlC = kDlB + 8,
    kRvOn = kDlC + 8, kRvType, kRvFreeze, kRvDecay, kRvPre, kRvSize, kRvTone, kRvLow, kRvMod, kRvSpecial,
    kMxChain,
    kMx,                               // 4 per channel (dry, chorus, delay, reverb): level, pan, mute, solo
    kEqOn = kMx + 16, kEqHpf, kEqLoF, kEqLoG, kEqMidF, kEqMidG, kEqMidQ, kEqHiF, kEqHiG, kEqLpf,
    kNumParams
};
enum UnitOff { uOn = 0, uType, uSync, uTime, uFb, uTone, uMod, uLevel };
enum MixOff { mLevel = 0, mPan, mMute, mSolo };

class Chimera : public mpc::Plugin
{
public:
    static mpc::PluginInfo makeInfo()
    {
        mpc::PluginInfo i;
        i.name = "Chimera";
        i.vendor = "binaryimage";
        i.uniqueId = MPC_FOURCC('B', 'R', '0', 'w');   // must match "uid" in plugin.json
        i.version = 1000;
        i.isInstrument = false;
        i.numInputs = 2;
        i.numOutputs = 2;
        i.tailSamples = 0;   // unknown: depends on feedback and reverb decay
        return i;
    }

    Chimera() : mpc::Plugin(makeInfo())
    {
        using namespace mpc;
        const std::vector<std::string> modes = {"I", "II", "I+II"};
        const std::vector<std::string> dtypes = {"Digital", "Tape", "Analog", "Lo-Fi", "Mod", "Reverse", "Ping-Pong"};
        const std::vector<std::string> sync = {"Free", "1/32", "1/16T", "1/16", "1/16D", "1/8T", "1/8", "1/8D",
                                               "1/4T", "1/4", "1/4D", "1/2", "1/2D", "1 Bar"};
        const std::vector<std::string> routing = {"Serial", "Parallel"};
        const std::vector<std::string> rtypes = {"Room", "Hall", "Plate", "Spring", "Shimmer", "Cloud", "Bloom", "Swell"};

        addParam(floatParam("out_gain", "Output", -24.0f, 12.0f, 0.0f, "dB", 1));

        // Chorus
        addParam(boolParam ("ch_on",   "Chorus On", true));
        addParam(choiceParam("ch_mode","Cho Mode", modes, 0));
        addParam(floatParam("ch_rate", "Cho Rate", 0.25f, 4.0f, 1.0f, "", 2, skewFor(0.25f, 4.0f, 1.0f)));
        addParam(floatParam("ch_depth","Cho Depth", 0.0f, 200.0f, 100.0f, "%", 0));
        addParam(floatParam("ch_width","Cho Width", 0.0f, 100.0f, 100.0f, "%", 0));
        addParam(floatParam("ch_tone", "Cho Tone", 2000.0f, 16000.0f, 9000.0f, "Hz", 0, skewFor(2000.0f, 16000.0f, 7000.0f)));
        addParam(floatParam("ch_hiss", "Cho Hiss", 0.0f, 100.0f, 0.0f, "%", 0));

        // Triple delay
        addParam(boolParam ("dl_on",      "Delay On", true));
        addParam(choiceParam("dl_routing","Dly Routing", routing, 1));
        const int   defType[3] = {1, 0, 2};
        const float defTime[3] = {375.0f, 500.0f, 250.0f};
        const float defFb[3]   = {35.0f, 30.0f, 40.0f};
        const char* U[3] = {"A", "B", "C"};
        for (int u = 0; u < 3; ++u)
        {
            char id[24], nm[24];
            auto I = [&](const char* s) { std::snprintf(id, sizeof id, "dl%c_%s", "abc"[u], s); return id; };
            auto N = [&](const char* s) { std::snprintf(nm, sizeof nm, "%s %s", U[u], s); return nm; };
            addParam(boolParam (I("on"),   N("On"), u == 0));
            addParam(choiceParam(I("type"),N("Type"), dtypes, defType[u]));
            addParam(choiceParam(I("sync"),N("Sync"), sync, 0));
            addParam(floatParam(I("time"), N("Time"), 1.0f, 2000.0f, defTime[u], "", 0, skewFor(1.0f, 2000.0f, 350.0f)));
            addParam(floatParam(I("fb"),   N("Feedback"), 0.0f, 100.0f, defFb[u], "%", 0));
            addParam(floatParam(I("tone"), N("Tone"), 0.0f, 100.0f, 60.0f, "%", 0));
            addParam(floatParam(I("mod"),  N("Mod"), 0.0f, 100.0f, 20.0f, "%", 0));
            addParam(floatParam(I("level"),N("Level"), 0.0f, 100.0f, 80.0f, "%", 0));
        }

        // Reverb
        addParam(boolParam ("rv_on",      "Reverb On", true));
        addParam(choiceParam("rv_type",   "Rev Type", rtypes, 1));
        addParam(boolParam ("rv_freeze",  "Freeze", false));
        addParam(floatParam("rv_decay",   "Decay", 0.2f, 30.0f, 3.5f, "", 1, skewFor(0.2f, 30.0f, 3.0f)));
        addParam(floatParam("rv_predelay","Pre-Delay", 0.0f, 500.0f, 20.0f, "", 0, skewFor(0.0f, 500.0f, 80.0f)));
        addParam(floatParam("rv_size",    "Rev Size", 0.0f, 100.0f, 60.0f, "%", 0));
        addParam(floatParam("rv_tone",    "Rev Tone", 0.0f, 100.0f, 60.0f, "%", 0));
        addParam(floatParam("rv_lowend",  "Low End", -100.0f, 100.0f, 0.0f, "", 0));
        addParam(floatParam("rv_mod",     "Rev Mod", 0.0f, 100.0f, 40.0f, "%", 0));
        addParam(floatParam("rv_special", "Special", 0.0f, 100.0f, 50.0f, "%", 0));

        // Mixer
        addParam(choiceParam("mx_chain", "Chain", routing, 0));
        const char* C[4] = {"dry", "cho", "dly", "rev"};
        const char* CN[4] = {"Dry", "Cho", "Dly", "Rev"};
        const float defLevel[4] = {0.0f, 0.0f, -4.0f, -6.0f};
        for (int c = 0; c < 4; ++c)
        {
            char id[24], nm[24];
            auto I = [&](const char* s) { std::snprintf(id, sizeof id, "mx_%s_%s", C[c], s); return id; };
            auto N = [&](const char* s) { std::snprintf(nm, sizeof nm, "%s %s", CN[c], s); return nm; };
            addParam(floatParam(I("level"), N("Level"), -60.0f, 6.0f, defLevel[c], "dB", 1, skewFor(-60.0f, 6.0f, -12.0f)));
            addParam(floatParam(I("pan"),   N("Pan"), -100.0f, 100.0f, 0.0f, "", 0));
            addParam(boolParam (I("mute"),  N("Mute"), false));
            addParam(boolParam (I("solo"),  N("Solo"), false));
        }

        // Master EQ
        addParam(boolParam ("eq_on",      "EQ On", true));
        addParam(floatParam("eq_hpf",     "Low Cut", 20.0f, 1000.0f, 20.0f, "Hz", 0, skewFor(20.0f, 1000.0f, 120.0f)));
        addParam(floatParam("eq_lo_freq", "Low Freq", 40.0f, 1000.0f, 120.0f, "Hz", 0, skewFor(40.0f, 1000.0f, 200.0f)));
        addParam(floatParam("eq_lo_gain", "Low Gain", -15.0f, 15.0f, 0.0f, "dB", 1));
        addParam(floatParam("eq_mid_freq","Mid Freq", 150.0f, 8000.0f, 1000.0f, "Hz", 0, skewFor(150.0f, 8000.0f, 1000.0f)));
        addParam(floatParam("eq_mid_gain","Mid Gain", -15.0f, 15.0f, 0.0f, "dB", 1));
        addParam(floatParam("eq_mid_q",   "Mid Q", 0.3f, 6.0f, 0.9f, "", 2, skewFor(0.3f, 6.0f, 1.0f)));
        addParam(floatParam("eq_hi_freq", "High Freq", 1000.0f, 16000.0f, 6000.0f, "Hz", 0, skewFor(1000.0f, 16000.0f, 5000.0f)));
        addParam(floatParam("eq_hi_gain", "High Gain", -15.0f, 15.0f, 0.0f, "dB", 1));
        addParam(floatParam("eq_lpf",     "High Cut", 1000.0f, 20000.0f, 20000.0f, "Hz", 0, skewFor(1000.0f, 20000.0f, 6000.0f)));

        if (numParams() != kNumParams) mpc::log("Chimera: parameter count mismatch (%d vs %d)", numParams(), (int)kNumParams);
        addPrograms();
    }

    void prepare(double sampleRate, int /*maxBlockSize*/) override
    {
        sr_ = sampleRate;
        chorus_.prepare(sampleRate);
        delay_.prepare(sampleRate);
        reverb_.prepare(sampleRate);
        eq_.prepare(sampleRate);
        const float fs = (float)sampleRate;
        for (auto* s : {&chorusIn_, &delayIn_, &master_, &eqMix_}) s->prepare(20.0f, fs);
        for (int c = 0; c < 4; ++c) { feed_[c].prepare(25.0f, fs); outL_[c].prepare(25.0f, fs); outR_[c].prepare(25.0f, fs); }
        snapAll();
    }

    void reset() override
    {
        chorus_.reset();
        delay_.reset();          // these only clear buffers: safe on the audio thread
        reverb_.reset();
        eq_.reset();
        snapAll();
    }

    bool formatValue(int index, float plain, std::string& out) override
    {
        char b[32];
        auto hz = [&](float v) { if (v >= 1000.0f) std::snprintf(b, sizeof b, v >= 10000.0f ? "%.1fk" : "%.2fk", (double)(v * 0.001f)); else std::snprintf(b, sizeof b, "%.0f", (double)v); };
        if (index == kChTone || index == kEqHpf || index == kEqLoF || index == kEqMidF || index == kEqHiF || index == kEqLpf)
        { hz(plain); out = b; return true; }
        if (index == kChRate) { std::snprintf(b, sizeof b, "x%.2f", (double)plain); out = b; return true; }
        if (index == kDlA + uTime || index == kDlB + uTime || index == kDlC + uTime || index == kRvPre)
        {
            if (plain >= 1000.0f) std::snprintf(b, sizeof b, "%.2f s", (double)(plain * 0.001f));
            else std::snprintf(b, sizeof b, "%.0f ms", (double)plain);
            out = b; return true;
        }
        if (index == kRvDecay) { std::snprintf(b, sizeof b, plain < 10.0f ? "%.1f s" : "%.0f s", (double)plain); out = b; return true; }
        if (index == kRvLow) { std::snprintf(b, sizeof b, "%+.0f", (double)plain); out = b; return true; }
        for (int c = 0; c < 4; ++c)
        {
            if (index == kMx + c * 4 + mLevel && plain <= -59.9f) { out = "-inf"; return true; }
            if (index == kMx + c * 4 + mPan)
            {
                const int p = (int)std::lround(plain);
                if (p == 0) std::snprintf(b, sizeof b, "C"); else std::snprintf(b, sizeof b, p < 0 ? "L%d" : "R%d", p < 0 ? -p : p);
                out = b; return true;
            }
        }
        return false;
    }

    void process(mpc::ProcessContext& ctx) override
    {
        const int n = ctx.numFrames;
        float* L = ctx.outputs[0];
        float* R = ctx.outputs[1];
        const float* inL = ctx.inputs[0];
        const float* inR = ctx.numInputs > 1 ? ctx.inputs[1] : ctx.inputs[0];
        const double bpm = ctx.transport.bpm > 20.0 ? (ctx.transport.bpm < 400.0 ? ctx.transport.bpm : 400.0) : 120.0;

        chorus_.setParams(getInt(kChMode), get(kChRate), get(kChDepth) * 0.01f, get(kChWidth) * 0.01f, get(kChTone), get(kChHiss) * 0.01f);
        chorusIn_.target = getBool(kChOn) ? 1.0f : 0.0f;

        delay_.setSerial(getInt(kDlRouting) == 0);
        delayIn_.target = getBool(kDlOn) ? 1.0f : 0.0f;
        for (int u = 0; u < 3; ++u)
        {
            const int base = kDlA + u * 8;
            DelayUnitParams p;
            p.on = getBool(base + uOn);
            p.type = getInt(base + uType);
            const int sync = getInt(base + uSync);
            p.timeMs = sync > 0 ? (float)(syncBeats(sync) * 60000.0 / bpm) : get(base + uTime);
            p.fb = get(base + uFb) * 0.01f;
            p.tone = get(base + uTone) * 0.01f;
            p.mod = get(base + uMod) * 0.01f;
            p.level = get(base + uLevel) * 0.01f;
            delay_.setUnit(u, p);
        }
        {
            ReverbParams p;
            p.on = getBool(kRvOn); p.type = getInt(kRvType); p.freeze = getBool(kRvFreeze);
            p.decay = get(kRvDecay); p.predelay = get(kRvPre); p.size = get(kRvSize) * 0.01f;
            p.tone = get(kRvTone) * 0.01f; p.lowEnd = get(kRvLow) * 0.01f; p.mod = get(kRvMod) * 0.01f;
            p.special = get(kRvSpecial) * 0.01f;
            reverb_.setParams(p);
        }

        const bool series = getInt(kMxChain) == 0;
        bool anySolo = false;
        for (int c = 0; c < 4; ++c) anySolo |= getBool(kMx + c * 4 + mSolo);
        for (int c = 0; c < 4; ++c)
        {
            const int base = kMx + c * 4;
            const float lin = dbToGain(get(base + mLevel));
            const bool audible = !getBool(base + mMute) && (!anySolo || getBool(base + mSolo));
            const float pan = get(base + mPan) * 0.01f;
            feed_[c].target = lin;
            outL_[c].target = audible ? lin * (pan > 0.0f ? 1.0f - pan : 1.0f) : 0.0f;
            outR_[c].target = audible ? lin * (pan < 0.0f ? 1.0f + pan : 1.0f) : 0.0f;
        }

        EQSettings e;
        e.hpf = get(kEqHpf); e.loF = get(kEqLoF); e.loG = get(kEqLoG); e.midF = get(kEqMidF); e.midG = get(kEqMidG);
        e.midQ = get(kEqMidQ); e.hiF = get(kEqHiF); e.hiG = get(kEqHiG); e.lpf = get(kEqLpf);
        eq_.setSettings(e);
        eqMix_.target = getBool(kEqOn) ? 1.0f : 0.0f;
        master_.target = dbToGain(get(kOut));

        for (int i = 0; i < n; ++i)
        {
            const float xL = inL[i], xR = inR[i];

            const float cin = chorusIn_.next();
            float cL, cR;
            chorus_.process(xL * cin, xR * cin, cL, cR, cin);

            const float fC = feed_[1].next(), fD = feed_[2].next();
            feed_[0].next(); feed_[3].next();

            const float din = delayIn_.next();
            float dInL = xL, dInR = xR;
            if (series) { dInL += fC * cL; dInR += fC * cR; }
            float dL, dR;
            delay_.process(dInL * din, dInR * din, dL, dR);

            float rInL = xL, rInR = xR;
            if (series) { rInL += fC * cL + fD * dL; rInR += fC * cR + fD * dR; }
            float rL, rR;
            reverb_.process(rInL, rInR, rL, rR);

            const float srcL[4] = {xL, cL, dL, rL}, srcR[4] = {xR, cR, dR, rR};
            float mL = 0.0f, mR = 0.0f;
            for (int c = 0; c < 4; ++c) { mL += srcL[c] * outL_[c].next(); mR += srcR[c] * outR_[c].next(); }

            const float em = eqMix_.next();
            const float eL = eq_.process(0, mL), eR = eq_.process(1, mR);
            const float g = master_.next();
            float oL = (mL + em * (eL - mL)) * g;
            float oR = (mR + em * (eR - mR)) * g;
            if (!std::isfinite(oL) || !std::isfinite(oR)) { oL = oR = 0.0f; }
            L[i] = oL; R[i] = oR;
        }
    }

private:
    void snapAll()
    {
        chorusIn_.snap(getBool(kChOn) ? 1.0f : 0.0f);
        delayIn_.snap(getBool(kDlOn) ? 1.0f : 0.0f);
        eqMix_.snap(getBool(kEqOn) ? 1.0f : 0.0f);
        master_.snap(dbToGain(get(kOut)));
        for (int c = 0; c < 4; ++c) { feed_[c].snap(0.0f); outL_[c].snap(0.0f); outR_[c].snap(0.0f); }
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
        auto A = [](int off) { return kDlA + off; };
        auto B = [](int off) { return kDlB + off; };
        auto Cc = [](int off) { return kDlC + off; };
        auto MX = [](int ch, int off) { return kMx + ch * 4 + off; };
        enum { Dry = 0, Cho, Dly, Rev };

        #include "chimera_presets.inc"
    }

    double sr_ = 44100.0;
    JunoChorus chorus_;
    TripleDelay delay_;
    MultiReverb reverb_;
    MasterEQ eq_;
    Smooth chorusIn_, delayIn_, master_, eqMix_, feed_[4], outL_[4], outR_[4];
};

MPC_PLUGIN_ENTRY(Chimera)
