#pragma once
// A stand-in for a real interface, used by the tests and by "Split Host --test": runs the HostCore at a
// steady block rate. Inputs 1-4 are sines (110, 220, 330, 440 Hz); inputs 5-8 are outputs 1-4 looped back,
// so one app can hear what another app played through Split.
#include "SplitCore.h"
#include <cmath>
#include <chrono>
#include <vector>

namespace split
{
    class TestClock
    {
    public:
        TestClock (HostCore& h) : host (h) {}
        ~TestClock() { stop(); }

        void start (double rate = 48000, int block = 256)
        {
            stop();
            sr = rate; n = block;
            std::vector<std::string> ins, outs;
            for (int i = 0; i < 8; ++i) { ins.push_back (i < 4 ? "Test sine " + std::to_string (i + 1) : "Loopback " + std::to_string (i - 3)); outs.push_back ("Out " + std::to_string (i + 1)); }
            host.publish (sr, n, ins, outs, n, n, "Split test clock");
            quit = false;
            th = std::thread ([this] { run(); });
        }

        void stop() { quit = true; if (th.joinable()) th.join(); }
        bool isRunning() const { return th.joinable() && ! quit; }
        double rate() const { return sr; }

    private:
        void run()
        {
            makeThreadRealtime();
            preciseTimers();
            std::vector<std::vector<float>> in (8, std::vector<float> ((size_t) n)), out (8, std::vector<float> ((size_t) n));
            std::vector<const float*> ip; std::vector<float*> op;
            for (auto& v : in) ip.push_back (v.data());
            for (auto& v : out) op.push_back (v.data());
            const auto period = std::chrono::duration<double> (n / sr);
            auto next = std::chrono::steady_clock::now();
            double phase[4] {};
            while (! quit)
            {
                for (int c = 0; c < 4; ++c)
                    for (int i = 0; i < n; ++i)
                    {
                        in[(size_t) c][(size_t) i] = 0.5f * (float) std::sin (phase[c]);
                        phase[c] += 2 * 3.14159265358979 * 110.0 * (c + 1) / sr;
                    }
                for (int c = 0; c < 4; ++c) in[(size_t) c + 4] = out[(size_t) c];
                host.process (ip.data(), 8, op.data(), 8, n);
                next += std::chrono::duration_cast<std::chrono::steady_clock::duration> (period);
                std::this_thread::sleep_until (next);
            }
        }

        HostCore& host;
        double sr = 48000; int n = 256;
        std::thread th;
        std::atomic<bool> quit { true };
    };
}
