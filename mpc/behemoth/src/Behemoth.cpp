// Behemoth.cpp - binaryimage's analog-style drum machine for the MPC.
//
// SPDX-License-Identifier: MIT
//
// Pads 1-12: the kit, one drum per pad (DrumBrute Impact-style voices)
//   1 Kick   2 Kick 2   3 Snare   4 Clap   5 Rim   6 Low Tom   7 Mid Tom   8 High Tom
//   9 Closed Hat   10 Open Hat (closed chokes it)   11 Cymbal   12 FM Drum
// Pads 13-16: the Mother (a DFAM-style voice): two VCOs + noise into a ladder filter, with
//   decay envelopes on pitch, filter and amp, and an 8-step pitch/velocity sequencer.
//   13 hit + advance one step   14 start the sequence   15 stop it   16 back to step 1
//
// Every drum has its own Output: Main or Out 2-8 (16 output channels = 8 stereo pairs), so each
// can go to its own MPC mixer channel. A drum sent to a pair the host doesn't give us plays on
// Main instead. The Routing switch picks a ready-made layout, or Custom to use each drum's own.
//
// drums ─► per-drum drive, level, pan ─► output pair ─► Grit (saturation) ─► Glue (gentle bus
// compression) ─► Volume

#include "mpc_plugin.h"
#include "dsp/tempo.h"
#include "bi_fx.h"
#include "golem_dsp.h"       // the ladder filter and polyBLEP from Golem
#include <cmath>
#include <cstdio>

using namespace bi;

namespace {
const int kKit = 12, kPairs = 8, kSteps = 8, kNumOuts = kPairs * 2;
enum Drum { dKick = 0, dKick2, dSnare, dClap, dRim, dTomL, dTomM, dTomH, dCHat, dOHat, dCym, dFM, dMother };
const char* kDrumNames[kKit + 1] = {"Kick", "Kick 2", "Snare", "Clap", "Rim", "Low Tom", "Mid Tom", "High Tom",
                                    "Closed Hat", "Open Hat", "Cymbal", "FM Drum", "Mother"};
const char* kDrumIds[kKit + 1] = {"kick", "kick2", "snare", "clap", "rim", "tom_l", "tom_m", "tom_h", "chat", "ohat", "cym", "fm", "mother"};
// the two character knobs of each kit drum
const char* kCharA[kKit] = {"Punch", "Sweep", "Snappy", "Spread", "Click", "Bend", "Bend", "Bend", "Tone", "Tone", "Tone", "FM"};
const char* kCharB[kKit] = {"Click", "Sub", "Tone", "Tone", "Ring", "Noise", "Noise", "Noise", "Metal", "Metal", "Metal", "Ratio"};
const float kDefA[kKit] = {50, 50, 55, 50, 50, 45, 45, 45, 55, 55, 50, 50};
const float kDefB[kKit] = {40, 50, 50, 50, 40, 20, 20, 20, 50, 50, 60, 40};
const float kDefDecay[kKit] = {45, 60, 40, 40, 20, 50, 45, 40, 20, 50, 65, 40};
const float kDefPan[kKit] = {0, 0, 0, 0, 10, -35, 0, 35, 25, 25, -25, 15};
float skewFor(float lo, float hi, float centre) { return std::log(0.5f) / std::log((centre - lo) / (hi - lo)); }
const int kSeqDivs[] = {2, 4, 7, 10};             // 1/32 1/16 1/8 1/4
// 808-style metal: six square waves at inharmonic ratios
const float kMetal[6] = {205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f};
}

enum P
{
    kVolume = 0, kRouting, kGrit, kGlue, kAccent, kSwing,
    kDrums,                                    // 8 per drum: tune decay level pan drive A B out
    kMoBase = kDrums + 8 * kKit,
    kMoTune = kMoBase, kMoDecay, kMoLevel, kMoPan, kMoDrive, kMoOut,
    kV1Wave, kV2Wave, kV1Lvl, kV2Lvl, kNoiseLvl, kV2Tune, kFM, kSync,
    kCutoff, kReso, kFltMode, kPitchEgAmt, kPitchEgDecay, kFltEgAmt, kFltEgDecay, kNoiseVcf,
    kSeqMode, kSeqRate, kSeqLen, kSeqPitchDst, kSeqVelVcf,
    kSeqPitch, kSeqVel = kSeqPitch + kSteps,
    kNumParams = kSeqVel + kSteps
};
enum DrumOff { oTune = 0, oDecay, oLevel, oPan, oDrive, oA, oB, oOut };
enum Routing { rCustom = 0, rAllMain, rKickSnare, rGroups, rEvery };

