#pragma once
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace bcv
{
    /** A small pool of always-ready threads for splitting one frame's pixel work across cores.
        The calling thread works too, so Workers (8) uses 7 extra threads plus the caller.
        Calls must come from one thread at a time (the render thread). */
    class Workers
    {
    public:
        explicit Workers (int threads);
        ~Workers();
        int size() const { return (int) pool.size() + 1; }

        /** Runs fn (y0, y1) over [0, rows) in chunks, on every thread, and waits for all of it. */
        void forRows (int rows, const std::function<void (int, int)>& fn);
        /** Runs a on a pool thread and b on this one, at the same time, and waits for both. */
        void both (const std::function<void()>& a, const std::function<void()>& b);
        /** How many of the threads take part (1 .. size()); for Keep cool. */
        void setActive (int n) { active = n < 1 ? 1 : (n > size() ? size() : n); }
        int getActive() const { return active; }

        /** How many cores to use on this machine: the big cores on a phone, all but one elsewhere. */
        static int recommended();

    private:
        std::vector<std::thread> pool;
        std::mutex m;
        std::condition_variable wake, done;
        const std::function<void (int, int)>* job = nullptr;
        const std::function<void()>* single = nullptr;
        int rows = 0, chunk = 1, participants = 0, finished = 0;
        std::atomic<int> next { 0 };
        unsigned long generation = 0;
        bool quit = false;
        int active = 1;
        void loop (int index);
        void runChunks();
    };

    /** How many threads a style may split its own pixel work over. The Renderer sets it every frame from its
        workers (so Keep cool / Keep the PC responsive count); 0 = all but one core. */
    std::atomic<int>& pixelThreads();
    inline int pixelThreadCount()
    {
        const int n = pixelThreads().load();
        return n > 0 ? n : (std::max) (1, (int) std::thread::hardware_concurrency() - 1);
    }

    /** Splits rows across the workers, or runs them all here when there are none. */
    inline void forRows (Workers* w, int rows, const std::function<void (int, int)>& fn)
    {
        if (w == nullptr || w->getActive() <= 1 || rows < 64) fn (0, rows);
        else w->forRows (rows, fn);
    }
}
