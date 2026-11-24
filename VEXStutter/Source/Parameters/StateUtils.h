#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <cmath>

/** Helpers shared by the session state and the preset files: both hold parameter values that come
    from outside (a host, a file) and are validated the same way. */
namespace vex::state
{
    inline const juce::Identifier kParamType { "PARAM" };
    inline const juce::Identifier kIdKey     { "id" };
    inline const juce::Identifier kValueKey  { "value" };

    /** A stored value as a number; anything that is not a plain finite number gives NaN (-> default). */
    inline double parseStoredNumber (const juce::var& stored)
    {
        if (stored.isInt() || stored.isInt64() || stored.isDouble() || stored.isBool())
            return (double) stored;
        if (! stored.isString())
            return std::nan ("");
        const auto t = stored.toString().trim();
        if (t.isEmpty() || t.length() > 32 || ! t.containsOnly ("0123456789.-+eE") || ! t.containsAnyOf ("0123456789"))
            return std::nan ("");
        return t.getDoubleValue();
    }

    /** `v` (real units) made legal for `param`: clamped and snapped; non-finite gives the default. */
    inline float legalValue (const juce::RangedAudioParameter& param, double v)
    {
        const auto& range = param.getNormalisableRange();
        if (! std::isfinite (v))
            return param.convertFrom0to1 (param.getDefaultValue());
        return range.snapToLegalValue (juce::jlimit (range.start, range.end, (float) v));
    }

    /** The current value of `param` in real units. */
    inline float realValue (const juce::RangedAudioParameter& param)
    {
        return param.convertFrom0to1 (param.getValue());
    }
}