class Behemoth : public mpc::Plugin
{
public:
    static mpc::PluginInfo makeInfo()
    {
        mpc::PluginInfo i;
        i.name = "Behemoth";
        i.vendor = "binaryimage";
        i.uniqueId = MPC_FOURCC('B', 'h', 'M', '5');   // must match "uid" in plugin.json
        i.version = 1000;
        i.isInstrument = true;
        i.numInputs = 0;
        i.numOutputs = kNumOuts;
        return i;
    }

    Behemoth() : mpc::Plugin(makeInfo())
    {
        using namespace mpc;
        std::vector<std::string> outs = {"Main"};
        for (int k = 2; k <= kPairs; ++k) outs.push_back("Out " + std::to_string(k));
        addParam(floatParam("volume", "Volume", -30.0f, 6.0f, -6.0f, "dB", 1));
        addParam(choiceParam("routing", "Routing", {"Custom", "All Main", "Kick/Snare", "Groups", "Every Drum"}, rAllMain));
        addParam(floatParam("grit", "Grit", 0.0f, 100.0f, 10.0f, "%", 0));
        addParam(floatParam("glue", "Glue", 0.0f, 100.0f, 20.0f, "%", 0));
        addParam(floatParam("accent", "Accent", 0.0f, 100.0f, 60.0f, "%", 0));
        addParam(floatParam("swing", "Swing", 0.0f, 75.0f, 0.0f, "%", 0));
        char id[40], nm[40];
        for (int d = 0; d < kKit; ++d)
        {
            auto I = [&](const char* s) { std::snprintf(id, sizeof id, "%s_%s", kDrumIds[d], s); return id; };
            auto N = [&](const char* s) { std::snprintf(nm, sizeof nm, "%s %s", kDrumNames[d], s); return nm; };
            addParam(floatParam(I("tune"), N("Tune"), -24.0f, 24.0f, 0.0f, "", 1));
            addParam(floatParam(I("decay"), N("Decay"), 0.0f, 100.0f, kDefDecay[d], "%", 0));
            addParam(floatParam(I("level"), N("Level"), 0.0f, 100.0f, 80.0f, "%", 0));
            addParam(floatParam(I("pan"), N("Pan"), -100.0f, 100.0f, kDefPan[d], "", 0));
            addParam(floatParam(I("drive"), N("Drive"), 0.0f, 100.0f, 0.0f, "%", 0));
            addParam(floatParam(I("a"), N(kCharA[d]), 0.0f, 100.0f, kDefA[d], "%", 0));
            addParam(floatParam(I("b"), N(kCharB[d]), 0.0f, 100.0f, kDefB[d], "%", 0));
            addParam(choiceParam(I("out"), N("Out"), outs, 0));
        }
        addParam(floatParam("mother_tune", "Mother Tune", -24.0f, 24.0f, 0.0f, "", 1));
        addParam(floatParam("mother_decay", "Mother Decay", 0.0f, 100.0f, 40.0f, "%", 0));
        addParam(floatParam("mother_level", "Mother Level", 0.0f, 100.0f, 80.0f, "%", 0));
        addParam(floatParam("mother_pan", "Mother Pan", -100.0f, 100.0f, 0.0f, "", 0));
        addParam(floatParam("mother_drive", "Mother Drive", 0.0f, 100.0f, 20.0f, "%", 0));
        addParam(choiceParam("mother_out", "Mother Out", outs, 0));
        addParam(choiceParam("v1_wave", "VCO1 Wave", {"Triangle", "Square"}, 0));
        addParam(choiceParam("v2_wave", "VCO2 Wave", {"Triangle", "Square"}, 1));
        addParam(floatParam("v1_lvl", "VCO1 Level", 0.0f, 100.0f, 80.0f, "%", 0));
        addParam(floatParam("v2_lvl", "VCO2 Level", 0.0f, 100.0f, 50.0f, "%", 0));
        addParam(floatParam("noise_lvl", "Noise Level", 0.0f, 100.0f, 10.0f, "%", 0));
        addParam(floatParam("v2_tune", "VCO2 Tune", -24.0f, 24.0f, 7.0f, "", 1));
        addParam(floatParam("fm", "1>2 FM", 0.0f, 100.0f, 0.0f, "%", 0));
        addParam(boolParam("sync", "Hard Sync", false));
        addParam(floatParam("cutoff", "Cutoff", 30.0f, 16000.0f, 1200.0f, "", 0, skewFor(30.0f, 16000.0f, 800.0f)));
        addParam(floatParam("reso", "Resonance", 0.0f, 100.0f, 30.0f, "%", 0));
        addParam(choiceParam("flt_mode", "Filter Mode", {"Low Pass", "High Pass"}, 0));
        addParam(floatParam("pitch_eg", "Pitch EG", -100.0f, 100.0f, 40.0f, "", 0));
        addParam(floatParam("pitch_eg_dec", "Pitch EG Decay", 0.0f, 100.0f, 25.0f, "%", 0));
        addParam(floatParam("flt_eg", "Filter EG", -100.0f, 100.0f, 50.0f, "", 0));
        addParam(floatParam("flt_eg_dec", "Filter EG Decay", 0.0f, 100.0f, 35.0f, "%", 0));
        addParam(floatParam("noise_vcf", "Noise>VCF", 0.0f, 100.0f, 0.0f, "%", 0));
        addParam(choiceParam("seq_mode", "Seq Mode", {"Off", "Transport", "Pads"}, 2));
        addParam(choiceParam("seq_rate", "Seq Rate", {"1/32", "1/16", "1/8", "1/4"}, 1));
        addParam(intParam("seq_len", "Seq Length", 1, kSteps, kSteps, ""));
        addParam(choiceParam("seq_pitch_dst", "Seq Pitch To", {"Both VCOs", "VCO1", "VCO2"}, 0));
        addParam(floatParam("seq_vel_vcf", "Seq Vel>VCF", 0.0f, 100.0f, 50.0f, "%", 0));
        for (int s = 0; s < kSteps; ++s)
        {
            std::snprintf(id, sizeof id, "pitch_%d", s + 1); std::snprintf(nm, sizeof nm, "Pitch %d", s + 1);
            addParam(floatParam(id, nm, -24.0f, 24.0f, 0.0f, "", 0));
        }
        for (int s = 0; s < kSteps; ++s)
        {
            std::snprintf(id, sizeof id, "vel_%d", s + 1); std::snprintf(nm, sizeof nm, "Velocity %d", s + 1);
            addParam(floatParam(id, nm, 0.0f, 100.0f, s % 2 ? 60.0f : 100.0f, "%", 0));
        }
        if (numParams() != kNumParams) mpc::log("Behemoth: parameter count mismatch (%d vs %d)", numParams(), (int)kNumParams);
        addPrograms();
    }

