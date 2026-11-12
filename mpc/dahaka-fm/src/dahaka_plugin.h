// dahaka_plugin.h - the MPC plugin wrapper shared by Dahaka FM, Dahaka Wave and Dahaka VA.
//
// SPDX-License-Identifier: MIT
//
// Parameter IDs match the desktop Dahaka (master, voiceMode, op1Coarse, wt1Pos, va1Unison,
// fltCutoff, mod1Src ...) so the desktop presets carry over. Each plugin only gets the parameters
// for its own engine, plus the shared ones: noise, filter, envelopes, LFOs and the mod matrix.
//
// Before including: #define DAHAKA_ENGINE, DAHAKA_NAME ("Dahaka FM" ...) and DAHAKA_UID (4 chars).

#ifndef DAHAKA_PLUGIN_H
#define DAHAKA_PLUGIN_H

#include "dahaka_core.h"

#include <algorithm>
#include <cstdio>
#include <map>

namespace dk {

// How a parameter's value is shown on the MPC screen.
enum class Show { Raw, Pct, BiPct, Sec, Hz, Ratio };

struct PresetValue { const char* id; float v; };
struct Preset { const char* name; std::vector<PresetValue> values; };

class DahakaPlugin : public mpc::Plugin
{
public:
    static mpc::PluginInfo makeInfo()
    {
        mpc::PluginInfo i;
        i.name = DAHAKA_NAME;
        i.vendor = "binaryimage";
        i.uniqueId = MPC_FOURCC(DAHAKA_UID[0], DAHAKA_UID[1], DAHAKA_UID[2], DAHAKA_UID[3]);
        i.version = 1000;
        i.isInstrument = true;
        i.numInputs = 0;
        i.numOutputs = 2;
        return i;
    }

    DahakaPlugin() : mpc::Plugin(makeInfo())
    {
        buildParams();
#if DAHAKA_ENGINE == 1
        (void)WavetableBank::get();   // build the band-limited tables now, not on the first note
#endif
    }

    // Presets are added by the plugin's .cpp (desktop-style id/value lists).
    // levels: per-preset Master trims (dB) from the level-matching pass, by preset name.
    void addPresets(const std::vector<Preset>& presets, const std::vector<PresetValue>& levels = {})
    {
        for (const auto& p : presets)
        {
            std::vector<float> v((size_t)numParams());
            for (int i = 0; i < numParams(); ++i) v[(size_t)i] = param(i).def;
            for (const auto& pv : p.values)
            {
                auto it = index_.find(pv.id);
                if (it == index_.end()) continue;                 // a desktop parameter this engine doesn't have
                float value = pv.v;
                const std::string id = pv.id;
                if (id.size() > 3 && id.compare(id.size() - 3, 3, "Dst") == 0)
                {
                    // Desktop stores the destination enum; here it's the position in this engine's list.
                    int pos = 0;
                    for (size_t k = 0; k < dstList_.size(); ++k) if (dstList_[k] == (int)pv.v) pos = (int)k;
                    value = (float)pos;
                }
                v[(size_t)it->second] = value;
            }
            for (const auto& lv : levels)
                if (std::strcmp(lv.id, p.name) == 0) v[(size_t)pMaster_] = jlimit(-48.0f, 6.0f, lv.v);
            addProgram(p.name, std::move(v));
        }
    }

    void prepare(double sampleRate, int /*maxBlockSize*/) override
    {
        fs_ = sampleRate;
        for (int i = 0; i < maxVoices; ++i) voices_[i].prepare(sampleRate, 0x1234567u + (uint32_t)i * 7919u);
        reset();
    }

    void reset() override
    {
        for (auto& v : voices_) v.kill();
        numHeld_ = 0;
        std::fill(std::begin(keyDown_), std::end(keyDown_), false);
        std::fill(std::begin(sustained_), std::end(sustained_), false);
        sustainPedal_ = false;
        lastNote_ = -1.0f;
        gm_ = GlobalMod();
        master_ = targetMaster();
    }

