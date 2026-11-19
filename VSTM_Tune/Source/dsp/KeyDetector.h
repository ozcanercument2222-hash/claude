#pragma once
//==============================================================================
//  Otomatik ton (key) bulucu: vokalden TAHMIN, bir kez analiz + kilit
//
//  Bu bulucu yalniz eklentiye gelen vokali duyar, beat'i duymaz: sonucu bir tahmindir.
//  Kullanicinin is akisinda ton beat'e gore secilir; vokalin yanlis / kromatik / gecis
//  notalari bu tonu degistirmemelidir. Bu yuzden:
//
//    1) DINLEME   kanit yok -> aday yok. Duzeltme en yakin yarim sese (guvenli geri donus).
//    2) ADAY      en iyi ton ve guveni gosterilir ("A? 41%"), ama kilitli degildir; duzeltme
//                 yine en yakin yarim sese. Kullanici istediginde "Simdi kilitle" der.
//    3) KILITLI   yeterli ve AYIRT EDICI kanit toplaninca (uzun bellekte ~4 s net sarki, guven >= 0.5,
//                 ayni aday arka arkaya 6 bakista, her bakista yeni sarki; gercek kayitlarda tipik
//                 olarak 10-20 s) ton bir kez kilitlenir ve bundan sonra
//                 KENDILIGINDEN DEGISMEZ. Yalniz kullanici degistirir: "Yeniden analiz"
//                 (kanit silinir, bastan dinler), "Onerilen tona gec" ya da ayri ve varsayilan
//                 kapali "Ton degisimlerini izle" (modulasyon) secenegi.
//
//  Kanit: sesli karelerdeki perde sinifi (C, C#, ... B) iki histogramda birikir: uzun bellek
//  (25 s unutma sabiti) ve kisa bellek (6 s; yalniz modulasyon izlenirken kullanilir). Her
//  yarim saniyede secili dizinin profiliyle 12 olasi tonik icin skor hesaplanir:
//      skor = sarkinin dizi icinde kalan payi + 0.15 x profil korelasyonu
//  (Major / minor icin Krumhansl-Kessler profilleri, diger diziler icin tonik ve besliye
//  agirlik veren ikili sablon.)
//
//  Guven = (en iyi skorun rakibe farki) x (ayirt edicilik) x (kanit miktari) x (nota cesitliligi), 0..1
//    - Tek nota ya da iki nota ton belirlemez: cesitlilik 0 -> guven 0.
//    - Ayirt edicilik: aday, sarkinin rakiplerin aciklayamadigi bir payini aciklamali. C D E G A
//      (pentatonik) C, F ve G majorun hepsine uyar: hangisi oldugunu yalniz F / F# / B / Bb
//      soyler. O notalar duyulmadan guven dusuk kalir, kilit olmaz (onceki surum profil
//      korelasyonuyla 3 s'de C'ye kilitleniyordu; tek seferlik kilitte bu kalici yanlis ton olurdu).
//      Ayni nota kumesini veren tonikler (simetrik ozel diziler) rakip sayilmaz: duzeltme aynidir.
//    - Gosterilen guven bu hesabin kendisidir (arayuz yuvarlar, baska bir sey eklemez).
//
//  Dizi turu degisince (Scale secimi ya da ozel dizi ac / kapa) kilitli ton korunur; mevcut kanit
//  yeni dizide BASKA bir tonigi emin bicimde (guven >= 0.5) gosteriyorsa ona gecilir (or. A minor
//  ezgide Minor -> Major: A -> C, ayni notalar). Ozel dizinin notalarini duzenlemek tonu oynatmaz.
//==============================================================================
#include "DspCommon.h"
#include "Scales.h"

namespace vstm
{
class KeyDetector
{
public:
    enum State { kListening = 0, kCandidate = 1, kLocked = 2, kNotNeeded = 3 };

