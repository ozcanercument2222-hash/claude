#pragma once
//==============================================================================
//  VSTM Tune - tasarim belirtecleri (renk, yazi, olcu) ve ortak cizim yardimcilari
//
//  Tum arayuz 1536 x 1024'luk sabit bir tasarim tuvali uzerinde kurulur ve pencere
//  boyutuna gore olceklenir (vektorel, her olcekte keskin).
//==============================================================================
#include <juce_gui_basics/juce_gui_basics.h>
#include "VSTMBinary.h"

namespace vui
{
constexpr float kDesignW = 1536.0f;
constexpr float kDesignH = 1024.0f;

namespace col
{
    inline const juce::Colour outside    { 0xff070709 };
    inline const juce::Colour bg         { 0xff0e0e12 };
    inline const juce::Colour bgTop      { 0xff111115 };
    inline const juce::Colour bgBottom   { 0xff0b0b0e };
    inline const juce::Colour frame      { 0xff28272e };   // window outline
    inline const juce::Colour card       { 0xff141318 };
    inline const juce::Colour cardTop    { 0xff17161c };
    inline const juce::Colour cardEdge   { 0xff2b2a32 };
    inline const juce::Colour well       { 0xff0b0b0e };   // inset boxes, displays
    inline const juce::Colour wellEdge   { 0xff23222a };
    inline const juce::Colour raised     { 0xff1c1b22 };
    inline const juce::Colour hairline   { 0xff1d1c23 };

    inline const juce::Colour text       { 0xffe9e8ee };   // values
    inline const juce::Colour value      { 0xffdedbe7 };   // numbers inside value boxes
    inline const juce::Colour label      { 0xffb3b1bb };   // UPPERCASE labels
    inline const juce::Colour dim        { 0xff8e8c97 };   // secondary text
    inline const juce::Colour faint      { 0xff77757f };   // tertiary / disabled

    inline const juce::Colour accent     { 0xff9378fd };   // violet
    inline const juce::Colour accentLo   { 0xff7c5df8 };
    inline const juce::Colour accentHi   { 0xffbba8fe };
    inline const juce::Colour accentText { 0xffb294fb };
    inline const juce::Colour accentDeep { 0xff3a2675 };
    inline const juce::Colour thumb      { 0xffcac5e8 };
    inline const juce::Colour human      { 0xfff5f3fb };   // the "Insansi" side of the hero knob
    inline const juce::Colour warn       { 0xffffb02e };
}

//------------------------------------------------------------------ fonts
struct Fonts
{
    Fonts()
    {
        regular  = juce::Typeface::createSystemTypefaceFor (VSTMBinary::InterRegular_ttf,  VSTMBinary::InterRegular_ttfSize);
        medium   = juce::Typeface::createSystemTypefaceFor (VSTMBinary::InterMedium_ttf,   VSTMBinary::InterMedium_ttfSize);
        semibold = juce::Typeface::createSystemTypefaceFor (VSTMBinary::InterSemiBold_ttf, VSTMBinary::InterSemiBold_ttfSize);
    }
    juce::Typeface::Ptr regular, medium, semibold;
};

enum class Weight { regular, medium, semibold };

inline juce::Font font (float height, Weight w = Weight::regular, float tracking = 0.0f)
{
    static juce::SharedResourcePointer<Fonts> fonts;
    const auto& face = w == Weight::semibold ? fonts->semibold : (w == Weight::medium ? fonts->medium : fonts->regular);
    auto opts = face != nullptr ? juce::FontOptions (face) : juce::FontOptions();
    // sizes are em sizes in design pixels (CSS-like), not JUCE's ascent+descent height
    return juce::Font (opts.withPointHeight (height).withKerningFactor (tracking));
}

/** Width of a line of text without the tracking that follows its last glyph. */
inline float textWidth (const juce::Font& f, const juce::String& s)
{
    const float w = juce::GlyphArrangement::getStringWidth (f, s);
    return s.isEmpty() ? 0.0f : w - f.getExtraKerningFactor() * f.getHeight();
}

/** f shrunk (never grown) so that s fits into maxWidth: translations are longer or shorter than
    the English text the layout was drawn with. */
inline juce::Font fitted (juce::Font f, const juce::String& s, float maxWidth)
{
    const float w = textWidth (f, s);
    if (maxWidth > 0.0f && w > maxWidth)
        f = f.withHeight (f.getHeight() * maxWidth / w);
    return f;
}

/** Text whose cap height is optically centred on anchor.y; x is the left edge, centre or right edge. */
inline void drawText (juce::Graphics& g, const juce::String& text, juce::Point<float> anchor, const juce::Font& f,
                      juce::Colour c, juce::Justification just = juce::Justification::centred)
{
    const float w = textWidth (f, text);
    const float capH = f.getAscent() * 0.727f;
    float x = anchor.x;
    if (just.testFlags (juce::Justification::horizontallyCentred)) x -= w * 0.5f;
    else if (just.testFlags (juce::Justification::right))          x -= w;
    juce::GlyphArrangement ga;
    ga.addLineOfText (f, text, x, anchor.y + capH * 0.5f);
    g.setColour (c);
    ga.draw (g);
}


/** Glow without blur: wide-to-narrow strokes. Only for live elements. */
inline void strokeGlow (juce::Graphics& g, const juce::Path& p, juce::Colour c, float width, float glow = 1.0f)
{
    const auto style = [] (float w) { return juce::PathStrokeType (w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded); };
    g.setColour (c.withMultipliedAlpha (0.05f * glow)); g.strokePath (p, style (width * 5.0f));
    g.setColour (c.withMultipliedAlpha (0.10f * glow)); g.strokePath (p, style (width * 3.0f));
    g.setColour (c.withMultipliedAlpha (0.22f * glow)); g.strokePath (p, style (width * 1.8f));
    g.setColour (c);                                    g.strokePath (p, style (width));
}

inline void fillGlowCircle (juce::Graphics& g, juce::Point<float> c, float r, juce::Colour col, float glow = 1.0f)
{
    for (int i = 3; i >= 1; --i)
    {
        const float rr = r * (1.0f + 0.65f * (float) i);
        g.setColour (col.withMultipliedAlpha (0.07f * glow * (float) (4 - i)));
        g.fillEllipse (c.x - rr, c.y - rr, rr * 2.0f, rr * 2.0f);
    }
    g.setColour (col);
    g.fillEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f);
}

