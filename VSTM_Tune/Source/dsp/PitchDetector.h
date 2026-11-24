#pragma once
//==============================================================================
//  Perde (temel frekans) algilayici
//
//  YIN (de Cheveigne & Kawahara 2002) - kaba arama, alcak-gecirgen ve seyreltilmis
//  (~11-12 kHz) sinyal uzerinde yapilir; sonuc tam ornekleme hizinda, kaba periyodun
//  cevresinde dar bir aralikta ince ayarlanir (yuksek notalarda da cent hassasiyeti).
//  Oktav atlamalarina karsi bir onceki periyotla sureklilik kontrolu yapar; oktav karari
//  (P mi 2P mi) seyreltilmis CMND ile degil, tam hizdaki sinyalin periyodikligiyle verilir.
//==============================================================================
#include "DspCommon.h"

namespace vstm
{
class PitchDetector
{
public:
    struct Result
    {
        bool   voiced = false;
        double period = 0.0;        // full-rate samples
        float  clarity = 0.0f;      // 1 - CMND minimum (0..1)
        float  rms = 0.0f;          // level of the analysed (low-passed) frame
        double centreDelay = 0.0;   // how many full-rate samples before "now" the estimate is centred
    };

    void prepare (double sampleRate, int decimationFactor)
    {
        fullRate = sampleRate;
        decim = std::max (1, decimationFactor);
        decRate = fullRate / decim;
        dec.assign ((size_t) kDecRing, 0.0f);
        frame.assign ((size_t) (kWindow + 1024), 0.0f);
        diff.assign (1024, 0.0f);
        cmnd.assign (1024, 0.0f);
        setRange (75.0, 1000.0);
        reset();
    }

    void setRange (double minHz, double maxHz)
    {
        tauMax = std::min (1000, (int) std::ceil (decRate / minHz) + 2);
        tauMin = std::max (2, (int) std::floor (decRate / maxHz));
        shortLagCheck = maxHz > kShortLagRangeHz;
    }

    void reset()
    {
        std::fill (dec.begin(), dec.end(), 0.0f);
        decCount = 0;
        wasVoiced = false;
        lastPeriod = 0.0;
        voicedRms = 0.0f;
        voicedRun = 0;
    }

    /** Feed one sample of the already low-passed, decimated signal. */
    void pushDecimated (float x) noexcept
    {
        dec[(size_t) (decCount & (kDecRing - 1))] = x;
        ++decCount;
    }

    /** Run one analysis on the newest samples.
        fullRing/fullMask: full-rate low-passed mono ring buffer, `endIndex` = index one past the newest sample. */
    Result analyse (const float* fullRing, int fullMask, int64_t endIndex) noexcept
    {
        Result r;
        const int n = kWindow + tauMax;
        if (decCount < n)
            return r;

        // newest n decimated samples, oldest first
        double energy = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const float v = dec[(size_t) ((decCount - n + i) & (kDecRing - 1))];
            frame[(size_t) i] = v;
            energy += (double) v * v;
        }
        r.rms = (float) std::sqrt (energy / n);

        // difference function + cumulative mean normalised difference
        diff[0] = 0.0f;
        cmnd[0] = 1.0f;
        double running = 0.0;
        for (int tau = 1; tau < tauMax; ++tau)
        {
            double s = 0.0;
            const float* a = frame.data();
            const float* b = frame.data() + tau;
            for (int j = 0; j < kWindow; ++j)
            {
                const float d = a[j] - b[j];
                s += (double) d * d;
            }
            diff[(size_t) tau] = (float) s;
            running += s;
            cmnd[(size_t) tau] = running > 0.0 ? (float) (s * tau / running) : 1.0f;
        }

        // first dip under the absolute threshold, else the global minimum
        int best = -1;
        for (int tau = tauMin; tau < tauMax - 1; ++tau)
        {
            if (cmnd[(size_t) tau] < kThreshold)
            {
                while (tau + 1 < tauMax - 1 && cmnd[(size_t) (tau + 1)] < cmnd[(size_t) tau])
                    ++tau;
                best = tau;
                break;
            }
        }
        if (best < 0)
        {
            float m = 1.0e9f;
            for (int tau = tauMin; tau < tauMax - 1; ++tau)
                if (cmnd[(size_t) tau] < m) { m = cmnd[(size_t) tau]; best = tau; }
        }
        if (best < 0)
            return r;

