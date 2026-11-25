#pragma once
//==============================================================================
//  VSTM Tune kontrolleri: LookAndFeel, knob'lar, deger kutulu kaydiricilar,
//  acilir secici, preset cubugu, ikon dugmeleri, alt bar anahtarlari
//
//  Etkilesim (tum knob'larda ayni):  dikey surukle | Shift = ince ayar |
//  cift tik = varsayilan | tekerlek | degere tikla = yazarak gir | sag tik = host menusu
//  Klavye (Tab ile odak; odak halkasi yalniz klavyeyle gelince gorunur):
//    ok tuslari = %1 | Shift + ok = %0,1 | PageUp / PageDown = %10 | Home / End = uclar |
//    Delete / Backspace = varsayilan | secici ve anahtarlarda Enter / Bosluk
//==============================================================================
#include <juce_audio_processors/juce_audio_processors.h>
#include "Theme.h"
#include "Icons.h"
#include "../Lang.h"

namespace vui
{
//==============================================================================
class VLookAndFeel : public juce::LookAndFeel_V4
{
public:
    VLookAndFeel()
    {
        using namespace juce;
        const Colour menuBg (0xff17161c);
        setColour (PopupMenu::backgroundColourId, menuBg);
        setColour (PopupMenu::textColourId, col::text);
        setColour (PopupMenu::headerTextColourId, col::dim);
        setColour (PopupMenu::highlightedBackgroundColourId, Colour (0xff2b2546));
        setColour (PopupMenu::highlightedTextColourId, Colours::white);
        setColour (TooltipWindow::backgroundColourId, Colour (0xff1b1a21));
        setColour (TooltipWindow::textColourId, col::text);
        setColour (TooltipWindow::outlineColourId, Colour (0xff34333c));
        setColour (TextEditor::backgroundColourId, col::well);
        setColour (TextEditor::textColourId, col::text);
        setColour (TextEditor::highlightColourId, col::accent.withAlpha (0.45f));
        setColour (TextEditor::highlightedTextColourId, Colours::white);
        setColour (TextEditor::outlineColourId, col::wellEdge);
        setColour (TextEditor::focusedOutlineColourId, col::accent);
        setColour (CaretComponent::caretColourId, col::accentHi);
        setColour (AlertWindow::backgroundColourId, menuBg);
        setColour (AlertWindow::textColourId, col::text);
        setColour (AlertWindow::outlineColourId, col::cardEdge);
        setColour (TextButton::buttonColourId, col::raised);
        setColour (TextButton::buttonOnColourId, col::accent);
        setColour (TextButton::textColourOffId, col::text);
        setColour (TextButton::textColourOnId, Colours::white);
        setColour (ResizableWindow::backgroundColourId, col::bg);
        setColour (Label::textColourId, col::text);
        setColour (ScrollBar::thumbColourId, col::faint);
    }

    juce::Font getPopupMenuFont() override                       { return font (16.0f); }
    juce::Font getTextButtonFont (juce::TextButton&, int) override { return font (15.0f, Weight::medium); }
    juce::Font getAlertWindowTitleFont() override                { return font (18.0f, Weight::semibold); }
    juce::Font getAlertWindowMessageFont() override              { return font (15.0f); }
    juce::Font getAlertWindowFont() override                     { return font (15.0f); }
    juce::Font getLabelFont (juce::Label&) override              { return font (15.0f); }

    void drawPopupMenuBackground (juce::Graphics& g, int w, int h) override
    {
        g.fillAll (findColour (juce::PopupMenu::backgroundColourId));
        g.setColour (col::cardEdge);
        g.drawRect (0, 0, w, h, 1);
    }

    void drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                            bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text,
                            const juce::String& shortcutKeyText, const juce::Drawable* icon, const juce::Colour* textColour) override
    {
        if (isSeparator)
        {
            g.setColour (col::cardEdge);
            g.fillRect (area.reduced (10, 0).withHeight (1).withY (area.getCentreY()));
            return;
        }

        auto r = area.toFloat().reduced (4.0f, 1.0f);
        if (isHighlighted && isActive)
        {
            g.setColour (findColour (juce::PopupMenu::highlightedBackgroundColourId));
            g.fillRoundedRectangle (r, 6.0f);
        }

        if (isTicked)
        {
            const float d = 7.0f;
            g.setColour (col::accent);
            g.fillEllipse (r.getX() + 10.0f, r.getCentreY() - d * 0.5f, d, d);
        }

        auto tc = textColour != nullptr ? *textColour : col::text;
        if (! isActive) tc = col::faint;
        else if (isTicked) tc = col::accentHi;
        g.setColour (tc);
        g.setFont (getPopupMenuFont());
        g.drawFittedText (text, r.withTrimmedLeft (28.0f).withTrimmedRight (24.0f).toNearestInt(), juce::Justification::centredLeft, 1);

        if (hasSubMenu)
            icons::drawCentred (g, icons::chevron (true, 4.5f), { r.getRight() - 14.0f, r.getCentreY() }, col::dim, 1.6f);
        juce::ignoreUnused (shortcutKeyText, icon);
    }

