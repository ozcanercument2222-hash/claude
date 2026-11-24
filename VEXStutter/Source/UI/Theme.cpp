#include "UI/Theme.h"

#include "BinaryData.h"

namespace vex::ui
{
namespace
{
    struct Faces
    {
        juce::Typeface::Ptr regular, medium, semibold;

        Faces()
        {
            regular  = juce::Typeface::createSystemTypefaceFor (BinaryData::InterRegular_ttf, BinaryData::InterRegular_ttfSize);
            medium   = juce::Typeface::createSystemTypefaceFor (BinaryData::InterMedium_ttf, BinaryData::InterMedium_ttfSize);
            semibold = juce::Typeface::createSystemTypefaceFor (BinaryData::InterSemiBold_ttf, BinaryData::InterSemiBold_ttfSize);
        }
    };

    Faces& faces()
    {
        static Faces f;         // message thread; shared by every editor instance
        return f;
    }
}

juce::Font font (float height, Weight weight)
{
    auto& f = faces();
    auto face = weight == Weight::semibold ? f.semibold : (weight == Weight::medium ? f.medium : f.regular);
    if (face != nullptr)
        return juce::Font (juce::FontOptions (face).withHeight (height));
    return juce::Font (juce::FontOptions (height, weight == Weight::regular ? juce::Font::plain : juce::Font::bold));
}

void drawTracked (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area, float size,
                  juce::Colour colour, juce::Justification just, float tracking, Weight weight)
{
    const auto f = font (size, weight);
    juce::GlyphArrangement ga;
    ga.addLineOfText (f, text, 0.0f, 0.0f);
    const int n = ga.getNumGlyphs();
    if (n == 0)
        return;
    const float extra = size * tracking;
    for (int i = 1; i < n; ++i)
        ga.getGlyph (i).moveBy ((float) i * extra, 0.0f);

    const auto bounds = ga.getBoundingBox (0, n, true);
    float scale = 1.0f;
    if (bounds.getWidth() > area.getWidth() && bounds.getWidth() > 0.0f)
        scale = area.getWidth() / bounds.getWidth();

    float x = area.getX();
    const float w = bounds.getWidth() * scale;
    if (just.testFlags (juce::Justification::horizontallyCentred))  x = area.getCentreX() - w * 0.5f;
    else if (just.testFlags (juce::Justification::right))          x = area.getRight() - w;

    const float baseline = area.getCentreY() + (f.getAscent() - f.getDescent()) * 0.5f * scale;
    g.setColour (colour);
    ga.draw (g, juce::AffineTransform::translation (-bounds.getX(), 0.0f).scaled (scale).translated (x, baseline));
}
}
