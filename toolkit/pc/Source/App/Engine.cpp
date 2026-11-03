#include "Engine.h"
#include "Settings.h"
#include "WinLoopback.h"
#include "Common/Files.h"
#include <cmath>

namespace tk
{
// ------------------------------------------------------------ Source

Source::Source (Kind k, juce::String n, int ch, double s) : kind (k), name (std::move (n)), channels (ch), sr (s)
{
    ringL.assign ((size_t) R, 0.0f); ringR.assign ((size_t) R, 0.0f);
    loud.prepare (s);
}

void Source::setSampleRate (double s)
{
    if (std::abs (s - sr.load()) < 1) return;
    sr = s;
    loud.prepare (s);
    const juce::SpinLock::ScopedLockType l (jamLock);
    jamPos = jamCount = 0;
}

void Source::feed (const float* l, const float* r, int n)
{
    if (r == nullptr) r = l;
    int pos = w.load();
    float pk = 0; double sq = 0;
    for (int i = 0; i < n; ++i)
    {
        ringL[(size_t) pos] = l[i]; ringR[(size_t) pos] = r[i];
        pos = (pos + 1) & (R - 1);
        float a = juce::jmax (std::abs (l[i]), std::abs (r[i]));
        pk = juce::jmax (pk, a);
        sq += 0.5 * ((double) l[i] * l[i] + (double) r[i] * r[i]);
    }
    w = pos;
    // meter ballistics: instant attack, ~300 ms release
    float dt = (float) n / (float) sr.load();
    pkHold = juce::jmax (pk, pkHold * std::exp (-dt / 0.3f));
    rmsAcc = rmsAcc + ((float) (sq / juce::jmax (1, n)) - rmsAcc) * (1 - std::exp (-dt / 0.3f));
    peakDb = 20 * std::log10 (pkHold + 1e-9f);
    rmsDb = 10 * std::log10 (rmsAcc + 1e-12f);
    if (resetLoudness.exchange (false)) loud.reset();
    loud.process (l, channels > 1 ? r : nullptr, n);
    lufsM = loud.momentary(); lufsS = loud.shortTerm(); lufsI = loud.integrated(); truePeak = loud.truePeakDb();
    lastFeed = juce::Time::getMillisecondCounter();
    frames += n;

    {   // jam ring
        const juce::SpinLock::ScopedLockType jl (jamLock);
        const auto len = (juce::int64) jamL.size();
        if (len > 0)
            for (int i = 0; i < n; ++i)
            {
                jamL[(size_t) jamPos] = (juce::int16) juce::jlimit (-32767, 32767, (int) std::lround (l[i] * 32767.0f));
                if (channels > 1) jamR[(size_t) jamPos] = (juce::int16) juce::jlimit (-32767, 32767, (int) std::lround (r[i] * 32767.0f));
                jamPos = (jamPos + 1) % len;
                if (jamCount < len) ++jamCount;
            }
    }

    int st = capState.load();
    if (st == 1)
    {
        // armed: start (with a little pre-roll from the ring) when the level crosses the threshold
        if (pk > armTh)
        {
            const juce::SpinLock::ScopedLockType cl (capLock);
            int pre = juce::jmin ((int) (0.15 * sr.load()), R - n - 1, cap.getNumSamples());
            int start = (w.load() - n - pre + R * 4) & (R - 1);
            for (int i = 0; i < pre; ++i)
            {
                cap.setSample (0, i, ringL[(size_t) ((start + i) & (R - 1))]);
                if (cap.getNumChannels() > 1) cap.setSample (1, i, ringR[(size_t) ((start + i) & (R - 1))]);
            }
            capLen = pre;
            capState = 2; st = 2;
        }
        else return;
    }
    if (st == 2)
    {
        const juce::SpinLock::ScopedLockType cl (capLock);
        if (capStartFrame.load() < 0) capStartFrame = frames.load() - n;
        int at = capLen.load(), room = cap.getNumSamples() - at, m = juce::jmin (room, n);
        if (m > 0)
        {
            cap.copyFrom (0, at, l, m);
            if (cap.getNumChannels() > 1) cap.copyFrom (1, at, r, m);
            capLen = at + m;
        }
        if (m < n) capState = 0;   // full: stop (UI picks it up)
    }
}

void Source::copyLatest (float* l, float* r, int n) const
{
    int end = w.load();
    for (int i = 0; i < n; ++i)
    {
        int k = (end - n + i + R * 4) & (R - 1);
        l[i] = ringL[(size_t) k];
        if (r != nullptr) r[i] = ringR[(size_t) k];
    }
}

void Source::setJamSeconds (int s)
{
    const juce::SpinLock::ScopedLockType l (jamLock);
    size_t len = (size_t) juce::jmax (1, s) * (size_t) std::lround (sr.load());
    jamL.assign (len, 0);
    jamR.assign (channels > 1 ? len : 0, 0);
    jamPos = jamCount = 0;
}

double Source::jamFilledSeconds() const
{
    const juce::SpinLock::ScopedLockType l (jamLock);
    return (double) jamCount / sr.load();
}

juce::AudioBuffer<float> Source::jamSnapshot (double seconds) const
{
    const juce::SpinLock::ScopedLockType l (jamLock);
    const auto len = (juce::int64) jamL.size();
    auto n = (int) juce::jmin (jamCount, (juce::int64) (seconds * sr.load()));
    juce::AudioBuffer<float> b (channels > 1 ? 2 : 1, juce::jmax (0, n));
    for (int i = 0; i < n; ++i)
    {
        auto k = ((jamPos - n + i) % len + len) % len;
        b.setSample (0, i, jamL[(size_t) k] / 32767.0f);
        if (channels > 1) b.setSample (1, i, jamR[(size_t) k] / 32767.0f);
    }
    return b;
}

void Source::startCapture (double maxSeconds)
{
    const juce::SpinLock::ScopedLockType cl (capLock);
    cap.setSize (channels > 1 ? 2 : 1, (int) (maxSeconds * sr.load()));
    cap.clear();
    capLen = 0;
    capStartFrame = -1;
    capState = 2;
}

void Source::armCapture (double maxSeconds, float thresholdDb)
{
    const juce::SpinLock::ScopedLockType cl (capLock);
    cap.setSize (channels > 1 ? 2 : 1, (int) (maxSeconds * sr.load()));
    cap.clear();
    capLen = 0;
    armTh = juce::Decibels::decibelsToGain (thresholdDb);
    capState = 1;
}

juce::AudioBuffer<float> Source::stopCapture()
{
    const juce::SpinLock::ScopedLockType cl (capLock);
    capState = 0;
    juce::AudioBuffer<float> out (cap.getNumChannels(), capLen.load());
    for (int c = 0; c < out.getNumChannels(); ++c) out.copyFrom (c, 0, cap, c, 0, out.getNumSamples());
    cap.setSize (1, 1);
    return out;
}

// ------------------------------------------------------------ Engine

struct Engine::Loopback
{
    void* handle = nullptr;
};

static void loopbackCallback (void* ctx, const float* l, const float* r, int n, int sr)
{
    auto* s = static_cast<Source*> (ctx);
    s->setSampleRate (sr);
    s->feed (l, r, n);
}

Engine::Engine (bool openDevices)
{
    jamMins = Settings::get().num ("jamMinutes", 5);
    server = std::make_unique<link::Server> ([this] (int id, const link::Packet& p) { onLinkAudio (id, p); },
                                             [this] (int id) { onLinkLost (id); });
    if (! openDevices) return;
    std::unique_ptr<juce::XmlElement> saved (juce::parseXML (Settings::get().str ("audioDevice")));
    devices.initialise (16, 2, saved.get(), true);
    devices.addChangeListener (this);
    devices.addAudioCallback (this);
    server->start();
}

Engine::~Engine()
{
    stopLoopback();
    devices.removeAudioCallback (this);
    devices.closeAudioDevice();
    server->stop();
    stopListening();
}

void Engine::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (auto xml = devices.createStateXml()) Settings::get().set ("audioDevice", xml->toString());
    notify();
}

