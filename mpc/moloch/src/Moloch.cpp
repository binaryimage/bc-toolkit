// Moloch.cpp - binaryimage's pitch nightmare for the MPC.
//
// SPDX-License-Identifier: MIT
//
// Up to three pitch-shifted copies of the input. On every step of the clock each one may jump to a
// new random interval (CHAOS sets the odds) taken from a SCALE, within a RANGE, gliding there with
// SLEW. The output feeds back through a CLIMB shifter, so the sound spirals up or sinks forever.
// GRAIN sets the shifter window (short = buzzy and metallic, long = smeared), SMEAR scatters it.
//
//   in ─► + ◄──── feedback ◄── CLIMB shifter ◄──────────┐
//         └─► voice 1/2/3 shifters (random pitch) ─► pan ┴─► tone ─► mix ─► out

#include "mpc_plugin.h"
#include "dsp/tempo.h"
#include "bi_fx.h"
#include <cmath>
#include <cstdio>

using namespace bi;

enum P
{
    kOut = 0, kMix, kRange, kScale, kSync, kRate, kChaos, kSlew, kVoices, kSpread, kFeedback, kClimb,
    kGrain, kSmear, kTone, kBend, kFreeze, kDetune,
    kNumParams
};
enum Scale { sChromatic = 0, sOctaves, sFifths, sTritones, sPhrygian, sDiminished, sMicro, sNumScales };

namespace {
float skewFor(float lo, float hi, float centre) { return std::log(0.5f) / std::log((centre - lo) / (hi - lo)); }
const int kSyncDiv[] = {-1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 15, 16};
const int kNumSync = (int)(sizeof(kSyncDiv) / sizeof(kSyncDiv[0]));
const int kMaxVoices = 3;
bool inScale(int scale, int st)
{
    const int m = ((st % 12) + 12) % 12;
    switch (scale)
    {
    case sOctaves:    return m == 0;
    case sFifths:     return m == 0 || m == 7 || m == 5;
    case sTritones:   return m == 0 || m == 6;
    case sPhrygian:   return m == 0 || m == 1 || m == 3 || m == 5 || m == 7 || m == 8 || m == 10;
    case sDiminished: return m == 0 || m == 3 || m == 6 || m == 9;
    default:          return true;
    }
}
}

class Moloch : public mpc::Plugin
{
public:
    static mpc::PluginInfo makeInfo()
    {
        mpc::PluginInfo i;
        i.name = "Moloch";
        i.vendor = "binaryimage";
        i.uniqueId = MPC_FOURCC('B', 'm', 'L', '4');   // must match "uid" in plugin.json
        i.version = 1000;
        i.isInstrument = false;
        i.numInputs = 2;
        i.numOutputs = 2;
        return i;
    }

    Moloch() : mpc::Plugin(makeInfo())
    {
        using namespace mpc;
        std::vector<std::string> sync = {"Free"};
        for (int k = 1; k < kNumSync; ++k) sync.push_back(dsp::divisionName(kSyncDiv[k]));
        addParam(floatParam("out", "Output", -24.0f, 12.0f, 0.0f, "dB", 1));
        addParam(floatParam("mix", "Mix", 0.0f, 100.0f, 70.0f, "%", 0));
        addParam(floatParam("range", "Range", 0.0f, 24.0f, 12.0f, "st", 0));
        addParam(choiceParam("scale", "Scale", {"Chromatic", "Octaves", "Fifths", "Tritones", "Phrygian", "Diminished", "Microtonal"}, sPhrygian));
        addParam(choiceParam("sync", "Sync", sync, 6));     // 1/8
        addParam(floatParam("rate", "Free Time", 30.0f, 2000.0f, 250.0f, "", 0, skewFor(30.0f, 2000.0f, 300.0f)));
        addParam(floatParam("chaos", "Chaos", 0.0f, 100.0f, 70.0f, "%", 0));
        addParam(floatParam("slew", "Slew", 0.0f, 100.0f, 10.0f, "%", 0));
        addParam(choiceParam("voices", "Voices", {"1", "2", "3"}, 1));
        addParam(floatParam("spread", "Spread", 0.0f, 100.0f, 60.0f, "%", 0));
        addParam(floatParam("feedback", "Feedback", 0.0f, 95.0f, 20.0f, "%", 0));
        addParam(floatParam("climb", "Climb", -12.0f, 12.0f, 0.0f, "", 1));
        addParam(floatParam("grain", "Grain", 15.0f, 200.0f, 60.0f, "ms", 0, skewFor(15.0f, 200.0f, 60.0f)));
        addParam(floatParam("smear", "Smear", 0.0f, 100.0f, 20.0f, "%", 0));
        addParam(floatParam("tone", "Tone", 500.0f, 20000.0f, 20000.0f, "", 0, skewFor(500.0f, 20000.0f, 4000.0f)));
        addParam(floatParam("bend", "Bend", -24.0f, 24.0f, 0.0f, "", 1));
        addParam(boolParam("freeze", "Hold", false));
        addParam(floatParam("detune", "Detune", 0.0f, 100.0f, 10.0f, "%", 0));
        if (numParams() != kNumParams) mpc::log("Moloch: parameter count mismatch");
        addPrograms();
    }

