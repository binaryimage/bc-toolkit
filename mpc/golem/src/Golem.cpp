// Golem.cpp - binaryimage's ARP 2600-style semi-modular synth for the MPC, with a 1601-style
// step sequencer and 30 colour-coded patch cables you plug in by touch.
//
// SPDX-License-Identifier: MIT
//
// Like the real thing, Golem makes sound with nothing patched: the main connections are wired
// behind the panel (the "normals"):
//
//   KBD / SEQ pitch ─► VCO1, VCO2, VCO3 (VCO3 can go LF or leave the keyboard)
//   VCO2 sine ─► VCO1 FM      VCO1 square ─► VCO2 FM      noise ─► VCO3 FM      VCO3 sine ─► VCO2 PWM
//   VCO1 + VCO2 ─► ring mod   noise ─► S&H ─► lag
//   VCO1/2/3 + noise + ring ─► mixer ─► VCF (4012 or 4072) ─► VCA (ADSR or AR) ─► pan ─► springs ─► out
//   VCO2 sine ─► VCF FM       ADSR ─► VCF      KBD ─► VCF
//
// The 30 cables add any output to any input on top of that. Plugging into VCF IN, RING IN, S&H IN
// or LAG IN replaces the normal, like a real jack breaking its normalled connection.
//
// Touch patching: tap an output jack, then an input jack. The worker thread (never the audio
// thread) puts the cable into the next free slot and lights both jacks in the cable's colour.
// Tap a lit input jack with nothing picked to pull its cable.

#include "mpc_plugin.h"
#include "dsp/adsr.h"
#include "dsp/tempo.h"
#include "bi_fx.h"
#include "golem_dsp.h"
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <thread>

using namespace bi;
using namespace golem;

// ---- jacks -----------------------------------------------------------------------------------
enum Src
{
    sNone = 0, sV1Saw, sV1Sq, sV2Saw, sV2Pulse, sV2Tri, sV2Sine, sV3Saw, sV3Pulse, sV3Tri, sV3Sine,
    sNoise, sRing, sSH, sLag, sADSR, sAR, sVCF, sSeqA, sSeqB, sSeqC, sClock, sKbd, sVelo, sWheel, sAT,
    sNumSrc
};
enum Dst
{
    dNone = 0, dV1Pitch, dV2Pitch, dV3Pitch, dAllPitch, dV2PW, dV3PW, dCutoff, dReso, dVcfIn, dVca,
    dRingIn, dSHIn, dSHClock, dLagIn, dADSRGate, dARGate, dSeqClock, dSeqReset, dReverb, dPan,
    dNumDst
};
static const char* kSrcNames[sNumSrc] = {"None", "VCO1 Saw", "VCO1 Sq", "VCO2 Saw", "VCO2 Pulse", "VCO2 Tri", "VCO2 Sine",
    "VCO3 Saw", "VCO3 Pulse", "VCO3 Tri", "VCO3 Sine", "Noise", "Ring Mod", "S&H", "Lag", "ADSR", "AR", "VCF Out",
    "Seq A", "Seq B", "Seq C", "Clock", "KBD CV", "Velocity", "Mod Wheel", "Aftertouch"};
static const char* kDstNames[dNumDst] = {"None", "VCO1 Pitch", "VCO2 Pitch", "VCO3 Pitch", "All Pitch", "VCO2 PW", "VCO3 PW",
    "VCF Cutoff", "VCF Reso", "VCF In", "VCA", "Ring In", "S&H In", "S&H Clock", "Lag In", "ADSR Gate", "AR Gate",
    "Seq Clock", "Seq Reset", "Reverb", "Pan"};
const int kNumCables = 30, kSteps = 16, kMulti = 31;

// ---- parameters (order is permanent) -----------------------------------------------------------
enum P
{
    kVolume = 0, kVoice, kGlide, kBend, kRetrig, kVelo,
    kV1Tune, kV1Fine, kV1FM,
    kV2Tune, kV2Fine, kV2PW, kV2PWM, kV2FM, kSync,
    kV3Tune, kV3Fine, kV3PW, kV3LF, kV3Kbd, kV3FM,
    kM1Lvl, kM1Wave, kM2Lvl, kM2Wave, kM3Lvl, kM3Wave, kNoiseLvl, kRingLvl, kNoiseColor,
    kFltType, kCutoff, kReso, kFltKbd, kFltEnv, kFltFM, kDrive,
    kA, kD, kS, kR, kArA, kArR, kVcaEnv, kVcaInit,
    kShRate, kShSync, kShSrc, kLag,
    kReverb, kRevDecay, kRevTone, kPan,
    kSeqMode, kSeqDiv, kSeqRate, kSeqLen, kSeqDir, kSeqQuant, kSeqRange, kSeqGate, kSeqTrans,
    kSeqA, kSeqB = kSeqA + kSteps, kSeqC = kSeqB + kSteps, kStep = kSeqC + kSteps,
    kPickOut = kStep + kSteps, kPickIn, kUnplug,
    kCable,                                         // 3 per cable: from, to, amount
    kJackOut = kCable + 3 * kNumCables,             // display only: one per output jack (sources 1..25)
    kJackIn = kJackOut + (sNumSrc - 1),             // display only: one per input jack (dests 1..20)
    kNumParams = kJackIn + (dNumDst - 1)
};
enum SeqMode { qOff = 0, qSync, qFree, qKey };
enum StepMode { stPlay = 0, stRest, stSkip, stTie };

namespace {
float skewFor(float lo, float hi, float centre) { return std::log(0.5f) / std::log((centre - lo) / (hi - lo)); }
const int kSeqDivs[] = {2, 3, 4, 5, 7, 10, 13};          // 1/32 1/16T 1/16 1/8T 1/8 1/4 1/2
const int kNumSeqDivs = 7;
const int kShDivs[] = {-1, 2, 4, 7, 10, 13, 15};          // Free, 1/32 1/16 1/8 1/4 1/2 1/1
const float kDefAmt[dNumDst] = {0, 25, 25, 25, 25, 40, 40, 50, 50, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 50, 50};
inline float noteHz(float n) { return 440.0f * std::exp2((n - 69.0f) * (1.0f / 12.0f)); }
}

