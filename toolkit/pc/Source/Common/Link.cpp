#include "Link.h"

namespace tk::link
{
juce::MemoryBlock Packet::encode() const
{
    juce::MemoryOutputStream o;
    auto utf = name.toRawUTF8();
    auto nl = (int) std::strlen (utf);
    o.writeInt ((int) kMagic);
    o.writeInt ((int) std::lround (sampleRate));
    o.writeShort ((short) channels);
    o.writeShort ((short) nl);
    o.writeInt (frames);
    o.write (utf, (size_t) nl);
    o.write (interleaved.data(), interleaved.size() * sizeof (float));
    return o.getMemoryBlock();
}

bool Packet::decode (const juce::MemoryBlock& m, Packet& p)
{
    juce::MemoryInputStream in (m, false);
    if (m.getSize() < 16 || (juce::uint32) in.readInt() != kMagic) return false;
    p.sampleRate = in.readInt();
    p.channels = in.readShort();
    int nl = in.readShort();
    p.frames = in.readInt();
    if (p.channels < 1 || p.channels > 2 || p.frames < 0 || p.frames > 48000 || nl < 0 || nl > 256) return false;
    juce::MemoryBlock nb;
    in.readIntoMemoryBlock (nb, nl);
    p.name = juce::String::fromUTF8 (static_cast<const char*> (nb.getData()), (int) nb.getSize());
    size_t want = (size_t) p.frames * (size_t) p.channels;
    if (in.getNumBytesRemaining() < (juce::int64) (want * sizeof (float))) return false;
    p.interleaved.resize (want);
    in.read (p.interleaved.data(), (int) (want * sizeof (float)));
    return true;
}

// ------------------------------------------------------------ client (plugin)

Client::Client() : juce::Thread ("BC Link"), juce::InterprocessConnection (false, 0x7a4b1c22)
{
    ringL.assign (48000 * 2, 0.0f); ringR.assign (48000 * 2, 0.0f);
    startThread (juce::Thread::Priority::low);
}

Client::~Client()
{
    signalThreadShouldExit();
    disconnect (200);
    stopThread (2000);
}

void Client::prepare (double sampleRate, int channels) { sr = sampleRate; chans = juce::jlimit (1, 2, channels); }

void Client::push (const float* l, const float* r, int n) noexcept
{
    if (! linked.load()) return;                 // nothing listening: costs nothing
    int s1, n1, s2, n2;
    fifo.prepareToWrite (n, s1, n1, s2, n2);
    if (n1 > 0) { std::memcpy (ringL.data() + s1, l, (size_t) n1 * 4); std::memcpy (ringR.data() + s1, r != nullptr ? r : l, (size_t) n1 * 4); }
    if (n2 > 0) { std::memcpy (ringL.data() + s2, l + n1, (size_t) n2 * 4); std::memcpy (ringR.data() + s2, (r != nullptr ? r : l) + n1, (size_t) n2 * 4); }
    fifo.finishedWrite (n1 + n2);
}

void Client::run()
{
    juce::uint32 lastTry = 0;
    while (! threadShouldExit())
    {
        if (! isConnected())
        {
            linked = false;
            auto now = juce::Time::getMillisecondCounter();
            if (now - lastTry > 1500) { lastTry = now; connectToSocket ("127.0.0.1", kPort, 150); }
            wait (100);
            continue;
        }
        int ready = fifo.getNumReady();
        if (ready < 256) { wait (5); continue; }
        Packet p;
        { const juce::ScopedLock sl (nameLock); p.name = name; }
        p.sampleRate = sr.load(); p.channels = chans.load();
        int s1, n1, s2, n2;
        fifo.prepareToRead (juce::jmin (ready, 4096), s1, n1, s2, n2);
        p.frames = n1 + n2;
        p.interleaved.resize ((size_t) p.frames * (size_t) p.channels);
        auto copy = [&] (int start, int count, int off) {
            for (int i = 0; i < count; ++i)
            {
                if (p.channels == 2) { p.interleaved[(size_t) (off + i) * 2] = ringL[(size_t) (start + i)]; p.interleaved[(size_t) (off + i) * 2 + 1] = ringR[(size_t) (start + i)]; }
                else p.interleaved[(size_t) (off + i)] = ringL[(size_t) (start + i)];
            }
        };
        copy (s1, n1, 0); copy (s2, n2, n1);
        fifo.finishedRead (n1 + n2);
        sendMessage (p.encode());
    }
}

// ------------------------------------------------------------ server (app)

struct Server::Conn : public juce::InterprocessConnection
{
    Conn (Server& s, int id) : juce::InterprocessConnection (false, 0x7a4b1c22), server (s), id (id) {}
    ~Conn() override { disconnect(); }
    void connectionMade() override {}
    void connectionLost() override { if (server.onLost) server.onLost (id); }
    void messageReceived (const juce::MemoryBlock& m) override
    {
        Packet p;
        if (Packet::decode (m, p) && server.onAudio) server.onAudio (id, p);
    }
    Server& server; int id;
};

Server::Server (Callback a, Lost l) : onAudio (std::move (a)), onLost (std::move (l)) {}
Server::~Server() { stop(); }

bool Server::start()
{
    if (! running) running = beginWaitingForSocket (kPort, "127.0.0.1");
    return running;
}

void Server::stop()
{
    InterprocessConnectionServer::stop();
    const juce::ScopedLock sl (lock);
    conns.clear();
    running = false;
}

juce::InterprocessConnection* Server::createConnectionObject()
{
    const juce::ScopedLock sl (lock);
    return conns.add (new Conn (*this, nextId++));
}
}