    void prepare(double sampleRate, int) override
    {
        sr_ = (float)sampleRate;
        for (auto& v : voice_) v.ps.prepare(sr_, 220.0f);
        climbPs_.prepare(sr_, 220.0f);
        mix_.prepare(30.0f, sr_); out_.prepare(30.0f, sr_);
        reset();
    }

    void reset() override
    {
        for (int k = 0; k < kMaxVoices; ++k)
        {
            Voice& v = voice_[k];
            v.ps.reset(); v.cur = v.target = 0.0f; v.det = 0.0f; v.ratio = 1.0f; v.gain = k < 2 ? 1.0f : 0.0f;
            v.ps.rng.seed(0x1234u + 977u * (uint32_t)k);
        }
        climbPs_.reset();
        fb_ = 0.0f; tone_[0].reset(); tone_[1].reset(); dc_[0].reset(); dc_[1].reset(); fbHp_.reset();
        clock_.reset(); ctr_ = 0;
        mix_.snap(get(kMix) * 0.01f); out_.snap(dbToGain(get(kOut)));
    }

    bool formatValue(int index, float plain, std::string& out) override
    {
        char b[32];
        if (index == kRate)
        {
            if (plain >= 1000.0f) std::snprintf(b, sizeof b, "%.2f s", (double)(plain * 0.001f));
            else std::snprintf(b, sizeof b, "%.0f ms", (double)plain);
            out = b; return true;
        }
        if (index == kTone)
        {
            if (plain >= 19900.0f) { out = "Open"; return true; }
            if (plain >= 1000.0f) std::snprintf(b, sizeof b, "%.1f kHz", (double)(plain * 0.001f)); else std::snprintf(b, sizeof b, "%.0f Hz", (double)plain);
            out = b; return true;
        }
        if (index == kClimb || index == kBend)
        {
            std::snprintf(b, sizeof b, std::fabs(plain) < 0.05f ? "0 st" : "%+.1f st", (double)plain);
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

        int sync = getInt(kSync); if (sync < 0) sync = 0; if (sync >= kNumSync) sync = kNumSync - 1;
        clock_.begin(ctx, sync > 0 ? mpc::dsp::divisionBeats(kSyncDiv[sync]) : 1.0f, get(kRate) * 0.001f * sr_, sync > 0);
        const float range = get(kRange), chaos = get(kChaos) * 0.01f, slew = get(kSlew) * 0.01f;
        int scale = getInt(kScale); if (scale < 0 || scale >= sNumScales) scale = 0;
        int nv = getInt(kVoices) + 1; if (nv < 1) nv = 1; if (nv > kMaxVoices) nv = kMaxVoices;
        const float spread = get(kSpread) * 0.01f, fb = get(kFeedback) * 0.01f;
        const float climbRatio = semis(get(kClimb));
        const float win = get(kGrain) * 0.001f * sr_, smear = get(kSmear) * 0.01f;
        const float bend = get(kBend), detune = get(kDetune) * 0.01f;
        const bool hold = getBool(kFreeze);
        for (auto& t : tone_) t.set(get(kTone), 0.707f, sr_);
        fbHp_.set(60.0f, sr_);
        mix_.target = get(kMix) * 0.01f; out_.target = dbToGain(get(kOut));
        // glide speed: SLEW is a fraction of the step
        const float glideSamples = std::max(1.0f, slew * clock_.stepSamples());
        const float glide = slew <= 0.001f ? 1.0f : 1.0f - std::exp(-4.0f / glideSamples);
        const float norm = 1.0f / std::sqrt((float)nv);
        float panL[kMaxVoices], panR[kMaxVoices];
        for (int k = 0; k < kMaxVoices; ++k)
        {
            float p = 0.0f;
            if (nv == 2) p = k == 0 ? -1.0f : 1.0f;
            else if (nv == 3) p = k == 0 ? -1.0f : (k == 1 ? 0.0f : 1.0f);
            p *= spread;
            panL[k] = std::sqrt(0.5f * (1.0f - p)) * 1.4142f * norm;
            panR[k] = std::sqrt(0.5f * (1.0f + p)) * 1.4142f * norm;
            voice_[k].gainTarget = k < nv ? 1.0f : 0.0f;
        }

        for (int i = 0; i < n; ++i)
        {
            const float xL = inL[i], xR = inR[i];
            if (clock_.tick() && !hold)
                for (int k = 0; k < nv; ++k)
                    if (rng_.chance(chaos)) { voice_[k].target = pickInterval(scale, range); voice_[k].det = rng_.bi() * detune * 0.5f; }

            const float in = 0.5f * (xL + xR) + fb_;
            float wl = 0.0f, wr = 0.0f;
            const bool updateRatio = (ctr_++ & 31) == 0;
            for (int k = 0; k < kMaxVoices; ++k)
            {
                Voice& v = voice_[k];
                v.gain += 0.002f * (v.gainTarget - v.gain);
                if (v.gain < 1e-4f && v.gainTarget == 0.0f) continue;
                v.cur += glide * (v.target - v.cur);
                if (updateRatio) v.ratio = semis(clampf(v.cur + bend + v.det, -36.0f, 36.0f));
                const float y = v.ps.process(in, v.ratio, win, smear) * v.gain;
                wl += y * panL[k]; wr += y * panR[k];
            }
            // feedback through the climb shifter: each pass moves the pitch again
            const float mono = 0.5f * (wl + wr);
            fb_ = fastTanh(fbHp_.hp(climbPs_.process(mono, climbRatio, win, smear * 0.5f)) * fb * 1.1f);

            float yL = dc_[0].process(tone_[0].processLP(wl)), yR = dc_[1].process(tone_[1].processLP(wr));
            const float m = mix_.next(), og = out_.next();
            yL = (xL + m * (yL - xL)) * og; yR = (xR + m * (yR - xR)) * og;
            if (!std::isfinite(yL) || !std::isfinite(yR)) { yL = yR = 0.0f; fb_ = 0.0f; }
            L[i] = clampf(yL, -4.0f, 4.0f); R[i] = clampf(yR, -4.0f, 4.0f);
        }
    }

private:
    struct Voice { PitchShifter ps; float cur = 0, target = 0, det = 0, ratio = 1, gain = 0, gainTarget = 0; };

    float pickInterval(int scale, float range)
    {
        const int r = (int)std::floor(range + 0.5f);
        if (scale == sMicro) return rng_.bi() * range;
        if (r <= 0) return 0.0f;
        for (int tries = 0; tries < 24; ++tries)
        {
            const int st = (int)std::floor(rng_.uni() * (2 * r + 1)) - r;
            if (inScale(scale, st)) return (float)st;
        }
        return 0.0f;
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
        #include "moloch_presets.inc"
    }

    float sr_ = 44100.0f;
    Voice voice_[kMaxVoices];
    PitchShifter climbPs_;
    Svf tone_[2];
    DcBlock dc_[2];
    OnePole fbHp_;
    Smooth mix_, out_;
    Rng rng_;
    StepClock clock_;
    float fb_ = 0.0f;
    uint32_t ctr_ = 0;
};

MPC_PLUGIN_ENTRY(Moloch)