    bool formatValue(int index, float plain, std::string& out) override
    {
        if (index < 0 || index >= (int)show_.size()) return false;
        char b[32];
        switch (show_[(size_t)index])
        {
            case Show::Pct: std::snprintf(b, sizeof b, "%d%%", (int)std::lround(plain * 100.0f)); break;
            case Show::BiPct: std::snprintf(b, sizeof b, "%+d%%", (int)std::lround(plain * 100.0f)); break;
            case Show::Sec:
                if (plain < 1.0f) std::snprintf(b, sizeof b, "%d ms", (int)std::lround(plain * 1000.0f));
                else std::snprintf(b, sizeof b, "%.2f s", (double)plain);
                break;
            case Show::Hz:
                if (plain >= 1000.0f) std::snprintf(b, sizeof b, "%.1fk Hz", (double)(plain * 0.001f));
                else std::snprintf(b, sizeof b, plain < 10.0f ? "%.2f Hz" : "%.0f Hz", (double)plain);
                break;
            case Show::Ratio: std::snprintf(b, sizeof b, "x%.2f", (double)plain); break;
            default: return false;
        }
        out = b;
        return true;
    }

    void process(mpc::ProcessContext& ctx) override
    {
        readParams();
        float* L = ctx.outputs[0];
        float* R = ctx.numOutputs > 1 ? ctx.outputs[1] : nullptr;
        int pos = 0;
        for (int e = 0; e < ctx.numMidiEvents; ++e)
        {
            const mpc::MidiEvent& ev = ctx.midi[e];
            if (ev.sampleOffset > pos) { renderRange(L, pos, ev.sampleOffset); pos = ev.sampleOffset; }
            handleMidi(ev);
        }
        renderRange(L, pos, ctx.numFrames);
        const float target = targetMaster();
        for (int i = 0; i < ctx.numFrames; ++i)
        {
            master_ += (target - master_) * 0.002f;
            float y = L[i] * master_;
            if (!std::isfinite(y)) y = 0.0f;
            L[i] = y;
            if (R) R[i] = y;   // Dahaka's voice is mono, like the desktop synth before its FX
        }
    }

private:
    // ----- parameters -----------------------------------------------------------
    std::map<std::string, int> index_;
    std::vector<Show> show_;
    std::vector<int> dstList_;         // this engine's mod destinations (desktop enum values)
    double fs_ = 44100.0;

    static float skewFor(float lo, float hi, float centre) { return std::log(0.5f) / std::log((centre - lo) / (hi - lo)); }

    int add(mpc::Param p, Show s)
    {
        index_[p.id] = numParams();
        show_.push_back(s);
        addParam(std::move(p));
        return numParams() - 1;
    }
    int addF(const char* id, const char* name, float lo, float hi, float def, Show s, float skew = 1.0f, const char* unit = "", int dec = 2)
    {
        return add(mpc::floatParam(id, name, lo, hi, def, unit, dec, skew), s);
    }
    int addI(const char* id, const char* name, int lo, int hi, int def, const char* unit = "") { return add(mpc::intParam(id, name, lo, hi, def, unit), Show::Raw); }
    int addC(const char* id, const char* name, const std::vector<std::string>& c, int def) { return add(mpc::choiceParam(id, name, c, def), Show::Raw); }
    int addB(const char* id, const char* name, bool def) { return add(mpc::boolParam(id, name, def), Show::Raw); }
    void addAdsr(const char* prefix, const char* label, float a, float d, float s, float r)
    {
        const float sk = skewFor(0.001f, 10.0f, 0.5f);
        char id[24], nm[24];
        std::snprintf(id, sizeof id, "%sA", prefix); std::snprintf(nm, sizeof nm, "%s Attack", label);  addF(id, nm, 0.001f, 10.0f, a, Show::Sec, sk);
        std::snprintf(id, sizeof id, "%sD", prefix); std::snprintf(nm, sizeof nm, "%s Decay", label);   addF(id, nm, 0.001f, 10.0f, d, Show::Sec, sk);
        std::snprintf(id, sizeof id, "%sS", prefix); std::snprintf(nm, sizeof nm, "%s Sustain", label); addF(id, nm, 0.0f, 1.0f, s, Show::Pct);
        std::snprintf(id, sizeof id, "%sR", prefix); std::snprintf(nm, sizeof nm, "%s Release", label); addF(id, nm, 0.001f, 10.0f, r, Show::Sec, sk);
    }

