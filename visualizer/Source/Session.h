#pragma once
#include "Renderer.h"
#include "Video.h"
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

        std::atomic<double> progress { 0 };
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
    };
}
