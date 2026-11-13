#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    // Kompresor hazir ayarlari (V5): tek knob (amount) + ratio + tepki karakteri
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

    // V6 ile gelen parametreler: AU'da V5 parametrelerinin ARKASINA eklensin diye surum ipucu 2
    constexpr int kV6 = 2;

    // Tam logaritmik aralik (attack / release)
    juce::NormalisableRange<float> logRange (float lo, float hi)
    {
        return { lo, hi,
                 [] (float s, float e, float n) { return s * std::pow (e / s, n); },
                 [] (float s, float e, float v) { return std::log (juce::jlimit (s, e, v) / s) / std::log (e / s); },
                 [] (float s, float e, float v) { return juce::jlimit (s, e, v); } };
    }

    juce::String msText (float v, int)
    {
        return v < 1.0f ? juce::String (v, 2) : (v < 10.0f ? juce::String (v, 1) : juce::String (juce::roundToInt (v)));
    }
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout SCCAudioProcessor::createLayout()
{
    using FloatAttr = juce::AudioParameterFloatAttributes;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    //--------------------------------------------------------------------------
    //  V5 parametreleri: kimlik, aralik ve varsayilanlar DEGISMEDI
    //--------------------------------------------------------------------------
    // Tek knob: 0..100 %  (threshold'u surer: %0 -> -1 dB, %100 -> -32 dB)
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "amount", 1 }, "Amount",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f,
        FloatAttr().withLabel ("%")));

    // Ratio: 1:1 .. 100:1  (2, 4, 8, 12, 16, 18 tuslari + serbest giris)
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "ratio", 1 }, "Ratio",
        juce::NormalisableRange<float> (1.0f, 100.0f, 0.1f, 0.4f), 4.0f,
        FloatAttr().withLabel (":1")));

    // Adaptif kompresorun tepki karakteri (V5'te gorunen adi "Character" idi;
    // V6'da "Character" artik renk motorunun adi oldugu icin "Response" oldu. Kimlik ayni.)
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "mode", 1 }, "Response",
        juce::StringArray { "Smooth", "Balanced", "Hard" }, 0));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "autogain", 1 }, "Auto Gain", true));

    // Sadece dedektor 100 Hz altini gormez (ses degismez)
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "lowcut", 1 }, "Low Cut", false));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "bypass", 1 }, "Bypass", false));

    //--------------------------------------------------------------------------
    //  ANALOG MODE: tamamen manuel kompresor
    //--------------------------------------------------------------------------
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "analog", kV6 }, "Analog Mode", false));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "a_thr", kV6 }, "Analog Threshold",
        juce::NormalisableRange<float> (-60.0f, 0.0f, 0.1f), -18.0f,
        FloatAttr().withLabel ("dB")));

    {
        juce::StringArray ratios;
        for (int i = 0; i < scc::kNumAnalogRatios; ++i)
            ratios.add (scc::kAnalogRatios[i] >= scc::kManualLimitRatio
                            ? juce::String ("LIMIT")
                            : juce::String (scc::kAnalogRatios[i], std::abs (scc::kAnalogRatios[i] - std::round (scc::kAnalogRatios[i])) < 0.01f ? 0 : 1) + ":1");
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { "a_ratio", kV6 }, "Analog Ratio", ratios, 3));        // 4:1
    }

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "a_attack", kV6 }, "Analog Attack",
        logRange (0.1f, 100.0f), 10.0f,
        FloatAttr().withLabel ("ms").withStringFromValueFunction (msText)));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "a_release", kV6 }, "Analog Release",
        logRange (10.0f, 1000.0f), 120.0f,
        FloatAttr().withLabel ("ms").withStringFromValueFunction (msText)));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "a_knee", kV6 }, "Analog Knee",
        juce::NormalisableRange<float> (0.0f, 24.0f, 0.1f), 6.0f,
        FloatAttr().withLabel ("dB")));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "a_makeup", kV6 }, "Analog Makeup",
        juce::NormalisableRange<float> (0.0f, 24.0f, 0.1f), 0.0f,
        FloatAttr().withLabel ("dB")));

    //--------------------------------------------------------------------------
    //  CHARACTER ENGINE
    //--------------------------------------------------------------------------
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "char_on", kV6 }, "Character On", false));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "char_amt", kV6 }, "Character Amount",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f,
        FloatAttr().withLabel ("%")));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "char_slot", kV6 }, "Character A/B",
        juce::StringArray { "A", "B" }, 0));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "char_match", kV6 }, "Character Level Match", false));

    juce::StringArray typeNames;
    for (int t = 0; t < scc::kNumCharacterTypes; ++t)
        typeNames.add (scc::characterTypeName (t));

    for (int slot = 0; slot < 2; ++slot)
    {
        const juce::String slotName = slot == 0 ? "A " : "B ";

        // A = Warm, B = Tape: A/B ilk andan itibaren iki farkli karakteri karsilastirir
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { sccparams::slotTypeId (slot), kV6 }, slotName + "Character Type",
            typeNames, slot == 0 ? (int) scc::kCharWarm : (int) scc::kCharTape));

        for (int i = 0; i < scc::kNumCustomParams; ++i)
        {
            const auto& info = scc::customParamInfo (i);
            layout.add (std::make_unique<juce::AudioParameterFloat> (
                juce::ParameterID { sccparams::slotParamId (slot, i), kV6 }, slotName + info.name,
                juce::NormalisableRange<float> (info.minValue, info.maxValue, 0.1f), info.defaultValue,
                FloatAttr().withLabel ("%")));
        }
    }

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

    analogParam   = apvts.getRawParameterValue ("analog");
    aThrParam     = apvts.getRawParameterValue ("a_thr");
    aRatioParam   = apvts.getRawParameterValue ("a_ratio");
    aAttackParam  = apvts.getRawParameterValue ("a_attack");
    aReleaseParam = apvts.getRawParameterValue ("a_release");
    aKneeParam    = apvts.getRawParameterValue ("a_knee");
    aMakeupParam  = apvts.getRawParameterValue ("a_makeup");

    charOnParam    = apvts.getRawParameterValue ("char_on");
    charAmtParam   = apvts.getRawParameterValue ("char_amt");
    charSlotParam  = apvts.getRawParameterValue ("char_slot");
    charMatchParam = apvts.getRawParameterValue ("char_match");

    for (int slot = 0; slot < 2; ++slot)
    {
        slotTypeParam[slot] = apvts.getRawParameterValue (sccparams::slotTypeId (slot));
        for (int i = 0; i < scc::kNumCustomParams; ++i)
            slotCustomParam[slot][i] = apvts.getRawParameterValue (sccparams::slotParamId (slot, i));
    }

    bypassParameter = apvts.getParameter ("bypass");
}