    void getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int standardHeight, int& w, int& h) override
    {
        juce::ignoreUnused (standardHeight);
        if (isSeparator) { w = 50; h = 9; return; }
        h = 32;
        w = (int) textWidth (getPopupMenuFont(), text) + 64;
    }

    void drawPopupMenuSectionHeader (juce::Graphics& g, const juce::Rectangle<int>& area, const juce::String& name) override
    {
        drawText (g, vstm::i18n::upper (name), { (float) area.getX() + 14.0f, (float) area.getCentreY() + 2.0f },
                  font (12.0f, Weight::semibold, 0.12f), col::dim, juce::Justification::left);
    }

    juce::Rectangle<int> getTooltipBounds (const juce::String& tip, juce::Point<int> pos, juce::Rectangle<int> parent) override
    {
        const auto layout = tooltipLayout (tip);
        const int w = (int) layout.getWidth() + 24, h = (int) layout.getHeight() + 16;
        return juce::Rectangle<int> (pos.x > parent.getCentreX() ? pos.x - (w + 12) : pos.x + 18,
                                     pos.y > parent.getCentreY() ? pos.y - (h + 8) : pos.y + 18, w, h)
            .constrainedWithin (parent);
    }

    void drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h) override
    {
        const auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h);
        g.setColour (findColour (juce::TooltipWindow::backgroundColourId));
        g.fillRoundedRectangle (r, 7.0f);
        g.setColour (findColour (juce::TooltipWindow::outlineColourId));
        g.drawRoundedRectangle (r.reduced (0.5f), 7.0f, 1.0f);
        tooltipLayout (text).draw (g, r.reduced (12.0f, 8.0f));
    }

    void drawCornerResizer (juce::Graphics& g, int w, int h, bool over, bool dragging) override
    {
        g.setColour ((over || dragging) ? col::dim : col::faint.withAlpha (0.6f));
        for (int i = 0; i < 3; ++i)
        {
            const float o = 4.0f + (float) i * 4.5f;
            g.drawLine ((float) w - o, (float) h - 3.0f, (float) w - 3.0f, (float) h - o, 1.2f);
        }
    }

    int getSliderThumbRadius (juce::Slider&) override { return 12; }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        auto c = col::raised;
        if (over) c = c.brighter (0.1f);
        if (down) c = c.darker (0.2f);
        g.setColour (c);
        g.fillRoundedRectangle (r, 7.0f);
        g.setColour (col::cardEdge.brighter (0.2f));
        g.drawRoundedRectangle (r, 7.0f, 1.0f);
    }

private:
    static juce::TextLayout tooltipLayout (const juce::String& text)
    {
        juce::AttributedString s;
        s.setJustification (juce::Justification::topLeft);
        s.append (text, font (14.0f), col::text);
        juce::TextLayout tl;
        tl.createLayoutWithBalancedLineLengths (s, 340.0f);
        return tl;
    }
};

//==============================================================================
/** Click-to-type value entry, shown in place over a value readout. */
class InlineEditor
{
public:
    void show (juce::Component& owner, juce::Rectangle<int> area, const juce::String& text, float fontHeight,
               std::function<void (const juce::String&)> commit)
    {
        editor = std::make_unique<juce::TextEditor>();
        auto* ed = editor.get();
        owner.addAndMakeVisible (ed);
        ed->setBounds (area);
        ed->setFont (font (fontHeight));
        ed->setJustification (juce::Justification::centred);
        ed->setIndents (6, juce::jmax (0, (area.getHeight() - (int) fontHeight) / 2 - 2));
        ed->setText (text, false);
        ed->selectAll();
        ed->grabKeyboardFocus();

        auto close = [this] { juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<juce::TextEditor> (editor.get()), this]
                                                               { if (safe != nullptr) editor.reset(); }); };
        ed->onReturnKey = [ed, commit, close] { commit (ed->getText()); close(); };
        ed->onEscapeKey = close;
        ed->onFocusLost = close;
    }

    bool isShowing() const noexcept { return editor != nullptr; }

private:
    std::unique_ptr<juce::TextEditor> editor;
};

//==============================================================================
/** Keyboard steps for a parameter slider (JUCE's own: 0.1 % per key and no host gesture). One key
    press = one complete host gesture, so automation records it like a mouse edit. Returns false
    for keys it does not use (they go on to the host: transport keys keep working). */
inline bool sliderKeyPressed (juce::Slider& s, juce::RangedAudioParameter* param, const juce::KeyPress& k)
{
    const int code = k.getKeyCode();
    const auto mods = k.getModifiers();
    if (mods.isCommandDown() || mods.isAltDown() || mods.isCtrlDown())
        return false;
    const bool fine = mods.isShiftDown();
    double target = -1.0;                                       // proportion of the range
    int dir = 0;
    const double now = s.valueToProportionOfLength (s.getValue());
    if (code == juce::KeyPress::upKey || code == juce::KeyPress::rightKey)        { target = now + (fine ? 0.001 : 0.01); dir = 1; }
    else if (code == juce::KeyPress::downKey || code == juce::KeyPress::leftKey)  { target = now - (fine ? 0.001 : 0.01); dir = -1; }
    else if (code == juce::KeyPress::pageUpKey)                                   { target = now + 0.1; dir = 1; }
    else if (code == juce::KeyPress::pageDownKey)                                 { target = now - 0.1; dir = -1; }
    else if (code == juce::KeyPress::homeKey)                                     target = 0.0;
    else if (code == juce::KeyPress::endKey)                                      target = 1.0;
    else if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey)
        target = s.valueToProportionOfLength (s.getDoubleClickReturnValue());
    else
        return false;

    const double cur = s.getValue();
    double v = s.proportionOfLengthToValue (juce::jlimit (0.0, 1.0, target));
    const double interval = s.getInterval();
    if (dir != 0 && interval > 0.0 && std::abs (v - cur) < 0.5 * interval)
        v = cur + dir * interval;                               // at least one step of the parameter's own resolution
    if (param != nullptr) param->beginChangeGesture();
    s.setValue (juce::jlimit (s.getMinimum(), s.getMaximum(), v), juce::sendNotificationSync);
    if (param != nullptr) param->endChangeGesture();
    return true;
}

