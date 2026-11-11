#include "Video.h"

#if JUCE_WINDOWS
 #include <windows.h>
 #include <mfapi.h>
 #include <mfidl.h>
 #include <mfreadwrite.h>
 #include <mferror.h>
 #include <codecapi.h>
 #pragma comment (lib, "mfplat.lib")
 #pragma comment (lib, "mfreadwrite.lib")
 #pragma comment (lib, "mfuuid.lib")
 #pragma comment (lib, "ole32.lib")
#else
 #include <cstdio>
#endif
#include <cstring>

namespace bcv
{
#if JUCE_WINDOWS
template <typename T> struct Com
{
    T* p = nullptr;
    ~Com() { if (p) p->Release(); }
    T** operator&() { return &p; }
    T* operator->() const { return p; }
    operator T*() const { return p; }
};

class MFEncoder : public VideoEncoder
{
public:
    ~MFEncoder() override { close(); }

    bool open (const juce::File& file, int w, int h, int f, juce::String& error) override
    {
        width = w; height = h; fps = f;
        HRESULT hr = CoInitializeEx (nullptr, COINIT_MULTITHREADED);
        comInit = SUCCEEDED (hr);
        if (FAILED (hr = MFStartup (MF_VERSION))) return fail (error, "Media Foundation is not available", hr);
        mfStarted = true;
        file.deleteFile();

        Com<IMFAttributes> attrs;
        MFCreateAttributes (&attrs, 4);
        attrs->SetUINT32 (MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
        attrs->SetUINT32 (MF_SINK_WRITER_DISABLE_THROTTLING, TRUE);
        attrs->SetGUID (MF_TRANSCODE_CONTAINERTYPE, MFTranscodeContainerType_MPEG4);
        if (FAILED (hr = MFCreateSinkWriterFromURL (file.getFullPathName().toWideCharPointer(), nullptr, attrs, &writer)))
            return fail (error, "Couldn't create " + file.getFileName(), hr);

        // video: H.264 High profile, YouTube-ish bitrates
        const UINT32 pixels = (UINT32) (w * h);
        UINT32 kbps = pixels >= 3840u * 2160u ? 45000 : pixels >= 2560u * 1440u ? 24000 : 12000;
        if (fps > 30) kbps = kbps * 3 / 2;
        {
            Com<IMFMediaType> out;
            MFCreateMediaType (&out);
            out->SetGUID (MF_MT_MAJOR_TYPE, MFMediaType_Video);
            out->SetGUID (MF_MT_SUBTYPE, MFVideoFormat_H264);
            out->SetUINT32 (MF_MT_AVG_BITRATE, kbps * 1000);
            out->SetUINT32 (MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
            out->SetUINT32 (MF_MT_MPEG2_PROFILE, eAVEncH264VProfile_High);
            MFSetAttributeSize (out, MF_MT_FRAME_SIZE, (UINT32) w, (UINT32) h);
            MFSetAttributeRatio (out, MF_MT_FRAME_RATE, (UINT32) fps, 1);
            MFSetAttributeRatio (out, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
            if (FAILED (hr = writer->AddStream (out, &videoStream))) return fail (error, "The H.264 encoder refused the video settings", hr);
        }
        {
            Com<IMFMediaType> in;
            MFCreateMediaType (&in);
            in->SetGUID (MF_MT_MAJOR_TYPE, MFMediaType_Video);
            in->SetGUID (MF_MT_SUBTYPE, MFVideoFormat_RGB32);
            in->SetUINT32 (MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
            in->SetUINT32 (MF_MT_DEFAULT_STRIDE, (UINT32) (w * 4));   // positive stride = top-down rows
            MFSetAttributeSize (in, MF_MT_FRAME_SIZE, (UINT32) w, (UINT32) h);
            MFSetAttributeRatio (in, MF_MT_FRAME_RATE, (UINT32) fps, 1);
            MFSetAttributeRatio (in, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
            if (FAILED (hr = writer->SetInputMediaType (videoStream, in, nullptr))) return fail (error, "The H.264 encoder refused the picture format", hr);
        }
        // audio: AAC 256 kbps, 48 kHz stereo
        {
            Com<IMFMediaType> out;
            MFCreateMediaType (&out);
            out->SetGUID (MF_MT_MAJOR_TYPE, MFMediaType_Audio);
            out->SetGUID (MF_MT_SUBTYPE, MFAudioFormat_AAC);
            out->SetUINT32 (MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
            out->SetUINT32 (MF_MT_AUDIO_SAMPLES_PER_SECOND, kAudioRate);
            out->SetUINT32 (MF_MT_AUDIO_NUM_CHANNELS, 2);
            out->SetUINT32 (MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 24000);
            if (FAILED (hr = writer->AddStream (out, &audioStream))) return fail (error, "The AAC encoder refused the audio settings", hr);
        }
        {
            Com<IMFMediaType> in;
            MFCreateMediaType (&in);
            in->SetGUID (MF_MT_MAJOR_TYPE, MFMediaType_Audio);
            in->SetGUID (MF_MT_SUBTYPE, MFAudioFormat_PCM);
            in->SetUINT32 (MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
            in->SetUINT32 (MF_MT_AUDIO_SAMPLES_PER_SECOND, kAudioRate);
            in->SetUINT32 (MF_MT_AUDIO_NUM_CHANNELS, 2);
            in->SetUINT32 (MF_MT_AUDIO_BLOCK_ALIGNMENT, 4);
            in->SetUINT32 (MF_MT_AUDIO_AVG_BYTES_PER_SECOND, kAudioRate * 4);
            if (FAILED (hr = writer->SetInputMediaType (audioStream, in, nullptr))) return fail (error, "The AAC encoder refused the audio format", hr);
        }
        if (FAILED (hr = writer->BeginWriting())) return fail (error, "Couldn't start writing the video", hr);
        writing = true;
        return true;
    }

    bool writeFrame (const juce::Image& img) override
    {
        if (! writing) return false;
        const DWORD bytes = (DWORD) (width * height * 4);
        Com<IMFMediaBuffer> buf;
        if (FAILED (MFCreateMemoryBuffer (bytes, &buf))) return false;
        BYTE* dst = nullptr;
        buf->Lock (&dst, nullptr, nullptr);
        {
            juce::Image::BitmapData bd (img, juce::Image::BitmapData::readOnly);
            if (bd.pixelFormat == juce::Image::ARGB && bd.pixelStride == 4)
                MFCopyImage (dst, width * 4, bd.data, bd.lineStride, (DWORD) width * 4, (DWORD) height);
            else
                for (int y = 0; y < height; ++y)
                    for (int x = 0; x < width; ++x)
                    {
                        auto c = bd.getPixelColour (x, y);
                        auto* p = dst + (size_t) (y * width + x) * 4;
                        p[0] = c.getBlue(); p[1] = c.getGreen(); p[2] = c.getRed(); p[3] = 255;
                    }
        }
        buf->Unlock();
        buf->SetCurrentLength (bytes);
        Com<IMFSample> sample;
        MFCreateSample (&sample);
        sample->AddBuffer (buf);
        const LONGLONG dur = 10000000LL / fps;
        sample->SetSampleTime (framesWritten * 10000000LL / fps);
        sample->SetSampleDuration (dur);
        ++framesWritten;
        HRESULT hr = writer->WriteSample (videoStream, sample);
        if (FAILED (hr)) { lastError = "Writing a video frame failed (" + hex (hr) + ")"; return false; }
        return true;
    }

    bool writeAudio (const int16_t* data, int frames) override
    {
        if (! writing || frames <= 0) return writing;
        const DWORD bytes = (DWORD) frames * 4;
        Com<IMFMediaBuffer> buf;
        if (FAILED (MFCreateMemoryBuffer (bytes, &buf))) return false;
        BYTE* dst = nullptr;
        buf->Lock (&dst, nullptr, nullptr);
        memcpy (dst, data, bytes);
        buf->Unlock();
        buf->SetCurrentLength (bytes);
        Com<IMFSample> sample;
        MFCreateSample (&sample);
        sample->AddBuffer (buf);
        sample->SetSampleTime (audioWritten * 10000000LL / kAudioRate);
        sample->SetSampleDuration ((LONGLONG) frames * 10000000LL / kAudioRate);
        audioWritten += frames;
        HRESULT hr = writer->WriteSample (audioStream, sample);
        if (FAILED (hr)) { lastError = "Writing audio failed (" + hex (hr) + ")"; return false; }
        return true;
    }

    bool close() override
    {
        bool ok = true;
        if (writer.p)
        {
            if (writing) { HRESULT hr = writer->Finalize(); ok = SUCCEEDED (hr); if (! ok) lastError = "Finishing the MP4 failed (" + hex (hr) + ")"; }
            writer.p->Release(); writer.p = nullptr;
        }
        writing = false;
        if (mfStarted) { MFShutdown(); mfStarted = false; }
        if (comInit) { CoUninitialize(); comInit = false; }
        return ok;
    }

private:
    Com<IMFSinkWriter> writer;
    DWORD videoStream = 0, audioStream = 0;
    int width = 0, height = 0, fps = 30;
    LONGLONG framesWritten = 0, audioWritten = 0;
    bool writing = false, mfStarted = false, comInit = false;

    static juce::String hex (HRESULT hr) { return "0x" + juce::String::toHexString ((int) hr).paddedLeft ('0', 8); }
    bool fail (juce::String& error, const juce::String& what, HRESULT hr) { error = what + " (" + hex (hr) + ")"; lastError = error; close(); return false; }
};

std::unique_ptr<VideoEncoder> VideoEncoder::create() { return std::make_unique<MFEncoder>(); }

#elif JUCE_ANDROID
}
#include "VideoAndroid.h"
namespace bcv
{
#else
/** Non-Windows: raw frames into ffmpeg, audio into a temp WAV, muxed on close. */
class FfmpegEncoder : public VideoEncoder
{
public:
    ~FfmpegEncoder() override { close(); }
    bool open (const juce::File& f, int w, int h, int fp, juce::String& error) override
    {
        file = f; width = w; height = h; fps = fp;
        file.deleteFile();
        videoTmp = file.getSiblingFile (file.getFileNameWithoutExtension() + ".video-tmp.mp4");
        audioTmp = file.getSiblingFile (file.getFileNameWithoutExtension() + ".audio-tmp.wav");
        juce::String cmd = "ffmpeg -loglevel error -y -f rawvideo -pix_fmt bgra -s " + juce::String (w) + "x" + juce::String (h)
                         + " -r " + juce::String (fps) + " -i - -c:v libx264 -preset veryfast -crf 18 -pix_fmt yuv420p \"" + videoTmp.getFullPathName() + "\"";
        pipe = popen (cmd.toRawUTF8(), "w");
        if (pipe == nullptr) { error = lastError = "ffmpeg not found"; return false; }
        audioTmp.deleteFile();
        auto* os = audioTmp.createOutputStream().release();
        wav.reset (juce::WavAudioFormat().createWriterFor (os, kAudioRate, 2, 16, {}, 0));
        row.resize ((size_t) w * 4);
        return wav != nullptr;
    }
    bool writeFrame (const juce::Image& img) override
    {
        juce::Image::BitmapData bd (img, juce::Image::BitmapData::readOnly);
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                auto c = bd.getPixelColour (x, y);
                auto* p = row.data() + x * 4;
                p[0] = c.getBlue(); p[1] = c.getGreen(); p[2] = c.getRed(); p[3] = 255;
            }
            if (fwrite (row.data(), 1, row.size(), pipe) != row.size()) return false;
        }
        return true;
    }
    bool writeAudio (const int16_t* data, int frames) override
    {
        juce::AudioBuffer<float> b (2, frames);
        for (int i = 0; i < frames; ++i) { b.setSample (0, i, data[i * 2] / 32768.0f); b.setSample (1, i, data[i * 2 + 1] / 32768.0f); }
        return wav->writeFromAudioSampleBuffer (b, 0, frames);
    }
    bool close() override
    {
        if (pipe == nullptr) return true;
        bool ok = pclose (pipe) == 0; pipe = nullptr;
        wav.reset();
        juce::String cmd = "ffmpeg -loglevel error -y -i \"" + videoTmp.getFullPathName() + "\" -i \"" + audioTmp.getFullPathName()
                         + "\" -c:v copy -c:a aac -b:a 256k -shortest -movflags +faststart \"" + file.getFullPathName() + "\"";
        ok = ok && std::system (cmd.toRawUTF8()) == 0;
        videoTmp.deleteFile(); audioTmp.deleteFile();
        if (! ok) lastError = "ffmpeg failed";
        return ok;
    }
private:
    juce::File file, videoTmp, audioTmp;
    int width = 0, height = 0, fps = 30;
    FILE* pipe = nullptr;
    std::unique_ptr<juce::AudioFormatWriter> wav;
    std::vector<uint8_t> row;
};

std::unique_ptr<VideoEncoder> VideoEncoder::create() { return std::make_unique<FfmpegEncoder>(); }
#endif
}
