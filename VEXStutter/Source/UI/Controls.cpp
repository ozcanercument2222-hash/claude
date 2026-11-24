#include "UI/Controls.h"
#include "Parameters/Choices.h"

#include <algorithm>
#include <cmath>

namespace vex::ui
{
namespace
{
    constexpr float kStartAngle = juce::MathConstants<float>::pi * 1.25f;     // 7:30
    constexpr float kEndAngle   = juce::MathConstants<float>::pi * 2.75f;     // 4:30

    juce::Point<float> polar (juce::Point<float> c, float r, float angle)
    {
        return { c.x + r * std::sin (angle), c.y - r * std::cos (angle) };
    }

    juce::AudioProcessorEditor* findEditor (juce::Component& c)
    {
        return c.findParentComponentOfClass<juce::AudioProcessorEditor>();
    }

    /** Inline value editor: a small text box placed over its owner. */
    class InlineEditor final : public juce::TextEditor
    {
    public:
        InlineEditor (juce::RangedAudioParameter& p) : param (p)
        {
            setJustification (juce::Justification::centred);
            setFont (font (14.0f, Weight::medium));
            setColour (juce::TextEditor::backgroundColourId, col::well);
            setColour (juce::TextEditor::textColourId, col::text);
            setColour (juce::TextEditor::outlineColourId, col::accent);
            setColour (juce::TextEditor::focusedOutlineColourId, col::accentHi);
            setColour (juce::TextEditor::highlightColourId, col::accent.withAlpha (0.4f));
            setColour (juce::CaretComponent::caretColourId, col::accentHi);
            setText (p.getCurrentValueAsText(), false);
            selectAll();
            onReturnKey = [this] { commit(); };
            onEscapeKey = [this] { close(); };
            onFocusLost = [this] { commit(); };
        }

    private:
        void commit()
        {
            if (done)
                return;
            const auto text = getText().trim();
            if (text.isNotEmpty())
            {
                const float v = param.getValueForText (text);
                if (std::isfinite (v))
                {
                    param.beginChangeGesture();
                    param.setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, v));
                    param.endChangeGesture();
                }
            }
            close();
        }

        void close()
        {
            if (done)
                return;
            done = true;
            juce::Component::SafePointer<juce::Component> self (this);
            juce::MessageManager::callAsync ([self]
            {
                if (auto* c = self.getComponent())
                {
                    if (auto* parent = c->getParentComponent())
                        parent->removeChildComponent (c);
                    delete c;
                }
            });
        }

        juce::RangedAudioParameter& param;
        bool done = false;
    };
}

void editParameterText (juce::Component& owner, juce::RangedAudioParameter& param, juce::Rectangle<int> where)
{
    auto* host = owner.getParentComponent();
    if (host == nullptr)
        return;
    auto* ed = new InlineEditor (param);
    const auto r = host->getLocalArea (&owner, where).withSizeKeepingCentre (juce::jmax (84, where.getWidth()), 26);
    ed->setBounds (r);
    host->addAndMakeVisible (ed);
    ed->grabKeyboardFocus();
}

void showParameterMenu (juce::Component& owner, juce::RangedAudioParameter& param)
{
    juce::PopupMenu menu;
    menu.addSectionHeader (param.getName (64) + ": " + param.getCurrentValueAsText());
    juce::Component::SafePointer<juce::Component> safe (&owner);
    menu.addItem ("Enter value...", [safe, &param]
    {
        if (auto* c = safe.getComponent())
            editParameterText (*c, param, c->getLocalBounds());
    });
    menu.addItem ("Default (" + param.getText (param.getDefaultValue(), 64) + ")", [&param]
    {
        param.beginChangeGesture();
        param.setValueNotifyingHost (param.getDefaultValue());
        param.endChangeGesture();
    });

    // the host's own items (automation, MIDI learn ...), where the host offers them
    if (auto* editor = findEditor (owner))
        if (auto* context = editor->getHostContext())
            if (auto hostMenu = context->getContextMenuForParameter (&param))
            {
                menu.addSeparator();
                menu.addSubMenu ("Host", hostMenu->getEquivalentPopupMenu());
            }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&owner));
}

