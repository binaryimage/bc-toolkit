// Mare.cpp - binaryimage's nightmare delay for the MPC.
//
// SPDX-License-Identifier: MIT
//
// A stereo (ping-pong) delay where every repeat is worse than the last. Inside the loop:
//   DRIFT   - each repeat is pitch-shifted again, so the echoes sink (or climb) as they go
//   REVERSE - each repeat may flip backwards (the odds per repeat)
//   ROT     - each repeat gets darker, grittier, more saturated and noisier
//   SMEAR   - allpass diffusion, so the repeats blur into a wash
//   WOBBLE  - wow on the delay time
// FREEZE holds whatever is in the loop forever; DUCK pushes the echoes down while you play.
//
//   in ─► + ─► delay L/R ─► drift ─► reverse ─► smear ─► rot ─► tone/low cut ─┬─► wet ─► mix ─► out
//         └──────────── feedback (crossed over for ping-pong) ◄───────────────┘

#include "mpc_plugin.h"
#include "dsp/tempo.h"
#include "bi_fx.h"
#include <cmath>
#include <cstdio>

using namespace bi;

enum P
{
    kOut = 0, kMix, kSync, kTime, kFeedback, kDrift, kRot, kReverse, kSmear, kWobble, kWobRate,
    kTone, kLowCut, kWidth, kDuck, kFreeze,
    kNumParams
};

namespace {
float skewFor(float lo, float hi, float centre) { return std::log(0.5f) / std::log((centre - lo) / (hi - lo)); }
const int kSyncDiv[] = {-1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
const int kNumSync = (int)(sizeof(kSyncDiv) / sizeof(kSyncDiv[0]));
const float kMaxDelaySec = 3.2f;

// Plays the loop signal backwards in chunks one repeat long (a reverse delay inside the loop).
struct Reverser
{
    Ring r;
    double pos = 0.0;
    int age = 0, len = 1;
    void prepare(float sr) { r.prepare((int)(sr * (kMaxDelaySec + 0.2f))); }
    void clear() { r.clear(); age = 0; len = 1; }
    float process(float x, int chunk)
    {
        r.write(x);
        if (age >= len) { len = std::max(chunk, 64); age = 0; pos = (double)(r.w - 1); }
        const float y = r.readAbs(pos);
        pos -= 1.0;
        const int fade = std::min(256, len / 4);
        float w = 1.0f;
        if (age < fade) w = (float)age / (float)fade;
        else if (len - age < fade) w = (float)(len - age) / (float)fade;
        ++age;
        return y * w;
    }
};

struct Allpass
{
    Ring r;
    void prepare(int n) { r.prepare(n + 4); }
    float process(float x, float d, float g)
    {
        const float z = r.readDelayLin(d);
        const float v = x + g * z;
        r.write(v);
        return z - g * v;
    }
};
}

class Mare : public mpc::Plugin
{
public:
    static mpc::PluginInfo makeInfo()
    {
        mpc::PluginInfo i;
        i.name = "Mare";
        i.vendor = "binaryimage";
        i.uniqueId = MPC_FOURCC('B', 'm', 'R', '2');   // must match "uid" in plugin.json
        i.version = 1000;
        i.isInstrument = false;
        i.numInputs = 2;
        i.numOutputs = 2;
        return i;
    }

    Mare() : mpc::Plugin(makeInfo())
    {
        using namespace mpc;
        std::vector<std::string> sync = {"Free"};
        for (int k = 1; k < kNumSync; ++k) sync.push_back(dsp::divisionName(kSyncDiv[k]));
        addParam(floatParam("out", "Output", -24.0f, 12.0f, 0.0f, "dB", 1));
        addParam(floatParam("mix", "Mix", 0.0f, 100.0f, 40.0f, "%", 0));
        addParam(choiceParam("sync", "Sync", sync, 8));        // 1/8D
        addParam(floatParam("time", "Free Time", 20.0f, 2000.0f, 375.0f, "", 0, skewFor(20.0f, 2000.0f, 350.0f)));
        addParam(floatParam("feedback", "Feedback", 0.0f, 100.0f, 55.0f, "%", 0));
        addParam(floatParam("drift", "Drift", -12.0f, 12.0f, 0.0f, "", 2, 1.0f));
        addParam(floatParam("rot", "Rot", 0.0f, 100.0f, 30.0f, "%", 0));
        addParam(floatParam("reverse", "Reverse", 0.0f, 100.0f, 0.0f, "%", 0));
        addParam(floatParam("smear", "Smear", 0.0f, 100.0f, 20.0f, "%", 0));
        addParam(floatParam("wobble", "Wobble", 0.0f, 100.0f, 20.0f, "%", 0));
        addParam(floatParam("wob_rate", "Wob Rate", 0.05f, 8.0f, 0.6f, "Hz", 2, skewFor(0.05f, 8.0f, 0.8f)));
        addParam(floatParam("tone", "Tone", 500.0f, 20000.0f, 8000.0f, "", 0, skewFor(500.0f, 20000.0f, 4000.0f)));
        addParam(floatParam("low_cut", "Low Cut", 20.0f, 1000.0f, 120.0f, "", 0, skewFor(20.0f, 1000.0f, 150.0f)));
        addParam(floatParam("width", "Ping-Pong", 0.0f, 100.0f, 60.0f, "%", 0));
        addParam(floatParam("duck", "Duck", 0.0f, 100.0f, 0.0f, "%", 0));
        addParam(boolParam("freeze", "Freeze", false));
        if (numParams() != kNumParams) mpc::log("Mare: parameter count mismatch");
        addPrograms();
    }

