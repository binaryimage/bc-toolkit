#pragma once
#include "Common/JuceHeader.h"
#include "Common/Dsp.h"
#include "Common/Link.h"
#include <array>
#include <atomic>
#include <map>
#include <memory>

namespace tk
{
    /** Audio shared between a page and the playback voices, so playing it never copies it. */
    using SharedAudio = std::shared_ptr<const juce::AudioBuffer<float>>;

    /** How one voice plays a stretch of audio. Positions are in samples of the source buffer. */
    struct PlayOptions
    {
        int start = 0, length = -1;     // -1 = to the end
        float gainDb = 0;
        double semitones = 0;           // repitch (changes speed, like an MPC pad's tune)
        bool reverse = false, loop = false;
        bool choke = true;              // stop every other voice first (mono); false lets hits overlap
        float fadeInMs = 0.7f, fadeOutMs = 4.0f;   // tiny ramps so starts, ends and stops never click
    };

    /** One thing that makes sound: an interface input, an Ableton track over Link, or the PC's own output. */
    class Source
    {
    public:
        enum Kind { input, linked, loopback, demo };
        Source (Kind k, juce::String n, int channels, double sr);

        void feed (const float* l, const float* r, int n);            // any producer thread
        void copyLatest (float* l, float* r, int n) const;            // UI thread
        void setSampleRate (double s);

        // jam recorder (always-on ring of the last N minutes)
        void setJamSeconds (int s);
        juce::AudioBuffer<float> jamSnapshot (double seconds) const;
        double jamFilledSeconds() const;

        // capture (record / auto-sample)
        void startCapture (double maxSeconds);
        void armCapture (double maxSeconds, float thresholdDb);
        juce::AudioBuffer<float> stopCapture();
        int captureState() const noexcept { return capState.load(); } // 0 idle, 1 armed, 2 recording
        double capturedSeconds() const noexcept { return capLen.load() / sr.load(); }
        /** running count of frames fed, and the frame number where the current capture began (-1 until it starts) */
        juce::int64 totalFrames() const noexcept { return frames.load(); }
        juce::int64 captureStartFrame() const noexcept { return capStartFrame.load(); }

        Kind kind; juce::String name; int channels; std::atomic<double> sr;
        std::atomic<bool> active { true };
        std::atomic<float> peakDb { -120 }, rmsDb { -120 };
        std::atomic<double> lufsM { -120 }, lufsS { -120 }, lufsI { -120 }, truePeak { -120 };
        std::atomic<juce::uint32> lastFeed { 0 };
        std::atomic<bool> resetLoudness { false };
        int linkId = 0;

    private:
        static constexpr int R = 16384;
        std::vector<float> ringL, ringR;
        std::atomic<int> w { 0 };
        Loudness loud;
        float pkHold = 0, rmsAcc = 0;
        // jam
        mutable juce::SpinLock jamLock;
        std::vector<juce::int16> jamL, jamR;
        juce::int64 jamPos = 0, jamCount = 0;
        // capture
        juce::SpinLock capLock;
        juce::AudioBuffer<float> cap;
        std::atomic<int> capLen { 0 }, capState { 0 };
        std::atomic<juce::int64> frames { 0 }, capStartFrame { -1 };
        float armTh = 0.03f;
    };

    /** Audio + MIDI hub for the whole app. */
    class Engine : private juce::AudioIODeviceCallback, private juce::ChangeListener, private juce::MidiInputCallback
    {
    public:
        Engine (bool openDevices = true);
        ~Engine() override;

        juce::AudioDeviceManager devices;
        double sampleRate() const { return sr.load(); }
        /** true when an output device is open (so voices actually advance) */
        bool hasOutput() { return devices.getCurrentAudioDevice() != nullptr; }

        // sources (pointers stay valid for the life of the app)
        juce::Array<Source*> sources (bool activeOnly = true) const;
        Source* source (int index) const;
        juce::String sourceLabel (const Source*) const;
        void renameInput (int index, const juce::String& name);
        std::function<void()> onSourcesChanged;

        // jam recorder
        void setJamMinutes (int m);
        int jamMinutes() const { return jamMins; }
        juce::File saveJam (double seconds, const juce::String& label);

