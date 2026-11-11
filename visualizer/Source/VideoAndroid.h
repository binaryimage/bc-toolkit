// Included by Video.cpp on Android: MP4 out of the phone's own encoders (MediaCodec) and muxer, through the NDK.
// Video: H.264 High or HEVC Main from the hardware encoder, fed NV12 (or I420) converted on all cores.
// Audio: AAC-LC 48 kHz stereo. Both go into one MP4 with MediaMuxer.
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>
#include <media/NdkMediaMuxer.h>
#include <fcntl.h>
#include <unistd.h>
#include <deque>

namespace bcv
{
class AndroidEncoder : public VideoEncoder
{
public:
    ~AndroidEncoder() override { close(); release(); }

    void setOptions (bool useHevc, int bps, Workers* w) override { hevc = useHevc; bitrate = bps; workers = w; }

    bool open (const juce::File& f, int w, int h, int frameRate, juce::String& error) override
    {
        file = f; width = w; height = h; fps = frameRate;
        file.deleteFile();
        fd = ::open (file.getFullPathName().toRawUTF8(), O_CREAT | O_TRUNC | O_RDWR, 0644);
        if (fd < 0) return fail (error, "Can't write " + file.getFullPathName());
        muxer = AMediaMuxer_new (fd, AMEDIAMUXER_OUTPUT_FORMAT_MPEG_4);
        if (muxer == nullptr) return fail (error, "The phone's MP4 writer didn't start");

        juce::String why;
        if (! (hevc && openVideo ("video/hevc", why)) && ! openVideo ("video/avc", why))
            return fail (error, "The video encoder refused " + juce::String (w) + "x" + juce::String (h) + " at " + juce::String (fps) + " fps" + (why.isNotEmpty() ? " (" + why + ")" : juce::String()));
        if (! openAudio()) return fail (error, "The AAC audio encoder didn't start");
        ySize = (size_t) stride * (size_t) sliceHeight;
        return true;
    }

    bool writeFrame (const juce::Image& img) override
    {
        if (video == nullptr) return false;
        ssize_t idx = -1;
        for (int tries = 0; tries < 2000 && idx < 0; ++tries)      // up to ~10 s for a free input buffer
        {
            idx = AMediaCodec_dequeueInputBuffer (video, 5000);
            if (idx < 0 && idx != AMEDIACODEC_INFO_TRY_AGAIN_LATER) { lastError = "The video encoder stopped (" + juce::String ((int) idx) + ")"; return false; }
            if (idx < 0 && ! drain (false)) return false;
        }
        if (idx < 0) { lastError = "The video encoder stopped taking frames"; return false; }
        size_t cap = 0;
        uint8_t* buf = AMediaCodec_getInputBuffer (video, (size_t) idx, &cap);
        // what the picture needs (rows up to the real height) and the whole padded frame the encoder lays out
        const size_t need = colourFormat == 21 ? ySize + (size_t) stride * (size_t) ((height + 1) / 2)
                                               : ySize + (size_t) (stride / 2) * (size_t) ((sliceHeight + 1) / 2 + (height + 1) / 2);
        const size_t full = ySize + (size_t) stride * (size_t) ((sliceHeight + 1) / 2);
        if (buf == nullptr || cap < need) { lastError = "Video input buffer too small (" + juce::String ((juce::int64) cap) + " < " + juce::String ((juce::int64) need) + ")"; return false; }
        convert (img, buf);
        const int64_t pts = framesIn * 1000000LL / fps;
        if (AMediaCodec_queueInputBuffer (video, (size_t) idx, 0, juce::jmin (cap, full), (uint64_t) pts, 0) != AMEDIA_OK) { lastError = "Queueing a video frame failed"; return false; }
        ++framesIn;
        return drain (false);
    }