void Engine::notify()
{
    juce::MessageManager::callAsync ([this] { if (onSourcesChanged) onSourcesChanged(); });
}

void Engine::audioDeviceAboutToStart (juce::AudioIODevice* d)
{
    sr = d->getCurrentSampleRate();
    auto active = d->getActiveInputChannels();
    auto names = d->getInputChannelNames();
    {
        const juce::ScopedLock sl (lock);
        int count = 0;
        for (int ch = 0; ch < names.size(); ++ch)
        {
            if (! active[ch]) continue;
            juce::String label = Settings::get().str ("inputName" + juce::String (ch), names[ch]);
            if (count < inputs.size())
            {
                inputs[count]->name = label;
                inputs[count]->active = true;
                inputs[count]->setSampleRate (sr);
            }
            else inputs.add (new Source (Source::input, label, 1, sr));
            ++count;
        }
        for (int i = count; i < inputs.size(); ++i) inputs[i]->active = false;
        for (auto* s : inputs) s->setJamSeconds (jamMins * 60);
    }
    tmpL.assign (8192, 0.0f);
    notify();
}

void Engine::audioDeviceIOCallbackWithContext (const float* const* in, int nin, float* const* out, int nout, int n,
                                               const juce::AudioIODeviceCallbackContext&)
{
    for (int c = 0; c < nin && c < inputs.size(); ++c)
        if (in[c] != nullptr) inputs.getUnchecked (c)->feed (in[c], nullptr, n);

    for (int c = 0; c < nout; ++c) if (out[c] != nullptr) juce::FloatVectorOperations::clear (out[c], n);
    if (nout == 0) return;
    float* L = out[0]; float* R = nout > 1 ? out[1] : nullptr;
    if (playing.load())
    {
        const juce::SpinLock::ScopedTryLockType tl (playLock);
        if (tl.isLocked() && playBuf.getNumSamples() > 0)
        {
            double step = playSr / sr.load();
            int end = playEnd.load();
            for (int i = 0; i < n; ++i)
            {
                int p = playPos.load();
                if (p >= end - 1) { playing = false; break; }
                float a = playBuf.getSample (0, p), b = playBuf.getSample (0, p + 1);
                float t = (float) playFrac, v = a + (b - a) * t;
                float v2 = v;
                if (playBuf.getNumChannels() > 1) { float c2 = playBuf.getSample (1, p), d2 = playBuf.getSample (1, p + 1); v2 = c2 + (d2 - c2) * t; }
                L[i] += v; if (R) R[i] += v2;
                playFrac += step;
                int adv = (int) playFrac; playFrac -= adv;
                playPos = p + adv;
            }
        }
    }
    {
        const juce::SpinLock::ScopedTryLockType tl (playLock);
        if (tl.isLocked() && generator) generator (L, R, n, sr.load());
    }
}

