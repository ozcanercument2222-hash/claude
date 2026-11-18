#pragma once
//==============================================================================
//  Otomatik ton (key) bulucu - guven (confidence) mekanizmali
//
//  Sesli karelerdeki perde sinifi (C, C#, ... B) iki histogramda birikir: uzun bellek
//  (25 s unutma sabiti) ve kisa bellek (6 s). Her yarim saniyede secili dizinin
//  profiliyle 12 olasi tonik icin skor hesaplanir:
//      skor = sarkinin dizi icinde kalan payi + 0.15 x profil korelasyonu
//  (Major / minor icin Krumhansl-Kessler profilleri, diger diziler icin tonik ve besliye
//  agirlik veren ikili sablon.)
//
//  Guven = (en iyi tonun ikinciye farki) x (kanit miktari) x (nota cesitliligi), 0..1.
//    - Tek nota ya da iki nota ton belirlemez: cesitlilik 0 -> guven 0, ton kilitlenmez,
//      duzeltme en yakin yarim sese gider (eskiden 2 s sonra kesin "C" diyordu).
//    - Ilk kilit ancak guven >= 0.5 iken: ayni notalari paylasan tonlar ayrilmadan (or. A minor
//      ezgide F henuz duyulmadan C / G) karar verilmez.
//    - Ton degisimi (modulasyon) kisa bellekte aranir: son notalarin bir kismi mevcut tonun
//      disindaysa ve baska bir ton onlari belirgin ve emin bicimde daha iyi kapsiyorsa, ~1 s
//      kararli kalinca gecilir; uzun bellek yeni tona gore yeniden kurulur (25 s'lik eski bilgi
//      geri cekmez).
//    - Sessizlikte / yeni ses yokken karar degismez.
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
        std::fill (std::begin (histLong), std::end (histLong), 0.0);
        std::fill (std::begin (histShort), std::end (histShort), 0.0);
        current = -1;
        candidate = -1;
        confidence = 0.0f;
        sinceEval = 0.0;
        newEvidence = 0.0;
        switchTo = -1;
        switchVotes = 0;
    }

    /** Call once per analysis hop. midi is only used when voiced. */
    void addFrame (bool voiced, double midi, double clarity, double dt) noexcept
    {
        const double decayLong = std::exp (-dt / kLongMemory);
        const double decayShort = std::exp (-dt / kShortMemory);
        for (int i = 0; i < 12; ++i)
        {
            histLong[i] *= decayLong;
            histShort[i] *= decayShort;
        }

        if (voiced)
        {
            const double nearest = std::round (midi);
            const double dev = std::abs (midi - nearest);
            const double w = dt * std::clamp (clarity, 0.0, 1.0) * std::max (0.0, 1.0 - dev * 2.0);
            const int pc = (((int) nearest % 12) + 12) % 12;
            histLong[pc] += w;
            histShort[pc] += w;
            newEvidence += w;
        }
    }

    /** Re-evaluates at most twice per second. Returns the detected tonic (0..11) or -1 while unsure. */
    int update (int scale, double dt) noexcept
    {
        sinceEval += dt;
        if (scale != lastScale)
        {
            lastScale = scale;
            current = -1;
            switchTo = -1;
            switchVotes = 0;
            sinceEval = 1.0;
        }
        if (sinceEval < 0.5)
            return current;
        sinceEval = 0.0;
        const double fresh = newEvidence;
        newEvidence = 0.0;

        if (scale == kChromatic)
        {
            candidate = 0;
            confidence = 1.0f;
            return current = 0;
        }

        Eval lng;
        evaluate (histLong, scale, lng);
        candidate = lng.best;

        if (current < 0)
        {
            if (lng.best >= 0 && lng.conf[lng.best] >= kLockConfidence)
                current = lng.best;
            confidence = current >= 0 ? lng.conf[current] : (lng.best >= 0 ? lng.conf[lng.best] : 0.0f);
            return current;
        }

        // locked: only new singing may move the key (silence / breaths never do)
        if (fresh >= kMinFreshEvidence)
        {
            Eval sht;
            evaluate (histShort, scale, sht);
            // modulation: the recent notes leave the current key (some of them are outside it) and
            // another key holds them better, clearly and confidently
            const bool modulation = sht.best >= 0 && sht.best != current
                                 && sht.conf[sht.best] >= kSwitchConfidence
                                 && sht.inside[current] < kModulationInside
                                 && sht.inside[sht.best] >= sht.inside[current] + 0.02
                                 && sht.score[sht.best] > sht.score[current] + kSwitchMargin;
            const bool drift = lng.best >= 0 && lng.best != current
                            && lng.conf[lng.best] >= kSwitchConfidence
                            && lng.score[lng.best] > lng.score[current] + kSwitchMargin;
            const int proposal = modulation ? sht.best : (drift ? lng.best : -1);
            if (proposal >= 0 && proposal == switchTo)
                ++switchVotes;
            else if (proposal >= 0)
            {
                switchTo = proposal;
                switchVotes = 1;
            }
            else if (switchVotes > 0)
                --switchVotes;                        // one weak look does not cancel a building switch
            if (switchTo >= 0 && switchVotes >= kSwitchVotes)
            {
                current = switchTo;
                switchTo = -1;
                switchVotes = 0;
                if (modulation)                       // the long memory follows the new key
                    std::copy (std::begin (histShort), std::end (histShort), std::begin (histLong));
                evaluate (histLong, scale, lng);
            }
        }
        confidence = lng.conf[current];
        return current;
    }

    int getKey() const noexcept                 { return current; }
    /** Best tonic so far, also before the key is locked (-1 = nothing heard yet). */
    int getCandidate() const noexcept           { return candidate; }
    /** 0..1: how sure the detector is about getKey() (or, before the lock, about getCandidate()). */
    float getConfidence() const noexcept        { return confidence; }