    bool writeAudio (const int16_t* data, int frames) override
    {
        if (audio == nullptr) return false;
        const uint8_t* src = reinterpret_cast<const uint8_t*> (data);
        size_t left = (size_t) frames * 4;
        while (left > 0)
        {
            ssize_t idx = -1;
            for (int tries = 0; tries < 2000 && idx < 0; ++tries)
            {
                idx = AMediaCodec_dequeueInputBuffer (audio, 5000);
                if (idx < 0 && idx != AMEDIACODEC_INFO_TRY_AGAIN_LATER) { lastError = "The audio encoder stopped (" + juce::String ((int) idx) + ")"; return false; }
                if (idx < 0 && ! drain (false)) return false;
            }
            if (idx < 0) { lastError = "The audio encoder stopped taking samples"; return false; }
            size_t cap = 0;
            uint8_t* buf = AMediaCodec_getInputBuffer (audio, (size_t) idx, &cap);
            const size_t n = juce::jmin (left, cap & ~(size_t) 3);
            if (buf == nullptr || n == 0) { lastError = "Audio input buffer unusable"; return false; }
            std::memcpy (buf, src, n);
            const int64_t pts = samplesIn * 1000000LL / kAudioRate;
            if (AMediaCodec_queueInputBuffer (audio, (size_t) idx, 0, n, (uint64_t) pts, 0) != AMEDIA_OK) { lastError = "Queueing audio failed"; return false; }
            samplesIn += (int64_t) (n / 4);
            src += n; left -= n;
        }
        return drain (false);
    }

    bool close() override
    {
        if (muxer == nullptr && fd < 0) return closed;
        bool ok = lastError.isEmpty();
        if (ok)
        {
            ok = endOfStream (video, framesIn * 1000000LL / juce::jmax (1, fps)) && endOfStream (audio, samplesIn * 1000000LL / kAudioRate);
            // wait for both encoders to hand over everything they have
            for (int i = 0; i < 4000 && ok && ! (videoDone && audioDone); ++i) ok = drain (true);
            if (ok && ! (videoDone && audioDone)) { lastError = "The encoders didn't finish"; ok = false; }
        }
        if (muxer != nullptr)
        {
            if (started && AMediaMuxer_stop (muxer) != AMEDIA_OK && ok) { lastError = "Finishing the MP4 failed"; ok = false; }
            if (! started && ok) { lastError = "Nothing was written"; ok = false; }
            AMediaMuxer_delete (muxer); muxer = nullptr;
        }
        if (fd >= 0) { ::close (fd); fd = -1; }
        release();
        closed = ok;
        return ok;
    }

private:
    juce::File file;
    int width = 0, height = 0, fps = 30, bitrate = 0, stride = 0, sliceHeight = 0, colourFormat = 21;
    bool hevc = false, started = false, videoDone = false, audioDone = false, closed = false;
    size_t ySize = 0;
    Workers* workers = nullptr;
    int fd = -1;
    AMediaMuxer* muxer = nullptr;
    AMediaCodec* video = nullptr;
    AMediaCodec* audio = nullptr;
    ssize_t videoTrack = -1, audioTrack = -1;
    int64_t framesIn = 0, samplesIn = 0;
    struct Pending { bool isVideo; std::vector<uint8_t> data; AMediaCodecBufferInfo info; };
    std::deque<Pending> pending;     // encoded before the muxer could start (it needs both tracks first)

    bool fail (juce::String& error, const juce::String& why) { error = lastError = why; return false; }

    void release()
    {
        for (auto** c : { &video, &audio })
            if (*c != nullptr) { AMediaCodec_stop (*c); AMediaCodec_delete (*c); *c = nullptr; }
    }

    int defaultBitrate (bool isHevc) const
    {
        // generous: the styles are full of fine detail, grain and static, which starves a thin bitrate
        const double pps = (double) width * height * fps;
        const double bps = pps * (isHevc ? 0.16 : 0.26);
        return (int) juce::jlimit (8.0e6, isHevc ? 100.0e6 : 120.0e6, bps);
    }

