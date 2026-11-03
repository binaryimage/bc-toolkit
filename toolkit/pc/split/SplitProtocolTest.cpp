// Runs the Split host core on a test clock and two apps' worth of client cores against it.
#include "SplitCore.h"
#include "SplitTestClock.h"
#include <cstdio>
#include <cmath>
#include <atomic>
#include <cstdlib>

using namespace split;
static int fails = 0, passes = 0;
static void check (bool ok, const char* what, const std::string& detail = {})
{
    std::printf ("%s  %s%s%s%s\n", ok ? "PASS" : "FAIL", what, detail.empty() ? "" : "   (", detail.c_str(), detail.empty() ? "" : ")");
    (ok ? passes : fails)++;
}

struct App : ClientCore::Handler
{
    ClientCore core;
    float writeValue = 0; int readCh = 0;
    std::atomic<int> blocks { 0 }, configs { 0 }, lost { 0 };
    std::atomic<float> readPeak { 0 }, readMean { 0 };
    void block (int64_t, int slot) override
    {
        auto* sh = core.shared();
        const int n = sh->bufferSize;
        float pk = 0, sum = 0;
        for (int i = 0; i < n; ++i) { float v = sh->in[slot][readCh][i]; pk = std::max (pk, std::abs (v)); sum += v; }
        readPeak = pk; readMean = sum / n;
        if (writeValue != 0) for (int i = 0; i < n; ++i) core.slot()->out[slot][0][i] = writeValue;
        ++blocks;
    }
    void configChanged() override { ++configs; }
    void hostLost() override { ++lost; }
};

// "--apps N": pretend to be three apps on an already running Split Host for N seconds (screenshots)
static int fakeApps (int seconds)
{
    App apps[3];
    const char* names[] = { "Ableton Live 12 Suite", "Rocksmith2014", "BC Toolkit" };
    std::string err;
    for (int i = 0; i < 3; ++i)
    {
        apps[i].writeValue = i == 0 ? 0.2f : 0;
        if (! apps[i].core.connect ({}, 3000, err) || ! apps[i].core.attach (names[i], &apps[i], err)) { std::printf ("app %d: %s\n", i, err.c_str()); return 1; }
        apps[i].core.setOutChannels (2);
        if (i < 2) apps[i].core.setRunning (true);
    }
    sleepMs (seconds * 1000);
    for (auto& a : apps) a.core.disconnect();
    return 0;
}

int main (int argc, char** argv)
{
    if (argc > 2 && std::string (argv[1]) == "--apps") return fakeApps (std::atoi (argv[2]));
    SharedRegion::unlink (kRegionName);
    for (int i = 0; i < kMaxClients; ++i) Signal::unlink (signalName (i));

    HostCore host;
    check (host.create(), "host makes the shared region");
    TestClock clock (host);
    clock.start (48000, 256);

    // the host's message-thread duties, as the Split Host app runs them
    std::atomic<bool> stopHousekeeping { false };
    std::thread house ([&] {
        while (! stopHousekeeping)
        {
            host.housekeeping();
            int who = -1; double r = host.pendingRateRequest (who);
            if (r > 0) { if (host.attachedCount() <= 1) { clock.stop(); clock.start (r, 256); } host.clearRateRequest(); }
            sleepMs (10);
        }
    });

    App a, b;
    a.writeValue = 0.25f; a.readCh = 0;
    b.readCh = 4;   // loopback of output 1: should carry what app A plays
    std::string err;
    check (a.core.connect ({}, 2000, err, true), "app A connects", err);
    check (b.core.connect ({}, 2000, err, true), "app B connects", err);
    check (a.core.attach ("Ableton Live 12", &a, err) && b.core.attach ("Rocksmith2014", &b, err), "both attach to the mixer", err);
    a.core.setOutChannels (1);
    a.core.setRunning (true); b.core.setRunning (true);
    sleepMs (1200);

    check (a.blocks > 150 && b.blocks > 150, "both apps get ~188 blocks a second at 48k / 256", std::to_string (a.blocks.load()) + " and " + std::to_string (b.blocks.load()));
    check (std::abs (a.readPeak - 0.5f) < 0.02f, "app A hears input 1 (the test sine)", std::to_string (a.readPeak.load()));
    check (std::abs (b.readMean - 0.25f) < 0.01f, "app B hears what app A played (mixed out, looped back in)", std::to_string (b.readMean.load()));
    check (a.core.slot()->late.load() < 5, "app A was almost never late", std::to_string (a.core.slot()->late.load()));
    auto cl = host.clients();
    check (cl.size() == 2 && cl[0].name == "Ableton Live 12" && cl[1].name == "Rocksmith2014", "the host lists both apps by name");

    // two apps attached: a rate change would yank the other one, so it's refused
    check (! a.core.requestRate (44100, 1500) && std::abs (host.shared()->sampleRate - 48000) < 1, "a rate change is refused while another app is attached");

    // the user changes the rate in the Split window: every app is told to reset
    int ca = a.configs, cb = b.configs;
    clock.stop(); clock.start (96000, 256);
    sleepMs (400);
    check (a.configs > ca && b.configs > cb, "a rate change in the host tells every app to reset");

    // alone, an app may change the rate itself (and isn't bounced into a reset for it)
    b.core.detach();
    ca = a.configs;
    check (a.core.requestRate (44100, 2000) && std::abs (host.shared()->sampleRate - 44100) < 1, "an app on its own can switch the rate", std::to_string (host.shared()->sampleRate));
    sleepMs (300);
    check (a.configs == ca, "the app that asked isn't sent a reset for its own change");
    int before = a.blocks; sleepMs (500);
    check (a.blocks - before > 70, "audio keeps flowing after the switch", std::to_string (a.blocks - before));

    // the host goes away
    clock.stop();
    stopHousekeeping = true; house.join();
    host.destroy();
    sleepMs (400);
    check (a.lost > 0, "apps hear about it when the host quits");
    a.core.disconnect();
    b.core.disconnect();
    SharedRegion::unlink (kRegionName);
    std::printf ("\n%d passed, %d failed\n", passes, fails);
    return fails == 0 ? 0 : 1;
}