//==============================================================================
void SCCAudioProcessor::prepareToPlay (double sampleRate, int)
{
    engine.prepare (sampleRate);
    currentSampleRate = sampleRate;
    setLatencySamples (0);              // minimum fazli oversampling: gecikme bildirilmez
}

bool SCCAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in  = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();

    if (in != out)
        return false;

    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

scc::EngineSettings SCCAudioProcessor::readSettings() const noexcept
{
    scc::EngineSettings s;

    s.adaptive.amountPct = amountParam->load();
    s.adaptive.ratio     = ratioParam->load();
    s.adaptive.mode      = (int) std::lround (modeParam->load());
    s.adaptive.autoGain  = autoGainParam->load() > 0.5f;
    s.adaptive.lowCut    = lowCutParam->load() > 0.5f;

    s.analogMode = analogParam->load() > 0.5f;
    s.manual.thresholdDb = aThrParam->load();
    s.manual.ratio       = scc::kAnalogRatios[juce::jlimit (0, scc::kNumAnalogRatios - 1, (int) std::lround (aRatioParam->load()))];
    s.manual.attackMs    = aAttackParam->load();
    s.manual.releaseMs   = aReleaseParam->load();
    s.manual.kneeDb      = aKneeParam->load();
    s.manual.makeupDb    = aMakeupParam->load();
    s.manual.autoGain    = s.adaptive.autoGain;         // AUTO GAIN ve LOW CUT iki modda da ayni dugmeler
    s.manual.lowCut      = s.adaptive.lowCut;

    s.character.on         = charOnParam->load() > 0.5f;
    s.character.intensity  = 0.01f * charAmtParam->load();
    s.character.slot       = charSlotParam->load() > 0.5f ? 1 : 0;
    s.character.levelMatch = charMatchParam->load() > 0.5f;
    for (int slot = 0; slot < 2; ++slot)
    {
        s.character.spec[slot].type = (int) std::lround (slotTypeParam[slot]->load());
        for (int i = 0; i < scc::kNumCustomParams; ++i)
            s.character.spec[slot].custom[i] = slotCustomParam[slot][i]->load();
    }

    s.bypass = bypassParam->load() > 0.5f;
    return s;
}

void SCCAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numIn  = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    for (int ch = numIn; ch < numOut; ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    engine.setSettings (readSettings());
    engine.process (buffer.getArrayOfWritePointers(),
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
    // canli agaca dokunmadan, kopyasi uzerinde tamamla (host bunu her is parcacigindan cagirabilir)
    auto state = apvts.copyState();
    state.setProperty ("preset", currentPreset.load(), nullptr);
    state.setProperty ("stateVersion", 6, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void SCCAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            const auto tree = juce::ValueTree::fromXml (*xml);

            // Eski (V5) oturumlar yeni parametreleri icermez. Icermedikleri her parametre
            // varsayilanina doner; boylece V5 oturumu V5'teki gibi calar (Analog ve Character kapali).
            for (auto* p : getParameters())
                if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
                    if (! tree.getChildWithProperty ("id", rp->getParameterID()).isValid())
                        rp->setValueNotifyingHost (rp->getDefaultValue());

            apvts.replaceState (tree);
            currentPreset = juce::jlimit (0, kNumPresets - 1, (int) apvts.state.getProperty ("preset", 0));
        }
}

//==============================================================================
void SCCAudioProcessor::setParameterReal (const juce::String& paramId, float value)
{
    if (auto* p = apvts.getParameter (paramId))
    {
        const auto& range = p->getNormalisableRange();
        const float v = juce::jlimit (range.start, range.end, value);
        const float n = juce::jlimit (0.0f, 1.0f, p->convertTo0to1 (v));
        if (std::abs (n - p->getValue()) > 1.0e-7f)
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (n);
            p->endChangeGesture();
        }
    }
}

//==============================================================================
//  Kompresor hazir ayarlari
//==============================================================================
int SCCAudioProcessor::getNumPresets()
{
    return kNumPresets;
}

const char* SCCAudioProcessor::getPresetName (int index)
{
    return kPresets[juce::jlimit (0, kNumPresets - 1, index)].name;
}

void SCCAudioProcessor::applyPreset (int index)
{
    index = juce::jlimit (0, kNumPresets - 1, index);
    const Preset& p = kPresets[index];

    // hazir ayarlar adaptif kompresor icindir: secilince Adaptive moda donulur
    setParameterReal ("analog", 0.0f);
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
//  Karakter slotlari ve presetleri
//==============================================================================
int SCCAudioProcessor::getActiveSlot() const noexcept
{
    return charSlotParam->load() > 0.5f ? 1 : 0;
}

void SCCAudioProcessor::setActiveSlot (int slot)
{
    setParameterReal ("char_slot", slot != 0 ? 1.0f : 0.0f);
}

scc::CharacterSpec SCCAudioProcessor::getSlotSpec (int slot) const
{
    slot = slot != 0 ? 1 : 0;
    scc::CharacterSpec spec;
    spec.type = juce::jlimit (0, (int) scc::kNumCharacterTypes - 1, (int) std::lround (slotTypeParam[slot]->load()));
    for (int i = 0; i < scc::kNumCustomParams; ++i)
        spec.custom[i] = slotCustomParam[slot][i]->load();
    return spec;
}

void SCCAudioProcessor::setSlotSpec (int slot, const scc::CharacterSpec& spec)
{
    slot = slot != 0 ? 1 : 0;
    for (int i = 0; i < scc::kNumCustomParams; ++i)
        setParameterReal (sccparams::slotParamId (slot, i), spec.custom[i]);
    setParameterReal (sccparams::slotTypeId (slot), (float) spec.type);
}

juce::String SCCAudioProcessor::getSlotPresetName (int slot) const
{
    return apvts.state.getProperty (slot != 0 ? "charNameB" : "charNameA", juce::String()).toString();
}

void SCCAudioProcessor::setSlotPresetName (int slot, const juce::String& name)
{
    apvts.state.setProperty (slot != 0 ? "charNameB" : "charNameA", name, nullptr);
}

bool SCCAudioProcessor::isSlotModified (int slot) const
{
    const int index = characterPresets.indexOf (getSlotPresetName (slot));
    if (index < 0)
        return false;

    const auto& preset = characterPresets.getPreset (index).spec;
    const auto  now    = getSlotSpec (slot);
    for (int i = 0; i < scc::kNumCustomParams; ++i)
        if (std::abs (now.custom[i] - preset.custom[i]) > 0.06f)
            return true;
    return false;
}

void SCCAudioProcessor::loadCharacterPreset (int slot, int presetIndex)
{
    if (presetIndex < 0 || presetIndex >= characterPresets.getNumPresets())
        return;

    const auto& preset = characterPresets.getPreset (presetIndex);
    setSlotSpec (slot, preset.spec);
    setSlotPresetName (slot, preset.name);
}

void SCCAudioProcessor::copySlot (int fromSlot, int toSlot)
{
    if ((fromSlot != 0) == (toSlot != 0))
        return;
    setSlotSpec (toSlot, getSlotSpec (fromSlot));
    setSlotPresetName (toSlot, getSlotPresetName (fromSlot));
}

void SCCAudioProcessor::swapSlots()
{
    const auto a = getSlotSpec (0), b = getSlotSpec (1);
    const auto nameA = getSlotPresetName (0), nameB = getSlotPresetName (1);
    setSlotSpec (0, b);
    setSlotSpec (1, a);
    setSlotPresetName (0, nameB);
    setSlotPresetName (1, nameA);
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SCCAudioProcessor();
}