class Golem : public mpc::Plugin
{
public:
    static mpc::PluginInfo makeInfo()
    {
        mpc::PluginInfo i;
        i.name = "Golem";
        i.vendor = "binaryimage";
        i.uniqueId = MPC_FOURCC('B', 'g', 'L', '6');   // must match "uid" in plugin.json
        i.version = 1000;
        i.isInstrument = true;
        i.numInputs = 0;
        i.numOutputs = 2;
        return i;
    }

    Golem() : mpc::Plugin(makeInfo())
    {
        using namespace mpc;
        const std::vector<std::string> wave4 = {"Saw", "Pulse", "Triangle", "Sine"};
        std::vector<std::string> srcN(kSrcNames, kSrcNames + sNumSrc), dstN(kDstNames, kDstNames + dNumDst);
        std::vector<std::string> seqDiv, shSync = {"Free"};
        for (int k = 0; k < kNumSeqDivs; ++k) seqDiv.push_back(dsp::divisionName(kSeqDivs[k]));
        for (int k = 1; k < 7; ++k) shSync.push_back(dsp::divisionName(kShDivs[k]));

        addParam(floatParam("volume", "Volume", -30.0f, 6.0f, -6.0f, "dB", 1));
        addParam(choiceParam("voice", "Voice Mode", {"Mono", "Duo"}, 0));
        addParam(floatParam("glide", "Glide", 0.0f, 2000.0f, 0.0f, "", 0, skewFor(0.0f, 2000.0f, 200.0f)));
        addParam(intParam("bend", "Bend Range", 0, 12, 2, "st"));
        addParam(choiceParam("retrig", "Retrig", {"Multi", "Legato"}, 0));
        addParam(floatParam("velo", "Velocity", 0.0f, 100.0f, 0.0f, "%", 0));

        addParam(floatParam("v1_tune", "VCO1 Tune", -36.0f, 36.0f, 0.0f, "", 0));
        addParam(floatParam("v1_fine", "VCO1 Fine", -50.0f, 50.0f, 0.0f, "ct", 0));
        addParam(floatParam("v1_fm", "VCO1 FM", 0.0f, 100.0f, 0.0f, "%", 0));
        addParam(floatParam("v2_tune", "VCO2 Tune", -36.0f, 36.0f, 0.0f, "", 0));
        addParam(floatParam("v2_fine", "VCO2 Fine", -50.0f, 50.0f, 6.0f, "ct", 0));
        addParam(floatParam("v2_pw", "VCO2 PW", 5.0f, 95.0f, 50.0f, "%", 0));
        addParam(floatParam("v2_pwm", "VCO2 PWM", 0.0f, 100.0f, 0.0f, "%", 0));
        addParam(floatParam("v2_fm", "VCO2 FM", 0.0f, 100.0f, 0.0f, "%", 0));
        addParam(boolParam("sync", "Sync", false));
        addParam(floatParam("v3_tune", "VCO3 Tune", -36.0f, 36.0f, -12.0f, "", 0));
        addParam(floatParam("v3_fine", "VCO3 Fine", -50.0f, 50.0f, -5.0f, "ct", 0));
        addParam(floatParam("v3_pw", "VCO3 PW", 5.0f, 95.0f, 50.0f, "%", 0));
        addParam(boolParam("v3_lf", "VCO3 LF", false));
        addParam(boolParam("v3_kbd", "VCO3 KBD", true));
        addParam(floatParam("v3_fm", "VCO3 FM", 0.0f, 100.0f, 0.0f, "%", 0));

        addParam(floatParam("m1_lvl", "VCO1 Level", 0.0f, 100.0f, 80.0f, "%", 0));
        addParam(choiceParam("m1_wave", "VCO1 Wave", {"Saw", "Square"}, 0));
        addParam(floatParam("m2_lvl", "VCO2 Level", 0.0f, 100.0f, 0.0f, "%", 0));
        addParam(choiceParam("m2_wave", "VCO2 Wave", wave4, 1));
        addParam(floatParam("m3_lvl", "VCO3 Level", 0.0f, 100.0f, 0.0f, "%", 0));
        addParam(choiceParam("m3_wave", "VCO3 Wave", wave4, 0));
        addParam(floatParam("noise_lvl", "Noise Level", 0.0f, 100.0f, 0.0f, "%", 0));
        addParam(floatParam("ring_lvl", "Ring Level", 0.0f, 100.0f, 0.0f, "%", 0));
        addParam(floatParam("noise_color", "Noise Color", 0.0f, 100.0f, 0.0f, "", 0));

        addParam(choiceParam("vcf_type", "VCF Type", {"4012", "4072"}, 0));
        addParam(floatParam("cutoff", "Cutoff", 20.0f, 18000.0f, 2500.0f, "", 0, skewFor(20.0f, 18000.0f, 1200.0f)));
        addParam(floatParam("reso", "Resonance", 0.0f, 100.0f, 20.0f, "%", 0));
        addParam(floatParam("vcf_kbd", "VCF KBD", 0.0f, 100.0f, 50.0f, "%", 0));
        addParam(floatParam("vcf_env", "VCF Env", -100.0f, 100.0f, 40.0f, "", 0));
        addParam(floatParam("vcf_fm", "VCF FM", 0.0f, 100.0f, 0.0f, "%", 0));
        addParam(floatParam("drive", "Drive", 0.0f, 100.0f, 20.0f, "%", 0));

        addParam(floatParam("adsr_a", "Attack", 1.0f, 8000.0f, 5.0f, "", 0, skewFor(1.0f, 8000.0f, 400.0f)));
        addParam(floatParam("adsr_d", "Decay", 1.0f, 8000.0f, 400.0f, "", 0, skewFor(1.0f, 8000.0f, 500.0f)));
        addParam(floatParam("adsr_s", "Sustain", 0.0f, 100.0f, 60.0f, "%", 0));
        addParam(floatParam("adsr_r", "Release", 1.0f, 8000.0f, 300.0f, "", 0, skewFor(1.0f, 8000.0f, 500.0f)));
        addParam(floatParam("ar_a", "AR Attack", 1.0f, 8000.0f, 3.0f, "", 0, skewFor(1.0f, 8000.0f, 400.0f)));
        addParam(floatParam("ar_r", "AR Release", 1.0f, 8000.0f, 250.0f, "", 0, skewFor(1.0f, 8000.0f, 500.0f)));
        addParam(choiceParam("vca_env", "VCA Env", {"AR", "ADSR"}, 1));
        addParam(floatParam("vca_init", "VCA Init", 0.0f, 100.0f, 0.0f, "%", 0));

        addParam(floatParam("sh_rate", "S&H Rate", 0.1f, 50.0f, 6.0f, "Hz", 2, skewFor(0.1f, 50.0f, 5.0f)));
        addParam(choiceParam("sh_sync", "S&H Sync", shSync, 0));
        addParam(choiceParam("sh_src", "S&H Source", {"Noise", "VCO2 Saw", "VCO3 Saw", "VCO3 Sine"}, 0));
        addParam(floatParam("lag", "Lag", 0.0f, 2000.0f, 60.0f, "", 0, skewFor(0.0f, 2000.0f, 200.0f)));

        addParam(floatParam("reverb", "Reverb", 0.0f, 100.0f, 20.0f, "%", 0));
        addParam(floatParam("rev_decay", "Rev Decay", 0.0f, 100.0f, 50.0f, "%", 0));
        addParam(floatParam("rev_tone", "Rev Tone", 0.0f, 100.0f, 50.0f, "%", 0));
        addParam(floatParam("pan", "Pan", -100.0f, 100.0f, 0.0f, "", 0));

        addParam(choiceParam("seq_mode", "Seq Mode", {"Off", "Sync", "Free", "Key"}, 0));
        addParam(choiceParam("seq_div", "Seq Rate", seqDiv, 2));
        addParam(floatParam("seq_hz", "Seq Free Hz", 0.5f, 30.0f, 6.0f, "Hz", 1, skewFor(0.5f, 30.0f, 5.0f)));
        addParam(intParam("seq_len", "Seq Length", 1, kSteps, kSteps, ""));
        addParam(choiceParam("seq_dir", "Seq Dir", {"Forward", "Backward", "Pendulum", "Random"}, 0));
        addParam(choiceParam("seq_quant", "Quantize", {"Off", "Chromatic", "Minor", "Phrygian"}, 1));
        addParam(intParam("seq_range", "Seq Range", 1, 4, 2, "oct"));
        addParam(floatParam("seq_gate", "Seq Gate", 5.0f, 100.0f, 50.0f, "%", 0));
        addParam(boolParam("seq_trans", "Seq Transpose", true));
        char id[32], nm[32];
        for (int row = 0; row < 3; ++row)
            for (int s = 0; s < kSteps; ++s)
            {
                std::snprintf(id, sizeof id, "seq_%c%d", "abc"[row], s + 1);
                std::snprintf(nm, sizeof nm, "%c%d", "ABC"[row], s + 1);
                addParam(floatParam(id, nm, 0.0f, 100.0f, 0.0f, "", 0));
            }
        for (int s = 0; s < kSteps; ++s)
        {
            std::snprintf(id, sizeof id, "step_%d", s + 1);
            std::snprintf(nm, sizeof nm, "Step %d", s + 1);
            addParam(choiceParam(id, nm, {"Play", "Rest", "Skip", "Tie"}, 0));
        }

        addParam(choiceParam("pick_out", "Patch Out", srcN, 0));
        addParam(choiceParam("pick_in", "Patch In", dstN, 0));
        addParam(boolParam("unplug", "Unplug All", false));
        for (int c = 0; c < kNumCables; ++c)
        {
            std::snprintf(id, sizeof id, "c%d_from", c + 1); std::snprintf(nm, sizeof nm, "C%d From", c + 1);
            addParam(choiceParam(id, nm, srcN, 0));
            std::snprintf(id, sizeof id, "c%d_to", c + 1); std::snprintf(nm, sizeof nm, "C%d To", c + 1);
            addParam(choiceParam(id, nm, dstN, 0));
            std::snprintf(id, sizeof id, "c%d_amt", c + 1); std::snprintf(nm, sizeof nm, "C%d Amt", c + 1);
            addParam(floatParam(id, nm, -100.0f, 100.0f, 50.0f, "%", 0));
        }
        for (int s = 1; s < sNumSrc; ++s)
        {
            std::snprintf(id, sizeof id, "jack_out_%d", s); std::snprintf(nm, sizeof nm, "Jack Out %d", s);
            addParam(intParam(id, nm, 0, kMulti, 0, ""));
        }
        for (int d = 1; d < dNumDst; ++d)
        {
            std::snprintf(id, sizeof id, "jack_in_%d", d); std::snprintf(nm, sizeof nm, "Jack In %d", d);
            addParam(intParam(id, nm, 0, kMulti, 0, ""));
        }
        if (numParams() != kNumParams) mpc::log("Golem: parameter count mismatch (%d vs %d)", numParams(), (int)kNumParams);
        addPrograms();
        worker_ = std::thread([this] { workerLoop(); });
        wake();
    }

