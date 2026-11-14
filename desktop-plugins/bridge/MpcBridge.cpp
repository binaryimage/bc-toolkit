#include "MpcBridge.h"
#include "MpcEditor.h"

// The plugin's own source defines this (MPC_PLUGIN_ENTRY at the end of its .cpp).
extern "C" AEffect* VSTPluginMain (audioMasterCallback host);

namespace bcp
{
// ============================================================ parameters

BridgeParam::BridgeParam (BridgeProcessor& o, int i)
    : juce::AudioProcessorParameterWithID (juce::ParameterID { juce::String (o.plugin().param (i).id), 1 },
                                           juce::String (o.plugin().param (i).name)),
      index (i), owner (o)
{
}

float BridgeParam::getValue() const { return owner.plugin().getNormalised (index); }

void BridgeParam::setValue (float v)
{
    lastReported = v;
    owner.plugin().setNormalised (index, v);
}

float BridgeParam::getDefaultValue() const
{
    const auto& p = owner.plugin().param (index);
    return p.toNormalised (p.def);
}

juce::String BridgeParam::getText (float normalised, int maximumLength) const
{
    auto& pl = owner.plugin();
    const auto& p = pl.param (index);
    const float plain = p.toPlain (normalised);
    std::string s;
    if (! pl.formatValue (index, plain, s)) s = p.display (plain);
    return juce::String (s).substring (0, juce::jmax (1, maximumLength));
}

float BridgeParam::getValueForText (const juce::String& text) const
{
    const auto& p = owner.plugin().param (index);
    if (p.kind == mpc::Param::Kind::Choice)
    {
        for (size_t c = 0; c < p.choices.size(); ++c)
            if (text.trim().equalsIgnoreCase (juce::String (p.choices[c]))) return p.toNormalised ((float) c);
        return getValue();
    }
    if (p.kind == mpc::Param::Kind::Bool)
        return (text.trim().equalsIgnoreCase ("on") || text.getFloatValue() >= 0.5f) ? 1.0f : 0.0f;
    return p.toNormalised (text.getFloatValue());
}

int BridgeParam::getNumSteps() const
{
    const auto& p = owner.plugin().param (index);
    switch (p.kind)
    {
        case mpc::Param::Kind::Bool:   return 2;
        case mpc::Param::Kind::Choice: return juce::jmax (2, (int) p.choices.size());
        case mpc::Param::Kind::Int:    return juce::jmax (2, (int) std::lround (p.max - p.min) + 1);
        default:                       return juce::AudioProcessor::getDefaultNumParameterSteps();
    }
}

bool BridgeParam::isDiscrete() const { return owner.plugin().param (index).kind != mpc::Param::Kind::Float; }
bool BridgeParam::isBoolean() const { return owner.plugin().param (index).kind == mpc::Param::Kind::Bool; }
juce::String BridgeParam::getLabel() const { return juce::String (owner.plugin().param (index).unit); }
bool BridgeParam::isAutomatable() const { return owner.plugin().param (index).automatable; }

juce::StringArray BridgeParam::getAllValueStrings() const
{
    const auto& p = owner.plugin().param (index);
    juce::StringArray s;
    if (p.kind == mpc::Param::Kind::Choice) for (auto& c : p.choices) s.add (c);
    return s;
}

// ============================================================ processor

juce::AudioProcessor::BusesProperties BridgeProcessor::busesFor (bool instrument)
{
    BusesProperties b;
    if (! instrument) b = b.withInput ("Input", juce::AudioChannelSet::stereo(), true);
    return b.withOutput ("Output", juce::AudioChannelSet::stereo(), true);
}

static bool isInstrumentBuild()
{
   #if BCP_IS_SYNTH
    return true;
   #else
    return false;
   #endif
}

BridgeProcessor::BridgeProcessor()
    : AudioProcessor (busesFor (isInstrumentBuild()))
{
    instrument = isInstrumentBuild();
    effect = VSTPluginMain (&BridgeProcessor::hostCallback);
    jassert (effect != nullptr);
    effect->user = this;
    mpcPlugin = static_cast<mpc::Plugin*> (effect->object);
    dispatch (effOpen);

    for (int i = 0; i < mpcPlugin->numParams(); ++i)
    {
        auto* p = new BridgeParam (*this, i);
        p->lastReported = mpcPlugin->getNormalised (i);
        params.push_back (p);
        addParameter (p);
    }
    setLatencySamples (mpcPlugin->info().latencySamples);
    midiEvents.resize (kMaxEvents);
    startTimerHz (20);
}

BridgeProcessor::~BridgeProcessor()
{
    stopTimer();
    dispatch (effMainsChanged, 0, 0);
    dispatch (effClose);   // deletes the plugin and frees the AEffect
    effect = nullptr; mpcPlugin = nullptr;
}

VstIntPtr BridgeProcessor::dispatch (VstInt32 opcode, VstInt32 index, VstIntPtr value, void* ptr, float opt)
{
    return effect != nullptr ? effect->dispatcher (effect, opcode, index, value, ptr, opt) : 0;
}

VstIntPtr BridgeProcessor::hostCallback (AEffect* e, VstInt32 opcode, VstInt32 index, VstIntPtr value, void* ptr, float opt)
{
    juce::ignoreUnused (value, ptr);
    auto* self = e != nullptr ? static_cast<BridgeProcessor*> (e->user) : nullptr;
    switch (opcode)
    {
        case audioMasterVersion: return 2400;
        case audioMasterGetTime: return self != nullptr ? (VstIntPtr) &self->timeInfo : 0;
        case audioMasterAutomate:
            // a parameter moved inside the plugin; the timer passes it on to the host
            juce::ignoreUnused (index, opt);
            return 1;
        case audioMasterIOChanged:
            if (self != nullptr && self->effect != nullptr) self->setLatencySamples (self->effect->initialDelay);
            return 1;
        case audioMasterUpdateDisplay:
            if (self != nullptr) self->updateHostDisplay (juce::AudioProcessorListener::ChangeDetails().withParameterInfoChanged (true));
            return 1;
        default: return 0;
    }
}

const juce::String BridgeProcessor::getName() const { return juce::String (mpcPlugin->info().name); }
bool BridgeProcessor::acceptsMidi() const { return instrument || mpcPlugin->info().wantsMidi; }
double BridgeProcessor::getTailLengthSeconds() const
{
    const int t = mpcPlugin->info().tailSamples;
    return t > 1 ? t / juce::jmax (1.0, getSampleRate()) : (instrument ? 0.0 : 8.0);
}

void BridgeProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    maxBlock = juce::jlimit (32, 8192, samplesPerBlock);
    dispatch (effMainsChanged, 0, 0);
    dispatch (effSetSampleRate, 0, 0, nullptr, (float) sampleRate);
    dispatch (effSetBlockSize, 0, maxBlock);
    dispatch (effMainsChanged, 0, 1);
    timeInfo = {};
    timeInfo.sampleRate = sampleRate;
}

