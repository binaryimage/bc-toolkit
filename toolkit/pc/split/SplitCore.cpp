#include "SplitCore.h"
#include <cstring>
#include <cmath>
#include <algorithm>

namespace split
{
static void copyName (char* dst, size_t cap, const std::string& s)
{
    size_t n = std::min (cap - 1, s.size());
    std::memcpy (dst, s.data(), n);
    dst[n] = 0;
}

// ============================================================ host

HostCore::~HostCore() { destroy(); }

bool HostCore::create()
{
    destroy();
    bool existed = false;
    if (! region.create (kRegionName, sizeof (Shared), existed)) return false;
    sh = static_cast<Shared*> (region.data());
    if (! existed || sh->magic != kMagic || sh->version != kVersion)
    {
        std::memset (static_cast<void*> (sh), 0, sizeof (Shared));
        sh->magic = kMagic; sh->version = kVersion;
        for (auto& c : sh->clients) c.outSeq.store (-1);
    }
    else
    {
        // a host died and we're taking over: keep apps that are still attached, drop the rest
        for (auto& c : sh->clients) if (! processAlive (c.pid.load())) c.state.store (clientFree);
    }
    for (int i = 0; i < kMaxClients; ++i) signals[i].openOrCreate (signalName (i));
    if (conceal == nullptr) conceal.reset (new Conceal[kMaxClients]);
    for (int i = 0; i < kMaxClients; ++i) { conceal[i].held = false; conceal[i].recovering = false; }
    sh->outputDelay = delay.load();
    sh->hostPid.store (currentPid());
    sh->hostState.store (hostStarting);
    return true;
}

void HostCore::destroy()
{
    if (sh != nullptr)
    {
        sh->hostState.store (hostNone);
        sh->hostPid.store (0);
        sh->configGen.fetch_add (1);
        for (auto& s : signals) s.signal();
    }
    for (auto& s : signals) s.close();
    region.close();
    sh = nullptr;
}

void HostCore::setState (HostState s, const std::string& error)
{
    if (sh == nullptr) return;
    if (! error.empty()) copyName (sh->error, sizeof (sh->error), error);
    else if (s != hostError) sh->error[0] = 0;
    sh->hostState.store (s);
    if (s != hostRunning) for (auto& sig : signals) sig.signal();
}

void HostCore::publish (double rate, int bufferSize, const std::vector<std::string>& ins, const std::vector<std::string>& outs,
                        int inLat, int outLat, const std::string& device)
{
    if (sh == nullptr) return;
    sh->hostState.store (hostReconfiguring);
    sh->sampleRate = rate;
    sh->bufferSize = std::min (bufferSize, kMaxFrames);
    sh->numIns = std::min ((int) ins.size(), kMaxCh);
    sh->numOuts = std::min ((int) outs.size(), kMaxCh);
    sh->inLatency = inLat; sh->outLatency = outLat;
    sh->outputDelay = delay.load();
    for (int i = 0; i < kMaxCh; ++i)
    {
        copyName (sh->inNames[i], 32, i < (int) ins.size() ? ins[(size_t) i] : std::string());
        copyName (sh->outNames[i], 32, i < (int) outs.size() ? outs[(size_t) i] : std::string());
    }
    copyName (sh->deviceName, sizeof (sh->deviceName), device);
    sh->error[0] = 0;
    sh->configGen.fetch_add (1);
    sh->hostState.store (hostRunning);
    for (auto& s : signals) s.signal();
}

void HostCore::process (const float* const* in, int numIns, float* const* out, int numOuts, int n)
{
    if (sh == nullptr) return;
    n = std::min (n, kMaxFrames);
    const int64_t b = sh->seq.load (std::memory_order_relaxed);
    const int slot = (int) (b % kSlots);

    // 1. publish this block's inputs
    const int ins = std::min (numIns, kMaxCh);
    for (int c = 0; c < kMaxCh; ++c)
    {
        float* dst = sh->in[slot][c];
        if (c < ins && in[c] != nullptr)
        {
            std::memcpy (dst, in[c], sizeof (float) * (size_t) n);
            float pk = 0;
            for (int i = 0; i < n; ++i) pk = std::max (pk, std::abs (dst[i]));
            sh->inPeak[c] = std::max (pk, sh->inPeak[c] * 0.92f);
        }
        else if (c < sh->numIns) std::memset (dst, 0, sizeof (float) * (size_t) n);
    }
    sh->blockTimeNs[slot] = nowNs();
    sh->seq.store (b + 1, std::memory_order_release);
    for (int k = 0; k < kMaxClients; ++k)
        if (sh->clients[k].state.load (std::memory_order_relaxed) == clientRunning) signals[k].signal();

    // 2. mix what every app made `delay` blocks ago (reported to the apps as extra output latency)
    for (int c = 0; c < numOuts; ++c) if (out[c] != nullptr) std::memset (out[c], 0, sizeof (float) * (size_t) n);
    const int64_t prev = b - delay.load (std::memory_order_relaxed);
    if (prev >= 0)
    {
        const int pslot = (int) (prev % kSlots);
        for (int k = 0; k < kMaxClients; ++k)
        {
            auto& cl = sh->clients[k];
            auto& cc = conceal[k];
            if (cl.state.load (std::memory_order_relaxed) != clientRunning) { cc.held = false; cc.recovering = false; continue; }
            const int64_t done = cl.outSeq.load (std::memory_order_acquire);
            const bool onTime = done >= prev && done - prev < kSlots;
            if (! onTime)
            {
                if (done >= 0 && done < prev) cl.late.fetch_add (1, std::memory_order_relaxed);
                // fill the gap: its last block, backwards (starts on the very sample it ended on), fading to nothing
                if (cc.held && cc.frames > 0)
                {
                    const int m = std::min (n, cc.frames);
                    for (int c = 0; c < std::min (cc.chans, numOuts); ++c)
                    {
                        if (out[c] == nullptr) continue;
                        const float* src = cc.last[c];
                        for (int i = 0; i < m; ++i) out[c][i] += src[cc.frames - 1 - i] * (1.0f - (float) (i + 1) / (float) m);
                    }
                }
                cc.held = false;
                cc.recovering = true;
                continue;
            }
            const int chans = std::min ({ (int) cl.outChannels.load (std::memory_order_relaxed), numOuts, kMaxCh });
            for (int c = 0; c < chans; ++c)
            {
                const float* src = cl.out[pslot][c];
                std::memcpy (cc.last[c], src, sizeof (float) * (size_t) n);
                if (out[c] == nullptr) continue;
                if (cc.recovering) for (int i = 0; i < n; ++i) out[c][i] += src[i] * ((float) (i + 1) / (float) n);   // back in from silence
                else               for (int i = 0; i < n; ++i) out[c][i] += src[i];
            }
            cc.chans = chans; cc.frames = n; cc.held = true; cc.recovering = false;
        }
    }
    for (int c = 0; c < std::min (numOuts, kMaxCh); ++c)
    {
        if (out[c] == nullptr) continue;
        float pk = 0;
        for (int i = 0; i < n; ++i) pk = std::max (pk, std::abs (out[c][i]));
        sh->outPeak[c] = std::max (pk, sh->outPeak[c] * 0.92f);
    }
}

void HostCore::setOutputDelay (int blocks)
{
    blocks = std::max (kOutputDelayBlocks, std::min (kMaxOutputDelayBlocks, blocks));
    if (blocks == delay.load()) return;
    delay.store (blocks);
    if (sh == nullptr) return;
    sh->outputDelay = blocks;
    sh->configGen.fetch_add (1);          // apps reset and pick up the new latency
    for (auto& s : signals) s.signal();
}

void HostCore::housekeeping()
{
    if (sh == nullptr) return;
    for (auto& c : sh->clients)
    {
        const uint32_t s = c.state.load();
        if (s == clientFree) continue;
        if (! processAlive (c.pid.load())) { c.state.store (clientFree); c.pid.store (0); }
    }
}

std::vector<HostCore::ClientInfo> HostCore::clients() const
{
    std::vector<ClientInfo> v;
    if (sh == nullptr) return v;
    for (int i = 0; i < kMaxClients; ++i)
    {
        auto& c = sh->clients[i];
        auto s = c.state.load();
        if (s == clientFree) continue;
        v.push_back ({ i, std::string (c.name, strnlen (c.name, sizeof (c.name))), s == clientRunning, c.late.load() });
    }
    return v;
}

int HostCore::attachedCount() const { return (int) clients().size(); }

double HostCore::pendingRateRequest (int& who) const
{
    if (sh == nullptr) return 0;
    uint32_t r = sh->rateRequestClient.load();
    if (r == 0) return 0;
    who = (int) r - 1;
    return sh->requestedRate;
}

void HostCore::clearRateRequest() { if (sh) sh->rateRequestClient.store (0); }

// ============================================================ client

ClientCore::~ClientCore() { disconnect(); }

bool ClientCore::connect (const std::string& hostExe, int timeoutMs, std::string& error, bool allowSameProcess)
{
    disconnect();
    auto hostUp = [this] {
        return sh != nullptr && sh->magic == kMagic && processAlive (sh->hostPid.load())
               && (sh->hostState.load() == hostRunning || sh->hostState.load() == hostError);
    };
    if (region.open (kRegionName, sizeof (Shared))) sh = static_cast<Shared*> (region.data());
    if (! allowSameProcess && sh != nullptr && sh->hostPid.load() == currentPid())
    {
        error = "Split can't be opened by its own host.";
        disconnect();
        return false;
    }
    if (! hostUp())
    {
        if (hostExe.empty() || ! launch (hostExe, "--background"))
        {
            error = "The Split host isn't running and couldn't be started" + (hostExe.empty() ? std::string (".") : " (" + hostExe + ").");
            disconnect();
            return false;
        }
        const uint32_t t0 = tickMs();
        while (tickMs() - t0 < (uint32_t) timeoutMs)
        {
            sleepMs (50);
            if (sh == nullptr && region.open (kRegionName, sizeof (Shared))) sh = static_cast<Shared*> (region.data());
            if (hostUp()) break;
        }
    }
    if (! hostUp())
    {
        error = "The Split host didn't come up. Open BC Split from the Start menu to see why.";
        disconnect();
        return false;
    }
    if (sh->hostState.load() == hostError)
    {
        error = std::string (sh->error, strnlen (sh->error, sizeof (sh->error)));
        if (error.empty()) error = "Split couldn't open your audio interface.";
        disconnect();
        return false;
    }
    knownGenShared = sh->configGen.load();
    return true;
}

void ClientCore::disconnect()
{
    detach();
    region.close();
    sh = nullptr;
}

bool ClientCore::attach (const std::string& appName, Handler* h, std::string& error)
{
    if (sh == nullptr) { error = "Not connected to the Split host."; return false; }
    if (mine != nullptr) return true;
    const uint32_t pid = currentPid();
    for (int i = 0; i < kMaxClients; ++i)
    {
        auto& c = sh->clients[i];
        uint32_t expected = clientFree;
        if (! c.state.compare_exchange_strong (expected, clientAttached)) continue;
        c.pid.store (pid);
        copyName (c.name, sizeof (c.name), appName);
        c.outSeq.store (-1);
        c.late.store (0);
        c.outChannels.store (0);
        c.heartbeat.store (tickMs());
        std::memset (c.out, 0, sizeof (c.out));
        mine = &c; index = i; handler = h;
        tick.openOrCreate (signalName (i));
        quit = false;
        knownGenShared = sh->configGen.load();
        worker = std::thread ([this] { loop(); });
        return true;
    }
    error = "Too many apps are using Split at once (8 is the limit).";
    return false;
}

void ClientCore::detach()
{
    if (mine == nullptr) return;
    running = false;
    mine->state.store (clientAttached);
    quit = true;
    tick.signal();
    if (worker.joinable()) worker.join();
    mine->pid.store (0);
    mine->state.store (clientFree);
    tick.close();
    mine = nullptr; index = -1; handler = nullptr;
}

void ClientCore::setRunning (bool shouldRun)
{
    if (mine == nullptr) return;
    running = shouldRun;
    mine->outSeq.store (-1);
    mine->state.store (shouldRun ? clientRunning : clientAttached);
}

bool ClientCore::requestRate (double rate, int timeoutMs)
{
    if (sh == nullptr || index < 0) return false;
    if (std::abs (sh->sampleRate - rate) < 0.5) return true;
    ownRequest = true;
    sh->requestedRate = rate;
    sh->rateRequestClient.store ((uint32_t) index + 1);
    const uint32_t t0 = tickMs();
    while (tickMs() - t0 < (uint32_t) timeoutMs)
    {
        sleepMs (25);
        if (sh->rateRequestClient.load() == 0) break;
    }
    // the reconfigure happens inside our own app's reset, so don't bounce it back at the app
    knownGenShared = sh->configGen.load();
    ownRequest = false;
    return std::abs (sh->sampleRate - rate) < 0.5 && sh->hostState.load() == hostRunning;
}

void ClientCore::loop()
{
    makeThreadRealtime();   // this thread runs the app's audio callback, like a real driver's would
    int64_t last = sh->seq.load();
    bool lostSent = false;
    while (! quit.load())
    {
        const bool woke = tick.wait (100);
        if (quit.load()) break;
        mine->heartbeat.store (tickMs());

        const uint32_t hs = sh->hostState.load();
        if (hs == hostNone || ! processAlive (sh->hostPid.load()))
        {
            if (! lostSent && handler != nullptr) { lostSent = true; handler->hostLost(); }
            continue;
        }
        const uint32_t gen = sh->configGen.load();
        if (gen != knownGenShared.load())
        {
            // own is read before the swap: requestRate stores the new gen before it clears ownRequest,
            // so a change we asked for ourselves never reaches the app as a reset
            const bool own = ownRequest.load();
            uint32_t expected = knownGenShared.load();
            if (expected != gen && knownGenShared.compare_exchange_strong (expected, gen) && ! own && handler != nullptr)
                handler->configChanged();
            last = sh->seq.load();
            continue;
        }
        if (! woke || ! running.load() || hs != hostRunning) continue;

        const int64_t s = sh->seq.load (std::memory_order_acquire);
        if (s < last) last = s;                 // host restarted its count
        const int d = outputDelayOf (sh);
        if (s - last > d + 1) last = s - d;     // fell way behind: skip ahead
        while (last < s && ! quit.load())
        {
            const int64_t b = last;              // block number = completed count - 1
            if (handler != nullptr) handler->block (b, (int) (b % kSlots));
            mine->outSeq.store (b, std::memory_order_release);
            ++last;
        }
    }
}
}
