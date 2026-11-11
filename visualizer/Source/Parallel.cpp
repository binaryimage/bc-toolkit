#include "Parallel.h"
#include <algorithm>
#include <cstdlib>

namespace bcv
{
Workers::Workers (int threads)
{
    threads = std::max (1, threads);
    for (int i = 0; i < threads - 1; ++i) pool.emplace_back ([this, i] { loop (i); });
    active = threads;
}

Workers::~Workers()
{
    { std::lock_guard<std::mutex> l (m); quit = true; ++generation; }
    wake.notify_all();
    for (auto& t : pool) t.join();
}

int Workers::recommended()
{
    if (const char* e = std::getenv ("BCV_THREADS")) if (std::atoi (e) > 0) return std::atoi (e);   // tests
    const int n = (int) std::thread::hardware_concurrency();
   #if defined (__ANDROID__)
    return std::clamp (n, 1, 8);          // Snapdragon 8 Elite: 2 prime + 6 performance cores, all worth using
   #else
    return std::clamp (n - 1, 1, 16);
   #endif
}

void Workers::runChunks()
{
    for (;;)
    {
        const int c = next.fetch_add (1);
        const int y0 = c * chunk;
        if (y0 >= rows) break;
        (*job) (y0, std::min (rows, y0 + chunk));
    }
}

void Workers::loop (int index)
{
    unsigned long seen = 0;
    for (;;)
    {
        bool mine = false, isSingle = false;
        {
            std::unique_lock<std::mutex> l (m);
            wake.wait (l, [&] { return quit || generation != seen; });
            if (quit) return;
            seen = generation;
            isSingle = single != nullptr;
            mine = isSingle ? index == 0 : index < participants - 1;
        }
        if (! mine) continue;
        if (isSingle) (*single)();
        else runChunks();
        { std::lock_guard<std::mutex> l (m); ++finished; }
        done.notify_all();
    }
}

void Workers::forRows (int n, const std::function<void (int, int)>& fn)
{
    const int helpers = std::min ((int) pool.size(), active - 1);
    if (helpers <= 0) { fn (0, n); return; }
    {
        std::lock_guard<std::mutex> l (m);
        job = &fn; single = nullptr; rows = n;
        participants = helpers + 1;
        chunk = std::max (8, n / (participants * 4));     // a few chunks each so fast threads pick up slack
        next = 0; finished = 0;
        ++generation;
    }
    wake.notify_all();
    runChunks();
    std::unique_lock<std::mutex> l (m);
    done.wait (l, [&] { return finished >= helpers; });
    job = nullptr;
}

void Workers::both (const std::function<void()>& a, const std::function<void()>& b)
{
    if (pool.empty() || active < 2) { a(); b(); return; }
    {
        std::lock_guard<std::mutex> l (m);
        single = &a; job = nullptr; finished = 0;
        ++generation;
    }
    wake.notify_all();
    b();
    std::unique_lock<std::mutex> l (m);
    done.wait (l, [&] { return finished >= 1; });
    single = nullptr;
}
}