    void prepare(double sampleRate, int) override
    {
        sr_ = (float)sampleRate;
        ladder_.setSampleRate(sr_);
        vol_.prepare(30.0f, sr_);
        reset();
    }

    void reset() override
    {
        for (auto& v : v_) v = Voice();
        ladder_.reset(); hp_.reset();
        mo_ = Mother();
        for (auto& c : comp_) c = 0.0f;
        clock_.reset(); seqPos_ = -1; padRun_ = false; wasPlaying_ = false; seqCount_ = 0.0f; seqSwingHold_ = 0;
        vol_.snap(dbToGain(get(kVolume)));
        for (int k = 0; k < kNumOuts; ++k) dc_[k].reset();
    }

    bool formatValue(int index, float plain, std::string& out) override
    {
        char b[40];
        const bool tune = index == kMoTune || index == kV2Tune || (index >= kDrums && index < kMoBase && (index - kDrums) % 8 == oTune);
        if (tune || (index >= kSeqPitch && index < kSeqVel))
        {
            std::snprintf(b, sizeof b, std::fabs(plain) < 0.05f ? "0 st" : (index >= kSeqPitch ? "%+.0f st" : "%+.1f st"), (double)plain); out = b; return true;
        }
        const bool pan = index == kMoPan || (index >= kDrums && index < kMoBase && (index - kDrums) % 8 == oPan);
        if (pan)
        {
            const int p = (int)std::lround(plain);
            if (p == 0) out = "C"; else { std::snprintf(b, sizeof b, p < 0 ? "L%d" : "R%d", p < 0 ? -p : p); out = b; }
            return true;
        }
        if (index == kCutoff) { if (plain >= 1000.0f) std::snprintf(b, sizeof b, "%.1f kHz", (double)(plain * 0.001f)); else std::snprintf(b, sizeof b, "%.0f Hz", (double)plain); out = b; return true; }
        if (index == kPitchEgAmt || index == kFltEgAmt) { std::snprintf(b, sizeof b, "%+d", (int)std::lround(plain)); out = b; return true; }
        return false;
    }

