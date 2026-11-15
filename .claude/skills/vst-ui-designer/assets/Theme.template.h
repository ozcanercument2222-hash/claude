#pragma once
//==============================================================================
//  Theme.template.h  -  design tokens, drawing helpers and base controls (JUCE 8)
//
//  How to use
//    1. Copy to Source/Theme.h (or merge into the project's existing theme
//       header, e.g. sccui in Source/UIComponents.h) and rename the namespace.
//    2. Change the tokens in `col`, `dim`, `type` to the palette chosen in the
//       design brief (see references/design-language.md). Everything below is
//       built on these tokens, so a re-skin is a token edit.
//    3. Embed your fonts (references/juce-implementation.md) and call
//       ui::loadFonts() once, before the editor is created.
//    4. Install the look-and-feel on the editor / view:
//           ui::RotaryLookAndFeel knobLnf;   // member of the editor
//           knob.setLookAndFeel (&knobLnf);  // and setLookAndFeel (nullptr) in the
//                                            // editor's destructor, before knobLnf dies
//
//  Defaults below are the "Graphite & Lime" palette.
//==============================================================================
#include <juce_gui_basics/juce_gui_basics.h>
#include <algorithm>
#include <cmath>

namespace ui
{
//------------------------------------------------------------------ tokens ----
namespace col
{
    inline const juce::Colour bgTop      { 0xff111311 };   // window background, gradient top
    inline const juce::Colour bgBottom   { 0xff050605 };   // window background, gradient bottom
    inline const juce::Colour card       { 0xff161916 };   // module / card surface
    inline const juce::Colour well       { 0xff0c0e0c };   // inset: displays, value boxes
    inline const juce::Colour raised     { 0xff1d211d };   // buttons, knob bodies
    inline const juce::Colour edge       { 0xff2c312c };   // hairline borders (decorative, low contrast)
    inline const juce::Colour text       { 0xffdde2dd };   // primary text
    inline const juce::Colour textDim    { 0xff8c948c };   // secondary text (must stay >= 4.5:1)
    inline const juce::Colour accent     { 0xffc8f72d };   // active arcs, selected states
    inline const juce::Colour accentHot  { 0xffe4ff7a };   // value being dragged, peaks
    inline const juce::Colour accentDeep { 0xff55c21a };   // unfilled tracks, fills under curves
    inline const juce::Colour warn       { 0xffffa62b };   // bypass, caution, GR meters
    inline const juce::Colour danger     { 0xffff4d3a };   // clip, error
}

namespace sp        // 4 px base grid
{
    constexpr float xs = 4.0f, s = 8.0f, m = 12.0f, l = 16.0f, xl = 24.0f;
}

namespace radius    // two radii only
{
    constexpr float control = 4.0f, card = 8.0f;
}

namespace dim       // control size tiers (design pixels)
{
    constexpr int hero = 180, primary = 84, secondary = 52, mini = 32;
    constexpr int controlHeight = 30, minHit = 24;
}

namespace type      // type scale at the design width; nothing below 10
{
    constexpr float label = 11.0f, value = 13.0f, title = 15.0f, hero = 30.0f;
}

//------------------------------------------------------------------- fonts ----
struct Fonts { juce::Typeface::Ptr regular, semibold; };
inline Fonts& fonts() { static Fonts f; return f; }

/** Call once at start-up with embedded font data (BinaryData::...). Null = system default. */
inline void loadFonts (const void* regularData, size_t regularSize,
                       const void* semiboldData, size_t semiboldSize)
{
    if (regularData != nullptr)  fonts().regular  = juce::Typeface::createSystemTypefaceFor (regularData, regularSize);
    if (semiboldData != nullptr) fonts().semibold = juce::Typeface::createSystemTypefaceFor (semiboldData, semiboldSize);
}

inline juce::Font font (float height, bool semibold = false)
{
    const auto face = (semibold && fonts().semibold != nullptr) ? fonts().semibold : fonts().regular;
    if (face != nullptr)
        return juce::Font (juce::FontOptions (face).withHeight (height));
    return juce::Font (juce::FontOptions (height, semibold ? juce::Font::bold : juce::Font::plain));
}

//--------------------------------------------------------- drawing helpers ----
/** 1 physical pixel in logical units: use for hairlines so they stay crisp at any scale. */
inline float hairline (const juce::Graphics& g)
{
    return 1.0f / std::max (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
}

/** UPPERCASE section / knob label with letter-spacing. Falls back to fitted text if it will not fit. */
inline void drawLabel (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area,
                       float size = type::label, juce::Colour colour = col::textDim,
                       juce::Justification just = juce::Justification::centred, float tracking = 0.08f)
{
    const auto f = font (size, true);
    const auto upper = text.toUpperCase();

    juce::GlyphArrangement ga;
    ga.addLineOfText (f, upper, 0.0f, 0.0f);
    const int n = ga.getNumGlyphs();
    const float extra = size * tracking;
    for (int i = 1; i < n; ++i)
        ga.getGlyph (i).moveBy ((float) i * extra, 0.0f);

    const auto b = ga.getBoundingBox (0, n, true);
    g.setColour (colour);
    if (n == 0 || b.getWidth() > area.getWidth())
    {
        g.setFont (f);
        g.drawFittedText (upper, area.getSmallestIntegerContainer(), just, 1, 0.8f);
        return;
    }

    float x = area.getX();
    if (just.testFlags (juce::Justification::horizontallyCentred))   x = area.getCentreX() - b.getWidth() * 0.5f;
    else if (just.testFlags (juce::Justification::right))           x = area.getRight() - b.getWidth();

    const float baseline = area.getCentreY() + (f.getAscent() - f.getDescent()) * 0.5f;
    ga.moveRangeOfGlyphs (0, n, x - b.getX(), baseline);
    ga.draw (g);
}

/** Plain value / body text, fitted to its box (never clipped, never below 80 % size). */
inline void drawText (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area,
                      float size = type::value, juce::Colour colour = col::text, bool semibold = false,
                      juce::Justification just = juce::Justification::centred)
{
    g.setColour (colour);
    g.setFont (font (size, semibold));
    g.drawFittedText (text, area.getSmallestIntegerContainer(), just, 1, 0.8f);
}

/** Glow without blur: three strokes, wide to narrow. Use only for live elements. */
inline void strokeGlow (juce::Graphics& g, const juce::Path& p, juce::Colour c, float width)
{
    const auto style = [] (float w) { return juce::PathStrokeType (w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded); };
    g.setColour (c.withAlpha (0.07f)); g.strokePath (p, style (width * 4.0f));
    g.setColour (c.withAlpha (0.18f)); g.strokePath (p, style (width * 2.3f));
    g.setColour (c);                   g.strokePath (p, style (width));
}

/** Module / card: subtle vertical gradient, 1 px border, top highlight. */
inline void fillCard (juce::Graphics& g, juce::Rectangle<float> r, float corner = radius::card)
{
    g.setGradientFill (juce::ColourGradient (col::card.brighter (0.05f), r.getX(), r.getY(),
                                             col::card,                  r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle (r, corner);
    g.setColour (col::edge);
    g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.0f);
    g.setColour (juce::Colours::white.withAlpha (0.05f));
    g.drawLine (r.getX() + corner, r.getY() + 1.0f, r.getRight() - corner, r.getY() + 1.0f, 1.0f);
}

/** Inset well for displays and value boxes: darker than the surroundings, inner top shadow. */
inline void fillWell (juce::Graphics& g, juce::Rectangle<float> r, float corner = radius::control)
{
    g.setColour (col::well);
    g.fillRoundedRectangle (r, corner);
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.drawLine (r.getX() + corner, r.getY() + 0.5f, r.getRight() - corner, r.getY() + 0.5f, 1.0f);
    g.setColour (col::edge.withAlpha (0.8f));
    g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.0f);
}

/** Window background: vertical gradient. Draw it once into a cached Image on resize if the editor repaints often. */
inline void fillBackground (juce::Graphics& g, juce::Rectangle<float> r)
{
    g.setGradientFill (juce::ColourGradient (col::bgTop, 0.0f, r.getY(), col::bgBottom, 0.0f, r.getBottom(), false));
    g.fillRect (r);
}

//-------------------------------------------------------- value formatting ----
inline juce::String formatFrequency (double hz)
{
    if (hz < 100.0)   return juce::String (hz, 1) + " Hz";
    if (hz < 1000.0)  return juce::String (juce::roundToInt (hz)) + " Hz";
    const double k = hz / 1000.0;
    return juce::String (k, k < 10.0 ? 2 : 1) + " kHz";
}

inline juce::String formatDb (double db, int decimals = 1)
{
    if (db <= -99.0) return "-inf dB";
    const double eps = 0.5 * std::pow (10.0, -decimals);
    if (std::abs (db) < eps) return juce::String (0.0, decimals) + " dB";
    return (db > 0.0 ? "+" : "") + juce::String (db, decimals) + " dB";
}

inline juce::String formatMs (double ms)
{
    if (ms >= 1000.0) return juce::String (ms / 1000.0, 2) + " s";
    if (ms >= 100.0)  return juce::String (juce::roundToInt (ms)) + " ms";
    return juce::String (ms, 1) + " ms";
}

inline juce::String formatPercent (double percent)   { return juce::String (juce::roundToInt (percent)) + " %"; }

/** Forgiving number parser for text entry: "1.2k", "1200", "1.2 kHz", "-3 dB", "-inf". */
inline double parseNumber (const juce::String& text, double fallback = 0.0, double infValue = -100.0)
{
    auto t = text.trim().toLowerCase().replace (juce::String::charToString ((juce::juce_wchar) 0x2212), "-");
    if (t.contains ("inf"))
        return infValue;
    const bool kilo = t.containsChar ('k');
    const auto num = t.retainCharacters ("0123456789.-+,").replaceCharacter (',', '.');
    if (num.isEmpty())
        return fallback;
    return num.getDoubleValue() * (kilo ? 1000.0 : 1.0);
}

//-------------------------------------------------------- meter ballistics ----
/** Peak meter with constant-rate fall, timed peak hold and a latching clip flag.
    Feed it the audio thread's atomic peak once per UI frame, with the real frame time. */
struct PeakMeter
{
    float levelDb = -100.0f;      // displayed bar
    float holdDb  = -100.0f;      // peak-hold line
    bool  clipped = false;        // latches until resetClip()

    float fallDbPerSecond = 20.0f;
    float holdSeconds     = 1.5f;
    float clipThresholdDb = -0.1f;

    void update (float peakLinear, double dtSeconds) noexcept
    {
        const float inDb  = juce::Decibels::gainToDecibels (peakLinear, -100.0f);
        const float fall  = fallDbPerSecond * (float) dtSeconds;

        levelDb = inDb > levelDb ? inDb : std::max (inDb, levelDb - fall);

        if (inDb >= holdDb)                { holdDb = inDb; holdLeft = (double) holdSeconds; }
        else if ((holdLeft -= dtSeconds) <= 0.0)  holdDb = std::max (levelDb, holdDb - fall);

        if (inDb >= clipThresholdDb)       clipped = true;
    }

    void resetClip() noexcept   { clipped = false; }

private:
    double holdLeft = 0.0;
};

/** Map dB to 0..1 along a meter scale (linear in dB, which reads like a log meter). */
inline float dbToProportion (float db, float minDb = -60.0f, float maxDb = 0.0f) noexcept
{
    return juce::jlimit (0.0f, 1.0f, (db - minDb) / (maxDb - minDb));
}

/** Level colour: accent below -6 dBFS, warn up to -1, danger above. */
inline juce::Colour meterColour (float db)
{
    if (db >= -1.0f) return col::danger;
    if (db >= -6.0f) return col::warn;
    return col::accent;
}

//------------------------------------------------------------ knob control ----
/** Rotary slider with the interaction pros expect: vertical drag, Shift = fine,
    double-click resets (set automatically by SliderAttachment), wheel, type-in is up to the host view.
    JUCE's Slider has no Shift-for-fine drag, so deltas are accumulated here; changing the
    sensitivity of the stock drag in mid-gesture would make the value jump.
    Gesture begin / end for host automation still comes from the base class (mouseDown / mouseUp). */
class ProKnob : public juce::Slider
{
public:
    explicit ProKnob (bool bipolar = false)
        : juce::Slider (juce::Slider::RotaryVerticalDrag, juce::Slider::NoTextBox)
    {
        setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,    // 7:30
                             juce::MathConstants<float>::pi * 2.75f,    // 4:30
                             true);
        setMouseDragSensitivity (260);          // pixels for the full range
        setVelocityBasedMode (false);           // same distance, same change
        setScrollWheelEnabled (true);
        setWantsKeyboardFocus (true);
        getProperties().set ("bipolar", bipolar);
    }

    float finePixelsForFullRange = 1800.0f;     // Shift held

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragging = e.mods.isLeftButtonDown() && ! e.mods.isPopupMenu() && ! e.mods.isAltDown();
        lastPos  = e.position;
        juce::Slider::mouseDown (e);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragging || ! isEnabled())
            return;

