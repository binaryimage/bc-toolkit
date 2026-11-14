#include "Session.h"

namespace bcv
{
const std::vector<VideoSize>& videoSizes()
{
    static const std::vector<VideoSize> s { { 1920, 1080, "1080p (1920 x 1080)" }, { 2560, 1440, "1440p (2560 x 1440)" }, { 3840, 2160, "4K (3840 x 2160)" } };
    return s;
}

bool loadTrack (const juce::File& file, Track& t, juce::String& error)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (file));
    if (r == nullptr) r.reset (fm.createReaderFor (file.createInputStream()));   // no (or a wrong) extension: try every format
    if (r == nullptr) { error = "Can't read " + file.getFileName() + " (use WAV, AIFF, FLAC, MP3 or OGG)"; return false; }
    if (r->lengthInSamples <= 0 || r->lengthInSamples > (juce::int64) (r->sampleRate * 60 * 60)) { error = "That file is empty or longer than an hour"; return false; }
    const int n = (int) r->lengthInSamples;
    juce::AudioBuffer<float> src ((int) juce::jmax (1u, r->numChannels), n);
    r->read (&src, 0, n, 0, true, true);

    const double ratio = r->sampleRate / Track::rate;
    const int outN = (int) std::ceil (n / ratio);
    t.audio.setSize (2, outN);
    for (int ch = 0; ch < 2; ++ch)
    {
        const float* in = src.getReadPointer (juce::jmin (ch, src.getNumChannels() - 1));
        if (std::abs (ratio - 1.0) < 1e-9) { t.audio.copyFrom (ch, 0, in, juce::jmin (n, outN)); continue; }
        juce::LagrangeInterpolator li;
        std::vector<float> padded ((size_t) n + 8, 0.0f);
        std::copy (in, in + n, padded.begin());
        li.process (ratio, padded.data(), t.audio.getWritePointer (ch), outN);
    }
    t.file = file;
    return true;
}

void setDemoTrack (Track& t, double seconds)
{
    t.audio = makeDemoTrack (Track::rate, seconds);
    t.file = juce::File();
}

ExportJob::ExportJob (const Track& tr, Look l, juce::File out, int w, int h, int f, double c)
    : juce::Thread ("BC Visualizer export"), output (std::move (out)), track (tr), look (std::move (l)), width (w), height (h), fps (f), cycle (c) {}

void ExportJob::run()
{
    const auto start = juce::Time::getMillisecondCounterHiRes();
    output.getParentDirectory().createDirectory();
    std::unique_ptr<Workers> workers;
    if (threads > 1) workers = std::make_unique<Workers> (threads);
    auto enc = VideoEncoder::create();
    enc->setOptions (hevc, bitrate, workers.get());
    if (! enc->open (output, width, height, fps, error)) { finished = true; return; }
    encoderInfo = enc->description;

    auto all = makeAll();
    const bool reel = look.style < 0;
    int current = reel ? 0 : juce::jlimit (0, (int) all.size() - 1, look.style);
    all[(size_t) current]->reset();

    Analyzer an;
    Renderer rr;
    rr.setWorkers (workers.get());
    StylePlayer stylePlayer;
    const bool usePlan = ! reel && ! plan.steps.empty();
    Frame fr;
    double speedT0 = juce::Time::getMillisecondCounterHiRes();
    int64_t speedF0 = 0;
    juce::Image img (juce::Image::ARGB, width, height, true, juce::SoftwareImageType());
    const double dur = track.seconds();
    const int64_t total = (int64_t) std::ceil (dur * fps);
    std::vector<int16_t> pcm;
    int64_t audioDone = 0;
    const int nAudio = track.audio.getNumSamples();

    for (int64_t i = 0; i < total; ++i)
    {
        if (threadShouldExit()) { cancelled = true; break; }
        const double t = (double) i / fps;
        if (reel)
        {
            int want = (int) (t / juce::jmax (0.25, cycle)) % (int) all.size();
            if (want != current) { current = want; all[(size_t) current]->reset(); }
            look.title = all[(size_t) current]->name();
        }
        an.analyse (track.audio, Track::rate, t, fps, fr);
        fr.duration = dur;
        fr.index = i;
        {
            juce::Graphics g (img);
            if (usePlan)
            {
                auto now = stylePlayer.at (t, plan, all);
                Look lk = look;
                lk.rollA = now.palA; lk.rollB = now.palB; lk.rollMix = now.palMix;
                rr.render (g, img.getBounds().toFloat(), fr, *now.a, lk, now.b, now.mix);
            }
            else rr.render (g, img.getBounds().toFloat(), fr, *all[(size_t) current], look);
        }
        // audio for this frame goes in first so the muxer always has both streams in step
        const int64_t audioEnd = juce::jmin ((int64_t) nAudio, (i + 1) * (int64_t) Track::rate / fps);
        const int chunk = (int) (audioEnd - audioDone);
        if (chunk > 0)
        {
            pcm.resize ((size_t) chunk * 2);
            const float* l = track.audio.getReadPointer (0) + audioDone;
            const float* r = track.audio.getReadPointer (1) + audioDone;
            for (int s = 0; s < chunk; ++s)
            {
                pcm[(size_t) s * 2] = (int16_t) std::lrint (juce::jlimit (-1.0f, 1.0f, l[s]) * 32767.0f);
                pcm[(size_t) s * 2 + 1] = (int16_t) std::lrint (juce::jlimit (-1.0f, 1.0f, r[s]) * 32767.0f);
            }
            if (! enc->writeAudio (pcm.data(), chunk)) { error = enc->lastError; break; }
            audioDone = audioEnd;
        }
        if (! enc->writeFrame (img)) { error = enc->lastError.isNotEmpty() ? enc->lastError : "Writing a frame failed"; break; }
        progress = (double) (i + 1) / (double) total;

        // speed, time left, a thumbnail now and then, and Keep cool
        const double nowMs = juce::Time::getMillisecondCounterHiRes();
        if (nowMs - speedT0 > 1500)
        {
            framesPerSecond = (double) (i + 1 - speedF0) * 1000.0 / (nowMs - speedT0);
            secondsLeft = framesPerSecond > 0 ? (double) (total - i - 1) / framesPerSecond : -1.0;
            speedT0 = nowMs; speedF0 = i + 1;
            {
                const int tw = 480, th = juce::jmax (1, tw * height / juce::jmax (1, width));
                juce::Image small (juce::Image::RGB, tw, th, true, juce::SoftwareImageType());
                { juce::Graphics gs (small); gs.setImageResamplingQuality (juce::Graphics::mediumResamplingQuality); gs.drawImage (img, small.getBounds().toFloat()); }
                const juce::ScopedLock l (thumbLock);
                thumb = small;
            }
            ++thumbVersion;
            const int h = heat ? heat() : 0;
            heatNow = h;
            const bool ease = keepCool && h >= 2;     // 2 = moderate: the phone is starting to throttle itself
            easing = ease;
            if (workers) workers->setActive (ease ? juce::jmax (2, workers->size() / (h >= 3 ? 4 : 2)) : workers->size());
        }
        if (easing) juce::Thread::sleep (heatNow >= 3 ? 20 : 6);
    }

    bool closed = enc->close();
    if (error.isEmpty() && ! closed) error = enc->lastError;
    ok = error.isEmpty() && ! cancelled && output.getSize() > 0;
    if (! ok) output.deleteFile();
    secondsTaken = (juce::Time::getMillisecondCounterHiRes() - start) / 1000.0;
    finished = true;
}
}
