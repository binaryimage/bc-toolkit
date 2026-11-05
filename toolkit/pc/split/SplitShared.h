#pragma once
// BC Split: the memory the host (which owns the real interface) shares with every app using the
// "BC Split" ASIO driver. Fixed-size types only: 32-bit (Rocksmith) and 64-bit (Ableton) clients
// map the same bytes.
#include <atomic>
#include <cstdint>
#include <cstddef>

namespace split
{
    constexpr uint32_t kMagic = 0x31505342;      // "BSP1"
    constexpr uint32_t kVersion = 1;
    constexpr int kMaxCh = 32;                  // channels each way
    constexpr int kMaxFrames = 2048;            // largest buffer size
    constexpr int kSlots = 4;                   // ring of blocks
    constexpr int kMaxClients = 8;
    /** apps get this many buffers to answer before their output is due (2 = room for uneven USB callbacks).
        The Split window's "Safety" setting can raise it to kMaxOutputDelayBlocks; Shared::outputDelay holds the live value. */
    constexpr int kOutputDelayBlocks = 2;
    constexpr int kMaxOutputDelayBlocks = kSlots - 1;   // the ring must keep the block being mixed apart from the one being written
    constexpr const char* kRegionName = "binaryimageSplit.v1";
    constexpr const char* kDriverName = "BC Split";
    constexpr const char* kHostExe = "BC Split Host.exe";
    constexpr const char* kClsid = "{6F3B8C2E-1D47-4A9B-9E3C-5B2A7D81F4C6}";

    enum HostState : uint32_t { hostNone = 0, hostStarting = 1, hostRunning = 2, hostError = 3, hostReconfiguring = 4 };
    enum ClientState : uint32_t { clientFree = 0, clientAttached = 1, clientRunning = 2 };

    struct alignas (64) ClientSlot
    {
        std::atomic<uint32_t> state;
        std::atomic<uint32_t> pid;
        std::atomic<uint32_t> heartbeat;     // ms tick, bumped by the client's thread
        std::atomic<uint32_t> outChannels;   // highest output channel it writes + 1
        std::atomic<int64_t> outSeq;         // last block whose output is complete
        std::atomic<uint32_t> late;          // blocks the host had to skip because this client was late
        uint32_t pad0;
        char name[64];
        float out[kSlots][kMaxCh][kMaxFrames];
    };

    struct alignas (64) Shared
    {
        uint32_t magic, version;
        std::atomic<uint32_t> hostPid;
        std::atomic<uint32_t> hostState;
        std::atomic<uint32_t> configGen;     // bumps when rate / buffer / channels change
        std::atomic<uint32_t> rateRequestClient;   // client index + 1 asking for requestedRate
        int32_t bufferSize, numIns, numOuts, inLatency, outLatency;
        int32_t outputDelay;                 // buffers apps get before their output is due (was padding: 0 = kOutputDelayBlocks)
        double sampleRate;
        double requestedRate;
        char deviceName[64];
        char error[192];
        char inNames[kMaxCh][32];
        char outNames[kMaxCh][32];
        std::atomic<int64_t> seq;            // blocks completed since the host started
        int64_t blockTimeNs[kSlots];         // system time of each block in the ring
        float inPeak[kMaxCh];                // meters for the host window and Toolkit
        float outPeak[kMaxCh];
        float in[kSlots][kMaxCh][kMaxFrames];
        ClientSlot clients[kMaxClients];
    };

    /** the live output delay; an older host left the field at 0, which means the default */
    inline int outputDelayOf (const Shared* s)
    {
        const int d = s != nullptr ? (int) s->outputDelay : 0;
        return d < kOutputDelayBlocks ? kOutputDelayBlocks : d > kMaxOutputDelayBlocks ? kMaxOutputDelayBlocks : d;
    }

    static_assert (std::atomic<int64_t>::is_always_lock_free, "64-bit atomics must be lock-free to share across processes");
    static_assert (sizeof (ClientSlot) % 64 == 0, "client slots stay cache-line aligned");
}

// The layout must be byte-identical in 32- and 64-bit builds (checked against the x64 layout).
static_assert (offsetof (split::Shared, in) == 2664, "Shared layout drifted between builds");
static_assert (offsetof (split::Shared, clients) == 1051264, "Shared layout drifted between builds");
static_assert (sizeof (split::Shared) == 9440896, "Shared layout drifted between builds");
static_assert (offsetof (split::ClientSlot, out) == 96, "ClientSlot layout drifted between builds");