        // the PC's own output (Windows "what you hear")
        bool loopbackAvailable() const;
        bool startLoopback();
        void stopLoopback();
        bool loopbackRunning() const;

        // preview playback through the output: up to 32 voices, mixed on the audio thread
        /** Play shared audio without copying it. Returns a token for the new voice. */
        int play (SharedAudio audio, double sampleRate, const PlayOptions& options = {});
        /** Copies b first (fine for short previews); stops anything else that's playing. */
        void play (const juce::AudioBuffer<float>& b, double sampleRate, int start = 0, int len = -1);
        /** Fades every voice out over a few milliseconds. */
        void stopPlayback();
        /** Where the most recently started voice is, in samples of its buffer (-1 when nothing has played). */
        int playPosition() const { return focusPos.load(); }
        /** True while the most recently started voice is still sounding. */
        bool isPlaying() const { return focusActive.load(); }
        int playingToken() const { return focusToken.load(); }
        int voicesPlaying() const { return voiceCount.load(); }
        /** Mix every voice into L/R at the output rate (the audio callback uses this; tests call it directly). */
        void renderVoices (float* L, float* R, int n, double outputRate);
        /** a continuous generator (wavetable preview, riff synth). nullptr to stop. */
        void setGenerator (std::function<void (float*, float*, int, double)> g);

        // MIDI
        juce::Array<juce::MidiDeviceInfo> midiOuts() const { return juce::MidiOutput::getAvailableDevices(); }
        juce::Array<juce::MidiDeviceInfo> midiIns() const { return juce::MidiInput::getAvailableDevices(); }
        juce::MidiOutput* midiOut (const juce::String& identifier);
        void sendNow (const juce::String& outId, const juce::MidiMessage& m);
        void allNotesOff (const juce::String& outId, int channel);
        /** open a MIDI input and get every message on a background thread */
        bool listen (const juce::String& inId, std::function<void (const juce::MidiMessage&)> cb);
        void stopListening();

        // demo signals for screenshots/tests
        void addDemoSources();
        void runDemo (double seconds);

        link::Server& linkServer() { return *server; }
        int linkCount() const;

    private:
        void audioDeviceIOCallbackWithContext (const float* const* in, int nin, float* const* out, int nout, int n,
                                               const juce::AudioIODeviceCallbackContext&) override;
        void audioDeviceAboutToStart (juce::AudioIODevice*) override;
        void audioDeviceStopped() override {}
        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;
        void onLinkAudio (int id, const link::Packet&);
        void onLinkLost (int id);
        void notify();

        mutable juce::CriticalSection lock;
        juce::OwnedArray<Source> inputs, extras;
        std::atomic<double> sr { 48000 };
        int jamMins = 5;
        std::unique_ptr<link::Server> server;
        std::vector<float> tmpL, tmpR;

        struct Voice
        {
            const juce::AudioBuffer<float>* buf = nullptr;
            double pos = 0, rate = 48000;   // rate = source rate x pitch ratio
            int lo = 0, hi = 0, token = 0;
            float gain = 1, env = 0, target = 1, fadeIn = 0.7f, fadeOut = 4.0f;
            bool reverse = false, loop = false, active = false;
        };
        static constexpr int kVoices = 32;
        juce::SpinLock playLock;                       // held only for pointer swaps on the message thread
        std::array<Voice, kVoices> voices;
        std::array<SharedAudio, kVoices> holders;      // keep each voice's audio alive; freed off the audio thread
        int nextToken = 1;
        std::atomic<int> focusToken { 0 }, focusPos { -1 }, voiceCount { 0 };
        std::atomic<bool> focusActive { false };
        std::function<void (float*, float*, int, double)> generator;

        std::map<juce::String, std::unique_ptr<juce::MidiOutput>> outs;
        std::unique_ptr<juce::MidiInput> in;
        std::function<void (const juce::MidiMessage&)> inCb;
        juce::CriticalSection midiLock;

        struct Loopback;
        std::unique_ptr<Loopback> loop;
        Source* loopSource = nullptr;
    };
}
