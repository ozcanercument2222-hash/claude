#pragma once
//==============================================================================
//  Perde ekrani (ust panel)
//
//  "Tarama" (sweep) gorunumu: dikey cizgi yazma kafasidir ve soldan saga ilerler,
//  arkasinda yeni egriyi birakir; onundeki egri bir onceki turdan kalir. Ekranda
//  6 saniye vardir. Mor egri = duzeltilmis cikis perdesi, noktalar = nota gecisleri
//  (yeni notanin basladigi ve oturdugu anlar). Kafadaki etiket o an calinan notayi ve
//  girisin o notadan sapmasini (cent) gosterir. Istege bagli soluk cizgi = ham giris.
//
//  Veri, ses is parcaciginden kilitsiz kuyrukla gelir; ses yokken ekran durur ve
//  yeniden cizim yapilmaz.
//==============================================================================
#include "Theme.h"
#include "Lang.h"
#include "dsp/TuneEngine.h"
#include "dsp/Scales.h"

namespace vui
{
class PitchDisplay : public juce::Component, private juce::Timer
{
public:
    explicit PitchDisplay (vstm::TuneEngine& e) : engine (e)
    {
        setOpaque (true);
        resetColumns();
        startTimerHz (60);
        setTitle (vstm::i18n::tr ("Pitch display"));
    }

    bool showInput = false;
    void setActive (bool a) { if (a != active) { active = a; repaint(); } }

    /** Pull everything queued so far (used by the screenshot test without a running timer). */
    void pump() { timerCallback(); }

    void resized() override { resetColumns(); }

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        const float corner = 20.0f;
        g.fillAll (col::bg);

        juce::Path frame;
        frame.addRoundedRectangle (b.reduced (0.5f), corner);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff111015), 0.0f, 0.0f, juce::Colour (0xff0c0c10), 0.0f, b.getBottom(), false));
        g.fillPath (frame);

        const auto plot = plotArea();

        // grid: C lines with labels, faint verticals every ~86 px
        {
            juce::Graphics::ScopedSaveState ss (g);
            g.reduceClipRegion (frame);
            g.setColour (juce::Colours::white.withAlpha (0.028f));
            for (float x = plot.getX() + 46.0f; x < plot.getRight(); x += 86.0f)
                g.fillRect (x, b.getY() + 1.0f, 1.0f, b.getHeight() - 2.0f);
            for (int oct = 0; oct <= 2; ++oct)
            {
                const float y = yFor ((float) (lowNote() + oct * 12));
                g.setColour (juce::Colours::white.withAlpha (0.045f));
                g.fillRect (plot.getX() - 6.0f, y, plot.getWidth() + 6.0f, 1.0f);
                if (oct < 2)
                {
                    g.setColour (juce::Colours::white.withAlpha (0.022f));
                    g.fillRect (plot.getX() - 6.0f, yFor ((float) (lowNote() + oct * 12 + 6)), plot.getWidth() + 6.0f, 1.0f);
                }
            }
        }

        for (int oct = 0; oct <= 2; ++oct)
        {
            const int n = lowNote() + oct * 12;
            drawText (g, "C" + juce::String (n / 12 - 1), { 27.0f, yFor ((float) n) }, font (18.0f), juce::Colour (0xff8f8d97), juce::Justification::left);
        }

        {
            juce::Graphics::ScopedSaveState ss (g);
            g.reduceClipRegion (plot.expanded (0.0f, 2.0f).withTrimmedLeft (-10.0f).toNearestInt());
            if (! active)
                g.beginTransparencyLayer (0.35f);

            if (showInput)
                drawTrace (g, inCol, juce::Colour (0xff8c86aa).withAlpha (0.5f), 1.3f, false);
            drawTrace (g, outCol, juce::Colour (0xffa38bfa), 2.2f, true);
            drawDots (g);
            drawHead (g);

            if (! active)
                g.endTransparencyLayer();
        }

        // fade the oldest part on the left edge
        {
            const float x0 = plot.getX() - 10.0f;
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff0f0f13), x0, 0.0f, juce::Colour (0x000f0f13), x0 + 46.0f, 0.0f, false));
            g.fillRect (juce::Rectangle<float> (x0, b.getY() + 2.0f, 46.0f, b.getHeight() - 4.0f));
        }

        if (! hasData)
            drawText (g, vstm::i18n::tr ("Sing or play audio to see the pitch"), { plot.getCentreX(), b.getCentreY() }, font (15.0f), col::faint);

        g.setColour (juce::Colour (0xff26252c));
        g.strokePath (frame, juce::PathStrokeType (1.0f));
    }

