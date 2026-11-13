// Ouroboros.cpp - binaryimage's chaotic reverser for the MPC.
//
// SPDX-License-Identifier: MIT
//
// Everything you play is recorded into a loop. On every step of the clock (synced to the MPC or
// free) Ouroboros may grab the audio that just went by and play it BACKWARDS, at its own pitch and
// length, crossfaded in and out. Modes:
//   Chaos    - each step is a coin toss (CHAOS): the live sound, or the last moment reversed.
//   Backmask - always reversed, step by step (a reverse delay), chaos varies the chunks.
//   Swell    - reversed chunks fade IN, so every hit is announced by its own ghost.
//   Spiral   - reversed chunks feed back into the loop, so the snake eats its own tail.
// GHOST adds reversed-reverb swells that rise into each step. FREEZE stops recording (the loop
// keeps getting chopped), FLIP reverses every step while it's on.
//
//   in ─┬─► loop buffer ◄── feedback (Spiral / Feedback) ──┐
//       │       └─► 2 grain heads per side (reversed) ──────┴─► tone ─┐
//       └─► reverb ─► ghost buffer ─► rising reversed swells ─────────┴─► mix ─► out

#include "mpc_plugin.h"
#include "dsp/tempo.h"
#include "bi_fx.h"
#include <cmath>
#include <cstdio>

using namespace bi;

enum P
{
    kOut = 0, kMix, kMode, kSync, kRate, kChaos, kLength, kJitter, kPitch, kFeedback,
    kGhost, kGhostSize, kTone, kWidth, kFade, kFreeze, kFlip,
    kNumParams
};
enum Mode { mChaos = 0, mBackmask, mSwell, mSpiral };

namespace {
float skewFor(float lo, float hi, float centre) { return std::log(0.5f) / std::log((centre - lo) / (hi - lo)); }
// Sync choices: Free, then SDK divisions 1/32 .. 2/1
const int kSyncDiv[] = {-1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 15, 16};
const int kNumSync = (int)(sizeof(kSyncDiv) / sizeof(kSyncDiv[0]));
}

class Ouroboros : public mpc::Plugin
{
public:
    static mpc::PluginInfo makeInfo()
    {
        mpc::PluginInfo i;
        i.name = "Ouroboros";
        i.vendor = "binaryimage";
        i.uniqueId = MPC_FOURCC('B', 'o', 'U', '3');   // must match "uid" in plugin.json
        i.version = 1000;
        i.isInstrument = false;
        i.numInputs = 2;
        i.numOutputs = 2;
        return i;
    }

    Ouroboros() : mpc::Plugin(makeInfo())
    {
        using namespace mpc;
        std::vector<std::string> sync = {"Free"};
        for (int k = 1; k < kNumSync; ++k) sync.push_back(dsp::divisionName(kSyncDiv[k]));
        addParam(floatParam("out", "Output", -24.0f, 12.0f, 0.0f, "dB", 1));
        addParam(floatParam("mix", "Mix", 0.0f, 100.0f, 100.0f, "%", 0));
        addParam(choiceParam("mode", "Mode", {"Chaos", "Backmask", "Swell", "Spiral"}, 0));
        addParam(choiceParam("sync", "Sync", sync, 9));                       // 1/4
        addParam(floatParam("rate", "Free Time", 30.0f, 2000.0f, 300.0f, "", 0, skewFor(30.0f, 2000.0f, 300.0f)));
        addParam(floatParam("chaos", "Chaos", 0.0f, 100.0f, 50.0f, "%", 0));
        addParam(floatParam("length", "Length", 25.0f, 400.0f, 100.0f, "%", 0, skewFor(25.0f, 400.0f, 100.0f)));
        addParam(floatParam("jitter", "Jitter", 0.0f, 100.0f, 20.0f, "%", 0));
        addParam(floatParam("pitch", "Pitch", -12.0f, 12.0f, 0.0f, "", 1));
        addParam(floatParam("feedback", "Feedback", 0.0f, 95.0f, 0.0f, "%", 0));
        addParam(floatParam("ghost", "Ghost", 0.0f, 100.0f, 20.0f, "%", 0));
        addParam(floatParam("ghost_size", "Ghost Size", 0.0f, 100.0f, 60.0f, "%", 0));
        addParam(floatParam("tone", "Tone", 500.0f, 20000.0f, 20000.0f, "", 0, skewFor(500.0f, 20000.0f, 4000.0f)));
        addParam(floatParam("width", "Width", 0.0f, 100.0f, 50.0f, "%", 0));
        addParam(floatParam("fade", "Fade", 1.0f, 100.0f, 15.0f, "%", 0));
        addParam(boolParam("freeze", "Freeze", false));
        addParam(boolParam("flip", "Flip", false));
        if (numParams() != kNumParams) mpc::log("Ouroboros: parameter count mismatch");
        addPrograms();
    }

