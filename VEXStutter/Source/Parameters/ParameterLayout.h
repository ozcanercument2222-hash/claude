#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace vex::params
{
    /** Version hint of every ParameterID (VST3 / AU parameter-list versioning). Parameters added in a
        later release get a higher hint; existing ones keep 1. */
    inline constexpr int kParameterVersion = 1;

    /** Hard safety cap of the delay feedback (specification section 11): the range itself stops at it. */
    inline constexpr float kMaxDelayFeedbackPercent = 85.0f;

    /** Choice lists as StringArrays (built from Parameters/Choices.h and Divisions.h). */
    const juce::StringArray& modeChoices();
    const juce::StringArray& styleChoices();
    const juce::StringArray& shapeChoices();
    const juce::StringArray& glitchTypeChoices();
    const juce::StringArray& divisionChoices();

    /** The complete parameter set, created once by the processor. */
    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
}