//==============================================================================
ParamSlider::ParamSlider (juce::RangedAudioParameter& p, juce::Slider::SliderStyle style)
    : juce::Slider (style, juce::Slider::NoTextBox), param (p)
{
    attachment = std::make_unique<juce::SliderParameterAttachment> (param, *this);
    setDoubleClickReturnValue (true, param.convertFrom0to1 (param.getDefaultValue()));
    setScrollWheelEnabled (true);
    setWantsKeyboardFocus (false);
    setPopupMenuEnabled (false);
    setRotaryParameters (kStartAngle, kEndAngle, true);
    setSliderSnapsToMousePosition (false);
}

ParamSlider::~ParamSlider() = default;

void ParamSlider::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        showParameterMenu (*this, param);
        return;
    }
    dragging = e.mods.isLeftButtonDown();
    lastPos = e.position;
    juce::Slider::mouseDown (e);                // begins the gesture
}

void ParamSlider::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging || ! isEnabled())
        return;
    const auto delta = e.position - lastPos;
    lastPos = e.position;
    const bool rotary = isRotary();
    const float travel = rotary ? (delta.x - delta.y) : delta.x;
    const float pixels = pixelsForFullRange * (e.mods.isShiftDown() ? 8.0f : 1.0f);
    const double pos = juce::jlimit (0.0, 1.0, valueToProportionOfLength (getValue()) + (double) (travel / pixels));
    setValue (proportionOfLengthToValue (pos), juce::sendNotificationSync);
}

void ParamSlider::mouseUp (const juce::MouseEvent& e)
{
    if (! dragging)
        return;
    dragging = false;
    juce::Slider::mouseUp (e);                  // ends the gesture
}

void ParamSlider::mouseDoubleClick (const juce::MouseEvent& e)
{
    juce::Slider::mouseDoubleClick (e);         // default value, as one gesture
}

//==============================================================================
Knob::Knob (juce::RangedAudioParameter& p, Size s, const juce::String& cap)
    : ParamSlider (p, juce::Slider::RotaryHorizontalVerticalDrag), size (s), caption (cap)
{
    pixelsForFullRange = s == Size::mini ? 180.0f : 260.0f;
}

