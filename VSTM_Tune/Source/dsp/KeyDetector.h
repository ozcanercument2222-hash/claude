#pragma once
//==============================================================================
//  Otomatik ton (key) bulucu
//
//  Sesli karelerdeki perde sinifi (C, C#, ... B) histogrami yavasca birikir (25 s
//  unutma sabiti). Her yarim saniyede secili dizinin profiliyle 12 olasi tonik
//  icin korelasyon hesaplanir; en iyi tonik, mevcut tondan belirgin sekilde daha
//  iyiyse degistirilir (titresmeyi onler). Major / minor icin Krumhansl-Kessler
//  profilleri, diger diziler icin tonik ve besliye agirlik veren ikili sablon.
//==============================================================================
#include "DspCommon.h"
#include "Scales.h"

namespace vstm
{
class KeyDetector
{
public:
    void reset() noexcept
    {
        std::fill (std::begin (hist), std::end (hist), 0.0);
        current = -1;
        sinceEval = 0.0;
    }

    /** Call once per analysis hop. midi is only used when voiced. */
    void addFrame (bool voiced, double midi, double clarity, double dt) noexcept
    {
        const double decay = std::exp (-dt / 25.0);
        for (auto& h : hist)
            h *= decay;

        if (voiced)
        {
            const double nearest = std::round (midi);
            const double dev = std::abs (midi - nearest);
            const double w = dt * std::clamp (clarity, 0.0, 1.0) * std::max (0.0, 1.0 - dev * 2.0);
            const int pc = (((int) nearest % 12) + 12) % 12;
            hist[pc] += w;
        }
    }

    /** Re-evaluates at most twice per second. Returns the detected tonic (0..11) or -1 when unsure. */
    int update (int scale, double dt) noexcept
    {
        sinceEval += dt;
        if (scale != lastScale)
        {
            lastScale = scale;
            current = -1;
            sinceEval = 1.0;
        }
        if (sinceEval < 0.5)
            return current;
        sinceEval = 0.0;

        double total = 0.0;
        for (double h : hist) total += h;
        if (total < 1.2)                       // about a second of clear singing
            return current;

        if (scale == kChromatic)
            return current = 0;

        double profile[12];
        makeProfile (scale, profile);

        // score = how much of the sung material belongs to the scale (dominant term)
        //       + profile correlation (decides between keys that share the same notes)
        const uint16_t mask = scaleMask (scale);
        double scores[12];
        int best = 0;
        for (int tonic = 0; tonic < 12; ++tonic)
        {
            double inside = 0.0;
            for (int i = 0; i < 12; ++i)
                if ((mask >> i) & 1u)
                    inside += hist[(tonic + i) % 12];
            scores[tonic] = inside / total + 0.15 * correlate (tonic, profile);
            if (scores[tonic] > scores[best])
                best = tonic;
        }

        if (current < 0 || (best != current && scores[best] > scores[current] + 0.03))
            current = best;
        return current;
    }

    int getKey() const noexcept { return current; }

private:
    static void makeProfile (int scale, double* p) noexcept
    {
        static const double major[12] = { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
        static const double minor[12] = { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };

        if (scale == kMajor)
        {
            std::copy (major, major + 12, p);
            return;
        }
        if (scale == kMinor || scale == kHarmonicMinor || scale == kMelodicMinor)
        {
            std::copy (minor, minor + 12, p);
            const uint16_t m = scaleMask (scale);
            for (int i = 0; i < 12; ++i)          // nudge toward the exact note set of the variant
                if ((m >> i) & 1u) p[i] += 0.8;
            return;
        }

        const uint16_t m = scaleMask (scale);
        for (int i = 0; i < 12; ++i)
            p[i] = ((m >> i) & 1u) ? 1.0 : 0.0;
        p[0] += 0.6;                               // tonic
        if ((m >> 7) & 1u) p[7] += 0.3;            // fifth
    }

    double correlate (int tonic, const double* profile) const noexcept
    {
        double mx = 0.0, my = 0.0;
        for (int i = 0; i < 12; ++i) { mx += hist[(tonic + i) % 12]; my += profile[i]; }
        mx /= 12.0; my /= 12.0;

        double sxy = 0.0, sxx = 0.0, syy = 0.0;
        for (int i = 0; i < 12; ++i)
        {
            const double dx = hist[(tonic + i) % 12] - mx, dy = profile[i] - my;
            sxy += dx * dy; sxx += dx * dx; syy += dy * dy;
        }
        return (sxx > 0.0 && syy > 0.0) ? sxy / std::sqrt (sxx * syy) : 0.0;
    }

    double hist[12] {};
    int current = -1;
    int lastScale = -1;
    double sinceEval = 0.0;
};
} // namespace vstm
