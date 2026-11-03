#pragma once
#include "JuceHeader.h"
#include <functional>

/** BC Link: the VST3 streams a track's audio to the standalone app over localhost. */
namespace tk::link
{
    constexpr int kPort = 53317;
    constexpr juce::uint32 kMagic = 0x544b4c31; // "TKL1"

    struct Packet
    {
        juce::String name;
        double sampleRate = 48000;
        int channels = 2, frames = 0;
        std::vector<float> interleaved;
        juce::MemoryBlock encode() const;
        static bool decode (const juce::MemoryBlock&, Packet&);
    };

    /** Plugin side: audio thread pushes, a background thread connects and sends. */
    class Client : private juce::Thread, private juce::InterprocessConnection
    {
    public:
        Client();
        ~Client() override;
        void setName (const juce::String& n) { const juce::ScopedLock sl (nameLock); name = n; }
        void prepare (double sampleRate, int channels);
        void push (const float* l, const float* r, int n) noexcept;   // audio thread
        bool isLinked() const noexcept { return linked.load(); }
    private:
        void run() override;
        void connectionMade() override { linked = true; }
        void connectionLost() override { linked = false; }
        void messageReceived (const juce::MemoryBlock&) override {}
        juce::AbstractFifo fifo { 48000 * 2 };
        std::vector<float> ringL, ringR;
        std::atomic<double> sr { 48000 };
        std::atomic<int> chans { 2 };
        std::atomic<bool> linked { false };
        juce::CriticalSection nameLock;
        juce::String name { "Ableton track" };
    };

    /** App side: accepts plugin connections and hands each packet to onAudio (connection thread). */
    class Server : private juce::InterprocessConnectionServer
    {
    public:
        using Callback = std::function<void (int connectionId, const Packet&)>;
        using Lost = std::function<void (int connectionId)>;
        Server (Callback onAudio, Lost onLost);
        ~Server() override;
        bool start();
        void stop();
        bool isRunning() const { return running; }
    private:
        juce::InterprocessConnection* createConnectionObject() override;
        struct Conn;
        Callback onAudio; Lost onLost;
        juce::OwnedArray<juce::InterprocessConnection> conns;
        juce::CriticalSection lock;
        std::atomic<int> nextId { 1 };
        bool running = false;
    };
}
