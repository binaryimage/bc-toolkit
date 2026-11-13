// Banshee.cpp - binaryimage's sorrow chorus for the MPC.
//
// SPDX-License-Identifier: MIT
//
// A wide, detuned, wailing chorus that never sits still:
//   VOICES     - 2 to 6 modulated delay taps, spread across the stereo field
//   RATE/DEPTH - the classic chorus sweep, DRIFT adds a slow seasick random wander per voice
//   DETUNE     - true pitch detune (cents) on top, so it's sour even when the sweep is still
//   WAIL       - a pitch-shifted ghost (octave up by default) with its own vibrato
//   CRY        - a sweeping vowel resonance, so the whole thing moans
//   AIR        - breath noise that follows your playing
//   SPACE      - a dark reverb behind it all
//
//   in ─► chorus voices (sweep + drift) ─► detune L/R ─► + wail ─► cry formants ─► + air
//        ─► low cut / tone ─► + space ─► mix ─► out

#include "mpc_plugin.h"
#include "bi_fx.h"
#include <cmath>
#include <cstdio>

using namespace bi;

enum P
{
    kOut = 0, kMix, kVoices, kRate, kDepth, kDrift, kDetune, kWail, kWailPitch, kCry, kCryRate,
    kAir, kFeedback, kWidth, kSpace, kTone, kLowCut,
    kNumParams
};

namespace {
float skewFor(float lo, float hi, float centre) { return std::log(0.5f) / std::log((centre - lo) / (hi - lo)); }
const int kVoiceCounts[] = {2, 3, 4, 6};
const int kMaxVoices = 6;
}

class Banshee : public mpc::Plugin
{
public:
    static mpc::PluginInfo makeInfo()
    {
        mpc::PluginInfo i;
        i.name = "Banshee";
        i.vendor = "binaryimage";
        i.uniqueId = MPC_FOURCC('B', 'b', 'S', '6');   // must match "uid" in plugin.json
        i.version = 1000;
        i.isInstrument = false;
        i.numInputs = 2;
        i.numOutputs = 2;
        return i;
    }

    Banshee() : mpc::Plugin(makeInfo())
    {
        using namespace mpc;
        addParam(floatParam("out", "Output", -24.0f, 12.0f, 0.0f, "dB", 1));
        addParam(floatParam("mix", "Mix", 0.0f, 100.0f, 50.0f, "%", 0));
        addParam(choiceParam("voices", "Voices", {"2", "3", "4", "6"}, 2));
        addParam(floatParam("rate", "Rate", 0.03f, 5.0f, 0.4f, "Hz", 2, skewFor(0.03f, 5.0f, 0.5f)));
        addParam(floatParam("depth", "Depth", 0.0f, 100.0f, 50.0f, "%", 0));
        addParam(floatParam("drift", "Drift", 0.0f, 100.0f, 40.0f, "%", 0));
        addParam(floatParam("detune", "Detune", 0.0f, 50.0f, 12.0f, "ct", 0));
        addParam(floatParam("wail", "Wail", 0.0f, 100.0f, 0.0f, "%", 0));
        addParam(floatParam("wail_pitch", "Wail Pitch", -12.0f, 24.0f, 12.0f, "", 0));
        addParam(floatParam("cry", "Cry", 0.0f, 100.0f, 20.0f, "%", 0));
        addParam(floatParam("cry_rate", "Cry Rate", 0.02f, 4.0f, 0.2f, "Hz", 2, skewFor(0.02f, 4.0f, 0.3f)));
        addParam(floatParam("air", "Air", 0.0f, 100.0f, 10.0f, "%", 0));
        addParam(floatParam("feedback", "Feedback", 0.0f, 90.0f, 0.0f, "%", 0));
        addParam(floatParam("width", "Width", 0.0f, 100.0f, 100.0f, "%", 0));
        addParam(floatParam("space", "Space", 0.0f, 100.0f, 25.0f, "%", 0));
        addParam(floatParam("tone", "Tone", 1000.0f, 20000.0f, 12000.0f, "", 0, skewFor(1000.0f, 20000.0f, 6000.0f)));
        addParam(floatParam("low_cut", "Low Cut", 20.0f, 800.0f, 80.0f, "", 0, skewFor(20.0f, 800.0f, 150.0f)));
        if (numParams() != kNumParams) mpc::log("Banshee: parameter count mismatch");
        addPrograms();
    }

