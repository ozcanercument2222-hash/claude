#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "UI/Icons.h"
#include "UI/Theme.h"

#include <functional>
#include <memory>

/** VEX Stutter controls. Every control is bound to a real parameter through a JUCE parameter
    attachment (gestures, automation and undo behave as the host expects); none holds a value of
    its own. Interaction is the same everywhere:
      drag (up / right increases), Shift + drag = fine, mouse wheel, double-click = default,
      right-click = menu (enter a value, default, and the host's own parameter menu). */
namespace vex::ui
{
/** Right-click menu for a parameter: type a value, reset, and the host's context-menu items. */
void showParameterMenu (juce::Component& owner, juce::RangedAudioParameter& param);

/** Opens a small text box over `owner` to type a value for `param` (Enter commits, Esc cancels). */
void editParameterText (juce::Component& owner, juce::RangedAudioParameter& param, juce::Rectangle<int> where);

//==============================================================================
/** Slider with relative drag, Shift = fine, wheel, double-click reset, right-click menu. */
class ParamSlider : public juce::Slider
{
public:
    ParamSlider (juce::RangedAudioParameter& p, juce::Slider::SliderStyle style);
    ~ParamSlider() override;

    juce::RangedAudioParameter& param;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

protected:
    float pixelsForFullRange = 240.0f;

private:
    std::unique_ptr<juce::SliderParameterAttachment> attachment;
    juce::Point<float> lastPos;
    bool dragging = false;
};

//==============================================================================
/** Rotary knob. hero: the Amount knob with its value inside; medium: Mix; mini: the FX knobs. */
class Knob final : public ParamSlider
{
public:
    enum class Size { hero, medium, mini };
    Knob (juce::RangedAudioParameter&, Size, const juce::String& caption = {});
    void paint (juce::Graphics&) override;

private:
    Size size;
    juce::String caption;
};

/** Horizontal slider: track, filled value (from the centre when bipolar), round thumb. */
class HSlider final : public ParamSlider
{
public:
    HSlider (juce::RangedAudioParameter&, bool bipolar = false);
    void paint (juce::Graphics&) override;
    void resized() override { pixelsForFullRange = (float) juce::jmax (80, getWidth() - 16); }

private:
    bool bipolar;
};

//==============================================================================
/** The parameter's value as text ("50.0%", "+12.5 ms"); double-click to type a value. */
class ValueText final : public juce::Component
{
public:
    ValueText (juce::RangedAudioParameter&, float fontSize = 14.0f, juce::Colour colour = col::text,
               juce::Justification just = juce::Justification::centredRight);
    void paint (juce::Graphics&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    juce::RangedAudioParameter& param;
    float fontSize;
    juce::Colour colour;
    juce::Justification just;
    juce::ParameterAttachment attachment;
};

//==============================================================================
/** "<  value  >" selector. A choice parameter steps through its choices (the centre opens the
    full list); a float parameter steps through `steps` (the centre accepts a typed value). */
class Selector final : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    explicit Selector (juce::RangedAudioParameter&, std::vector<float> floatSteps = {});
    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    std::function<juce::String (float)> formatter;     // optional display text for the value

private:
    void stepBy (int delta);
    void showList();
    void timerCallback() override;
    int zoneAt (juce::Point<float>) const;              // -1 left arrow, 0 centre, +1 right arrow

    juce::RangedAudioParameter& param;
    std::vector<float> steps;
    juce::ParameterAttachment attachment;
    int hoverZone = -2, pressedZone = -2, repeatCount = 0;
};

//==============================================================================
/** The five mode buttons (Stutter / Repeat / Gate / Reverse / Glitch) bound to the Mode parameter. */
class ModeSelector final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit ModeSelector (juce::RangedAudioParameter& modeParam);
    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    juce::String getTooltipFor (int index) const;

private:
    juce::Rectangle<float> buttonArea (int index) const;
    int indexAt (juce::Point<float>) const;

    juce::RangedAudioParameter& param;
    juce::ParameterAttachment attachment;
    int selected = 0, hover = -1;
};

//==============================================================================
/** On / off switch bound to a bool parameter. */
class Switch final : public juce::Button
{
public:
    explicit Switch (juce::RangedAudioParameter&);
    void paintButton (juce::Graphics&, bool hover, bool down) override;

private:
    juce::ButtonParameterAttachment attachment;
};

/** Text pill bound to a bool parameter (BYPASS, FREEZE, HOST / FREE). */
class PillToggle final : public juce::Button
{
public:
    PillToggle (juce::RangedAudioParameter&, const juce::String& onText, const juce::String& offText,
                juce::Colour onColour, icons::Id icon = icons::Id::close, bool showIcon = false);
    void paintButton (juce::Graphics&, bool hover, bool down) override;

    /** Freeze: on but not frozen yet (waiting for 1 s of audio). */
    bool pending = false;

private:
    juce::ButtonParameterAttachment attachment;
    juce::String onText, offText;
    juce::Colour onColour;
    icons::Id icon;
    bool showIcon;
};

/** Round power button: the inverse of Bypass (lit = processing). */
class PowerButton final : public juce::Button
{
public:
    explicit PowerButton (juce::RangedAudioParameter& bypass);
    void paintButton (juce::Graphics&, bool hover, bool down) override;
    using juce::Button::clicked;
    void clicked() override;

private:
    juce::RangedAudioParameter& bypass;
    juce::ParameterAttachment attachment;
};

/** Icon-only button (header: save, browse, settings; panel: edit, close). */
class IconButton final : public juce::Button
{
public:
    IconButton (const juce::String& name, icons::Id id, float iconScale = 0.62f);
    void paintButton (juce::Graphics&, bool hover, bool down) override;

private:
    icons::Id id;
    float iconScale;
};

//==============================================================================
/** Vertical peak meter: 20 dB/s fall, 1.5 s peak hold, -60 ... 0 dBFS, clip flag (click to reset). */
class LevelMeter final : public juce::Component, public juce::SettableTooltipClient
{
public:
    LevelMeter() { setInterceptsMouseClicks (true, false); }
    /** Feed the latest peak (linear) once per UI frame; returns true when a repaint is needed. */
    bool update (float peak, double dtSeconds);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { clipped = false; repaint(); }

private:
    float levelDb = -100.0f, holdDb = -100.0f;
    double holdLeft = 0.0;
    bool clipped = false;
};
}