juce::Array<Source*> Engine::sources (bool activeOnly) const
{
    const juce::ScopedLock sl (lock);
    juce::Array<Source*> a;
    for (auto* s : inputs) if (! activeOnly || s->active) a.add (s);
    for (auto* s : extras) if (! activeOnly || s->active) a.add (s);
    return a;
}

Source* Engine::source (int index) const
{
    auto a = sources();
    return juce::isPositiveAndBelow (index, a.size()) ? a[index] : nullptr;
}

juce::String Engine::sourceLabel (const Source* s) const
{
    if (s == nullptr) return {};
    switch (s->kind)
    {
        case Source::linked: return "Ableton: " + s->name;
        case Source::loopback: return "PC audio";
        default: return s->name;
    }
}

void Engine::renameInput (int index, const juce::String& n)
{
    const juce::ScopedLock sl (lock);
    if (! juce::isPositiveAndBelow (index, inputs.size())) return;
    inputs[index]->name = n;
    // map back to the hardware channel number
    if (auto* d = devices.getCurrentAudioDevice())
    {
        auto active = d->getActiveInputChannels();
        int count = 0;
        for (int ch = 0; ch < d->getInputChannelNames().size(); ++ch)
            if (active[ch]) { if (count == index) { Settings::get().set ("inputName" + juce::String (ch), n); break; } ++count; }
    }
}

void Engine::setJamMinutes (int m)
{
    jamMins = juce::jlimit (1, 30, m);
    Settings::get().set ("jamMinutes", jamMins);
    for (auto* s : sources (false)) s->setJamSeconds (jamMins * 60);
}

juce::File Engine::saveJam (double seconds, const juce::String& label)
{
    auto dir = Settings::get().folder ("jamFolder", musicFolder().getChildFile ("Toolkit Jams")).getChildFile (stamp() + (label.isNotEmpty() ? " " + safeName (label) : juce::String()));
    dir.createDirectory();
    int idx = 1;
    juce::AudioBuffer<float> mix;
    double mixSr = 0;
    for (auto* s : sources())
    {
        auto b = s->jamSnapshot (seconds);
        if (b.getNumSamples() == 0 || peakOf (b) < 1e-4f) { ++idx; continue; }   // skip silent channels
        writeWav (dir.getChildFile (juce::String (idx).paddedLeft ('0', 2) + " " + safeName (sourceLabel (s)) + ".wav"), b, s->sr, 24);
        if (mixSr == 0 || std::abs (mixSr - s->sr) < 1)
        {
            mixSr = s->sr;
            if (mix.getNumSamples() < b.getNumSamples()) mix.setSize (2, b.getNumSamples(), true, true);
            for (int c = 0; c < 2; ++c) mix.addFrom (c, mix.getNumSamples() - b.getNumSamples(), b, juce::jmin (c, b.getNumChannels() - 1), 0, b.getNumSamples(), 0.5f);
        }
        ++idx;
    }
    if (mix.getNumSamples() > 0)
    {
        float pk = peakOf (mix);
        if (pk > 0.98f) mix.applyGain (0.98f / pk);
        writeWav (dir.getChildFile ("00 Mix.wav"), mix, mixSr, 24);
    }
    return dir;
}

