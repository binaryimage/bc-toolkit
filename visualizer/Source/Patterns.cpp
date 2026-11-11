#include "Patterns.h"
#include <algorithm>

namespace bcv
{
SongEvents scanSong (const juce::AudioBuffer<float>& audio, double sampleRate, std::function<bool()> shouldStop)
{
    SongEvents s;
    s.duration = audio.getNumSamples() / sampleRate;
    if (s.duration <= 0) return s;
    constexpr double fps = 30;     // the same rate the preview and the 30 fps export listen at
    Analyzer an;
    Frame f;
    int lastSection = 0;
    float lastDrop = 0;
    const int64_t total = (int64_t) (s.duration * fps);
    for (int64_t i = 0; i < total; ++i)
    {
        if ((i & 255) == 0 && shouldStop && shouldStop()) { s.events.clear(); s.duration = 0; return s; }
        const double t = (double) i / fps;
        an.analyse (audio, sampleRate, t, fps, f);
        const bool slam = f.drop > 0.9f && lastDrop < 0.5f;
        const bool section = f.section != lastSection;
        if (slam || section) s.events.push_back ({ t, slam, slam ? f.drop : f.change });
        lastSection = f.section;
        lastDrop = f.drop;
    }
    return s;
}

int Plan::stepAt (double t) const
{
    int k = 0;
    for (int i = 1; i < (int) steps.size(); ++i)
        if (steps[(size_t) i].fadeStart <= t) k = i; else break;
    return k;
}

namespace
{
    /** Hands out the next style: picks in order, a shuffled deck of picks, or random from everything. */
    struct Order
    {
        Order (const Patterns& p, int numStyles) : rnd ((juce::int64) p.seed * 2654435761u + 17), n (numStyles), shuffle (p.shuffle), random (p.mode == Patterns::random)
        {
            for (int s : p.picks) if (s >= 0 && s < n && std::find (picks.begin(), picks.end(), s) == picks.end()) picks.push_back (s);
        }
        int next()
        {
            int s;
            if (random)
            {
                const int avoid = juce::jmin (12, n / 3);          // don't come back to one you just saw
                do s = rnd.nextInt (n);
                while (std::find (recent.end() - juce::jmin ((int) recent.size(), avoid), recent.end(), s) != recent.end());
            }
            else if (! shuffle) s = picks[(size_t) (pos++ % (int) picks.size())];
            else
            {
                if (deck.empty())
                {
                    deck = picks;
                    for (int i = (int) deck.size() - 1; i > 0; --i) std::swap (deck[(size_t) i], deck[(size_t) rnd.nextInt (i + 1)]);
                    if (deck.size() > 1 && ! recent.empty() && deck.back() == recent.back()) std::swap (deck.front(), deck.back());
                }
                s = deck.back(); deck.pop_back();
            }
            recent.push_back (s);
            return s;
        }
        juce::Random rnd;
        int n, pos = 0;
        bool shuffle, random;
        std::vector<int> picks, deck, recent;
    };
}

Plan makePlan (const Patterns& p, const SongEvents& song, int singleStyle, int numStyles)
{
    Plan plan;
    singleStyle = juce::jlimit (0, juce::jmax (0, numStyles - 1), singleStyle);
    Order order (p, numStyles);
    const bool switching = numStyles > 1 && ! song.empty()
                        && ((p.mode == Patterns::random) || (p.mode == Patterns::picked && order.picks.size() > 1));
    if (! switching)
    {
        const int only = p.mode == Patterns::picked && order.picks.size() == 1 ? order.picks[0] : singleStyle;
        plan.steps.push_back ({ 0, 0, only });
        return plan;
    }

    const double D = song.duration;
    double fade = juce::jlimit (0.0, 10.0, p.fadeSeconds);
    const double every = juce::jmax (2.0, p.everySeconds);
    if (p.when == Patterns::timer) fade = juce::jmin (fade, every * 0.8);
    const double minS = juce::jmax (p.minSeconds, fade + 1.0);
    const double maxS = juce::jmax (p.maxSeconds, minS + 1.0);
    const double endGuard = D - (fade * 0.5 + 3.0);       // no switch in the last few seconds

    plan.steps.push_back ({ 0, 0, order.next() });
    double last = 0;
    size_t idx = 0;
    const auto& ev = song.events;
    for (;;)
    {
        double at; bool slam = false;
        if (p.when == Patterns::followSong)
        {
            while (idx < ev.size() && ev[idx].t < last + minS) ++idx;
            if (idx < ev.size() && ev[idx].t <= last + maxS) { at = ev[idx].t; slam = ev[idx].slam; ++idx; }
            else at = last + maxS;
        }
        else
        {
            at = last + every;
            if (p.cutOnSlams)
            {
                const double from = last + juce::jmax (2.0, juce::jmin (every * 0.5, minS));
                while (idx < ev.size() && ev[idx].t < from) ++idx;
                for (size_t j = idx; j < ev.size() && ev[j].t < at; ++j)
                    if (ev[j].slam) { at = ev[j].t; slam = true; idx = j + 1; break; }
            }
        }
        if (at > endGuard) break;
        const bool cut = slam && p.cutOnSlams;
        Plan::Step s;
        s.style = order.next();
        s.fade = cut ? 0.0 : fade;
        s.fadeStart = cut ? at : juce::jmax (0.0, at - fade * 0.5);   // fades are centred on the change
        plan.steps.push_back (s);
        last = at;
    }
    return plan;
}

StylePlayer::Now StylePlayer::at (double t, const Plan& plan, std::vector<std::unique_ptr<Viz>>& all)
{
    Now now;
    if (plan.steps.empty() || all.empty()) return now;
    const int n = (int) all.size();
    const int k = plan.stepAt (t);
    const auto& step = plan.steps[(size_t) k];
    int a = juce::jlimit (0, n - 1, step.style), b = -1;
    if (k > 0 && step.fade > 0 && t < step.fadeStart + step.fade)
    {
        b = a;
        a = juce::jlimit (0, n - 1, plan.steps[(size_t) k - 1].style);
        const float u = (float) juce::jlimit (0.0, 1.0, (t - step.fadeStart) / step.fade);
        now.mix = u * u * (3 - 2 * u);
    }
    for (int s : { a, b })
        if (s >= 0 && s != onA && s != onB) all[(size_t) s]->reset();   // it's just come on: start it fresh
    onA = a; onB = b;
    now.a = all[(size_t) a].get();
    now.b = b >= 0 ? all[(size_t) b].get() : nullptr;
    now.styleA = a; now.styleB = b;
    if (b == a) { now.b = nullptr; now.styleB = -1; now.mix = 0; }
    return now;
}
}
