#pragma once
#include "Common/JuceHeader.h"
#include "Common/Dsp.h"
#include "Common/Link.h"
#include <atomic>
#include <map>

namespace tk
{
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

        // preview playback through the output
        void play (const juce::AudioBuffer<float>& b, double sampleRate, int start = 0, int len = -1);
        void stopPlayback();
        int playPosition() const { return playPos.load(); }
        bool isPlaying() const { return playing.load(); }
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

        juce::SpinLock playLock;
        juce::AudioBuffer<float> playBuf;
        double playSr = 48000, playFrac = 0;
        std::atomic<int> playPos { 0 }, playEnd { 0 };
        std::atomic<bool> playing { false };
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