    void prepare(double sampleRate, int) override
    {
        sr_ = (float)sampleRate;
        for (int c = 0; c < 2; ++c)
        {
            line_[c].prepare((int)(sr_ * (kMaxDelaySec + 0.1f)));
            ps_[c].prepare(sr_, 120.0f);
            rev_[c].prepare(sr_);
            ap1_[c].prepare((int)(0.02f * sr_)); ap2_[c].prepare((int)(0.02f * sr_));
        }
        time_.prepare(120.0f, sr_); mix_.prepare(30.0f, sr_); out_.prepare(30.0f, sr_); fbS_.prepare(30.0f, sr_);
        revAmt_[0].prepare(15.0f, sr_); revAmt_[1].prepare(15.0f, sr_);
        reset();
    }

    void reset() override
    {
        for (int c = 0; c < 2; ++c)
        {
            line_[c].clear(); ps_[c].reset(); rev_[c].clear(); ap1_[c].r.clear(); ap2_[c].r.clear();
            tone_[c].reset(); lowcut_[c].reset(); dc_[c].reset(); fbSig_[c] = 0.0f; hold_[c] = 0.0f; holdCount_[c] = 0.0f;
            revAmt_[c].snap(0.0f); repeatCount_[c] = 0;
        }
        duckEnv_.reset(); wobPh_ = 0.0f;
        time_.snap(delaySamples(120.0)); mix_.snap(get(kMix) * 0.01f); out_.snap(dbToGain(get(kOut))); fbS_.snap(get(kFeedback) * 0.01f);
    }