    ~Golem() override
    {
        { std::lock_guard<std::mutex> lk(mtx_); quit_ = true; }
        cv_.notify_all();
        if (worker_.joinable()) worker_.join();
    }

    void prepare(double sampleRate, int) override
    {
        sr_ = (float)sampleRate;
        vcf_.setSampleRate(sr_);
        adsr_.setSampleRate(sampleRate); ar_.setSampleRate(sampleRate);
        springL_.prepare(sr_, 37.0f); springR_.prepare(sr_, 43.0f);
        vol_.prepare(30.0f, sr_);
        reset();
    }

    void reset() override
    {
        vcf_.reset(); adsr_.reset(); ar_.reset(); springL_.reset(); springR_.reset();
        vco_[0].reset(0.0f); vco_[1].reset(0.33f); vco_[2].reset(0.66f);
        for (auto& v : src_) v = 0.0f;
        for (auto& v : prevGate_) v = 0.0f;
        nHeld_ = 0; lastNote_ = 48; glideN_[0] = glideN_[1] = glideN_[2] = 48.0f;
        shVal_ = 0.0f; shPh_ = 0.0f; lagVal_ = 0.0f; keyGate_ = false;
        seqPos_ = -1; seqDirUp_ = true; seqGateLeft_ = 0; seqStepLen_ = 1.0f; seqCount_ = 0.0f; seqWasPlaying_ = false; seqNote_ = 48.0f;
        seqLast_ = -1; seqStarted_ = false; seqTieHold_ = false;
        wheel_ = 0.0f; bend_ = 0.0f; at_ = 0.0f; velo_ = 1.0f;
        vol_.snap(dbToGain(get(kVolume)));
        dc_[0].reset(); dc_[1].reset();
    }