        const auto delta  = e.position - lastPos;
        lastPos = e.position;

        const float travel = delta.x - delta.y;                       // right or up increases
        const float pixels = e.mods.isShiftDown() ? finePixelsForFullRange
                                                  : (float) getMouseDragSensitivity();
        const double p = juce::jlimit (0.0, 1.0, valueToProportionOfLength (getValue()) + (double) (travel / pixels));
        setValue (proportionOfLengthToValue (p), juce::sendNotificationSync);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        dragging = false;
        juce::Slider::mouseUp (e);
    }

private:
    juce::Point<float> lastPos;
    bool dragging = false;
};

/** Flat-modern knob: track, value arc (bipolar from centre), body, pointer.
    Drag state brightens the arc; disabled dims everything. Set the "bipolar" property on the slider. */
class RotaryLookAndFeel : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                           float pos, float startAngle, float endAngle, juce::Slider& s) override
    {
        const auto b = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (2.0f);
        const float d = std::min (b.getWidth(), b.getHeight());
        const auto  c = b.getCentre();
        const float R = d * 0.5f;

        const float arcW  = juce::jlimit (2.5f, 9.0f, d * 0.075f);
        const float arcR  = R - arcW * 0.5f;
        const float bodyR = arcR - arcW * 1.1f;

        const bool  dragging = s.isMouseButtonDown();
        const bool  hover    = s.isMouseOverOrDragging();
        const float alpha    = s.isEnabled() ? 1.0f : 0.4f;
        const bool  bipolar  = (bool) s.getProperties().getWithDefault ("bipolar", false);

        const float angle = startAngle + pos * (endAngle - startAngle);
        const float from  = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;

        const auto style = juce::PathStrokeType (arcW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

        // unfilled track
        juce::Path track;
        track.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
        g.setColour (col::accentDeep.withAlpha (0.18f * alpha));
        g.strokePath (track, style);

        // value arc
        if (std::abs (angle - from) > 0.01f)
        {
            juce::Path value;
            value.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, std::min (from, angle), std::max (from, angle), true);
            g.setColour ((dragging ? col::accentHot : col::accent).withMultipliedAlpha (alpha));
            g.strokePath (value, style);
        }

        // body
        g.setGradientFill (juce::ColourGradient (col::raised.brighter (hover ? 0.12f : 0.06f), c.x, c.y - bodyR,
                                                 col::well.brighter (0.02f),                   c.x, c.y + bodyR, false));
        g.fillEllipse (c.x - bodyR, c.y - bodyR, bodyR * 2.0f, bodyR * 2.0f);
        g.setColour (col::edge.brighter (0.2f).withMultipliedAlpha (alpha));
        g.drawEllipse (c.x - bodyR, c.y - bodyR, bodyR * 2.0f, bodyR * 2.0f, 1.0f);

        // pointer
        const auto at = [&] (float radius) { return juce::Point<float> (c.x + radius * std::sin (angle), c.y - radius * std::cos (angle)); };
        g.setColour ((dragging ? col::accentHot : col::text).withMultipliedAlpha (alpha));
        g.drawLine (juce::Line<float> (at (bodyR * 0.40f), at (bodyR * 0.86f)), juce::jlimit (1.5f, 3.0f, d * 0.03f));

        // keyboard focus ring
        if (s.hasKeyboardFocus (false))
        {
            g.setColour (col::accent.withAlpha (0.8f));
            g.drawEllipse (c.x - R, c.y - R, R * 2.0f, R * 2.0f, 1.0f);
        }
    }
};

