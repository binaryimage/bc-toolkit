// Grendel.cpp - binaryimage's tape abomination for the MPC.
//
// SPDX-License-Identifier: MIT
//
//   in ─► drive ─► [ tape loop: wow + flutter move the play head ] ─► age (bump, dull, low cut)
//        ─► dropouts ─► crush ─► + hiss ─► tone ─► mix ─► out
//
// The play head can be thrown around: TAPE STOP slows it to a halt (and spins back up when you
// let go), REWIND drags it backwards, and TAPE EAT / POSSESSED fire short events on a second head:
// chewed-tape jumps, brakes, backwards grabs and wow spikes, crossfaded in and out of the main head.

#include "mpc_plugin.h"
#include "dsp/tempo.h"
#include "bi_fx.h"
#include <cmath>
#include <cstdio>

using namespace bi;

enum P
{
    kOut = 0, kMix, kDrive, kAge, kWow, kWowRate, kFlutter, kDropout, kEat, kCrush, kHiss,
    kPossessed, kStop, kStopTime, kStartTime, kWidth, kTone, kRewind, kGrid,
    kNumParams
};

namespace {
const float kBaseDelayMs = 12.0f;      // play head sits this far behind the record head (dry is delayed to match)
const float kWowMaxMs = 6.0f;
float skewFor(float lo, float hi, float centre) { return std::log(0.5f) / std::log((centre - lo) / (hi - lo)); }
}

class Grendel : public mpc::Plugin
{
public:
    static mpc::PluginInfo makeInfo()
    {
        mpc::PluginInfo i;
        i.name = "Grendel";
        i.vendor = "binaryimage";
        i.uniqueId = MPC_FOURCC('B', 'g', 'R', '7');   // must match "uid" in plugin.json
        i.version = 1000;
        i.isInstrument = false;
        i.numInputs = 2;
        i.numOutputs = 2;
        return i;
    }

    Grendel() : mpc::Plugin(makeInfo())
    {
        using namespace mpc;
        std::vector<std::string> grid = {"Free"};
        for (int d : {4, 7, 10, 13, 15}) grid.push_back(dsp::divisionName(d));   // 1/16 1/8 1/4 1/2 1/1
        addParam(floatParam("out", "Output", -24.0f, 12.0f, 0.0f, "dB", 1));
        addParam(floatParam("mix", "Mix", 0.0f, 100.0f, 100.0f, "%", 0));
        addParam(floatParam("drive", "Drive", 0.0f, 100.0f, 30.0f, "%", 0));
        addParam(floatParam("age", "Age", 0.0f, 100.0f, 30.0f, "%", 0));
        addParam(floatParam("wow", "Wow", 0.0f, 100.0f, 25.0f, "%", 0));
        addParam(floatParam("wow_rate", "Wow Rate", 0.05f, 4.0f, 0.5f, "Hz", 2, skewFor(0.05f, 4.0f, 0.7f)));
        addParam(floatParam("flutter", "Flutter", 0.0f, 100.0f, 15.0f, "%", 0));
        addParam(floatParam("dropout", "Dropouts", 0.0f, 100.0f, 0.0f, "%", 0));
        addParam(floatParam("eat", "Tape Eat", 0.0f, 100.0f, 0.0f, "%", 0));
        addParam(floatParam("crush", "Crush", 0.0f, 100.0f, 0.0f, "%", 0));
        addParam(floatParam("hiss", "Hiss", 0.0f, 100.0f, 10.0f, "%", 0));
        addParam(floatParam("possessed", "Possessed", 0.0f, 100.0f, 0.0f, "%", 0));
        addParam(boolParam("stop", "Tape Stop", false));
        addParam(floatParam("stop_time", "Stop Time", 50.0f, 4000.0f, 800.0f, "", 0, skewFor(50.0f, 4000.0f, 700.0f)));
        addParam(floatParam("start_time", "Start Time", 20.0f, 2000.0f, 300.0f, "", 0, skewFor(20.0f, 2000.0f, 350.0f)));
        addParam(floatParam("width", "Width", 0.0f, 100.0f, 50.0f, "%", 0));
        addParam(floatParam("tone", "Tone", -100.0f, 100.0f, 0.0f, "", 0));
        addParam(boolParam("rewind", "Rewind", false));
        addParam(choiceParam("grid", "Grid", grid, 0));
        if (numParams() != kNumParams) mpc::log("Grendel: parameter count mismatch");
        addPrograms();
    }

