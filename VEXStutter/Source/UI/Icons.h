#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>

/** Vector icons drawn in a 1 x 1 box and scaled to their area: crisp at every UI scale, no bitmaps. */
namespace vex::ui::icons
{
enum class Id
{
    stutter, repeat, gate, reverse, glitch,
    clock, sparkle, glitchType, intensity, note, waves, shuffle, layers, shape,
    save, folder, gear, power, snowflake, chevronLeft, chevronRight, logoWave, sliders, close
};

struct Shape
{
    juce::Path stroke, fill;
};

inline void star (juce::Path& p, float cx, float cy, float r)
{
    p.startNewSubPath (cx, cy - r);
    p.quadraticTo (cx, cy, cx + r, cy);
    p.quadraticTo (cx, cy, cx, cy + r);
    p.quadraticTo (cx, cy, cx - r, cy);
    p.quadraticTo (cx, cy, cx, cy - r);
    p.closeSubPath();
}

inline void polyline (juce::Path& p, std::initializer_list<float> xy)
{
    const float* v = xy.begin();
    const auto n = (int) (xy.size() / 2);
    p.startNewSubPath (v[0], v[1]);
    for (int i = 1; i < n; ++i)
        p.lineTo (v[2 * i], v[2 * i + 1]);
}

inline Shape make (Id id)
{
    Shape s;
    auto& st = s.stroke;
    auto& fi = s.fill;
    constexpr float pi = 3.14159265f;
    switch (id)
    {
        case Id::stutter:
        {
            const float h[] = { 0.34f, 0.62f, 0.92f, 0.62f, 0.34f };
            for (int i = 0; i < 5; ++i)
            {
                const float x = 0.12f + 0.19f * (float) i;
                st.startNewSubPath (x, 0.5f - h[i] * 0.5f);
                st.lineTo (x, 0.5f + h[i] * 0.5f);
            }
            break;
        }
        case Id::repeat:
            st.startNewSubPath (0.08f, 0.42f);
            st.lineTo (0.08f, 0.3f);
            st.quadraticTo (0.08f, 0.14f, 0.24f, 0.14f);
            st.lineTo (0.76f, 0.14f);
            st.quadraticTo (0.92f, 0.14f, 0.92f, 0.3f);
            st.lineTo (0.92f, 0.38f);
            polyline (st, { 0.8f, 0.27f, 0.92f, 0.4f, 1.0f, 0.27f });
            st.startNewSubPath (0.92f, 0.58f);
            st.lineTo (0.92f, 0.7f);
            st.quadraticTo (0.92f, 0.86f, 0.76f, 0.86f);
            st.lineTo (0.24f, 0.86f);
            st.quadraticTo (0.08f, 0.86f, 0.08f, 0.7f);
            st.lineTo (0.08f, 0.62f);
            polyline (st, { 0.0f, 0.73f, 0.08f, 0.6f, 0.2f, 0.73f });
            break;
        case Id::gate:
            polyline (st, { 0.02f, 0.82f, 0.28f, 0.82f, 0.28f, 0.18f, 0.72f, 0.18f, 0.72f, 0.82f, 0.98f, 0.82f });
            break;
        case Id::reverse:
            fi.addTriangle (0.5f, 0.18f, 0.5f, 0.82f, 0.04f, 0.5f);
            fi.addTriangle (0.96f, 0.18f, 0.96f, 0.82f, 0.5f, 0.5f);
            break;
        case Id::glitch:
            for (auto [x, y] : { std::pair { 0.04f, 0.04f }, { 0.68f, 0.04f }, { 0.36f, 0.36f }, { 0.04f, 0.68f }, { 0.68f, 0.68f } })
                fi.addRectangle (x, y, 0.28f, 0.28f);
            fi.addRectangle (0.36f, 0.04f, 0.14f, 0.14f);
            fi.addRectangle (0.82f, 0.4f, 0.14f, 0.14f);
            fi.addRectangle (0.18f, 0.82f, 0.14f, 0.14f);
            break;
        case Id::clock:
            st.addEllipse (0.06f, 0.06f, 0.88f, 0.88f);
            polyline (st, { 0.5f, 0.24f, 0.5f, 0.5f, 0.68f, 0.62f });
            break;
        case Id::sparkle:
            star (fi, 0.42f, 0.56f, 0.42f);
            star (fi, 0.82f, 0.18f, 0.17f);
            break;
        case Id::glitchType:
            polyline (st, { 0.0f, 0.5f, 0.12f, 0.5f, 0.22f, 0.22f, 0.32f, 0.8f, 0.42f, 0.06f, 0.52f, 0.94f,
                            0.62f, 0.3f, 0.72f, 0.68f, 0.82f, 0.5f, 1.0f, 0.5f });
            break;
        case Id::intensity:
        {
            const float h[] = { 0.3f, 0.55f, 0.85f, 1.0f, 0.85f, 0.55f, 0.3f };
            for (int i = 0; i < 7; ++i)
            {
                const float x = 0.06f + 0.147f * (float) i;
                st.startNewSubPath (x, 0.5f - h[i] * 0.45f);
                st.lineTo (x, 0.5f + h[i] * 0.45f);
            }
            break;
        }
        case Id::note:
            polyline (st, { 0.36f, 0.78f, 0.36f, 0.12f, 0.9f, 0.04f, 0.9f, 0.7f });
            st.startNewSubPath (0.36f, 0.27f);
            st.lineTo (0.9f, 0.19f);
            fi.addEllipse (0.08f, 0.66f, 0.3f, 0.24f);
            fi.addEllipse (0.62f, 0.58f, 0.3f, 0.24f);
            break;
        case Id::waves:
            for (float y : { 0.25f, 0.5f, 0.75f })
            {
                st.startNewSubPath (0.02f, y);
                st.cubicTo (0.2f, y - 0.16f, 0.32f, y - 0.16f, 0.5f, y);
                st.cubicTo (0.68f, y + 0.16f, 0.8f, y + 0.16f, 0.98f, y);
            }
            break;
        case Id::shuffle:
            st.startNewSubPath (0.02f, 0.24f);
            st.lineTo (0.3f, 0.24f);
            st.cubicTo (0.52f, 0.24f, 0.5f, 0.76f, 0.72f, 0.76f);
            st.lineTo (0.94f, 0.76f);
            polyline (st, { 0.8f, 0.62f, 0.96f, 0.76f, 0.8f, 0.9f });
            st.startNewSubPath (0.02f, 0.76f);
            st.lineTo (0.3f, 0.76f);
            st.cubicTo (0.52f, 0.76f, 0.5f, 0.24f, 0.72f, 0.24f);
            st.lineTo (0.94f, 0.24f);
            polyline (st, { 0.8f, 0.1f, 0.96f, 0.24f, 0.8f, 0.38f });
            break;
        case Id::layers:
            polyline (st, { 0.5f, 0.06f, 0.96f, 0.3f, 0.5f, 0.54f, 0.04f, 0.3f });
            st.closeSubPath();
            polyline (st, { 0.04f, 0.5f, 0.5f, 0.74f, 0.96f, 0.5f });
            polyline (st, { 0.04f, 0.7f, 0.5f, 0.94f, 0.96f, 0.7f });
            break;
        case Id::shape:
            polyline (st, { 0.02f, 0.86f, 0.22f, 0.86f, 0.36f, 0.14f, 0.64f, 0.14f, 0.78f, 0.86f, 0.98f, 0.86f });
            break;
        case Id::save:
            polyline (st, { 0.08f, 0.58f, 0.08f, 0.92f, 0.92f, 0.92f, 0.92f, 0.58f });
            st.startNewSubPath (0.5f, 0.06f);
            st.lineTo (0.5f, 0.68f);
            polyline (st, { 0.27f, 0.46f, 0.5f, 0.69f, 0.73f, 0.46f });
            break;
        case Id::folder:
            polyline (st, { 0.06f, 0.84f, 0.06f, 0.16f, 0.38f, 0.16f, 0.48f, 0.28f, 0.94f, 0.28f, 0.94f, 0.84f });
            st.closeSubPath();
            st.startNewSubPath (0.06f, 0.4f);
            st.lineTo (0.94f, 0.4f);
            break;
        case Id::gear:
        {
            const int teeth = 8;
            for (int i = 0; i < teeth * 2; ++i)
            {
                const float a0 = (float) i * pi / (float) teeth;
                const float r = (i % 2 == 0) ? 0.47f : 0.36f;
                const float a1 = a0 + pi / (float) teeth;
                const float x0 = 0.5f + r * std::sin (a0), y0 = 0.5f - r * std::cos (a0);
                const float x1 = 0.5f + r * std::sin (a1), y1 = 0.5f - r * std::cos (a1);
                if (i == 0) st.startNewSubPath (x0, y0); else st.lineTo (x0, y0);
                st.lineTo (x1, y1);
            }
            st.closeSubPath();
            st.addEllipse (0.34f, 0.34f, 0.32f, 0.32f);
            break;
        }
        case Id::power:
            st.addCentredArc (0.5f, 0.55f, 0.4f, 0.4f, 0.0f, pi * 0.22f, pi * 1.78f, true);
            st.startNewSubPath (0.5f, 0.04f);
            st.lineTo (0.5f, 0.46f);
            break;
        case Id::snowflake:
            for (int i = 0; i < 3; ++i)
            {
                const float a = (float) i * pi / 3.0f;
                const float dx = 0.46f * std::sin (a), dy = 0.46f * std::cos (a);
                st.startNewSubPath (0.5f - dx, 0.5f - dy);
                st.lineTo (0.5f + dx, 0.5f + dy);
                for (float sgn : { -1.0f, 1.0f })
                {
                    const float ex = 0.5f + sgn * dx * 0.62f, ey = 0.5f + sgn * dy * 0.62f;
                    const float bx = sgn * 0.16f * std::sin (a + 0.9f), by = sgn * 0.16f * std::cos (a + 0.9f);
                    const float cx = sgn * 0.16f * std::sin (a - 0.9f), cy = sgn * 0.16f * std::cos (a - 0.9f);
                    polyline (st, { ex + bx, ey + by, ex, ey, ex + cx, ey + cy });
                }
            }
            break;
        case Id::chevronLeft:
            polyline (st, { 0.66f, 0.12f, 0.3f, 0.5f, 0.66f, 0.88f });
            break;
        case Id::chevronRight:
            polyline (st, { 0.34f, 0.12f, 0.7f, 0.5f, 0.34f, 0.88f });
            break;
        case Id::logoWave:
            polyline (st, { 0.0f, 0.5f, 0.1f, 0.5f, 0.18f, 0.3f, 0.26f, 0.72f, 0.34f, 0.06f, 0.44f, 0.94f, 0.53f, 0.22f,
                            0.62f, 0.78f, 0.7f, 0.38f, 0.78f, 0.6f, 0.86f, 0.5f, 1.0f, 0.5f });
            break;
        case Id::sliders:
            for (auto [y, x] : { std::pair { 0.2f, 0.66f }, { 0.5f, 0.32f }, { 0.8f, 0.58f } })
            {
                st.startNewSubPath (0.04f, y);
                st.lineTo (0.96f, y);
                fi.addEllipse (x - 0.11f, y - 0.11f, 0.22f, 0.22f);
            }
            break;
        case Id::close:
            st.startNewSubPath (0.18f, 0.18f); st.lineTo (0.82f, 0.82f);
            st.startNewSubPath (0.82f, 0.18f); st.lineTo (0.18f, 0.82f);
            break;
    }
    return s;
}

/** Draws icon `id` into `area` (kept square, centred). `weight` scales the stroke width. */
inline void draw (juce::Graphics& g, Id id, juce::Rectangle<float> area, juce::Colour colour, float weight = 1.0f)
{
    const float size = std::min (area.getWidth(), area.getHeight());
    const auto box = juce::Rectangle<float> (size, size).withCentre (area.getCentre());
    const auto t = juce::AffineTransform::scale (size).translated (box.getX(), box.getY());
    const auto shape = make (id);
    g.setColour (colour);
    if (! shape.fill.isEmpty())
        g.fillPath (shape.fill, t);
    if (! shape.stroke.isEmpty())
        g.strokePath (shape.stroke, juce::PathStrokeType (std::max (1.0f, size * 0.085f * weight), juce::PathStrokeType::curved,
                                                          juce::PathStrokeType::rounded), t);
}
}
