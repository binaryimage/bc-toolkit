#include "Files.h"

namespace tk
{
juce::File musicFolder() { return juce::File::getSpecialLocation (juce::File::userMusicDirectory); }

juce::File appDataFolder()
{
    auto f = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("binaryimage Toolkit");
    f.createDirectory();
    return f;
}

juce::File mpcDrive()
{
   #if JUCE_WINDOWS
    juce::File k ("K:\\");
    if (k.isDirectory()) return k;
   #endif
    return {};
}

juce::Array<juce::File> mpcExportFolders (const juce::String& sub)
{
    juce::Array<juce::File> out;
    auto k = mpcDrive();
    if (k.isDirectory()) out.add (k.getChildFile (sub));
    out.add (musicFolder().getChildFile ("MPC Songs").getChildFile (sub));
    return out;
}

juce::String safeName (const juce::String& s)
{
    auto t = s.trim().replaceCharacters ("\\/:*?\"<>|", "_________");
    return t.isEmpty() ? juce::String ("Untitled") : t;
}

juce::String stamp() { return juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M-%S"); }

juce::AudioFormatManager& formats()
{
    static juce::AudioFormatManager m;
    static bool init = false;
    if (! init) { m.registerBasicFormats(); init = true; }   // WAV, AIFF, FLAC, Ogg, MP3 (+ Windows Media on Windows)
    return m;
}

bool writeWav (const juce::File& f, const juce::AudioBuffer<float>& b, double sr, int bits)
{
    f.getParentDirectory().createDirectory();
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> os (f.createOutputStream());
    if (os == nullptr) return false;
    auto opts = juce::AudioFormatWriterOptions().withSampleRate (sr).withNumChannels (b.getNumChannels()).withBitsPerSample (bits);
    auto w = wav.createWriterFor (os, opts);
    if (w == nullptr) return false;
    return w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}

bool readAudio (const juce::File& f, juce::AudioBuffer<float>& out, double& sr, double maxSeconds)
{
    std::unique_ptr<juce::AudioFormatReader> r (formats().createReaderFor (f));
    if (r == nullptr) return false;
    sr = r->sampleRate;
    auto len = (int) juce::jmin ((juce::int64) (maxSeconds * sr), r->lengthInSamples);
    out.setSize ((int) juce::jlimit (1u, 2u, r->numChannels), len);
    r->read (&out, 0, len, 0, true, out.getNumChannels() > 1);
    return true;
}

// ------------------------------------------------------------ MIDI file

bool writeMidiFile (const juce::File& f, const std::vector<std::pair<juce::String, std::vector<NoteEvent>>>& tracks, double bpm)
{
    juce::MidiFile mf;
    const int ppq = 480;
    mf.setTicksPerQuarterNote (ppq);
    juce::MidiMessageSequence tempo;
    tempo.addEvent (juce::MidiMessage::tempoMetaEvent ((int) (60000000.0 / bpm)), 0);
    tempo.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4), 0);
    mf.addTrack (tempo);
    for (auto& [name, notes] : tracks)
    {
        juce::MidiMessageSequence seq;
        seq.addEvent (juce::MidiMessage::textMetaEvent (3, name), 0);
        for (auto& n : notes)
        {
            double t0 = n.beat * ppq, t1 = (n.beat + n.lengthBeats) * ppq;
            seq.addEvent (juce::MidiMessage::noteOn (n.channel, n.note, (juce::uint8) juce::jlimit (1, 127, n.velocity)), t0);
            seq.addEvent (juce::MidiMessage::noteOff (n.channel, n.note), t1);
        }
        seq.updateMatchedPairs();
        mf.addTrack (seq);
    }
    f.getParentDirectory().createDirectory();
    f.deleteFile();
    juce::FileOutputStream os (f);
    return os.openedOk() && mf.writeTo (os, 1);
}

// ------------------------------------------------------------ SFZ

bool writeSfz (const juce::File& f, const std::vector<ZoneSample>& zones)
{
    juce::String s = "// made by BC Toolkit\n<control>\ndefault_path=./\n\n<group>\nampeg_release=0.4\n\n";
    for (auto& z : zones)
        s << "<region> sample=" << z.file << " pitch_keycenter=" << z.root << " lokey=" << z.lo << " hikey=" << z.hi
          << " lovel=" << z.velLo << " hivel=" << z.velHi << "\n";
    return f.replaceWithText (s);
}

// ------------------------------------------------------------ MPC programs (see writeXpm* notes in Xpm section)

static juce::String xmlEsc (const juce::String& s) { return s.replace ("&", "&amp;").replace ("<", "&lt;").replace (">", "&gt;").replace ("\"", "&quot;"); }

static juce::String xpmHeader()
{
    return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n\n<MPCVObject>\n  <Version>\n    <File_Version>2.1</File_Version>\n"
           "    <Application>MPC-V</Application>\n    <Application_Version>2.11.0.0</Application_Version>\n"
           "    <Platform>Windows</Platform>\n  </Version>\n";
}

