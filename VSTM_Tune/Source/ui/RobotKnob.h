#pragma once
//==============================================================================
//  ROBOTISM - ana (hero) knob
//
//  Sol yay (mor)  = Robotik bolge, sag yay (beyaz) = Insansi bolge, ayrim 12 yonunde.
//  Isaretci hangi tarafa donukse o bolge parlar ve isir, diger taraf soner; ortada
//  ikisi de esit (gorseldeki varsayilan gorunum). Uzerine gelince / surukleyince
//  altinda deger yazar: "Robotik 40%", "Dengeli", "Insansi 60%".
//
//  Hareketsiz govde (hale, golge, celik bilezik, kubbe, V logosu) ekranin gercek piksel
//  olceginde bir kez cizilip saklanir; deger degisince yalniz yaylar ve isaretci cizilir.
//==============================================================================
#include "Controls.h"

namespace vui
{
class RobotKnob : public ProKnob
{
public:
    RobotKnob()
    {
        setMouseDragSensitivity (360);
        setTitle ("Robotism");
    }

    /** Centre of the knob inside this component and the design radius of the value arc. */
    juce::Point<float> centre() const { return getLocalBounds().toFloat().getCentre(); }
    static constexpr float kArcR = 153.0f;

    bool hitTest (int x, int y) override
    {
        return centre().getDistanceFrom ({ (float) x, (float) y }) < 168.0f;
    }

    void paint (juce::Graphics& g) override
    {
        const float scale = g.getInternalContext().getPhysicalPixelScaleFactor();
        if (body.isNull() || std::abs (scale - bodyScale) > 0.01f)
            renderBody (scale);

        g.drawImageTransformed (body, juce::AffineTransform::scale (1.0f / bodyScale));
        drawArcs (g);
        drawPointer (g);
        if (isMouseOverOrDragging() || showReadout)
            drawReadout (g);
    }