    bool openVideo (const char* mime, juce::String& why)
    {
        const bool isHevc = juce::String (mime) == "video/hevc";
        const int bps = bitrate > 0 ? (isHevc ? (int) (bitrate * 0.6) : bitrate) : defaultBitrate (isHevc);
        // NV12 first (what the Snapdragon encoders want), then I420, then "flexible" read as I420
        for (int colour : { 21, 19, 0x7F420888 })
            for (int extras = 1; extras >= 0; --extras)       // with the speed hints, then without if refused
            {
                AMediaCodec* c = AMediaCodec_createEncoderByType (mime);
                if (c == nullptr) { why = juce::String (mime) + " not available"; return false; }
                AMediaFormat* fmt = AMediaFormat_new();
                AMediaFormat_setString (fmt, AMEDIAFORMAT_KEY_MIME, mime);
                AMediaFormat_setInt32 (fmt, AMEDIAFORMAT_KEY_WIDTH, width);
                AMediaFormat_setInt32 (fmt, AMEDIAFORMAT_KEY_HEIGHT, height);
                AMediaFormat_setInt32 (fmt, AMEDIAFORMAT_KEY_BIT_RATE, bps);
                AMediaFormat_setInt32 (fmt, AMEDIAFORMAT_KEY_FRAME_RATE, fps);
                AMediaFormat_setInt32 (fmt, AMEDIAFORMAT_KEY_I_FRAME_INTERVAL, 1);
                AMediaFormat_setInt32 (fmt, AMEDIAFORMAT_KEY_COLOR_FORMAT, colour);
                AMediaFormat_setInt32 (fmt, "bitrate-mode", 1);                       // VBR
                AMediaFormat_setInt32 (fmt, "color-standard", 1);                     // BT.709, what YouTube assumes for HD
                AMediaFormat_setInt32 (fmt, "color-range", 2);                        // limited (TV) range
                AMediaFormat_setInt32 (fmt, "color-transfer", 3);                     // SDR video
                if (extras)
                {
                    AMediaFormat_setInt32 (fmt, "profile", isHevc ? 1 : 8);           // HEVC Main / H.264 High
                    AMediaFormat_setInt32 (fmt, "priority", 1);                       // not real-time: as fast as it can go
                    AMediaFormat_setFloat (fmt, "operating-rate", (float) juce::jmin (240, fps * 4));
                }
                const auto st = AMediaCodec_configure (c, fmt, nullptr, nullptr, AMEDIACODEC_CONFIGURE_FLAG_ENCODE);
                AMediaFormat_delete (fmt);
                if (st != AMEDIA_OK || AMediaCodec_start (c) != AMEDIA_OK) { AMediaCodec_delete (c); continue; }

                stride = width; sliceHeight = height; colourFormat = colour;
                if (AMediaFormat* in = AMediaCodec_getInputFormat (c))
                {
                    int32_t v = 0;
                    if (AMediaFormat_getInt32 (in, "stride", &v) && v >= width) stride = v;
                    if (AMediaFormat_getInt32 (in, "slice-height", &v) && v >= height) sliceHeight = v;
                    if (AMediaFormat_getInt32 (in, AMEDIAFORMAT_KEY_COLOR_FORMAT, &v) && (v == 19 || v == 21)) colourFormat = v;
                    AMediaFormat_delete (in);
                }
                if (colourFormat != 21) colourFormat = 19;
                video = c;
                char* name = nullptr;
                juce::String codecName;
                if (AMediaCodec_getName (c, &name) == AMEDIA_OK && name != nullptr) { codecName = name; AMediaCodec_releaseName (c, name); }
                description = juce::String (isHevc ? "HEVC" : "H.264") + " " + juce::String (bps / 1000000) + " Mbps"
                            + (codecName.containsIgnoreCase ("android") || codecName.containsIgnoreCase ("google") ? " (software)" : " (hardware)");
                return true;
            }
        why = "no colour format accepted";
        return false;
    }