    /** Forget every piece of evidence and the lock: listen again from now on (Re-analyze). */
    void reset() noexcept
    {
        std::fill (std::begin (histLong), std::end (histLong), 0.0);
        std::fill (std::begin (histShort), std::end (histShort), 0.0);
        current = -1;
        candidate = -1;
        confidence = 0.0f;
        lockConfidence = 0.0f;
        sinceEval = 0.0;
        newEvidence = 0.0;
        switchTo = -1;
        switchVotes = 0;
        stableKey = -1;
        stableLooks = 0;
        suggestion = -1;
        suggestionConfidence = 0.0f;
        state = kListening;
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

    /** Re-evaluates at most twice per second. Returns the locked tonic (0..11) or -1 while unlocked. */
    int update (int scale, double dt, bool followChanges = false) noexcept
    {
        return update (scale, scaleMask (scale), dt, followChanges);
    }

    /** Same with an explicit note set (bit i = i semitones above the tonic), e.g. a custom scale;
        scale -1 = no known scale type (a plain template is built from the mask).
        followChanges: the opt-in "follow key changes" mode (modulation tracking after the lock). */
    int update (int scale, uint16_t mask, double dt, bool followChanges = false) noexcept
    {
        sinceEval += dt;
        mask = (uint16_t) (mask & 0x0fff);
        const bool everyNote = mask == 0x0fff || mask == 0;
        const int identity = (scale + 1) * 4096 + mask;
        if (identity != lastIdentity)
        {
            // another note set: a lock in progress starts over; an existing lock only moves when
            // the scale TYPE changed and the evidence clearly names another tonic for it
            const bool typeChanged = scale != lastScaleType;
            lastIdentity = identity;
            lastScaleType = scale;
            stableKey = -1;
            stableLooks = 0;
            switchTo = -1;
            switchVotes = 0;
            if (current >= 0 && typeChanged && ! everyNote)
            {
                Eval e;
                evaluate (histLong, scale, mask, e);
                if (e.best >= 0 && e.best != current && e.conf[e.best] >= kLockConfidence)
                {
                    current = e.best;
                    lockConfidence = e.conf[e.best];
                }
            }
            sinceEval = 1.0;                                    // look right away
        }
        if (sinceEval < 0.5)
            return current;
        sinceEval = 0.0;
        const double fresh = newEvidence;
        newEvidence = 0.0;

        if (everyNote)                                           // every note allowed: the key does not matter
        {
            state = kNotNeeded;
            suggestion = -1;
            return current;
        }

        Eval lng;
        evaluate (histLong, scale, mask, lng);

        if (current < 0)
        {
            candidate = lng.best;
            confidence = lng.best >= 0 ? lng.conf[lng.best] : 0.0f;
            // a look only confirms when new singing arrived since the last one (no lock on silence)
            if (fresh >= kMinFreshEvidence)
            {
                if (candidate >= 0 && confidence >= kLockConfidence)
                {
                    stableLooks = candidate == stableKey ? stableLooks + 1 : 1;
                    stableKey = candidate;
                }
                else
                {
                    stableLooks = 0;
                    stableKey = -1;
                }
            }
            double total = 0.0;
            for (double v : histLong) total += v;
            if (stableKey >= 0 && stableLooks >= kLockLooks && total >= kLockMinEvidence && confidence >= kLockConfidence)
            {
                current = stableKey;
                lockConfidence = confidence;
            }
            state = current >= 0 ? kLocked : (candidate >= 0 ? kCandidate : kListening);
            return current;
        }

        // locked. Only the opt-in mode lets the singing move the key (silence / breaths never do)
        if (followChanges && fresh >= kMinFreshEvidence)
        {
            Eval sht;
            evaluate (histShort, scale, mask, sht);
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
                lockConfidence = modulation ? sht.conf[current] : lng.conf[current];
                switchTo = -1;
                switchVotes = 0;
                if (modulation)                       // the long memory follows the new key
                    std::copy (std::begin (histShort), std::end (histShort), std::begin (histLong));
                evaluate (histLong, scale, mask, lng);
            }
        }

        // shown, never applied by itself: the evidence now clearly points to another key
        candidate = lng.best;
        suggestion = -1;
        suggestionConfidence = 0.0f;
        if (lng.best >= 0 && lng.best != current && lng.conf[lng.best] >= kSwitchConfidence
            && lng.score[lng.best] > lng.score[current] + kSwitchMargin)
        {
            suggestion = lng.best;
            suggestionConfidence = lng.conf[lng.best];
        }
        confidence = lockConfidence;
        state = kLocked;
        return current;
    }

    /** The user locks a key now ("Lock now", "Use the suggested key", a restored session). The
        evidence is kept; nothing moves the key afterwards unless the user asks again. */
    void lockTo (int key, float conf) noexcept
    {
        if (key < 0 || key > 11)
            return;
        current = key;
        lockConfidence = std::isfinite (conf) ? clamp01 (conf) : 0.0f;
        confidence = lockConfidence;
        candidate = key;
        switchTo = -1;
        switchVotes = 0;
        stableKey = -1;
        stableLooks = 0;
        suggestion = -1;
        suggestionConfidence = 0.0f;
        if (state != kNotNeeded)
            state = kLocked;
    }

    /** Registers the note set the next update() will see, without judging it: a key the user just
        locked (or a restored session) is not re-judged because the scale arrived with it. */
    void noteScale (int scale, uint16_t mask) noexcept
    {
        lastIdentity = (scale + 1) * 4096 + (mask & 0x0fff);
        lastScaleType = scale;
    }