    void prepare(double sampleRate, int) override
    {
        sr_ = (float)sampleRate;
        for (int c = 0; c < 2; ++c) { buf_[c].prepare((int)(sr_ * 10.0f)); ghostBuf_[c].prepare((int)(sr_ * 10.0f)); }
        reverb_.prepare(sr_);
        mix_.prepare(30.0f, sr_); out_.prepare(30.0f, sr_);
        reset();
    }

    void reset() override
    {
        for (int c = 0; c < 2; ++c)
        {
            buf_[c].clear(); ghostBuf_[c].clear(); tone_[c].reset(); dc_[c].reset();
            for (auto& g : grain_[c]) g = Grain();
            ghostGrain_[c] = Grain();
            fbL_[c] = 0.0f;
            revAmt_[c] = 0.0f;
        }
        reverb_.reset();
        clock_.reset();
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
        if (index == kPitch)
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

        const int mode = getInt(kMode);
        int sync = getInt(kSync); if (sync < 0) sync = 0; if (sync >= kNumSync) sync = kNumSync - 1;
        clock_.begin(ctx, sync > 0 ? mpc::dsp::divisionBeats(kSyncDiv[sync]) : 1.0f, get(kRate) * 0.001f * sr_, sync > 0);
        const float chaos = get(kChaos) * 0.01f, length = get(kLength) * 0.01f, jitter = get(kJitter) * 0.01f;
        const float ratio = semis(get(kPitch));
        float fb = get(kFeedback) * 0.01f;
        if (mode == mSpiral) fb = std::max(fb, 0.35f);
        const float ghost = get(kGhost) * 0.01f, gsize = get(kGhostSize) * 0.01f;
        const float width = get(kWidth) * 0.01f, fade = get(kFade) * 0.01f;
        const bool freeze = getBool(kFreeze), flip = getBool(kFlip);
        reverb_.set(0.3f + 0.7f * gsize, 0.8f + 6.0f * gsize, 0.35f, 0.4f, 0.0f, false);
        for (auto& t : tone_) t.set(get(kTone), 0.707f, sr_);
        mix_.target = get(kMix) * 0.01f; out_.target = dbToGain(get(kOut));
        const bool wantGhost = ghost > 0.001f;

        for (int i = 0; i < n; ++i)
        {
            const float x[2] = {inL[i], inR[i]};

            // ghost: a reverb whose tail we'll play backwards
            float gl = 0.0f, gr = 0.0f;
            if (wantGhost) { reverb_.process(x[0], x[1], gl, gr); }
            if (!freeze) { ghostBuf_[0].write(gl); ghostBuf_[1].write(gr); }

            // record (plus feedback); frozen = keep chopping what's already there
            if (!freeze)
            {
                buf_[0].write(x[0] + fbL_[0]);
                buf_[1].write(x[1] + fbL_[1]);
            }

            if (clock_.tick()) onStep(mode, chaos, length, jitter, ratio, width, fade, flip, wantGhost);

            float wet[2];
            for (int c = 0; c < 2; ++c)
            {
                float rev = 0.0f, env = 0.0f;
                for (auto& g : grain_[c]) if (g.active) { float e; rev += runGrain(buf_[c], g, e); env += e; }
                env = std::min(env, 1.0f);
                // Chaos and Swell keep the live sound under the reversed bits; Backmask / Spiral don't
                float live = 0.0f;
                if (mode == mChaos) live = x[c] * (1.0f - env);
                else if (mode == mSwell) live = x[c];
                float y = live + rev;
                if (ghostGrain_[c].active) { float e; y += ghost * 1.6f * runGrain(ghostBuf_[c], ghostGrain_[c], e); }
                y = tone_[c].processLP(y);
                wet[c] = dc_[c].process(y);
                fbL_[c] = fastTanh(rev * fb);
            }

            const float m = mix_.next(), og = out_.next();
            float yL = (x[0] + m * (wet[0] - x[0])) * og;
            float yR = (x[1] + m * (wet[1] - x[1])) * og;
            if (!std::isfinite(yL) || !std::isfinite(yR)) { yL = yR = 0.0f; fbL_[0] = fbL_[1] = 0.0f; }
            L[i] = clampf(yL, -4.0f, 4.0f); R[i] = clampf(yR, -4.0f, 4.0f);
        }
    }

private:
    struct Grain
    {
        bool active = false;
        double pos = 0.0;
        float speed = -1.0f;
        int len = 1, age = 0, fadeIn = 1, fadeOut = 1;
        bool swell = false;
        float gain = 1.0f;
    };

