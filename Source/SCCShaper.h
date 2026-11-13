//==============================================================================
//  SCCShaper.h  -  Dalga sekillendirici cekirdegi (JUCE'dan bagimsiz)
//
//  Tek bir parametrik egri ailesi; butun karakter tipleri bunu farkli sekilde,
//  farkli filtre / zarf yapilari icinde kullanir:
//
//      T      : dogrusal bolge. |v| <= T iken cikis = giris (hic bozulma yok).
//               T = 0  -> egri sifirdan itibaren bukulur,  T -> 1 -> sadece tepeler
//      h      : sertlik. 0 = yumusak (v / sqrt(1 + v^2)),  1 = sert kirpma
//      alpha  : negatif yarim dalganin olcegi. 1 = simetrik (tek harmonikler),
//               < 1 = negatif taraf daha gec doyar (asimetri -> cift harmonikler)
//
//  Tavan pozitif tarafta 1.0, negatif tarafta 1 / alpha.
//
//  Anti-aliasing: oversampling'e ek olarak 1. derece ADAA (antiderivative
//  anti-aliasing). ADAA yalnizca egrinin DOGRUSAL OLMAYAN KALINTISINA
//  ( S(v) - v ) uygulanir; dogrusal kisim aynen gecer. Boylece dogrusal
//  bolgede sinyal bit duzeyinde degismez ve ADAA'nin tiz kaybi olusmaz.
//==============================================================================
#ifndef SCC_SHAPER_H_INCLUDED
#define SCC_SHAPER_H_INCLUDED

#include <algorithm>
#include <cmath>

namespace scc
{

struct ShaperShape
{
    double T     = 0.0;     // 0 .. 0.98
    double h     = 0.0;     // 0 .. 1
    double alpha = 1.0;     // 0.3 .. 1
};

namespace shaper
{
    // --- bukulen kisim: k(w), w >= 0;  k(0) = 0, k'(0) = 1, k(inf) = 1
    inline double k (double w, double h) noexcept
    {
        return (1.0 - h) * w / std::sqrt (1.0 + w * w) + h * std::min (w, 1.0);
    }

    // integral: kI(w) = int_0^w k
    inline double kI (double w, double h) noexcept
    {
        return (1.0 - h) * (std::sqrt (1.0 + w * w) - 1.0)
             + h * (w <= 1.0 ? 0.5 * w * w : w - 0.5);
    }

    // turev
    inline double kD (double w, double h) noexcept
    {
        const double q = 1.0 + w * w;
        return (1.0 - h) / (q * std::sqrt (q)) + (w < 1.0 ? h : 0.0);
    }

    // --- pozitif taraf (v >= 0): dogrusal bolge + bukulen kisim
    inline double pos (double v, const ShaperShape& s) noexcept
    {
        if (v <= s.T) return v;
        const double r = 1.0 - s.T;
        return s.T + r * k ((v - s.T) / r, s.h);
    }

    inline double posI (double v, const ShaperShape& s) noexcept
    {
        if (v <= s.T) return 0.5 * v * v;
        const double r = 1.0 - s.T;
        return 0.5 * s.T * s.T + s.T * (v - s.T) + r * r * kI ((v - s.T) / r, s.h);
    }

    inline double posD (double v, const ShaperShape& s) noexcept
    {
        if (v <= s.T) return 1.0;
        return kD ((v - s.T) / (1.0 - s.T), s.h);
    }

    // --- tam egri S(v), integrali SI(v) ve turevi SD(v)
    inline double S (double v, const ShaperShape& s) noexcept
    {
        return v >= 0.0 ? pos (v, s) : -pos (-v * s.alpha, s) / s.alpha;
    }

    inline double SI (double v, const ShaperShape& s) noexcept
    {
        return v >= 0.0 ? posI (v, s) : posI (-v * s.alpha, s) / (s.alpha * s.alpha);
    }

    inline double SD (double v, const ShaperShape& s) noexcept
    {
        return v >= 0.0 ? posD (v, s) : posD (-v * s.alpha, s);
    }
}

//------------------------------------------------------------------------------
//  ADAA'li sekillendirici (bir kanal). Sekil parametreleri ornekten ornege
//  degisebilir: her iki integral de O ANKI sekille hesaplanir (zipper / patlama olmaz).
//------------------------------------------------------------------------------
struct AdaaShaper
{
    double v1 = 0.0;

    inline double process (double v, const ShaperShape& s) noexcept
    {
        const double d = v - v1;
        double y;

        if (std::abs (d) > 1.0e-5)
        {
            const double r1 = shaper::SI (v,  s) - 0.5 * v  * v;
            const double r0 = shaper::SI (v1, s) - 0.5 * v1 * v1;
            y = v + (r1 - r0) / d;
        }
        else
        {
            const double m = 0.5 * (v + v1);
            y = v + (shaper::S (m, s) - m);
        }

        v1 = v;
        return y;
    }

    void reset() noexcept { v1 = 0.0; }
};

} // namespace scc

#endif // SCC_SHAPER_H_INCLUDED