void Knob::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const float d = std::min (b.getWidth(), b.getHeight());
    const auto c = b.getCentre();
    const float R = d * 0.5f - 1.0f;
    const float pos = (float) valueToProportionOfLength (getValue());
    const float angle = kStartAngle + pos * (kEndAngle - kStartAngle);
    const bool hover = isMouseOverOrDragging(), drag = isMouseButtonDown();
    const float alpha = isEnabled() ? 1.0f : 0.45f;

    const float arcW = size == Size::hero ? R * 0.075f : (size == Size::medium ? R * 0.1f : R * 0.16f);
    const float arcR = size == Size::hero ? R * 0.86f : R - arcW * 0.5f - 1.0f;
    const float bodyR = size == Size::hero ? R * 0.66f : arcR - arcW * 1.25f;
    const auto stroke = juce::PathStrokeType (arcW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

    if (size == Size::hero)
    {
        // halo, then the bezel: a dark ring behind the arc
        for (int i = 4; i >= 1; --i)
        {
            const float e = (float) i * 2.5f;
            g.setColour (col::accent.withAlpha (0.045f * alpha));
            g.drawEllipse (juce::Rectangle<float> (R * 2.0f, R * 2.0f).withCentre (c).expanded (e - 2.0f), 2.5f);
        }
        g.setGradientFill (juce::ColourGradient (col::panelTop.brighter (0.15f), c.x, c.y - R, col::well, c.x, c.y + R, false));
        g.fillEllipse (juce::Rectangle<float> (R * 2.0f, R * 2.0f).withCentre (c));
        g.setColour (col::edgeLit.withAlpha (0.55f));
        g.drawEllipse (juce::Rectangle<float> (R * 2.0f, R * 2.0f).withCentre (c).reduced (0.5f), 1.0f);
        g.setColour (col::well);
        g.fillEllipse (juce::Rectangle<float> (arcR * 2.0f + arcW * 2.2f, arcR * 2.0f + arcW * 2.2f).withCentre (c));
    }

    // track
    juce::Path track;
    track.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, kStartAngle, kEndAngle, true);
    g.setColour (col::accentDim.withAlpha (0.55f * alpha));
    g.strokePath (track, stroke);

    // value arc, with a soft glow
    if (pos > 0.002f)
    {
        juce::Path arc;
        arc.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, kStartAngle, angle, true);
        const auto bounds = arc.getBounds();
        for (int i = 3; i >= 1; --i)
        {
            g.setColour (col::accent.withAlpha (0.07f * alpha * (hover ? 1.4f : 1.0f)));
            g.strokePath (arc, juce::PathStrokeType (arcW * (1.0f + 0.9f * (float) i), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        auto grad = juce::ColourGradient (col::accent, bounds.getBottomLeft(), drag ? col::text : col::accentHi, bounds.getTopRight(), false);
        grad.multiplyOpacity (alpha);
        g.setGradientFill (grad);
        g.strokePath (arc, stroke);
    }

    // body
    const auto body = juce::Rectangle<float> (bodyR * 2.0f, bodyR * 2.0f).withCentre (c);
    g.setGradientFill (juce::ColourGradient (col::panelTop.brighter (hover ? 0.22f : 0.14f), c.x - bodyR * 0.5f, c.y - bodyR,
                                             col::well, c.x + bodyR * 0.3f, c.y + bodyR, false));
    g.fillEllipse (body);
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.drawEllipse (body.reduced (0.5f), 1.0f);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.drawEllipse (body.expanded (1.0f), 1.0f);

    // pointer
    const float p0 = size == Size::hero ? bodyR + 2.0f : bodyR * 0.35f;
    const float p1 = size == Size::hero ? arcR + arcW * 0.5f : bodyR * 0.92f;
    g.setColour ((drag ? col::accentHi : col::text).withMultipliedAlpha (alpha));
    g.drawLine (juce::Line<float> (polar (c, p0, angle), polar (c, p1, angle)), size == Size::mini ? 1.6f : 2.4f);

    if (size == Size::hero)
    {
        const auto text = juce::String (juce::roundToInt (getValue())) + "%";
        drawText (g, text, juce::Rectangle<float> (bodyR * 2.0f, bodyR * 0.62f).withCentre (c.translated (0.0f, -bodyR * 0.12f)),
                  bodyR * 0.48f, (drag ? col::accentHi : col::text).withMultipliedAlpha (alpha), juce::Justification::centred, Weight::medium);
        drawTracked (g, caption, juce::Rectangle<float> (bodyR * 2.0f, 16.0f).withCentre (c.translated (0.0f, bodyR * 0.38f)),
                     12.0f, col::textDim.withMultipliedAlpha (alpha), juce::Justification::centred, 0.12f, Weight::medium);
    }
}

//==============================================================================
HSlider::HSlider (juce::RangedAudioParameter& p, bool bip)
    : ParamSlider (p, juce::Slider::LinearHorizontal), bipolar (bip)
{
}

void HSlider::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const float thumbR = 7.5f;
    const float x0 = b.getX() + thumbR + 1.0f, x1 = b.getRight() - thumbR - 1.0f;
    const float cy = b.getCentreY();
    const float pos = (float) valueToProportionOfLength (getValue());
    const float tx = x0 + pos * (x1 - x0);
    const bool hover = isMouseOverOrDragging(), drag = isMouseButtonDown();
    const float alpha = isEnabled() ? 1.0f : 0.45f;

    const auto track = juce::Rectangle<float> (x0 - 3.0f, cy - 3.0f, (x1 - x0) + 6.0f, 6.0f);
    g.setColour (col::well);
    g.fillRoundedRectangle (track, 3.0f);
    g.setColour (col::edge);
    g.drawRoundedRectangle (track.reduced (0.5f), 3.0f, 1.0f);

    const float from = bipolar ? (x0 + x1) * 0.5f : x0 - 3.0f;
    auto fill = juce::Rectangle<float>::leftTopRightBottom (std::min (from, tx), cy - 3.0f, std::max (from, tx), cy + 3.0f);
    if (fill.getWidth() > 0.5f)
    {
        g.setColour (col::accent.withAlpha (0.18f * alpha));
        g.fillRoundedRectangle (fill.expanded (2.0f, 2.5f), 5.0f);
        auto grad = juce::ColourGradient (col::accent, fill.getX(), cy, col::accentHi, fill.getRight(), cy, false);
        grad.multiplyOpacity (alpha);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (fill, 3.0f);
    }
    if (bipolar)
    {
        g.setColour (col::textFaint.withMultipliedAlpha (alpha));
        g.fillRect (juce::Rectangle<float> (1.0f, 10.0f).withCentre ({ (x0 + x1) * 0.5f, cy }));
    }

    const auto thumb = juce::Rectangle<float> (thumbR * 2.0f, thumbR * 2.0f).withCentre ({ tx, cy });
    if (hover || drag)
    {
        g.setColour (col::accentHi.withAlpha (drag ? 0.28f : 0.16f));
        g.fillEllipse (thumb.expanded (4.0f));
    }
    g.setGradientFill (juce::ColourGradient (col::accentHi.withMultipliedAlpha (alpha), tx, cy - thumbR,
                                             col::accent.withMultipliedAlpha (alpha), tx, cy + thumbR, false));
    g.fillEllipse (thumb);
    g.setColour (juce::Colours::white.withAlpha (0.55f * alpha));
    g.drawEllipse (thumb.reduced (0.75f), 1.2f);
}