// ------------------------------------------------------------ loopback

bool Engine::loopbackAvailable() const { return tkwin::loopbackSupported(); }
bool Engine::loopbackRunning() const { return loop != nullptr && loop->handle != nullptr; }

bool Engine::startLoopback()
{
    if (loopbackRunning()) return true;
    {
        const juce::ScopedLock sl (lock);
        if (loopSource == nullptr)
        {
            loopSource = extras.add (new Source (Source::loopback, "PC audio", 2, 48000));
            loopSource->setJamSeconds (jamMins * 60);
        }
        loopSource->active = true;
    }
    loop = std::make_unique<Loopback>();
    loop->handle = tkwin::loopbackStart (&loopbackCallback, loopSource);
    if (loop->handle == nullptr) { loop.reset(); loopSource->active = false; notify(); return false; }
    loopSource->setJamSeconds (jamMins * 60);
    notify();
    return true;
}

void Engine::stopLoopback()
{
    if (loop != nullptr) { tkwin::loopbackStop (loop->handle); loop.reset(); }
    if (loopSource != nullptr) loopSource->active = false;
}

// ------------------------------------------------------------ link

void Engine::onLinkAudio (int id, const link::Packet& p)
{
    Source* s = nullptr;
    bool added = false;
    {
        const juce::ScopedLock sl (lock);
        for (auto* e : extras) if (e->kind == Source::linked && e->linkId == id) s = e;
        if (s == nullptr)
            for (auto* e : extras) if (e->kind == Source::linked && ! e->active && e->name == p.name) s = e;   // reconnect: reuse
        if (s == nullptr) { s = extras.add (new Source (Source::linked, p.name, p.channels, p.sampleRate)); s->setJamSeconds (jamMins * 60); added = true; }
        if (! s->active || s->linkId != id) { s->active = true; s->linkId = id; added = true; }
        s->name = p.name;
    }
    s->setSampleRate (p.sampleRate);
    std::vector<float> l ((size_t) p.frames), r ((size_t) p.frames);
    for (int i = 0; i < p.frames; ++i)
    {
        l[(size_t) i] = p.interleaved[(size_t) (i * p.channels)];
        r[(size_t) i] = p.interleaved[(size_t) (i * p.channels + (p.channels > 1 ? 1 : 0))];
    }
    s->feed (l.data(), r.data(), p.frames);
    if (added) notify();
}

void Engine::onLinkLost (int id)
{
    {
        const juce::ScopedLock sl (lock);
        for (auto* e : extras) if (e->kind == Source::linked && e->linkId == id) e->active = false;
    }
    notify();
}

int Engine::linkCount() const
{
    const juce::ScopedLock sl (lock);
    int n = 0;
    for (auto* e : extras) if (e->kind == Source::linked && e->active) ++n;
    return n;
}

// ------------------------------------------------------------ playback

void Engine::play (const juce::AudioBuffer<float>& b, double s, int start, int len)
{
    const juce::SpinLock::ScopedLockType l (playLock);
    playBuf.makeCopyOf (b);
    playSr = s; playFrac = 0;
    playPos = juce::jlimit (0, juce::jmax (0, b.getNumSamples() - 1), start);
    playEnd = len < 0 ? b.getNumSamples() : juce::jmin (b.getNumSamples(), start + len);
    playing = b.getNumSamples() > 1;
}

void Engine::stopPlayback() { playing = false; }

void Engine::setGenerator (std::function<void (float*, float*, int, double)> g)
{
    const juce::SpinLock::ScopedLockType l (playLock);
    generator = std::move (g);
}

// ------------------------------------------------------------ MIDI

juce::MidiOutput* Engine::midiOut (const juce::String& id)
{
    const juce::ScopedLock sl (midiLock);
    if (id.isEmpty()) return nullptr;
    auto it = outs.find (id);
    if (it != outs.end()) return it->second.get();
    auto o = juce::MidiOutput::openDevice (id);
    auto* raw = o.get();
    if (o != nullptr) outs[id] = std::move (o);
    return raw;
}