    int pMaster_, pVoiceMode_, pGlide_, pBend_, pNoise_;
    int pAlgo_ = -1, pFmDepth_ = -1, pFmFb_ = -1, pFmLevel_ = -1, pOp_[numOps] {};
    int pWt_[numWt] {};
    int pVa_[numVa] {}, pVaSync_ = -1, pVaSub_ = -1, pVaDrift_ = -1;
    int pFlt_, pFEnv_, pAEnv_, pAmpVel_, pMEnv_, pLfo_[numLfos], pMod_[numModSlots];

    void buildParams()
    {
        pMaster_ = addF("master", "Master", -48.0f, 6.0f, -6.0f, Show::Raw, 1.0f, "dB", 1);   // param 0: harmless
        pVoiceMode_ = addC("voiceMode", "Voice Mode", {"Poly", "Mono", "Legato"}, 0);
        pGlide_ = addF("glide", "Glide", 0.0f, 2.0f, 0.0f, Show::Sec, skewFor(0.0f, 2.0f, 0.25f));
        pBend_ = addI("bendRange", "Bend Range", 0, 24, 2, "st");

        if (kFM)
        {
            pAlgo_ = addC("fmAlgo", "Algorithm", {"1 Stack", "2 Branch", "3 Twin Mod", "4 Two Pairs", "5 Fan Out", "6 Stack+Sine", "7 Split", "8 Additive"}, 0);
            pFmDepth_ = addF("fmDepth", "FM Depth", 0.0f, 1.0f, 0.3f, Show::Pct);
            pFmFb_ = addF("fmFeedback", "Feedback", 0.0f, 1.0f, 0.0f, Show::Pct);
            pFmLevel_ = addF("fmLevel", "FM Level", 0.0f, 1.0f, 0.8f, Show::Pct);
            const int coarseDef[numOps] = {1, 1, 2, 1};
            const float levelDef[numOps] = {1.0f, 0.5f, 0.5f, 0.5f};
            const char* L[numOps] = {"A", "B", "C", "D"};
            for (int i = 0; i < numOps; ++i)
            {
                char id[24], nm[24];
                auto I = [&](const char* s) { std::snprintf(id, sizeof id, "op%d%s", i + 1, s); return id; };
                auto N = [&](const char* s) { std::snprintf(nm, sizeof nm, "%s %s", L[i], s); return nm; };
                pOp_[i] = addC(I("Wave"), N("Wave"), {"Sine", "Half Sine", "Abs Sine", "Soft Square", "Soft Saw", "Quarter Sine"}, 0);
                addI(I("Coarse"), N("Coarse"), 0, 16, coarseDef[i]);
                addF(I("Fine"), N("Fine"), 0.0f, 1.0f, 0.0f, Show::Pct);
                addF(I("Level"), N("Level"), 0.0f, 1.0f, levelDef[i], Show::Pct);
                addF(I("Vel"), N("Velocity"), 0.0f, 1.0f, 0.5f, Show::Pct);
                char pre[8]; std::snprintf(pre, sizeof pre, "op%d", i + 1);
                addAdsr(pre, L[i], 0.002f, 0.6f, i == 0 ? 1.0f : 0.6f, 0.3f);
            }
        }
        if (kWT)
        {
            for (int i = 0; i < numWt; ++i)
            {
                char id[24], nm[24];
                auto I = [&](const char* s) { std::snprintf(id, sizeof id, "wt%d%s", i + 1, s); return id; };
                auto N = [&](const char* s) { std::snprintf(nm, sizeof nm, "W%d %s", i + 1, s); return nm; };
                pWt_[i] = addC(I("Table"), N("Table"), {"Classic", "Harmonic Sweep", "Vox Formant", "PWM", "Sync Sweep", "Sine Fold", "Odd/Even", "Prime Bells"}, i == 0 ? 0 : 1);
                addF(I("Pos"), N("Position"), 0.0f, 1.0f, 0.5f, Show::Pct);
                addI(I("Semi"), N("Semi"), -24, 24, 0, "st");
                addF(I("Fine"), N("Fine"), -100.0f, 100.0f, i == 0 ? 0.0f : 7.0f, Show::Raw, 1.0f, "ct", 0);
                addF(I("Level"), N("Level"), 0.0f, 1.0f, i == 0 ? 0.8f : 0.0f, Show::Pct);
                addC(I("MutMode"), N("Mutator"), {"Off", "FM-Lin", "Sync", "Phase Warp", "Wavestack"}, 0);
                addF(I("MutAmt"), N("Mut Amt"), 0.0f, 1.0f, 0.0f, Show::Pct);
                addF(I("MutRatio"), N("Mut Ratio"), 0.25f, 8.0f, 1.0f, Show::Ratio, skewFor(0.25f, 8.0f, 2.0f));
            }
        }
        if (kVA)
        {
            for (int i = 0; i < numVa; ++i)
            {
                char id[24], nm[24];
                auto I = [&](const char* s) { std::snprintf(id, sizeof id, "va%d%s", i + 1, s); return id; };
                auto N = [&](const char* s) { std::snprintf(nm, sizeof nm, "V%d %s", i + 1, s); return nm; };
                pVa_[i] = addC(I("Wave"), N("Wave"), {"Saw", "Pulse", "Triangle", "Sine"}, i == 0 ? 0 : 1);
                addI(I("Semi"), N("Semi"), -24, 24, 0, "st");
                addF(I("Fine"), N("Fine"), -100.0f, 100.0f, i == 0 ? 0.0f : -6.0f, Show::Raw, 1.0f, "ct", 0);
                addF(I("Pw"), N("PW"), 0.05f, 0.95f, 0.5f, Show::Pct);
                addI(I("Unison"), N("Unison"), 1, maxUnison, 1);
                addF(I("Detune"), N("Detune"), 0.0f, 1.0f, 0.25f, Show::Pct);
                addF(I("Level"), N("Level"), 0.0f, 1.0f, i == 0 ? 0.8f : 0.0f, Show::Pct);
            }
            pVaSync_ = addB("vaSync", "V2 Sync", false);
            pVaSub_ = addF("vaSub", "Sub Osc", 0.0f, 1.0f, 0.0f, Show::Pct);
            pVaDrift_ = addF("vaDrift", "Drift", 0.0f, 1.0f, 0.15f, Show::Pct);
        }
        pNoise_ = addF("noise", "Noise", 0.0f, 1.0f, 0.0f, Show::Pct);

        pFlt_ = addC("fltMode", "Flt Mode", {"LP 12", "LP 24", "Band", "HP 12", "Notch"}, 1);
        addF("fltCutoff", "Cutoff", 20.0f, 20000.0f, 8000.0f, Show::Hz, skewFor(20.0f, 20000.0f, 1000.0f));
        addF("fltReso", "Resonance", 0.0f, 1.0f, 0.15f, Show::Pct);
        addF("fltDrive", "Drive", 0.0f, 1.0f, 0.0f, Show::Pct);
        addF("fltEnv", "Flt Env", -1.0f, 1.0f, 0.0f, Show::BiPct);
        addF("fltKey", "Key Track", 0.0f, 1.0f, 0.5f, Show::Pct);
        addF("fltVel", "Flt Vel", 0.0f, 1.0f, 0.0f, Show::Pct);
        pFEnv_ = numParams(); addAdsr("fEnv", "Flt", 0.002f, 0.4f, 0.3f, 0.3f);
        pAEnv_ = numParams(); addAdsr("aEnv", "Amp", 0.002f, 0.3f, 0.8f, 0.25f);
        pAmpVel_ = addF("ampVel", "Amp Vel", 0.0f, 1.0f, 0.5f, Show::Pct);
        pMEnv_ = numParams(); addAdsr("mEnv", "Mod", 0.01f, 0.5f, 0.0f, 0.3f);

        for (int i = 0; i < numLfos; ++i)
        {
            char id[24], nm[24];
            std::snprintf(id, sizeof id, "lfo%dShape", i + 1); std::snprintf(nm, sizeof nm, "LFO%d Shape", i + 1);
            pLfo_[i] = addC(id, nm, {"Sine", "Triangle", "Saw", "Square", "S&H", "Smooth Rnd"}, 0);
            std::snprintf(id, sizeof id, "lfo%dRate", i + 1); std::snprintf(nm, sizeof nm, "LFO%d Rate", i + 1);
            addF(id, nm, 0.02f, 40.0f, i == 0 ? 5.0f : 0.3f, Show::Hz, skewFor(0.02f, 40.0f, 2.0f));
            std::snprintf(id, sizeof id, "lfo%dRetrig", i + 1); std::snprintf(nm, sizeof nm, "LFO%d Retrig", i + 1);
            addB(id, nm, i == 0);
        }

        // Mod destinations this engine has (desktop enum values, in menu order)
        std::vector<std::string> dstNames;
        auto D = [&](int e, const char* n) { dstList_.push_back(e); dstNames.push_back(n); };
        D(dstOff, "Off"); D(dstPitch, "Pitch");
        if (kFM) { D(dstFmDepth, "FM Depth"); D(dstFmFeedback, "FM Feedback"); D(dstFmLevel, "FM Level"); }
        if (kWT) { D(dstWt1Pos, "WT1 Pos"); D(dstWt2Pos, "WT2 Pos"); D(dstWt1Mut, "WT1 Mutator"); D(dstWt2Mut, "WT2 Mutator"); D(dstWt1Level, "WT1 Level"); D(dstWt2Level, "WT2 Level"); }
        if (kVA) { D(dstVaPw, "Pulse Width"); D(dstVaDetune, "Detune"); D(dstVa1Level, "VA1 Level"); D(dstVa2Level, "VA2 Level"); D(dstVa2Pitch, "VA2 Pitch"); }
        D(dstCutoff, "Cutoff"); D(dstResonance, "Resonance"); D(dstAmp, "Amp"); D(dstNoise, "Noise");
        const std::vector<std::string> srcNames = {"Off", "LFO 1", "LFO 2", "Mod Env", "Filter Env", "Velocity", "Mod Wheel", "Aftertouch", "Key Track", "Random"};
        auto dstPos = [&](int e) { for (size_t k = 0; k < dstList_.size(); ++k) if (dstList_[k] == e) return (int)k; return 0; };
        const int firstDst = kFM ? dstFmDepth : (kWT ? dstWt1Pos : dstCutoff);
        const float firstAmt = kWT ? 0.5f : 0.3f;
        for (int i = 0; i < numModSlots; ++i)
        {
            char id[24], nm[24];
            std::snprintf(id, sizeof id, "mod%dSrc", i + 1); std::snprintf(nm, sizeof nm, "M%d Source", i + 1);
            pMod_[i] = addC(id, nm, srcNames, i == 0 ? srcModWheel : srcOff);
            std::snprintf(id, sizeof id, "mod%dDst", i + 1); std::snprintf(nm, sizeof nm, "M%d Dest", i + 1);
            addC(id, nm, dstNames, i == 0 ? dstPos(firstDst) : 0);
            std::snprintf(id, sizeof id, "mod%dAmt", i + 1); std::snprintf(nm, sizeof nm, "M%d Amount", i + 1);
            addF(id, nm, -1.0f, 1.0f, i == 0 ? firstAmt : 0.0f, Show::BiPct);
        }
    }