        // A shorter period the decimated grid hides. A signal with period P is just as periodic with
        // 2P, 3P, 4P: YIN takes the first dip under its threshold, but at the top of the range the
        // period is 8 - 12 decimated samples and falls between two lags (soprano "i" at E6, 1319 Hz:
        // the 2nd harmonic dominates after the anti-alias filter and YIN read 2P or 4P, 12 / 24 st
        // low, in every earlier version). Only a dip that is already visible at best / k (k = 4, 3, 2,
        // shortest first) is checked, on the full-rate signal, with the octave rule's own test: the
        // short period wins unless the long one is clearly more periodic (a real long period, e.g.
        // pulses that alternate, keeps it). Only short candidate lags (<= kSubharmonicMaxLag, above
        // ~0.9-1 kHz): applied to every lag, voices with a weak fundamental went an octave UP (real-
        // derived corpus: sustained octave runs 14 -> 18; 1.2 report), where the grid is fine anyway.
        // Only in a range that reaches above kShortLagRangeHz (Soprano / Alto): in the Tenor range
        // (top 1050 Hz) it fixed nothing measurable and made two kinds of error (1.2 report, step 11):
        // female voices went an octave up, and where YIN had already taken a wrong short lag in a
        // consonant inside a held low note it moved that further up (m11 "dona": 525 Hz -> 1050 Hz for a
        // 208 Hz voice), and the hard-tuned grains peaked up to +9.6 dB over the dry signal there.
        if (shortLagCheck && best >= 2 * tauMin && tauMin <= kSubharmonicMaxLag)
        {
            const double frameCentre = (double) (kWindow + tauMax) * decim - (kWindow + best) * decim * 0.5;
            for (int k = 4; k >= 2; --k)
            {
                const double sub = (double) best / k;
                if (sub < tauMin || sub > kSubharmonicMaxLag)
                    continue;
                const int lm = localMin ((int) std::lround (sub));
                if (lm < 0 || cmnd[(size_t) lm] > kSubharmonicDip || std::abs (lm - sub) > 1.5)
                    continue;
                const int w = std::clamp ((int) (1.5 * best * decim), 256, 2048);
                const int64_t base = std::min (endIndex - (int64_t) std::llround (frameCentre) - (w + best * decim) / 2,
                                               endIndex - w - (int64_t) (best * decim + decim + 1));
                if (! clearlyMorePeriodic (periodicity (best, base, w, fullRing, fullMask),
                                          periodicity (lm, base, w, fullRing, fullMask)))
                {
                    best = lm;
                    break;
                }
            }
        }

        // continuity: guard against octave jumps relative to the previous voiced period.
        // Octave choices are judged on the full-rate signal between the lag samples
        // (periodicity()): on the decimated grid a short period reads too shallow (A5 at ~12 kHz:
        // 13.4 samples -> CMND 0.024 at lag 13 but 0.006 at lag 27).
        if (wasVoiced && lastPeriod > 0.0)
        {
            const double prevTau = lastPeriod / decim;
            const double ratio = best / prevTau;
            if (ratio > 1.8 && ratio < 2.2)
            {
                // YIN dropped an octave: the previous octave stays while it is still a dip, unless
                // the long lag is overwhelmingly more periodic (a real downward leap). Real voices
                // often alternate slightly from pulse to pulse, so "somewhat more periodic" at 2P
                // is no evidence: that would move them an octave down.
                const int alt = localMin ((int) std::lround (best * 0.5));
                if (alt > 0 && cmnd[(size_t) alt] < kStayVoiced)
                {
                    const int w = std::clamp ((int) (1.5 * best * decim), 256, 2048);
                    const int64_t base = endIndex - w - (best * decim + decim + 1);
                    if (! (periodicity (best, base, w, fullRing, fullMask) + kOctaveAbsMargin
                           < kOctaveRelease * periodicity (alt, base, w, fullRing, fullMask)))
                        best = alt;
                }
            }
            else if (ratio > 0.45 && ratio < 0.55)
            {
                // YIN went an octave up. A signal with period P is also periodic with 2P, so the
                // previous (lower) octave is kept only when the signal is clearly more periodic
                // with the long lag (odd harmonics present: the period really is the long one).
                // The old rule kept it whenever its decimated dip was not much worse, which is
                // always true for a periodic signal: a legato octave-up leap stayed an octave low
                // for the whole note.
                const int alt = localMin ((int) std::lround (best * 2.0));
                if (alt > 0)
                {
                    const int w = std::clamp ((int) (1.5 * alt * decim), 256, 2048);
                    const int64_t base = endIndex - w - (alt * decim + decim + 1);
                    if (clearlyMorePeriodic (periodicity (alt, base, w, fullRing, fullMask),
                                             periodicity (best, base, w, fullRing, fullMask)))
                        best = alt;
                }
            }
        }

