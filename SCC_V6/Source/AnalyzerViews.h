#ifndef SCC_ANALYZER_VIEWS_H_INCLUDED
#define SCC_ANALYZER_VIEWS_H_INCLUDED

#include <array>
#include <memory>
#include <vector>
#include "UIComponents.h"
#include "SCCCharacterAnalysis.h"
#include "SCCSpectrumAnalyzer.h"

//==============================================================================
//  Character Designer'in gorsellestirmeleri. Ucu de GERCEK veriye dayanir:
//
//    SpectrumView      : ses motorunun karakter katina GIREN ve CIKAN gercek
//                        sinyalinin FFT'si (FFT burada, arayuz is parcaciginda).
//    HarmonicBarsView  : ses yolundakiyle ayni CharacterEngine sinifinin bir test
//                        sinusune verdigi cevabin olculen harmonikleri.
//    CurveView         : ses islemenin kullandigi statik egri fonksiyonunun kendisi.
//==============================================================================
namespace sccui
{

//==============================================================================
//  Gercek zamanli spektrum: gri = karakter oncesi, yesil = karakter sonrasi,
//  aradaki dolgu = karakterin EKLEDIGI icerik (harmonikler)
//==============================================================================
class SpectrumView : public juce::Component
{
public:
    SpectrumView (const scc::AnalyzerFifo& preFifo, const scc::AnalyzerFifo& postFifo)
        : pre (preFifo), post (postFifo),
          analyzer (std::make_unique<scc::SpectrumAnalyzer>()),
          mags ((size_t) scc::SpectrumAnalyzer::kNumBins, -140.0f)
    {
        setInterceptsMouseClicks (false, false);
        inDb.fill (kMinDb);
        outDb.fill (kMinDb);
    }

    void setSampleRate (double newRate) { sampleRate = juce::jmax (8000.0, newRate); }

    // zamanlayicidan (arayuz is parcacigi): FFT + yumusatma
    void update()
    {
        analyzer->compute (pre, mags.data());
        accumulate (inDb);
        analyzer->compute (post, mags.data());
        accumulate (outDb);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff070807));
        g.fillRoundedRectangle (r, 9.0f);
        g.setColour (kEdge);
        g.drawRoundedRectangle (r.reduced (0.5f), 9.0f, 1.3f);

        const auto area = r.reduced (10.0f, 10.0f).withTrimmedBottom (14.0f).withTrimmedLeft (26.0f);

        // izgara: frekans
        const float freqs[] = { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f };
        const char* labels[] = { "50", "100", "200", "500", "1k", "2k", "5k", "10k" };
        for (int i = 0; i < 8; ++i)
        {
            const float x = area.getX() + area.getWidth() * xFor (freqs[i]);
            g.setColour (juce::Colours::white.withAlpha (0.05f));
            g.fillRect (x, area.getY(), 1.0f, area.getHeight());
            drawLabel (g, labels[i], { x - 18.0f, area.getBottom() + 2.0f, 36.0f, 12.0f }, 9.5f, kTextDim);
        }
        // izgara: seviye
        for (int db = -24; db > (int) kMinDb; db -= 24)
        {
            const float y = area.getY() + area.getHeight() * yFor ((float) db);
            g.setColour (juce::Colours::white.withAlpha (0.05f));
            g.fillRect (area.getX(), y, area.getWidth(), 1.0f);
            drawLabel (g, juce::String (db), { r.getX() + 4.0f, y - 6.0f, 28.0f, 12.0f }, 9.5f, kTextDim, false, juce::Justification::centredRight);
        }

        g.saveState();
        g.reduceClipRegion (area.toNearestInt());

        const float dx = area.getWidth() / (float) (kCols - 1);
        auto yAt = [&area, this] (float db) { return area.getY() + area.getHeight() * yFor (db); };

        // karakterin ekledigi icerik: cikis girisin ustundeyse arasini doldur
        {
            juce::Path added;
            added.startNewSubPath (area.getX(), yAt (std::max (inDb[0], outDb[0])));
            for (int i = 1; i < kCols; ++i)
                added.lineTo (area.getX() + (float) i * dx, yAt (std::max (inDb[(size_t) i], outDb[(size_t) i])));
            for (int i = kCols - 1; i >= 0; --i)
                added.lineTo (area.getX() + (float) i * dx, yAt (inDb[(size_t) i]));
            added.closeSubPath();
            g.setColour (kNeon.withAlpha (0.20f));
            g.fillPath (added);
        }

        // giris (karakter oncesi)
        {
            juce::Path in;
            in.startNewSubPath (area.getX(), area.getBottom());
            for (int i = 0; i < kCols; ++i)
                in.lineTo (area.getX() + (float) i * dx, yAt (inDb[(size_t) i]));
            in.lineTo (area.getRight(), area.getBottom());
            in.closeSubPath();
            g.setColour (juce::Colour (0xffb9bfb9).withAlpha (0.22f));
            g.fillPath (in);
        }

