#ifndef SCC_CHARACTER_DESIGNER_H_INCLUDED
#define SCC_CHARACTER_DESIGNER_H_INCLUDED

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <functional>
#include <memory>
#include "PluginProcessor.h"
#include "UIComponents.h"
#include "AnalyzerViews.h"

//==============================================================================
//  CHARACTER DESIGNER  (ana ekranla ayni 960 x 736 tasarim alani)
//
//  Kompresor calismaya devam ederken karakter burada kurulur ve DAW'dan gelen
//  gercek ses uzerinde ANINDA duyulur: her knob dogrudan bir plugin parametresidir,
//  ses motoru o parametreyi bir sonraki blokta okur. Kaydetme / render gerekmez.
//
//    ust satir   : karakter presetleri (fabrika + kullanici), A / B, MATCH
//    tip satiri  : ON, yedi tip
//    orta        : gercek zamanli spektrum (giris / cikis), doygunluk egrisi,
//                  harmonik imza
//    alt         : Designer'in 15 kontrolu (5 grup) + global CHARACTER knob'u
//==============================================================================
class DesignerPanel : public juce::Component,
                      private juce::Timer
{
public:
    static constexpr int kW = 960;
    static constexpr int kH = 736;

    explicit DesignerPanel (SCCAudioProcessor& p);
    ~DesignerPanel() override;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void visibilityChanged() override;

    std::function<void()> onClose;      // "COMPRESSOR" dugmesi

private:
    enum class NameEdit { none, saveAs, rename };

    void timerCallback() override;
    void attachKnobsToSlot (int slot);
    void refreshProbe (bool force);

    // preset islemleri
    void showPresetMenu();
    void stepPreset (int delta);
    void savePreset();
    void beginNameEdit (NameEdit mode);
    void commitNameEdit();
    void cancelNameEdit();
    void deletePreset();
    void setStatus (const juce::String& text, bool isError = false);
    bool slotHoldsUserPreset (int slot) const;

    SCCAudioProcessor& proc;

    // ust satir
    sccui::NeonButton    backButton   { "COMPRESSOR", 12.5f };
    sccui::ChevronButton presetPrev   { sccui::ChevronButton::PointLeft };
    sccui::ChevronButton presetNext   { sccui::ChevronButton::PointRight };
    sccui::ReadoutBox    presetBox    { 13.5f };
    juce::TextEditor     nameEditor;
    sccui::NeonButton    saveButton   { "SAVE",    12.0f };
    sccui::NeonButton    saveAsButton { "SAVE AS", 12.0f };
    sccui::NeonButton    renameButton { "RENAME",  12.0f };
    sccui::NeonButton    deleteButton { "DELETE",  12.0f };
    sccui::NeonButton    slotAButton  { "A", 14.0f };
    sccui::NeonButton    slotBButton  { "B", 14.0f };
    sccui::NeonButton    copyButton   { "A > B", 12.0f };
    sccui::NeonButton    swapButton   { "SWAP",  12.0f };
    sccui::NeonButton    matchButton  { "MATCH", 12.0f, true };

    // tip satiri
    sccui::NeonButton    onButton     { "ON", 12.5f, true };
    std::array<std::unique_ptr<sccui::NeonButton>, scc::kNumCharacterTypes> typeButtons;
    sccui::InfoText      statusText   { 11.5f };

    // gorsellestirme
    sccui::SpectrumView     spectrum;
    sccui::CurveView        curve;
    sccui::HarmonicBarsView harmonics;
    sccui::ReadoutBox       thdBox { 13.5f }, evenBox { 13.5f }, oddBox { 13.5f }, matchBox { 13.5f };

    // kontroller
    std::array<std::unique_ptr<sccui::LabeledKnob>, scc::kNumCustomParams> knobs;
    sccui::LabeledKnob   amountKnob   { "AMOUNT" };
    sccui::LevelMeter    outMeter;
    sccui::ReadoutBox    grBox { 13.5f }, levelBox { 13.5f };
    sccui::InfoText      hintText { 10.5f, juce::Justification::topLeft };

    // durum
    int      shownSlot    = -1;
    int      shownType    = -1;
    NameEdit nameEdit     = NameEdit::none;
    int      deleteArmed  = 0;          // > 0 iken DELETE "SURE?" gosterir (zamanlayici adimi)
    int      statusTicks  = 0;
    int      probeTick    = 0;
    float    meterOutDb[2] { -90.0f, -90.0f };
    double   levelFast = 0.0, levelSlow = 0.0;

    // harmonik sondasi (ses yolundakiyle ayni CharacterEngine sinifi, ayri ornek)
    scc::CharacterProbe probe;
    scc::CharacterSpec  probeSpec;
    double probeIntensity = -1.0, probeLevelDb = 1000.0;
    bool   probeOn = false;

    // attachment'lar en sonda: once onlar yok edilsin
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> onAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> matchAttach;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DesignerPanel)
};

#endif // SCC_CHARACTER_DESIGNER_H_INCLUDED
