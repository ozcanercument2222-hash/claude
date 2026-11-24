#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "DSP/StutterEngine.h"
#include "Presets/PresetManager.h"

#include <atomic>
#include <cmath>

/** VEX Stutter audio processor: owns the parameters, the session state, the presets and the DSP
    engine. It contains no drawing code; the editor depends on the processor, never the reverse.

    Threads
      processBlock()                    audio thread: no allocation, no lock, no file I/O, no UI call
      prepareToPlay()                   host thread while audio is stopped: every buffer is allocated here
      get / setStateInformation         host or message thread, never the audio thread
      parameter values                  atomics inside the APVTS parameters: safe from any thread
      selection (setSelection)          message thread writes, audio thread reads (std::atomic)
      engine status, capture columns    audio thread writes, the editor reads (std::atomic only,
                                        see StutterEngine.h / CaptureBuffer.h)
      presets                           message thread only (PresetManager)
*/
class VEXStutterAudioProcessor final : public juce::AudioProcessor
{
public:
    static constexpr const char* kProductName = "VEX Stutter";

    /** Version of the saved state layout. Bump when it changes; loading must keep accepting every
        older version (missing parameters take their default). */
    static constexpr int kStateVersion = 1;

    VEXStutterAudioProcessor();
    ~VEXStutterAudioProcessor() override = default;

    // audio
    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;
    using juce::AudioProcessor::processBlockBypassed;

    // editor
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    // identity
    const juce::String getName() const override { return kProductName; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }

    /** The repeats of the current phrase plus the delay and reverb tails, from the current settings. */
    double getTailLengthSeconds() const override;

    // programs: presets are the PresetManager's job
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    // state
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    /** The Bypass parameter is the host's bypass: the DAW's bypass button, the plug-in's Bypass
        button and its power icon are one state and can never disagree (specification section 12). */
    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParam; }

    juce::AudioProcessorValueTreeState& getState() noexcept { return apvts; }
    double getCurrentSampleRate() const noexcept { return currentSampleRate; }

    /** Builds a clean parameter tree from untrusted saved data: unknown IDs are dropped, the first of
        duplicate IDs wins, missing or non-finite values take the default, every value is clamped
        and snapped to its parameter's range. Public for the tests and the preset manager. */
    juce::ValueTree sanitiseState (const juce::ValueTree& loaded) const;

    // ---- for the editor ------------------------------------------------------------------------
    /** Meters, timing and what is playing (atomics written by the audio thread). */
    vex::dsp::EngineStatus& getEngineStatus() noexcept { return engine.status; }
    /** Waveform columns of the capture (atomics; see CaptureBuffer). */
    const vex::dsp::CaptureBuffer& getCapture() const noexcept { return engine.getCapture(); }

    /** Part of the frozen capture the repeats read, 0..1 (message thread). Saved with the session,
        not with presets: it belongs to the audio captured in this session. */
    void setSelection (float start, float end) noexcept;
    float getSelectionStart() const noexcept { return selectionStart.load (std::memory_order_relaxed); }
    float getSelectionEnd() const noexcept   { return selectionEnd.load (std::memory_order_relaxed); }

    vex::presets::PresetManager& getPresetManager() noexcept { return presetManager; }

    /** Editor size as a factor of its design size (0.7 - 1.6); saved with the session. */
    static constexpr float kMinUiScale = 0.7f, kMaxUiScale = 1.6f;
    float getUiScale() const noexcept { return uiScale.load (std::memory_order_relaxed); }
    void setUiScale (float s) noexcept
    {
        if (std::isfinite (s))
            uiScale.store (juce::jlimit (kMinUiScale, kMaxUiScale, s), std::memory_order_relaxed);
    }

private:
    void runEngine (juce::AudioBuffer<float>&, bool hostBypass);
    vex::dsp::EngineParams readParams() const noexcept;
    vex::dsp::Transport readTransport() const;

    struct RawParams
    {
        std::atomic<float>* bypass = nullptr, *mode = nullptr, *amount = nullptr, *sync = nullptr, *rate = nullptr,
                          * rateMs = nullptr, *length = nullptr, *style = nullptr, *shape = nullptr, *smoothness = nullptr,
                          * offsetMs = nullptr, *mix = nullptr, *glitchType = nullptr, *intensity = nullptr, *freeze = nullptr,
                          * fxEnabled = nullptr, *delaySync = nullptr, *delayRate = nullptr, *delayTimeMs = nullptr,
                          * delayFeedback = nullptr, *delayMix = nullptr, *reverbSize = nullptr, *reverbDamping = nullptr,
                          * reverbMix = nullptr;
    };

    juce::AudioProcessorValueTreeState apvts;
    juce::AudioParameterBool* bypassParam = nullptr;
    RawParams raw;
    double currentSampleRate = 44100.0;

    vex::dsp::StutterEngine engine;
    std::atomic<bool> prepared { false };
    // default selection: the most recent quarter of the capture ("freeze what was just sung")
    static constexpr float kDefaultSelectionStart = 0.75f, kDefaultSelectionEnd = 1.0f;
    std::atomic<float> selectionStart { kDefaultSelectionStart }, selectionEnd { kDefaultSelectionEnd }, uiScale { 1.0f };
    vex::presets::PresetManager presetManager;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VEXStutterAudioProcessor)
};
