#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <cmath>

/** VEX Stutter design tokens and drawing helpers (dark blue / cyan, specification section 14).

    Design brief
      Concept     a precise performance instrument for vocals: dark navy glass, one blue accent,
                  cyan for values; glow only on what is live or selected.
      Hero        the Amount knob; second tier Mix, the mode buttons and the waveform.
      Canvas      1120 x 630 logical pixels (16:9, the mockup's proportions), scaled as a whole
                  (70 - 160 %), so every size below is in logical pixels on a 4 px grid.
      Type        Inter (SIL OFL, embedded): labels 11-12 px SemiBold, values 13-15 px, hero 38 px.
*/
namespace vex::ui
{
namespace col
{
    inline const juce::Colour bg        { 0xff03050b };   // around the frame
    inline const juce::Colour frameTop  { 0xff0a1121 };
    inline const juce::Colour frameBot  { 0xff04070f };
    inline const juce::Colour panelTop  { 0xff0d1528 };
    inline const juce::Colour panelBot  { 0xff070c19 };
    inline const juce::Colour well      { 0xff040915 };   // waveform, selector pills, slider tracks
    inline const juce::Colour edge      { 0xff1a2946 };   // panel borders
    inline const juce::Colour edgeLit   { 0xff2c5592 };   // frame border, hover
    inline const juce::Colour accent    { 0xff2f8cff };   // arcs, fills, selected state
    inline const juce::Colour accentHi  { 0xff74c6ff };   // highlights, dragging
    inline const juce::Colour accentDim { 0xff17427d };   // unfilled arcs
    inline const juce::Colour cyan      { 0xff52c7ff };   // values
    inline const juce::Colour text      { 0xffe7efff };   // primary text
    inline const juce::Colour textDim   { 0xffa3b4d1 };   // labels
    inline const juce::Colour textFaint { 0xff7488aa };   // tertiary / disabled-looking text
    inline const juce::Colour wave      { 0xff7d90b2 };   // waveform outside the selection
    inline const juce::Colour waveSel   { 0xff4aa6ff };   // waveform inside the selection
    inline const juce::Colour ice       { 0xffa6e6ff };   // frozen
    inline const juce::Colour warn      { 0xffffb547 };   // bypassed
    inline const juce::Colour danger    { 0xffff5468 };   // clip
}

namespace dims
{
    constexpr int width = 1120, height = 630;
    constexpr float panelCorner = 12.0f, controlCorner = 8.0f;
}

//------------------------------------------------------------------- fonts ----
enum class Weight { regular, medium, semibold };

/** Embedded Inter faces (loaded once, message thread). */
juce::Font font (float height, Weight weight = Weight::regular);

/** Text with extra letter spacing (labels, the STUTTER wordmark). */
void drawTracked (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area, float size,
                  juce::Colour colour, juce::Justification just = juce::Justification::centredLeft,
                  float tracking = 0.06f, Weight weight = Weight::semibold);

/** Plain text, fitted to its box. */
inline void drawText (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area, float size,
                      juce::Colour colour, juce::Justification just = juce::Justification::centred,
                      Weight weight = Weight::regular)
{
    g.setColour (colour);
    g.setFont (font (size, weight));
    g.drawFittedText (text, area.getSmallestIntegerContainer(), just, 1, 0.85f);
}

//--------------------------------------------------------- drawing helpers ----
/** Soft glow around a rounded rectangle without blurring: a few widening, fading strokes. */
inline void glow (juce::Graphics& g, juce::Rectangle<float> r, float corner, juce::Colour c, float size, float strength = 1.0f)
{
    const int steps = 6;
    for (int i = steps; i >= 1; --i)
    {
        const float t = (float) i / (float) steps;
        g.setColour (c.withAlpha (0.055f * strength * (1.0f - t) + 0.012f * strength));
        g.drawRoundedRectangle (r.expanded (size * t), corner + size * t, size / (float) steps * 1.6f);
    }
}

/** Panel ("card"): navy gradient, hairline border, faint top highlight. */
inline void drawPanel (juce::Graphics& g, juce::Rectangle<float> r, float corner = dims::panelCorner, float borderAlpha = 1.0f)
{
    g.setGradientFill (juce::ColourGradient (col::panelTop, r.getX(), r.getY(), col::panelBot, r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle (r, corner);
    // rim: lit from above (blue), fading to the plain edge colour at the bottom
    g.setGradientFill (juce::ColourGradient (col::accent.withAlpha (0.42f * borderAlpha), r.getX(), r.getY(),
                                             col::edge.withMultipliedAlpha (borderAlpha), r.getX(), r.getY() + std::min (60.0f, r.getHeight()), false));
    g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.06f), r.getCentreX(), r.getY(),
                                             juce::Colours::transparentWhite, r.getRight(), r.getY(), true));
    g.drawLine (r.getX() + corner, r.getY() + 1.0f, r.getRight() - corner, r.getY() + 1.0f, 1.0f);
}

/** Inset well (selector pills, slider tracks, the waveform). */
inline void drawWell (juce::Graphics& g, juce::Rectangle<float> r, float corner)
{
    g.setColour (col::well);
    g.fillRoundedRectangle (r, corner);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.drawLine (r.getX() + corner, r.getY() + 0.75f, r.getRight() - corner, r.getY() + 0.75f, 1.0f);
    g.setColour (col::edge.withAlpha (0.9f));
    g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.0f);
}

/** The accent gradient used by every value arc and fill (blue -> cyan). */
inline juce::ColourGradient accentGradient (juce::Point<float> from, juce::Point<float> to)
{
    return juce::ColourGradient (col::accent, from, col::accentHi, to, false);
}

/** Milliseconds as text: "62.5 ms", "234 ms", "1.25 s". */
inline juce::String formatMs (double ms)
{
    if (ms >= 1000.0) return juce::String (ms / 1000.0, 2) + " s";
    if (ms >= 100.0)  return juce::String (juce::roundToInt (ms)) + " ms";
    return juce::String (ms, 1) + " ms";
}

/** Converts the plain parameter text into the UI's format ("75.0" + "%" -> "75.0%"). */
inline juce::String withUnit (const juce::String& value, const juce::String& unit)
{
    return unit.isEmpty() ? value : value + (unit == "%" ? "" : " ") + unit;
}
}