    // Runs one grain for one sample; e is its window (how much it's "covering" the live sound).
    float runGrain(const Ring& r, Grain& g, float& e)
    {
        float w;
        if (g.swell)
        {
            const float t = (float)g.age / (float)g.len;
            w = t * t;
            const int left = g.len - g.age;
            if (left < g.fadeOut) w *= (float)left / (float)g.fadeOut;
        }
        else
        {
            w = 1.0f;
            if (g.age < g.fadeIn) w = (float)g.age / (float)g.fadeIn;
            const int left = g.len - g.age;
            if (left < g.fadeOut) w = std::min(w, (float)left / (float)g.fadeOut);
        }
        const float y = r.readAbs(g.pos) * w * g.gain;
        g.pos += g.speed;
        if (++g.age >= g.len) g.active = false;
        e = w;
        return y;
    }

    void startGrain(int c, Grain& g, int len, float speed, float fadeFrac, bool swell, double startBack, const Ring& r)
    {
        g.active = true;
        g.len = std::max(len, 64);
        g.age = 0;
        g.speed = speed;
        g.swell = swell;
        g.gain = 1.0f;
        g.fadeIn = std::max(16, (int)(g.len * 0.5f * fadeFrac));
        g.fadeOut = std::max(16, (int)(g.len * 0.5f * fadeFrac));
        // start just behind the record head and read backwards over the audio that just went by
        g.pos = (double)(r.w - 2) - startBack;
        (void)c;
    }

    void onStep(int mode, float chaos, float length, float jitter, float ratio, float width, float fade, bool flip, bool wantGhost)
    {
        const float step = clock_.stepSamples();
        const int maxLen = buf_[0].size() / 2;
        bool shared = rng_.uni() >= width;
        bool rev0 = false; int len0 = 0; float spd0 = 0; double back0 = 0;
        for (int c = 0; c < 2; ++c)
        {
            bool rev; int len; float spd; double back;
            if (c == 1 && shared) { rev = rev0; len = len0; spd = spd0; back = back0; }
            else
            {
                float p;
                switch (mode)
                {
                case mChaos:    p = chaos; break;
                case mBackmask: p = 1.0f - 0.35f * chaos; break;       // chaos leaves some gaps
                default:        p = 1.0f - 0.15f * chaos; break;
                }
                if (flip) p = 1.0f;
                rev = rng_.chance(p);
                float l = step * length * (1.0f + jitter * rng_.bi() * 0.6f);
                if (mode != mChaos && chaos > 0.0f && rng_.chance(chaos * 0.4f)) l *= rng_.chance(0.5f) ? 0.5f : 0.25f;
                len = (int)clampf(l, 256.0f, (float)maxLen);
                float sp = ratio;
                if (chaos > 0.5f && rng_.chance((chaos - 0.5f) * 0.6f)) sp *= rng_.chance(0.5f) ? 0.5f : 2.0f;   // octave jumps
                spd = -sp;
                back = jitter * rng_.uni() * step * 0.5f;
            }
            if (c == 0) { rev0 = rev; len0 = len; spd0 = spd; back0 = back; }
            if (!rev) { for (auto& g : grain_[c]) if (g.active) g.len = std::min(g.len, g.age + g.fadeOut); continue; }
            // retire the older grain with its fade-out, start the new one in a free slot
            Grain* slot = &grain_[c][0];
            if (grain_[c][0].active) slot = &grain_[c][1];
            if (slot->active) slot = grain_[c][0].age > grain_[c][1].age ? &grain_[c][0] : &grain_[c][1];
            for (auto& g : grain_[c]) if (g.active && &g != slot) g.len = std::min(g.len, g.age + g.fadeOut);
            startGrain(c, *slot, len, spd, fade, mode == mSwell, back, buf_[c]);
            if (mode == mSwell) slot->gain = 1.3f;
        }
        if (wantGhost)
            for (int c = 0; c < 2; ++c)
            {
                startGrain(c, ghostGrain_[c], (int)clampf(step, 256.0f, (float)maxLen), -1.0f, 0.1f, true, 0.0, ghostBuf_[c]);
                ghostGrain_[c].fadeOut = 64;
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
        #include "ouroboros_presets.inc"
    }

    float sr_ = 44100.0f;
    Ring buf_[2], ghostBuf_[2];
    Grain grain_[2][2], ghostGrain_[2];
    Reverb reverb_;
    Svf tone_[2];
    DcBlock dc_[2];
    Smooth mix_, out_;
    Rng rng_;
    StepClock clock_;
    float fbL_[2] = {0, 0}, revAmt_[2] = {0, 0};
};

MPC_PLUGIN_ENTRY(Ouroboros)
