#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/TuneEngine.h"
#include "PresetManager.h"

class VSTMTuneProcessor : public juce::AudioProcessor,
                          private juce::AudioProcessorValueTreeState::Listener
{
public:
    VSTMTuneProcessor();
    ~VSTMTuneProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                     { return true; }

    const juce::String getName() const override         { return "VSTM Tune"; }
    bool acceptsMidi() const override                   { return false; }
    bool producesMidi() const override                  { return false; }
    bool isMidiEffect() const override                  { return false; }
    double getTailLengthSeconds() const override        { return 0.0; }

    int getNumPrograms() override                       { return 1; }
    int getCurrentProgram() override                    { return 0; }
    void setCurrentProgram (int) override               {}
    const juce::String getProgramName (int) override    { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParam; }

    juce::AudioProcessorValueTreeState apvts;
    vstm::PresetManager presets { apvts };
    vstm::TuneEngine engine;

    /** Editor size, as a fraction of the 1536 x 1024 design canvas. Saved with the session. */
    std::atomic<float> uiScale { 0.75f };

private:
    void parameterChanged (const juce::String& parameterID, float newValue) override;
    vstm::TuneEngine::Params readParams() const noexcept;

    /** A parameter's live value for the audio thread. A stored value that is not a number
        (damaged session or preset) reads as the parameter's default. */
    struct RawParam
    {
        std::atomic<float>* value = nullptr;
        float fallback = 0.0f;
        float get() const noexcept { const float v = value->load(); return std::isfinite (v) ? v : fallback; }
    };

    juce::AudioParameterBool* bypassParam = nullptr;
    RawParam pKey, pScale, pCorrection, pRetune, pHumanize, pRobotism, pVibrato,
             pFormant, pTranspose, pMix, pBypass, pRealtime, pRange;

    double currentRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VSTMTuneProcessor)
};