    float targetMaster() const { const float db = get(pMaster_); return std::pow(10.0f, db * 0.05f); }

    void readParams()
    {
        SynthParams& p = params_;
        p.voiceMode = getInt(pVoiceMode_);
        p.glide = get(pGlide_);
        p.bendRange = (float)getInt(pBend_);
        if (kFM)
        {
            p.algo = getInt(pAlgo_);
            p.fmDepth = get(pFmDepth_); p.fmFeedback = get(pFmFb_); p.fmLevel = get(pFmLevel_);
            for (int i = 0; i < numOps; ++i)
            {
                const int b = pOp_[i];
                auto& o = p.op[i];
                o.wave = getInt(b);
                const int coarse = getInt(b + 1);
                o.ratio = (coarse == 0 ? 0.5f : (float)coarse) * (1.0f + get(b + 2));
                o.level = get(b + 3); o.vel = get(b + 4);
                o.env = {get(b + 5), get(b + 6), get(b + 7), get(b + 8)};
            }
        }
        if (kWT)
            for (int i = 0; i < numWt; ++i)
            {
                const int b = pWt_[i];
                auto& w = p.wt[i];
                w.table = getInt(b); w.pos = get(b + 1);
                w.pitchRatio = std::exp2(((float)getInt(b + 2) + get(b + 3) / 100.0f) / 12.0f);
                w.level = get(b + 4); w.mutMode = getInt(b + 5); w.mutAmt = get(b + 6); w.mutRatio = get(b + 7);
            }
        if (kVA)
        {
            for (int i = 0; i < numVa; ++i)
            {
                const int b = pVa_[i];
                auto& v = p.va[i];
                v.wave = getInt(b);
                v.pitchRatio = std::exp2(((float)getInt(b + 1) + get(b + 2) / 100.0f) / 12.0f);
                v.pw = get(b + 3); v.unison = getInt(b + 4); v.detune = get(b + 5); v.level = get(b + 6);
            }
            p.vaSync = getBool(pVaSync_); p.vaSub = get(pVaSub_); p.vaDrift = get(pVaDrift_);
        }
        p.noise = get(pNoise_);
        p.fltMode = getInt(pFlt_);
        p.cutoff = get(pFlt_ + 1); p.reso = get(pFlt_ + 2); p.drive = get(pFlt_ + 3);
        p.fltEnv = get(pFlt_ + 4); p.fltKey = get(pFlt_ + 5); p.fltVel = get(pFlt_ + 6);
        p.fEnv = {get(pFEnv_), get(pFEnv_ + 1), get(pFEnv_ + 2), get(pFEnv_ + 3)};
        p.aEnv = {get(pAEnv_), get(pAEnv_ + 1), get(pAEnv_ + 2), get(pAEnv_ + 3)};
        p.ampVel = get(pAmpVel_);
        p.mEnv = {get(pMEnv_), get(pMEnv_ + 1), get(pMEnv_ + 2), get(pMEnv_ + 3)};
        for (int i = 0; i < numLfos; ++i) { p.lfoShape[i] = getInt(pLfo_[i]); p.lfoRate[i] = get(pLfo_[i] + 1); p.lfoRetrig[i] = getBool(pLfo_[i] + 2); }
        for (int i = 0; i < numModSlots; ++i)
        {
            p.mod[i].src = getInt(pMod_[i]);
            const int d = getInt(pMod_[i] + 1);
            p.mod[i].dst = (d >= 0 && d < (int)dstList_.size()) ? dstList_[(size_t)d] : 0;
            p.mod[i].amt = get(pMod_[i] + 2);
        }
    }