private:
    static constexpr double kLongMemory = 25.0;          // seconds
    static constexpr double kShortMemory = 6.0;
    static constexpr float kLockConfidence = 0.5f;
    static constexpr float kSwitchConfidence = 0.6f;
    static constexpr double kSwitchMargin = 0.03;
    static constexpr double kModulationInside = 0.97;    // some recent notes lie outside the current key
    static constexpr double kMinFreshEvidence = 0.15;    // ~0.2 s of clear singing since the last look
    static constexpr int kSwitchVotes = 3;               // the same new key wins 3 looks (~1 s, a miss takes one back)

    struct Eval
    {
        int best = -1;
        double score[12] {}, inside[12] {};
        float conf[12] {};
    };

    /** Scores, inside fractions and confidences of the 12 tonics on one histogram. */
    static void evaluate (const double* h, int scale, Eval& e) noexcept
    {
        double total = 0.0;
        for (int i = 0; i < 12; ++i) total += h[i];
        e.best = -1;
        std::fill (std::begin (e.conf), std::end (e.conf), 0.0f);
        if (total < 0.2)                               // nothing worth judging yet
            return;

        double profile[12];
        makeProfile (scale, profile);
        const uint16_t mask = scaleMask (scale);
        int best = 0;
        for (int tonic = 0; tonic < 12; ++tonic)
        {
            double inside = 0.0;
            for (int i = 0; i < 12; ++i)
                if ((mask >> i) & 1u)
                    inside += h[(tonic + i) % 12];
            e.inside[tonic] = inside / total;
            // dominant term: how much of the sung material belongs to the scale;
            // profile correlation decides between keys that share the same notes
            e.score[tonic] = e.inside[tonic] + 0.15 * correlate (h, tonic, profile);
            if (e.score[tonic] > e.score[best])
                best = tonic;
        }
        e.best = best;

        // evidence: about 1.5 - 3 s of clear singing; variety: how many different notes carry weight
        int variety = 0;
        for (int i = 0; i < 12; ++i)
            if (h[i] >= 0.05 * total)
                ++variety;
        const float evidence = smoothstep (0.8f, 2.0f, (float) total);
        const float spread = smoothstep (2.5f, 5.0f, (float) variety);
        for (int k = 0; k < 12; ++k)
        {
            double rival = -1.0e9;
            for (int j = 0; j < 12; ++j)
                if (j != k) rival = std::max (rival, e.score[j]);
            const float margin = clamp01 ((float) ((e.score[k] - rival - 0.01) / 0.06));
            e.conf[k] = margin * evidence * spread;
        }
    }

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

    static double correlate (const double* h, int tonic, const double* profile) noexcept
    {
        double mx = 0.0, my = 0.0;
        for (int i = 0; i < 12; ++i) { mx += h[(tonic + i) % 12]; my += profile[i]; }
        mx /= 12.0; my /= 12.0;

        double sxy = 0.0, sxx = 0.0, syy = 0.0;
        for (int i = 0; i < 12; ++i)
        {
            const double dx = h[(tonic + i) % 12] - mx, dy = profile[i] - my;
            sxy += dx * dy; sxx += dx * dx; syy += dy * dy;
        }
        return (sxx > 0.0 && syy > 0.0) ? sxy / std::sqrt (sxx * syy) : 0.0;
    }

    double histLong[12] {}, histShort[12] {};
    int current = -1, candidate = -1;
    float confidence = 0.0f;
    int lastScale = -1;
    double sinceEval = 0.0, newEvidence = 0.0;
    int switchTo = -1, switchVotes = 0;
};
} // namespace vstm