    void parameterChanged(int index, float) override
    {
        if (index >= kPickOut && index < kNumParams) wake();
    }

    bool formatValue(int index, float plain, std::string& out) override
    {
        char b[40];
        auto ms = [&](float v) { if (v >= 1000.0f) std::snprintf(b, sizeof b, "%.2f s", (double)(v * 0.001f)); else std::snprintf(b, sizeof b, "%.0f ms", (double)v); out = b; };
        if (index == kA || index == kD || index == kR || index == kArA || index == kArR || index == kLag) { ms(plain); return true; }
        if (index == kGlide) { if (plain < 0.5f) out = "Off"; else ms(plain); return true; }
        if (index == kV1Tune || index == kV2Tune || index == kV3Tune)
        {
            const int v = (int)std::lround(plain);
            std::snprintf(b, sizeof b, v == 0 ? "0 st" : "%+d st", v); out = b; return true;
        }
        if (index == kCutoff) { if (plain >= 1000.0f) std::snprintf(b, sizeof b, "%.1f kHz", (double)(plain * 0.001f)); else std::snprintf(b, sizeof b, "%.0f Hz", (double)plain); out = b; return true; }
        if (index == kNoiseColor) { out = plain < 33.0f ? "White" : (plain < 66.0f ? "Pink" : "Low"); std::snprintf(b, sizeof b, " %d", (int)std::lround(plain)); out += b; return true; }
        if (index == kFltEnv || index == kPan) { std::snprintf(b, sizeof b, "%+d", (int)std::lround(plain)); out = b; return true; }
        if (index >= kSeqA && index < kSeqB)       // pitch row: show the quantised note offset
        {
            const float st = seqSemis(plain);
            if (std::fabs(st) < 0.05f) out = "0 st"; else { std::snprintf(b, sizeof b, getInt(kSeqQuant) == 0 ? "%+.1f st" : "%+.0f st", (double)st); out = b; } return true;
        }
        if (index >= kCable && index < kJackOut && (index - kCable) % 3 == 2) { std::snprintf(b, sizeof b, "%+d%%", (int)std::lround(plain)); out = b; return true; }
        return false;
    }

