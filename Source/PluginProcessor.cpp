#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    // Hazir ayarlar: tek knob (amount) + ratio + karakter
    struct Preset { const char* name; float amount; float ratio; int mode; };

    const Preset kPresets[] = {
        { "Default",         50.0f,  4.0f, 0 },
        { "Light Touch",     30.0f,  2.0f, 0 },
        { "Natural Vocal",   45.0f,  4.0f, 0 },
        { "Smooth Control",  65.0f,  8.0f, 0 },
        { "Tight Vocal",     75.0f,  8.0f, 1 },
        { "Radio Rap",       85.0f, 12.0f, 2 },
        { "Full Smooth",    100.0f,  8.0f, 0 },
        { "Max Squash",     100.0f, 18.0f, 2 },
    };
    constexpr int kNumPresets = (int) (sizeof (kPresets) / sizeof (kPresets[0]));
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout SCCAudioProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // Tek knob: 0..100 %  (threshold'u surer: %0 -> -1 dB, %100 -> -32 dB)
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "amount", 1 }, "Amount",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")));

    // Ratio: 1:1 .. 100:1  (2, 4, 8, 12, 16, 18 tuslari + serbest giris)
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "ratio", 1 }, "Ratio",
        juce::NormalisableRange<float> (1.0f, 100.0f, 0.1f, 0.4f), 4.0f,
        juce::AudioParameterFloatAttributes().withLabel (":1")));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "mode", 1 }, "Character",
        juce::StringArray { "Smooth", "Balanced", "Hard" }, 0));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "autogain", 1 }, "Auto Gain", true));

    // Sadece dedektor 100 Hz altini gormez (ses degismez)
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "lowcut", 1 }, "Low Cut", false));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "bypass", 1 }, "Bypass", false));

    return layout;
}

//==============================================================================
SCCAudioProcessor::SCCAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "SCC_STATE", createLayout())
{
    amountParam   = apvts.getRawParameterValue ("amount");
    ratioParam    = apvts.getRawParameterValue ("ratio");
    modeParam     = apvts.getRawParameterValue ("mode");
    autoGainParam = apvts.getRawParameterValue ("autogain");
    lowCutParam   = apvts.getRawParameterValue ("lowcut");
    bypassParam   = apvts.getRawParameterValue ("bypass");
}

//==============================================================================
void SCCAudioProcessor::prepareToPlay (double sampleRate, int)
{
    compressor.prepare (sampleRate);
}

bool SCCAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in  = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();

    if (in != out)
        return false;

    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void SCCAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numIn  = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    for (int ch = numIn; ch < numOut; ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    scc::Settings s;
    s.amountPct = amountParam->load();
    s.ratio     = ratioParam->load();
    s.mode      = (int) std::lround (modeParam->load());
    s.autoGain  = autoGainParam->load() > 0.5f;
    s.lowCut    = lowCutParam->load() > 0.5f;
    s.bypass    = bypassParam->load() > 0.5f;
    compressor.setSettings (s);

    compressor.process (buffer.getArrayOfWritePointers(),
                        juce::jmin (buffer.getNumChannels(), 2),
                        buffer.getNumSamples());
}

//==============================================================================
juce::AudioProcessorEditor* SCCAudioProcessor::createEditor()
{
    return new SCCAudioProcessorEditor (*this);
}

void SCCAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    apvts.state.setProperty ("preset", currentPreset.load(), nullptr);

    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void SCCAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            currentPreset = juce::jlimit (0, kNumPresets - 1, (int) apvts.state.getProperty ("preset", 0));
        }
}

//==============================================================================
//  Hazir ayarlar
//==============================================================================
int SCCAudioProcessor::getNumPresets()
{
    return kNumPresets;
}

const char* SCCAudioProcessor::getPresetName (int index)
{
    return kPresets[juce::jlimit (0, kNumPresets - 1, index)].name;
}

void SCCAudioProcessor::setParameterReal (const char* paramId, float value)
{
    if (auto* p = apvts.getParameter (paramId))
    {
        const auto& range = p->getNormalisableRange();
        const float v = juce::jlimit (range.start, range.end, value);
        p->beginChangeGesture();
        p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, p->convertTo0to1 (v)));
        p->endChangeGesture();
    }
}

void SCCAudioProcessor::applyPreset (int index)
{
    index = juce::jlimit (0, kNumPresets - 1, index);
    const Preset& p = kPresets[index];

    setParameterReal ("amount", p.amount);
    setParameterReal ("ratio",  p.ratio);
    setParameterReal ("mode",   (float) p.mode);
    currentPreset = index;
}

bool SCCAudioProcessor::isPresetModified() const
{
    const Preset& p = kPresets[juce::jlimit (0, kNumPresets - 1, currentPreset.load())];

    return std::abs (amountParam->load() - p.amount) > 0.15f
        || std::abs (ratioParam->load()  - p.ratio)  > 0.06f
        || (int) std::lround (modeParam->load()) != p.mode;
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SCCAudioProcessor();
}