        // cikis (karakter sonrasi)
        {
            juce::Path out;
            for (int i = 0; i < kCols; ++i)
            {
                const float px = area.getX() + (float) i * dx, py = yAt (outDb[(size_t) i]);
                if (i == 0) out.startNewSubPath (px, py); else out.lineTo (px, py);
            }
            strokeGlow (g, out, kNeon, 1.5f);
        }
        g.restoreState();

        // aciklama
        const float lx = area.getRight() - 150.0f, ly = area.getY() + 4.0f;
        g.setColour (juce::Colour (0xffb9bfb9).withAlpha (0.5f));
        g.fillRect (lx, ly + 4.0f, 12.0f, 4.0f);
        drawLabel (g, "IN", { lx + 16.0f, ly, 24.0f, 12.0f }, 9.5f, kTextDim, false, juce::Justification::centredLeft);
        g.setColour (kNeon);
        g.fillRect (lx + 44.0f, ly + 5.0f, 12.0f, 2.0f);
        drawLabel (g, "OUT", { lx + 60.0f, ly, 28.0f, 12.0f }, 9.5f, kTextDim, false, juce::Justification::centredLeft);
        g.setColour (kNeon.withAlpha (0.30f));
        g.fillRect (lx + 92.0f, ly + 3.0f, 12.0f, 6.0f);
        drawLabel (g, "ADDED", { lx + 108.0f, ly, 44.0f, 12.0f }, 9.5f, kTextDim, false, juce::Justification::centredLeft);
    }

private:
    static constexpr int   kCols  = 200;
    static constexpr float kMinDb = -96.0f;
    static constexpr float kMinHz = 20.0f, kMaxHz = 20000.0f;

    static float xFor (float hz)   { return std::log (hz / kMinHz) / std::log (kMaxHz / kMinHz); }
    static float yFor (float db)   { return juce::jlimit (0.0f, 1.0f, db / kMinDb); }
    static float hzFor (float x01) { return kMinHz * std::pow (kMaxHz / kMinHz, x01); }

    // FFT kutularini logaritmik kolonlara indir; hizli yuksel, yavas dus
    void accumulate (std::array<float, kCols>& dest)
    {
        const float binsPerHz = (float) scc::SpectrumAnalyzer::kFftSize / (float) sampleRate;
        const int   lastBin   = scc::SpectrumAnalyzer::kNumBins - 1;

        for (int c = 0; c < kCols; ++c)
        {
            const float lo = hzFor (((float) c - 0.5f) / (float) (kCols - 1)) * binsPerHz;
            const float hi = hzFor (((float) c + 0.5f) / (float) (kCols - 1)) * binsPerHz;

            float v;
            if (hi - lo < 1.0f)
            {
                // kolon bir kutudan dar: komsu iki kutu arasinda enterpolasyon
                const float centre = juce::jlimit (0.0f, (float) lastBin - 0.001f, 0.5f * (lo + hi));
                const int   k = (int) centre;
                const float t = centre - (float) k;
                v = mags[(size_t) k] + t * (mags[(size_t) (k + 1)] - mags[(size_t) k]);
            }
            else
            {
                v = -200.0f;
                const int k0 = juce::jlimit (0, lastBin, (int) std::ceil (lo));
                const int k1 = juce::jlimit (0, lastBin, (int) std::floor (hi));
                for (int k = k0; k <= std::max (k0, k1); ++k)
                    v = std::max (v, mags[(size_t) k]);
            }

            v = std::max (v, kMinDb);
            float& d = dest[(size_t) c];
            d = (v > d) ? d + 0.7f * (v - d) : std::max (v, d - 1.6f);      // ~48 dB/sn dusus
        }
    }

    const scc::AnalyzerFifo& pre;
    const scc::AnalyzerFifo& post;
    std::unique_ptr<scc::SpectrumAnalyzer> analyzer;
    std::vector<float> mags;
    std::array<float, kCols> inDb {}, outDb {};
    double sampleRate = 48000.0;
};

//==============================================================================
//  Harmonik imza: H1 (temel) .. H8. Cift harmonikler yesil, tek harmonikler turuncu.
//==============================================================================
class HarmonicBarsView : public juce::Component
{
public:
    HarmonicBarsView() { setInterceptsMouseClicks (false, false); }