    void process(mpc::ProcessContext& ctx) override
    {
        const int n = ctx.numFrames;
        float* L = ctx.outputs[0];
        float* R = ctx.outputs[1];

        // ---- cables (read once per block)
        int nc = 0;
        for (int c = 0; c < kNumCables; ++c)
        {
            const int s = getInt(kCable + 3 * c), d = getInt(kCable + 3 * c + 1);
            if (s <= 0 || s >= sNumSrc || d <= 0 || d >= dNumDst) continue;
            cab_[nc].src = s; cab_[nc].dst = d; cab_[nc].amt = get(kCable + 3 * c + 2) * 0.01f; ++nc;
        }
        bool breaks[dNumDst] = {};
        for (int c = 0; c < nc; ++c) breaks[cab_[c].dst] = true;

        // ---- block settings
        const bool duo = getInt(kVoice) == 1;
        const float glideMs = get(kGlide);
        const float glideC = glideMs < 0.5f ? 1.0f : 1.0f - std::exp(-1.0f / (glideMs * 0.001f * sr_));
        const float bendSt = bend_ * (float)getInt(kBend);
        const float t1 = get(kV1Tune) + get(kV1Fine) * 0.01f, t2 = get(kV2Tune) + get(kV2Fine) * 0.01f, t3 = get(kV3Tune) + get(kV3Fine) * 0.01f;
        const float fm1 = get(kV1FM) * 0.01f, fm2 = get(kV2FM) * 0.01f, fm3 = get(kV3FM) * 0.01f;
        const float pw2 = get(kV2PW) * 0.01f, pw3 = get(kV3PW) * 0.01f, pwm2 = get(kV2PWM) * 0.01f;
        const bool sync = getBool(kSync), lf3 = getBool(kV3LF), kbd3 = getBool(kV3Kbd);
        const float m1 = get(kM1Lvl) * 0.01f, m2 = get(kM2Lvl) * 0.01f, m3 = get(kM3Lvl) * 0.01f;
        const int w1 = getInt(kM1Wave), w2 = getInt(kM2Wave), w3 = getInt(kM3Wave);
        const float noiseL = get(kNoiseLvl) * 0.01f, ringL = get(kRingLvl) * 0.01f, nColor = get(kNoiseColor) * 0.01f;
        const int ftype = getInt(kFltType);
        const float cutoff = get(kCutoff), reso = get(kReso) * 0.01f, fKbd = get(kFltKbd) * 0.01f;
        const float fEnv = get(kFltEnv) * 0.01f, fFM = get(kFltFM) * 0.01f, drive = get(kDrive) * 0.01f;
        adsr_.set(get(kA), get(kD), get(kS) * 0.01f, get(kR));
        ar_.set(get(kArA), 1.0f, 1.0f, get(kArR));
        const bool vcaAdsr = getInt(kVcaEnv) == 1;
        const float vcaInit = get(kVcaInit) * 0.01f, veloAmt = get(kVelo) * 0.01f;
        const int shSync = getInt(kShSync), shSrc = getInt(kShSrc);
        const double bpm = (ctx.transport.bpm > 1.0 && ctx.transport.bpm < 999.0) ? ctx.transport.bpm : 120.0;
        const float shInc = shSync > 0 ? (float)(bpm / 60.0 / mpc::dsp::divisionBeats(kShDivs[std::min(shSync, 6)])) / sr_ : get(kShRate) / sr_;
        const float lagMs = get(kLag);
        const float lagC = lagMs < 0.5f ? 1.0f : 1.0f - std::exp(-1.0f / (lagMs * 0.001f * sr_));
        const float rev = get(kReverb) * 0.01f, revDecay = get(kRevDecay) * 0.01f, revTone = get(kRevTone) * 0.01f;
        const float pan = get(kPan) * 0.01f;
        vol_.target = dbToGain(get(kVolume));
        const bool legato = getInt(kRetrig) == 1;

        // ---- sequencer settings
        const int seqMode = getInt(kSeqMode);
        const bool playing = ctx.transport.playing;
        int sdiv = getInt(kSeqDiv); if (sdiv < 0) sdiv = 0; if (sdiv >= kNumSeqDivs) sdiv = kNumSeqDivs - 1;
        const float stepBeats = mpc::dsp::divisionBeats(kSeqDivs[sdiv]);
        bool seqLocked = false;
        if (seqMode == qSync || (seqMode == qKey && playing))
        {
            clock_.begin(ctx, stepBeats, sr_ / get(kSeqRate), true);
            seqLocked = clock_.locked;
            seqStepLen_ = (float)(stepBeats * 60.0 / bpm) * sr_;
        }
        else { seqStepLen_ = sr_ / get(kSeqRate); }
        if (seqMode == qSync && playing && !seqWasPlaying_) { seqPos_ = -1; seqDirUp_ = true; clock_.reset(); }
        seqWasPlaying_ = playing;
        if (seqMode != lastSeqMode_) { seqPos_ = -1; seqStarted_ = false; seqDirUp_ = true; lastSeqMode_ = seqMode; }
        // the sequencer owns pitch and gates while it runs; otherwise the keys play normally
        seqActive_ = (seqMode == qSync && playing) || seqMode == qFree || seqMode == qKey;
        if (!seqActive_) { seqGateLeft_ = 0; seqTieHold_ = false; }

        int ev = 0;
        for (int i = 0; i < n; ++i)
        {
            // ---- MIDI at this frame
            while (ev < ctx.numMidiEvents && ctx.midi[ev].sampleOffset <= i) { handleMidi(ctx.midi[ev], legato); ++ev; }

            // ---- sequencer clock
            bool seqTick = false;
            if (seqMode == qSync) seqTick = playing && seqLocked && clock_.tick();
            else if (seqMode == qFree || (seqMode == qKey && nHeld_ > 0))
            {
                if (seqMode == qKey && playing && seqLocked) seqTick = clock_.tick();
                else { seqCount_ += 1.0f; if (!seqStarted_ || seqCount_ >= seqStepLen_) { seqCount_ = 0.0f; seqTick = true; seqStarted_ = true; } }
            }
            if (seqMode == qKey && nHeld_ == 0) { seqStarted_ = false; seqPos_ = -1; }
            // patched seq clock / reset (rising edges)
            float mod[dNumDst] = {};
            for (int c = 0; c < nc; ++c) mod[cab_[c].dst] += src_[cab_[c].src] * cab_[c].amt;
            if (edge(dSeqReset, mod[dSeqReset])) { seqPos_ = -1; seqDirUp_ = true; }
            if (edge(dSeqClock, mod[dSeqClock])) seqTick = true;
            if (seqActive_ && seqTick) advanceSeq();
            bool seqGate = false;
            if (seqActive_ && seqPos_ >= 0)
            {
                if (seqGateLeft_ > 0) { --seqGateLeft_; seqGate = true; }
                seqGate = seqGate || seqTieHold_;
            }
            src_[sClock] = (seqActive_ && seqClockLeft_ > 0) ? 1.0f : 0.0f;
            if (seqClockLeft_ > 0) --seqClockLeft_;

            // ---- pitches
            float target[3];
            const bool seqPitch = seqActive_ && seqPos_ >= 0 && (seqMode != qKey || nHeld_ > 0);
            if (seqPitch) target[0] = target[1] = target[2] = seqNote_;
            else
            {
                const int lo = nHeld_ > 0 ? lowNote() : lastNote_, hi = nHeld_ > 0 ? highNote() : lastNote_;
                const int last = nHeld_ > 0 ? held_[nHeld_ - 1] : lastNote_;
                if (duo) { target[0] = (float)lo; target[1] = target[2] = (float)hi; }
                else target[0] = target[1] = target[2] = (float)last;
            }
            for (int v = 0; v < 3; ++v) glideN_[v] += glideC * (target[v] - glideN_[v]);
            const float kbdCv = (glideN_[0] - 60.0f) / 24.0f;
            const float allP = mod[dAllPitch] * 24.0f;
            const float p1 = glideN_[0] + t1 + bendSt + allP + mod[dV1Pitch] * 24.0f + fm1 * 12.0f * src_[sV2Sine];
            const float p2 = glideN_[1] + t2 + bendSt + allP + mod[dV2Pitch] * 24.0f + fm2 * 12.0f * src_[sV1Sq];
            float f3;
            {
                const float key3 = kbd3 ? glideN_[2] - 60.0f + bendSt : 0.0f;
                const float p3 = t3 + key3 + allP + mod[dV3Pitch] * 24.0f + fm3 * 12.0f * src_[sNoise];
                f3 = lf3 ? 2.0f * std::exp2(p3 * (1.0f / 12.0f)) : noteHz(60.0f + p3);
            }
            vco_[0].tick(noteHz(p1) / sr_, 0.5f);
            const float pwA = clampf(pw2 + pwm2 * 0.45f * src_[sV3Sine] + mod[dV2PW] * 0.45f, 0.05f, 0.95f);
            vco_[1].tick(noteHz(p2) / sr_, pwA, sync && vco_[0].wrapped, vco_[0].wrapFrac);
            vco_[2].tick(f3 / sr_, clampf(pw3 + mod[dV3PW] * 0.45f, 0.05f, 0.95f));

            // ---- noise, ring, S&H, lag
            const float nz = noise_.process(nColor);
            const float ringA = breaks[dRingIn] ? mod[dRingIn] : vco_[0].saw;
            const float ring = ringA * vco_[1].saw;
            shPh_ += shInc;
            bool shTrig = false;
            if (breaks[dSHClock]) shTrig = edge(dSHClock, mod[dSHClock]);
            else if (shPh_ >= 1.0f) shTrig = true;
            if (shPh_ >= 1.0f) shPh_ -= std::floor(shPh_);
            if (shTrig)
            {
                float in;
                if (breaks[dSHIn]) in = mod[dSHIn];
                else if (shSrc == 1) in = vco_[1].saw; else if (shSrc == 2) in = vco_[2].saw; else if (shSrc == 3) in = vco_[2].sine;
                else in = noise_.rng.bi();
                shVal_ = clampf(in, -1.0f, 1.0f);
            }
            const float lagIn = breaks[dLagIn] ? mod[dLagIn] : shVal_;
            lagVal_ += lagC * (lagIn - lagVal_);

            // ---- envelopes: keys, sequencer and patched gates
            const bool gADSR = keyGate_ || seqGate || level(dADSRGate, mod[dADSRGate]);
            const bool gAR = keyGate_ || seqGate || level(dARGate, mod[dARGate]);
            if (seqActive_ && seqRetrig_) { adsr_.noteOn(); ar_.noteOn(); seqRetrig_ = false; }
            if (keyRetrig_) { adsr_.noteOn(); ar_.noteOn(); keyRetrig_ = false; }
            if (gADSR && !adsrHeld_) { adsr_.noteOn(); } else if (!gADSR && adsrHeld_) adsr_.noteOff();
            if (gAR && !arHeld_) { ar_.noteOn(); } else if (!gAR && arHeld_) ar_.noteOff();
            adsrHeld_ = gADSR; arHeld_ = gAR;
            const float eA = adsr_.process(), eR = ar_.process();

            // ---- mixer -> VCF -> VCA
            float mix = 0.0f;
            if (!breaks[dVcfIn])
            {
                mix += m1 * (w1 == 0 ? vco_[0].saw : vco_[0].square);
                mix += m2 * pick(vco_[1], w2);
                mix += m3 * pick(vco_[2], w3);
                mix += noiseL * nz + ringL * ring;
                mix *= 0.6f;
            }
            else mix = mod[dVcfIn];
            mix += noise_.rng.bi() * 1e-4f;      // analog hiss: lets a screaming filter start on its own
            const float cutSt = fKbd * (glideN_[0] - 60.0f) + fEnv * 72.0f * eA + fFM * 36.0f * vco_[1].sine + mod[dCutoff] * 60.0f;
            const float fc = cutoff * std::exp2(clampf(cutSt, -96.0f, 96.0f) * (1.0f / 12.0f));
            const float y = vcf_.process(mix, fc, clampf(reso + mod[dReso], 0.0f, 1.05f), ftype, drive);
            const float env = vcaAdsr ? eA : eR;
            const float vg = clampf(vcaInit + env + mod[dVca], 0.0f, 1.5f) * (1.0f - veloAmt + veloAmt * velo_);
            const float out = y * vg;

            // ---- outputs for the patch bay (next sample sees these)
            src_[sV1Saw] = vco_[0].saw; src_[sV1Sq] = vco_[0].square;
            src_[sV2Saw] = vco_[1].saw; src_[sV2Pulse] = vco_[1].pulse; src_[sV2Tri] = vco_[1].tri; src_[sV2Sine] = vco_[1].sine;
            src_[sV3Saw] = vco_[2].saw; src_[sV3Pulse] = vco_[2].pulse; src_[sV3Tri] = vco_[2].tri; src_[sV3Sine] = vco_[2].sine;
            src_[sNoise] = nz; src_[sRing] = ring; src_[sSH] = shVal_; src_[sLag] = lagVal_;
            src_[sADSR] = eA; src_[sAR] = eR; src_[sVCF] = y;
            src_[sKbd] = kbdCv; src_[sVelo] = velo_; src_[sWheel] = wheel_; src_[sAT] = at_;

            // ---- pan, springs, volume
            const float pp = clampf(pan + mod[dPan], -1.0f, 1.0f);
            float oL = out * std::sqrt(0.5f * (1.0f - pp)) * 1.414f, oR = out * std::sqrt(0.5f * (1.0f + pp)) * 1.414f;
            const float rv = clampf(rev + mod[dReverb], 0.0f, 1.0f);
            if (rv > 0.001f || revActive_ > 0)
            {
                const float sL = springL_.process(out, revDecay, revTone), sR = springR_.process(out, revDecay, revTone);
                oL += sL * rv * 1.2f; oR += sR * rv * 1.2f;
                revActive_ = rv > 0.001f ? (int)(sr_ * 6.0f) : revActive_ - 1;
            }
            const float g = vol_.next();
            float yL = dc_[0].process(oL) * g, yR = dc_[1].process(oR) * g;
            if (!std::isfinite(yL) || !std::isfinite(yR)) { yL = yR = 0.0f; vcf_.reset(); }
            L[i] = clampf(yL, -4.0f, 4.0f); R[i] = clampf(yR, -4.0f, 4.0f);
        }
        while (ev < ctx.numMidiEvents) { handleMidi(ctx.midi[ev], legato); ++ev; }
    }

private:
    struct Cable { int src = 0, dst = 0; float amt = 0.0f; };