    // ----- voices / MIDI (desktop Engine.cpp) ------------------------------------
    Voice voices_[maxVoices];
    SynthParams params_;
    GlobalMod gm_;
    int held_[128] {};
    int numHeld_ = 0;
    bool keyDown_[128] {}, sustained_[128] {}, sustainPedal_ = false;
    float lastNote_ = -1.0f, master_ = 0.5f;
    uint64_t ageCounter_ = 0;

    void renderRange(float* L, int from, int to)
    {
        if (to <= from) return;
        for (auto& v : voices_) v.render(L + from, to - from, params_, gm_);
    }

    Voice& findVoiceForNote()
    {
        Voice* best = nullptr;
        for (auto& v : voices_) if (!v.isActive()) return v;
        for (auto& v : voices_) if (v.isReleased() && (best == nullptr || v.getLevel() < best->getLevel())) best = &v;
        if (best) return *best;
        for (auto& v : voices_) if (best == nullptr || v.age < best->age) best = &v;
        return *best;
    }

    void removeHeld(int note)
    {
        for (int i = 0; i < numHeld_; ++i)
            if (held_[i] == note) { for (int j = i + 1; j < numHeld_; ++j) held_[j - 1] = held_[j]; --numHeld_; return; }
    }

    void noteOn(int note, float velocity)
    {
        keyDown_[note] = true;
        sustained_[note] = false;
        const bool glideOn = params_.glide > 0.0005f;
        const float from = (glideOn && lastNote_ >= 0.0f) ? lastNote_ : -1.0f;
        if (params_.voiceMode == Poly)
        {
            auto& v = findVoiceForNote();
            v.age = ++ageCounter_;
            v.start(note, velocity, true, from);
            lastNote_ = (float)note;
            return;
        }
        auto& v = voices_[0];
        const bool heldBefore = numHeld_ > 0;
        removeHeld(note);
        if (numHeld_ < 128) held_[numHeld_++] = note;
        const bool legato = params_.voiceMode == Legato && heldBefore && v.isActive();
        float glideFrom = -1.0f;
        if (glideOn && v.isActive()) glideFrom = (params_.voiceMode == Mono || legato) ? v.getCurrentPitch() : -1.0f;
        v.start(note, velocity, !legato, glideFrom);
        lastNote_ = (float)note;
    }