        const float minVal = cmnd[(size_t) best];
        double tauF = best;
        if (best > 1 && best < tauMax - 1)
        {
            const double a = cmnd[(size_t) (best - 1)], b = cmnd[(size_t) best], c = cmnd[(size_t) (best + 1)];
            const double den = a - 2.0 * b + c;
            if (den > 1.0e-12)
                tauF = best + std::clamp (0.5 * (a - c) / den, -1.0, 1.0);
        }

        r.clarity = 1.0f - std::min (1.0f, minVal);
        const bool loudEnough = r.rms > kGate;
        // A voice that keeps going: while the previous hop was voiced, this dip continues its pitch
        // (within kContinuityStep semitones) and the level stays near the voice's own recent level,
        // a shallower dip still counts as voiced. Fast wide vibrato in low voices smears the dip within
        // one frame (bass +-200 ct at 6.5 Hz: clarity 0.58 - 0.69 in the steep phases, under the 0.66
        // needed to stay voiced: a third of each cycle dropped out). A consonant or the end of a note
        // loses level (below the analysis band's -6 dB) or pitch continuity and still ends the voicing.
        // Only an established voice (kContinuityMinHops voiced hops in a row, ~22 ms): a chance period in
        // aperiodic sound (an irregular click train became voiced for 5 hops) must not be held on to.
        bool continuing = false;
        if (wasVoiced && lastPeriod > 0.0 && voicedRun >= kContinuityMinHops)
            continuing = std::abs (12.0 * std::log2 (tauF * decim / lastPeriod)) < kContinuityStep
                      && r.rms > kContinuityLevel * voicedRms;
        r.voiced = loudEnough && minVal < (wasVoiced ? (continuing ? kStayVoicedContinuing : kStayVoiced) : kBecomeVoiced);
        if (r.voiced)
            voicedRms = voicedRms > 0.0f && wasVoiced ? voicedRms + (r.rms - voicedRms) * 0.05f : r.rms;
        voicedRun = r.voiced ? std::min (voicedRun + 1, 1 << 20) : 0;
        // The estimate describes the middle of the YIN frame (centreDelay before "now"): the voicing
        // decision and the time stamp the engine files it under (TuneEngine::Rec) both come from that
        // frame, so the full-rate refinement must measure the same audio. 1.0 / 1.1 refined on the
        // newest samples (~6 ms back, the frame centre is ~19-26 ms back): in vibrato and at note
        // changes the period was ~17 ms newer than its time stamp (exact-truth corpus, 1.2 report).
        r.centreDelay = (double) (kWindow + tauMax) * decim - (kWindow + tauF) * decim * 0.5;
        r.period = refine (tauF * decim, fullRing, fullMask, endIndex, endIndex - (int64_t) std::llround (r.centreDelay));

        wasVoiced = r.voiced;
        if (r.voiced)
            lastPeriod = r.period;
        return r;
    }

    double getDecimatedRate() const noexcept { return decRate; }

private:
    static constexpr int kDecRing = 8192;
    static constexpr int kWindow = 256;
    static constexpr float kThreshold = 0.15f;
    static constexpr float kBecomeVoiced = 0.22f;
    static constexpr float kStayVoiced = 0.34f;
    static constexpr float kStayVoicedContinuing = 0.50f;   // the voice keeps its pitch and level: see analyse()
    static constexpr double kContinuityStep = 2.0;          // semitones
    static constexpr float kContinuityLevel = 0.5f;         // rms relative to the recent voiced rms
    static constexpr int kContinuityMinHops = 8;             // voiced hops in a row before the rule applies
    static constexpr float kGate = 0.0015f;       // about -56 dBFS
    static constexpr int kRefineLags = 64;        // lags per refine() window (192 kHz: 2 x 18 + 2)
    // Octave thresholds on periodicity() residuals, chosen on real voices (145 recordings: 14
    // VocalSet singers, RAVDESS, a dry rap vocal, a separated stem; 7 variants compared, see
    // VSTM_TUNE_AUDIT.md 16.1). Real voices often alternate slightly from pulse to pulse, so twice
    // the period is commonly "somewhat" more periodic: only a clear (up) or overwhelming (down)
    // difference moves the estimate away from the previous octave.
    static constexpr double kOctaveRatio = 0.5;          // keep the lower octave: residual < 1/2 ...
    static constexpr double kOctaveAbsMargin = 0.01;     // ... and not just both tiny
    static constexpr double kOctaveRelease = 0.25;       // leave the previous octave downwards: < 1/4
    static constexpr float kSubharmonicDip = 0.5f;          // a dip at best / k worth checking (decimated CMND)
    static constexpr double kSubharmonicMaxLag = 12.0;      // decimated samples: only where the grid is too coarse
    static constexpr double kShortLagRangeHz = 1200.0;      // the check runs only when the range's top is above this