    void process(mpc::ProcessContext& ctx) override
    {
        const int n = ctx.numFrames;
        const int pairsAvail = std::max(1, std::min(kPairs, ctx.numOutputs / 2));
        for (int c = 0; c < ctx.numOutputs; ++c) for (int i = 0; i < n; ++i) ctx.outputs[c][i] = 0.0f;

        // ---- per-block drum settings
        const int routing = getInt(kRouting);
        const float accent = get(kAccent) * 0.01f;
        for (int d = 0; d <= kKit; ++d)
        {
            Settings& s = set_[d];
            const int base = d < kKit ? kDrums + 8 * d : kMoBase;
            s.tune = get(base + oTune);
            s.decay = get(base + oDecay) * 0.01f;
            s.level = get(base + oLevel) * 0.01f;
            const float pan = get(base + oPan) * 0.01f;
            s.panL = std::sqrt(0.5f * (1.0f - pan)) * 1.4142f; s.panR = std::sqrt(0.5f * (1.0f + pan)) * 1.4142f;
            s.drive = get(base + oDrive) * 0.01f;
            if (d < kKit) { s.a = get(base + oA) * 0.01f; s.b = get(base + oB) * 0.01f; }
            int pair = routeOf(d, routing, getInt(d < kKit ? base + oOut : kMoOut));
            s.pair = pair < pairsAvail ? pair : 0;
            if (d < kKit) { prepDrum(d, s); if (v_[d].active) voiceFilters(d, v_[d], s); }
        }
        moSettings();
        const float grit = get(kGrit) * 0.01f, glue = get(kGlue) * 0.01f;
        vol_.target = dbToGain(get(kVolume));
        const float gGain = 1.0f + grit * 4.0f, gComp = 1.0f / (1.0f + grit * 1.5f);
        const float thr = dbToGain(-6.0f - 18.0f * glue), ratioInv = 1.0f / (1.0f + 3.0f * glue);
        const float att = 1.0f - std::exp(-1.0f / (0.003f * sr_)), rel = 1.0f - std::exp(-1.0f / (0.12f * sr_));

        // ---- Mother sequencer clock
        const int seqMode = getInt(kSeqMode);
        const bool playing = ctx.transport.playing;
        int sr = getInt(kSeqRate); if (sr < 0) sr = 0; if (sr > 3) sr = 3;
        const float stepBeats = mpc::dsp::divisionBeats(kSeqDivs[sr]);
        clock_.begin(ctx, stepBeats, 0.0f, true);
        const double bpm = (ctx.transport.bpm > 1.0 && ctx.transport.bpm < 999.0) ? ctx.transport.bpm : 120.0;
        const float stepLen = (float)(stepBeats * 60.0 / bpm) * sr_;
        if (seqMode == 1 && playing && !wasPlaying_) { seqPos_ = -1; clock_.reset(); }
        wasPlaying_ = playing;
        const bool seqRunning = (seqMode == 1 && playing) || (seqMode == 2 && padRun_);
        const float swing = get(kSwing) * 0.01f;

        int ev = 0;
        for (int i = 0; i < n; ++i)
        {
            while (ev < ctx.numMidiEvents && ctx.midi[ev].sampleOffset <= i) { handleMidi(ctx.midi[ev], accent); ++ev; }

            if (seqRunning)
            {
                bool tick;
                if (clock_.locked) tick = clock_.tick();
                else { seqCount_ += 1.0f; tick = seqCount_ >= stepLen || seqPos_ < 0; if (tick) seqCount_ = 0.0f; }
                if (tick)
                {
                    // swing: every second step waits a little
                    const int next = nextStep();
                    if (swing > 0.0f && (next & 1)) seqSwingHold_ = (int)(swing * 0.5f * stepLen);
                    else seqSwingHold_ = 0;
                    pendingStep_ = next;
                    if (seqSwingHold_ == 0) { fireStep(pendingStep_); pendingStep_ = -1; }
                }
                else if (pendingStep_ >= 0 && --seqSwingHold_ <= 0) { fireStep(pendingStep_); pendingStep_ = -1; }
            }
            else if (seqMode == 2 || seqMode == 0) { if (!padRun_) { /* idle */ } }

            for (int d = 0; d < kKit; ++d)
            {
                Voice& v = v_[d];
                if (!v.active) continue;
                const float y = renderDrum(d, v, set_[d]);
                const Settings& s = set_[d];
                float o = y * s.level * v.vel;
                if (s.drive > 0.001f) o = fastTanh(o * (1.0f + 6.0f * s.drive)) / (1.0f + 1.5f * s.drive);
                ctx.outputs[s.pair * 2][i] += o * s.panL;
                ctx.outputs[s.pair * 2 + 1][i] += o * s.panR;
            }
            if (mo_.active)
            {
                const Settings& s = set_[dMother];
                const float o = renderMother() * s.level;
                ctx.outputs[s.pair * 2][i] += o * s.panL;
                ctx.outputs[s.pair * 2 + 1][i] += o * s.panR;
            }
        }
        while (ev < ctx.numMidiEvents) { handleMidi(ctx.midi[ev], accent); ++ev; }

        // ---- bus: grit, glue, volume on every pair in use
        for (int p = 0; p < pairsAvail; ++p)
        {
            float* L = ctx.outputs[p * 2];
            float* R = ctx.outputs[p * 2 + 1];
            float& env = comp_[p];
            for (int i = 0; i < n; ++i)
            {
                float l = L[i], r = R[i];
                if (grit > 0.001f) { l = fastTanh(l * gGain) * gComp; r = fastTanh(r * gGain) * gComp; }
                if (glue > 0.001f)
                {
                    const float lv = std::max(std::fabs(l), std::fabs(r));
                    env += (lv > env ? att : rel) * (lv - env);
                    env = undenorm(env);
                    if (env > thr) { const float g = std::pow(env / thr, ratioInv - 1.0f); l *= g; r *= g; }
                }
                const float vg = p == 0 ? vol_.next() : vol_.v;
                l = dc_[p * 2].process(l) * vg; r = dc_[p * 2 + 1].process(r) * vg;
                if (!std::isfinite(l) || !std::isfinite(r)) { l = r = 0.0f; }
                L[i] = clampf(l, -4.0f, 4.0f); R[i] = clampf(r, -4.0f, 4.0f);
            }
        }
    }

private:
    struct Settings { float tune = 0, decay = 0.5f, level = 0.8f, panL = 1, panR = 1, drive = 0, a = 0.5f, b = 0.5f; int pair = 0;
                     float tr = 1, c1 = 0.999f, c2 = 0.999f, cp = 0.999f, gap = 0.01f, ratio = 1; };
    struct Voice
    {
        bool active = false;
        float vel = 1.0f, t = 0.0f;              // t = seconds since the hit
        float ph[6] = {0, 0, 0, 0, 0, 0};
        float env = 0.0f, env2 = 0.0f, penv = 0.0f, clapT = 0.0f;
        Svf f1, f2;
        OnePole lp, hp;
        Rng rng;
    };
    struct Mother
    {
        bool active = false;
        float ph1 = 0, ph2 = 0, ampEnv = 0, pEnv = 0, fEnv = 0, vel = 1, stepPitch = 0, stepVel = 1;
        golem::Noise noise;
    };

