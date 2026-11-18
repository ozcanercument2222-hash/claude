#pragma once
#include "PluginProcessor.h"
#include "ui/Controls.h"
#include "ui/RobotKnob.h"
#include "ui/PitchDisplay.h"
#include "ui/ScaleEditor.h"

//==============================================================================
/** Preset pill: < name >. Click the name for the full list, Save / Delete. */
class PresetBar : public juce::Component
{
public:
    explicit PresetBar (vstm::PresetManager& pm) : presets (pm) { setTitle ("Presets"); }

    std::function<void()> onSaveRequest;

    void refresh()
    {
        const auto name = presets.getCurrentName();
        const bool dirty = presets.isDirty();
        if (name != shownName || dirty != shownDirty) { shownName = name; shownDirty = dirty; repaint(); }
    }

    void paint (juce::Graphics& g) override;
    void mouseMove (const juce::MouseEvent& e) override  { const int z = zoneAt (e.x); if (z != hoverZone) { hoverZone = z; repaint(); } }
    void mouseExit (const juce::MouseEvent&) override     { hoverZone = -1; repaint(); }
    void mouseDown (const juce::MouseEvent& e) override;

private:
    int zoneAt (int x) const { return x < 68 ? 0 : (x > getWidth() - 68 ? 2 : 1); }
    void showMenu();

    vstm::PresetManager& presets;
    juce::String shownName;
    bool shownDirty = false;
    int hoverZone = -1;
};

//==============================================================================
/** Everything lives on a 1536 x 1024 design canvas that is scaled to the window. */
class Canvas : public juce::Component
{
public:
    void paint (juce::Graphics& g) override;
    static void drawBackground (juce::Graphics& g);

private:
    juce::Image cache;
    float cacheScale = 0.0f;
};

//==============================================================================
class VSTMTuneEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit VSTMTuneEditor (VSTMTuneProcessor&);
    ~VSTMTuneEditor() override;

    void resized() override;
    void paint (juce::Graphics& g) override { g.fillAll (vui::col::outside); }

    /** Screenshot / test helpers. */
    vui::PitchDisplay& getDisplay() { return display; }
    vui::RobotKnob& getRobotKnob() { return robot; }
    void showCustomScaleEditor() { setCustomScale (true); openScaleEditor(); }
    /** Closes the custom scale call-out if it is open. */
    void closeScaleEditor();

private:
    void timerCallback() override;
    void showSizeMenu();
    void showSettingsMenu();
    void showRobotismInfo();
    void openScaleEditor();
    void setCustomScale (bool on);
    void askPresetName();
    void showHostMenu (juce::RangedAudioParameter& p);
    juce::RangedAudioParameter& param (const juce::String& id) { return *audio.apvts.getParameter (id); }

    VSTMTuneProcessor& audio;
    vui::VLookAndFeel lnf;

    Canvas canvas;
    PresetBar presetBar;
    vui::IconButton saveButton, sizeButton, settingsButton;
    vui::PitchDisplay display;
    vui::DropSelector keySelector, scaleSelector;
    vui::ValueSlider correction, retune, humanize;
    vui::RobotKnob robot;
    juce::SliderParameterAttachment robotAttachment;
    vui::InfoButton infoButton;
    vui::MiniKnob vibratoKnob, formantKnob, transposeKnob, mixKnob;
    vui::FooterToggle autotuneToggle, realtimeToggle, adaptiveToggle;
    vui::IconButton waveButton;
    juce::TooltipWindow tooltips { this, 650 };

    bool lastActive = true;
    int lastDetectedKey = -2;
    bool lastCustom = false;
    // The custom scale call-out and its content are owned here and deleted with the editor.
    // (CallOutBox::launchAsynchronously owns its box inside a JUCE object: the editor can neither
    // delete that box nor be sure it is gone before the editor itself is.)
    std::unique_ptr<vui::ScaleNoteEditor> scaleContent;
    std::unique_ptr<juce::CallOutBox> scaleCallout;
    static constexpr float kSureKey = 0.5f;      // Auto key confidence below this shows "?"

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VSTMTuneEditor)
};