    void noteOff(int note)
    {
        keyDown_[note] = false;
        if (sustainPedal_) { sustained_[note] = true; return; }
        if (params_.voiceMode == Poly)
        {
            for (auto& v : voices_) if (v.isActive() && !v.isReleased() && v.getNote() == note) v.release();
            return;
        }
        removeHeld(note);
        auto& v = voices_[0];
        if (v.getNote() != note) return;
        if (numHeld_ > 0)
        {
            const int back = held_[numHeld_ - 1];
            v.start(back, 1.0f, false, params_.glide > 0.0005f ? v.getCurrentPitch() : -1.0f);
            lastNote_ = (float)back;
        }
        else v.release();
    }

    void allNotesOff()
    {
        for (auto& v : voices_) v.release();
        numHeld_ = 0;
        std::fill(std::begin(keyDown_), std::end(keyDown_), false);
        std::fill(std::begin(sustained_), std::end(sustained_), false);
    }

    void handleMidi(const mpc::MidiEvent& ev)
    {
        if (ev.isNoteOn()) noteOn(ev.note(), ev.velocity() / 127.0f);
        else if (ev.isNoteOff()) noteOff(ev.note());
        else if (ev.isPitchBend()) gm_.bend = (float)ev.pitchBend() / 8192.0f;
        else if (ev.isAftertouch()) gm_.aftertouch = ev.data[1] / 127.0f;
        else if (ev.isController())
        {
            const int cc = ev.controller(), val = ev.controllerValue();
            if (cc == 1) gm_.wheel = val / 127.0f;
            else if (cc == 64)
            {
                sustainPedal_ = val >= 64;
                if (!sustainPedal_)
                    for (int n = 0; n < 128; ++n)
                        if (sustained_[n]) { sustained_[n] = false; if (!keyDown_[n]) noteOff(n); }
            }
            else if (cc == 120) for (auto& v : voices_) v.kill();
            else if (cc == 123) allNotesOff();
        }
    }
};

} // namespace dk

#endif // DAHAKA_PLUGIN_H