    bool openAudio()
    {
        audio = AMediaCodec_createEncoderByType ("audio/mp4a-latm");
        if (audio == nullptr) return false;
        AMediaFormat* fmt = AMediaFormat_new();
        AMediaFormat_setString (fmt, AMEDIAFORMAT_KEY_MIME, "audio/mp4a-latm");
        AMediaFormat_setInt32 (fmt, AMEDIAFORMAT_KEY_SAMPLE_RATE, kAudioRate);
        AMediaFormat_setInt32 (fmt, AMEDIAFORMAT_KEY_CHANNEL_COUNT, 2);
        AMediaFormat_setInt32 (fmt, AMEDIAFORMAT_KEY_BIT_RATE, 320000);
        AMediaFormat_setInt32 (fmt, AMEDIAFORMAT_KEY_AAC_PROFILE, 2);                // AAC-LC
        AMediaFormat_setInt32 (fmt, AMEDIAFORMAT_KEY_MAX_INPUT_SIZE, 16384);
        bool ok = AMediaCodec_configure (audio, fmt, nullptr, nullptr, AMEDIACODEC_CONFIGURE_FLAG_ENCODE) == AMEDIA_OK
               && AMediaCodec_start (audio) == AMEDIA_OK;
        AMediaFormat_delete (fmt);
        if (! ok) { AMediaCodec_delete (audio); audio = nullptr; }
        return ok;
    }

    /** RGB to BT.709 limited-range YUV 4:2:0, two rows at a time, spread over the cores */
    void convert (const juce::Image& img, uint8_t* out)
    {
        juce::Image::BitmapData bd (img, juce::Image::BitmapData::readOnly);
        const int w = juce::jmin (width, img.getWidth()), h = juce::jmin (height, img.getHeight());
        uint8_t* Y = out;
        uint8_t* UV = out + ySize;                                           // NV12: U,V interleaved
        uint8_t* U = UV;                                                    // I420: U plane then V plane
        uint8_t* V = UV + (size_t) (stride / 2) * (size_t) ((sliceHeight + 1) / 2);
        const int pairs = (h + 1) / 2;
        forRows (workers, pairs, [&] (int p0, int p1)
        {
            for (int p = p0; p < p1; ++p)
            {
                const int y0 = p * 2, y1 = juce::jmin (h - 1, y0 + 1);
                const uint8_t* l0 = bd.getLinePointer (y0);
                const uint8_t* l1 = bd.getLinePointer (y1);
                uint8_t* yr0 = Y + (size_t) y0 * (size_t) stride;
                uint8_t* yr1 = Y + (size_t) y1 * (size_t) stride;
                for (int x = 0; x < w; x += 2)
                {
                    const int x1 = juce::jmin (w - 1, x + 1);
                    int sr = 0, sg = 0, sb = 0;
                    auto luma = [&] (const uint8_t* line, int xx, uint8_t* dst)
                    {
                        const auto* px = reinterpret_cast<const juce::PixelARGB*> (line + (size_t) xx * (size_t) bd.pixelStride);
                        const int r = px->getRed(), g = px->getGreen(), b = px->getBlue();
                        sr += r; sg += g; sb += b;
                        *dst = (uint8_t) (16 + ((47 * r + 157 * g + 16 * b + 128) >> 8));
                    };
                    luma (l0, x, yr0 + x); luma (l0, x1, yr0 + x1);
                    luma (l1, x, yr1 + x); luma (l1, x1, yr1 + x1);
                    sr = (sr + 2) >> 2; sg = (sg + 2) >> 2; sb = (sb + 2) >> 2;
                    const int u = juce::jlimit (16, 240, 128 + ((-26 * sr - 87 * sg + 112 * sb + 128) >> 8));
                    const int v = juce::jlimit (16, 240, 128 + ((112 * sr - 102 * sg - 10 * sb + 128) >> 8));
                    if (colourFormat == 21)
                    {
                        uint8_t* c = UV + (size_t) p * (size_t) stride + (size_t) x;
                        c[0] = (uint8_t) u; c[1] = (uint8_t) v;
                    }
                    else
                    {
                        const size_t o = (size_t) p * (size_t) (stride / 2) + (size_t) (x / 2);
                        U[o] = (uint8_t) u; V[o] = (uint8_t) v;
                    }
                }
            }
        });
    }

