#ifndef SCC_PLUGIN_PROCESSOR_H_INCLUDED
#define SCC_PLUGIN_PROCESSOR_H_INCLUDED

#include <juce_audio_processors/juce_audio_processors.h>
#include "SCCEngine.h"
#include "CharacterPresetManager.h"

//==============================================================================
//  SCC V6 - Single Knob Compressor
//
//  Parametre kimlikleri (degismez):
//    V5      : amount, ratio, mode, autogain, lowcut, bypass
//    Analog  : analog, a_thr, a_ratio, a_attack, a_release, a_knee, a_makeup
//    Karakter: char_on, char_amt, char_slot, char_match
//    A slotu : ca_type, ca_harm, ca_even, ... (Character Designer'in 15 kontrolu)
//    B slotu : cb_type, cb_harm, cb_even, ...
//
//  V5 ile kaydedilmis oturumlar ve V5 otomasyonu aynen calisir: V5 kimlikleri,
//  araliklari ve varsayilanlari degismedi; yeni ozellikler varsayilan olarak
//  kapalidir (Analog Mode kapali, Character kapali).
//==============================================================================
namespace sccparams
{
    inline juce::String slotPrefix (int slot)             { return slot == 0 ? "ca_" : "cb_"; }
    inline juce::String slotTypeId (int slot)             { return slotPrefix (slot) + "type"; }
    inline juce::String slotParamId (int slot, int index) { return slotPrefix (slot) + scc::customParamInfo (index).id; }
}

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
    using juce::AudioProcessor::processBlock;

    // Host'un kendi bypass dugmesi plugin'in yumusak gecisli BYPASS'ina baglanir
    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParameter; }

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
    //  Arayuz icin ses motoruna erisim (yalnizca kilitsiz / atomik veriler okunur)
    //==========================================================================
    scc::EngineTelemetry& getTelemetry() noexcept       { return engine.telemetry; }
    const scc::AnalyzerFifo& getAnalyzerPre() const noexcept  { return engine.analyzerPre; }
    const scc::AnalyzerFifo& getAnalyzerPost() const noexcept { return engine.analyzerPost; }
    double getEngineSampleRate() const noexcept         { return currentSampleRate.load(); }

    //==========================================================================
    //  Kompresor hazir ayarlari (V5). Host'un program listesine eklenmez (host
    //  state'i ezmesin diye); sadece arayuzdeki PRESET secici kullanir.
    //==========================================================================
    static int getNumPresets();
    static const char* getPresetName (int index);
    int  getCurrentPreset() const noexcept { return currentPreset.load(); }
    void applyPreset (int index);
    bool isPresetModified() const;

    //==========================================================================
    //  Karakter slotlari (A / B) ve karakter presetleri. Hepsi mesaj is
    //  parcaciginda cagrilir; ses is parcacigi yalnizca parametreleri okur.
    //==========================================================================
    CharacterPresetManager& getCharacterPresets() noexcept { return characterPresets; }

    int  getActiveSlot() const noexcept;
    void setActiveSlot (int slot);

    scc::CharacterSpec getSlotSpec (int slot) const;
    void setSlotSpec (int slot, const scc::CharacterSpec& spec);

    juce::String getSlotPresetName (int slot) const;
    void setSlotPresetName (int slot, const juce::String& name);
    bool isSlotModified (int slot) const;            // slot, adini tasidigi presetten farkli mi

    void loadCharacterPreset (int slot, int presetIndex);
    void copySlot (int fromSlot, int toSlot);
    void swapSlots();

    //==========================================================================
    void setParameterReal (const juce::String& paramId, float value);

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    scc::EngineSettings readSettings() const noexcept;

    scc::Engine engine;
    CharacterPresetManager characterPresets;

    // V5
    std::atomic<float>* amountParam   = nullptr;
    std::atomic<float>* ratioParam    = nullptr;
    std::atomic<float>* modeParam     = nullptr;
    std::atomic<float>* autoGainParam = nullptr;
    std::atomic<float>* lowCutParam   = nullptr;
    std::atomic<float>* bypassParam   = nullptr;

    // Analog Mode
    std::atomic<float>* analogParam   = nullptr;
    std::atomic<float>* aThrParam     = nullptr;
    std::atomic<float>* aRatioParam   = nullptr;
    std::atomic<float>* aAttackParam  = nullptr;
    std::atomic<float>* aReleaseParam = nullptr;
    std::atomic<float>* aKneeParam    = nullptr;
    std::atomic<float>* aMakeupParam  = nullptr;

    // Character Engine
    std::atomic<float>* charOnParam    = nullptr;
    std::atomic<float>* charAmtParam   = nullptr;
    std::atomic<float>* charSlotParam  = nullptr;
    std::atomic<float>* charMatchParam = nullptr;
    std::atomic<float>* slotTypeParam[2] = { nullptr, nullptr };
    std::atomic<float>* slotCustomParam[2][scc::kNumCustomParams] = {};

    juce::AudioProcessorParameter* bypassParameter = nullptr;

    std::atomic<int>    currentPreset { 0 };
    std::atomic<double> currentSampleRate { 48000.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SCCAudioProcessor)
};

#endif // SCC_PLUGIN_PROCESSOR_H_INCLUDED