    void prepare(double sampleRate, int) override
    {
        sr_ = (float)sampleRate;
        for (auto& t : tape_) t.prepare((int)(sr_ * 14.0f));
        for (auto& d : dry_) d.prepare((int)(sr_ * 0.05f));
        mix_.prepare(30.0f, sr_); out_.prepare(30.0f, sr_); drive_.prepare(30.0f, sr_);
        reset();
    }

    void reset() override
    {
        for (auto& t : tape_) t.clear();
        for (auto& d : dry_) d.clear();
        for (int c = 0; c < 2; ++c)
        {
            bump_[c].reset(); dull_[c].reset(); lowcut_[c].reset(); stopLp_[c].reset(); tilt_[c].reset(); dc_[c].reset();
            hissLp_[c].reset(); hold_[c] = 0.0f; holdCount_[c] = 0.0f; drop_[c] = 1.0f; dropTarget_[c] = 1.0f; dropLeft_[c] = 0;
        }
        mode_ = Lock; spd_ = 1.0f; freeLag_ = 0.0; xf_ = 0.0f; ev_.active = false; evGain_ = 0.0f;
        wowPh_ = 0.0f; flPh_ = 0.0f; wowRnd_ = 0.0f; wowRndT_ = 0.0f; spike_ = 0.0f; clock_.reset();
        mix_.snap(get(kMix) * 0.01f); out_.snap(dbToGain(get(kOut))); drive_.snap(get(kDrive) * 0.01f);
    }