/** Remembers whether keyboard focus came from the Tab key: only then a focus ring is drawn, so a
    mouse click (which also focuses, to take arrow keys) leaves the look as it was. */
struct TabFocus
{
    bool shown = false;
    void gained (juce::Component& c, juce::Component::FocusChangeType cause) { shown = cause == juce::Component::focusChangedByTabKey; c.repaint(); }
    void lost (juce::Component& c)                                           { shown = false; c.repaint(); }
};

//==============================================================================
/** Rotary slider with pro gestures: vertical drag, Shift = fine, wheel, double-click reset, keys. */
class ProKnob : public juce::Slider
{
public:
    ProKnob() : juce::Slider (juce::Slider::RotaryVerticalDrag, juce::Slider::NoTextBox)
    {
        setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        setMouseDragSensitivity (280);
        setVelocityBasedMode (false);
        setScrollWheelEnabled (true);
        setPopupMenuEnabled (false);
        setWantsKeyboardFocus (true);
    }

    std::function<void()> onContextMenu;
    /** The parameter behind the knob: keyboard edits are sent to the host as gestures. */
    juce::RangedAudioParameter* gestureParam = nullptr;

    bool keyPressed (const juce::KeyPress& k) override             { return sliderKeyPressed (*this, gestureParam, k); }
    void focusGained (FocusChangeType cause) override              { tabFocus.gained (*this, cause); }
    void focusLost (FocusChangeType) override                      { tabFocus.lost (*this); }
    bool showsFocusRing() const noexcept                           { return tabFocus.shown && hasKeyboardFocus (false); }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
        {
            if (onContextMenu) onContextMenu();
            return;
        }
        dragging = e.mods.isLeftButtonDown() && ! e.mods.isAltDown();
        lastPos = e.position;
        juce::Slider::mouseDown (e);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragging || ! isEnabled())
            return;
        const auto d = e.position - lastPos;
        lastPos = e.position;
        const float travel = d.x * 0.35f - d.y;
        const float pixels = e.mods.isShiftDown() ? 2200.0f : (float) getMouseDragSensitivity();
        const double p = juce::jlimit (0.0, 1.0, valueToProportionOfLength (getValue()) + (double) (travel / pixels));
        setValue (proportionOfLengthToValue (p), juce::sendNotificationSync);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        dragging = false;
        juce::Slider::mouseUp (e);
    }

    void mouseEnter (const juce::MouseEvent& e) override { juce::Slider::mouseEnter (e); repaint(); }
    void mouseExit (const juce::MouseEvent& e) override  { juce::Slider::mouseExit (e); repaint(); }

    float proportion() { return (float) valueToProportionOfLength (getValue()); }
    float angleFor (float p) const
    {
        const auto rp = getRotaryParameters();
        return rp.startAngleRadians + p * (rp.endAngleRadians - rp.startAngleRadians);
    }

protected:
    bool isDragging() const noexcept { return dragging && isMouseButtonDown(); }

private:
    juce::Point<float> lastPos;
    bool dragging = false;
    TabFocus tabFocus;
};

//==============================================================================
/** The four PARAMETERS knobs: steel-dark body, violet value arc, white pointer. */
class MiniKnobSlider : public ProKnob
{
public:
    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        const auto c = b.getCentre();
        const float k = juce::jmin (b.getWidth(), b.getHeight()) / 104.0f;
        const float p = proportion();
        const auto rp = getRotaryParameters();
        const float a0 = rp.startAngleRadians, a1 = rp.endAngleRadians;
        const float a = angleFor (p);
        const bool hot = isDragging();
        const bool over = isMouseOverOrDragging();

        // soft drop shadow
        {
            juce::ColourGradient sh (juce::Colours::black.withAlpha (0.65f), c.x, c.y + 5.0f * k,
                                     juce::Colours::transparentBlack, c.x, c.y + 5.0f * k + 52.0f * k, true);
            g.setGradientFill (sh);
            g.fillEllipse (c.x - 52.0f * k, c.y - 47.0f * k, 104.0f * k, 104.0f * k);
        }