    bool endOfStream (AMediaCodec* c, int64_t pts)
    {
        if (c == nullptr) return false;
        for (int tries = 0; tries < 2000; ++tries)
        {
            const ssize_t idx = AMediaCodec_dequeueInputBuffer (c, 5000);
            if (idx < 0 && idx != AMEDIACODEC_INFO_TRY_AGAIN_LATER) { lastError = "An encoder stopped while finishing"; return false; }
            if (idx >= 0)
                return AMediaCodec_queueInputBuffer (c, (size_t) idx, 0, 0, (uint64_t) pts, AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) == AMEDIA_OK;
            if (! drain (false)) return false;
        }
        lastError = "Couldn't finish the stream";
        return false;
    }

    /** moves whatever the encoders have finished into the MP4; wait = block briefly for more */
    bool drain (bool wait)
    {
        for (int which = 0; which < 2; ++which)
        {
            AMediaCodec* c = which == 0 ? video : audio;
            bool& done = which == 0 ? videoDone : audioDone;
            if (c == nullptr || done) continue;
            for (;;)
            {
                AMediaCodecBufferInfo info {};
                const ssize_t idx = AMediaCodec_dequeueOutputBuffer (c, &info, wait ? 2000 : 0);
                if (idx == AMEDIACODEC_INFO_TRY_AGAIN_LATER) break;
                if (idx == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED)
                {
                    AMediaFormat* of = AMediaCodec_getOutputFormat (c);
                    const ssize_t t = AMediaMuxer_addTrack (muxer, of);
                    AMediaFormat_delete (of);
                    if (t < 0) { lastError = "The MP4 writer refused a track"; return false; }
                    (which == 0 ? videoTrack : audioTrack) = t;
                    if (! started && videoTrack >= 0 && audioTrack >= 0)
                    {
                        if (AMediaMuxer_start (muxer) != AMEDIA_OK) { lastError = "The MP4 writer didn't start"; return false; }
                        started = true;
                        while (! pending.empty())
                        {
                            auto& pd = pending.front();
                            AMediaMuxer_writeSampleData (muxer, (size_t) (pd.isVideo ? videoTrack : audioTrack), pd.data.data(), &pd.info);
                            pending.pop_front();
                        }
                    }
                    continue;
                }
                if (idx == AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED) continue;
                if (idx < 0) { lastError = juce::String (which == 0 ? "The video" : "The audio") + " encoder failed (" + juce::String ((int) idx) + ")"; return false; }
                size_t size = 0;
                uint8_t* data = AMediaCodec_getOutputBuffer (c, (size_t) idx, &size);
                const bool config = (info.flags & AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG) != 0;
                if (data != nullptr && info.size > 0 && ! config)
                {
                    if (started)
                        AMediaMuxer_writeSampleData (muxer, (size_t) (which == 0 ? videoTrack : audioTrack), data + info.offset, &info);
                    else
                    {
                        Pending pd { which == 0, std::vector<uint8_t> (data + info.offset, data + info.offset + info.size), info };
                        pd.info.offset = 0;
                        pending.push_back (std::move (pd));
                    }
                }
                AMediaCodec_releaseOutputBuffer (c, (size_t) idx, false);
                if ((info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) != 0) { done = true; break; }
            }
        }
        return true;
    }
};

std::unique_ptr<VideoEncoder> VideoEncoder::create() { return std::make_unique<AndroidEncoder>(); }
}