    /** For screenshots / demos: show the readout without hovering. */
    bool showReadout = false;

private:
    //--------------------------------------------------------------------------
    void drawArcs (juce::Graphics& g)
    {
        const auto c = centre();
        const auto rp = getRotaryParameters();
        const float a0 = rp.startAngleRadians, a1 = rp.endAngleRadians, mid = 0.5f * (a0 + a1);
        const float v = proportion();
        const float robot = juce::jlimit (0.0f, 1.0f, (0.5f - v) * 2.0f), human = juce::jlimit (0.0f, 1.0f, (v - 0.5f) * 2.0f);
        const float purpleI = 1.0f - 0.62f * human, whiteI = 1.0f - 0.62f * robot;
        const float purpleGlow = 1.0f + 1.4f * robot, whiteGlow = 1.0f + 1.4f * human;
        const float w = 8.0f;
        const bool hot = isDragging();

        auto arc = [&] (float from, float to)
        {
            juce::Path p;
            p.addCentredArc (c.x, c.y, kArcR, kArcR, 0.0f, from, to, true);
            return p;
        };

        // --- glow underlays (one colour per zone)
        const auto purpleZone = arc (a0, mid), whiteZone = arc (mid, a1);
        const auto purpleGlowC = juce::Colour (0xff8a63ff);
        const auto whiteGlowC = juce::Colour (0xffe9e4ff);
        const std::pair<float, float> glowLayers[] = { { 7.0f, 0.04f }, { 3.8f, 0.08f }, { 2.1f, 0.16f } };
        for (const auto& [mul, alpha] : glowLayers)
        {
            g.setColour (purpleGlowC.withAlpha (juce::jmin (1.0f, alpha * purpleGlow * purpleI)));
            g.strokePath (purpleZone, icons::stroke (w * mul));
            g.setColour (whiteGlowC.withAlpha (juce::jmin (1.0f, alpha * whiteGlow * whiteI)));
            g.strokePath (whiteZone, icons::stroke (w * mul));
        }

        // --- core strokes, coloured along the arc (no conic gradients in JUCE: short segments)
        const int segs = 64;
        for (int zone = 0; zone < 2; ++zone)
        {
            const float from = zone == 0 ? a0 : mid, to = zone == 0 ? mid : a1;
            for (int i = 0; i < segs; ++i)
            {
                const float t0 = (float) i / segs, t1 = (float) (i + 1) / segs;
                const float tm = 0.5f * (t0 + t1);
                juce::Colour cc;
                if (zone == 0)
                {
                    // dim violet at 7:30 -> bright lilac at 12
                    const auto lo = juce::Colour (0xff2e1f60), mi = juce::Colour (0xff8a66f6), hi = juce::Colour (0xffcdb3ff);
                    cc = tm < 0.55f ? lo.interpolatedWith (mi, tm / 0.55f) : mi.interpolatedWith (hi, (tm - 0.55f) / 0.45f);
                    cc = cc.withMultipliedBrightness (0.22f + 0.78f * purpleI);
                    if (hot && v < 0.5f) cc = cc.brighter (0.15f);
                }
                else
                {
                    // lilac-white at 12 -> pure white at 4:30
                    const auto lo = juce::Colour (0xffd8d0f4), hi = juce::Colour (0xffffffff);
                    cc = lo.interpolatedWith (hi, juce::jmin (1.0f, tm * 1.6f));
                    cc = cc.withMultipliedBrightness (0.18f + 0.82f * whiteI);
                }
                juce::Path seg;
                const float e = (i == segs - 1) ? 0.0f : 0.004f;   // tiny overlap hides seams
                seg.addCentredArc (c.x, c.y, kArcR, kArcR, 0.0f, from + (to - from) * t0, from + (to - from) * t1 + e, true);
                const bool outerEnd = (zone == 0 && i == 0) || (zone == 1 && i == segs - 1);
                g.setColour (cc);
                g.strokePath (seg, juce::PathStrokeType (w, juce::PathStrokeType::curved,
                                                         outerEnd ? juce::PathStrokeType::rounded : juce::PathStrokeType::butt));
            }
        }

        // bright seam where the two zones meet
        juce::Path seam;
        seam.addCentredArc (c.x, c.y, kArcR, kArcR, 0.0f, mid - 0.012f, mid + 0.012f, true);
        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.strokePath (seam, juce::PathStrokeType (w, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
    }

    void drawPointer (juce::Graphics& g)
    {
        const auto c = centre();
        const float a = angleFor (proportion());
        const auto at = [&] (float r) { return juce::Point<float> (c.x + r * std::sin (a), c.y - r * std::cos (a)); };
        juce::Path p;
        p.startNewSubPath (at (108.0f));
        p.lineTo (at (131.0f));
        const auto colour = isDragging() ? col::accentHi : juce::Colour (0xfff3f1fa);
        g.setColour (colour.withAlpha (0.12f));
        g.strokePath (p, icons::stroke (12.0f));
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.strokePath (p, icons::stroke (6.5f), juce::AffineTransform::translation (0.0f, 1.0f));
        g.setColour (colour);
        g.strokePath (p, icons::stroke (5.0f));
    }

    void drawReadout (juce::Graphics& g)
    {
        const float v = getValue() < 49.95 ? 0.0f : (getValue() > 50.05 ? 2.0f : 1.0f);
        const auto text = getTextFromValue (getValue());
        const auto f = font (15.5f, Weight::medium, 0.04f);
        const float w = textWidth (f, text) + 34.0f;
        const auto c = centre().translated (0.0f, 165.0f);
        const auto r = juce::Rectangle<float> (w, 30.0f).withCentre (c);
        g.setColour (juce::Colour (0xff17161d).withAlpha (0.96f));
        g.fillRoundedRectangle (r, 15.0f);
        g.setColour (v < 0.5f ? col::accent.withAlpha (0.6f) : (v > 1.5f ? juce::Colours::white.withAlpha (0.45f) : col::cardEdge));
        g.drawRoundedRectangle (r.reduced (0.5f), 15.0f, 1.0f);
        drawText (g, text, r.getCentre(), f, v < 0.5f ? col::accentHi : col::text);
    }

    //--------------------------------------------------------------------------
    void renderBody (float scale)
    {
        bodyScale = scale;
        const int w = juce::roundToInt ((float) getWidth() * scale), h = juce::roundToInt ((float) getHeight() * scale);
        body = juce::Image (juce::Image::ARGB, juce::jmax (1, w), juce::jmax (1, h), true);
        juce::Graphics g (body);
        g.addTransform (juce::AffineTransform::scale (scale));

        const auto c = centre();
        const auto rp = getRotaryParameters();
        const float a0 = rp.startAngleRadians, a1 = rp.endAngleRadians;

        // unlit groove under the arc
        {
            juce::Path groove;
            groove.addCentredArc (c.x, c.y, kArcR, kArcR, 0.0f, a0, a1, true);
            g.setColour (juce::Colour (0xff08080a));
            g.strokePath (groove, icons::stroke (11.0f));
        }

        // big soft drop shadow
        {
            juce::ColourGradient sh (juce::Colours::black.withAlpha (0.92f), c.x, c.y + 22.0f,
                                     juce::Colours::transparentBlack, c.x, c.y + 22.0f + 172.0f, true);
            g.setGradientFill (sh);
            g.fillEllipse (c.x - 172.0f, c.y + 22.0f - 172.0f, 344.0f, 344.0f);
        }

        // steel bezel ring with fine ticks
        const float rOut = 138.0f, rIn = 114.0f;
        {
            juce::ColourGradient bg (juce::Colour (0xff35343b), c.x, c.y - rOut, juce::Colour (0xff0c0c0e), c.x, c.y + rOut, false);
            bg.addColour (0.45, juce::Colour (0xff1e1d23));
            g.setGradientFill (bg);
            g.fillEllipse (c.x - rOut, c.y - rOut, rOut * 2.0f, rOut * 2.0f);

            // knurl / tick texture along the travel
            for (int i = 0; i <= 90; ++i)
            {
                const float a = a0 + (a1 - a0) * (float) i / 90.0f;
                const bool major = (i % 9) == 0;
                const float r0 = major ? 119.0f : 122.0f, r1 = 133.0f;
                const auto p0 = juce::Point<float> (c.x + r0 * std::sin (a), c.y - r0 * std::cos (a));
                const auto p1 = juce::Point<float> (c.x + r1 * std::sin (a), c.y - r1 * std::cos (a));
                const float lit = 0.5f + 0.5f * -std::cos (a);                 // brighter toward the top
                g.setColour (juce::Colour (0xff5a5964).withAlpha ((major ? 0.6f : 0.32f) * (0.35f + 0.65f * lit)));
                g.drawLine ({ p0, p1 }, major ? 1.3f : 0.9f);
            }

            // rim highlights
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff5d5c66), c.x, c.y - rOut,
                                                     juce::Colour (0xff121115), c.x, c.y + rOut * 0.3f, false));
            g.drawEllipse (c.x - rOut + 0.7f, c.y - rOut + 0.7f, rOut * 2.0f - 1.4f, rOut * 2.0f - 1.4f, 1.6f);
            g.setColour (juce::Colours::black.withAlpha (0.8f));
            g.drawEllipse (c.x - rIn - 1.5f, c.y - rIn - 1.5f, rIn * 2.0f + 3.0f, rIn * 2.0f + 3.0f, 3.0f);
        }

