#pragma once
#include "Common/JuceHeader.h"
#include "Parallel.h"

namespace bcv
{
    /** MP4 writer: H.264 video + AAC audio at 48 kHz stereo.
        Windows uses Media Foundation, Android the phone's hardware encoder (MediaCodec); elsewhere it pipes to ffmpeg (if installed).
        All calls must come from the same thread. */
    class VideoEncoder
    {
    public:
        virtual ~VideoEncoder() = default;
        /** before open(): HEVC instead of H.264 (where the encoder has it), bits per second (0 = pick one for the size),
            and threads for the colour conversion. Encoders that can't do these ignore them. */
        virtual void setOptions (bool hevc, int bitsPerSecond, Workers* workers) { juce::ignoreUnused (hevc, bitsPerSecond, workers); }
        /** what open() actually used, for the status line ("H.264 hardware", "HEVC" ...) */
        juce::String description;
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
