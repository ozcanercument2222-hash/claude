#ifndef SCC_PLUGIN_EDITOR_H_INCLUDED
#define SCC_PLUGIN_EDITOR_H_INCLUDED

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <memory>
#include "PluginProcessor.h"
#include "UIComponents.h"

//==============================================================================
//  960 x 600 sabit tasarim alani. Editor pencereyi buyutunce bu alan olceklenir.
//==============================================================================
class MainView : public juce::Component,
                 private juce::Timer
{
public:
    static constexpr int kW = 960;
    static constexpr int kH = 600;

    explicit MainView (SCCAudioProcessor& p);
    ~MainView() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;
    void setParamReal (const char* paramId, float value);
    void commitCustomRatio();
    void showPresetMenu();

    SCCAudioProcessor& proc;

    sccui::KnobLookAndFeel knobLnf;
    juce::Slider amountSlider;
    sccui::ScopeView scope;
    sccui::LevelMeter inMeter, outMeter;
    sccui::ThresholdDisplay thresholdDisplay;
    sccui::ValueText amountValue    { 30.0f };
    sccui::ValueText thresholdValue { 24.0f };
    sccui::ReadoutBox ratioBox, attackBox, releaseBox, kneeBox;
    std::array<std::unique_ptr<sccui::NeonButton>, 6> ratioButtons;
    juce::Label customLabel;
    sccui::ChevronButton ratioUp { true }, ratioDown { false };
    sccui::NeonButton autoGainButton { "AUTO GAIN", 12.5f, true };
    sccui::NeonButton lowCutButton   { "LOW CUT",   12.5f, true };
    std::array<std::unique_ptr<sccui::ModeButton>, 3> modeButtons;

    // ust cubuk: preset secici + bypass
    sccui::ChevronButton presetPrev { sccui::ChevronButton::PointLeft };
    sccui::ChevronButton presetNext { sccui::ChevronButton::PointRight };
    sccui::ReadoutBox    presetBox  { 14.0f };
    sccui::NeonButton    bypassButton { "BYPASS", 14.0f, true };

    std::atomic<float>* amountParam = nullptr;
    std::atomic<float>* ratioParam  = nullptr;
    std::atomic<float>* modeParam   = nullptr;
    std::atomic<float>* bypassParam = nullptr;

    float meterInDb[2]  { -90.0f, -90.0f };
    float meterOutDb[2] { -90.0f, -90.0f };
    float shownRatio = -1.0f;

    // attachment'lar en sonda: once onlar yok edilsin
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> amountAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> autoGainAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> lowCutAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttach;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainView)
};

//==============================================================================
class SCCAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit SCCAudioProcessorEditor (SCCAudioProcessor&);
    ~SCCAudioProcessorEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    SCCAudioProcessor& proc;
    MainView view;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SCCAudioProcessorEditor)
};

#endif // SCC_PLUGIN_EDITOR_H_INCLUDED
