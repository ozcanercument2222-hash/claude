#pragma once
//==============================================================================
//  Vektorel ikonlar ve VSTM logosu (her olcekte keskin; emoji / font glifi yok)
//==============================================================================
#include "Theme.h"

namespace vui::icons
{
inline juce::PathStrokeType stroke (float w)
{
    return juce::PathStrokeType (w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
}

/** "VSTM" wordmark centre-lines on a 131 x 26 grid (stroked 5.4 px, mitred, butt caps). */
inline juce::Path logoPath()
{
    juce::Path p;
    const float t = 2.7f, b = 23.3f;
    // V
    p.startNewSubPath (3.0f, t);
    p.lineTo (21.5f, 23.6f);
    p.lineTo (40.0f, t);
    // S
    p.startNewSubPath (70.6f, t);
    p.lineTo (50.0f, t);
    p.quadraticTo (46.5f, t, 46.5f, t + 3.5f);
    p.lineTo (46.5f, 9.3f);
    p.quadraticTo (46.5f, 12.8f, 50.0f, 12.8f);
    p.lineTo (64.0f, 12.8f);
    p.quadraticTo (67.5f, 12.8f, 67.5f, 16.3f);
    p.lineTo (67.5f, b - 3.5f);
    p.quadraticTo (67.5f, b, 64.0f, b);
    p.lineTo (43.4f, b);
    // T (its bar runs into the M)
    p.startNewSubPath (72.2f, t);
    p.lineTo (98.7f, t);
    p.startNewSubPath (84.0f, t);
    p.lineTo (84.0f, 26.0f);
    // M
    p.startNewSubPath (98.7f, 26.0f);
    p.lineTo (98.7f, t);
    p.lineTo (113.5f, 17.4f);
    p.lineTo (128.3f, t);
    p.lineTo (128.3f, 26.0f);
    return p;
}

/** Metallic VSTM wordmark; `area` gives the top-left corner and the height (width follows: 131/26). */
inline void drawLogo (juce::Graphics& g, juce::Rectangle<float> area, float brightness = 1.0f)
{
    juce::Path outline;
    juce::PathStrokeType (5.4f, juce::PathStrokeType::mitered, juce::PathStrokeType::butt).createStrokedPath (outline, logoPath());
    outline.applyTransform (juce::AffineTransform::scale (area.getHeight() / 26.0f).translated (area.getX(), area.getY()));
    const auto ob = outline.getBounds();

    if (brightness > 0.6f)
    {
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillPath (outline, juce::AffineTransform::translation (0.0f, 1.5f));
    }

    juce::ColourGradient grad (juce::Colour (0xffdcdbe1).withMultipliedBrightness (brightness), ob.getX(), ob.getY(),
                               juce::Colour (0xff86858d).withMultipliedBrightness (brightness), ob.getX(), ob.getBottom(), false);
    grad.addColour (0.45, juce::Colour (0xffbab9c1).withMultipliedBrightness (brightness));
    grad.addColour (0.56, juce::Colour (0xff9c9ba3).withMultipliedBrightness (brightness));
    g.setGradientFill (grad);
    g.fillPath (outline);
}

inline juce::Path chevron (bool right, float size = 8.0f)
{
    juce::Path p;
    const float d = right ? 1.0f : -1.0f;
    p.startNewSubPath (-0.5f * size * d, -size);
    p.lineTo (0.5f * size * d, 0.0f);
    p.lineTo (-0.5f * size * d, size);
    return p;
}

inline juce::Path chevronDown (float w = 7.0f, float h = 4.5f)
{
    juce::Path p;
    p.startNewSubPath (-w, -h * 0.5f);
    p.lineTo (0.0f, h * 0.5f + 0.5f);
    p.lineTo (w, -h * 0.5f);
    return p;
}

inline juce::Path download()
{
    juce::Path p;
    p.startNewSubPath (0.0f, -12.0f);
    p.lineTo (0.0f, 4.0f);
    p.startNewSubPath (-6.5f, -2.5f);
    p.lineTo (0.0f, 4.0f);
    p.lineTo (6.5f, -2.5f);
    p.startNewSubPath (-12.0f, 3.5f);
    p.lineTo (-12.0f, 10.5f);
    p.lineTo (12.0f, 10.5f);
    p.lineTo (12.0f, 3.5f);
    return p;
}

inline juce::Path windowIcon()
{
    juce::Path p;
    p.addRoundedRectangle (-15.0f, -12.0f, 30.0f, 24.0f, 5.0f);
    p.startNewSubPath (8.5f, -6.5f);
    p.lineTo (8.5f, 6.5f);
    return p;
}

inline juce::Path gear()
{
    juce::Path p;
    const int teeth = 8;
    const int steps = 160;
    for (int i = 0; i <= steps; ++i)
    {
        const float a = juce::MathConstants<float>::twoPi * (float) i / (float) steps;
        const float wave = 0.5f + 0.5f * std::cos (a * (float) teeth);
        const float r = 10.6f + 3.0f * std::pow (wave, 2.2f);
        const auto pt = juce::Point<float> (r * std::sin (a), -r * std::cos (a));
        if (i == 0) p.startNewSubPath (pt); else p.lineTo (pt);
    }
    p.closeSubPath();
    return p;
}

inline juce::Path pulse()
{
    juce::Path p;
    p.startNewSubPath (-9.0f, 0.5f);
    p.lineTo (-4.5f, 0.5f);
    p.lineTo (-2.0f, -5.0f);
    p.lineTo (1.5f, 5.5f);
    p.lineTo (3.5f, -0.5f);
    p.lineTo (9.0f, -0.5f);
    return p;
}

/** Padlock (stroked): a locked automatic key. About 12 x 15 units, centred on 0. */
inline juce::Path padlock()
{
    juce::Path p;
    p.addRoundedRectangle (-6.0f, -1.5f, 12.0f, 9.0f, 1.8f);
    p.startNewSubPath (-3.6f, -1.5f);
    p.lineTo (-3.6f, -4.0f);
    p.addCentredArc (0.0f, -4.0f, 3.6f, 3.4f, 0.0f, -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi);
    p.lineTo (3.6f, -1.5f);
    return p;
}

inline void drawCentred (juce::Graphics& g, const juce::Path& p, juce::Point<float> c, juce::Colour colour, float width, float scale = 1.0f)
{
    g.setColour (colour);
    g.strokePath (p, stroke (width), juce::AffineTransform::scale (scale).translated (c));
}
} // namespace vui::icons