//==============================================================================
ValueText::ValueText (juce::RangedAudioParameter& p, float size, juce::Colour c, juce::Justification j)
    : param (p), fontSize (size), colour (c), just (j),
      attachment (p, [this] (float) { repaint(); }, nullptr)
{
    attachment.sendInitialUpdate();
    setMouseCursor (juce::MouseCursor::IBeamCursor);
}

void ValueText::paint (juce::Graphics& g)
{
    const auto unit = param.getLabel();
    drawText (g, withUnit (param.getCurrentValueAsText(), unit), getLocalBounds().toFloat(), fontSize,
              colour.withMultipliedAlpha (isEnabled() ? 1.0f : 0.45f), just, Weight::medium);
}

void ValueText::mouseDoubleClick (const juce::MouseEvent&)
{
    editParameterText (*this, param, getLocalBounds());
}

void ValueText::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        showParameterMenu (*this, param);
}

//==============================================================================
Selector::Selector (juce::RangedAudioParameter& p, std::vector<float> floatSteps)
    : param (p), steps (std::move (floatSteps)), attachment (p, [this] (float) { repaint(); }, nullptr)
{
    attachment.sendInitialUpdate();
}

int Selector::zoneAt (juce::Point<float> pt) const
{
    const float arrowW = std::min (34.0f, (float) getWidth() * 0.22f);
    if (pt.x < arrowW) return -1;
    if (pt.x > (float) getWidth() - arrowW) return 1;
    return 0;
}

