#pragma once
#include "JuceHeader.h"
#include <vector>

namespace tk
{
    // ------------------------------------------------------------ places
    juce::File musicFolder();                 // ~/Music (Windows: the user's Music library)
    juce::File appDataFolder();               // %APPDATA%/binaryimage Toolkit
    /** The MPC drive (K: "Purple Idea") if it's plugged in, else an invalid File. */
    juce::File mpcDrive();
    /** Where MPC-ready exports go: K:/<sub> when present, plus a copy under Music/MPC Songs/<sub>. */
    juce::Array<juce::File> mpcExportFolders (const juce::String& sub);
    juce::String safeName (const juce::String& s);
    juce::String stamp();                     // 2026-11-02 14-31-07

    // ------------------------------------------------------------ audio
    bool writeWav (const juce::File& f, const juce::AudioBuffer<float>& b, double sampleRate, int bits = 24);
    bool readAudio (const juce::File& f, juce::AudioBuffer<float>& out, double& sampleRate, double maxSeconds = 600);
    juce::AudioFormatManager& formats();

    // ------------------------------------------------------------ MIDI
    struct NoteEvent { double beat; double lengthBeats; int note; int velocity; int channel = 1; };
    /** A type-1 MIDI file, one track per vector entry. */
    bool writeMidiFile (const juce::File& f, const std::vector<std::pair<juce::String, std::vector<NoteEvent>>>& tracks, double bpm);

    // ------------------------------------------------------------ sampler programs
    struct ZoneSample { juce::String file; int root, lo, hi, velLo = 1, velHi = 127; };
    bool writeSfz (const juce::File& f, const std::vector<ZoneSample>& zones);
    /** MPC keygroup program (.xpm) for multisampled instruments. */
    bool writeXpmKeygroup (const juce::File& f, const juce::String& programName, const std::vector<ZoneSample>& zones);
    /** MPC drum program (.xpm): up to 16 samples on pads A01..A16. */
    bool writeXpmDrum (const juce::File& f, const juce::String& programName, const juce::StringArray& sampleFiles);

    // ------------------------------------------------------------ SysEx
    /** Split raw bytes into complete F0..F7 messages. */
    std::vector<juce::MemoryBlock> splitSysex (const juce::MemoryBlock& raw);
}