void Engine::sendNow (const juce::String& id, const juce::MidiMessage& m)
{
    if (auto* o = midiOut (id)) o->sendMessageNow (m);
}

void Engine::allNotesOff (const juce::String& id, int ch)
{
    if (auto* o = midiOut (id))
    {
        o->sendMessageNow (juce::MidiMessage::allNotesOff (ch));
        for (int n = 0; n < 128; ++n) o->sendMessageNow (juce::MidiMessage::noteOff (ch, n));
    }
}

bool Engine::listen (const juce::String& id, std::function<void (const juce::MidiMessage&)> cb)
{
    stopListening();
    const juce::ScopedLock sl (midiLock);
    in = juce::MidiInput::openDevice (id, this);
    if (in == nullptr) return false;
    inCb = std::move (cb);
    in->start();
    return true;
}

void Engine::stopListening()
{
    std::unique_ptr<juce::MidiInput> old;
    {
        const juce::ScopedLock sl (midiLock);
        old = std::move (in);
        inCb = nullptr;
    }
    if (old) old->stop();
}

void Engine::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m)
{
    std::function<void (const juce::MidiMessage&)> cb;
    { const juce::ScopedLock sl (midiLock); cb = inCb; }
    if (cb) cb (m);
}

// ------------------------------------------------------------ demo (screenshots + tests)

void Engine::addDemoSources()
{
    const juce::ScopedLock sl (lock);
    const char* names[] = { "2600", "Hydrasynth", "Leviasynth", "MiniFreak", "Messenger", "MPC drums", "Mic", "SQ-1 hum" };
    for (auto* n : names) { inputs.add (new Source (Source::input, n, 1, 48000)); inputs.getLast()->setJamSeconds (60); }
    auto* l = extras.add (new Source (Source::linked, "Lead bus", 2, 48000));
    l->setJamSeconds (60);
    sr = 48000;
}

void Engine::runDemo (double seconds)
{
    const int n = 512;
    std::vector<float> L ((size_t) n), Rr ((size_t) n);
    auto all = sources();
    static double ph[16] = {};
    juce::Random rnd (3);
    for (int block = 0; block < (int) (seconds * 48000 / n); ++block)
    {
        for (int si = 0; si < all.size(); ++si)
        {
            for (int i = 0; i < n; ++i)
            {
                double t = (block * n + i) / 48000.0, v = 0;
                switch (si)
                {
                    case 0: ph[0] += 110.6 / 48000; v = 0.5 * (2 * (ph[0] - std::floor (ph[0])) - 1); break;                 // 2600 saw, slightly sharp A2
                    case 1: for (double f : { 146.83, 174.61, 220.0 }) v += 0.18 * std::sin (2 * juce::MathConstants<double>::pi * f * t); break;
                    case 2: ph[2] += 73.42 / 48000; v = 0.4 * std::sin (2 * juce::MathConstants<double>::pi * ph[2] + 2.5 * std::sin (2 * juce::MathConstants<double>::pi * ph[2] * 3)); break;
                    case 3: ph[3] += 293.66 / 48000; v = 0.35 * ((ph[3] - std::floor (ph[3])) < 0.3 ? 1 : -1); break;
                    case 4: ph[4] += 36.71 / 48000; v = 0.6 * std::sin (2 * juce::MathConstants<double>::pi * ph[4]); break;
                    case 5: { double k = std::fmod (t, 0.5); v = 0.9 * std::sin (2 * juce::MathConstants<double>::pi * (50 + 200 * std::exp (-k * 30)) * k) * std::exp (-k * 8); break; }
                    case 6: v = 0.02 * (rnd.nextFloat() * 2 - 1); break;
                    case 7: v = 0.03 * std::sin (2 * juce::MathConstants<double>::pi * 60 * t) + 0.012 * std::sin (2 * juce::MathConstants<double>::pi * 180 * t) + 0.001 * (rnd.nextFloat() - 0.5); break;
                    default: ph[8] += 220 / 48000.0; v = 0.3 * std::sin (2 * juce::MathConstants<double>::pi * ph[8]) + 0.1 * std::sin (2 * juce::MathConstants<double>::pi * ph[8] * 3.01);
                }
                L[(size_t) i] = (float) v;
                Rr[(size_t) i] = (float) (si == 8 ? v * 0.7 + 0.2 * std::sin (t * 900) : v);
            }
            all[si]->feed (L.data(), Rr.data(), n);
        }
    }
}
}