void Selector::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat().reduced (0.5f);
    const float corner = b.getHeight() * 0.5f;
    const float alpha = isEnabled() ? 1.0f : 0.45f;
    drawWell (g, b, corner);
    if (hoverZone == 0)
    {
        g.setColour (col::accent.withAlpha (0.08f));
        g.fillRoundedRectangle (b.reduced (2.0f), corner - 2.0f);
    }
    const float arrowW = std::min (34.0f, b.getWidth() * 0.22f);
    const float iconSize = std::min (14.0f, b.getHeight() * 0.42f);
    const auto left = juce::Rectangle<float> (b.getX(), b.getY(), arrowW, b.getHeight());
    const auto right = juce::Rectangle<float> (b.getRight() - arrowW, b.getY(), arrowW, b.getHeight());
    auto arrowColour = [&] (int zone)
    {
        return (pressedZone == zone ? col::accentHi : (hoverZone == zone ? col::text : col::textDim)).withMultipliedAlpha (alpha);
    };
    icons::draw (g, icons::Id::chevronLeft, juce::Rectangle<float> (iconSize, iconSize).withCentre (left.getCentre()), arrowColour (-1), 1.4f);
    icons::draw (g, icons::Id::chevronRight, juce::Rectangle<float> (iconSize, iconSize).withCentre (right.getCentre()), arrowColour (1), 1.4f);

    const float value = param.convertFrom0to1 (param.getValue());
    const auto text = formatter ? formatter (value) : withUnit (param.getCurrentValueAsText(), param.getLabel());
    drawText (g, text, b.withTrimmedLeft (arrowW).withTrimmedRight (arrowW), std::min (15.0f, b.getHeight() * 0.48f),
              col::text.withMultipliedAlpha (alpha), juce::Justification::centred, Weight::medium);
}

void Selector::mouseMove (const juce::MouseEvent& e)
{
    const int z = zoneAt (e.position);
    if (z != hoverZone)
    {
        hoverZone = z;
        setMouseCursor (z == 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void Selector::mouseExit (const juce::MouseEvent&)
{
    hoverZone = -2;
    repaint();
}

void Selector::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        showParameterMenu (*this, param);
        return;
    }
    pressedZone = zoneAt (e.position);
    if (pressedZone == 0)
        showList();
    else
    {
        stepBy (pressedZone);
        repeatCount = 0;
        startTimer (420);                       // hold an arrow to keep stepping
    }
    repaint();
}

void Selector::mouseUp (const juce::MouseEvent&)
{
    stopTimer();
    pressedZone = -2;
    repaint();
}

void Selector::timerCallback()
{
    if (pressedZone == -1 || pressedZone == 1)
    {
        stepBy (pressedZone);
        startTimer (++repeatCount > 6 ? 60 : 120);
    }
    else
        stopTimer();
}

void Selector::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    const float d = std::abs (w.deltaY) > std::abs (w.deltaX) ? w.deltaY : -w.deltaX;
    if (std::abs (d) > 0.0f)
        stepBy ((d > 0.0f) != w.isReversed ? 1 : -1);
}

void Selector::stepBy (int delta)
{
    const float value = param.convertFrom0to1 (param.getValue());
    float next = value;
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (&param))
        next = (float) juce::jlimit (0, choice->choices.size() - 1, juce::roundToInt (value) + delta);
    else if (! steps.empty())
    {
        if (delta > 0)
        {
            next = steps.back();
            for (float s : steps) if (s > value + 1.0e-3f) { next = s; break; }
        }
        else
        {
            next = steps.front();
            for (auto it = steps.rbegin(); it != steps.rend(); ++it) if (*it < value - 1.0e-3f) { next = *it; break; }
        }
    }
    if (! juce::exactlyEqual (next, value))
        attachment.setValueAsCompleteGesture (next);
}

void Selector::showList()
{
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (&param))
    {
        juce::PopupMenu menu;
        const int current = choice->getIndex();
        for (int i = 0; i < choice->choices.size(); ++i)
            menu.addItem (i + 1, choice->choices[i], true, i == current);
        juce::Component::SafePointer<Selector> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMinimumWidth (getWidth()),
                            [safe] (int result)
                            {
                                if (result > 0 && safe != nullptr)
                                    safe->attachment.setValueAsCompleteGesture ((float) (result - 1));
                                if (safe != nullptr) { safe->pressedZone = -2; safe->repaint(); }
                            });
    }
    else
    {
        editParameterText (*this, param, getLocalBounds());
        pressedZone = -2;
    }
}

//==============================================================================
ModeSelector::ModeSelector (juce::RangedAudioParameter& p)
    : param (p), attachment (p, [this] (float v) { selected = juce::roundToInt (v); repaint(); }, nullptr)
{
    attachment.sendInitialUpdate();
}