static juce::String xpmLayer (int number, const juce::String& sample, int root, int velLo, int velHi, bool keyTrack = true)
{
    juce::String s;
    s << "            <Layer number=\"" << number << "\">\n"
      << "              <Active>" << (sample.isEmpty() ? "False" : "True") << "</Active>\n"
      << "              <Volume>1.000000</Volume>\n"
      << "              <Pan>0.500000</Pan>\n"
      << "              <Pitch>0.000000</Pitch>\n"
      << "              <TuneCoarse>0</TuneCoarse>\n"
      << "              <TuneFine>0</TuneFine>\n"
      << "              <VelStart>" << velLo << "</VelStart>\n"
      << "              <VelEnd>" << velHi << "</VelEnd>\n"
      << "              <SampleStart>0</SampleStart>\n"
      << "              <SampleEnd>0</SampleEnd>\n"
      << "              <Loop>False</Loop>\n"
      << "              <Direction>0</Direction>\n"
      << "              <Offset>0</Offset>\n"
      << "              <Slice>0</Slice>\n"
      << "              <SampleName>" << xmlEsc (sample) << "</SampleName>\n"
      << "              <SampleFile></SampleFile>\n"
      << "              <RootNote>" << root << "</RootNote>\n"
      << "              <KeyTrack>" << (keyTrack ? "True" : "False") << "</KeyTrack>\n"
      << "            </Layer>\n";
    return s;
}

bool writeXpmKeygroup (const juce::File& f, const juce::String& name, const std::vector<ZoneSample>& zones)
{
    // group velocity layers that share a key range into one keygroup
    std::vector<std::vector<ZoneSample>> groups;
    for (auto& z : zones)
    {
        bool placed = false;
        for (auto& g : groups)
            if (g.front().lo == z.lo && g.front().hi == z.hi && g.size() < 4) { g.push_back (z); placed = true; break; }
        if (! placed) groups.push_back ({ z });
    }
    juce::String s = xpmHeader();
    s << "  <Program type=\"Keygroup\">\n"
      << "    <ProgramName>" << xmlEsc (name) << "</ProgramName>\n"
      << "    <KeygroupNumKeygroups>" << (int) groups.size() << "</KeygroupNumKeygroups>\n"
      << "    <KeygroupPitchBendRange>0.000000</KeygroupPitchBendRange>\n"
      << "    <KeygroupWheelToLfo>0.000000</KeygroupWheelToLfo>\n"
      << "    <Instruments>\n";
    for (size_t i = 0; i < groups.size(); ++i)
    {
        auto& g = groups[i];
        s << "      <Instrument number=\"" << (int) i + 1 << "\">\n"
          << "        <LowNote>" << g.front().lo << "</LowNote>\n"
          << "        <HighNote>" << g.front().hi << "</HighNote>\n"
          << "        <IgnoreBaseNote>False</IgnoreBaseNote>\n"
          << "        <TriggerMode>0</TriggerMode>\n"
          << "        <VolumeAttack>0.000000</VolumeAttack>\n"
          << "        <VolumeRelease>0.050000</VolumeRelease>\n"
          << "        <Layers>\n";
        for (size_t l = 0; l < 4; ++l)
        {
            if (l < g.size()) s << xpmLayer ((int) l + 1, juce::File (g[l].file).getFileNameWithoutExtension(), g[l].root + 1, g[l].velLo, g[l].velHi);
            else s << xpmLayer ((int) l + 1, {}, 61, 0, 127);
        }
        s << "        </Layers>\n      </Instrument>\n";
    }
    s << "    </Instruments>\n  </Program>\n</MPCVObject>\n";
    return f.replaceWithText (s);
}

bool writeXpmDrum (const juce::File& f, const juce::String& name, const juce::StringArray& samples)
{
    juce::String s = xpmHeader();
    s << "  <Program type=\"Drum\">\n"
      << "    <ProgramName>" << xmlEsc (name) << "</ProgramName>\n"
      << "    <Instruments>\n";
    for (int i = 0; i < 128; ++i)
    {
        juce::String sample = i < samples.size() ? juce::File (samples[i]).getFileNameWithoutExtension() : juce::String();
        s << "      <Instrument number=\"" << i + 1 << "\">\n"
          << "        <TriggerMode>1</TriggerMode>\n"
          << "        <Layers>\n" << xpmLayer (1, sample, 0, 0, 127, false) << "        </Layers>\n"
          << "      </Instrument>\n";
    }
    s << "    </Instruments>\n    <PadNoteMap>\n";
    for (int p = 0; p < 128; ++p) s << "      <PadNote number=\"" << p + 1 << "\">\n        <Note>" << (p < 16 ? 37 + p : 0) << "</Note>\n      </PadNote>\n";
    s << "    </PadNoteMap>\n  </Program>\n</MPCVObject>\n";
    return f.replaceWithText (s);
}

// ------------------------------------------------------------ SysEx

std::vector<juce::MemoryBlock> splitSysex (const juce::MemoryBlock& raw)
{
    std::vector<juce::MemoryBlock> out;
    auto* d = static_cast<const juce::uint8*> (raw.getData());
    size_t n = raw.getSize(), start = n;
    for (size_t i = 0; i < n; ++i)
    {
        if (d[i] == 0xF0) start = i;
        else if (d[i] == 0xF7 && start < n) { out.emplace_back (d + start, i - start + 1); start = n; }
    }
    return out;
}
}
