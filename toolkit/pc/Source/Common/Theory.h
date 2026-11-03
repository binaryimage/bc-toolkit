#pragma once
#include "JuceHeader.h"
#include "Files.h"
#include <vector>

namespace tk::theory
{
    struct Scale { const char* name; std::vector<int> steps; };
    const std::vector<Scale>& scales();          // dark ones first
    std::vector<int> chordInScale (int rootPc, const Scale&, int degree, int octave, bool seventh);

    struct RiffOptions
    {
        int root = 2;            // D
        int scale = 0;           // index into scales()
        int bars = 4;
        double bpm = 118;
        int bassStyle = 0;       // 0 EBM 16ths, 1 pulse 8ths, 2 long notes, 3 boom-bap
        bool sevenths = false;
        unsigned seed = 1;
    };
    struct Riff
    {
        std::vector<int> degrees;                      // chord degree per bar
        juce::StringArray chordNames;
        std::vector<NoteEvent> chords, bass, lead;
        double bpm = 118; int bars = 4;
    };
    Riff makeRiff (const RiffOptions&);
}