    void prepare(double sampleRate, int) override
    {
        sr_ = (float)sampleRate;
        for (auto& d : line_) d.prepare((int)(sr_ * 0.06f));
        for (auto& p : det_) p.prepare(sr_, 60.0f);
        wailPs_.prepare(sr_, 80.0f);
        reverb_.prepare(sr_);
        mix_.prepare(30.0f, sr_); out_.prepare(30.0f, sr_);
        for (auto& g : vGain_) g.prepare(40.0f, sr_);
        reset();
    }

    void reset() override
    {
        for (auto& d : line_) d.clear();
        for (auto& p : det_) p.reset();
        wailPs_.reset(); reverb_.reset();
        for (int c = 0; c < 2; ++c) { f1_[c].reset(); f2_[c].reset(); tone_[c].reset(); low_[c].reset(); airBp_[c].reset(); dc_[c].reset(); fb_[c] = 0.0f; }
        env_.reset(); lfo_ = 0.0f; cryPh_ = 0.0f; vibPh_ = 0.0f;
        for (int k = 0; k < kMaxVoices; ++k) { drift_[k] = 0.0f; driftT_[k] = 0.0f; driftCount_[k] = 0; vGain_[k].snap(k < 4 ? 1.0f : 0.0f); }
        mix_.snap(get(kMix) * 0.01f); out_.snap(dbToGain(get(kOut)));
    }