        // dark outer band, unlit track and value arc on its outer edge
        const float rBand = 47.5f * k;
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1b1a20), c.x, c.y - rBand, juce::Colour (0xff0b0b0e), c.x, c.y + rBand, false));
        g.fillEllipse (c.x - rBand, c.y - rBand, rBand * 2.0f, rBand * 2.0f);

        const float rArc = 45.0f * k, wArc = 3.8f * k;
        juce::Path track;
        track.addCentredArc (c.x, c.y, rArc, rArc, 0.0f, a0, a1, true);
        g.setColour (juce::Colour (0xff2a2930));
        g.strokePath (track, icons::stroke (wArc));

        if (p > 0.002f)
        {
            juce::Path arc;
            arc.addCentredArc (c.x, c.y, rArc, rArc, 0.0f, a0, a, true);
            g.setColour (col::accent.withAlpha (0.10f));
            g.strokePath (arc, icons::stroke (wArc * 2.4f));
            juce::ColourGradient cg (juce::Colour (0xff7d5ff4), c.x - rArc, c.y + rArc,
                                     hot ? col::accentHi.brighter (0.2f) : juce::Colour (0xffb7a3fb), c.x, c.y - rArc, false);
            g.setGradientFill (cg);
            g.strokePath (arc, icons::stroke (wArc));
        }

        // body
        const float rB = 38.0f * k;
        g.setGradientFill (juce::ColourGradient (juce::Colour (over ? 0xff2c2b32 : 0xff26252b), c.x, c.y - rB,
                                                 juce::Colour (0xff0e0e11), c.x, c.y + rB, false));
        g.fillEllipse (c.x - rB, c.y - rB, rB * 2.0f, rB * 2.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.13f), c.x, c.y - rB,
                                                 juce::Colours::white.withAlpha (0.0f), c.x, c.y + rB * 0.3f, false));
        g.drawEllipse (c.x - rB, c.y - rB, rB * 2.0f, rB * 2.0f, 1.2f * k);
        const float rI = 32.0f * k;
        g.setColour (juce::Colour (0xff3b3a42).withAlpha (0.30f));
        g.drawEllipse (c.x - rI, c.y - rI, rI * 2.0f, rI * 2.0f, 1.0f * k);

        // pointer
        const auto at = [&] (float r) { return juce::Point<float> (c.x + r * std::sin (a), c.y - r * std::cos (a)); };
        g.setColour ((hot ? col::accentHi : juce::Colour (0xfff2f0f8)));
        g.drawLine (juce::Line<float> (at (19.0f * k), at (35.5f * k)), 3.0f * k);

        if (showsFocusRing())
        {
            g.setColour (col::accent.withAlpha (0.7f));
            g.drawEllipse (c.x - 50.0f * k, c.y - 50.0f * k, 100.0f * k, 100.0f * k, 1.0f);
        }
    }

    bool hitTest (int x, int y) override
    {
        return getLocalBounds().toFloat().getCentre().getDistanceFrom ({ (float) x, (float) y }) < (float) getWidth() * 0.5f;
    }
};

//==============================================================================
/** Knob + UPPERCASE label + value readout (click the value to type one). */
class MiniKnob : public juce::Component
{
public:
    MiniKnob (juce::RangedAudioParameter& p, const juce::String& labelText, const juce::String& tip)
        : param (p), label (labelText), attachment (std::make_unique<juce::SliderParameterAttachment> (p, knob))
    {
        addAndMakeVisible (knob);
        knob.gestureParam = &p;
        knob.setTooltip (tip);
        knob.setTitle (p.getName (64));
        knob.onValueChange = [this] { repaint (valueArea().toNearestInt().expanded (4)); };
        setTitle (p.getName (64));
    }

    MiniKnobSlider knob;
    juce::RangedAudioParameter& getParameter() const noexcept { return param; }

    /** 1.6 Auto Character: the knob shows a value the plugin sets (plainValue) and cannot be edited; its parameter
        keeps its own (manual) value meanwhile. Off: bound to the parameter again. */
    void setManaged (bool shouldBeManaged, float plainValue)
    {
        if (shouldBeManaged == managed && (! managed || std::abs (plainValue - managedValue) < 1.0e-4f))
            return;
        managed = shouldBeManaged;
        managedValue = plainValue;
        if (managed)
        {
            attachment.reset();
            knob.setValue (plainValue, juce::dontSendNotification);
        }
        else if (attachment == nullptr)
            attachment = std::make_unique<juce::SliderParameterAttachment> (param, knob);
        knob.setEnabled (! managed);
        repaint();
    }
    bool isManaged() const noexcept { return managed; }

    void resized() override
    {
        knob.setBounds (juce::Rectangle<int> (0, 0, 104, 104).withCentre ({ getWidth() / 2, 52 }));
    }

    void paint (juce::Graphics& g) override
    {
        const float cx = (float) getWidth() * 0.5f;
        drawText (g, label, { cx, 120.0f }, fitted (font (15.8f, Weight::medium, 0.05f), label, (float) getWidth() - 12.0f), col::label);
        if (! editor.isShowing())
            drawText (g, managed ? param.getText (param.convertTo0to1 (managedValue), 32) : param.getCurrentValueAsText(),
                      { cx, 151.0f }, font (23.0f), managed ? col::accentText : col::text);
        if (managed && ! editor.isShowing())
            drawAutoBadge (g, { cx + 30.0f + 0.5f * textWidth (font (23.0f), param.getText (param.convertTo0to1 (managedValue), 32)), 151.0f });
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! managed && valueArea().contains (e.position))
            editor.show (*this, valueArea().toNearestInt(), param.getCurrentValueAsText(), 21.0f, [this] (const juce::String& t)
            {
                const float v = param.getValueForText (t);
                param.beginChangeGesture();
                param.setValueNotifyingHost (v);
                param.endChangeGesture();
                repaint();
            });
    }

private:
    juce::Rectangle<float> valueArea() const { return { (float) getWidth() * 0.5f - 60.0f, 135.0f, 120.0f, 32.0f }; }

    juce::RangedAudioParameter& param;
    juce::String label;
    std::unique_ptr<juce::SliderParameterAttachment> attachment;
    InlineEditor editor;
    bool managed = false;
    float managedValue = 0.0f;
};

