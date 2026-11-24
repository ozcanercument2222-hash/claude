#include "Parameters/ParameterLayout.h"
#include "Parameters/Choices.h"
#include "Parameters/Divisions.h"
#include "Parameters/ParameterIDs.h"

#include <cmath>
#include <memory>

namespace vex::params
{
namespace
{
    using Layout = juce::AudioProcessorValueTreeState::ParameterLayout;

    juce::ParameterID makeId (const char* id) { return juce::ParameterID { id, kParameterVersion }; }

    /** Number typed by the user: "75", "75 %", "-12.5ms" ... (unit text is ignored). */
    float parseNumber (const juce::String& text)
    {
        return text.retainCharacters ("0123456789.,-+").replaceCharacter (',', '.').getFloatValue();
    }

    void addBool (Layout& layout, const char* id, const juce::String& name, bool defaultValue,
                  const juce::String& onText = "On", const juce::String& offText = "Off")
    {
        layout.add (std::make_unique<juce::AudioParameterBool> (
            makeId (id), name, defaultValue,
            juce::AudioParameterBoolAttributes()
                .withStringFromValueFunction ([onText, offText] (bool value, int) { return value ? onText : offText; })
                .withValueFromStringFunction ([onText] (const juce::String& text)
                {
                    const auto t = text.trim();
                    return t.equalsIgnoreCase (onText) || t == "1" || t.equalsIgnoreCase ("on") || t.equalsIgnoreCase ("true");
                })));
    }

    void addChoice (Layout& layout, const char* id, const juce::String& name, const juce::StringArray& choices, int defaultIndex)
    {
        layout.add (std::make_unique<juce::AudioParameterChoice> (makeId (id), name, choices, defaultIndex));
    }

    /** 0 - maxPercent %, shown with one decimal. */
    void addPercent (Layout& layout, const char* id, const juce::String& name, float defaultPercent, float maxPercent = 100.0f)
    {
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            makeId (id), name, juce::NormalisableRange<float> (0.0f, maxPercent, 0.1f), defaultPercent,
            juce::AudioParameterFloatAttributes()
                .withLabel ("%")
                .withStringFromValueFunction ([] (float value, int) { return juce::String (value, 1); })
                .withValueFromStringFunction ([maxPercent] (const juce::String& text)
                {
                    return juce::jlimit (0.0f, maxPercent, parseNumber (text));
                })));
    }

    /** Milliseconds; `centre` > 0 skews the range so that value sits in the middle of the control. */
    void addMs (Layout& layout, const char* id, const juce::String& name, float lo, float hi, float defaultMs,
                float centre = 0.0f, bool signedDisplay = false)
    {
        juce::NormalisableRange<float> range (lo, hi, 0.1f);
        if (centre > 0.0f)
            range.setSkewForCentre (centre);

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            makeId (id), name, range, defaultMs,
            juce::AudioParameterFloatAttributes()
                .withLabel ("ms")
                .withStringFromValueFunction ([signedDisplay] (float value, int) -> juce::String
                {
                    if (std::abs (value) < 0.05f)
                        return "0.0";
                    return (signedDisplay && value > 0.0f ? "+" : "") + juce::String (value, 1);
                })
                .withValueFromStringFunction ([lo, hi] (const juce::String& text)
                {
                    return juce::jlimit (lo, hi, parseNumber (text));
                })));
    }
}

namespace
{
    template <std::size_t N>
    juce::StringArray toArray (const char* const (&names)[N])
    {
        juce::StringArray a;
        for (auto* n : names)
            a.add (n);
        return a;
    }
}

const juce::StringArray& modeChoices()       { static const auto a = toArray (choices::modeNames);       return a; }
const juce::StringArray& styleChoices()      { static const auto a = toArray (choices::styleNames);      return a; }
const juce::StringArray& shapeChoices()      { static const auto a = toArray (choices::shapeNames);      return a; }
const juce::StringArray& glitchTypeChoices() { static const auto a = toArray (choices::glitchTypeNames); return a; }

const juce::StringArray& divisionChoices()
{
    static const juce::StringArray choices = []
    {
        juce::StringArray names;
        for (const auto& d : divisions::table)
            names.add (d.name);
        return names;
    }();
    return choices;
}

Layout createLayout()
{
    Layout layout;

    // Defaults: specification section 13 where it gives one; the others are provisional until
    // listening tests (docs/ARCHITECTURE.md, section 4).
    addBool    (layout, ids::bypass,     "Bypass",      false);

    addChoice  (layout, ids::mode,       "Mode",        modeChoices(), 0);                        // Stutter
    addPercent (layout, ids::amount,     "Amount",      75.0f);
    addBool    (layout, ids::sync,       "Sync",        true, "Host", "Free");
    addChoice  (layout, ids::rate,       "Rate",        divisionChoices(), divisions::defaultIndex);   // 1/8
    addMs      (layout, ids::rateMs,     "Rate (Free)", 10.0f, 2000.0f, 250.0f, 250.0f);
    addPercent (layout, ids::length,     "Length",      50.0f);
    addChoice  (layout, ids::style,      "Style",       styleChoices(), 0);                       // Sharp
    addChoice  (layout, ids::shape,      "Shape",       shapeChoices(), 0);                       // Hard
    addPercent (layout, ids::smoothness, "Smoothness",  40.0f);
    addMs      (layout, ids::offsetMs,   "Offset",      -100.0f, 100.0f, 0.0f, 0.0f, true);
    addPercent (layout, ids::mix,        "Mix",         100.0f);
    addChoice  (layout, ids::glitchType, "Glitch Type", glitchTypeChoices(), 0);                  // Pixel
    addPercent (layout, ids::intensity,  "Intensity",   70.0f);
    addBool    (layout, ids::freeze,     "Freeze",      false);

    addBool    (layout, ids::fxEnabled,     "FX Chain",       true);
    addBool    (layout, ids::delaySync,     "Delay Sync",     true, "Host", "Free");
    addChoice  (layout, ids::delayRate,     "Delay Rate",     divisionChoices(), divisions::defaultIndex);
    addMs      (layout, ids::delayTimeMs,   "Delay Time",     1.0f, 2000.0f, 250.0f, 250.0f);
    addPercent (layout, ids::delayFeedback, "Delay Feedback", 30.0f, kMaxDelayFeedbackPercent);
    addPercent (layout, ids::delayMix,      "Delay Mix",      0.0f);
    addPercent (layout, ids::reverbSize,    "Reverb Size",    50.0f);
    addPercent (layout, ids::reverbDamping, "Reverb Damping", 50.0f);
    addPercent (layout, ids::reverbMix,     "Reverb Mix",     0.0f);

    return layout;
}
}