    static float pick(const Vco& v, int w) { return w == 0 ? v.saw : (w == 1 ? v.pulse : (w == 2 ? v.tri : v.sine)); }
    bool edge(int d, float v) { const bool hi = v > 0.5f, was = prevGate_[d] > 0.5f; prevGate_[d] = v; return hi && !was; }
    bool level(int d, float v) { prevGate_[d] = v; return v > 0.5f; }

    // ---- keys
    void handleMidi(const mpc::MidiEvent& m, bool legato)
    {
        if (m.isNoteOn())
        {
            const int note = m.data[1];
            removeHeld(note);
            if (nHeld_ < 16) held_[nHeld_++] = note;
            lastNote_ = note;
            velo_ = (float)m.data[2] / 127.0f;
            if (!seqActive_)
            {
                if (!keyGate_ || !legato) keyRetrig_ = true;
                keyGate_ = true;
            }
            else if (getInt(kSeqMode) == qKey && nHeld_ == 1) { seqPos_ = -1; seqStarted_ = false; }
        }
        else if (m.isNoteOff())
        {
            removeHeld(m.data[1]);
            if (nHeld_ == 0) keyGate_ = false;
            else if (!legato && !seqActive_) keyRetrig_ = true;
        }
        else if (m.isPitchBend()) bend_ = (float)m.pitchBend() / 8192.0f;
        else if (m.isAftertouch()) at_ = (float)m.data[1] / 127.0f;
        else if (m.isController())
        {
            if (m.controller() == 1) wheel_ = (float)m.data[2] / 127.0f;
            else if (m.controller() == 123 || m.controller() == 120) { nHeld_ = 0; keyGate_ = false; }
        }
    }
    void removeHeld(int note)
    {
        int k = 0;
        for (int i = 0; i < nHeld_; ++i) if (held_[i] != note) held_[k++] = held_[i];
        nHeld_ = k;
    }
    int lowNote() const { int v = 127; for (int i = 0; i < nHeld_; ++i) v = std::min(v, held_[i]); return v; }
    int highNote() const { int v = 0; for (int i = 0; i < nHeld_; ++i) v = std::max(v, held_[i]); return v; }

