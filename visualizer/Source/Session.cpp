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
    auto enc = VideoEncoder::create();
    if (! enc->open (output, width, height, fps, error)) { finished = true; return; }

    auto all = makeAll();
    const bool reel = look.style < 0;
    int current = reel ? 0 : juce::jlimit (0, (int) all.size() - 1, look.style);
    all[(size_t) current]->reset();

    Analyzer an;
    Renderer rr;
    Frame fr;
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
            rr.render (g, img.getBounds().toFloat(), fr, *all[(size_t) current], look);
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
    }

    bool closed = enc->close();
    if (error.isEmpty() && ! closed) error = enc->lastError;
    ok = error.isEmpty() && ! cancelled && output.getSize() > 0;
    if (! ok) output.deleteFile();
    secondsTaken = (juce::Time::getMillisecondCounterHiRes() - start) / 1000.0;
    finished = true;
}
}