//==============================================================================
/** Horizontal track with violet fill and a lavender thumb (PITCH CORRECTION, RETUNE, HUMANIZE). */
class TrackSlider : public juce::Slider
{
public:
    TrackSlider() : juce::Slider (juce::Slider::LinearHorizontal, juce::Slider::NoTextBox)
    {
        setScrollWheelEnabled (true);
        setVelocityModeParameters (0.3, 1, 0.0, true, juce::ModifierKeys::shiftModifier);
        setPopupMenuEnabled (false);
        setWantsKeyboardFocus (true);
    }

    std::function<void()> onContextMenu;
    juce::RangedAudioParameter* gestureParam = nullptr;

    bool keyPressed (const juce::KeyPress& k) override             { return sliderKeyPressed (*this, gestureParam, k); }
    void focusGained (FocusChangeType cause) override              { tabFocus.gained (*this, cause); }
    void focusLost (FocusChangeType) override                      { tabFocus.lost (*this); }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) { if (onContextMenu) onContextMenu(); return; }
        juce::Slider::mouseDown (e);
    }
    void mouseEnter (const juce::MouseEvent& e) override { juce::Slider::mouseEnter (e); repaint(); }
    void mouseExit (const juce::MouseEvent& e) override  { juce::Slider::mouseExit (e); repaint(); }

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        const float r = 12.5f;
        const float trackH = 11.0f;
        const auto track = juce::Rectangle<float> (b.getX(), b.getCentreY() - trackH * 0.5f, b.getWidth(), trackH);
        const float p = (float) valueToProportionOfLength (getValue());
        const float tx = b.getX() + 12.0f + p * (b.getWidth() - 24.0f);
        const bool hot = isMouseButtonDown();

        // empty track
        g.setColour (juce::Colour (0xff09080c));
        g.fillRoundedRectangle (track, trackH * 0.5f);
        g.setColour (juce::Colour (0xff1c1b22));
        g.drawRoundedRectangle (track.reduced (0.5f), trackH * 0.5f, 1.0f);

        // fill
        auto fill = track.withRight (juce::jmax (track.getX() + trackH, tx));
        juce::ColourGradient cg (col::accentLo, fill.getX(), 0.0f, col::accentHi, fill.getRight(), 0.0f, false);
        g.setColour (col::accent.withAlpha (0.10f));
        g.fillRoundedRectangle (fill.expanded (2.0f, 4.0f), trackH);
        g.setColour (col::accent.withAlpha (0.16f));
        g.fillRoundedRectangle (fill.expanded (1.0f, 2.0f), trackH);
        g.setGradientFill (cg);
        g.fillRoundedRectangle (fill, trackH * 0.5f);
        g.setColour (juce::Colours::white.withAlpha (0.18f));
        g.fillRoundedRectangle (fill.withHeight (2.0f).translated (0.0f, 1.5f).reduced (trackH * 0.5f, 0.0f), 1.0f);

        // thumb
        const auto c = juce::Point<float> (tx, b.getCentreY());
        juce::ColourGradient sh (juce::Colours::black.withAlpha (0.6f), c.x, c.y + 3.0f, juce::Colours::transparentBlack, c.x, c.y + 3.0f + r * 1.6f, true);
        g.setGradientFill (sh);
        g.fillEllipse (c.x - r * 1.6f, c.y + 3.0f - r * 1.6f, r * 3.2f, r * 3.2f);
        g.setGradientFill (juce::ColourGradient (hot ? juce::Colour (0xffeeeaff) : juce::Colour (0xffdcd8f2), c.x, c.y - r,
                                                 juce::Colour (0xffa9a3cf), c.x, c.y + r, false));
        g.fillEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f);
        g.setColour (juce::Colours::white.withAlpha (isMouseOverOrDragging() ? 0.75f : 0.45f));
        g.drawEllipse (c.x - r + 0.6f, c.y - r + 0.6f, r * 2.0f - 1.2f, r * 2.0f - 1.2f, 1.0f);
        g.setColour (juce::Colour (0xff5b5678).withAlpha (0.6f));
        g.drawEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f, 0.8f);
        if (tabFocus.shown && hasKeyboardFocus (false))
        {
            g.setColour (col::accent.withAlpha (0.8f));
            g.drawEllipse (c.x - r - 4.0f, c.y - r - 4.0f, r * 2.0f + 8.0f, r * 2.0f + 8.0f, 1.2f);
        }
    }

private:
    TabFocus tabFocus;
};

/** Value box (inset well) above a TrackSlider. */
class ValueSlider : public juce::Component
{
public:
    enum class Align { left, centre };

    ValueSlider (juce::RangedAudioParameter& p, Align a, const juce::String& tip)
        : param (p), align (a), attachment (std::make_unique<juce::SliderParameterAttachment> (p, slider))
    {
        addAndMakeVisible (slider);
        slider.gestureParam = &p;
        slider.setTooltip (tip);
        slider.setTitle (p.getName (64));
        slider.onValueChange = [this] { repaint (boxArea().toNearestInt()); };
        setTitle (p.getName (64));
    }

    TrackSlider slider;

    /** 1.6 Auto Character: shows a value the plugin sets (plainValue), not editable; the parameter keeps its own
        (manual) value meanwhile. Off: bound to the parameter again. */
    void setManaged (bool shouldBeManaged, float plainValue)
    {
        if (shouldBeManaged == managed && (! managed || std::abs (plainValue - managedValue) < 1.0e-4f))
            return;
        managed = shouldBeManaged;
        managedValue = plainValue;
        if (managed)
        {
            attachment.reset();
            slider.setValue (plainValue, juce::dontSendNotification);
        }
        else if (attachment == nullptr)
            attachment = std::make_unique<juce::SliderParameterAttachment> (param, slider);
        slider.setEnabled (! managed);
        repaint();
    }
    bool isManaged() const noexcept { return managed; }

