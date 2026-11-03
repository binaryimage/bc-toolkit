#pragma once
// Windows "what you hear" capture (WASAPI loopback on the default output). Kept free of JUCE headers.
namespace tkwin
{
    using LoopbackCallback = void (*) (void* ctx, const float* left, const float* right, int frames, int sampleRate);
    /** Starts a capture thread. Returns a handle, or nullptr if the default output can't be opened. */
    void* loopbackStart (LoopbackCallback cb, void* ctx);
    void loopbackStop (void* handle);
    bool loopbackSupported();
}