    int routeOf(int d, int routing, int own) const
    {
        switch (routing)
        {
        case rAllMain: return 0;
        case rKickSnare: return (d == dKick || d == dKick2) ? 1 : ((d == dSnare || d == dClap || d == dRim) ? 2 : 0);
        case rGroups:
            if (d == dKick || d == dKick2) return 1;
            if (d == dSnare || d == dClap || d == dRim) return 2;
            if (d == dTomL || d == dTomM || d == dTomH) return 3;
            if (d == dCHat || d == dOHat || d == dCym) return 4;
            if (d == dFM) return 5;
            if (d == dMother) return 6;
            return 0;
        case rEvery:
        {
            static const int map[kKit + 1] = {1, 2, 3, 4, 4, 5, 5, 5, 6, 6, 6, 7, 0};
            return map[d];
        }
        default: return own;
        }
    }

    void handleMidi(const mpc::MidiEvent& m, float accent)
    {
        if (!m.isNoteOn()) return;
        const int pad = ((m.data[1] - 36) % 16 + 16) % 16;      // pads 1-16 = C1..D#2, repeating up the keyboard
        const float vel = (float)m.data[2] / 127.0f;
        const float g = (1.0f - accent) + accent * vel * 1.15f;
        if (pad >= 0 && pad < kKit) trigger(pad, g);
        else if (pad == 12) { fireStep(nextStep(), g); }
        else if (pad == 13) { padRun_ = true; seqPos_ = -1; seqCount_ = 0.0f; }
        else if (pad == 14) { padRun_ = false; }
        else if (pad == 15) { seqPos_ = -1; }
    }

    void trigger(int d, float vel)
    {
        Voice& v = v_[d];
        v.active = true; v.vel = vel; v.t = 0.0f; v.env = 1.0f; v.env2 = 1.0f; v.penv = 1.0f; v.clapT = 0.0f;
        if (d == dKick || d == dKick2 || d == dTomL || d == dTomM || d == dTomH || d == dFM) v.ph[0] = v.ph[1] = 0.0f;
        if (d == dCHat && v_[dOHat].active) v_[dOHat].env *= 0.02f;            // closed hat chokes the open one
        if (d == dOHat || d == dCHat || d == dCym) { v.f1.reset(); v.f2.reset(); }
        voiceFilters(d, v, set_[d]);
    }

