#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include "dsp/TuneEngine.h"
#include "PresetManager.h"
#include "Authorization.h"
#include "MidiLearn.h"

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
    void processBlock (juce::AudioBuffer<double>&, juce::MidiBuffer&) override;     // 1.5: 64-bit hosts (converted)
    bool supportsDoublePrecisionProcessing() const override { return true; }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                     { return true; }

    const juce::String getName() const override         { return "VSTM Tune"; }
    bool acceptsMidi() const override                   { return true; }      // 1.5: MIDI target notes
    bool producesMidi() const override                  { return true; }      // 1.5: notes as MIDI (when switched on)
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
    vstm::MidiLearn midiLearn { *this };      // 1.5 (roadmap 47 / 48): MIDI CC assignments (session)
    /** 1.4: signed admin authorization (Authorization.h). Message thread only. */
    vstm::AuthorizationManager auth;

    /** Editor size, as a fraction of the 1536 x 1024 design canvas. Saved with the session. */
    std::atomic<float> uiScale { 0.75f };

    //---------------------------------------------------------------- Auto key and presets
    // Session state, not parameters (nothing to automate): saved with the session (state version 2).
    // Called from the editor (message thread); the host is told that the plugin state changed.
    /** Lock the automatic key (an estimate from the vocal) to `key` 0..11 now. */
    void lockAutoKey (int key, float confidence);
    /** Forget the vocal evidence and the lock; listen again. */
    void reanalyzeAutoKey();
    /** Opt-in: after the lock, follow key changes (modulation) of the singing. Off by default. */
    void setFollowKeyChanges (bool follow);
    bool getFollowKeyChanges() const noexcept           { return followKeyChanges.load(); }
    /** Loading a preset keeps Key, Scale and the custom scale (default on). */
    void setPresetsKeepKey (bool keep);

    //---------------------------------------------------------------- admin (1.4)
    /** Expert engine switches of the developer panel. They only change while an admin authorization is
        valid, are never saved (session, preset) and are all on (= normal behaviour) otherwise. */
    enum class Expert { peakGuard = 0, backdateSwitch, freshAnalysis, swingKeep, count };
    /** Message thread. Ignored (false) without a valid admin authorization. */
    bool setExpert (Expert e, bool on);
    bool getExpert (Expert e) const noexcept       { return expert[(int) e].load (std::memory_order_relaxed); }
    /** All expert switches back on (called when the authorization goes away). */
    void resetExpert() noexcept;

    /** 1: VSTM Tune 1.0. 2: adds autoKeyLock, autoKeyConf, followKeyChanges, presetsKeepKey. */
    static constexpr int kStateVersion = 2;

private:
    void parameterChanged (const juce::String& parameterID, float newValue) override;
public:
    /** 1.6 Auto Character -> manual: the values ROBOTISM gives the managed controls now become their manual values,
        so the sound does not change. Called by the editor before it switches Auto Character off, and by the
        parameter listener when the host does (not while a session or preset is being restored). */
    void handOffCharacter();
private:
    std::atomic<bool> restoringState { false };
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
             pFormant, pTranspose, pMix, pBypass, pRealtime, pRange, pCustomScale, pAdaptive, pTuningRef, pProtectCons,
             pFormantFollow, pTransition, pTolerance, pConfidence, pSynthVib, pOctave, pMidiTarget, pMidiOut, pMidiOctave, pTuningSys, pScaleTr, pAttack, pRelease, pAutoChar;
    RawParam pScaleNote[12];
    RawParam pScaleBypass[12], pTuneCents[12];      // 1.5
    juce::AudioBuffer<float> doubleScratch;   // 1.5: 64-bit blocks are processed through this (allocated in prepareToPlay)
    std::array<uint8_t, 128> midiHeld {};     // 1.5: held MIDI notes (count per note, audio thread only)
    int midiOutNote = -1;                     // 1.5: the note last sent as MIDI (-1 = none sounding)
    std::array<int8_t, 128> heldOrder {};     // 1.5 MIDI perform: held notes, oldest first (audio thread only)
    int heldCount = 0;
    int voiceNote = -1;                       // 1.5: the note the output is playing now (engine note changes)
    juce::MidiBuffer midiChunk, midiChunkOut; // 1.5: MIDI of a 64-bit block longer than promised (preallocated)

    double currentRate = 44100.0;
    std::atomic<bool> expert[(int) Expert::count] { true, true, true, true };
    std::atomic<bool> followKeyChanges { false };
public:
    /** 1.6: authorized users save presets as the readable .drp (text) while this is on (preset menu; saved with
        the session). Without authorization it has no effect: presets are saved as the binary .drp. */
    std::atomic<bool> readablePresets { true };
    bool savesReadablePresets() const noexcept { return auth.isAdmin() && readablePresets.load(); }
private:

    void notifyStateChanged();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VSTMTuneProcessor)
};
