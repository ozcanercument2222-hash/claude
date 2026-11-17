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

    juce::AudioParameterBool* bypassParam = nullptr;
    std::atomic<float>* pKey = nullptr;
    std::atomic<float>* pScale = nullptr;
    std::atomic<float>* pCorrection = nullptr;
    std::atomic<float>* pRetune = nullptr;
    std::atomic<float>* pHumanize = nullptr;
    std::atomic<float>* pRobotism = nullptr;
    std::atomic<float>* pVibrato = nullptr;
    std::atomic<float>* pFormant = nullptr;
    std::atomic<float>* pTranspose = nullptr;
    std::atomic<float>* pMix = nullptr;
    std::atomic<float>* pBypass = nullptr;
    std::atomic<float>* pRealtime = nullptr;
    std::atomic<float>* pRange = nullptr;

    double currentRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VSTMTuneProcessor)
};