    /** box = value well, track = slider (both in this component's coordinates) */
    void setLayout (juce::Rectangle<int> box, juce::Rectangle<int> track)
    {
        boxR = box;
        slider.setBounds (track.withSizeKeepingCentre (track.getWidth(), 34));
    }

    void paint (juce::Graphics& g) override
    {
        const auto box = boxArea();
        fillWell (g, box, 14.0f, hoverBox);
        if (editor.isShowing())
            return;

        // number big, unit small: "15 ms", "35%", "100%"
        auto text = managed ? param.getText (param.convertTo0to1 (managedValue), 32) : param.getCurrentValueAsText();
        if (managed)
            drawAutoBadge (g, { box.getRight() - 34.0f, box.getCentreY() });
        juce::String number = text, unit;
        const int split = text.indexOfAnyOf ("% m");
        if (split > 0)
        {
            number = text.substring (0, split).trim();
            unit = text.substring (split).trim();
        }
        const auto fn = font (31.0f);
        const auto fu = font (unit == "%" ? 24.0f : 22.0f);
        const float gap = unit == "%" ? 1.0f : 7.0f;
        const float wn = textWidth (fn, number), wu = unit.isEmpty() ? 0.0f : textWidth (fu, unit);
        const float total = wn + (unit.isEmpty() ? 0.0f : gap + wu);
        float x = align == Align::centre ? box.getCentreX() - total * 0.5f : box.getX() + 20.0f;
        const float cy = box.getCentreY();
        const auto valueColour = managed ? col::accentText : col::value;
        drawText (g, number, { x, cy }, fn, valueColour, juce::Justification::left);
        if (unit.isNotEmpty())
        {
            // unit sits on the number's baseline
            const float baseShift = (fn.getAscent() - fu.getAscent()) * 0.36f;
            drawText (g, unit, { x + wn + gap, cy + baseShift }, fu, valueColour.withAlpha (0.85f), juce::Justification::left);
        }
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const bool h = boxArea().contains (e.position);
        if (h != hoverBox) { hoverBox = h; repaint (boxArea().toNearestInt().expanded (2)); }
    }
    void mouseExit (const juce::MouseEvent&) override { if (hoverBox) { hoverBox = false; repaint(); } }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! managed && boxArea().contains (e.position))
            editor.show (*this, boxArea().reduced (6.0f).toNearestInt(), param.getCurrentValueAsText(), 26.0f, [this] (const juce::String& t)
            {
                param.beginChangeGesture();
                param.setValueNotifyingHost (param.getValueForText (t));
                param.endChangeGesture();
                repaint();
            });
    }

private:
    juce::Rectangle<float> boxArea() const { return boxR.toFloat(); }

    juce::RangedAudioParameter& param;
    Align align;
    std::unique_ptr<juce::SliderParameterAttachment> attachment;
    juce::Rectangle<int> boxR;
    InlineEditor editor;
    bool hoverBox = false;
    bool managed = false;
    float managedValue = 0.0f;
};

//==============================================================================
/** Inset drop-down bound to a choice parameter (KEY, SCALE). */
class DropSelector : public juce::Component, public juce::SettableTooltipClient
{
public:
    DropSelector (juce::RangedAudioParameter& p, const juce::String& tip)
        : param (p), attachment (p, [this] (float) { repaint(); })
    {
        attachment.sendInitialUpdate();
        setTooltip (tip);
        setTitle (p.getName (64));
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setWantsKeyboardFocus (true);
    }