juce::Rectangle<float> ModeSelector::buttonArea (int index) const
{
    const int n = vex::choices::numModes;
    const float gap = 10.0f;
    const float w = ((float) getWidth() - gap * (float) (n - 1)) / (float) n;
    return { (float) index * (w + gap), 0.0f, w, (float) getHeight() };
}

int ModeSelector::indexAt (juce::Point<float> pt) const
{
    for (int i = 0; i < vex::choices::numModes; ++i)
        if (buttonArea (i).contains (pt))
            return i;
    return -1;
}

juce::String ModeSelector::getTooltipFor (int index) const
{
    switch (index)
    {
        case 0: return "Stutter: the first slice of each phrase is captured and repeated.";
        case 1: return "Repeat: a new fragment is captured every beat and repeated until the next one.";
        case 2: return "Gate: the voice is chopped rhythmically (Length = open part of each slice).";
        case 3: return "Reverse: like Stutter, with the repeats played backwards.";
        case 4: return "Glitch: repeats varied by Glitch Type and Intensity (seeded, repeatable).";
        default: return {};
    }
}

void ModeSelector::paint (juce::Graphics& g)
{
    static constexpr icons::Id ids[] = { icons::Id::stutter, icons::Id::repeat, icons::Id::gate, icons::Id::reverse, icons::Id::glitch };
    for (int i = 0; i < vex::choices::numModes; ++i)
    {
        const auto r = buttonArea (i).reduced (1.0f);
        const bool on = i == selected, over = i == hover;
        if (on)
        {
            glow (g, r, 10.0f, col::accent, 7.0f, 1.6f);
            g.setGradientFill (juce::ColourGradient (col::accent.withAlpha (0.26f), r.getX(), r.getY(),
                                                     col::accent.withAlpha (0.08f), r.getX(), r.getBottom(), false));
            g.fillRoundedRectangle (r, 10.0f);
            g.setColour (col::accentHi);
            g.drawRoundedRectangle (r.reduced (0.75f), 10.0f, 1.5f);
        }
        else
        {
            g.setGradientFill (juce::ColourGradient (col::panelTop.brighter (over ? 0.16f : 0.08f), r.getX(), r.getY(),
                                                     col::panelBot, r.getX(), r.getBottom(), false));
            g.fillRoundedRectangle (r, 10.0f);
            g.setColour (over ? col::edgeLit : col::edge);
            g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.0f);
        }
        const float iconSize = std::min (28.0f, r.getHeight() * 0.36f);
        icons::draw (g, ids[i], juce::Rectangle<float> (iconSize, iconSize).withCentre ({ r.getCentreX(), r.getY() + r.getHeight() * 0.38f }),
                     on ? col::accentHi : (over ? col::text : col::textDim), 1.15f);
        drawText (g, vex::choices::modeNames[i], juce::Rectangle<float> (r.getWidth(), 18.0f).withCentre ({ r.getCentreX(), r.getY() + r.getHeight() * 0.76f }),
                  13.5f, on ? col::text : col::textDim, juce::Justification::centred, on ? Weight::semibold : Weight::medium);
    }
}

