#ifndef SCC_PLUGIN_EDITOR_H_INCLUDED
#define SCC_PLUGIN_EDITOR_H_INCLUDED

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <functional>
#include <memory>
#include "PluginProcessor.h"
#include "UIComponents.h"
#include "CharacterDesigner.h"

//==============================================================================
//  Ana ekran: 960 x 736 sabit tasarim alani. Editor pencereyi buyutunce olceklenir.
//
//    ust       : logo, kompresor PRESET'i, BYPASS, MODE (ADAPTIVE / ANALOG)
//    orta      : metreler, threshold gostergesi, ana knob, dalga formu
//    alt cubuk : ratio + secenekler (moda gore degisir)
//    en alt    : CHARACTER seridi (ON, AMOUNT, tip, A/B, DESIGNER)
//
//  ADAPTIVE: ana knob AMOUNT'tur; attack / release / knee otomatik, kutularda okunur.
//  ANALOG  : ana knob THRESHOLD'dur; attack / release / knee / makeup elle ayarlanir.
//==============================================================================
class MainView : public juce::Component,
                 private juce::Timer
{
public:
    static constexpr int kW = 960;
    static constexpr int kH = 736;

    explicit MainView (SCCAudioProcessor& p);
    ~MainView() override;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void visibilityChanged() override;

    std::function<void()> onOpenDesigner;

private:
    void timerCallback() override;
    void commitCustomRatio();
    void showPresetMenu();
    void applyMode (bool analog);

    SCCAudioProcessor& proc;

    // ---- kompresor (V5)
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

    // ---- ust cubuk: preset secici, bypass, calisma modu
    sccui::ChevronButton presetPrev { sccui::ChevronButton::PointLeft };
    sccui::ChevronButton presetNext { sccui::ChevronButton::PointRight };
    sccui::ReadoutBox    presetBox  { 14.0f };
    sccui::NeonButton    bypassButton   { "BYPASS", 14.0f, true };
    sccui::NeonButton    adaptiveButton { "ADAPTIVE", 12.5f };
    sccui::NeonButton    analogButton   { "ANALOG",   12.5f };

    // ---- Analog Mode (manuel) kontrolleri
    juce::Slider thresholdSlider;
    sccui::LabeledKnob attackKnob  { "ATTACK" };
    sccui::LabeledKnob releaseKnob { "RELEASE" };
    sccui::LabeledKnob kneeKnob    { "KNEE" };
    sccui::LabeledKnob makeupKnob  { "MAKEUP" };
    std::array<std::unique_ptr<sccui::NeonButton>, scc::kNumAnalogRatios> analogRatioButtons;

    // ---- CHARACTER seridi
    sccui::NeonButton  charOnButton    { "ON",    12.5f, true };
    sccui::NeonButton  charMatchButton { "MATCH", 12.0f, true };
    sccui::LabeledKnob charKnob        { "AMOUNT" };
    std::array<std::unique_ptr<sccui::NeonButton>, scc::kNumCharacterTypes> typeButtons;
    sccui::InfoText    typeInfo        { 11.0f };
    sccui::NeonButton  slotAButton     { "A", 14.0f };
    sccui::NeonButton  slotBButton     { "B", 14.0f };
    sccui::NeonButton  designerButton  { "CHARACTER DESIGNER", 11.5f };

    std::atomic<float>* amountParam  = nullptr;
    std::atomic<float>* ratioParam   = nullptr;
    std::atomic<float>* modeParam    = nullptr;
    std::atomic<float>* bypassParam  = nullptr;
    std::atomic<float>* analogParam  = nullptr;
    std::atomic<float>* aThrParam    = nullptr;
    std::atomic<float>* aRatioParam  = nullptr;
    std::atomic<float>* aKneeParam   = nullptr;
    std::atomic<float>* charOnParam  = nullptr;
    std::atomic<float>* charAmtParam = nullptr;

    float meterInDb[2]  { -90.0f, -90.0f };
    float meterOutDb[2] { -90.0f, -90.0f };
    float shownRatio = -1.0f;
    int   shownAnalog = -1;             // -1: henuz uygulanmadi

    // attachment'lar en sonda: once onlar yok edilsin
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> amountAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> thresholdAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> autoGainAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> lowCutAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> charOnAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> charMatchAttach;

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

    void showDesigner (bool shouldShowDesigner);
    bool isDesignerShown() const noexcept { return designer.isVisible(); }

    static constexpr int kMinWidth = 600, kMaxWidth = 1920;

private:
    SCCAudioProcessor& proc;
    MainView view;
    DesignerPanel designer;
    bool constructed = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SCCAudioProcessorEditor)
};

#endif // SCC_PLUGIN_EDITOR_H_INCLUDED