    // decay knob (0..1) -> per-sample multiplier for a time constant between lo and hi seconds
    float decayCoef(float k, float lo, float hi) const { const float tau = lo * std::pow(hi / lo, k); return std::exp(-1.0f / (tau * sr_)); }

    float sq(float& ph, float hz) { ph += hz / sr_; if (ph >= 1.0f) ph -= 1.0f; return ph < 0.5f ? 1.0f : -1.0f; }
    float sine(float& ph, float hz) { ph += hz / sr_; if (ph >= 1.0f) ph -= 1.0f; return std::sin(kTwoPi * ph); }

    // per-block: envelope coefficients and pitch for one drum (no exp/pow per sample)
    void prepDrum(int d, Settings& s)
    {
        s.tr = std::exp2(s.tune * (1.0f / 12.0f));
        const float k = s.decay;
        auto ex = [this](float sec) { return std::exp(-1.0f / (std::max(sec, 0.0005f) * sr_)); };
        switch (d)
        {
        case dKick:  s.c1 = decayCoef(k, 0.06f, 1.2f); s.cp = ex(0.008f + 0.03f * s.a); break;
        case dKick2: s.c1 = decayCoef(k, 0.1f, 3.0f); s.cp = ex(0.02f + 0.25f * s.a); break;
        case dSnare: s.c1 = decayCoef(k * 0.6f, 0.05f, 0.4f); s.c2 = decayCoef(k, 0.06f, 0.7f); break;
        case dClap:  s.c1 = decayCoef(k, 0.04f, 0.6f); s.gap = 0.006f + 0.016f * s.a; s.c2 = ex(0.004f); break;
        case dRim:   s.c1 = decayCoef(k, 0.01f, 0.12f); break;
        case dTomL: case dTomM: case dTomH: s.c1 = decayCoef(k, 0.08f, 1.0f); s.c2 = ex(0.02f); s.cp = ex(0.06f); break;
        case dCHat:  s.c1 = decayCoef(k, 0.01f, 0.12f); break;
        case dOHat:  s.c1 = decayCoef(k, 0.08f, 0.8f); break;
        case dCym:   s.c1 = decayCoef(k, 0.3f, 3.0f); break;
        case dFM:    s.c1 = decayCoef(k, 0.05f, 1.0f); s.c2 = ex(0.01f + 0.08f * k); s.ratio = 0.5f + std::floor(s.b * 8.0f) * 0.5f + 0.13f * s.b; break;
        default: break;
        }
    }
    void voiceFilters(int d, Voice& v, const Settings& s)
    {
        switch (d)
        {
        case dSnare: v.f1.set(1500.0f + 6000.0f * s.b, 0.8f, sr_); v.hp.set(4000.0f, sr_); break;
        case dClap:  v.f1.set(700.0f + 2500.0f * s.b, 1.6f, sr_); break;
        case dRim:   v.hp.set(300.0f, sr_); break;
        case dTomL: case dTomM: case dTomH:
        {
            const float base = d == dTomL ? 80.0f : (d == dTomM ? 120.0f : 175.0f);
            v.f1.set(base * 6.0f * s.tr, 0.7f, sr_); break;
        }
        case dCHat: case dOHat: case dCym:
        {
            const float f = d == dCym ? 3500.0f + 4000.0f * s.a : 6000.0f + 5000.0f * s.a;
            v.f1.set(f * std::sqrt(s.tr), d == dCym ? 0.8f : 1.2f, sr_);
            v.f2.set(f * 0.8f * std::sqrt(s.tr), 0.707f, sr_);
            break;
        }
        default: break;
        }
    }