private:
    static constexpr double kSweepSeconds = 6.0;

    juce::Rectangle<float> plotArea() const
    {
        return getLocalBounds().toFloat().withTrimmedLeft (74.0f).withTrimmedRight (6.0f).reduced (0.0f, 8.0f);
    }

    int lowNote() const { return low; }

    float yFor (float midi) const
    {
        // C(low) at y = 152, C(low + 24) at y = 28 (design units, 176 px tall display)
        const float h = (float) getHeight();
        const float top = h * (28.0f / 176.0f), bottom = h * (152.0f / 176.0f);
        return bottom - (midi - lowAnim) * (bottom - top) / 24.0f;
    }

    float xFor (int column) const
    {
        const auto p = plotArea();
        return p.getX() + (float) column * p.getWidth() / (float) juce::jmax (1, numCols);
    }

    void resetColumns()
    {
        numCols = juce::jmax (16, (int) plotArea().getWidth());
        outCol.assign ((size_t) numCols, std::numeric_limits<float>::quiet_NaN());
        inCol.assign ((size_t) numCols, std::numeric_limits<float>::quiet_NaN());
        flags.assign ((size_t) numCols, 0);
        head = 0.0;
    }

    void timerCallback() override
    {
        vstm::TuneEngine::DisplayPoint p;
        const double hopSec = engine.getHopSeconds();
        const double colsPerPoint = (double) numCols * hopSec / kSweepSeconds;
        bool any = false;

        while (engine.display.pop (p))
        {
            any = true;
            hasData = true;
            const bool voiced = (p.flags & vstm::TuneEngine::kVoiced) != 0;
            const int from = (int) std::floor (head);
            head += colsPerPoint;
            if (head >= numCols) head -= numCols;
            const int to = (int) std::floor (head);

            auto write = [&] (int c, bool fresh)
            {
                const auto i = (size_t) c;
                if (fresh) flags[i] = 0;
                outCol[i] = voiced ? p.outMidi : std::numeric_limits<float>::quiet_NaN();
                inCol[i]  = voiced ? p.inMidi  : std::numeric_limits<float>::quiet_NaN();
                flags[i] |= (uint8_t) (p.flags & (vstm::TuneEngine::kNoteChange | vstm::TuneEngine::kSettled));
            };

            if (to == from) write (from, false);
            else
                for (int c = (from + 1) % numCols; ; c = (c + 1) % numCols)
                {
                    write (c, true);
                    if (c == to) break;
                }

            if (voiced)
            {
                lastOut = p.outMidi;
                lastIn = p.inMidi;
                lastDev = p.deviation;
                lastNote = p.note;
                sinceVoiced = 0;
                // keep the trace inside the window: move by octaves
                if (p.outMidi > (float) (low + 25)) low += 12;
                else if (p.outMidi < (float) (low - 1)) low -= 12;
                low = juce::jlimit (12, 84, low);
            }
        }

        if (any)
            ++sinceVoiced;

        const bool moving = std::abs (lowAnim - (float) low) > 0.01f;
        if (moving)
            lowAnim += ((float) low - lowAnim) * 0.18f;
        else
            lowAnim = (float) low;

        if (any || moving)
            repaint();
    }

    void drawTrace (juce::Graphics& g, const std::vector<float>& data, juce::Colour c, float width, bool glow)
    {
        const int h = (int) std::floor (head);
        juce::Path p;
        auto addRun = [&] (int from, int to)
        {
            bool pen = false;
            for (int i = from; i < to; ++i)
            {
                const float v = data[(size_t) i];
                if (std::isnan (v)) { pen = false; continue; }
                const juce::Point<float> pt (xFor (i), yFor (v));
                if (! pen) { p.startNewSubPath (pt); pen = true; }
                else p.lineTo (pt);
            }
        };
        addRun (0, h + 1);
        addRun (juce::jmin (numCols, h + 3), numCols);   // previous sweep, not joined to the head

        if (glow)
        {
            g.setColour (c.withAlpha (0.10f));
            g.strokePath (p, juce::PathStrokeType (width * 3.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (c.withAlpha (0.20f));
            g.strokePath (p, juce::PathStrokeType (width * 1.9f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (c);
            g.strokePath (p, juce::PathStrokeType (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        else
        {
            g.setColour (c);
            g.strokePath (p, juce::PathStrokeType (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }

    void drawDots (juce::Graphics& g)
    {
        const int h = (int) std::floor (head);
        float lastX = -100.0f;
        for (int i = 0; i < numCols; ++i)
        {
            if ((flags[(size_t) i] & (vstm::TuneEngine::kNoteChange | vstm::TuneEngine::kSettled)) == 0) continue;
            const float v = outCol[(size_t) i];
            if (std::isnan (v) || (i > h && i < h + 3)) continue;
            const float x = xFor (i);
            if (x - lastX < 14.0f) continue;
            lastX = x;
            const auto pt = juce::Point<float> (x, yFor (v));
            g.setColour (col::accent.withAlpha (0.18f));
            g.fillEllipse (pt.x - 8.0f, pt.y - 8.0f, 16.0f, 16.0f);
            g.setColour (juce::Colour (0xff9d84f8));
            g.fillEllipse (pt.x - 5.2f, pt.y - 5.2f, 10.4f, 10.4f);
        }
    }

    void drawHead (juce::Graphics& g)
    {
        if (! hasData)
            return;
        const auto b = getLocalBounds().toFloat();
        const float x = xFor ((int) std::floor (head)) + 0.5f;
        juce::ColourGradient lg (col::accent.withAlpha (0.25f), x, b.getY(), col::accent.withAlpha (0.25f), x, b.getBottom(), false);
        lg.addColour (0.45, col::accentHi.withAlpha (0.95f));
        lg.addColour (0.55, col::accentHi.withAlpha (0.95f));
        g.setColour (col::accent.withAlpha (0.10f));
        g.fillRect (x - 3.0f, b.getY(), 6.0f, b.getHeight());
        g.setGradientFill (lg);
        g.fillRect (x - 1.0f, b.getY(), 2.0f, b.getHeight());

        if (sinceVoiced > 20 || std::isnan (lastOut))
            return;

        const auto dot = juce::Point<float> (x, yFor (lastOut));
        fillGlowCircle (g, dot, 7.5f, juce::Colour (0xffd2c8fc), 1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.9f));
        g.drawEllipse (dot.x - 7.5f, dot.y - 7.5f, 15.0f, 15.0f, 1.2f);

        // current note tag
        if (lastNote >= 0)
        {
            const int centsOff = juce::roundToInt (lastDev * 100.0f);
            const juce::String name = juce::String (vstm::noteName (lastNote)) + juce::String (lastNote / 12 - 1);
            const juce::String dev = (centsOff > 0 ? "+" : "") + juce::String (centsOff) + " ct";
            const auto fn = font (15.0f, Weight::semibold), fd = font (13.5f);
            const float w = textWidth (fn, name) + textWidth (fd, dev) + 30.0f;
            auto r = juce::Rectangle<float> (w, 26.0f).withPosition (dot.x + 14.0f, dot.y - 34.0f);
            if (r.getRight() > b.getRight() - 8.0f) r.setX (dot.x - 14.0f - w);
            if (r.getY() < b.getY() + 6.0f) r.setY (dot.y + 10.0f);
            g.setColour (juce::Colour (0xff17161d).withAlpha (0.94f));
            g.fillRoundedRectangle (r, 8.0f);
            g.setColour (col::accent.withAlpha (0.45f));
            g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);
            drawText (g, name, { r.getX() + 10.0f, r.getCentreY() }, fn, col::text, juce::Justification::left);
            drawText (g, dev, { r.getRight() - 10.0f, r.getCentreY() }, fd,
                      std::abs (centsOff) <= 10 ? col::dim : col::accentText, juce::Justification::right);
        }
    }

    vstm::TuneEngine& engine;
    std::vector<float> outCol, inCol;
    std::vector<uint8_t> flags;
    int numCols = 0;
    double head = 0.0;
    int low = 48;               // C3 at the bottom, like the reference layout
    float lowAnim = 48.0f;
    bool hasData = false, active = true;
    float lastOut = std::numeric_limits<float>::quiet_NaN(), lastIn = 0.0f, lastDev = 0.0f;
    int lastNote = -1;
    int sinceVoiced = 1000;
};
} // namespace vui