/** Inset well: value boxes, selectors. */
inline void fillWell (juce::Graphics& g, juce::Rectangle<float> r, float corner, bool hover = false)
{
    g.setColour (hover ? col::well.brighter (0.06f) : col::well);
    g.fillRoundedRectangle (r, corner);
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.drawLine (r.getX() + corner, r.getY() + 1.0f, r.getRight() - corner, r.getY() + 1.0f, 1.0f);
    g.setColour (hover ? col::wellEdge.brighter (0.25f) : col::wellEdge);
    g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.0f);
}

/** Uniform pseudo-random noise image used to give large surfaces a little grain. */
inline juce::Image makeNoise (int w, int h, float alpha, uint32_t seed = 1234)
{
    juce::Image img (juce::Image::ARGB, w, h, true);
    juce::Image::BitmapData bd (img, juce::Image::BitmapData::writeOnly);
    uint32_t s = seed;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
        {
            s = s * 1664525u + 1013904223u;
            const auto v = (uint8_t) (s >> 24);
            bd.setPixelColour (x, y, juce::Colour (v, v, v).withAlpha (alpha));
        }
    return img;
}
/** 1.6 Auto Character: a small "AUTO" tag on a control whose value the plugin sets (centred at c). */
inline void drawAutoBadge (juce::Graphics& g, juce::Point<float> c)
{
    const juce::Rectangle<float> r (c.x - 23.0f, c.y - 9.5f, 46.0f, 19.0f);
    g.setColour (col::accent.withAlpha (0.18f));
    g.fillRoundedRectangle (r, 6.5f);
    g.setColour (col::accent.withAlpha (0.6f));
    g.drawRoundedRectangle (r.reduced (0.5f), 6.5f, 1.0f);
    drawText (g, "AUTO", c.translated (0.0f, 0.5f), font (11.5f, Weight::medium, 0.1f), col::accentText);
}

} // namespace vui
