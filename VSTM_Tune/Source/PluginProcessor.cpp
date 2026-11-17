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
    pKey        = apvts.getRawParameterValue (ids::key);
    pScale      = apvts.getRawParameterValue (ids::scale);
    pCorrection = apvts.getRawParameterValue (ids::correction);
    pRetune     = apvts.getRawParameterValue (ids::retune);
    pHumanize   = apvts.getRawParameterValue (ids::humanize);
    pRobotism   = apvts.getRawParameterValue (ids::robotism);
    pVibrato    = apvts.getRawParameterValue (ids::vibrato);
    pFormant    = apvts.getRawParameterValue (ids::formant);
    pTranspose  = apvts.getRawParameterValue (ids::transpose);
    pMix        = apvts.getRawParameterValue (ids::mix);
    pBypass     = apvts.getRawParameterValue (ids::bypass);
    pRealtime   = apvts.getRawParameterValue (ids::realtime);
    pRange      = apvts.getRawParameterValue (ids::range);

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
    p.key = juce::roundToInt (pKey->load()) - 1;
    p.scale = juce::jlimit (0, (int) kNumScales - 1, juce::roundToInt (pScale->load()));
    p.amount = pCorrection->load() * 0.01f;
    p.retuneMs = pRetune->load();
    p.humanize = pHumanize->load() * 0.01f;
    p.robotism = pRobotism->load() * 0.01f;
    p.vibratoScale = pVibrato->load() * 0.01f;
    p.formant = pFormant->load();
    p.transpose = std::round (pTranspose->load());
    p.mix = pMix->load() * 0.01f;
    p.active = pBypass->load() < 0.5f;
    p.range = juce::roundToInt (pRange->load());
    return p;
}

void VSTMTuneProcessor::prepareToPlay (double sampleRate, int)
{
    currentRate = sampleRate;
    engine.setParams (readParams());
    engine.prepare (sampleRate, pRealtime->load() > 0.5f);
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
    const bool rt = pRealtime->load() > 0.5f;
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

void VSTMTuneProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("presetName", presets.getCurrentName(), nullptr);
    state.setProperty ("uiScale", (double) uiScale.load(), nullptr);
    state.setProperty ("version", 1, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void VSTMTuneProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            apvts.replaceState (state);
            uiScale = (float) juce::jlimit (0.5, 1.0, (double) state.getProperty ("uiScale", 0.75));
            presets.restoreCurrent (state.getProperty ("presetName", "Default Preset").toString());
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VSTMTuneProcessor();
}