    float renderDrum(int d, Voice& v, const Settings& s)
    {
        const float tr = s.tr;
        float y = 0.0f;
        v.t += 1.0f / sr_;
        switch (d)
        {
        case dKick:
        {
            v.penv *= s.cp; v.env *= s.c1;
            y = sine(v.ph[0], 50.0f * tr * (1.0f + (2.0f + 7.0f * s.a) * v.penv)) * v.env;
            if (v.t < 0.006f) y += v.rng.bi() * s.b * 0.8f * (1.0f - v.t / 0.006f);
            y = fastTanh(y * 1.4f);
            break;
        }
        case dKick2:
        {
            v.penv *= s.cp; v.env *= s.c1;
            const float x = sine(v.ph[0], 45.0f * tr * (1.0f + 3.0f * v.penv));
            y = (x * (1.0f - s.b) + fastTanh(x * 3.0f) * s.b) * v.env;
            break;
        }
        case dSnare:
        {
            v.env *= s.c1; v.env2 *= s.c2;
            const float body = 0.6f * sine(v.ph[0], 185.0f * tr) + 0.4f * sine(v.ph[1], 330.0f * tr);
            const float nz = v.f1.processBP(v.rng.bi()) * 1.6f + v.hp.hp(v.rng.bi()) * 0.4f * s.b;
            y = body * v.env * (1.0f - 0.5f * s.a) + nz * v.env2 * s.a * 1.3f;
            break;
        }
        case dClap:
        {
            const int hit = (int)(v.t / s.gap);
            float e;
            if (hit < 3) { if ((int)((v.t - 1.0f / sr_) / s.gap) != hit) v.env2 = 1.0f; v.env2 *= s.c2; e = v.env2; }
            else { v.env *= s.c1; e = v.env; }
            y = v.f1.processBP(v.rng.bi()) * e * 2.2f;
            break;
        }
        case dRim:
        {
            v.env *= s.c1;
            y = (0.6f * sine(v.ph[0], 1700.0f * tr) + 0.5f * s.b * sine(v.ph[1], 480.0f * tr)) * v.env;
            if (v.t < 0.002f) y += v.rng.bi() * s.a;
            y = v.hp.hp(y);
            break;
        }
        case dTomL: case dTomM: case dTomH:
        {
            const float base = d == dTomL ? 80.0f : (d == dTomM ? 120.0f : 175.0f);
            v.penv *= s.cp; v.env *= s.c1; v.env2 *= s.c2;
            y = sine(v.ph[0], base * tr * (1.0f + 0.8f * s.a * v.penv)) * v.env + v.f1.processBP(v.rng.bi()) * s.b * v.env2;
            break;
        }
        case dCHat: case dOHat: case dCym:
        {
            const float pitch = tr * (d == dCym ? 0.8f : 1.0f);
            float m = 0.0f;
            for (int k = 0; k < 6; ++k) m += sq(v.ph[k], kMetal[k] * pitch);
            m *= 0.16f;
            const float src = m * s.b + v.rng.bi() * (1.0f - s.b) * 0.7f;
            v.env *= s.c1;
            y = v.f2.processHP(v.f1.processBP(src)) * 2.0f * v.env;
            break;
        }
        case dFM:
        {
            v.env *= s.c1; v.env2 *= s.c2;
            const float hz = 110.0f * tr;
            const float mod = sine(v.ph[1], hz * s.ratio) * s.a * 6.0f * v.env2;
            v.ph[0] += hz / sr_ * (1.0f + mod); v.ph[0] -= std::floor(v.ph[0]);
            y = std::sin(kTwoPi * v.ph[0]) * v.env;
            break;
        }
        default: break;
        }
        if (v.env < 0.0003f && v.t > 0.05f) v.active = false;
        return y;
    }

    // ---- the Mother -------------------------------------------------------------------------------
    struct MoS { float tune, v2tune, fm, lvl1, lvl2, noise, cut, reso, pegAmt, pegC, fegAmt, fegC, ampC, noiseVcf, velVcf, drive; int w1, w2, mode, pdst; bool sync; };
    void moSettings()
    {
        MoS& m = mos_;
        m.tune = get(kMoTune); m.v2tune = get(kV2Tune); m.fm = get(kFM) * 0.01f;
        m.lvl1 = get(kV1Lvl) * 0.01f; m.lvl2 = get(kV2Lvl) * 0.01f; m.noise = get(kNoiseLvl) * 0.01f;
        m.cut = get(kCutoff); m.reso = get(kReso) * 0.01f; m.mode = getInt(kFltMode);
        m.pegAmt = get(kPitchEgAmt) * 0.01f; m.pegC = decayCoef(get(kPitchEgDecay) * 0.01f, 0.005f, 1.5f);
        m.fegAmt = get(kFltEgAmt) * 0.01f; m.fegC = decayCoef(get(kFltEgDecay) * 0.01f, 0.005f, 2.0f);
        m.ampC = decayCoef(get(kMoDecay) * 0.01f, 0.01f, 3.0f);
        m.noiseVcf = get(kNoiseVcf) * 0.01f; m.velVcf = get(kSeqVelVcf) * 0.01f; m.drive = get(kMoDrive) * 0.01f;
        m.w1 = getInt(kV1Wave); m.w2 = getInt(kV2Wave); m.pdst = getInt(kSeqPitchDst); m.sync = getBool(kSync);
    }

