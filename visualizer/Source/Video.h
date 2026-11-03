#pragma once
#include "Common/JuceHeader.h"

namespace bcv
{
    /** MP4 writer: H.264 video + AAC audio at 48 kHz stereo.
        Windows uses Media Foundation; elsewhere it pipes to ffmpeg (if installed).
        All calls must come from the same thread. */
    class VideoEncoder
    {
    public:
        virtual ~VideoEncoder() = default;
        virtual bool open (const juce::File& file, int width, int height, int fps, juce::String& error) = 0;
        /** frame must be width x height; written in order */
        virtual bool writeFrame (const juce::Image& frame) = 0;
        /** interleaved 16-bit stereo at 48 kHz */
        virtual bool writeAudio (const int16_t* interleaved, int frames) = 0;
        virtual bool close() = 0;
        juce::String lastError;

        static std::unique_ptr<VideoEncoder> create();
        static constexpr int kAudioRate = 48000;
    };
}