    bool formatValue(int index, float plain, std::string& out) override
    {
        char b[32];
        if (index == kTime)
        {
            if (plain >= 1000.0f) std::snprintf(b, sizeof b, "%.2f s", (double)(plain * 0.001f));
            else std::snprintf(b, sizeof b, "%.0f ms", (double)plain);
            out = b; return true;
        }
        if (index == kTone || index == kLowCut)
        {
            if (index == kTone && plain >= 19900.0f) { out = "Open"; return true; }
            if (plain >= 1000.0f) std::snprintf(b, sizeof b, "%.1f kHz", (double)(plain * 0.001f)); else std::snprintf(b, sizeof b, "%.0f Hz", (double)plain);
            out = b; return true;
        }
        if (index == kDrift)
        {
            std::snprintf(b, sizeof b, std::fabs(plain) < 0.005f ? "0 st" : "%+.2f st", (double)plain);
            out = b; return true;
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

        const double bpm = (ctx.transport.bpm > 1.0 && ctx.transport.bpm < 999.0) ? ctx.transport.bpm : 120.0;
        time_.target = delaySamples(bpm);
        const bool freeze = getBool(kFreeze);
        fbS_.target = freeze ? 1.0f : std::min(get(kFeedback) * 0.01f * 1.02f, 1.02f);
        const float drift = get(kDrift);
        const float ratio = semis(drift);
        const bool doDrift = std::fabs(drift) > 0.005f;
        const float rot = freeze ? 0.0f : get(kRot) * 0.01f;
        const float revP = get(kReverse) * 0.01f, smear = get(kSmear) * 0.01f;
        const float wob = get(kWobble) * 0.01f, wobInc = get(kWobRate) / sr_;
        const float width = get(kWidth) * 0.01f, duck = get(kDuck) * 0.01f;
        for (int c = 0; c < 2; ++c)
        {
            tone_[c].set(freeze ? 20000.0f : get(kTone) * (1.0f - 0.55f * rot), 0.6f, sr_);
            lowcut_[c].set(freeze ? 20.0f : get(kLowCut), sr_);
        }
        duckEnv_.set(3.0f, 250.0f, sr_);
        mix_.target = get(kMix) * 0.01f; out_.target = dbToGain(get(kOut));
        const float bits = 16.0f - 9.0f * rot * rot;
        const float quant = std::pow(2.0f, bits - 1.0f);
        const float holdLen = 1.0f + 6.0f * rot * rot * rot;
        const float sat = 1.0f + 2.5f * rot;
        const float hiss = rot * rot * 0.004f;
        const float apD1 = 0.0071f * sr_, apD2 = 0.0113f * sr_, apG = 0.72f * smear;

        for (int i = 0; i < n; ++i)
        {
            const float xL = inL[i], xR = inR[i];
            const float d = time_.next(), fb = fbS_.next();
            wobPh_ += wobInc; if (wobPh_ >= 1.0f) wobPh_ -= 1.0f;
            const float wobD = wob * 0.006f * sr_ * (0.5f + 0.5f * std::sin(kTwoPi * wobPh_));
            const int chunk = (int)d;

            float wet[2];
            for (int c = 0; c < 2; ++c)
            {
                float y = line_[c].readDelay(d + wobD + (c ? 0.0f : 1.0f));
                if (doDrift) y = ps_[c].process(y, ratio, std::min(0.08f * sr_, 0.5f * d), 0.1f);
                // reverse: decided once per repeat
                if (revP > 0.0f || revAmt_[c].v > 0.0001f)
                {
                    if (++repeatCount_[c] >= chunk) { repeatCount_[c] = 0; revAmt_[c].target = rng_.chance(revP) ? 1.0f : 0.0f; }
                    const float r = rev_[c].process(y, chunk);
                    const float a = revAmt_[c].next();
                    y += a * (r - y);
                }
                if (smear > 0.001f) y = ap2_[c].process(ap1_[c].process(y, apD1 * (c ? 1.13f : 1.0f), apG), apD2 * (c ? 0.91f : 1.0f), apG);
                // rot: crushed, saturated, hissy
                if (rot > 0.001f)
                {
                    holdCount_[c] += 1.0f;
                    if (holdCount_[c] >= holdLen) { holdCount_[c] -= holdLen; hold_[c] = std::floor(y * quant + 0.5f) / quant; }
                    y = fastTanh(hold_[c] * sat) / sat * (1.0f + 0.3f * rot) + rng_.bi() * hiss * clampf(std::fabs(y) * 40.0f, 0.0f, 1.0f);
                }
                y = lowcut_[c].hp(tone_[c].processLP(y));
                wet[c] = dc_[c].process(y);
            }
            // feedback with ping-pong crossover
            const float inMono = 0.5f * (xL + xR);
            const float feedIn = freeze ? 0.0f : 1.0f;
            const float aL = feedIn * (xL + width * (inMono - xL));
            const float aR = feedIn * (xR * (1.0f - width));
            const float fL = wet[0] * (1.0f - width) + wet[1] * width;
            const float fR = wet[1] * (1.0f - width) + wet[0] * width;
            line_[0].write(undenorm(aL + softSat(fL * fb)));
            line_[1].write(undenorm(aR + softSat(fR * fb)));

            const float lvl = duckEnv_.process(inMono);
            const float dk = 1.0f - duck * clampf(lvl * 4.0f, 0.0f, 1.0f);
            const float m = mix_.next(), og = out_.next();
            // dry stays full up to 50 %, the echoes come up to full by 50 %
            const float dg = std::min(1.0f, 2.0f * (1.0f - m)), wg = std::min(1.0f, 2.0f * m) * dk * 1.2f;
            float yL = (xL * dg + wet[0] * wg) * og, yR = (xR * dg + wet[1] * wg) * og;
            if (!std::isfinite(yL) || !std::isfinite(yR)) { yL = yR = 0.0f; line_[0].clear(); line_[1].clear(); }
            L[i] = clampf(yL, -4.0f, 4.0f); R[i] = clampf(yR, -4.0f, 4.0f);
        }
    }

private:
    static float softSat(float x) { return std::fabs(x) < 0.8f ? x : fastTanh(x); }

    float delaySamples(double bpm)
    {
        int sync = getInt(kSync); if (sync < 0) sync = 0; if (sync >= kNumSync) sync = kNumSync - 1;
        float sec = sync > 0 ? (float)(mpc::dsp::divisionBeats(kSyncDiv[sync]) * 60.0 / bpm) : get(kTime) * 0.001f;
        return clampf(sec, 0.005f, kMaxDelaySec) * sr_;
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
        #include "mare_presets.inc"
    }

    float sr_ = 44100.0f;
    Ring line_[2];
    PitchShifter ps_[2];
    Reverser rev_[2];
    Allpass ap1_[2], ap2_[2];
    Svf tone_[2];
    OnePole lowcut_[2];
    DcBlock dc_[2];
    EnvFollow duckEnv_;
    Smooth time_, mix_, out_, fbS_, revAmt_[2];
    Rng rng_;
    float fbSig_[2] = {0, 0}, hold_[2] = {0, 0}, holdCount_[2] = {0, 0}, wobPh_ = 0.0f;
    int repeatCount_[2] = {0, 0};
};

MPC_PLUGIN_ENTRY(Mare)