    // ---- sequencer
    float seqSemis(float slider) const
    {
        const float st = slider * 0.01f * 12.0f * (float)getInt(kSeqRange);
        const int q = getInt(kSeqQuant);
        if (q == 0) return st;
        const int r = (int)std::lround(st);
        if (q == 1) return (float)r;
        static const int minor[7] = {0, 2, 3, 5, 7, 8, 10}, phryg[7] = {0, 1, 3, 5, 7, 8, 10};
        const int* sc = q == 2 ? minor : phryg;
        const int oct = r / 12, deg = r % 12;
        int best = sc[0], bd = 99;
        for (int k = 0; k < 7; ++k) { const int dd = std::abs(sc[k] - deg); if (dd < bd) { bd = dd; best = sc[k]; } }
        if (std::abs(12 - deg) < bd) best = 12;
        return (float)(oct * 12 + best);
    }

    void advanceSeq()
    {
        const int len = std::max(1, std::min(kSteps, getInt(kSeqLen)));
        const int dir = getInt(kSeqDir);
        int pos = seqPos_;
        for (int tries = 0; tries < kSteps + 1; ++tries)
        {
            if (pos < 0) pos = dir == 1 ? len - 1 : 0;
            else if (dir == 0) pos = (pos + 1) % len;
            else if (dir == 1) pos = (pos - 1 + len) % len;
            else if (dir == 2)
            {
                if (len == 1) pos = 0;
                else { if (seqDirUp_ && pos >= len - 1) seqDirUp_ = false; else if (!seqDirUp_ && pos <= 0) seqDirUp_ = true; pos += seqDirUp_ ? 1 : -1; }
            }
            else pos = (int)(rng_.uni() * (float)len) % len;
            if (getInt(kStep + pos) != stSkip) break;
        }
        seqPos_ = pos;
#ifdef GOLEM_DEBUG
        std::fprintf(stderr, "advance -> %d\n", pos);
#endif
        const int mode = getInt(kStep + pos);
        if (mode == stSkip) { seqGateLeft_ = 0; seqTieHold_ = false; return; }    // every step skipped
        const int root = getBool(kSeqTrans) && nHeld_ > 0 ? held_[nHeld_ - 1] : 48;
        if (mode != stRest) seqNote_ = (float)root + seqSemis(get(kSeqA + pos));
        src_[sSeqA] = seqSemis(get(kSeqA + pos)) / 24.0f;
        src_[sSeqB] = get(kSeqB + pos) * 0.01f;
        src_[sSeqC] = get(kSeqC + pos) * 0.01f;
        seqClockLeft_ = (int)(seqStepLen_ * 0.5f);
        const float gf = get(kSeqGate) * 0.01f;
        if (mode == stPlay) { seqGateLeft_ = std::max(1, (int)(seqStepLen_ * gf)); seqRetrig_ = true; seqTieHold_ = false; }
        else if (mode == stTie) { seqGateLeft_ = (int)seqStepLen_ + 1; seqTieHold_ = false; }
        else { seqGateLeft_ = 0; seqTieHold_ = false; }
    }

    // ---- touch patching (worker thread) ----------------------------------------------------------
    void wake() { pending_.store(true); cv_.notify_all(); }