    /** Optional dim text after the value, e.g. the detected key next to "Auto". */
    std::function<juce::String()> suffix;
    /** Optional: draw a padlock after the suffix (a locked automatic key). */
    std::function<bool()> suffixLocked;
    /** Optional separators: menu item indices after which a separator is inserted. */
    juce::Array<int> separatorsAfter;
    /** Optional text shown instead of the parameter's choice (e.g. "Custom"); empty = the choice.
        While it is shown, no choice is ticked in the menu. */
    std::function<juce::String()> textOverride;
    /** Optional extra menu items after the choices (ids >= kExtraItem; add your own separator) and what they do. */
    std::function<void (juce::PopupMenu&)> addExtraItems;
    std::function<void (int)> onExtraItem;
    /** Called after one of the parameter's choices was picked from the menu. */
    std::function<void (int)> onChoice;
    static constexpr int kExtraItem = 1000;

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat().reduced (0.5f);
        fillWell (g, r, 14.0f, isMouseOver());
        const auto choices = param.getAllValueStrings();
        const int idx = juce::roundToInt (param.convertFrom0to1 (param.getValue()));
        const auto over = textOverride ? textOverride() : juce::String();
        const auto text = over.isNotEmpty() ? over : choices[juce::jlimit (0, choices.size() - 1, idx)];
        const auto s = suffix ? suffix() : juce::String();
        const bool locked = suffixLocked && suffixLocked();
        const auto fs = font (17.0f);
        const float lockW = locked ? 20.0f : 0.0f;
        const float room = r.getWidth() - 25.0f - 62.0f - (s.isNotEmpty() ? textWidth (fs, s) + 10.0f : 0.0f) - lockW;
        const auto f = fitted (font (22.5f), text, room);       // long scale names in some languages
        drawText (g, text, { r.getX() + 25.0f, r.getCentreY() }, f, col::text, juce::Justification::left);
        float x = r.getX() + 25.0f + textWidth (f, text) + 10.0f;
        if (s.isNotEmpty())
        {
            drawText (g, s, { x, r.getCentreY() }, fs, col::accentText.withAlpha (0.9f), juce::Justification::left);
            x += textWidth (fs, s);
        }
        if (locked)
            icons::drawCentred (g, icons::padlock(), { x + 12.0f, r.getCentreY() - 0.5f }, col::accentText.withAlpha (0.9f), 1.5f);
        icons::drawCentred (g, icons::chevronDown(), { r.getRight() - 36.0f, r.getCentreY() }, isMouseOver() ? col::text : col::label, 2.2f);
        if (tabFocus.shown && hasKeyboardFocus (false))
        {
            g.setColour (col::accent.withAlpha (0.8f));
            g.drawRoundedRectangle (r.reduced (1.0f), 13.0f, 1.2f);
        }
    }

    bool keyPressed (const juce::KeyPress& k) override
    {
        const int code = k.getKeyCode();
        if (code == juce::KeyPress::returnKey || code == juce::KeyPress::spaceKey || code == juce::KeyPress::downKey)
        {
            showMenu();
            return true;
        }
        return false;
    }
    void focusGained (FocusChangeType cause) override   { tabFocus.gained (*this, cause); }
    void focusLost (FocusChangeType) override           { tabFocus.lost (*this); }

    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { repaint(); }

    void mouseDown (const juce::MouseEvent&) override { showMenu(); }

    void showMenu()
    {
        juce::PopupMenu m;
        const auto choices = param.getAllValueStrings();
        const int idx = juce::roundToInt (param.convertFrom0to1 (param.getValue()));
        const bool overridden = textOverride && textOverride().isNotEmpty();
        for (int i = 0; i < choices.size(); ++i)
        {
            m.addItem (i + 1, choices[i], true, i == idx && ! overridden);
            if (separatorsAfter.contains (i))
                m.addSeparator();
        }
        if (addExtraItems)
            addExtraItems (m);
        juce::Component::SafePointer<DropSelector> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMinimumWidth (getWidth()).withStandardItemHeight (32),
                         [safe] (int result)
                         {
                             if (safe == nullptr || result <= 0)
                                 return;
                             if (result >= kExtraItem)
                             {
                                 if (safe->onExtraItem) safe->onExtraItem (result);
                                 return;
                             }
                             safe->attachment.setValueAsCompleteGesture ((float) (result - 1));
                             if (safe->onChoice) safe->onChoice (result - 1);
                         });
    }

private:
    juce::RangedAudioParameter& param;
    juce::ParameterAttachment attachment;
    TabFocus tabFocus;
};

//==============================================================================
class IconButton : public juce::Component, public juce::SettableTooltipClient
{
public:
    IconButton (juce::Path p, float strokeWidth = 2.1f, bool boxed = false)
        : path (std::move (p)), width (strokeWidth), box (boxed)
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setWantsKeyboardFocus (false);
    }

    std::function<void()> onClick;
    bool lit = false;

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        const bool over = isMouseOver();
        if (box)
        {
            const auto br = r.withSizeKeepingCentre (36.0f, 36.0f);
            g.setColour (lit ? col::accent.withAlpha (0.14f) : juce::Colour (0xff141319));
            g.fillRoundedRectangle (br, 6.0f);
            g.setColour (lit ? col::accent.withAlpha (0.55f) : juce::Colour (0xff26252c));
            g.drawRoundedRectangle (br.reduced (0.5f), 6.0f, 1.0f);
        }
        const auto c = lit ? col::accentHi : (over ? col::text : juce::Colour (0xff9a99a2));
        icons::drawCentred (g, path, r.getCentre(), c, width);
    }

    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { repaint(); }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (getLocalBounds().contains (e.getPosition()) && onClick)
            onClick();
    }

private:
    juce::Path path;
    float width;
    bool box;
};

//==============================================================================
/** "i" pill next to the ROBOTISM title. */
class InfoButton : public juce::Component, public juce::SettableTooltipClient
{
public:
    InfoButton() { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    std::function<void()> onClick;

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat().withSizeKeepingCentre (36.0f, 30.0f);
        g.setColour (isMouseOver() ? juce::Colour (0xff27262e) : juce::Colour (0xff1c1b22));
        g.fillRoundedRectangle (r, 11.0f);
        drawText (g, "i", r.getCentre().translated (0.0f, 0.5f), font (19.0f, Weight::medium), isMouseOver() ? col::text : col::label);
    }
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { repaint(); }
    void mouseUp (const juce::MouseEvent& e) override { if (getLocalBounds().contains (e.getPosition()) && onClick) onClick(); }
};

//==============================================================================
/** 1.6 Auto Character: a lit pill next to the ROBOTISM title ("AUTO"). onToggle decides what a click does (the
    editor hands the auto values over before switching off); the state follows the parameter (host automation). */