        // domed cap
        const float rD = 111.0f;
        {
            juce::ColourGradient dg (juce::Colour (0xff35343b), c.x - 20.0f, c.y - 70.0f, juce::Colour (0xff111014), c.x + 30.0f, c.y + rD, true);
            dg.addColour (0.55, juce::Colour (0xff222127));
            g.setGradientFill (dg);
            g.fillEllipse (c.x - rD, c.y - rD, rD * 2.0f, rD * 2.0f);

            // fine grain (leather-like) clipped to the cap
            juce::Graphics::ScopedSaveState ss (g);
            juce::Path cap;
            cap.addEllipse (c.x - rD, c.y - rD, rD * 2.0f, rD * 2.0f);
            g.reduceClipRegion (cap);
            const auto noise = makeNoise (juce::roundToInt (rD * 2.0f), juce::roundToInt (rD * 2.0f), 0.055f, 77);
            g.drawImageAt (noise, juce::roundToInt (c.x - rD), juce::roundToInt (c.y - rD));
        }
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.20f), c.x, c.y - rD,
                                                 juce::Colours::white.withAlpha (0.0f), c.x, c.y + rD * 0.2f, false));
        g.drawEllipse (c.x - rD, c.y - rD, rD * 2.0f, rD * 2.0f, 1.6f);
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.drawEllipse (c.x - rD + 3.0f, c.y - rD + 3.0f, rD * 2.0f - 6.0f, rD * 2.0f - 6.0f, 1.0f);

        // engraved V
        {
            juce::Path v;
            v.startNewSubPath (c.x - 29.5f, c.y - 14.0f);
            v.lineTo (c.x, c.y + 13.0f);
            v.lineTo (c.x + 29.5f, c.y - 14.0f);
            const auto st = juce::PathStrokeType (6.6f, juce::PathStrokeType::mitered, juce::PathStrokeType::butt);
            g.setColour (juce::Colours::black.withAlpha (0.7f));
            g.strokePath (v, st, juce::AffineTransform::translation (0.0f, -1.2f));
            g.setColour (juce::Colours::white.withAlpha (0.10f));
            g.strokePath (v, st, juce::AffineTransform::translation (0.0f, 1.3f));
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff7a7983), c.x, c.y - 14.0f, juce::Colour (0xff57565e), c.x, c.y + 14.0f, false));
            g.strokePath (v, st);
        }
    }

    juce::Image body;
    float bodyScale = 0.0f;
};
} // namespace vui