    bool formatValue(int index, float plain, std::string& out) override
    {
        char b[32];
        if (index == kStopTime || index == kStartTime)
        {
            if (plain >= 1000.0f) std::snprintf(b, sizeof b, "%.2f s", (double)(plain * 0.001f));
            else std::snprintf(b, sizeof b, "%.0f ms", (double)plain);
            out = b; return true;
        }
        if (index == kTone)
        {
            const int v = (int)std::lround(plain);
            if (v == 0) out = "Flat"; else { std::snprintf(b, sizeof b, v < 0 ? "Dark %d" : "Bright %d", v < 0 ? -v : v); out = b; }
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

        // ---- per-block settings
        const float age = get(kAge) * 0.01f, wow = get(kWow) * 0.01f, flutter = get(kFlutter) * 0.01f;
        const float dropAmt = get(kDropout) * 0.01f, eat = get(kEat) * 0.01f, crush = get(kCrush) * 0.01f;
        const float hiss = get(kHiss) * 0.01f, poss = get(kPossessed) * 0.01f, width = get(kWidth) * 0.01f;
        const float tone = get(kTone) * 0.01f;
        mix_.target = get(kMix) * 0.01f; out_.target = dbToGain(get(kOut)); drive_.target = get(kDrive) * 0.01f;
        for (int c = 0; c < 2; ++c)
        {
            bump_[c].set(70.0f + 40.0f * age, 1.2f, sr_);
            dull_[c].set(19000.0f * std::pow(0.17f, age), sr_);
            lowcut_[c].set(15.0f + 70.0f * age, sr_);
            tilt_[c].set(900.0f, sr_);
            hissLp_[c].set(6000.0f - 3000.0f * age, sr_);
        }
        const float bumpGain = 0.9f * age;
        const float bits = 16.0f - 12.5f * crush * crush;
        const float quant = std::pow(2.0f, bits - 1.0f);
        const float holdLen = 1.0f + 22.0f * crush * crush * crush;
        const float hissLevel = hiss * hiss * 0.05f;
        const float wowDepth = (wow + spike_) * kWowMaxMs * 0.001f * sr_;
        const float flDepth = flutter * 0.35f * 0.001f * sr_;
        const float wowInc = get(kWowRate) / sr_;
        const float stopT = get(kStopTime) * 0.001f * sr_, startT = get(kStartTime) * 0.001f * sr_;
        const bool stopBtn = getBool(kStop), rewBtn = getBool(kRewind);
        const int grid = getInt(kGrid);
        static const int gridDiv[6] = {0, 4, 7, 10, 13, 15};
        clock_.begin(ctx, grid > 0 ? mpc::dsp::divisionBeats(gridDiv[grid]) : 0.25f, sr_ * 0.25f, grid > 0);
        const float base = kBaseDelayMs * 0.001f * sr_;
        spike_ *= 0.92f;
        const float tL = tone > 0 ? 1.0f + 1.5f * tone : 1.0f, tH = tone < 0 ? 1.0f + 0.9f * tone : 1.0f + 1.2f * tone;
        const float tLow = tone < 0 ? 1.0f - 0.6f * tone : 1.0f - 0.3f * tone;
        (void)tL;

        for (int i = 0; i < n; ++i)
        {
            const float xL = inL[i], xR = inR[i];

            // ---- record: drive into the tape
            const float dr = drive_.next();
            const float g = 1.0f + dr * dr * 9.0f;
            const float comp = 1.0f / (1.0f + dr * 1.6f);
            tape_[0].write(fastTanh(g * xL + 0.12f * dr) * comp - fastTanh(0.12f * dr) * comp);
            tape_[1].write(fastTanh(g * xR + 0.12f * dr) * comp - fastTanh(0.12f * dr) * comp);
            dry_[0].write(xL); dry_[1].write(xR);
            const double w = (double)(tape_[0].w - 1);

            // ---- wow (slow sine + random drift) and flutter
            wowPh_ += wowInc; if (wowPh_ >= 1.0f) wowPh_ -= 1.0f;
            if (--wowRndCount_ <= 0) { wowRndT_ = rng_.bi(); wowRndCount_ = (int)(sr_ * (0.2f + rng_.uni() * 0.8f)); }
            wowRnd_ += 0.00005f * (wowRndT_ - wowRnd_);
            flPh_ += (7.3f + 3.0f * wowRnd_) / sr_; if (flPh_ >= 1.0f) flPh_ -= 1.0f;
            const float wl = 0.5f + 0.5f * (0.7f * std::sin(kTwoPi * wowPh_) + 0.3f * wowRnd_);
            const float wr = 0.5f + 0.5f * (0.7f * std::sin(kTwoPi * (wowPh_ + 0.25f * width)) + 0.3f * wowRnd_ * (1.0f - width));
            const float fl = std::sin(kTwoPi * flPh_) * 0.6f + 0.4f * rng_.bi() * flutter;
            const float fr = std::sin(kTwoPi * (flPh_ + 0.3f * width)) * 0.6f + 0.4f * rng_.bi() * flutter;
            const float modL = wowDepth * wl + flDepth * (0.5f + 0.5f * fl);
            const float modR = wowDepth * wr + flDepth * (0.5f + 0.5f * fr);

            // ---- main transport: locked to the record head, or free (stop / start / rewind)
            updateTransport(stopBtn, rewBtn, stopT, startT, base, w);
            double posMain = (mode_ == Lock) ? w - base : freePos_;
            if (mode_ != Lock) freePos_ += spd_;
            if (freePos_ < w - (double)tape_[0].size() + 4.0 * sr_) freePos_ = w - (double)tape_[0].size() + 4.0 * sr_;
            float mL = readTape(0, posMain - modL), mR = readTape(1, posMain - modR);
            if (xf_ > 0.0f)       // crossfading from a free head back to lock
            {
                const float a = std::sin(0.5f * kPi * xf_);
                mL = mL * (1.0f - a * a) + readTape(0, xfPos_ - modL) * a * a;
                mR = mR * (1.0f - a * a) + readTape(1, xfPos_ - modR) * a * a;
                xfPos_ += xfSpd_;
                xf_ -= 1.0f / (0.04f * sr_); if (xf_ < 0.0f) xf_ = 0.0f;
            }
            const float sp = std::fabs(mode_ == Lock ? 1.0f : spd_);
            const float speedGain = clampf(sp * 5.0f, 0.0f, 1.0f);

            // ---- events: tape eat + possessed
            const bool step = clock_.tick();
            if (!ev_.active && mode_ == Lock)
            {
                bool fire = false;
                if (grid > 0) { if (step && rng_.chance(0.08f + 0.6f * poss * poss + 0.25f * eat)) fire = (poss > 0.0f || eat > 0.0f); }
                else fire = rng_.chance((eat * 1.5f + poss * 3.0f) / sr_);
                if (fire) startEvent(eat, poss, base, w);
            }
            float oL = mL, oR = mR;
            if (ev_.active || evGain_ > 0.0f) processEvent(oL, oR, mL, mR, modL, modR, w);
            if (grid > 0 && step && poss > 0.0f && rng_.chance(0.25f * poss)) spike_ = 1.5f + 2.0f * poss * rng_.uni();
            if (grid == 0 && rng_.chance(poss * 0.4f / sr_)) spike_ = 1.5f + 2.0f * poss * rng_.uni();

            // ---- age: head bump, dull top, thin bottom; slowing tape gets darker
            float ch[2] = {oL, oR};
            const float stopCut = 400.0f + 18000.0f * std::pow(clampf(sp, 0.0f, 1.0f), 0.9f);
            if (mode_ != Lock) { stopLp_[0].set(stopCut, sr_); stopLp_[1].set(stopCut, sr_); }
            for (int c = 0; c < 2; ++c)
            {
                float y = ch[c];
                y += bumpGain * bump_[c].processBP(y);
                y = dull_[c].lp(y);
                y = lowcut_[c].hp(y);
                if (mode_ != Lock) y = stopLp_[c].lp(y) * speedGain;
                // dropouts: the oxide flakes off
                if (dropLeft_[c] > 0) { if (--dropLeft_[c] == 0) dropTarget_[c] = 1.0f; }
                else if (rng_.chance(dropAmt * dropAmt * 4.0f / sr_))
                {
                    dropTarget_[c] = 1.0f - (0.5f + 0.5f * rng_.uni()) * (0.4f + 0.6f * dropAmt);
                    dropLeft_[c] = (int)(sr_ * (0.02f + 0.25f * rng_.uni() * rng_.uni()));
                    if (rng_.chance(1.0f - width)) { dropTarget_[1 - c] = dropTarget_[c]; dropLeft_[1 - c] = dropLeft_[c]; }
                }
                drop_[c] += 0.004f * (dropTarget_[c] - drop_[c]);
                y *= drop_[c];
                // crush: sample-and-hold + bit depth
                if (crush > 0.001f)
                {
                    holdCount_[c] += 1.0f;
                    if (holdCount_[c] >= holdLen) { holdCount_[c] -= holdLen; hold_[c] = std::floor(y * quant + 0.5f) / quant; }
                    y = hold_[c];
                }
                // hiss
                y += hissLp_[c].lp(rng_.bi()) * hissLevel * (0.6f + 0.4f * drop_[c]);
                // tone tilt around 900 Hz
                const float lo = tilt_[c].lp(y);
                y = lo * tLow + (y - lo) * tH;
                ch[c] = dc_[c].process(y);
            }

            const float m = mix_.next(), og = out_.next();
            const float dL = dry_[0].readDelayLin(base), dR = dry_[1].readDelayLin(base);
            float yL = (dL + m * (ch[0] - dL)) * og, yR = (dR + m * (ch[1] - dR)) * og;
            if (!std::isfinite(yL) || !std::isfinite(yR)) { yL = yR = 0.0f; }
            L[i] = clampf(yL, -4.0f, 4.0f); R[i] = clampf(yR, -4.0f, 4.0f);
        }
    }

private:
    enum Mode { Lock, Stopping, Stopped, Starting, Rewinding };

    float readTape(int c, double pos) const { return tape_[c].readAbs(pos); }

    void updateTransport(bool stopBtn, bool rewBtn, float stopT, float startT, float base, double w)
    {
        switch (mode_)
        {
        case Lock:
            if (rewBtn) { mode_ = Rewinding; freePos_ = w - base; spd_ = 1.0f; }
            else if (stopBtn) { mode_ = Stopping; freePos_ = w - base; spd_ = 1.0f; stopPh_ = 0.0f; }
            break;
        case Stopping:
            if (rewBtn) { mode_ = Rewinding; break; }
            if (!stopBtn) { mode_ = Starting; break; }
            stopPh_ += 1.0f / std::max(stopT, 1.0f);
            spd_ = stopPh_ >= 1.0f ? 0.0f : std::pow(1.0f - stopPh_, 1.6f);
            if (stopPh_ >= 1.0f) mode_ = Stopped;
            break;
        case Stopped:
            spd_ = 0.0f;
            if (rewBtn) mode_ = Rewinding;
            else if (!stopBtn) { mode_ = Starting; freePos_ = w - base - 0.5 * startT; }
            break;
        case Starting:
            if (rewBtn) { mode_ = Rewinding; break; }
            if (stopBtn) { mode_ = Stopping; stopPh_ = 1.0f - std::pow(std::max(spd_, 0.0f), 1.0f / 1.6f); break; }
            spd_ += 1.0f / std::max(startT, 1.0f);
            if (spd_ >= 1.0f) { spd_ = 1.0f; backToLock(); }
            break;
        case Rewinding:
            if (!rewBtn) { if (stopBtn) { mode_ = Stopped; spd_ = 0.0f; } else backToLock(); break; }
            spd_ += (-3.0f - spd_) * (1.0f / (0.25f * sr_));
            break;
        }
    }

    void backToLock() { xf_ = 1.0f; xfPos_ = freePos_; xfSpd_ = 1.0f; mode_ = Lock; }

    struct Event { bool active = false; double pos = 0; float spd = 1, spdTarget = 1, slew = 0.001f; int left = 0, len = 1; int kind = 0; float chatter = 0, chPh = 0, chRate = 0; };

    void startEvent(float eat, float poss, float base, double w)
    {
        Event& e = ev_;
        e.active = true;
        const float r = rng_.uni() * (eat + poss + 1e-6f);
        const bool possessed = r >= eat;
        e.kind = possessed ? (int)(rng_.uni() * 4.0f) : 0;   // 0 chew, 1 brake, 2 backwards grab, 3 warble
        e.pos = w - base;
        switch (e.kind)
        {
        case 0:   // chewed: jump back, jerky speed, chatter
            e.pos -= (0.01f + 0.07f * rng_.uni()) * sr_;
            e.spd = 0.6f + 0.8f * rng_.uni(); e.spdTarget = e.spd; e.slew = 0.0005f;
            e.len = (int)(sr_ * (0.03f + 0.22f * rng_.uni()));
            e.chatter = 0.5f + 0.5f * rng_.uni(); e.chRate = (18.0f + 45.0f * rng_.uni()) / sr_;
            break;
        case 1:   // brake: speed dives then comes back
            e.spd = 1.0f; e.spdTarget = 0.15f + 0.3f * rng_.uni(); e.slew = 1.0f / (sr_ * (0.05f + 0.12f * rng_.uni()));
            e.len = (int)(sr_ * (0.12f + 0.3f * rng_.uni()));
            e.chatter = 0.0f;
            break;
        case 2:   // backwards grab
            e.spd = -1.0f; e.spdTarget = -1.0f; e.slew = 0.0f;
            e.len = (int)(sr_ * (0.08f + 0.3f * rng_.uni()));
            e.chatter = 0.0f;
            break;
        default:  // warble: fast pitch wobble
            e.spd = 1.0f; e.spdTarget = 1.0f; e.slew = 0.0f;
            e.len = (int)(sr_ * (0.15f + 0.35f * rng_.uni()));
            e.chatter = 0.0f; e.chRate = (5.0f + 9.0f * rng_.uni()) / sr_;
            break;
        }
        e.left = e.len; e.chPh = 0.0f;
    }

    void processEvent(float& oL, float& oR, float mL, float mR, float modL, float modR, double w)
    {
        Event& e = ev_;
        const float fadeIn = 1.0f / (0.006f * sr_), fadeOut = 1.0f / (0.02f * sr_);
        if (e.active)
        {
            evGain_ = std::min(1.0f, evGain_ + fadeIn);
            if (--e.left <= 0) e.active = false;
        }
        else evGain_ = std::max(0.0f, evGain_ - fadeOut);
        float spd = e.spd;
        if (e.kind == 1)
        {
            const bool firstHalf = e.left > e.len / 2;
            e.spd += ((firstHalf ? e.spdTarget : 1.0f) - e.spd) * std::min(1.0f, e.slew * 3.0f);
            spd = e.spd;
        }
        else if (e.kind == 3)
        {
            e.chPh += e.chRate; if (e.chPh >= 1.0f) e.chPh -= 1.0f;
            spd = 1.0f + 0.35f * std::sin(kTwoPi * e.chPh);
        }
        else if (e.kind == 0 && rng_.chance(40.0f / sr_)) e.spd = 0.5f + 1.0f * rng_.uni();
        e.pos += spd;
        if (e.pos > w - 2.0) e.pos = w - 2.0;
        float a = tape_[0].readAbs(e.pos - modL), b = tape_[1].readAbs(e.pos - modR);
        if (e.kind == 0)
        {
            e.chPh += e.chRate; if (e.chPh >= 1.0f) e.chPh -= 1.0f;
            const float gate = e.chPh < 0.5f ? 1.0f : 1.0f - e.chatter;
            a *= gate; b *= gate;
        }
        const float g = std::sin(0.5f * kPi * evGain_);
        const float gg = g * g;
        oL = mL * (1.0f - gg) + a * gg;
        oR = mR * (1.0f - gg) + b * gg;
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
        #include "grendel_presets.inc"
    }

    float sr_ = 44100.0f;
    Ring tape_[2], dry_[2];
    Svf bump_[2];
    OnePole dull_[2], lowcut_[2], stopLp_[2], tilt_[2], hissLp_[2];
    DcBlock dc_[2];
    Smooth mix_, out_, drive_;
    Rng rng_;
    StepClock clock_;
    Mode mode_ = Lock;
    float spd_ = 1.0f, stopPh_ = 0.0f;
    double freePos_ = 0.0, freeLag_ = 0.0, xfPos_ = 0.0;
    float xf_ = 0.0f, xfSpd_ = 1.0f;
    Event ev_;
    float evGain_ = 0.0f;
    float wowPh_ = 0.0f, flPh_ = 0.0f, wowRnd_ = 0.0f, wowRndT_ = 0.0f, spike_ = 0.0f;
    int wowRndCount_ = 0;
    float hold_[2] = {0, 0}, holdCount_[2] = {0, 0};
    float drop_[2] = {1, 1}, dropTarget_[2] = {1, 1};
    int dropLeft_[2] = {0, 0};
};

MPC_PLUGIN_ENTRY(Grendel)