void BridgeProcessor::releaseResources() { dispatch (effMainsChanged, 0, 0); }

bool BridgeProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()) return false;
    if (instrument) return layouts.getMainInputChannelSet().isDisabled();
    const auto in = layouts.getMainInputChannelSet();
    return in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono();
}

void BridgeProcessor::fillTimeInfo()
{
    VstTimeInfo t {};
    t.sampleRate = getSampleRate();
    t.tempo = 120.0; t.timeSigNumerator = 4; t.timeSigDenominator = 4;
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            if (auto bpm = pos->getBpm())            { t.tempo = *bpm; t.flags |= kVstTempoValid; }
            if (auto ppq = pos->getPpqPosition())    { t.ppqPos = *ppq; t.flags |= kVstPpqPosValid; }
            if (auto bar = pos->getPpqPositionOfLastBarStart()) { t.barStartPos = *bar; t.flags |= kVstBarsValid; }
            if (auto ts = pos->getTimeSignature())   { t.timeSigNumerator = ts->numerator; t.timeSigDenominator = ts->denominator; t.flags |= kVstTimeSigValid; }
            if (auto smp = pos->getTimeInSamples())  t.samplePos = (double) *smp;
            if (auto loop = pos->getLoopPoints())    { t.cycleStartPos = loop->ppqStart; t.cycleEndPos = loop->ppqEnd; t.flags |= kVstCyclePosValid; }
            if (pos->getIsPlaying())   t.flags |= kVstTransportPlaying;
            if (pos->getIsRecording()) t.flags |= kVstTransportRecording;
            if (pos->getIsLooping())   t.flags |= kVstTransportCycleActive;
        }
    }
    else t.flags |= kVstTempoValid;
    timeInfo = t;
}

void BridgeProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int total = buffer.getNumSamples();
    const int inCh = getTotalNumInputChannels();
    if (! instrument && inCh == 1 && buffer.getNumChannels() >= 2) buffer.copyFrom (1, 0, buffer, 0, 0, total);   // mono in: feed both sides
    for (int c = 2; c < buffer.getNumChannels(); ++c) buffer.clear (c, 0, total);
    if (buffer.getNumChannels() < 2) { buffer.clear(); return; }

    fillTimeInfo();
    const double samplesPerBeat = timeInfo.sampleRate * 60.0 / juce::jmax (1.0, timeInfo.tempo);

    auto it = midi.cbegin();
    for (int start = 0; start < total;)
    {
        const int n = juce::jmin (maxBlock, total - start);
        // this chunk's MIDI
        int count = 0;
        for (; it != midi.cend() && (*it).samplePosition < start + n; ++it)
        {
            const auto m = (*it).getMessage();
            if (count >= kMaxEvents || m.getRawDataSize() > 3 || m.getRawDataSize() < 1 || m.isSysEx()) continue;
            auto& ev = midiEvents[(size_t) count];
            ev = {};
            ev.type = kVstMidiType;
            ev.byteSize = (VstInt32) sizeof (VstMidiEvent);
            ev.deltaFrames = juce::jmax (0, (*it).samplePosition - start);
            const auto* raw = m.getRawData();
            for (int b = 0; b < juce::jmin (3, m.getRawDataSize()); ++b) ev.midiData[b] = (char) raw[b];
            eventBlock.events[count] = reinterpret_cast<VstEvent*> (&ev);
            ++count;
        }
        if (count > 0)
        {
            eventBlock.numEvents = count;
            eventBlock.reserved = 0;
            dispatch (effProcessEvents, 0, 0, &eventBlock);
        }

        float* chans[2] = { buffer.getWritePointer (0, start), buffer.getWritePointer (1, start) };
        effect->processReplacing (effect, instrument ? nullptr : chans, chans, (VstInt32) n);

        start += n;
        if (start < total)
        {
            // move the clock on for the next chunk
            timeInfo.samplePos += n;
            if (timeInfo.flags & kVstPpqPosValid) timeInfo.ppqPos += n / samplesPerBeat;
        }
    }
    midi.clear();

    // never hand the host NaN or wild values
    for (int c = 0; c < 2; ++c)
    {
        auto* d = buffer.getWritePointer (c);
        for (int i = 0; i < total; ++i)
            if (! std::isfinite (d[i])) d[i] = 0.0f;
            else d[i] = juce::jlimit (-8.0f, 8.0f, d[i]);
    }
}

juce::String BridgeProcessor::valueText (int index) const
{
    std::string s = mpcPlugin->displayText (index);
    juce::String t (s);
    const auto& p = mpcPlugin->param (index);
    if (! p.unit.empty() && p.kind == mpc::Param::Kind::Float && ! t.endsWithIgnoreCase (juce::String (p.unit)))
        t << " " << juce::String (p.unit);
    return t;
}

void BridgeProcessor::setFromUi (int index, float normalised, bool beginGesture, bool endGesture)
{
    auto* p = params[(size_t) index];
    normalised = juce::jlimit (0.0f, 1.0f, normalised);
    if (beginGesture) p->beginChangeGesture();
    if (std::abs (p->getValue() - normalised) > 1.0e-7f) p->setValueNotifyingHost (normalised);
    if (endGesture) p->endChangeGesture();
}

void BridgeProcessor::timerCallback()
{
    // changes made inside the plugin (a preset, a MIDI CC, a button that resets itself) go to the host here
    for (auto* p : params)
    {
        const float v = mpcPlugin->getNormalised (p->index);
        if (std::abs (v - p->lastReported.load()) > 1.0e-6f)
        {
            p->lastReported = v;
            p->sendValueChangedMessageToListeners (v);
        }
    }
}

int BridgeProcessor::getNumPrograms() { return juce::jmax (1, mpcPlugin->numPrograms()); }
int BridgeProcessor::getCurrentProgram() { return mpcPlugin->currentProgram(); }
void BridgeProcessor::setCurrentProgram (int index)
{
    if (index < 0 || index >= mpcPlugin->numPrograms()) return;
    mpcPlugin->setProgram (index);
    ++uiProgramVersion;
    if (juce::MessageManager::existsAndIsCurrentThread()) timerCallback();
    updateHostDisplay (juce::AudioProcessorListener::ChangeDetails().withProgramChanged (true));
}
const juce::String BridgeProcessor::getProgramName (int index) { return juce::String (mpcPlugin->programName (index)); }

void BridgeProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    // "BCP1" + program index + the SDK's own state chunk (the same bytes the MPC saves)
    const auto& chunk = mpcPlugin->internalSaveState();
    juce::MemoryOutputStream out (dest, false);
    out.writeInt (0x31504342);   // "BCP1"
    out.writeInt (mpcPlugin->currentProgram());
    out.writeInt ((int) chunk.size());
    if (! chunk.empty()) out.write (chunk.data(), chunk.size());
}

void BridgeProcessor::setStateInformation (const void* data, int size)
{
    juce::MemoryInputStream in (data, (size_t) juce::jmax (0, size), false);
    if (size < 12 || in.readInt() != 0x31504342) return;
    const int program = in.readInt();
    const int len = in.readInt();
    if (len <= 0 || len > size - 12) return;
    std::vector<uint8_t> chunk ((size_t) len);
    in.read (chunk.data(), len);
    if (program >= 0 && program < mpcPlugin->numPrograms()) mpcPlugin->setProgram (program);   // keeps the name showing
    mpcPlugin->internalLoadState (chunk.data(), chunk.size());
    ++uiProgramVersion;
    if (juce::MessageManager::existsAndIsCurrentThread()) timerCallback();
}

juce::AudioProcessorEditor* BridgeProcessor::createEditor() { return new BridgeEditor (*this); }
}

// JUCE's plugin entry
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new bcp::BridgeProcessor(); }
