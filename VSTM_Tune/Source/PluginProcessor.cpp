#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Parameters.h"

using namespace vstm;

VSTMTuneProcessor::VSTMTuneProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "VSTMTune", params::createLayout())
{
    bypassParam = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter (ids::bypass));
    auto raw = [this] (const juce::String& id)
    {
        auto* prm = apvts.getParameter (id);
        return RawParam { apvts.getRawParameterValue (id), prm->convertFrom0to1 (prm->getDefaultValue()) };
    };
    pKey        = raw (ids::key);
    pScale      = raw (ids::scale);
    pCorrection = raw (ids::correction);
    pRetune     = raw (ids::retune);
    pHumanize   = raw (ids::humanize);
    pRobotism   = raw (ids::robotism);
    pVibrato    = raw (ids::vibrato);
    pFormant    = raw (ids::formant);
    pTranspose  = raw (ids::transpose);
    pMix        = raw (ids::mix);
    pBypass     = raw (ids::bypass);
    pRealtime   = raw (ids::realtime);
    pRange      = raw (ids::range);
    pCustomScale = raw (ids::customScale);
    pAdaptive   = raw (ids::adaptive);
    pTuningRef  = raw (ids::tuningRef);
    pProtectCons = raw (ids::protectCons);
    pFormantFollow = raw (ids::formantFollow);
    pTransition = raw (ids::transition);
    pTolerance = raw (ids::tolerance);
    pConfidence = raw (ids::confidence);
    pSynthVib = raw (ids::synthVib);
    pOctave = raw (ids::octave);
    pMidiTarget = raw (ids::midiTarget);
    pMidiOut = raw (ids::midiOut);
    pMidiOctave = raw (ids::midiOctave);
    pTuningSys = raw (ids::tuningSys);
    pScaleTr = raw (ids::scaleTr);
    pAttack = raw (ids::attack);
    pRelease = raw (ids::release);
    pAutoChar = raw (ids::autoCharacter);
    for (int i = 0; i < 12; ++i)
        pScaleNote[i] = raw (ids::scaleNote (i));
    for (int i = 0; i < 12; ++i)
        pScaleBypass[i] = raw (ids::scaleBypass (i));
    for (int i = 0; i < 12; ++i)
        pTuneCents[i] = raw (ids::tuneCents (i));

    apvts.addParameterListener (ids::realtime, this);
    apvts.addParameterListener (ids::autoCharacter, this);
    apvts.state.setProperty ("presetName", presets.getCurrentName(), nullptr);

    engine.prepare (currentRate, true);
    setLatencySamples (engine.getLatencySamples());
}

VSTMTuneProcessor::~VSTMTuneProcessor()
{
    apvts.removeParameterListener (ids::realtime, this);
    apvts.removeParameterListener (ids::autoCharacter, this);
}

bool VSTMTuneProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return in == out;
}

