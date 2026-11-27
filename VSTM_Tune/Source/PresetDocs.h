#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "DrpText.h"

namespace vstm
{
/** 1.6 readable .drp: the preset with an explanation of every value. What the comments say comes from the code:
    range, step, default, choices, unit and automation from the parameter objects themselves; the engine
    conversion from PluginProcessor::readParams (written out per parameter below, kept in step with it by the
    tests); the Auto Character curves from params::characterFor, whose example values are computed by calling it.
    Unknown IDs (a newer version's) are written back unexplained, so nothing is lost. Message thread only. */
drp::text::Document describePreset (const juce::String& name, const std::vector<std::pair<juce::String, float>>& values,
                                    juce::AudioProcessorValueTreeState& apvts);
} // namespace vstm