void ModeSelector::mouseMove (const juce::MouseEvent& e)
{
    const int i = indexAt (e.position);
    if (i != hover)
    {
        hover = i;
        setTooltip (getTooltipFor (i));
        setMouseCursor (i >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void ModeSelector::mouseExit (const juce::MouseEvent&)
{
    hover = -1;
    repaint();
}

void ModeSelector::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        showParameterMenu (*this, param);
        return;
    }
    const int i = indexAt (e.position);
    if (i >= 0 && i != selected)
        attachment.setValueAsCompleteGesture ((float) i);
}

//==============================================================================
Switch::Switch (juce::RangedAudioParameter& p)
    : juce::Button (p.getName (64)), attachment (p, *this)
{
    setClickingTogglesState (true);
}

void Switch::paintButton (juce::Graphics& g, bool hover, bool)
{
    const auto b = getLocalBounds().toFloat().reduced (1.0f);
    const auto pill = juce::Rectangle<float> (std::min (b.getWidth(), b.getHeight() * 1.9f), b.getHeight()).withCentre (b.getCentre());
    const float r = pill.getHeight() * 0.5f;
    const bool on = getToggleState();
    if (on)
    {
        glow (g, pill, r, col::accent, 5.0f, 1.2f);
        g.setGradientFill (juce::ColourGradient (col::accent, pill.getX(), pill.getY(), col::accentHi, pill.getRight(), pill.getBottom(), false));
        g.fillRoundedRectangle (pill, r);
    }
    else
    {
        drawWell (g, pill, r);
    }
    if (hover)
    {
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.fillRoundedRectangle (pill, r);
    }
    const float kr = r - 3.0f;
    const float kx = on ? pill.getRight() - r : pill.getX() + r;
    g.setColour (on ? juce::Colours::white : col::textDim);
    g.fillEllipse (juce::Rectangle<float> (kr * 2.0f, kr * 2.0f).withCentre ({ kx, pill.getCentreY() }));
}

//==============================================================================
PillToggle::PillToggle (juce::RangedAudioParameter& p, const juce::String& on, const juce::String& off,
                        juce::Colour c, icons::Id i, bool show)
    : juce::Button (p.getName (64)), attachment (p, *this), onText (on), offText (off), onColour (c), icon (i), showIcon (show)
{
    setClickingTogglesState (true);
}

void PillToggle::paintButton (juce::Graphics& g, bool hover, bool down)
{
    const auto b = getLocalBounds().toFloat().reduced (1.0f);
    const float r = std::min (10.0f, b.getHeight() * 0.5f);
    const bool on = getToggleState();
    const float alpha = isEnabled() ? 1.0f : 0.45f;
    const auto c = on ? onColour : col::edgeLit;
    if (on)
    {
        glow (g, b, r, onColour, 5.0f, pending ? 0.6f : 1.2f);
        g.setColour (onColour.withAlpha ((pending ? 0.1f : 0.2f) * alpha));
        g.fillRoundedRectangle (b, r);
    }
    else
    {
        g.setColour (col::well.withAlpha (0.6f));
        g.fillRoundedRectangle (b, r);
    }
    if (hover || down)
    {
        g.setColour (juce::Colours::white.withAlpha (down ? 0.1f : 0.05f));
        g.fillRoundedRectangle (b, r);
    }
    g.setColour (c.withMultipliedAlpha (alpha));
    g.drawRoundedRectangle (b.reduced (0.5f), r, on ? 1.5f : 1.0f);

    const auto textColour = (on ? onColour.brighter (0.4f) : col::text).withMultipliedAlpha (alpha);
    auto area = b.reduced (8.0f, 0.0f);
    const auto label = on ? onText : offText;
    if (showIcon)
    {
        const float s = std::min (14.0f, b.getHeight() * 0.5f);
        const float textW = juce::GlyphArrangement::getStringWidth (font (12.5f, Weight::semibold), label) * 1.08f + 4.0f;
        const float total = s + 6.0f + textW;
        const float x = b.getCentreX() - total * 0.5f;
        icons::draw (g, icon, { x, b.getCentreY() - s * 0.5f, s, s }, textColour, 1.1f);
        area = { x + s + 6.0f, b.getY(), textW, b.getHeight() };
        drawTracked (g, label, area, 12.5f, textColour, juce::Justification::centredLeft, 0.08f);
    }
    else
        drawTracked (g, label, area, 12.5f, textColour, juce::Justification::centred, 0.1f);
}

//==============================================================================
PowerButton::PowerButton (juce::RangedAudioParameter& p)
    : juce::Button ("Power"), bypass (p), attachment (p, [this] (float) { repaint(); }, nullptr)
{
    attachment.sendInitialUpdate();
    setTooltip ("Power: the plug-in is processing when lit. The same state as Bypass and the host's bypass.");
}

void PowerButton::clicked()
{
    attachment.setValueAsCompleteGesture (bypass.getValue() > 0.5f ? 0.0f : 1.0f);
}

void PowerButton::paintButton (juce::Graphics& g, bool hover, bool down)
{
    const auto b = getLocalBounds().toFloat().reduced (2.0f);
    const float d = std::min (b.getWidth(), b.getHeight());
    const auto circle = juce::Rectangle<float> (d, d).withCentre (b.getCentre());
    const bool active = bypass.getValue() < 0.5f;
    if (active)
        glow (g, circle, d * 0.5f, col::accent, 6.0f, 1.3f);
    g.setGradientFill (juce::ColourGradient (col::panelTop.brighter (hover ? 0.2f : 0.1f), circle.getX(), circle.getY(),
                                             col::well, circle.getX(), circle.getBottom(), false));
    g.fillEllipse (circle);
    g.setColour (active ? col::accent : col::edgeLit);
    g.drawEllipse (circle.reduced (0.75f), 1.5f);
    icons::draw (g, icons::Id::power, circle.reduced (d * 0.27f), down ? col::text : (active ? col::accentHi : col::textFaint), 1.25f);
}

//==============================================================================
IconButton::IconButton (const juce::String& name, icons::Id i, float scale)
    : juce::Button (name), id (i), iconScale (scale)
{
}

void IconButton::paintButton (juce::Graphics& g, bool hover, bool down)
{
    const auto b = getLocalBounds().toFloat();
    if (hover || down)
    {
        g.setColour (col::accent.withAlpha (down ? 0.22f : 0.12f));
        g.fillRoundedRectangle (b.reduced (1.0f), 8.0f);
    }
    const float s = std::min (b.getWidth(), b.getHeight()) * iconScale;
    icons::draw (g, id, juce::Rectangle<float> (s, s).withCentre (b.getCentre()), hover ? col::accentHi : col::textDim, 1.1f);
}

//==============================================================================
bool LevelMeter::update (float peak, double dt)
{
    const float inDb = juce::Decibels::gainToDecibels (peak, -100.0f);
    const float oldLevel = levelDb, oldHold = holdDb;
    const bool oldClip = clipped;
    const float fall = 20.0f * (float) dt;
    levelDb = inDb > levelDb ? inDb : std::max (inDb, levelDb - fall);
    if (inDb >= holdDb) { holdDb = inDb; holdLeft = 1.5; }
    else if ((holdLeft -= dt) <= 0.0) holdDb = std::max (levelDb, holdDb - fall);
    if (inDb >= -0.1f) clipped = true;
    auto px = [this] (float db) { return juce::roundToInt (juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f) * (float) getHeight()); };
    return px (oldLevel) != px (levelDb) || px (oldHold) != px (holdDb) || oldClip != clipped;
}