    int nextStep()
    {
        const int len = std::max(1, std::min(kSteps, getInt(kSeqLen)));
        seqPos_ = seqPos_ < 0 ? 0 : (seqPos_ + 1) % len;
        return seqPos_;
    }

    void fireStep(int step, float padVel = 1.0f)
    {
        mo_.stepPitch = get(kSeqPitch + step);
        mo_.stepVel = get(kSeqVel + step) * 0.01f * padVel;
        if (mo_.stepVel <= 0.001f) return;          // a 0-velocity step is a rest
        mo_.active = true; mo_.ampEnv = 1.0f; mo_.pEnv = 1.0f; mo_.fEnv = 1.0f; mo_.vel = mo_.stepVel;
    }

    float tri(float ph) const { return 1.0f - 4.0f * std::fabs(ph - 0.5f); }

    float renderMother()
    {
        const MoS& m = mos_;
        Mother& o = mo_;
        o.pEnv *= m.pegC; o.fEnv *= m.fegC; o.ampEnv *= m.ampC;
        const float stepSt = o.stepPitch;
        const float p1 = m.tune + (m.pdst != 2 ? stepSt : 0.0f) + m.pegAmt * 36.0f * o.pEnv;
        const float p2 = m.tune + m.v2tune + (m.pdst != 1 ? stepSt : 0.0f) + m.pegAmt * 36.0f * o.pEnv;
        const float f1 = 55.0f * std::exp2(p1 * (1.0f / 12.0f));
        const float dt1 = clampf(f1 / sr_, 1e-6f, 0.45f);
        o.ph1 += dt1;
        bool wrap = false;
        if (o.ph1 >= 1.0f) { o.ph1 -= 1.0f; wrap = true; }
        const float s1 = m.w1 == 0 ? tri(o.ph1) : (o.ph1 < 0.5f ? 1.0f : -1.0f) + golem::polyBlep(o.ph1, dt1) - golem::polyBlep(std::fmod(o.ph1 + 0.5f, 1.0f), dt1);
        const float f2 = 55.0f * std::exp2(p2 * (1.0f / 12.0f)) * (1.0f + m.fm * 2.5f * s1);
        const float dt2 = clampf(std::fabs(f2) / sr_, 1e-6f, 0.45f);
        o.ph2 += dt2; if (o.ph2 >= 1.0f) o.ph2 -= 1.0f;
        if (m.sync && wrap) o.ph2 = o.ph1 * dt2 / dt1;
        const float s2 = m.w2 == 0 ? tri(o.ph2) : (o.ph2 < 0.5f ? 1.0f : -1.0f) + golem::polyBlep(o.ph2, dt2) - golem::polyBlep(std::fmod(o.ph2 + 0.5f, 1.0f), dt2);
        const float nz = o.noise.process(0.0f);
        const float mix = (s1 * m.lvl1 + s2 * m.lvl2 + nz * m.noise) * 0.6f;
        const float cutSt = m.fegAmt * 72.0f * o.fEnv + m.velVcf * 24.0f * (o.vel - 0.5f) + m.noiseVcf * 24.0f * nz;
        const float fc = m.cut * std::exp2(clampf(cutSt, -80.0f, 80.0f) * (1.0f / 12.0f));
        float y = ladder_.process(mix, fc, m.reso, 0, m.drive);
        if (m.mode == 1) { hp_.set(fc, 0.6f + m.reso * 4.0f, sr_); y = hp_.processHP(mix) * (1.0f + m.drive); }
        y *= o.ampEnv * o.vel * 1.8f;
        if (m.drive > 0.001f) y = fastTanh(y * (1.0f + 3.0f * m.drive)) / (1.0f + m.drive);
        if (o.ampEnv < 0.0002f) o.active = false;
        return y;
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
        #include "behemoth_presets.inc"
    }

    float sr_ = 44100.0f;
    Voice v_[kKit];
    Settings set_[kKit + 1];
    MoS mos_{};
    Mother mo_;
    golem::Ladder ladder_;
    Svf hp_;
    DcBlock dc_[kNumOuts];
    Smooth vol_;
    StepClock clock_;
    float comp_[kPairs] = {};
    int seqPos_ = -1, pendingStep_ = -1, seqSwingHold_ = 0;
    bool padRun_ = false, wasPlaying_ = false;
    float seqCount_ = 0.0f;
};

MPC_PLUGIN_ENTRY(Behemoth)