vstm::TuneEngine::Params VSTMTuneProcessor::readParams() const noexcept
{
    TuneEngine::Params p;
    p.key = juce::jlimit (-1, 11, juce::roundToInt (pKey.get()) - 1);
    p.scale = juce::jlimit (0, (int) kNumScales - 1, juce::roundToInt (pScale.get()));
    p.amount = pCorrection.get() * 0.01f;
    p.retuneMs = pRetune.get();
    p.humanize = pHumanize.get() * 0.01f;
    p.robotism = pRobotism.get() * 0.01f;
    p.vibratoScale = pVibrato.get() * 0.01f;
    if (pAutoChar.get() > 0.5f)
    {
        // 1.6 Auto Character: ROBOTISM drives the managed controls (their manual values wait, untouched). The values
        // go through each parameter's own range (its step), exactly as a hand-over stores them: at the default
        // ROBOTISM this is the factory default bit for bit, and switching off continues bit for bit.
        const auto c = vstm::params::characterFor (pRobotism.get());
        auto snap = [this] (const juce::String& id, float plain)
        {
            auto* prm = apvts.getParameter (id);
            if (prm == nullptr)
                return plain;
            const float n = prm->convertTo0to1 (plain);
            // the parameter's default is held unsnapped by the host state (APVTS): the same representation here
            return std::abs (n - prm->getDefaultValue()) < 1.0e-9f ? prm->getNormalisableRange().convertFrom0to1 (n)
                                                                  : prm->convertFrom0to1 (n);
        };
        p.retuneMs = snap (ids::retune, c.retuneMs);
        p.humanize = snap (ids::humanize, c.humanizePct) * 0.01f;
        p.vibratoScale = snap (ids::vibrato, c.vibratoPct) * 0.01f;
    }
    p.formant = pFormant.get();
    p.transpose = std::round (pTranspose.get());
    p.mix = pMix.get() * 0.01f;
    p.active = pBypass.get() < 0.5f;
    p.range = juce::jlimit (0, 2, juce::roundToInt (pRange.get()));
    p.customScale = pCustomScale.get() > 0.5f;
    uint16_t mask = 0;
    for (int i = 0; i < 12; ++i)
        if (pScaleNote[i].get() > 0.5f)
            mask = (uint16_t) (mask | (1u << i));
    p.customMask = mask;
    uint16_t bypass = 0;
    for (int i = 0; i < 12; ++i)
        if (pScaleBypass[i].get() > 0.5f)
            bypass = (uint16_t) (bypass | (1u << i));
    p.bypassMask = bypass;
    {
        // 1.5 (roadmap 45): cents per degree of the tuning system
        using namespace vstm::params;
        const int sys = juce::jlimit (0, (int) kNumTunings - 1, juce::roundToInt (pTuningSys.get()));
        for (int d = 0; d < 12; ++d)
            p.degreeCents[d] = sys == kCustomTuning ? pTuneCents[d].get() : kTuningCents[sys][d];
    }
    {
        using namespace vstm::params;
        auto pick = [] (float v, int n) { return juce::jlimit (0, n - 1, juce::roundToInt (v)); };
        p.toleranceCt = kToleranceCt[pick (pTolerance.get(), (int) std::size (kToleranceCt))];
        p.confidenceMin = kConfidence[pick (pConfidence.get(), (int) std::size (kConfidence))];
        const int sv = pick (pSynthVib.get(), (int) std::size (kSynthVibRate));
        p.synthVibRateHz = kSynthVibRate[sv];
        p.synthVibDepthCt = kSynthVibDepth[sv];
        p.octave = pick (pOctave.get(), 5) - 2;
        p.scaleSteps = pick (pScaleTr.get(), 15) - 7;
        p.attackMs = kAttackMs[pick (pAttack.get(), (int) std::size (kAttackMs))];
        p.releaseMs = kReleaseMs[pick (pRelease.get(), (int) std::size (kReleaseMs))];
    }
    p.adaptive = pAdaptive.get() > 0.5f;
    p.tuningRefHz = juce::jlimit (415.0f, 466.0f, pTuningRef.get());
    p.protectConsonants = pProtectCons.get() > 0.5f;
    p.formantFollowsTranspose = pFormantFollow.get() > 0.5f;
    p.transitionMs = vstm::params::kTransitionMs[juce::jlimit (0, (int) std::size (vstm::params::kTransitionMs) - 1,
                                                               juce::roundToInt (pTransition.get()))];
    p.followKeyChanges = followKeyChanges.load (std::memory_order_relaxed);
    p.peakGuard = getExpert (Expert::peakGuard);
    p.backdateSwitch = getExpert (Expert::backdateSwitch);
    p.freshAnalysis = getExpert (Expert::freshAnalysis);
    p.swingKeep = getExpert (Expert::swingKeep);
    return p;
}

bool VSTMTuneProcessor::setExpert (Expert e, bool on)
{
    if (! auth.isAdmin())
        return false;
    expert[(int) e].store (on, std::memory_order_relaxed);
    return true;
}

void VSTMTuneProcessor::resetExpert() noexcept
{
    for (auto& x : expert)
        x.store (true, std::memory_order_relaxed);
}

void VSTMTuneProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentRate = sampleRate;
    doubleScratch.setSize (juce::jmax (2, getTotalNumOutputChannels()), juce::jmax (64, samplesPerBlock), false, true, false);
    midiChunk.ensureSize (4096);
    midiChunkOut.ensureSize (4096);
    voiceNote = -1;          // the engine starts over; a note still sounding gets its note-off in the next block
    engine.setParams (readParams());
    engine.prepare (sampleRate, pRealtime.get() > 0.5f);
    setLatencySamples (engine.getLatencySamples());
}

void VSTMTuneProcessor::parameterChanged (const juce::String& id, float newValue)
{
    // The latency mode is not automatable, so this arrives from the UI or a state restore.
    if (id == ids::realtime)
        setLatencySamples (TuneEngine::latencyFor (currentRate, newValue > 0.5f));
    // 1.6: Auto Character switched off by the host (automation) or the UI: the auto values become the manual ones,
    // unless a session or a preset is setting every value anyway (its own manual values must win)
    if (id == ids::autoCharacter && newValue < 0.5f && ! restoringState.load() && ! presets.isApplying())
        handOffCharacter();
}

void VSTMTuneProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const int numIn = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    for (int ch = numIn; ch < numOut; ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    // 1.5 (roadmap 47 / 48): MIDI CC -> parameters (assigned by MIDI Learn), before the parameters are read
    for (const auto meta : midi)
        midiLearn.handle (meta.getMessage());

    auto params = readParams();
    const bool midiMode = pMidiTarget.get() > 0.5f;
    auto heldMask = [this]
    {
        uint16_t m = 0;
        for (int n = 0; n < 128; ++n)
            if (midiHeld[(size_t) n] > 0)
                m = (uint16_t) (m | (1u << (n % 12)));
        return m;
    };
    const bool perform = midiMode && pMidiOctave.get() > 0.5f;
    auto lastHeld = [this] { return heldCount > 0 ? (int) heldOrder[(size_t) heldCount - 1] : -1; };
    params.midiMask = midiMode ? heldMask() : (uint16_t) 0;
    params.midiNote = perform ? lastHeld() : -1;
    engine.setParams (params);
    const bool rt = pRealtime.get() > 0.5f;
    if (engine.isRealtime() != rt)
        engine.setRealtime (rt);

    const int nc = juce::jmin (2, buffer.getNumChannels());
    if (nc == 0)
    {
        midi.clear();
        return;
    }
    const int64_t blockStart = engine.getInputSampleCount();
    float* chans[2] = { buffer.getWritePointer (0), nc > 1 ? buffer.getWritePointer (1) : nullptr };

    // 1.5 MIDI target notes: the block is processed in pieces between the MIDI events, so a note takes effect
    // where it was played (the engine is block-size independent: same output for any split). Held notes are
    // tracked even when the mode is off, so switching it on mid-note works.
    int done = 0;
    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        const int at = juce::jlimit (done, buffer.getNumSamples(), meta.samplePosition);
        bool changed = false;
        auto dropHeld = [this] (int note)
        {
            for (int i = 0; i < heldCount; ++i)
                if (heldOrder[(size_t) i] == note)
                {
                    for (int j = i; j + 1 < heldCount; ++j)
                        heldOrder[(size_t) j] = heldOrder[(size_t) j + 1];
                    --heldCount;
                    return;
                }
        };
        if (msg.isNoteOn())
        {
            auto& c = midiHeld[(size_t) msg.getNoteNumber()];
            c = (uint8_t) juce::jmin (255, c + 1);
            dropHeld (msg.getNoteNumber());                      // the newest press is the perform note
            if (heldCount < 128)
                heldOrder[(size_t) heldCount++] = (int8_t) msg.getNoteNumber();
            changed = true;
        }
        else if (msg.isNoteOff())
        {
            auto& c = midiHeld[(size_t) msg.getNoteNumber()];
            c = (uint8_t) (c > 0 ? c - 1 : 0);
            if (c == 0)
                dropHeld (msg.getNoteNumber());
            changed = true;
        }
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
        {
            midiHeld.fill (0);
            heldCount = 0;
            changed = true;
        }
        if (changed && midiMode)
        {
            if (at > done)
            {
                float* part[2] = { chans[0] + done, chans[1] != nullptr ? chans[1] + done : nullptr };
                engine.process (part, nc, at - done);
                done = at;
            }
            params.midiMask = heldMask();
            params.midiNote = perform ? lastHeld() : -1;
            engine.setParams (params);
        }
    }
    if (buffer.getNumSamples() > done)
    {
        float* part[2] = { chans[0] + done, chans[1] != nullptr ? chans[1] + done : nullptr };
        engine.process (part, nc, buffer.getNumSamples() - done);
    }

    // 1.5 (roadmap 49 / 53): the note the voice is tuned to goes out as MIDI (channel 1, velocity 100) at the
    // sample where the corrected voice plays it; a note-off when it changes or the voice stops. Switching the
    // option off or bypassing ends a sounding note at the block start. The incoming MIDI is not passed through.
    midi.clear();
    const bool sendNotes = pMidiOut.get() > 0.5f && pBypass.get() < 0.5f;
    const int lastSample = buffer.getNumSamples() - 1;
    auto sync = [&] (int at)
    {
        const int want = sendNotes && voiceNote >= 0 && voiceNote < 128 ? voiceNote : -1;
        if (want == midiOutNote)
            return;
        if (midiOutNote >= 0)
            midi.addEvent (juce::MidiMessage::noteOff (1, midiOutNote), at);
        if (want >= 0)
            midi.addEvent (juce::MidiMessage::noteOn (1, want, (juce::uint8) 100), at);
        midiOutNote = want;
    };
    sync (0);
    engine.takeNoteEvents (blockStart, [&] (int at, int note)
    {
        voiceNote = note;
        sync (juce::jmin (at, lastSample));
    });
}

