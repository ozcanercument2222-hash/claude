#pragma once
//==============================================================================
//  Parametre kimlikleri, araliklari ve host'ta gorunen deger yazilari
//
//  Kimlikler (ID) hic degismez; adlar ve deger yazilari arayuz dilindedir (Lang.h).
//  Yazilan deger her dilde okunur (or. "Robotik 40%" Ingilizce arayuzde de gecerli).
//==============================================================================
#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/Scales.h"
#include "Lang.h"

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
// added after 1.0 (version hint 2): appended after the 1.0 parameters, 1.0 IDs and order unchanged
inline const juce::String customScale { "customscale" };
inline const juce::String adaptive    { "adaptive" };
/** Custom scale note i = i semitones above the key (0 = the key itself). */
inline juce::String scaleNote (int i)  { return "scalenote" + juce::String (i); }
}

namespace vstm::params
{
inline juce::StringArray keyChoices()
{
    juce::StringArray s { i18n::tr ("Auto") };
    for (int i = 0; i < 12; ++i)
        s.add (noteName (i));
    return s;
}

inline juce::StringArray scaleChoices()
{
    juce::StringArray s;
    for (int i = 0; i < kNumScales; ++i)
        s.add (i18n::tr (scaleName (i)));
    return s;
}

inline juce::StringArray rangeChoices()
{
    return { i18n::tr ("Soprano / Alto"), i18n::tr ("Tenor / Mid"), i18n::tr ("Bass / Baritone") };
}

/** Interval names of the custom scale notes (above the key): 1, b2, 2, b3, 3, 4, #4, 5, b6, 6, b7, 7. */
inline const char* degreeName (int i)
{
    static const char* names[] = { "1", "b2", "2", "b3", "3", "4", "#4", "5", "b6", "6", "b7", "7" };
    return names[((i % 12) + 12) % 12];
}

/** The custom scale starts out as the major scale. */
inline bool customNoteDefault (int i) { return ((scaleMask (kMajor) >> ((i % 12 + 12) % 12)) & 1u) != 0; }

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
        return i18n::tr ("Balanced");
    return i18n::tr (v < 50.0f ? "Robotic" : "Human") + " " + juce::String (amount) + "%";
}

inline float robotismFromText (const juce::String& t)
{
    const auto lower = t.toLowerCase();
    const float n = (float) lower.retainCharacters ("0123456789.").getDoubleValue();
    auto says = [&lower] (const char* key)                 // the word in any UI language
    {
        for (const auto& w : i18n::allTexts (key))
            if (w.isNotEmpty() && lower.contains (w.toLowerCase()))
                return true;
        return false;
    };
    if (says ("Balanced") || lower.contains ("deng") || lower.contains ("bal")) return 50.0f;
    if (says ("Robotic") || lower.contains ("rob"))                             return 50.0f - n * 0.5f;
    if (says ("Human") || lower.contains ("ns") || lower.contains ("hum"))      return 50.0f + n * 0.5f;
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

    using i18n::tr;
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::key, 1 }, tr ("Key"), keyChoices(), 0));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::scale, 1 }, tr ("Scale"), scaleChoices(), kMajor));

    floatParam (ids::correction, tr ("Pitch Correction"), { 0.0f, 100.0f, 0.1f }, 100.0f, percentText, numberFromText);

    // 0 - 200 ms; skew puts 15 ms near the middle of the slider, where most vocals live
    NormalisableRange<float> retuneRange { 0.0f, 200.0f, 0.1f };
    retuneRange.setSkewForCentre (20.0f);
    floatParam (ids::retune, tr ("Retune Speed"), retuneRange, 15.0f, msText, numberFromText);

    floatParam (ids::humanize, tr ("Humanize"), { 0.0f, 100.0f, 0.1f }, 35.0f, percentText, numberFromText);
    floatParam (ids::robotism, tr ("Robotism"), { 0.0f, 100.0f, 0.1f }, 50.0f, robotismText, robotismFromText);
    floatParam (ids::vibrato, tr ("Vibrato Scale"), { 0.0f, 200.0f, 0.1f }, 100.0f, percentText, numberFromText);
    floatParam (ids::formant, tr ("Formant"), { -12.0f, 12.0f, 0.1f }, 0.0f, formantText, numberFromText);
    floatParam (ids::transpose, tr ("Pitch Range"), { -12.0f, 12.0f, 1.0f }, 0.0f, semitoneText, numberFromText);
    floatParam (ids::mix, tr ("Mix"), { 0.0f, 100.0f, 0.1f }, 100.0f, percentText, numberFromText);

    const auto onOff = AudioParameterBoolAttributes()
                           .withStringFromValueFunction ([] (bool b, int) { return i18n::tr (b ? "On" : "Off"); })
                           .withValueFromStringFunction ([] (const String& s)
                           {
                               for (const auto& w : i18n::allTexts ("On"))
                                   if (s.trim().equalsIgnoreCase (w)) return true;
                               return s.trim() == "1" || s.trim().equalsIgnoreCase ("true");
                           });
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::bypass, 1 }, tr ("Bypass"), false, onOff));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::realtime, 1 }, tr ("Realtime"), true,
                                                      AudioParameterBoolAttributes (onOff).withAutomatable (false)));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::range, 1 }, tr ("Vocal Range"), rangeChoices(), 1,
                                                        AudioParameterChoiceAttributes().withAutomatable (false)));

    // after 1.0: custom scale (12 notes relative to the key) and Adaptive, after everything 1.0 had
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::customScale, 2 }, tr ("Custom Scale"), false, onOff));
    for (int i = 0; i < 12; ++i)
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::scaleNote (i), 2 },
                                                          tr ("Custom Note") + " " + degreeName (i), customNoteDefault (i), onOff));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::adaptive, 2 }, tr ("Adaptive"), false, onOff));
    return layout;
}
} // namespace vstm::params
