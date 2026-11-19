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
    for (int i = 0; i < 12; ++i)
        pScaleNote[i] = raw (ids::scaleNote (i));

    apvts.addParameterListener (ids::realtime, this);
    apvts.state.setProperty ("presetName", presets.getCurrentName(), nullptr);

    engine.prepare (currentRate, true);
    setLatencySamples (engine.getLatencySamples());
}

VSTMTuneProcessor::~VSTMTuneProcessor()
{
    apvts.removeParameterListener (ids::realtime, this);
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
    p.adaptive = pAdaptive.get() > 0.5f;
    p.followKeyChanges = followKeyChanges.load (std::memory_order_relaxed);
    return p;
}

void VSTMTuneProcessor::prepareToPlay (double sampleRate, int)
{
    currentRate = sampleRate;
    engine.setParams (readParams());
    engine.prepare (sampleRate, pRealtime.get() > 0.5f);
    setLatencySamples (engine.getLatencySamples());
}

void VSTMTuneProcessor::parameterChanged (const juce::String& id, float newValue)
{
    // The latency mode is not automatable, so this arrives from the UI or a state restore.
    if (id == ids::realtime)
        setLatencySamples (TuneEngine::latencyFor (currentRate, newValue > 0.5f));
}

void VSTMTuneProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numIn = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    for (int ch = numIn; ch < numOut; ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    engine.setParams (readParams());
    const bool rt = pRealtime.get() > 0.5f;
    if (engine.isRealtime() != rt)
        engine.setRealtime (rt);

    const int nc = juce::jmin (2, buffer.getNumChannels());
    if (nc == 0)
        return;
    float* chans[2] = { buffer.getWritePointer (0), nc > 1 ? buffer.getWritePointer (1) : nullptr };
    engine.process (chans, nc, buffer.getNumSamples());
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

            presets.restoreCurrent (state.getProperty ("presetName", "Default Preset").toString());
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VSTMTuneProcessor();
}