    bool formatValue(int index, float plain, std::string& out) override
    {
        char b[32];
        if (index == kTone || index == kLowCut)
        {
            if (index == kTone && plain >= 19900.0f) { out = "Open"; return true; }
            if (plain >= 1000.0f) std::snprintf(b, sizeof b, "%.1f kHz", (double)(plain * 0.001f)); else std::snprintf(b, sizeof b, "%.0f Hz", (double)plain);
            out = b; return true;
        }
        if (index == kWailPitch)
        {
            const int v = (int)std::lround(plain);
            std::snprintf(b, sizeof b, v == 0 ? "0 st" : "%+d st", v);
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

        int vi = getInt(kVoices); if (vi < 0) vi = 0; if (vi > 3) vi = 3;
        const int nv = kVoiceCounts[vi];
        const float rateInc = get(kRate) / sr_;
        const float depth = get(kDepth) * 0.01f * 0.009f * sr_;       // up to 9 ms sweep
        const float drift = get(kDrift) * 0.01f * 0.012f * sr_;       // up to 12 ms wander
        const float cents = get(kDetune);
        const float upR = std::pow(2.0f, cents / 1200.0f), dnR = 1.0f / upR;
        const float wail = get(kWail) * 0.01f, wailSt = std::floor(get(kWailPitch) + 0.5f);
        const float cry = get(kCry) * 0.01f, cryInc = get(kCryRate) / sr_;
        const float air = get(kAir) * 0.01f, fb = get(kFeedback) * 0.01f;
        const float width = get(kWidth) * 0.01f, space = get(kSpace) * 0.01f;
        for (int c = 0; c < 2; ++c) { tone_[c].set(get(kTone), 0.707f, sr_); low_[c].set(get(kLowCut), sr_); airBp_[c].set(c ? 4300.0f : 3600.0f, 1.2f, sr_); }
        reverb_.set(0.85f, 2.5f + 4.0f * space, 0.6f, 0.6f, 25.0f, false);
        env_.set(5.0f, 200.0f, sr_);
        mix_.target = get(kMix) * 0.01f; out_.target = dbToGain(get(kOut));
        for (int k = 0; k < kMaxVoices; ++k) vGain_[k].target = k < nv ? 1.0f : 0.0f;
        const float vNorm = 1.4f / std::sqrt((float)nv);
        const bool wantSpace = space > 0.001f;
        // formant pair: sweeps between "oo" (300/870) and "ah" (750/1150)
        int formantCtr = 0;

        for (int i = 0; i < n; ++i)
        {
            const float xL = inL[i], xR = inR[i];
            const float lvl = env_.process(0.5f * (xL + xR));
            line_[0].write(xL + fb_[0]);
            line_[1].write(xR + fb_[1]);

            lfo_ += rateInc; if (lfo_ >= 1.0f) lfo_ -= 1.0f;
            float cl = 0.0f, cr = 0.0f, tap[2] = {0.0f, 0.0f};
            for (int k = 0; k < kMaxVoices; ++k)
            {
                const float g = vGain_[k].next();
                if (g < 1e-4f) continue;
                if (--driftCount_[k] <= 0) { driftT_[k] = rng_.uni(); driftCount_[k] = (int)(sr_ * (0.4f + 1.6f * rng_.uni())); }
                drift_[k] += 0.00003f * (driftT_[k] - drift_[k]);
                const float ph = lfo_ + (float)k / (float)nv;
                const float tri = 2.0f * std::fabs(ph - std::floor(ph) - 0.5f);       // 0..1 triangle
                const float d = 0.007f * sr_ + depth * tri + drift * drift_[k];
                const int side = k & 1;
                const float y = line_[side].readDelay(d) * g;
                if (k < 2) tap[side] = y;                         // feedback comes from the first tap on each side
                const float pan = (side ? 1.0f : -1.0f) * width * (0.5f + 0.5f * (float)(k >> 1) / 2.0f);
                cl += y * (1.0f - std::max(pan, 0.0f));
                cr += y * (1.0f + std::min(pan, 0.0f));
            }
            cl *= vNorm; cr *= vNorm;
            fb_[0] = fastTanh(tap[0] * fb); fb_[1] = fastTanh(tap[1] * fb);
            // sour detune: left a little sharp, right a little flat
            if (cents > 0.5f) { cl = 0.5f * (cl + det_[0].process(cl, upR, 0.035f * sr_, 0.2f)); cr = 0.5f * (cr + det_[1].process(cr, dnR, 0.035f * sr_, 0.2f)); }
            // the wail: pitched ghost with a slow, sad vibrato
            if (wail > 0.001f)
            {
                vibPh_ += 5.2f / sr_; if (vibPh_ >= 1.0f) vibPh_ -= 1.0f;
                if ((formantCtr & 15) == 0) wailRatio_ = semis(wailSt + 0.35f * std::sin(kTwoPi * vibPh_) + 0.3f * (drift_[0] - 0.5f));
                const float w = wailPs_.process(0.5f * (cl + cr), wailRatio_, 0.05f * sr_, 0.3f) * wail * 1.3f;
                cl += w * (1.0f - 0.3f * width); cr += w * (1.0f - 0.3f * width);
            }
            // cry: two formants sweeping between vowels
            if ((formantCtr++ & 15) == 0)
            {
                cryPh_ += cryInc * 16.0f; if (cryPh_ >= 1.0f) cryPh_ -= 1.0f;
                const float v = 0.5f + 0.5f * std::sin(kTwoPi * cryPh_);
                for (int c = 0; c < 2; ++c)
                {
                    const float sh = c ? 1.06f : 1.0f;
                    f1_[c].set((300.0f + 450.0f * v) * sh, 5.0f, sr_);
                    f2_[c].set((870.0f + 280.0f * v) * sh, 7.0f, sr_);
                }
            }
            float out[2] = {cl, cr};
            for (int c = 0; c < 2; ++c)
            {
                float y = out[c];
                if (cry > 0.001f) y += cry * 3.0f * (f1_[c].processBP(y) + 0.7f * f2_[c].processBP(y));
                if (air > 0.001f) y += airBp_[c].processBP(rng_.bi()) * air * 0.6f * std::min(lvl * 4.0f, 1.0f);
                y = tone_[c].processLP(low_[c].hp(y));
                out[c] = dc_[c].process(y);
            }
            if (wantSpace)
            {
                float rl, rr;
                reverb_.process(out[0], out[1], rl, rr);
                out[0] += space * 0.8f * rl; out[1] += space * 0.8f * rr;
            }
            const float m = mix_.next(), og = out_.next();
            const float dg = std::min(1.0f, 2.0f * (1.0f - m)), wg = std::min(1.0f, 2.0f * m);
            float yL = (xL * dg + out[0] * wg) * og, yR = (xR * dg + out[1] * wg) * og;
            if (!std::isfinite(yL) || !std::isfinite(yR)) { yL = yR = 0.0f; fb_[0] = fb_[1] = 0.0f; }
            L[i] = clampf(yL, -4.0f, 4.0f); R[i] = clampf(yR, -4.0f, 4.0f);
        }
    }

private:
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
        #include "banshee_presets.inc"
    }

    float sr_ = 44100.0f;
    Ring line_[2];
    PitchShifter det_[2], wailPs_;
    Reverb reverb_;
    Svf f1_[2], f2_[2], tone_[2], airBp_[2];
    OnePole low_[2];
    DcBlock dc_[2];
    EnvFollow env_;
    Smooth mix_, out_, vGain_[kMaxVoices];
    Rng rng_;
    float lfo_ = 0.0f, cryPh_ = 0.0f, vibPh_ = 0.0f, wailRatio_ = 2.0f, fb_[2] = {0, 0};
    float drift_[kMaxVoices] = {}, driftT_[kMaxVoices] = {};
    int driftCount_[kMaxVoices] = {};
};

MPC_PLUGIN_ENTRY(Banshee)
