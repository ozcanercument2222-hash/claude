#ifndef SCC_PLUGIN_PROCESSOR_H_INCLUDED
#define SCC_PLUGIN_PROCESSOR_H_INCLUDED

#include <juce_audio_processors/juce_audio_processors.h>
#include "SCCDsp.h"

//==============================================================================
//  SCC - Single Knob Compressor
//==============================================================================
class SCCAudioProcessor : public juce::AudioProcessor
{
public:
    SCCAudioProcessor();
    ~SCCAudioProcessor() override = default;

    //==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==========================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    //==========================================================================
    const juce::String getName() const override
    {
       #ifdef JucePlugin_Name
        return JucePlugin_Name;
       #else
        return "SCC";
       #endif
    }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override    { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    //==========================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==========================================================================
    scc::Telemetry& getTelemetry() noexcept { return compressor.telemetry; }

    //==========================================================================
    //  Dahili hazir ayarlar. Host'un program listesine eklenmez (host state'i
    //  ezmesin diye); sadece arayuzdeki PRESET secici kullanir.
    //==========================================================================
    static int getNumPresets();
    static const char* getPresetName (int index);
    int  getCurrentPreset() const noexcept { return currentPreset.load(); }
    void applyPreset (int index);
    bool isPresetModified() const;

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void setParameterReal (const char* paramId, float value);

    scc::Compressor compressor;

    std::atomic<float>* amountParam   = nullptr;
    std::atomic<float>* ratioParam    = nullptr;
    std::atomic<float>* modeParam     = nullptr;
    std::atomic<float>* autoGainParam = nullptr;
    std::atomic<float>* lowCutParam   = nullptr;
    std::atomic<float>* bypassParam   = nullptr;

    std::atomic<int> currentPreset { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SCCAudioProcessor)
};

#endif // SCC_PLUGIN_PROCESSOR_H_INCLUDED