    bool isLocked() const noexcept              { return current >= 0; }
    /** The locked tonic, -1 while unlocked. */
    int getKey() const noexcept                 { return current; }
    /** Best tonic of the evidence so far (-1 = nothing heard yet). */
    int getCandidate() const noexcept           { return candidate; }
    /** Unlocked: how sure the detector is about the candidate (0..1). Locked: the confidence the key
        had when it was locked (a user lock keeps the value it was given). */
    float getConfidence() const noexcept        { return confidence; }
    /** Locked only: another tonic the evidence now clearly prefers (shown, not applied), else -1. */
    int getSuggestion() const noexcept          { return suggestion; }
    float getSuggestionConfidence() const noexcept { return suggestionConfidence; }
    /** kListening, kCandidate, kLocked, or kNotNeeded (every note allowed: the key does not matter). */
    int getState() const noexcept               { return state; }

private:
    static constexpr double kLongMemory = 25.0;          // seconds
    static constexpr double kShortMemory = 6.0;
    // The one-shot lock waits for several phrases: an opening phrase often leans on another key (the
    // dominant: "Caro mio ben" opens in A, the song is in D). Chosen on 145 real recordings (VSTM_TUNE_AUDIT.md
    // 17.2): locks that disagree with the key the whole recording points to, by stable looks / evidence:
    // 3 / none 10 of 57, 6 / none 6 of 37, 3 / 4.0 1 of 25, 6 / 4.0 0 of 20 (median lock 7 -> 17.5 s).
    static constexpr float kLockConfidence = 0.5f;
    static constexpr int kLockLooks = 6;                 // the same confident candidate in 6 looks with new singing (~3 s)
    static constexpr double kLockMinEvidence = 4.0;      // long-memory weight: about 4 s of clear, in-tune singing
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

    static uint16_t rotated (uint16_t mask, int tonic) noexcept
    {
        const unsigned m = mask & 0x0fffu;
        return (uint16_t) (((m << tonic) | (m >> (12 - tonic))) & 0x0fffu);
    }

    /** Scores, inside fractions and confidences of the 12 tonics on one histogram. */
    static void evaluate (const double* h, int scale, uint16_t mask, Eval& e) noexcept
    {
        double total = 0.0;
        for (int i = 0; i < 12; ++i) total += h[i];
        e.best = -1;
        std::fill (std::begin (e.conf), std::end (e.conf), 0.0f);
        if (total < 0.2)                               // nothing worth judging yet
            return;

        double profile[12];
        makeProfile (scale, mask, profile);
        uint16_t notes[12];                            // the absolute note set of each tonic
        int best = 0;
        for (int tonic = 0; tonic < 12; ++tonic)
        {
            notes[tonic] = rotated (mask, tonic);
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
            // rivals: tonics that would tune to another note set (the same set tunes the same)
            double rivalScore = -1.0e9, rivalInside = -1.0;
            for (int j = 0; j < 12; ++j)
                if (j != k && notes[j] != notes[k])
                {
                    rivalScore = std::max (rivalScore, e.score[j]);
                    rivalInside = std::max (rivalInside, e.inside[j]);
                }
            if (rivalInside < 0.0)                     // every tonic gives the same notes
            {
                e.conf[k] = evidence * spread;
                continue;
            }
            const float margin = clamp01 ((float) ((e.score[k] - rivalScore - 0.01) / 0.06));
            // the candidate must explain sung notes its rivals cannot (about 2.5 % of the singing
            // for full weight): without a note that tells the keys apart the guess stays a guess
            const float distinct = clamp01 ((float) ((e.inside[k] - rivalInside + 0.005) / 0.03));
            e.conf[k] = margin * distinct * evidence * spread;
        }
    }

    static void makeProfile (int scale, uint16_t m, double* p) noexcept
    {
        static const double major[12] = { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
        static const double minor[12] = { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };

        if (scale == kMajor && m == scaleMask (kMajor))
        {
            std::copy (major, major + 12, p);
            return;
        }
        if ((scale == kMinor || scale == kHarmonicMinor || scale == kMelodicMinor) && m == scaleMask (scale))
        {
            std::copy (minor, minor + 12, p);
            for (int i = 0; i < 12; ++i)          // nudge toward the exact note set of the variant
                if ((m >> i) & 1u) p[i] += 0.8;
            return;
        }

        // any other note set (modes, pentatonics, Hicaz, a custom scale): tonic and fifth weighted
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
    float confidence = 0.0f, lockConfidence = 0.0f;
    int lastIdentity = -1, lastScaleType = -2;
    double sinceEval = 0.0, newEvidence = 0.0;
    int switchTo = -1, switchVotes = 0;
    int stableKey = -1, stableLooks = 0;
    int suggestion = -1;
    float suggestionConfidence = 0.0f;
    int state = kListening;
};
} // namespace vstm