void VSTMTuneProcessor::processBlock (juce::AudioBuffer<double>& buffer, juce::MidiBuffer& midi)
{
    // 1.5 (roadmap 88): 64-bit hosts. The engine works in 32 bit; the block goes through the float path in
    // pieces of the buffer allocated in prepareToPlay (nothing is allocated here).
    const int nc = juce::jmin (buffer.getNumChannels(), doubleScratch.getNumChannels());
    const int cap = doubleScratch.getNumSamples();
    if (cap <= 0)
    {
        midi.clear();
        return;
    }
    // a block longer than the host promised goes in pieces, each with its own MIDI (no event is seen twice)
    const bool pieces = buffer.getNumSamples() > cap;
    if (pieces)
        midiChunkOut.clear();
    for (int start = 0; start < buffer.getNumSamples(); start += cap)
    {
        const int n = juce::jmin (cap, buffer.getNumSamples() - start);
        juce::AudioBuffer<float> view (doubleScratch.getArrayOfWritePointers(), nc, n);
        for (int ch = 0; ch < nc; ++ch)
        {
            const double* src = buffer.getReadPointer (ch, start);
            float* dst = view.getWritePointer (ch);
            for (int i = 0; i < n; ++i) dst[i] = (float) src[i];
        }
        if (pieces)
        {
            midiChunk.clear();
            midiChunk.addEvents (midi, start, n, -start);
            processBlock (view, midiChunk);
            midiChunkOut.addEvents (midiChunk, 0, n, start);
        }
        else
            processBlock (view, midi);
        for (int ch = 0; ch < nc; ++ch)
        {
            const float* src = view.getReadPointer (ch);
            double* dst = buffer.getWritePointer (ch, start);
            for (int i = 0; i < n; ++i) dst[i] = (double) src[i];
        }
    }
    if (pieces)
    {
        midi.clear();
        midi.addEvents (midiChunkOut, 0, -1, 0);
    }
}

void VSTMTuneProcessor::handOffCharacter()
{
    const auto c = vstm::params::characterFor (pRobotism.get());
    const std::pair<const juce::String*, float> values[] = { { &ids::retune, c.retuneMs }, { &ids::humanize, c.humanizePct },
                                                             { &ids::vibrato, c.vibratoPct } };
    for (const auto& [id, plain] : values)
        if (auto* prm = apvts.getParameter (*id))
        {
            const float v = prm->convertTo0to1 (plain);
            if (std::abs (prm->getValue() - v) > 1.0e-6f)
                prm->setValueNotifyingHost (v);
        }
}

