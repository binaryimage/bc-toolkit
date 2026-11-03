#include "LinkProcessor.h"
#include "LinkEditor.h"

LinkProcessor::LinkProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
}

bool LinkProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
    return in == out && (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo());
}

void LinkProcessor::prepareToPlay (double s, int)
{
    sr = s;
    client.prepare (s, getMainBusNumInputChannels());
    client.setName (displayName());
}

void LinkProcessor::processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals nd;
    const int n = b.getNumSamples();
    const float* l = b.getReadPointer (0);
    const float* r = b.getNumChannels() > 1 ? b.getReadPointer (1) : nullptr;
    client.push (l, r, n);   // audio itself is left exactly as it came in
    float pk = 0;
    int wp = w.load();
    for (int i = 0; i < n; ++i)
    {
        float v = r != nullptr ? 0.5f * (l[i] + r[i]) : l[i];
        pk = juce::jmax (pk, std::abs (l[i]), r != nullptr ? std::abs (r[i]) : 0.0f);
        ring[(size_t) wp] = v;
        wp = (wp + 1) & (R - 1);
    }
    w = wp;
    peak = juce::jmax (pk, peak.load() * 0.9f);
}

void LinkProcessor::copyRecent (float* dest, int n) const
{
    n = juce::jmin (n, R);
    int wp = w.load();
    for (int i = 0; i < n; ++i) dest[i] = ring[(size_t) ((wp - n + i) & (R - 1))];
}

juce::String LinkProcessor::displayName() const
{
    const juce::ScopedLock sl (nameLock);
    if (custom.isNotEmpty()) return custom;
    if (trackName.isNotEmpty()) return trackName;
    return "Ableton track";
}

void LinkProcessor::setCustomName (const juce::String& s)
{
    { const juce::ScopedLock sl (nameLock); custom = s.trim(); }
    client.setName (displayName());
}

void LinkProcessor::updateTrackProperties (const TrackProperties& p)
{
    { const juce::ScopedLock sl (nameLock); trackName = p.name.value_or (juce::String()); }
    client.setName (displayName());
}

void LinkProcessor::getStateInformation (juce::MemoryBlock& m)
{
    juce::ValueTree t ("ToolkitLink");
    t.setProperty ("name", customName(), nullptr);
    juce::MemoryOutputStream os (m, false);
    t.writeToStream (os);
}

void LinkProcessor::setStateInformation (const void* d, int size)
{
    auto t = juce::ValueTree::readFromData (d, (size_t) size);
    if (t.isValid()) setCustomName (t["name"].toString());
}

juce::AudioProcessorEditor* LinkProcessor::createEditor() { return new LinkEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new LinkProcessor(); }