//---------------------------------------------------------------- buttons ----
/** Toggle / action button. ON = tinted fill + accent outline + accent text (unmistakable),
    OFF = outline with primary text. Label never changes with state. */
class ProButton : public juce::Button
{
public:
    explicit ProButton (const juce::String& label, bool isToggle = true) : juce::Button (label)
    {
        setClickingTogglesState (isToggle);
        setWantsKeyboardFocus (true);
    }

    void paintButton (juce::Graphics& g, bool hover, bool down) override
    {
        const auto r     = getLocalBounds().toFloat().reduced (0.5f);
        const bool on    = getToggleState();
        const float a    = isEnabled() ? 1.0f : 0.4f;

        auto fill = on ? col::accent.withAlpha (0.16f) : col::raised;
        if (hover && ! on) fill = fill.brighter (0.08f);
        if (down)          fill = fill.darker (0.2f);

        g.setColour (fill.withMultipliedAlpha (a));
        g.fillRoundedRectangle (r, radius::control);

        g.setColour ((on ? col::accent : col::edge.brighter (0.35f)).withMultipliedAlpha (a));
        g.drawRoundedRectangle (r, radius::control, on ? 1.5f : 1.0f);

        if (hasKeyboardFocus (false))
        {
            g.setColour (col::accent.withAlpha (0.7f));
            g.drawRoundedRectangle (r.expanded (2.0f), radius::control + 2.0f, 1.0f);
        }

        drawText (g, getButtonText(), r.reduced (sp::s, 0.0f), type::value,
                  (on ? col::accent : col::text).withMultipliedAlpha (a), true);
    }
};

} // namespace ui
