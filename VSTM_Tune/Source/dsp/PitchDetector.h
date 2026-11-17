#pragma once
//==============================================================================
//  Perde (temel frekans) algilayici
//
//  YIN (de Cheveigne & Kawahara 2002) - kaba arama, alcak-gecirgen ve seyreltilmis
//  (~11-12 kHz) sinyal uzerinde yapilir; sonuc tam ornekleme hizinda, kaba periyodun
//  cevresinde dar bir aralikta ince ayarlanir (yuksek notalarda da cent hassasiyeti).
//  Oktav atlamalarina karsi bir onceki periyotla sureklilik kontrolu yapar.
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
    }

    void reset()
    {
        std::fill (dec.begin(), dec.end(), 0.0f);
        decCount = 0;
        wasVoiced = false;
        lastPeriod = 0.0;
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

        // continuity: guard against octave jumps relative to the previous voiced period
        if (wasVoiced && lastPeriod > 0.0)
        {
            const double prevTau = lastPeriod / decim;
            const double ratio = best / prevTau;
            if (ratio > 1.8 && ratio < 2.2)
            {
                const int alt = localMin ((int) std::lround (best * 0.5));
                if (alt > 0 && cmnd[(size_t) alt] < kStayVoiced)
                    best = alt;
            }
            else if (ratio > 0.45 && ratio < 0.55)
            {
                const int alt = localMin ((int) std::lround (best * 2.0));
                if (alt > 0 && cmnd[(size_t) alt] < cmnd[(size_t) best] + 0.04f)
                    best = alt;
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
        r.voiced = loudEnough && minVal < (wasVoiced ? kStayVoiced : kBecomeVoiced);
        r.period = refine (tauF * decim, fullRing, fullMask, endIndex);
        r.centreDelay = (double) (kWindow + tauMax) * decim - (kWindow + tauF) * decim * 0.5;

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
    static constexpr float kGate = 0.0015f;       // about -56 dBFS

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

    /** Narrow full-rate search around the coarse period; returns a sub-sample period. */
    double refine (double coarse, const float* x, int mask, int64_t endIndex) const noexcept
    {
        if (decim <= 1 || x == nullptr)
            return coarse;

        const int lo = std::max (2, (int) std::floor (coarse) - decim - 1);
        const int hi = (int) std::ceil (coarse) + decim + 1;
        const int w = std::clamp ((int) (1.5 * coarse), 256, 2048);
        const int64_t base = endIndex - w - hi;
        if (base < 0)
            return coarse;

        double vals[64];
        const int count = std::min (64, hi - lo + 1);
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
            return coarse;

        const double a = vals[bestIdx - 1], b = vals[bestIdx], c = vals[bestIdx + 1];
        const double den = a - 2.0 * b + c;
        double p = lo + bestIdx;
        if (den > 1.0e-18)
            p += std::clamp (0.5 * (a - c) / den, -0.5, 0.5);
        return p;
    }

    double fullRate = 44100.0, decRate = 11025.0;
    int decim = 4;
    int tauMin = 8, tauMax = 150;
    std::vector<float> dec, frame, diff, cmnd;
    int64_t decCount = 0;
    bool wasVoiced = false;
    double lastPeriod = 0.0;
};
} // namespace vstm