class AutoPill : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit AutoPill (juce::RangedAudioParameter& p) : param (p), attachment (p, [this] (float) { repaint(); })
    {
        attachment.sendInitialUpdate();
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setTitle (p.getName (64));
        setWantsKeyboardFocus (true);
    }
    std::function<void (bool turnOn)> onToggle;
    bool isOn() const { return param.getValue() > 0.5f; }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat().withSizeKeepingCentre ((float) getWidth() - 4.0f, 30.0f);
        const bool on = isOn(), over = isMouseOver();
        g.setColour (on ? col::accent.withAlpha (over ? 0.30f : 0.22f) : (over ? juce::Colour (0xff27262e) : juce::Colour (0xff1c1b22)));
        g.fillRoundedRectangle (r, 11.0f);
        if (on)
        {
            g.setColour (col::accent.withAlpha (0.75f));
            g.drawRoundedRectangle (r.reduced (0.5f), 11.0f, 1.2f);
        }
        const float dotX = r.getX() + 13.0f;
        if (on) fillGlowCircle (g, { dotX, r.getCentreY() }, 4.2f, juce::Colour (0xffb59bfd), 0.9f);
        else { g.setColour (juce::Colour (0xff3a3942)); g.fillEllipse (dotX - 4.2f, r.getCentreY() - 4.2f, 8.4f, 8.4f); }
        drawText (g, "AUTO", { dotX + 11.0f, r.getCentreY() + 0.5f }, font (13.6f, Weight::medium, 0.12f),
                  on ? col::accentText : (over ? col::text : col::label), juce::Justification::left);
        if (tabFocus.shown && hasKeyboardFocus (false))
        {
            g.setColour (col::accent.withAlpha (0.8f));
            g.drawRoundedRectangle (r.expanded (2.0f), 13.0f, 1.2f);
        }
    }
    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k.getKeyCode() != juce::KeyPress::returnKey && k.getKeyCode() != juce::KeyPress::spaceKey)
            return false;
        toggle();
        return true;
    }
    void focusGained (FocusChangeType cause) override   { tabFocus.gained (*this, cause); }
    void focusLost (FocusChangeType) override           { tabFocus.lost (*this); }
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { repaint(); }
    void mouseUp (const juce::MouseEvent& e) override  { if (getLocalBounds().contains (e.getPosition())) toggle(); }

private:
    void toggle()
    {
        const bool turnOn = ! isOn();
        if (onToggle) onToggle (turnOn);
        else attachment.setValueAsCompleteGesture (turnOn ? 1.0f : 0.0f);
    }
    juce::RangedAudioParameter& param;
    juce::ParameterAttachment attachment;
    TabFocus tabFocus;
};

//==============================================================================
/** Footer status toggles: lit dot + tracked caps text (AUTOTUNE ON / REALTIME). */
class FooterToggle : public juce::Component, public juce::SettableTooltipClient
{
public:
    FooterToggle (juce::RangedAudioParameter& p, bool inverted, bool dotLeft, juce::String onText, juce::String offText)
        : param (p), invert (inverted), leftDot (dotLeft), textOn (std::move (onText)), textOff (std::move (offText)),
          attachment (p, [this] (float) { repaint(); })
    {
        attachment.sendInitialUpdate();
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setTitle (p.getName (64));
        setWantsKeyboardFocus (true);
    }

    bool isOn() const { return (param.getValue() > 0.5f) != invert; }

    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k.getKeyCode() != juce::KeyPress::returnKey && k.getKeyCode() != juce::KeyPress::spaceKey)
            return false;
        attachment.setValueAsCompleteGesture (param.getValue() > 0.5f ? 0.0f : 1.0f);
        return true;
    }
    void focusGained (FocusChangeType cause) override   { tabFocus.gained (*this, cause); }
    void focusLost (FocusChangeType) override           { tabFocus.lost (*this); }

    /** Width this toggle needs for its longer text plus the dot and margins (layout follows the language). */
    float preferredWidth() const
    {
        const auto f = labelFont();
        return juce::jmax (textWidth (f, textOn), textWidth (f, textOff)) + (leftDot ? 55.0f + 16.0f : 54.0f + 16.0f);
    }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        const bool on = isOn();
        const bool over = isMouseOver();
        const auto dotC = on ? col::accent : juce::Colour (0xff3a3942);
        const float cy = r.getCentreY();
        const float dotX = leftDot ? r.getX() + 18.0f : r.getRight() - 18.0f;
        if (on) fillGlowCircle (g, { dotX, cy }, 5.6f, juce::Colour (0xffb59bfd), 0.9f);
        else { g.setColour (dotC); g.fillEllipse (dotX - 5.6f, cy - 5.6f, 11.2f, 11.2f); }

        const auto f = labelFont();
        auto tc = on ? (leftDot ? col::accentText : juce::Colour (0xff8f8d98)) : col::faint;
        if (over) tc = tc.brighter (0.25f);
        const auto& text = on ? textOn : textOff;
        if (leftDot)
            drawText (g, text, { r.getX() + 55.0f, cy }, f, tc, juce::Justification::left);
        else
            drawText (g, text, { r.getRight() - 54.0f, cy }, f, tc, juce::Justification::right);
        if (tabFocus.shown && hasKeyboardFocus (false))
        {
            g.setColour (col::accent.withAlpha (0.8f));
            g.drawRoundedRectangle (r.reduced (2.0f), r.getHeight() * 0.5f - 2.0f, 1.2f);
        }
    }

    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { repaint(); }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (getLocalBounds().contains (e.getPosition()))
            attachment.setValueAsCompleteGesture (param.getValue() > 0.5f ? 0.0f : 1.0f);
    }

private:
    juce::Font labelFont() const { return font (leftDot ? 16.6f : 14.6f, Weight::medium, 0.14f); }

    juce::RangedAudioParameter& param;
    bool invert, leftDot;
    juce::String textOn, textOff;
    juce::ParameterAttachment attachment;
    TabFocus tabFocus;
};
} // namespace vui
