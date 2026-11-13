// Fenrir.cpp - binaryimage's brutal distortion for the MPC.
//
// SPDX-License-Identifier: MIT
//
//   in ─► gate detector ─────────────────────────────────────────────────┐
//    └─► tight (low cut) ─► BITE (resonant pre-emphasis) ─► + HOWL feedback   │
//          ─► [2x oversampled] stage 1 (6 types) ─► stage 2 (6 types)          │
//          ─► post EQ (low, mid, high cut) ─► noise gate ◄────────────────┘
//          ─► CHOP (rhythmic gate patterns on the MPC's clock) ─► mix ─► out
//
// STARVE sags the bias with the playing level, so notes sputter and spit as they die.
// HOWL feeds the output back through a band-pass at the bite frequency: it screams.

#include "mpc_plugin.h"
#include "dsp/tempo.h"
#include "bi_fx.h"
#include <cmath>
#include <cstdio>

using namespace bi;

enum P
{
    kOut = 0, kMix, kTight, kDrive1, kType1, kDrive2, kType2, kBite, kBiteFreq, kStarve, kGate,
    kChop, kChopRate, kChopDepth, kChopShape, kLow, kMid, kHighCut, kWidth, kHowl,
    kNumParams
};
enum Type { tTube = 0, tFuzz, tRectify, tFold, tDiode, tCrush, tNumTypes };

namespace {
float skewFor(float lo, float hi, float centre) { return std::log(0.5f) / std::log((centre - lo) / (hi - lo)); }
// 16-step chop patterns (1 = open)
const uint16_t kPatterns[] = {
    0xFFFF,   // Off
    0xAAAA,   // Pulse     x.x.x.x.x.x.x.x.
    0xDDDD,   // Gallop    xx.xxx.xxx.xxx.x
    0x2222,   // Upbeat    ..x...x...x...x.
    0xB6DB,   // Machine   x.xx.xx.xx.xx.xx
    0x9249,   // Broken    x..x..x..x..x..x
    0xF0EA,   // Stutter   xxxx....xxx.x.x.
    0x0000,   // Random
};
const int kNumPatterns = 8;
const int kChopDivs[] = {2, 4, 7, 10};   // 1/32 1/16 1/8 1/4

inline float shape(int type, float x, float bias)
{
    switch (type)
    {
    case tTube:    return fastTanh(x + 0.25f + bias) - fastTanh(0.25f + bias);
    case tFuzz:  { const float u = clampf(x * 1.4f + bias, -1.0f, 1.0f); return 1.5f * u - 0.5f * u * u * u; }
    case tRectify:{ const float t = fastTanh(x + bias); return t > 0.0f ? t : -0.35f * t; }
    case tFold:  { const float t = (x + bias) * 0.25f + 0.25f; return 4.0f * std::fabs(t - std::floor(t + 0.5f)) - 1.0f; }
    case tDiode:   return clampf(x + bias, -0.55f, 0.75f) * 1.4f;
    default:     { const float t = fastTanh(x + bias); return std::floor(t * 6.0f + 0.5f) * (1.0f / 6.0f); }
    }
}
}

class Fenrir : public mpc::Plugin
{
public:
    static mpc::PluginInfo makeInfo()
    {
        mpc::PluginInfo i;
        i.name = "Fenrir";
        i.vendor = "binaryimage";
        i.uniqueId = MPC_FOURCC('B', 'f', 'N', '9');   // must match "uid" in plugin.json
        i.version = 1000;
        i.isInstrument = false;
        i.numInputs = 2;
        i.numOutputs = 2;
        return i;
    }

    Fenrir() : mpc::Plugin(makeInfo())
    {
        using namespace mpc;
        const std::vector<std::string> types = {"Tube", "Fuzz", "Rectify", "Fold", "Diode", "Crush"};
        addParam(floatParam("out", "Output", -24.0f, 12.0f, 0.0f, "dB", 1));
        addParam(floatParam("mix", "Mix", 0.0f, 100.0f, 100.0f, "%", 0));
        addParam(floatParam("tight", "Tight", 20.0f, 600.0f, 60.0f, "", 0, skewFor(20.0f, 600.0f, 120.0f)));
        addParam(floatParam("drive1", "Drive 1", 0.0f, 100.0f, 50.0f, "%", 0));
        addParam(choiceParam("type1", "Type 1", types, tTube));
        addParam(floatParam("drive2", "Drive 2", 0.0f, 100.0f, 0.0f, "%", 0));
        addParam(choiceParam("type2", "Type 2", types, tFuzz));
        addParam(floatParam("bite", "Bite", 0.0f, 100.0f, 40.0f, "%", 0));
        addParam(floatParam("bite_freq", "Bite Freq", 400.0f, 5000.0f, 1800.0f, "", 0, skewFor(400.0f, 5000.0f, 1500.0f)));
        addParam(floatParam("starve", "Starve", 0.0f, 100.0f, 0.0f, "%", 0));
        addParam(floatParam("gate", "Gate", -80.0f, -10.0f, -80.0f, "", 0));
        addParam(choiceParam("chop", "Chop", {"Off", "Pulse", "Gallop", "Upbeat", "Machine", "Broken", "Stutter", "Random"}, 0));
        addParam(choiceParam("chop_rate", "Chop Rate", {"1/32", "1/16", "1/8", "1/4"}, 1));
        addParam(floatParam("chop_depth", "Chop Depth", 0.0f, 100.0f, 100.0f, "%", 0));
        addParam(floatParam("chop_shape", "Chop Shape", 0.0f, 100.0f, 20.0f, "%", 0));
        addParam(floatParam("low", "Low", -12.0f, 12.0f, 0.0f, "dB", 1));
        addParam(floatParam("mid", "Mid", -12.0f, 12.0f, 0.0f, "dB", 1));
        addParam(floatParam("high_cut", "High Cut", 1000.0f, 20000.0f, 9000.0f, "", 0, skewFor(1000.0f, 20000.0f, 6000.0f)));
        addParam(floatParam("width", "Width", 0.0f, 100.0f, 0.0f, "%", 0));
        addParam(floatParam("howl", "Howl", 0.0f, 100.0f, 0.0f, "%", 0));
        if (numParams() != kNumParams) mpc::log("Fenrir: parameter count mismatch");
        addPrograms();
    }