juce::AudioProcessorEditor* VSTMTuneProcessor::createEditor()
{
    return new VSTMTuneEditor (*this);
}

void VSTMTuneProcessor::lockAutoKey (int key, float confidence)
{
    engine.requestKeyLock (key, confidence);
    notifyStateChanged();
}

void VSTMTuneProcessor::reanalyzeAutoKey()
{
    engine.requestKeyReanalyze();
    notifyStateChanged();
}

void VSTMTuneProcessor::setFollowKeyChanges (bool follow)
{
    followKeyChanges = follow;
    notifyStateChanged();
}

void VSTMTuneProcessor::setPresetsKeepKey (bool keep)
{
    presets.setKeepTonal (keep);
    notifyStateChanged();
}

void VSTMTuneProcessor::notifyStateChanged()
{
    // not a parameter: without this a host would not know the project has unsaved changes
    updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true));
}

void VSTMTuneProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("presetName", presets.getCurrentName(), nullptr);
    state.setProperty ("uiScale", (double) uiScale.load(), nullptr);
    state.setProperty ("version", kStateVersion, nullptr);
    // the automatic key: the locked tonic (-1 = not locked) and its confidence; a lock that was just
    // restored and has not reached the audio thread yet counts
    state.setProperty ("autoKeyLock", engine.getKeyLockForState(), nullptr);
    state.setProperty ("autoKeyConf", (double) engine.getKeyConfidenceForState(), nullptr);
    state.setProperty ("followKeyChanges", followKeyChanges.load() ? 1 : 0, nullptr);
    state.setProperty ("presetsKeepKey", presets.getKeepTonal() ? 1 : 0, nullptr);
    state.setProperty ("midiCC", midiLearn.toString(), nullptr);          // 1.5: MIDI Learn assignments
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

namespace
{
/** Session properties read defensively: a damaged or hand-edited value falls back to the default. */
int intProperty (const juce::ValueTree& t, const char* id, int lo, int hi, int fallback)
{
    const auto v = t.getProperty (id).toString().trim();
    if (v.isEmpty() || ! v.containsOnly ("-0123456789"))
        return fallback;
    const int i = v.getIntValue();
    return i >= lo && i <= hi ? i : fallback;
}

bool boolProperty (const juce::ValueTree& t, const char* id, bool fallback)
{
    const auto v = t.getProperty (id).toString().trim().toLowerCase();
    if (v == "1" || v == "true" || v == "yes") return true;
    if (v == "0" || v == "false" || v == "no") return false;
    return fallback;
}
} // namespace

void VSTMTuneProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            struct Restoring { std::atomic<bool>& a; explicit Restoring (std::atomic<bool>& x) : a (x) { a = true; } ~Restoring() { a = false; } } scope (restoringState);
            apvts.replaceState (state);

            // a damaged session can hold "nan" / "inf" (JUCE reads them as numbers): back to the default
            for (auto* prm : getParameters())
                if (! std::isfinite (prm->getValue()))
                    prm->setValueNotifyingHost (prm->getDefaultValue());

            const double scale = state.getProperty ("uiScale", 0.75);
            uiScale = (float) (std::isfinite (scale) ? juce::jlimit (0.5, 1.0, scale) : 0.75);

            // state version 2 (absent in 1.0 sessions: no lock, follow off, presets keep the key).
            // A newer version's unknown properties are ignored; the known ones are read the same way.
            followKeyChanges = boolProperty (state, "followKeyChanges", false);
            presets.setKeepTonal (boolProperty (state, "presetsKeepKey", true));
            const int lock = intProperty (state, "autoKeyLock", -1, 11, -1);
            const double conf = state.getProperty ("autoKeyConf", 0.0);
            if (lock >= 0)
                engine.requestKeyLock (lock, (float) (std::isfinite (conf) ? juce::jlimit (0.0, 1.0, conf) : 0.0));
            else
                engine.requestKeyReanalyze();           // the restored session had no lock: none now either

            midiLearn.fromString (state.getProperty ("midiCC").toString());    // 1.5; older sessions: none
            presets.restoreCurrent (state.getProperty ("presetName", "Default Preset").toString());
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VSTMTuneProcessor();
}