    void setNotify(int idx, float plain)
    {
        if (std::fabs(get(idx) - plain) < 1e-4f) return;
        set(idx, plain);
        notifyHostParameterChanged(idx);
    }

    void workerLoop()
    {
        std::unique_lock<std::mutex> lk(mtx_);
        while (true)
        {
            cv_.wait_for(lk, std::chrono::milliseconds(500), [this] { return quit_ || pending_.load(); });
            if (quit_) return;
            if (!pending_.exchange(false)) continue;
            lk.unlock();
            std::this_thread::sleep_for(std::chrono::milliseconds(25));     // let the tap settle
            patchStep();
            lk.lock();
        }
    }

    int cableFrom(int c) const { return getInt(kCable + 3 * c); }
    int cableTo(int c) const { return getInt(kCable + 3 * c + 1); }

    void patchStep()
    {
        if (getBool(kUnplug))
        {
            for (int c = 0; c < kNumCables; ++c) { setNotify(kCable + 3 * c, 0.0f); setNotify(kCable + 3 * c + 1, 0.0f); }
            setNotify(kUnplug, 0.0f);
            setNotify(kPickOut, 0.0f); setNotify(kPickIn, 0.0f);
        }
        const int po = getInt(kPickOut), pi = getInt(kPickIn);
        if (pi > 0)
        {
            int existing = -1;
            for (int c = 0; c < kNumCables; ++c) if (cableTo(c) == pi && cableFrom(c) > 0) { existing = c; break; }
            if (po > 0)
            {
                if (existing >= 0)
                {
                    if (cableFrom(existing) == po) { setNotify(kCable + 3 * existing, 0.0f); setNotify(kCable + 3 * existing + 1, 0.0f); }
                    else setNotify(kCable + 3 * existing, (float)po);
                }
                else
                {
                    for (int c = 0; c < kNumCables; ++c)
                        if (cableFrom(c) == 0 || cableTo(c) == 0)
                        {
                            setNotify(kCable + 3 * c, (float)po);
                            setNotify(kCable + 3 * c + 1, (float)pi);
                            setNotify(kCable + 3 * c + 2, kDefAmt[pi]);
                            break;
                        }
                }
                setNotify(kPickOut, 0.0f); setNotify(kPickIn, 0.0f);
            }
            else if (existing >= 0)       // tapped a lit input on its own: pull every cable in it
            {
                for (int c = 0; c < kNumCables; ++c)
                    if (cableTo(c) == pi) { setNotify(kCable + 3 * c, 0.0f); setNotify(kCable + 3 * c + 1, 0.0f); }
                setNotify(kPickIn, 0.0f);
            }
        }
        // jack lights: cable number (its colour), or the striped "split" frame for a shared output
        for (int s = 1; s < sNumSrc; ++s)
        {
            int first = 0, count = 0;
            for (int c = 0; c < kNumCables; ++c) if (cableFrom(c) == s && cableTo(c) > 0) { if (!count) first = c + 1; ++count; }
            setNotify(kJackOut + s - 1, (float)(count > 1 ? kMulti : first));
        }
        for (int d = 1; d < dNumDst; ++d)
        {
            int first = 0;
            for (int c = 0; c < kNumCables; ++c) if (cableTo(c) == d && cableFrom(c) > 0) { first = c + 1; break; }
            setNotify(kJackIn + d - 1, (float)first);
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
            // jack lights follow the cables in the preset
            for (int s = 1; s < sNumSrc; ++s)
            {
                int first = 0, count = 0;
                for (int c = 0; c < kNumCables; ++c)
                    if ((int)v[(size_t)(kCable + 3 * c)] == s && (int)v[(size_t)(kCable + 3 * c + 1)] > 0) { if (!count) first = c + 1; ++count; }
                v[(size_t)(kJackOut + s - 1)] = (float)(count > 1 ? kMulti : first);
            }
            for (int d = 1; d < dNumDst; ++d)
            {
                int first = 0;
                for (int c = 0; c < kNumCables; ++c)
                    if ((int)v[(size_t)(kCable + 3 * c + 1)] == d && (int)v[(size_t)(kCable + 3 * c)] > 0) { first = c + 1; break; }
                v[(size_t)(kJackIn + d - 1)] = (float)first;
            }
            addProgram(name, std::move(v));
        };
        #include "golem_presets.inc"
    }

    float sr_ = 44100.0f;
    Vco vco_[3];
    Ladder vcf_;
    Noise noise_;
    Spring springL_, springR_;
    mpc::dsp::ADSR adsr_, ar_;
    DcBlock dc_[2];
    Smooth vol_;
    Rng rng_;
    StepClock clock_;
    Cable cab_[kNumCables];
    float src_[sNumSrc] = {};
    float prevGate_[dNumDst] = {};
    int held_[16] = {}, nHeld_ = 0, lastNote_ = 48;
    float glideN_[3] = {48, 48, 48};
    float shVal_ = 0, shPh_ = 0, lagVal_ = 0;
    bool keyGate_ = false, keyRetrig_ = false, adsrHeld_ = false, arHeld_ = false;
    int lastSeqMode_ = 0, seqPos_ = -1, seqGateLeft_ = 0, seqClockLeft_ = 0, seqLast_ = -1, revActive_ = 0;
    bool seqActive_ = false, seqDirUp_ = true, seqWasPlaying_ = false, seqStarted_ = false, seqRetrig_ = false, seqTieHold_ = false;
    float seqStepLen_ = 1.0f, seqCount_ = 0.0f, seqNote_ = 48.0f;
    float wheel_ = 0.0f, bend_ = 0.0f, at_ = 0.0f, velo_ = 1.0f;

    std::thread worker_;
    std::mutex mtx_;
    std::condition_variable cv_;
    std::atomic<bool> pending_{false};
    bool quit_ = false;
};

MPC_PLUGIN_ENTRY(Golem)