    void prepare(double sampleRate, int) override
    {
        sr_ = (float)sampleRate;
        mix_.prepare(30.0f, sr_); out_.prepare(30.0f, sr_); g1_.prepare(30.0f, sr_); g2_.prepare(30.0f, sr_);
        reset();
    }

    void reset() override
    {
        for (int c = 0; c < 2; ++c)
        {
            tight_[c].reset(); tight2_[c].reset(); bite_[c].reset(); howlBp_[c].reset(); aa1_[c].reset(); aa2_[c].reset();
            low_[c].reset(); mid_[c].reset(); dcMid_[c].reset(); hc_[c].reset(); hc2_[c].reset(); dc_[c].reset(); sag_[c].reset();
            prevIn_[c] = 0.0f; howl_[c] = 0.0f; crushHold_[c] = 0.0f;
        }
        gateEnv_.reset(); gateGain_ = 1.0f; chopGain_ = 1.0f; chopIdx_ = 0; chopOpen_ = true; clock_.reset();
        mix_.snap(get(kMix) * 0.01f); out_.snap(dbToGain(get(kOut)));
        g1_.snap(driveGain(get(kDrive1))); g2_.snap(driveGain(get(kDrive2)));
    }

    bool formatValue(int index, float plain, std::string& out) override
    {
        char b[32];
        if (index == kTight || index == kBiteFreq || index == kHighCut)
        {
            if (index == kHighCut && plain >= 19900.0f) { out = "Open"; return true; }
            if (plain >= 1000.0f) std::snprintf(b, sizeof b, "%.1f kHz", (double)(plain * 0.001f)); else std::snprintf(b, sizeof b, "%.0f Hz", (double)plain);
            out = b; return true;
        }
        if (index == kGate)
        {
            if (plain <= -79.5f) out = "Off"; else { std::snprintf(b, sizeof b, "%.0f dB", (double)plain); out = b; }
            return true;
        }
        return false;
    }

