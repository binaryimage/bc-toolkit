#pragma once
#include "Renderer.h"
#include "Video.h"
#include "Patterns.h"
#include <atomic>

namespace bcv
{
    /** A loaded track, converted to 48 kHz stereo (the rate the MP4 is written at). */
    struct Track
    {
        juce::File file;
        juce::AudioBuffer<float> audio;
        static constexpr double rate = 48000.0;
        double seconds() const { return audio.getNumSamples() / rate; }
        bool loaded() const { return audio.getNumSamples() > 0; }
    };

    bool loadTrack (const juce::File&, Track&, juce::String& error);
    void setDemoTrack (Track&, double seconds);

    struct VideoSize { int w, h; const char* label; };
    const std::vector<VideoSize>& videoSizes();

    /** Renders and encodes a whole video. Runs on its own thread, or inline via runNow(). */
    class ExportJob : public juce::Thread
    {
    public:
        /** style -1 = demo reel: every style in turn, cycleSeconds each, titled with the style name */
        ExportJob (const Track& track, Look look, juce::File out, int width, int height, int fps, double cycleSeconds = 4);
        ~ExportJob() override { stopThread (10000); }
        void run() override;
        bool runNow() { run(); return ok; }

        // ---- optional, set before starting (the defaults are the old behaviour: one style, one thread)
        Plan plan;                       // several styles fading into each other (empty = look.style all the way)
        int threads = 1;                 // cores to render on
        bool hevc = false;               // HEVC instead of H.264, where the encoder has it
        int bitrate = 0;                 // bits per second, 0 = the encoder picks for the size
        std::atomic<bool> keepCool { false };
        std::function<int()> heat;       // 0 cool .. 4+ very hot (Android thermal status); used by Keep cool

        // ---- live readouts while it runs
        std::atomic<double> progress { 0 };
        std::atomic<double> framesPerSecond { 0 };   // render speed over the last couple of seconds
        std::atomic<double> secondsLeft { -1 };
        std::atomic<int> heatNow { 0 };
        std::atomic<bool> easing { false };          // Keep cool is holding it back right now
        std::atomic<int> thumbVersion { 0 };
        juce::String encoderInfo;                    // e.g. "H.264 60 Mbps (hardware)", set once it opens
        /** a small copy of the latest frame, for showing progress */
        juce::Image getThumb() { const juce::ScopedLock l (thumbLock); return thumb.createCopy(); }
        std::atomic<bool> finished { false };
        bool ok = false, cancelled = false;
        juce::String error;
        juce::File output;
        double secondsTaken = 0;

    private:
        const Track& track;
        Look look;
        int width, height, fps;
        double cycle;
        juce::Image thumb;
        juce::CriticalSection thumbLock;
    };
}
