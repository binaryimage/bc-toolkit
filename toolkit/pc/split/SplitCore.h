#pragma once
// The two halves of Split's protocol, kept free of JUCE and COM so they run (and get tested) anywhere.
//   HostCore   lives in the Split Host, inside the real interface's audio callback.
//   ClientCore lives in the driver DLL, one per app that opens "BC Split".
#include "SplitShared.h"
#include "SplitPlatform.h"
#include <thread>
#include <atomic>
#include <string>
#include <vector>

namespace split
{
    // ------------------------------------------------------------ host
    class HostCore
    {
    public:
        ~HostCore();
        /** make the shared region and the client signals; false if the region couldn't be made */
        bool create();
        void destroy();
        Shared* shared() const { return sh; }

        void setState (HostState s, const std::string& error = {});
        /** the interface opened (or reopened): publish its format to the clients */
        void publish (double sampleRate, int bufferSize, const std::vector<std::string>& inNames,
                      const std::vector<std::string>& outNames, int inLatency, int outLatency, const std::string& deviceName);

        /** audio thread: hand the inputs to every client, mix their previous block into the outputs */
        void process (const float* const* in, int numIns, float* const* out, int numOuts, int numFrames);

        /** message thread, a few times a second: frees slots of apps that quit or crashed */
        void housekeeping();
        struct ClientInfo { int index; std::string name; bool running; uint32_t late; };
        std::vector<ClientInfo> clients() const;
        int attachedCount() const;
        /** a client asked for another sample rate (only granted when it's the only app attached); 0 = none */
        double pendingRateRequest (int& clientIndex) const;
        void clearRateRequest();

    private:
        SharedRegion region;
        Shared* sh = nullptr;
        Signal signals[kMaxClients];
    };

    // ------------------------------------------------------------ client
    class ClientCore
    {
    public:
        struct Handler
        {
            virtual ~Handler() = default;
            /** one block: read sh.in[slot], write slotOut->out[slot] (zeroed channels need no writing) */
            virtual void block (int64_t blockNumber, int slot) = 0;
            /** rate, buffer size or channels changed on the host: the app should reset */
            virtual void configChanged() = 0;
            /** the host went away */
            virtual void hostLost() = 0;
        };

        ~ClientCore();
        /** attach to a running host; if none is running and hostExe is set, start it and wait up to timeoutMs */
        bool connect (const std::string& hostExe, int timeoutMs, std::string& error, bool allowSameProcess = false);
        void disconnect();
        Shared* shared() const { return sh; }
        bool isConnected() const { return sh != nullptr; }

        /** take a slot in the host's mixer (name shows in the Split window) */
        bool attach (const std::string& appName, Handler* h, std::string& error);
        void detach();
        ClientSlot* slot() const { return mine; }
        int slotIndex() const { return index; }

        void setRunning (bool shouldRun);
        void setOutChannels (int n) { if (mine) mine->outChannels.store ((uint32_t) n); }

        /** ask the host for another sample rate; waits up to timeoutMs for it to switch */
        bool requestRate (double rate, int timeoutMs);

    private:
        void loop();
        SharedRegion region;
        Shared* sh = nullptr;
        ClientSlot* mine = nullptr;
        int index = -1;
        Handler* handler = nullptr;
        Signal tick;
        std::thread worker;
        std::atomic<bool> quit { false }, running { false }, ownRequest { false };
        std::atomic<uint32_t> knownGenShared { 0 };
    };
}