    void process(mpc::ProcessContext& ctx) override
    {
        const int n = ctx.numFrames;
        const float* inL = ctx.inputs[0];
        const float* inR = ctx.numInputs > 1 ? ctx.inputs[1] : ctx.inputs[0];
        float* L = ctx.outputs[0];
        float* R = ctx.outputs[1];

        const int t1 = clampi(getInt(kType1)), t2 = clampi(getInt(kType2));
        const float d2 = get(kDrive2) * 0.01f;
        const float bite = get(kBite) * 0.01f, starve = get(kStarve) * 0.01f, width = get(kWidth) * 0.01f;
        const float howl = get(kHowl) * 0.01f;
        const float gateThr = dbToGain(get(kGate)), gateOn = get(kGate) > -79.5f ? 1.0f : 0.0f;
        int chop = getInt(kChop); if (chop < 0 || chop >= kNumPatterns) chop = 0;
        int crate = getInt(kChopRate); if (crate < 0 || crate > 3) crate = 1;
        const float chopDepth = get(kChopDepth) * 0.01f;
        const float chopCoef = 1.0f - std::exp(-1.0f / ((0.4f + 25.0f * get(kChopShape) * 0.01f) * 0.001f * sr_));
        const float lowG = dbToGain(get(kLow)) - 1.0f, midG = dbToGain(get(kMid)) - 1.0f;
        clock_.begin(ctx, mpc::dsp::divisionBeats(kChopDivs[crate]), sr_ * 0.125f, true);
        for (int c = 0; c < 2; ++c)
        {
            const float spread = c == 0 ? 1.0f : 1.0f + 0.18f * width;
            tight_[c].set(get(kTight), 0.707f, sr_);
            tight2_[c].set(get(kTight), sr_);
            bite_[c].set(get(kBiteFreq) * spread, 1.3f + 2.0f * bite, sr_);
            howlBp_[c].set(get(kBiteFreq) * spread * 0.5f, 6.0f, sr_);
            aa1_[c].set(17000.0f, 0.54f, sr_ * 2.0f); aa2_[c].set(17000.0f, 1.31f, sr_ * 2.0f);
            low_[c].set(140.0f, sr_);
            mid_[c].set(750.0f, 0.8f, sr_);
            hc_[c].set(get(kHighCut), 0.707f, sr_); hc2_[c].set(std::min(get(kHighCut) * 1.4f, 20000.0f), sr_);
            sag_[c].set(2.0f, 80.0f, sr_);
        }
        gateEnv_.set(0.5f, 40.0f, sr_);
        mix_.target = get(kMix) * 0.01f; out_.target = dbToGain(get(kOut));
        g1_.target = driveGain(get(kDrive1)); g2_.target = driveGain(get(kDrive2));
        const float comp1 = 1.0f, comp2 = 1.0f / (1.0f + 0.25f * d2);

        for (int i = 0; i < n; ++i)
        {
            const float x[2] = {inL[i], inR[i]};
            const float g1 = g1_.next(), g2 = g2_.next();

            // gate opens on the input level (before the dirt, so the noise floor can't hold it open)
            const float lvl = gateEnv_.process(0.5f * (x[0] + x[1]));
            const float gt = gateOn > 0.0f ? (lvl > gateThr ? 1.0f : 0.0f) : 1.0f;
            gateGain_ += (gt > gateGain_ ? 0.02f : 0.0012f) * (gt - gateGain_);

            // chop
            if (clock_.tick())
            {
                chopIdx_ = clock_.locked ? (int)(clock_.lastStep & 15) : ((chopIdx_ + 1) & 15);
                if (chop == kNumPatterns - 1) chopOpen_ = rng_.chance(0.55f);
                else chopOpen_ = (kPatterns[chop] >> (15 - chopIdx_)) & 1;
            }
            const float ct = chop == 0 ? 1.0f : (chopOpen_ ? 1.0f : 1.0f - chopDepth);
            chopGain_ += chopCoef * (ct - chopGain_);

            float y[2];
            for (int c = 0; c < 2; ++c)
            {
                float s = tight_[c].processHP(x[c]);
                s = tight2_[c].hp(s);
                s += bite * 2.5f * bite_[c].processBP(s);
                s += howl * 1.8f * howl_[c] * clampf(lvl * 30.0f, 0.0f, 1.0f);   // howl dies when you stop playing
                const float bias = (c == 0 ? 0.12f : -0.12f) * width - starve * 2.5f * sag_[c].process(s);
                // 2x oversampled stages: midpoint then sample
                float o = 0.0f;
                for (int k = 0; k < 2; ++k)
                {
                    const float u = k == 0 ? 0.5f * (prevIn_[c] + s) : s;
                    float v = shape(t1, u * g1, bias) * comp1;
                    if (d2 > 0.001f) v = shape(t2, dcMid_[c].process(v) * g2, 0.0f) * comp2;
                    v = aa2_[c].processLP(aa1_[c].processLP(v));
                    o = v;
                }
                prevIn_[c] = s;
                o = dc_[c].process(o);
                // post EQ
                o += lowG * low_[c].lp(o);
                o += midG * mid_[c].processBP(o);
                o = hc2_[c].lp(hc_[c].processLP(o));
                howl_[c] = fastTanh(howlBp_[c].processBP(o) * 2.0f);
                y[c] = o * gateGain_ * chopGain_ * 0.5f;
            }

            const float m = mix_.next(), og = out_.next();
            float yL = (x[0] + m * (y[0] - x[0])) * og, yR = (x[1] + m * (y[1] - x[1])) * og;
            if (!std::isfinite(yL) || !std::isfinite(yR)) { yL = yR = 0.0f; howl_[0] = howl_[1] = 0.0f; }
            L[i] = clampf(yL, -4.0f, 4.0f); R[i] = clampf(yR, -4.0f, 4.0f);
        }
    }

private:
    static int clampi(int t) { return t < 0 ? 0 : (t >= tNumTypes ? tNumTypes - 1 : t); }
    static float driveGain(float pct) { return dbToGain(pct * 0.48f); }   // 0..48 dB

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
        #include "fenrir_presets.inc"
    }

    float sr_ = 44100.0f;
    Svf tight_[2], bite_[2], howlBp_[2], aa1_[2], aa2_[2], mid_[2], hc_[2];
    OnePole tight2_[2], low_[2], hc2_[2];
    DcBlock dc_[2], dcMid_[2];
    EnvFollow sag_[2], gateEnv_;
    Smooth mix_, out_, g1_, g2_;
    Rng rng_;
    StepClock clock_;
    float prevIn_[2] = {0, 0}, howl_[2] = {0, 0}, crushHold_[2] = {0, 0};
    float gateGain_ = 1.0f, chopGain_ = 1.0f;
    int chopIdx_ = 0;
    bool chopOpen_ = true;
};

MPC_PLUGIN_ENTRY(Fenrir)
