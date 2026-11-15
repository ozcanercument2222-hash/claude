#ifndef SCC_KNOB_STYLE_H_INCLUDED
#define SCC_KNOB_STYLE_H_INCLUDED

#include <juce_gui_basics/juce_gui_basics.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include "SCCKnobFont.h"

//==============================================================================
//  SCC knob stili: fircalanmis celik cerceve, kara cam kubbe, parlayan beyaz yay,
//  cerceveyi asan kapsul isaretci ve kubbenin ortasinda deger okumasi.
//
//  Tum knob'lar (ana knob, Analog Mode, Character Designer) bu dosyadaki tek
//  cizim koduyla cizilir; boyuta gore otomatik olceklenir.
//
//  Hizli ayar noktalari:
//    kKnobLight   : yay / isaretci / okuma rengi. kNeon yaparsan eski yesil parlama.
//    Geo          : cerceve kalinligi, yay kalinligi, isaretci boyutu
//    makeBezelImage : celigin isik dagilimi ve fircalama dokusu
//
//  Performans: cerceve + cam + iz bandi (degerden bagimsiz her sey) bir kez, fiziksel
//  piksel cozunurlugunde bir Image'a cizilir ve onbellekte tutulur. Deger degisince
//  yalnizca yay, isaretci ve yazi yeniden cizilir.
//==============================================================================
namespace sccui
{

//  Knob rengi (sicak beyaz). Eski neon yesil icin:  inline const juce::Colour kKnobLight = kNeon;
inline const juce::Colour kKnobLight { 0xfff4f1ea };

namespace knobstyle
{

constexpr float kPi = juce::MathConstants<float>::pi;

//------------------------------------------------------------------------------
//  Kucuk matematik yardimcilari
//------------------------------------------------------------------------------
inline float saturate (float v) noexcept { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

inline float smoothstep (float a, float b, float x) noexcept
{
    const float t = saturate ((x - a) / (b - a));
    return t * t * (3.0f - 2.0f * t);
}

// Deterministik hash (rastgele sayi uretecinden bagimsiz: cerceve her derlemede ayni gorunur)
inline float hash01 (std::uint32_t n) noexcept
{
    n ^= n >> 16;  n *= 0x7feb352du;
    n ^= n >> 15;  n *= 0x846ca68bu;
    n ^= n >> 16;
    return (float) (n & 0xffffffu) / 16777215.0f;
}

// Duzgun 1 boyutlu deger gurultusu (0..1)
inline float valueNoise (float x) noexcept
{
    const float fl = std::floor (x);
    const auto  i  = (std::uint32_t) (std::int32_t) fl;
    const float t  = x - fl;
    const float s  = t * t * (3.0f - 2.0f * t);
    const float a  = hash01 (i);
    const float b  = hash01 (i + 1u);
    return a + (b - a) * s;
}

//------------------------------------------------------------------------------
//  Gomulu yazi tipi (Oxanium Medium). Bellekten yuklenir, tum knob'lar paylasir.
//------------------------------------------------------------------------------
struct FontHolder
{
    FontHolder() : typeface (juce::Typeface::createSystemTypefaceFor (kKnobFontData, kKnobFontSize)) {}
    juce::Typeface::Ptr typeface;
};

inline juce::Font makeFont (const juce::Typeface::Ptr& tf, float height)
{
    if (tf != nullptr)
        return juce::Font (juce::FontOptions (tf).withHeight (height));
    return juce::Font (juce::FontOptions (height));
}

inline float textWidth (const juce::Font& f, const juce::String& s)
{
    juce::GlyphArrangement ga;
    ga.addLineOfText (f, s, 0.0f, 0.0f);
    return ga.getBoundingBox (0, -1, true).getWidth();
}

//------------------------------------------------------------------------------
//  Geometri: her olcu knob yaricapindan (R) turetilir. Kucuk knob'larda yayin ve
//  isaretcinin okunur kalmasi icin mutlak alt sinirlar vardir.
//------------------------------------------------------------------------------
struct Geo
{
    // Oranlar, bir referans gorselden olculdu (dis halka %5, hemen icinde ince parlak
    // pah cizgisi, sonra koyu oluk; yay merkezi 0.79 rb, kalinligi 0.058 rb).
    explicit Geo (juce::Rectangle<float> b) noexcept
    {
        R  = std::min (b.getWidth(), b.getHeight()) * 0.5f;
        cx = b.getCentreX();
        cy = b.getCentreY();

        rb      = R * 0.925f;                               // celik cerceve dis yaricapi
        ringT   = std::max (rb * 0.060f, 2.6f);            // dis halkanin kalinligi
        lipT    = std::max (rb * 0.022f, 1.1f);            // ince parlak pah cizgisi
        gap     = std::max (rb * 0.008f, 0.6f);
        ringIn  = rb - ringT;
        lipOut  = ringIn - gap;
        lipIn   = lipOut - lipT;
        glassR  = lipOut;                                  // cam, pah cizgisinin altina kadar uzanir
        arcR    = rb * 0.790f;                             // yay orta cizgisi
        arcW    = std::min (std::max (rb * 0.064f, 3.0f), rb * 0.14f);
        capR    = rb * 0.985f;                             // isaretci merkezi (cerceve uzerinde)
        capLen  = std::max (rb * 0.135f, 7.0f);
        capW    = std::max (rb * 0.034f, 2.2f);
    }

    juce::Point<float> point (float radius, float angle) const noexcept
    {
        return { cx + radius * std::sin (angle), cy - radius * std::cos (angle) };
    }

    float R, cx, cy, rb, ringT, lipT, gap, ringIn, lipOut, lipIn, glassR, arcR, arcW, capR, capLen, capW;
};

//------------------------------------------------------------------------------
//  Fircalanmis celik cerceve: piksel piksel uretilir. Konik isik DORT capraz lobdan
//  olusur (45, 135, 225, 315 derece), geri kalani koyu celiktir; uzerine halka boyunca
//  uzayan fircalama cizgileri biner. Iki halka: dis cerceve + ince parlak pah cizgisi.
//------------------------------------------------------------------------------
inline juce::Image makeBezelImage (int side, float R, const Geo& geo)
{
    juce::Image img (juce::Image::ARGB, side, side, true);
    juce::Image::BitmapData bd (img, juce::Image::BitmapData::writeOnly);

    const float scale   = (float) side / (2.0f * R);        // mantiksal birim -> fiziksel piksel
    const float aa      = 1.0f / scale;
    const float rb      = geo.rb;
    const float rRingIn = geo.ringIn;
    const float rLipOut = geo.lipOut;
    const float rLipIn  = geo.lipIn;
    const float rSplit  = 0.5f * (rLipOut + rRingIn);

    auto coverage = [scale] (float r, float inner, float outer)
    {
        return saturate ((outer - r) * scale + 0.5f) * saturate ((r - inner) * scale + 0.5f);
    };

    for (int y = 0; y < side; ++y)
    {
        for (int x = 0; x < side; ++x)
        {
            const float dx = ((float) x + 0.5f) / scale - R;
            const float dy = ((float) y + 0.5f) / scale - R;
            const float r  = std::sqrt (dx * dx + dy * dy);

            if (r > rb + aa || r < rLipIn - aa)
                continue;

            const float th = std::atan2 (dx, -dy);           // 12'den saat yonunde

            // konik isik: dort capraz lob; sag ust en parlak, sol alt daha sonuk
            const float lobe  = std::pow (std::max (0.0f, std::cos (4.0f * (th - 0.80f))), 2.2f);
            const float lobeB = std::pow (std::max (0.0f, std::cos (4.0f * (th - 0.80f - 0.39f))), 2.0f);
            const float amp   = 0.62f + 0.25f * std::cos (th - 0.85f);
            const float glint = 0.5f + 0.5f * std::cos (th + 0.95f);      // sol ustte genis hafif parilti

            // firca dokusu: halka boyunca uzun cizgiler x yaricap boyunca ince tane
            const float rp     = r * scale;
            const float streak = valueNoise (th * 11.0f + rp * 0.012f + 3.0f);
            const float grain  = valueNoise (rp * 1.3f + 31.0f);
            const float tex    = (0.72f + 0.56f * streak) * (0.80f + 0.40f * grain);

            float L, alpha;

            if (r >= rSplit)                                  // dis cerceve
            {
                alpha = coverage (r, rRingIn, rb);
                const float u     = saturate ((rb - r) / (rb - rRingIn));     // 0 = dis kenar
                const float body  = 0.07f + lobe * amp + 0.10f * glint * glint;
                const float rim   = 1.0f - smoothstep (0.0f, 0.16f, u);       // jilet gibi dis kenar
                const float shade = smoothstep (0.70f, 1.0f, u);              // ic kenar golgesi
                L = body * tex * (0.85f + 0.25f * (1.0f - u))
                    + rim * (0.10f + 0.45f * lobe * amp + 0.25f * glint) - shade * 0.06f;
            }
            else                                              // ince parlak pah cizgisi
            {
                alpha = coverage (r, rLipIn, rLipOut);
                L = (0.20f + 0.50f * (0.30f + 0.70f * lobeB) + 0.12f * glint) * (0.78f + 0.44f * streak);
            }

            L = saturate (L);

            auto* px = reinterpret_cast<juce::PixelARGB*> (bd.getPixelPointer (x, y));
            px->setARGB ((std::uint8_t) juce::roundToInt (alpha * 255.0f),
                         (std::uint8_t) juce::roundToInt (saturate (L * 0.97f) * 255.0f),
                         (std::uint8_t) juce::roundToInt (saturate (L * 0.99f) * 255.0f),
                         (std::uint8_t) juce::roundToInt (saturate (L * 1.03f) * 255.0f));
            px->premultiply();
        }
    }
    return img;
}

//------------------------------------------------------------------------------
//  Degerden bagimsiz katman: golge, cam kubbe, yansima, cerceve, iz bandi.
//  Koordinat sistemi: merkez (R, R), birim = mantiksal piksel.
//------------------------------------------------------------------------------
inline void buildStaticLayer (juce::Image& img, int side, float R,
                              float startAngle, float endAngle, bool bipolar)
{
    img.clear (img.getBounds());
    const Geo geo ({ 0.0f, 0.0f, 2.0f * R, 2.0f * R });
    const float scale = (float) side / (2.0f * R);
    const float cx = geo.cx, cy = geo.cy, rb = geo.rb, gR = geo.glassR;

    {
        juce::Graphics g (img);
        g.addTransform (juce::AffineTransform::scale (scale));

        // 1) yumusak temas golgesi (cerceve disinda hafif koyulasma)
        {
            juce::ColourGradient sh (juce::Colours::black.withAlpha (0.60f), cx, cy + 0.03f * R,
                                     juce::Colours::black.withAlpha (0.0f),  cx, cy + 0.03f * R + R, true);
            sh.addColour ((double) (rb * 0.97f / R), juce::Colours::black.withAlpha (0.50f));
            g.setGradientFill (sh);
            g.fillEllipse (cx - R, cy - R, 2.0f * R, 2.0f * R);
        }

        // 2) taban disk (oluk bu renkte gorunur)
        g.setColour (juce::Colour (0xff050506));
        g.fillEllipse (cx - rb, cy - rb, 2.0f * rb, 2.0f * rb);

        // 3) cam kubbe: sol ustten aydinlanan, kenarlara dogru kararan
        {
            const float hx = cx - 0.25f * gR, hy = cy - 0.35f * gR;
            juce::ColourGradient glass (juce::Colour (0xff161719), hx, hy,
                                        juce::Colour (0xff040405), hx + 1.30f * gR, hy, true);
            glass.addColour (0.50, juce::Colour (0xff09090a));
            g.setGradientFill (glass);
            g.fillEllipse (cx - gR, cy - gR, 2.0f * gR, 2.0f * gR);

            juce::ColourGradient vig (juce::Colours::black.withAlpha (0.0f), cx, cy,
                                      juce::Colours::black.withAlpha (0.55f), cx + gR, cy, true);
            vig.addColour (0.78, juce::Colours::black.withAlpha (0.0f));
            g.setGradientFill (vig);
            g.fillEllipse (cx - gR, cy - gR, 2.0f * gR, 2.0f * gR);
        }

        // 4) cam yansimasi: sol ustte capraz kesik bir aydinlik
        {
            juce::Path clip;
            clip.addEllipse (cx - gR, cy - gR, 2.0f * gR, 2.0f * gR);

            juce::Path shard;
            const auto A = juce::Point<float> (cx + gR * -0.22f, cy + gR * -1.26f);
            const auto B = juce::Point<float> (cx + gR *  1.11f, cy + gR *  0.17f);
            shard.startNewSubPath (A);
            shard.lineTo (B);
            shard.lineTo (cx - 1.4f * gR, B.y);
            shard.lineTo (cx - 1.4f * gR, A.y);
            shard.closeSubPath();

            g.saveState();
            g.reduceClipRegion (clip);
            juce::ColourGradient sheen (juce::Colours::white.withAlpha (0.070f), cx, cy - gR,
                                        juce::Colours::white.withAlpha (0.0f),   cx, cy + 0.17f * gR, false);
            g.setGradientFill (sheen);
            g.fillPath (shard);
            g.restoreState();
        }
    }

    // 4b) cok koyu gradyanlar 8 bitte bant olusturur; +-1 seviyelik gurultu bantlari gizler
    {
        juce::Image::BitmapData bd (img, juce::Image::BitmapData::readWrite);
        const float inv = 1.0f / scale;
        for (int y = 0; y < side; ++y)
        {
            for (int x = 0; x < side; ++x)
            {
                const float dx = ((float) x + 0.5f) * inv - cx;
                const float dy = ((float) y + 0.5f) * inv - cy;
                if (dx * dx + dy * dy > (gR - inv) * (gR - inv))
                    continue;

                auto* px = reinterpret_cast<juce::PixelARGB*> (bd.getPixelPointer (x, y));
                if (px->getAlpha() != 255)
                    continue;

                const auto ux = (std::uint32_t) x, uy = (std::uint32_t) y;
                const float n = hash01 (ux * 73856093u ^ uy * 19349663u)
                              + hash01 (ux * 83492791u + uy * 2654435761u) - 1.0f;      // ucgen dagilim -1..1
                const int d = juce::roundToInt (n * 1.4f);
                auto clampByte = [] (int v) { return (std::uint8_t) juce::jlimit (0, 255, v); };
                px->setARGB (255, clampByte (px->getRed() + d), clampByte (px->getGreen() + d), clampByte (px->getBlue() + d));
            }
        }
    }

    // 5) celik cerceve (piksel piksel uretilmis, 1:1 yerlestirilir)
    {
        const auto bezel = makeBezelImage (side, R, geo);
        juce::Graphics g (img);
        g.drawImageAt (bezel, 0, 0);
    }

    // 6) iz bandi + iki kutuplu knob'larda orta isaret
    {
        juce::Graphics g (img);
        g.addTransform (juce::AffineTransform::scale (scale));

        juce::Path track;
        track.addCentredArc (cx, cy, geo.arcR, geo.arcR, 0.0f, startAngle, endAngle, true);

        g.setColour (juce::Colours::black.withAlpha (0.38f));                       // hafif cukur
        g.strokePath (track, juce::PathStrokeType (geo.arcW + 2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
        g.setColour (kKnobLight.withAlpha (0.090f));
        g.strokePath (track, juce::PathStrokeType (geo.arcW, juce::PathStrokeType::curved, juce::PathStrokeType::butt));

        if (bipolar)
        {
            const float mid = 0.5f * (startAngle + endAngle);
            const auto p1 = geo.point (geo.arcR - geo.arcW * 0.95f, mid);
            const auto p2 = geo.point (geo.arcR + geo.arcW * 0.95f, mid);
            g.setColour (kKnobLight.withAlpha (0.55f));
            g.drawLine (p1.x, p1.y, p2.x, p2.y, std::max (1.0f, R * 0.012f));
        }
    }
}

//------------------------------------------------------------------------------
//  Degere bagli parcalar
//------------------------------------------------------------------------------
struct Params
{
    float pos        = 0.0f;                  // 0..1 (egri uygulanmis konum)
    float startAngle = 0.0f, endAngle = 0.0f; // JUCE: 12'den saat yonunde, radyan
    bool  bipolar    = false;                 // true: yay ortadan baslar
    bool  enabled    = true;
    bool  hot        = false;                 // fare ustunde / surukleniyor
    juce::String readout;                     // kubbenin ortasindaki yazi ("75%", "-24.0 dB")
};

inline void drawCapsule (juce::Graphics& g, const Geo& geo, float angle, float grow)
{
    juce::Path cap;
    const float len = geo.capLen + grow * 1.2f;         // boyuna az, enine cok buyur: dar, kapsulu izleyen parilti
    const float wid = geo.capW + grow * 2.0f;
    cap.addRoundedRectangle (geo.cx - wid * 0.5f, geo.cy - geo.capR - len * 0.5f, wid, len, wid * 0.5f);
    g.fillPath (cap, juce::AffineTransform::rotation (angle, geo.cx, geo.cy));
}

inline void drawReadout (juce::Graphics& g, const juce::Typeface::Ptr& tf, const Geo& geo,
                         const juce::String& text, juce::Colour colour, bool glow)
{
    if (text.isEmpty())
        return;

    // "75%"  ->  "75" + "%";   "-24.0 dB"  ->  "-24.0" + "dB"
    int cut = -1;
    for (int i = 0; i < text.length(); ++i)
        if (text[i] == ' ' || text[i] == '%') { cut = i; break; }

    juce::String number = cut < 0 ? text : text.substring (0, cut);
    const juce::String unit = cut < 0 ? juce::String() : text.substring (cut).trim();
    if (number.startsWithChar ('-'))
        number = juce::String::charToString ((juce::juce_wchar) 0x2212) + number.substring (1);   // gercek eksi

    // sigdirma: halkanin icinde, kenarlardan pay birakarak
    const float innerR = geo.arcR - geo.arcW * 0.5f - geo.rb * 0.06f;
    const float maxW   = 2.0f * innerR * 0.82f;

    float H = geo.rb * 0.42f;
    constexpr float unitScale = 0.70f;

    auto measure = [&] (float h, juce::Font& fn, juce::Font& fu)
    {
        fn = makeFont (tf, h);
        fu = makeFont (tf, h * unitScale);
        const float wn = textWidth (fn, number);
        const float wu = unit.isEmpty() ? 0.0f : textWidth (fu, unit);
        return std::pair<float, float> (wn, wu);
    };

    const float gapScale = unit == "%" ? 0.09f : 0.20f;       // harf birimleri ("ms", "dB") sayidan belirgin ayrilir

    juce::Font fn (juce::FontOptions (10.0f)), fu (juce::FontOptions (10.0f));
    auto w = measure (H, fn, fu);
    float total = w.first + (unit.isEmpty() ? 0.0f : H * gapScale + w.second);
    if (total > maxW)
    {
        H = std::max (7.5f, H * maxW / total);
        w = measure (H, fn, fu);
        total = w.first + (unit.isEmpty() ? 0.0f : H * gapScale + w.second);
    }

    const float digitH   = H * 0.69f;                            // Oxanium rakam yuksekligi
    const float baseline = geo.cy + digitH * 0.5f - geo.R * 0.012f;
    const float x0       = geo.cx - total * 0.5f;
    const float xu       = x0 + w.first + H * gapScale;

    juce::GlyphArrangement gaNum, gaUnit;
    gaNum.addLineOfText (fn, number, x0, baseline);
    if (unit.isNotEmpty())
        gaUnit.addLineOfText (fu, unit, xu, baseline);

    if (glow)                                                    // yumusak isik halesi
    {
        const float d = std::max (1.0f, geo.R * 0.010f);
        const float off[8][2] = { { -d, 0 }, { d, 0 }, { 0, -d }, { 0, d }, { -d, -d }, { d, -d }, { -d, d }, { d, d } };
        g.setColour (colour.withAlpha (0.115f));
        for (const auto& o : off)
        {
            gaNum.draw (g, juce::AffineTransform::translation (o[0], o[1]));
            gaUnit.draw (g, juce::AffineTransform::translation (o[0], o[1]));
        }
    }

    g.setColour (colour);
    gaNum.draw (g);
    g.setColour (colour.withMultipliedAlpha (0.90f));
    gaUnit.draw (g);

    // yazinin altinda kenarlari sonen ince cizgi (yalnizca buyuk knob'larda)
    if (glow && geo.R >= 70.0f)
    {
        const float y  = geo.cy + geo.R * 0.245f;
        const float hw = geo.R * 0.26f;
        juce::ColourGradient line (colour.withAlpha (0.0f), geo.cx - hw, y, colour.withAlpha (0.0f), geo.cx + hw, y, false);
        line.addColour (0.5, colour.withAlpha (0.60f));
        g.setGradientFill (line);
        g.fillRect (geo.cx - hw, y - 0.5f, 2.0f * hw, 1.0f);
    }
}

//==============================================================================
//  Cizici: her LookAndFeel kendi ornegini tasir (onbellek knob boyutuna ozeldir).
//==============================================================================
class KnobRenderer
{
public:
    void draw (juce::Graphics& g, juce::Rectangle<float> bounds, const Params& p)
    {
        const Geo geo (bounds);
        if (geo.R < 6.0f)
            return;

        // ---- degerden bagimsiz katman (fiziksel cozunurlukte onbellek)
        const float scale = std::max (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
        const int   side  = std::max (8, juce::roundToInt (2.0f * geo.R * scale));
        auto differs = [] (float a, float b) { return std::abs (a - b) > 1.0e-4f; };
        if (! cache.isValid() || cacheSide != side || differs (cacheR, geo.R)
            || differs (cacheStart, p.startAngle) || differs (cacheEnd, p.endAngle) || cacheBipolar != p.bipolar)
        {
            cache = juce::Image (juce::Image::ARGB, side, side, true);
            buildStaticLayer (cache, side, geo.R, p.startAngle, p.endAngle, p.bipolar);
            cacheSide = side;  cacheR = geo.R;
            cacheStart = p.startAngle;  cacheEnd = p.endAngle;  cacheBipolar = p.bipolar;
        }
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (cache, { geo.cx - geo.R, geo.cy - geo.R, 2.0f * geo.R, 2.0f * geo.R });

        // ---- kilitli: cerceve ve cam soluklasir; yay ve yazi gri kalir (okunur ama "kapali" gorunur)
        if (! p.enabled)
        {
            g.setColour (juce::Colours::black.withAlpha (0.34f));
            g.fillEllipse (geo.cx - geo.rb, geo.cy - geo.rb, 2.0f * geo.rb, 2.0f * geo.rb);
        }

        // ---- renkler
        const juce::Colour light = p.enabled ? (p.hot ? kKnobLight.brighter (0.05f) : kKnobLight)
                                             : juce::Colour (0xff8d9296).withAlpha (0.70f);
        const float glowGain = p.hot ? 1.3f : 1.0f;

        const float angle = p.startAngle + p.pos * (p.endAngle - p.startAngle);
        const float from  = p.bipolar ? 0.5f * (p.startAngle + p.endAngle) : p.startAngle;

        // ---- deger yayi: duz uclu, disa dogru parlayan
        if (std::abs (angle - from) > 0.004f)
        {
            juce::Path arc;
            arc.addCentredArc (geo.cx, geo.cy, geo.arcR, geo.arcR, 0.0f,
                               std::min (from, angle), std::max (from, angle), true);

            if (p.enabled)
            {
                // sert uclarin radyal kesik birakmamasi icin yuvarlak uclu, ic ice 8 genis cizgi
                for (int k = 7; k >= 0; --k)
                {
                    g.setColour (light.withAlpha (saturate (0.028f * glowGain)));
                    g.strokePath (arc, juce::PathStrokeType (geo.arcW * (1.45f + 0.70f * (float) k),
                                                             juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                }
            }
            g.setColour (light);
            g.strokePath (arc, juce::PathStrokeType (geo.arcW, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
        }

        // ---- cerceveyi asan kapsul isaretci
        {
            if (p.enabled)
            {
                const float growth[] = { geo.capW * 1.9f, geo.capW * 1.2f, geo.capW * 0.6f };
                const float alpha[]  = { 0.045f, 0.080f, 0.160f };
                for (int i = 0; i < 3; ++i)
                {
                    g.setColour (light.withAlpha (saturate (alpha[i] * glowGain)));
                    drawCapsule (g, geo, angle, growth[i]);
                }
            }
            g.setColour (light);
            drawCapsule (g, geo, angle, 0.0f);
        }

        // ---- merkez okuma
        drawReadout (g, fonts->typeface, geo, p.readout, light, p.enabled && geo.R >= 40.0f);

        // ---- fare ustunde: cerceve kenarinda ince parlama
        if (p.hot && p.enabled)
        {
            g.setColour (juce::Colours::white.withAlpha (0.10f));
            g.drawEllipse (geo.cx - geo.rb + 0.5f, geo.cy - geo.rb + 0.5f, 2.0f * geo.rb - 1.0f, 2.0f * geo.rb - 1.0f, 1.0f);
        }

    }

private:
    juce::SharedResourcePointer<FontHolder> fonts;
    juce::Image cache;
    int   cacheSide = 0;
    float cacheR = 0.0f, cacheStart = 0.0f, cacheEnd = 0.0f;
    bool  cacheBipolar = false;
};

} // namespace knobstyle
} // namespace sccui

#endif // SCC_KNOB_STYLE_H_INCLUDED