    static bool clearlyMorePeriodic (double longLag, double shortLag) noexcept
    {
        return longLag + kOctaveAbsMargin < kOctaveRatio * shortLag;
    }

    /** How periodic the newest full-rate samples are with a lag near the decimated lag `coarseTau`:
        sum (x[j] - x[j+L])^2 / sum (x[j]^2 + x[j+L]^2), minimum over the full-rate lags around it,
        refined between them. 0 = exactly periodic, about 1 = unrelated. Only used for octave
        decisions (a few hops per phrase), never on every hop. */
    double periodicity (int coarseTau, int64_t base, int window, const float* x, int mask) const noexcept
    {
        const double centre = (double) coarseTau * decim;
        const int lo = std::max (2, (int) std::floor (centre) - decim - 1);
        const int count = std::min (64, (int) std::ceil (centre) + decim + 1 - lo + 1);
        if (x == nullptr || base < 0 || count < 3)
            return cmnd[(size_t) coarseTau];

        double vals[64];
        int bestIdx = 0;
        for (int i = 0; i < count; ++i)
        {
            double s = 0.0, e = 0.0;
            for (int j = 0; j < window; ++j)
            {
                const float a = x[(base + j) & mask], b = x[(base + j + lo + i) & mask];
                const float d = a - b;
                s += (double) d * d;
                e += (double) a * a + (double) b * b;
            }
            vals[i] = e > 1.0e-20 ? s / e : 1.0;
            if (vals[i] < vals[bestIdx]) bestIdx = i;
        }
        double v = vals[bestIdx];
        if (bestIdx > 0 && bestIdx < count - 1)
        {
            const double a = vals[bestIdx - 1], c = vals[bestIdx + 1];
            const double den = a - 2.0 * v + c;
            if (den > 1.0e-12)
                v = std::clamp (v - (a - c) * (a - c) / (8.0 * den), 0.0, v);
        }
        return v;
    }

    int localMin (int around) const noexcept
    {
        if (around < tauMin || around >= tauMax - 1)
            return -1;
        int best = around;
        for (int t = std::max (tauMin, around - 2); t <= std::min (tauMax - 2, around + 2); ++t)
            if (cmnd[(size_t) t] < cmnd[(size_t) best])
                best = t;
        return best;
    }

    /** Narrow full-rate search around the coarse period; returns a sub-sample period.
        endIndex = one past the newest full-rate sample; centreIndex = the middle of the audio the
        coarse (YIN) estimate describes: the compared stretch is centred there. */
    double refine (double coarse, const float* x, int mask, int64_t endIndex, int64_t centreIndex) const noexcept
    {
        if (decim <= 1 || x == nullptr)
            return coarse;

        // The stretch compared here is centred on the YIN frame, so the refined period describes the
        // same audio as the coarse one, the voicing and the time stamp. While the pitch moves (vibrato,
        // slides) the minimum can still lie just outside +-(decim + 1) of the coarse value (that is
        // interpolated on the decimated grid): a minimum on the edge moves the search window downhill,
        // so it stays in the same dip (a wider search found other dips), at most 8 % of the period away
        // from the coarse estimate; beyond that the coarse value is kept.
        const int w = std::clamp ((int) (1.5 * coarse), 256, 2048);
        const int radius = decim + 1;
        const double maxDrift = std::max ((double) radius, 0.08 * coarse);
        double centre = coarse;
        for (int step = 0; step < 4; ++step)
        {
            const int lo = std::max (2, (int) std::floor (centre) - radius);
            const int hi = (int) std::ceil (centre) + radius;
            // centred on the analysed audio; near the bottom of the range the stretch can be longer than
            // twice the frame delay: then it ends at the newest sample (a little older than the centre)
            const int64_t base = std::min (centreIndex - (int64_t) ((w + hi) / 2), endIndex - w - hi);
            if (base < 0)
                return coarse;

            double vals[kRefineLags];
            const int count = std::min (kRefineLags, hi - lo + 1);
            int bestIdx = -1;
            double bestVal = 1.0e30;
            for (int i = 0; i < count; ++i)
            {
                const int lag = lo + i;
                double s = 0.0;
                for (int j = 0; j < w; ++j)
                {
                    const float d = x[(base + j) & mask] - x[(base + j + lag) & mask];
                    s += (double) d * d;
                }
                vals[i] = s;
                if (s < bestVal) { bestVal = s; bestIdx = i; }
            }

            if (bestIdx <= 0 || bestIdx >= count - 1)
            {
                // still sloping down at the edge: follow it, within the allowed drift
                centre += bestIdx <= 0 ? -radius : radius;
                if (std::abs (centre - coarse) > maxDrift)
                    return coarse;
                continue;
            }

            const double a = vals[bestIdx - 1], b = vals[bestIdx], c = vals[bestIdx + 1];
            const double den = a - 2.0 * b + c;
            double p = lo + bestIdx;
            if (den > 1.0e-18)
                p += std::clamp (0.5 * (a - c) / den, -0.5, 0.5);
            return p;
        }
        return coarse;
    }

