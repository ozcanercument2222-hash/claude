#ifndef SCC_UI_COMPONENTS_H_INCLUDED
#define SCC_UI_COMPONENTS_H_INCLUDED

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <cmath>
#include <functional>
#include <memory>
#include "SCCDsp.h"
#include "SCCCharacterTypes.h"
#include "SCCKnobStyle.h"

//==============================================================================
//  SCC arayuz parcalari (karanlik panel + neon yesil)
//==============================================================================
namespace sccui
{

//------------------------------------------------------------------------------
//  Karakter tiplerinin arayuzdeki kisa adi ve tek satirlik aciklamasi
//------------------------------------------------------------------------------
inline const char* characterTypeLabel (int type) noexcept
{
    static const char* const labels[scc::kNumCharacterTypes] =
        { "CLEAN", "WARM", "TAPE", "TUBE", "XFORMER", "DRIVE", "CUSTOM" };
    return labels[type < 0 ? 0 : (type >= scc::kNumCharacterTypes ? scc::kNumCharacterTypes - 1 : type)];
}

inline const char* characterTypeDescription (int type) noexcept
{
    static const char* const text[scc::kNumCharacterTypes] =
    {
        "CLEAN  -  transparent peak rounding; nothing is added below the knee",
        "WARM  -  even-order warmth focused on the low-mids, gently softened top",
        "TAPE  -  soft odd-order saturation, self-compressing highs, 75 Hz head bump",
        "TUBE  -  asymmetric even-order drive; the bias shifts with the signal level",
        "TRANSFORMER  -  core saturation on the lows, clean highs",
        "DRIVE  -  two-stage overdrive into a hard clipper, tightened low end",
        "CUSTOM  -  your own character, built in the DESIGNER",
    };
    return text[type < 0 ? 0 : (type >= scc::kNumCharacterTypes ? scc::kNumCharacterTypes - 1 : type)];
}

//------------------------------------------------------------------------------
//  Renkler
//------------------------------------------------------------------------------
inline const juce::Colour kBgTop    { 0xff111311 };
inline const juce::Colour kBgBottom { 0xff050605 };
inline const juce::Colour kEdge     { 0xff2c312c };
inline const juce::Colour kNeon     { 0xffc8f72d };
inline const juce::Colour kNeonDeep { 0xff55c21a };
inline const juce::Colour kText     { 0xffdde2dd };
inline const juce::Colour kTextDim  { 0xff8c948c };
inline const juce::Colour kBox      { 0xff0c0e0c };
inline const juce::Colour kAmber    { 0xffffa62b };     // bypass, tek harmonikler, uyari

//------------------------------------------------------------------------------
//  Yardimcilar
//------------------------------------------------------------------------------
inline void useFont (juce::Graphics& g, float height, bool bold)
{
    g.setFont (g.getCurrentFont().withHeight (height)
                                 .withStyle (bold ? juce::Font::bold : juce::Font::plain));
}

inline void drawLabel (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area,
                       float size, juce::Colour colour, bool bold = false,
                       juce::Justification just = juce::Justification::centred)
{
    // Sigmayan yazi kirpilmaz, yatayda daraltilir (yazi tipi genislikleri isletim
    // sistemine gore degisir; ayni yerlesim her yerde okunakli kalsin).
    useFont (g, size, bold);
    g.setColour (colour);
    g.drawFittedText (text, area.getSmallestIntegerContainer(), just, 1, 0.7f);
}

// Parlayan cizgi: 3 kat, genisten inceye
inline void strokeGlow (juce::Graphics& g, const juce::Path& p, juce::Colour c, float width)
{
    g.setColour (c.withAlpha (0.07f));
    g.strokePath (p, juce::PathStrokeType (width * 4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (c.withAlpha (0.18f));
    g.strokePath (p, juce::PathStrokeType (width * 2.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (c);
    g.strokePath (p, juce::PathStrokeType (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

//==============================================================================
//  Knob. Ana knob, Analog Mode ve Character Designer ayni cizimi kullanir; cizimin
//  kendisi SCCKnobStyle.h'dedir (celik cerceve, cam kubbe, beyaz yay, isaretci).
//  Slider ozellikleri (slider.getProperties()):
//    "bipolar"       : true ise yay ortadan baslar
//    "centerReadout" : false ise kubbenin ortasina deger yazilmaz (varsayilan true)
//  Ortadaki yazi, slider'in textFromValueFunction'indan gelir.
//==============================================================================
class KnobLookAndFeel : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                           float pos, float startAngle, float endAngle, juce::Slider& slider) override
    {
        knobstyle::Params p;
        p.pos        = pos;
        p.startAngle = startAngle;
        p.endAngle   = endAngle;
        p.bipolar    = (bool) slider.getProperties().getWithDefault ("bipolar", false);
        p.enabled    = slider.isEnabled();
        p.hot        = slider.isMouseOverOrDragging();
        if ((bool) slider.getProperties().getWithDefault ("centerReadout", true))
            p.readout = slider.getTextFromValue (slider.getValue());

        renderer.draw (g, { (float) x, (float) y, (float) width, (float) height }, p);
    }

private:
    knobstyle::KnobRenderer renderer;       // degerden bagimsiz katmanin onbellegi bu ornege ozeldir
};

//==============================================================================
//  Neon dugme (ratio tuslari, auto gain)
//==============================================================================
class NeonButton : public juce::Button
{
public:
    explicit NeonButton (const juce::String& labelText, float fontSize = 14.0f, bool led = false)
        : juce::Button (labelText), label (labelText), size (fontSize), withLed (led) {}

    // acikken kullanilacak renk (varsayilan neon yesil)
    void setAccent (juce::Colour c)
    {
        accent = c;
        repaint();
    }

    void setLabel (const juce::String& newLabel)
    {
        if (newLabel != label)
        {
            label = newLabel;
            repaint();
        }
    }

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        const auto r = getLocalBounds().toFloat().reduced (3.0f);
        const bool on = getToggleState();
        const float corner = r.getHeight() * 0.22f;

        if (! isEnabled())
        {
            g.setColour (kBox);
            g.fillRoundedRectangle (r, corner);
            g.setColour (kEdge.withAlpha (0.6f));
            g.drawRoundedRectangle (r, corner, 1.0f);
            drawLabel (g, label, r, size, kTextDim.withAlpha (0.35f), false);
            return;
        }

        if (on)
        {
            g.setColour (accent.withAlpha (0.10f));
            g.fillRoundedRectangle (r.expanded (2.0f), corner + 2.0f);
        }

        g.setColour (down ? juce::Colour (0xff1a1e1a) : kBox);
        g.fillRoundedRectangle (r, corner);

        g.setColour (on ? accent : (over ? kTextDim : kEdge));
        g.drawRoundedRectangle (r, corner, on ? 2.0f : 1.2f);

        auto textArea = r;
        if (withLed)
        {
            auto ledArea = textArea.removeFromLeft (r.getHeight() * 0.8f);
            const auto c = ledArea.getCentre();
            if (on)
            {
                g.setColour (accent.withAlpha (0.30f));
                g.fillEllipse (c.x - 6.0f, c.y - 6.0f, 12.0f, 12.0f);
            }
            g.setColour (on ? accent : kEdge.brighter (0.3f));
            g.fillEllipse (c.x - 3.5f, c.y - 3.5f, 7.0f, 7.0f);
        }

        drawLabel (g, label, textArea, size, on ? accent : (over ? kText : kTextDim), on);
    }

private:
    juce::String label;
    float size;
    bool withLed;
    juce::Colour accent = kNeon;
};

//==============================================================================
//  Karakter dugmesi (ikon + yazi)
//==============================================================================
class ModeButton : public juce::Button
{
public:
    enum Kind { smooth = 0, balanced = 1, hard = 2 };

    ModeButton (const juce::String& labelText, Kind k) : juce::Button (labelText), label (labelText), kind (k) {}

    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        const bool on = getToggleState();
        auto b = getLocalBounds().toFloat();
        const auto box = b.removeFromTop (b.getHeight() * 0.62f).reduced (3.0f);
        const float corner = box.getHeight() * 0.22f;

        if (on)
        {
            g.setColour (kNeon.withAlpha (0.10f));
            g.fillRoundedRectangle (box.expanded (2.0f), corner + 2.0f);
        }
        g.setColour (kBox);
        g.fillRoundedRectangle (box, corner);
        g.setColour (on ? kNeon : (over ? kTextDim : kEdge));
        g.drawRoundedRectangle (box, corner, on ? 2.0f : 1.2f);

        // ikon
        const auto ic = box.reduced (box.getWidth() * 0.24f, box.getHeight() * 0.26f);
        juce::Path p;
        if (kind == hard)
        {
            p.startNewSubPath (ic.getX(), ic.getY());
            p.lineTo (ic.getCentreX(), ic.getY());
            p.lineTo (ic.getCentreX(), ic.getBottom());
            p.lineTo (ic.getRight(), ic.getBottom());
        }
        else
        {
            const float amp = (kind == smooth) ? 0.50f : 0.28f;
            for (int i = 0; i <= 28; ++i)
            {
                const float t = (float) i / 28.0f;
                const float px = ic.getX() + t * ic.getWidth();
                const float py = ic.getCentreY() - std::sin (t * 2.0f * juce::MathConstants<float>::pi) * ic.getHeight() * amp;
                if (i == 0) p.startNewSubPath (px, py); else p.lineTo (px, py);
            }
        }
        g.setColour (on ? kNeon : kTextDim);
        g.strokePath (p, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        drawLabel (g, label, b, 11.0f, on ? kNeon : kTextDim, on);
    }

private:
    juce::String label;
    Kind kind;
};

//==============================================================================
//  Yukari / asagi ok (custom ratio)
//==============================================================================
class ChevronButton : public juce::Button
{
public:
    enum Direction { PointUp, PointDown, PointLeft, PointRight };

    explicit ChevronButton (bool pointsUp) : juce::Button ("chevron"), dir (pointsUp ? PointUp : PointDown) {}
    explicit ChevronButton (Direction d)   : juce::Button ("chevron"), dir (d) {}

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        const auto r = getLocalBounds().toFloat();
        const auto c = r.getCentre();

        juce::Path p;
        if (dir == PointUp || dir == PointDown)
        {
            const float w = r.getWidth() * 0.26f;
            const float h = r.getHeight() * 0.17f;
            const float d = (dir == PointUp) ? 1.0f : -1.0f;
            p.startNewSubPath (c.x - w, c.y + d * h);
            p.lineTo (c.x, c.y - d * h);
            p.lineTo (c.x + w, c.y + d * h);
        }
        else
        {
            const float h = r.getHeight() * 0.26f;      // dikey yarim boy
            const float w = r.getWidth() * 0.17f;       // yatay yarim boy
            const float d = (dir == PointLeft) ? 1.0f : -1.0f;
            p.startNewSubPath (c.x + d * w, c.y - h);
            p.lineTo (c.x - d * w, c.y);
            p.lineTo (c.x + d * w, c.y + h);
        }

        g.setColour (down ? kNeon : (over ? kText : kTextDim));
        g.strokePath (p, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

private:
    Direction dir;
};

//==============================================================================
//  Stereo seviye metresi (parcali LED)
//==============================================================================
class LevelMeter : public juce::Component
{
public:
    LevelMeter() { setInterceptsMouseClicks (false, false); }

    void setLevelsDb (float left, float right)
    {
        if (std::abs (left - dbL) > 0.0f || std::abs (right - dbR) > 0.0f)
        {
            dbL = left;
            dbR = right;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        constexpr int segs = 36;
        const auto area = getLocalBounds().toFloat();
        const float gap = 3.0f;
        const float barW = (area.getWidth() - gap) * 0.5f;
        const float segH = area.getHeight() / (float) segs;
        const float dbPerSeg = 60.0f / (float) segs;

        for (int ch = 0; ch < 2; ++ch)
        {
            const float level = (ch == 0) ? dbL : dbR;
            const float x = area.getX() + (float) ch * (barW + gap);

            for (int i = 0; i < segs; ++i)
            {
                const float segDb = -60.0f + dbPerSeg * (float) i;      // bu parcanin alt siniri
                const float y = area.getBottom() - (float) (i + 1) * segH;
                const bool lit = level > segDb + dbPerSeg * 0.25f;

                juce::Colour c = kNeonDeep.interpolatedWith (kNeon, (float) i / (float) (segs - 1));
                if (segDb >= -3.0f) c = juce::Colour (0xffffa62b);
                if (segDb >= -0.5f) c = juce::Colour (0xffff4d3a);

                g.setColour (lit ? c : kNeonDeep.withAlpha (0.10f));
                g.fillRect (juce::Rectangle<float> (x, y + 0.7f, barW, segH - 1.4f));
            }
        }
    }

private:
    float dbL = -90.0f, dbR = -90.0f;
};

//==============================================================================
//  Threshold gostergesi (dikey, sadece gosterir - knob surer)
//==============================================================================
class ThresholdDisplay : public juce::Component
{
public:
    ThresholdDisplay() { setInterceptsMouseClicks (false, false); }

    // threshold (dB) ve diz (knee) genisligi (dB)
    void setValues (float thresholdDb, float kneeWidthDb)
    {
        if (std::abs (thresholdDb - value) > 0.01f || std::abs (kneeWidthDb - knee) > 0.01f)
        {
            value = thresholdDb;
            knee  = kneeWidthDb;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        const float trackX = r.getX() + r.getWidth() * 0.46f;
        const float top    = r.getY() + 6.0f;
        const float bottom = r.getBottom() - 6.0f;
        const float y = top + (bottom - top) * juce::jlimit (0.0f, 1.0f, -value / 60.0f);

        // yol
        g.setColour (juce::Colour (0xff1a1d1a));
        g.fillRoundedRectangle (trackX - 2.0f, top, 4.0f, bottom - top, 2.0f);

        // marker'in altindaki bolum (kompresyonun calistigi alan)
        g.setColour (kNeonDeep.withAlpha (0.65f));
        g.fillRoundedRectangle (trackX - 2.0f, y, 4.0f, bottom - y, 2.0f);

        // diz (knee) bolgesi: esigin etrafinda [T - W/2 , T + W/2]
        {
            const float yTop = top + (bottom - top) * juce::jlimit (0.0f, 1.0f, -(value + knee * 0.5f) / 60.0f);
            const float yBot = top + (bottom - top) * juce::jlimit (0.0f, 1.0f, -(value - knee * 0.5f) / 60.0f);
            g.setColour (kNeon.withAlpha (0.16f));
            g.fillRoundedRectangle (trackX - 7.0f, yTop, 14.0f, std::max (yBot - yTop, 2.0f), 3.0f);
            g.setColour (kNeon.withAlpha (0.55f));
            g.fillRect (trackX - 9.0f, yTop - 0.5f, 18.0f, 1.0f);
            g.fillRect (trackX - 9.0f, yBot - 0.5f, 18.0f, 1.0f);
        }

        // uc ve alt isaret
        g.setColour (kTextDim.withAlpha (0.6f));
        g.fillRect (trackX + 6.0f, top - 0.5f, 8.0f, 1.0f);
        g.fillRect (trackX + 6.0f, bottom - 0.5f, 8.0f, 1.0f);
        drawLabel (g, "0",   { trackX + 18.0f, top - 7.0f,    30.0f, 14.0f }, 11.0f, kTextDim, false, juce::Justification::centredLeft);
        drawLabel (g, "-60", { trackX + 18.0f, bottom - 7.0f, 30.0f, 14.0f }, 11.0f, kTextDim, false, juce::Justification::centredLeft);

        // ucgen isaretci
        juce::Path tri;
        tri.addTriangle (trackX - 20.0f, y - 8.0f, trackX - 20.0f, y + 8.0f, trackX - 5.0f, y);
        g.setColour (kNeon.withAlpha (0.25f));
        g.fillPath (tri, juce::AffineTransform::scale (1.35f, 1.35f, trackX - 12.0f, y));
        g.setColour (kNeon);
        g.fillPath (tri);
    }

private:
    float value = -1.0f;
    float knee  = 0.0f;
};

//==============================================================================
//  Okunan deger kutusu (ratio / attack / release)
//==============================================================================
class ReadoutBox : public juce::Component
{
public:
    explicit ReadoutBox (float fontSize = 17.0f) : size (fontSize) { setInterceptsMouseClicks (false, false); }

    // preset kutusu gibi tiklanabilir yapmak icin
    void makeClickable()
    {
        setInterceptsMouseClicks (true, false);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (onClick && e.mouseWasClicked())
            onClick();
    }

    void setText (const juce::String& t)
    {
        if (t != text)
        {
            text = t;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat().reduced (1.5f);
        g.setColour (kBox);
        g.fillRoundedRectangle (r, 7.0f);
        g.setColour (kEdge.brighter (0.25f));
        g.drawRoundedRectangle (r, 7.0f, 1.3f);
        drawLabel (g, text, r, size, kNeon);
    }

    std::function<void()> onClick;

private:
    juce::String text;
    float size;
};

//==============================================================================
//  Kutusuz buyuk deger yazisi (AMOUNT % ve THRESHOLD dB)
//==============================================================================
class ValueText : public juce::Component
{
public:
    explicit ValueText (float fontSize) : size (fontSize) { setInterceptsMouseClicks (false, false); }

    void setText (const juce::String& t)
    {
        if (t != text)
        {
            text = t;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        const float off[4][2] = { { -1.0f, 0.0f }, { 1.0f, 0.0f }, { 0.0f, -1.0f }, { 0.0f, 1.0f } };
        for (const auto& o : off)                                   // hafif parlama
            drawLabel (g, text, r.translated (o[0], o[1]), size, kNeon.withAlpha (0.12f), false);
        drawLabel (g, text, r, size, kNeon, false);
    }

private:
    juce::String text;
    float size;
};

//==============================================================================
//  Dalga formu ekrani: gri = giris zarfi, yesil = gain reduction
//==============================================================================
class ScopeView : public juce::Component
{
public:
    explicit ScopeView (scc::Telemetry& t) : tel (t) { setInterceptsMouseClicks (false, false); }

    void setBypassed (bool shouldBeBypassed)
    {
        if (shouldBeBypassed != bypassed)
        {
            bypassed = shouldBeBypassed;
            repaint();
        }
    }

    // sag ust kose: calisma modu;  sol ust kose: karakter (aciksa)
    void setTags (const juce::String& modeText, const juce::String& characterText)
    {
        modeTag = modeText;
        charTag = characterText;
    }

    void paint (juce::Graphics& g) override
    {
        constexpr int N = 330;                                             // gosterilen kolon (~3.3 sn)
        constexpr std::uint32_t mask = (std::uint32_t) scc::Telemetry::kScopeSize - 1u;

        const auto r = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff070807));
        g.fillRoundedRectangle (r, 9.0f);
        g.setColour (kEdge);
        g.drawRoundedRectangle (r.reduced (0.5f), 9.0f, 1.3f);

        const auto area = r.reduced (9.0f, 9.0f);

        // izgara
        g.setColour (juce::Colours::white.withAlpha (0.04f));
        for (int i = 1; i < 10; ++i)
            g.fillRect (area.getX() + area.getWidth() * (float) i / 10.0f, area.getY(), 1.0f, area.getHeight());
        for (int i = 1; i < 6; ++i)
            g.fillRect (area.getX(), area.getY() + area.getHeight() * (float) i / 6.0f, area.getWidth(), 1.0f);

        // veriyi cek (en eski -> en yeni)
        const std::uint32_t head = tel.scopeHead.load (std::memory_order_acquire);
        std::array<float, (size_t) N> hh {}, grv {};
        for (int i = 0; i < N; ++i)
        {
            const std::uint32_t idx = (head - (std::uint32_t) (N - i)) & mask;
            const float lin = tel.scopeIn[idx].load (std::memory_order_relaxed);
            const float db  = 20.0f * std::log10 (std::max (lin, 1.0e-5f));
            hh[(size_t) i]  = juce::jlimit (0.0f, 1.0f, (db + 54.0f) / 54.0f);
            grv[(size_t) i] = tel.scopeGr[idx].load (std::memory_order_relaxed);
        }

        const float dx    = area.getWidth() / (float) (N - 1);
        const float midY  = area.getCentreY() + area.getHeight() * 0.14f;
        const float halfH = area.getHeight() * 0.34f;

        // dalga formu (simetrik zarf)
        {
            juce::Path wave;
            wave.startNewSubPath (area.getX(), midY - hh[0] * halfH);
            for (int i = 1; i < N; ++i)
                wave.lineTo (area.getX() + (float) i * dx, midY - hh[(size_t) i] * halfH);
            for (int i = N - 1; i >= 0; --i)
                wave.lineTo (area.getX() + (float) i * dx, midY + hh[(size_t) i] * halfH);
            wave.closeSubPath();

            g.setColour (juce::Colour (0xffb9bfb9).withAlpha (0.42f));
            g.fillPath (wave);
            g.setColour (juce::Colour (0xffe6eae6).withAlpha (0.30f));
            g.strokePath (wave, juce::PathStrokeType (0.8f));
        }

        // gain reduction egrisi (yukaridan asagi: 0 dB ust cizgi)
        const float grTop  = area.getY() + area.getHeight() * 0.10f;
        const float grSpan = area.getHeight() * 0.52f;
        {
            const float dash[] = { 3.0f, 4.0f };
            g.setColour (juce::Colours::white.withAlpha (0.14f));
            g.drawDashedLine (juce::Line<float> (area.getX(), grTop, area.getRight(), grTop), dash, 2, 1.0f);
        }

        juce::Path grPath;
        for (int i = 0; i < N; ++i)
        {
            const float px = area.getX() + (float) i * dx;
            const float py = grTop + grSpan * juce::jlimit (0.0f, 1.0f, grv[(size_t) i] / 30.0f);
            if (i == 0) grPath.startNewSubPath (px, py); else grPath.lineTo (px, py);
        }
        strokeGlow (g, grPath, kNeon, 2.0f);

        // uctaki nokta
        {
            const float px = area.getRight();
            const float py = grTop + grSpan * juce::jlimit (0.0f, 1.0f, grv[(size_t) (N - 1)] / 30.0f);
            g.setColour (kNeon.withAlpha (0.30f));
            g.fillEllipse (px - 8.0f, py - 8.0f, 16.0f, 16.0f);
            g.setColour (kNeon);
            g.fillEllipse (px - 4.0f, py - 4.0f, 8.0f, 8.0f);
        }

        // yazilar: GR ve makeup
        const float gr = tel.grDb.load (std::memory_order_relaxed);
        const float mk = tel.makeupDb.load (std::memory_order_relaxed);
        drawLabel (g, "GR  -" + juce::String (gr, 1) + " dB", { area.getX() + 4.0f, area.getBottom() - 18.0f, 140.0f, 16.0f },
                   11.0f, kNeon, true, juce::Justification::centredLeft);
        if (mk > 0.05f)
            drawLabel (g, "MAKEUP  +" + juce::String (mk, 1) + " dB", { area.getRight() - 164.0f, area.getBottom() - 18.0f, 160.0f, 16.0f },
                       11.0f, kTextDim, false, juce::Justification::centredRight);

        if (modeTag.isNotEmpty())
            drawLabel (g, modeTag, { area.getRight() - 124.0f, area.getY() + 1.0f, 120.0f, 12.0f },
                       9.5f, kTextDim, true, juce::Justification::centredRight);
        if (charTag.isNotEmpty())
            drawLabel (g, charTag, { area.getX() + 4.0f, area.getY() + 1.0f, 220.0f, 12.0f },
                       9.5f, kNeon.withAlpha (0.85f), true, juce::Justification::centredLeft);

        if (bypassed)
        {
            g.setColour (juce::Colours::black.withAlpha (0.55f));
            g.fillRoundedRectangle (r, 9.0f);
            drawLabel (g, "BYPASSED", r, 26.0f, juce::Colour (0xffffa62b), true);
        }
    }

private:
    scc::Telemetry& tel;
    bool bypassed = false;
    juce::String modeTag, charTag;
};

//==============================================================================
//  Kucuk knob (Analog Mode ve Character Designer): ana knob ile ayni cizim, boyuta
//  gore otomatik olceklenir. Iki kutuplu (-..+) knob'larda yay ortadan baslar;
//  Slider'in "bipolar" ozelligi ile secilir.
//==============================================================================
class SmallKnobLookAndFeel : public KnobLookAndFeel {};

//==============================================================================
//  Baslik + knob + deger. Bir plugin parametresine baglanir; slot degisince
//  baska bir parametreye yeniden baglanabilir (attach).
//==============================================================================
class LabeledKnob : public juce::Component
{
public:
    explicit LabeledKnob (const juce::String& titleText, bool isBipolar = false)
        : title (titleText)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                                    juce::MathConstants<float>::pi * 2.75f, true);
        slider.setMouseDragSensitivity (180);
        slider.getProperties().set ("bipolar", isBipolar);
        slider.setLookAndFeel (&lnf);
        slider.setRepaintsOnMouseActivity (true);       // fare ustundeyken cerceve parlar
        slider.onValueChange = [this] { repaint(); };
        installReadout();
        addAndMakeVisible (slider);
    }

    ~LabeledKnob() override
    {
        attachment.reset();
        slider.setLookAndFeel (nullptr);
    }

    void attach (juce::AudioProcessorValueTreeState& state, const juce::String& paramId)
    {
        attachment.reset();
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, paramId, slider);
        installReadout();                               // attachment kendi metin fonksiyonunu kurar; bizimkini geri koy
        repaint();
    }

    void setTitle (const juce::String& t)   { if (t != title) { title = t; repaint(); } }
    void setTitleSize (float s)             { titleSize = s; }
    void setValueSize (float s)             { valueSize = s; }

    // true (varsayilan): deger kubbenin ortasinda yazilir, altta satir ayrilmaz.
    // false: eski duzen, deger knob'un altinda.
    void setValueInside (bool inside)
    {
        if (inside != valueInside)
        {
            valueInside = inside;
            slider.getProperties().set ("centerReadout", valueInside);
            resized();
            repaint();
        }
    }

    // kontrol su an sesi etkilemiyorsa (ornegin CUSTOM secili degilken) soluk ve kilitli
    void setActive (bool shouldBeActive)
    {
        if (shouldBeActive != active)
        {
            active = shouldBeActive;
            slider.setEnabled (active);
            repaint();
        }
    }

    std::function<juce::String (double)> formatValue;

    void resized() override
    {
        auto r = getLocalBounds();
        r.removeFromTop (juce::roundToInt (titleSize + 5.0f));
        if (! valueInside)
            r.removeFromBottom (juce::roundToInt (valueSize + 5.0f));
        const int side = std::min (r.getWidth(), r.getHeight());
        slider.setBounds (r.withSizeKeepingCentre (side, side));
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        const auto top = r.removeFromTop (titleSize + 5.0f);
        drawLabel (g, title, top, titleSize, active ? kText : kTextDim.withAlpha (0.55f));

        if (! valueInside)
        {
            const auto bottom = r.removeFromBottom (valueSize + 5.0f);
            drawLabel (g, slider.getTextFromValue (slider.getValue()), bottom, valueSize,
                       active ? kNeon : kTextDim.withAlpha (0.55f));
        }
    }

    juce::Slider slider;

private:
    // Knob'un ortasindaki / altindaki yazi tek yerden gelir: formatValue (yoksa tam sayi)
    void installReadout()
    {
        slider.textFromValueFunction = [this] (double v)
        {
            return formatValue ? formatValue (v) : juce::String (juce::roundToInt (v));
        };
    }

    SmallKnobLookAndFeel lnf;
    juce::String title;
    float titleSize = 11.0f, valueSize = 12.5f;
    bool  valueInside = true;
    bool  active = true;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

//==============================================================================
//  Tek satir bilgi yazisi (tip aciklamasi, durum mesaji)
//==============================================================================
class InfoText : public juce::Component
{
public:
    explicit InfoText (float fontSize = 11.5f, juce::Justification j = juce::Justification::centredLeft)
        : size (fontSize), just (j)
    {
        setInterceptsMouseClicks (false, false);
    }

    void setText (const juce::String& t, juce::Colour c = kTextDim)
    {
        if (t != text || c != colour)
        {
            text = t;
            colour = c;
            repaint();
        }
    }

    const juce::String& getText() const noexcept { return text; }

    void paint (juce::Graphics& g) override
    {
        useFont (g, size, false);
        g.setColour (colour);
        g.drawFittedText (text, getLocalBounds(), just, 2, 0.85f);
    }

private:
    juce::String text;
    juce::Colour colour = kTextDim;
    float size;
    juce::Justification just;
};

} // namespace sccui

#endif // SCC_UI_COMPONENTS_H_INCLUDED
