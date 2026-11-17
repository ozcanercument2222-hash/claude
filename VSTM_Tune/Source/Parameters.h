#pragma once
//==============================================================================
//  Parametre kimlikleri, araliklari ve host'ta gorunen deger yazilari
//==============================================================================
#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/Scales.h"

namespace vstm::ids
{
inline const juce::String key        { "key" };
inline const juce::String scale      { "scale" };
inline const juce::String correction { "correction" };
inline const juce::String retune     { "retune" };
inline const juce::String humanize   { "humanize" };
inline const juce::String robotism   { "robotism" };
inline const juce::String vibrato    { "vibscale" };
inline const juce::String formant    { "formant" };
inline const juce::String transpose  { "transpose" };
inline const juce::String mix        { "mix" };
inline const juce::String bypass     { "bypass" };
inline const juce::String realtime   { "realtime" };
inline const juce::String range      { "range" };
}

namespace vstm::params
{
inline juce::StringArray keyChoices()
{
    juce::StringArray s { "Auto" };
    for (int i = 0; i < 12; ++i)
        s.add (noteName (i));
    return s;
}

inline juce::StringArray scaleChoices()
{
    juce::StringArray s;
    for (int i = 0; i < kNumScales; ++i)
        s.add (scaleName (i));
    return s;
}

inline juce::StringArray rangeChoices() { return { "Soprano / Alto", "Tenor / Mid", "Bass / Baritone" }; }

//------------------------------------------------------------------ text
inline juce::String percentText (float v)             { return juce::String (juce::roundToInt (v)) + "%"; }

inline juce::String msText (float v)
{
    if (v < 9.95f)
        return juce::String (v, 1) + " ms";
    return juce::String (juce::roundToInt (v)) + " ms";
}

inline juce::String robotismText (float v)
{
    const int amount = juce::roundToInt (std::abs (v - 50.0f) * 2.0f);
    if (amount == 0)
        return "Dengeli";
    return (v < 50.0f ? juce::String ("Robotik ") : juce::String (juce::CharPointer_UTF8 ("\xc4\xb0nsans\xc4\xb1 "))) + juce::String (amount) + "%";
}

inline float robotismFromText (const juce::String& t)
{
    const auto lower = t.toLowerCase();
    const float n = (float) lower.retainCharacters ("0123456789.").getDoubleValue();
    if (lower.contains ("rob"))  return 50.0f - n * 0.5f;
    if (lower.contains ("ns") || lower.contains ("hum")) return 50.0f + n * 0.5f;
    if (lower.contains ("deng") || lower.contains ("bal")) return 50.0f;
    return n;
}

inline juce::String formantText (float v)
{
    if (std::abs (v) < 0.05f)
        return "0.0";
    return (v > 0.0f ? "+" : "") + juce::String (v, 1);
}

inline juce::String semitoneText (float v)
{
    const int st = juce::roundToInt (v);
    return (st > 0 ? "+" : "") + juce::String (st) + " st";
}

inline float numberFromText (const juce::String& t)
{
    return (float) t.replace (juce::String::charToString ((juce::juce_wchar) 0x2212), "-")
                    .retainCharacters ("0123456789.,-+").replaceCharacter (',', '.').getDoubleValue();
}

//------------------------------------------------------------------ layout
inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto floatParam = [&] (const String& id, const String& name, NormalisableRange<float> range, float def,
                           std::function<String (float)> toText, std::function<float (const String&)> fromText)
    {
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { id, 1 }, name, range, def,
            AudioParameterFloatAttributes()
                .withStringFromValueFunction ([toText] (float v, int) { return toText (v); })
                .withValueFromStringFunction ([fromText] (const String& s) { return fromText (s); })));
    };

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::key, 1 }, "Key", keyChoices(), 0));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::scale, 1 }, "Scale", scaleChoices(), kMajor));

    floatParam (ids::correction, "Pitch Correction", { 0.0f, 100.0f, 0.1f }, 100.0f, percentText, numberFromText);

    // 0 - 200 ms; skew puts 15 ms near the middle of the slider, where most vocals live
    NormalisableRange<float> retuneRange { 0.0f, 200.0f, 0.1f };
    retuneRange.setSkewForCentre (20.0f);
    floatParam (ids::retune, "Retune Speed", retuneRange, 15.0f, msText, numberFromText);

    floatParam (ids::humanize, "Humanize", { 0.0f, 100.0f, 0.1f }, 35.0f, percentText, numberFromText);
    floatParam (ids::robotism, "Robotism", { 0.0f, 100.0f, 0.1f }, 50.0f, robotismText, robotismFromText);
    floatParam (ids::vibrato, "Vibrato Scale", { 0.0f, 200.0f, 0.1f }, 100.0f, percentText, numberFromText);
    floatParam (ids::formant, "Formant", { -12.0f, 12.0f, 0.1f }, 0.0f, formantText, numberFromText);
    floatParam (ids::transpose, "Pitch Range", { -12.0f, 12.0f, 1.0f }, 0.0f, semitoneText, numberFromText);
    floatParam (ids::mix, "Mix", { 0.0f, 100.0f, 0.1f }, 100.0f, percentText, numberFromText);

    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::bypass, 1 }, "Bypass", false));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::realtime, 1 }, "Realtime", true,
                                                      AudioParameterBoolAttributes().withAutomatable (false)));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::range, 1 }, "Vocal Range", rangeChoices(), 1,
                                                        AudioParameterChoiceAttributes().withAutomatable (false)));
    return layout;
}
} // namespace vstm::params
