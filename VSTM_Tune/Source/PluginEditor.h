#pragma once
#include "PluginProcessor.h"
#include "ui/Controls.h"
#include "ui/RobotKnob.h"
#include "ui/PitchDisplay.h"
#include "ui/ScaleEditor.h"
#include "ui/DeveloperPanel.h"

//==============================================================================
/** Preset pill: < name >. Click the name for the full list, Save / Delete. */
class PresetBar : public juce::Component
{
public:
    explicit PresetBar (vstm::PresetManager& pm) : presets (pm) { setTitle ("Presets"); }

    std::function<void()> onSaveRequest;
    std::function<void()> onRenameRequest;
    /** "Presets keep key and scale" was toggled (the owner saves it and tells the host). */
    std::function<void (bool)> onKeepKeyChanged;
    /** 1.4: import a .drp / .vstmpreset preset; import, remove an authorization file. */
    std::function<void()> onImportPreset, onImportAuthorization, onRemoveAuthorization;
    /** Empty unless an admin authorization is valid; then the status line shown in the menu. */
    std::function<juce::String()> authorizationStatus;
    std::function<bool()> isAuthorized, readableOn;          // 1.6 readable .drp (shown only when authorized)
    std::function<void (bool)> onReadableChanged;

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
/** Dim note right of the KEY label while the key is Auto: the key is an estimate from the vocal
    (the plugin does not hear the beat). */
class KeyHint : public juce::Component
{
public:
    KeyHint() { setInterceptsMouseClicks (false, false); }
    void setShown (bool s) { if (s != shown) { shown = s; repaint(); } }
    bool isShown() const noexcept { return shown; }
    void paint (juce::Graphics& g) override;

private:
    bool shown = false;
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
    /** The ROBOTISM "i" call-out (owned like the scale editor: it goes with the editor). */
    void showRobotismInfo();
    void closeRobotismInfo();
    /** What the KEY box shows after "Auto" ("· A", "· A? 41%", "· …", nothing) and whether a padlock follows. */
    juce::String keyStatusText() const;
    bool keyStatusLocked() const;
    /** The status line of the Auto key menu (tests: it must state the same confidence the engine holds). */
    juce::String autoKeyMenuStatus() const;
    bool isKeyHintShown() const noexcept { return keyHint.isShown(); }
    /** 1.4 developer panel (admin only). */
    void showDeveloperPanel (bool show);
    bool isDeveloperPanelShown() const noexcept { return devPanel != nullptr && devPanel->isVisible(); }
    /** Text for a rejected .drp file (translated); `authorization` selects the wrong-type wording. */
    static juce::String drpErrorText (vstm::drp::Error e, bool authorization);

private:
    void timerCallback() override;
    void showSizeMenu();
    void showSettingsMenu();
    void openScaleEditor();
    bool isAutoKey();
    void addAutoKeyItems (juce::PopupMenu& m);
    void autoKeyAction (int itemId);
    void askRenamePreset();
    void setCustomScale (bool on);
    void askPresetName();
    void showHostMenu (juce::RangedAudioParameter& p);
    void showCustomTuningDialog();          // 1.5 (roadmap 45): the 12 custom cents
    void importPreset();
    void importAuthorization();
    void removeAuthorization();
    juce::String authorizationStatusText() const;
    juce::RangedAudioParameter& param (const juce::String& id) { return *audio.apvts.getParameter (id); }

    VSTMTuneProcessor& audio;
    vui::VLookAndFeel lnf;

    Canvas canvas;
    PresetBar presetBar;
    vui::IconButton saveButton, sizeButton, settingsButton;
    vui::PitchDisplay display;
    vui::DropSelector keySelector, scaleSelector;
    KeyHint keyHint;
    vui::ValueSlider correction, retune, humanize;
    vui::RobotKnob robot;
    juce::SliderParameterAttachment robotAttachment;
    vui::InfoButton infoButton;
    vui::AutoPill autoPill;                 // 1.6 Auto Character (left of the ROBOTISM title)
    vui::MiniKnob vibratoKnob, formantKnob, transposeKnob, mixKnob;
    vui::FooterToggle autotuneToggle, realtimeToggle, adaptiveToggle;
    vui::IconButton waveButton;
    juce::TooltipWindow tooltips { this, 650 };

    bool lastActive = true;
    void updateAutoCharacter();             // 1.6: managed controls follow ROBOTISM while Auto Character is on
    int lastKeyShown = -1;
    bool lastCustom = false;
    // The custom scale call-out and its content are owned here and deleted with the editor.
    // (CallOutBox::launchAsynchronously owns its box inside a JUCE object: the editor can neither
    // delete that box nor be sure it is gone before the editor itself is.)
    std::unique_ptr<vui::ScaleNoteEditor> scaleContent;
    std::unique_ptr<juce::CallOutBox> scaleCallout;
    std::unique_ptr<juce::Label> infoContent;
    std::unique_ptr<juce::CallOutBox> infoCallout;
    std::unique_ptr<juce::FileChooser> chooser;            // must live while its async dialog is open
    std::unique_ptr<vui::DeveloperPanel> devPanel;
    int authCheckTicks = 0;
    // ids of the Auto key items in the KEY menu (after the 13 choices)
    enum { kLockNow = vui::DropSelector::kExtraItem + 11, kUseSuggestion, kReanalyze, kFollow };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VSTMTuneEditor)
};