    void setSignature (const scc::HarmonicSignature& s, double levelDbFs, bool usingLiveLevel)
    {
        sig = s;
        levelDb = levelDbFs;
        live = usingLiveLevel;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff070807));
        g.fillRoundedRectangle (r, 9.0f);
        g.setColour (kEdge);
        g.drawRoundedRectangle (r.reduced (0.5f), 9.0f, 1.3f);

        drawLabel (g, "HARMONICS", { r.getX() + 10.0f, r.getY() + 6.0f, 90.0f, 12.0f }, 10.0f, kTextDim, true, juce::Justification::centredLeft);
        drawLabel (g, (live ? "LIVE " : "REF ") + juce::String (levelDb, 0) + " dB",
                   { r.getRight() - 84.0f, r.getY() + 6.0f, 76.0f, 12.0f }, 9.5f, live ? kNeon : kTextDim, false, juce::Justification::centredRight);

        const auto area = r.reduced (10.0f, 0.0f).withTrimmedTop (24.0f).withTrimmedBottom (18.0f);
        constexpr float minDb = -80.0f;

        for (int db = -20; db > (int) minDb; db -= 20)
        {
            const float y = area.getY() + area.getHeight() * ((float) db / minDb);
            g.setColour (juce::Colours::white.withAlpha (0.05f));
            g.fillRect (area.getX(), y, area.getWidth(), 1.0f);
        }

        const int n = scc::HarmonicSignature::kNumHarmonics;
        const float slot = area.getWidth() / (float) n;
        for (int h = 0; h < n; ++h)
        {
            const float db   = (float) juce::jlimit ((double) minDb, 0.0, sig.levelDb[h]);
            const float top  = area.getY() + area.getHeight() * (db / minDb);
            const float x    = area.getX() + slot * (float) h + slot * 0.18f;
            const bool  even = ((h + 1) % 2) == 0;
            const juce::Colour c = (h == 0) ? kTextDim : (even ? kNeon : kAmber);

            g.setColour (c.withAlpha (0.10f));
            g.fillRect (x, area.getY(), slot * 0.64f, area.getHeight());
            if (area.getBottom() - top > 0.5f)
            {
                g.setColour (c.withAlpha (h == 0 ? 0.55f : 0.95f));
                g.fillRect (x, top, slot * 0.64f, area.getBottom() - top);
            }
            drawLabel (g, h == 0 ? "F" : juce::String (h + 1), { x - 4.0f, area.getBottom() + 3.0f, slot * 0.64f + 8.0f, 12.0f }, 10.0f, c);
        }
    }

private:
    scc::HarmonicSignature sig;
    double levelDb = -12.0;
    bool   live = false;
};

//==============================================================================
//  Doygunluk egrisi: giris -> cikis. Egri, ses islemenin kullandigi fonksiyonla
//  cizilir (scc::character::displayCurve). Nokta: sinyalin o anki tepe seviyesi.
//==============================================================================
class CurveView : public juce::Component
{
public:
    CurveView() { setInterceptsMouseClicks (false, false); }

    void setVoicing (const scc::Voicing& v, double signalLevel, bool engineOn)
    {
        voicing = v;
        level   = juce::jlimit (0.0, 1.0, signalLevel);
        on      = engineOn;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff070807));
        g.fillRoundedRectangle (r, 9.0f);
        g.setColour (kEdge);
        g.drawRoundedRectangle (r.reduced (0.5f), 9.0f, 1.3f);

        drawLabel (g, "SATURATION CURVE", { r.getX() + 10.0f, r.getY() + 6.0f, 130.0f, 12.0f }, 10.0f, kTextDim, true, juce::Justification::centredLeft);

        const auto area = r.reduced (12.0f, 0.0f).withTrimmedTop (24.0f).withTrimmedBottom (10.0f);
        const float cx = area.getCentreX(), cy = area.getCentreY();
        const float sx = area.getWidth() * 0.5f, sy = area.getHeight() * 0.5f;

        // eksenler ve dogrusal referans (y = x)
        g.setColour (juce::Colours::white.withAlpha (0.07f));
        g.fillRect (area.getX(), cy, area.getWidth(), 1.0f);
        g.fillRect (cx, area.getY(), 1.0f, area.getHeight());
        {
            const float dash[] = { 3.0f, 4.0f };
            g.setColour (juce::Colours::white.withAlpha (0.16f));
            g.drawDashedLine (juce::Line<float> (area.getX(), area.getBottom(), area.getRight(), area.getY()), dash, 2, 1.0f);
        }

        g.saveState();
        g.reduceClipRegion (area.toNearestInt());

        // egri
        constexpr int N = 120;
        juce::Path curve;
        const double lv = std::max (level, 0.02);
        for (int i = 0; i <= N; ++i)
        {
            const double x = -1.0 + 2.0 * (double) i / (double) N;
            const double y = on ? scc::character::displayCurve (voicing, x, lv) : x;
            const float px = cx + (float) x * sx, py = cy - (float) juce::jlimit (-1.2, 1.2, y) * sy;
            if (i == 0) curve.startNewSubPath (px, py); else curve.lineTo (px, py);
        }
        strokeGlow (g, curve, on ? kNeon : kTextDim, 1.8f);

        // sinyalin su anki tepe seviyesi
        if (level > 0.003)
        {
            for (const double sgn : { -1.0, 1.0 })
            {
                const double x = sgn * level;
                const double y = on ? scc::character::displayCurve (voicing, x, lv) : x;
                const float px = cx + (float) x * sx, py = cy - (float) juce::jlimit (-1.2, 1.2, y) * sy;
                g.setColour (kAmber.withAlpha (0.25f));
                g.fillRect (px - 0.5f, area.getY(), 1.0f, area.getHeight());
                g.setColour (kAmber);
                g.fillEllipse (px - 3.5f, py - 3.5f, 7.0f, 7.0f);
            }
        }
        g.restoreState();
    }

private:
    scc::Voicing voicing;
    double level = 0.0;
    bool   on = false;
};

} // namespace sccui

#endif // SCC_ANALYZER_VIEWS_H_INCLUDED