    double fullRate = 44100.0, decRate = 11025.0;
    int decim = 4;
    int tauMin = 8, tauMax = 150;
    std::vector<float> dec, frame, diff, cmnd;
    int64_t decCount = 0;
    bool wasVoiced = false;
    double lastPeriod = 0.0;
    bool shortLagCheck = false;    // setRange(): the short-lag check of analyse() is used in this range
    float voicedRms = 0.0f;             // smoothed rms of the recent voiced frames (~50 hops)
    int voicedRun = 0;                  // voiced hops in a row
};
//==============================================================================
/** Cleans up the raw detector output before it drives the correction.

    Real vocals (and vocal stems from a separator) give short, wrong estimates at
    harmonic ratios: an octave, an octave + fifth, a fifth, mostly 5-20 ms long and
    mostly at the start / end of a sung phrase. In a hard-tune setting each of those
    becomes an audible "blip" to a wrong note. Rules:
      - a phrase only starts after 3 agreeing estimates (~9 ms),
      - a jump of more than 4.5 semitones is only accepted once it has lasted ~20 ms
        (a real leap); shorter jumps are ignored and the last good pitch is held,
      - a 1-2 hop unvoiced dropout inside a phrase is bridged. */
class PitchTracker
{
public:
    void reset() noexcept
    {
        voiced = false;
        period = 0.0;
        onsetCount = pendingCount = gapCount = 0;
    }

    PitchDetector::Result process (PitchDetector::Result r) noexcept
    {
        if (! r.voiced || r.period <= 1.0)
        {
            onsetCount = 0;
            pendingCount = 0;
            if (voiced && ++gapCount <= kMaxGap)
            {
                r.voiced = true;               // bridge a tiny dropout
                r.period = period;
                return r;
            }
            voiced = false;
            r.voiced = false;
            return r;
        }
        gapCount = 0;

        if (! voiced)
        {
            if (onsetCount > 0 && semitones (onsetPeriod, r.period) < 1.5)
                ++onsetCount;
            else
                onsetCount = 1;
            onsetPeriod = r.period;

            if (onsetCount < kOnsetHops)
            {
                r.voiced = false;
                return r;
            }
            voiced = true;
            period = r.period;
            pendingCount = 0;
            return r;
        }

        if (semitones (period, r.period) <= kMaxStep)
        {
            period = r.period;
            pendingCount = 0;
            return r;
        }

        // a big jump: accept only if it persists
        if (pendingCount > 0 && semitones (pendingPeriod, r.period) < 1.5)
            ++pendingCount;
        else
            pendingCount = 1;
        pendingPeriod = r.period;

        if (pendingCount >= kLeapHops)
        {
            period = r.period;
            pendingCount = 0;
            return r;
        }

        r.period = period;                      // hold the last trusted pitch
        return r;
    }

private:
    static double semitones (double a, double b) noexcept { return std::abs (12.0 * std::log2 (a / b)); }

    static constexpr int kOnsetHops = 3;
    static constexpr int kLeapHops = 7;
    static constexpr int kMaxGap = 2;
    static constexpr double kMaxStep = 4.5;

    bool voiced = false;
    double period = 0.0, onsetPeriod = 0.0, pendingPeriod = 0.0;
    int onsetCount = 0, pendingCount = 0, gapCount = 0;
};

} // namespace vstm