void LevelMeter::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const float corner = b.getWidth() * 0.5f;
    g.setColour (col::well);
    g.fillRoundedRectangle (b, corner);
    g.setColour (col::edge);
    g.drawRoundedRectangle (b.reduced (0.5f), corner, 1.0f);

    auto toY = [&b] (float db) { return b.getBottom() - juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f) * b.getHeight(); };
    const float y = toY (levelDb);
    if (y < b.getBottom() - 0.5f)
    {
        const auto bar = juce::Rectangle<float>::leftTopRightBottom (b.getX() + 1.0f, y, b.getRight() - 1.0f, b.getBottom() - 1.0f);
        juce::ColourGradient grad (col::accent, 0.0f, b.getBottom(), col::danger, 0.0f, b.getY(), false);
        grad.addColour (0.75, col::cyan);           // -15 dB
        grad.addColour (0.9, col::warn);            // -6 dB
        g.setGradientFill (grad);
        g.fillRoundedRectangle (bar, std::max (1.0f, corner - 1.0f));
    }
    if (holdDb > -60.0f)
    {
        g.setColour (holdDb >= -0.1f ? col::danger : col::text.withAlpha (0.8f));
        g.fillRect (juce::Rectangle<float> (b.getX() + 1.0f, toY (holdDb) - 1.0f, b.getWidth() - 2.0f, 2.0f));
    }
    if (clipped)
    {
        g.setColour (col::danger);
        g.fillRoundedRectangle (b.withHeight (4.0f), 2.0f);
    }
}
}
