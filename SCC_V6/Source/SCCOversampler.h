//==============================================================================
//  SCCOversampler.h  -  Character Engine icin oversampling (JUCE'dan bagimsiz)
//
//  Minimum fazli, polifaz IIR "halfband" filtreler (iki allpass kolu).
//    * Gecikme bildirmez: plugin canli kayitta da kullanilabilir (V5 ile ayni).
//    * 2x veya 4x (iki kademe). 4x'te:
//        kademe 1 (1x <-> 2x) : dar gecis bandi, yuksek bastirma
//        kademe 2 (2x <-> 4x) : genis gecis bandi, az katsayi (ucuz)
//    * Katsayilar eliptik halfband tasarimiyla calisma aninda hesaplanir
//      (prepare() icinde; ses is parcaciginda tasarim yapilmaz).
//
//  Butun karakter isleme (kuru/islak karisim dahil) yuksek hizda yapilir, boylece
//  filtrelerin faz kaymasi karisimda tarak filtre etkisi olusturmaz.
//==============================================================================
#ifndef SCC_OVERSAMPLER_H_INCLUDED
#define SCC_OVERSAMPLER_H_INCLUDED

#include <algorithm>
#include <cmath>

namespace scc
{

//------------------------------------------------------------------------------
//  Polifaz halfband katsayi tasarimi (eliptik, iki allpass kolu)
//    numCoefs   : toplam allpass katsayisi (iki kola paylastirilir)
//    transition : gecis bandi genisligi, YUKSEK ornekleme hizina gore (0 .. 0.5)
//                 gecis bandi fs/4 cevresinde ortalanir
//------------------------------------------------------------------------------
namespace halfband
{
    constexpr double kPi = 3.14159265358979323846;

    inline double ipow (double x, int n) noexcept
    {
        double r = 1.0;
        while (n > 0) { if (n & 1) r *= x; x *= x; n >>= 1; }
        return r;
    }

    inline void transitionParam (double& k, double& q, double transition) noexcept
    {
        k  = std::tan ((1.0 - transition * 2.0) * kPi / 4.0);
        k *= k;
        const double kksqrt = std::pow (1.0 - k * k, 0.25);
        const double e  = 0.5 * (1.0 - kksqrt) / (1.0 + kksqrt);
        const double e2 = e * e;
        const double e4 = e2 * e2;
        q = e * (1.0 + e4 * (2.0 + e4 * (15.0 + 150.0 * e4)));
    }

    inline double accNum (double q, int order, int c) noexcept
    {
        int i = 0, j = 1;
        double acc = 0.0, term;
        do
        {
            term  = ipow (q, i * (i + 1));
            term *= std::sin ((double) (i * 2 + 1) * (double) c * kPi / (double) order) * (double) j;
            acc  += term;
            j = -j;
            ++i;
        }
        while (std::abs (term) > 1.0e-100 && i < 64);
        return acc;
    }

    inline double accDen (double q, int order, int c) noexcept
    {
        int i = 1, j = -1;
        double acc = 0.0, term;
        do
        {
            term  = ipow (q, i * i);
            term *= std::cos ((double) (i * 2) * (double) c * kPi / (double) order) * (double) j;
            acc  += term;
            j = -j;
            ++i;
        }
        while (std::abs (term) > 1.0e-100 && i < 64);
        return acc;
    }

    inline void design (double* coefs, int numCoefs, double transition) noexcept
    {
        double k, q;
        transitionParam (k, q, transition);
        const int order = numCoefs * 2 + 1;

        for (int index = 0; index < numCoefs; ++index)
        {
            const int    c    = index + 1;
            const double num  = accNum (q, order, c) * std::pow (q, 0.25);
            const double den  = accDen (q, order, c) + 0.5;
            const double ww   = num / den;
            const double wwsq = ww * ww;
            const double x    = std::sqrt ((1.0 - wwsq * k) * (1.0 - wwsq / k)) / (1.0 + wwsq);
            coefs[index] = (1.0 - x) / (1.0 + x);
        }
    }

    // Tasarimin kuramsal durdurma bandi bastirmasi (dB, genlik)
    inline double attenuationDb (int numCoefs, double transition) noexcept
    {
        double k, q;
        transitionParam (k, q, transition);
        const int order = numCoefs * 2 + 1;
        return -5.0 * std::log10 (4.0 * ipow (q, order));
    }
}

//------------------------------------------------------------------------------
//  Tek halfband kademesi (bir kanal): 2x yukari ve 2x asagi ornekleme
//------------------------------------------------------------------------------
class HalfbandStage
{
public:
    static constexpr int kMaxCoefs = 16;

    void design (int numCoefficients, double transition) noexcept
    {
        numCoefs = std::clamp (numCoefficients, 2, kMaxCoefs);
        halfband::design (coef, numCoefs, transition);
        reset();
    }

    void reset() noexcept
    {
        for (int i = 0; i < kMaxCoefs; ++i)
            upX[i] = upY[i] = dnX[i] = dnY[i] = 0.0;
    }

    // bir giris ornegi -> iki cikis ornegi
    inline void up (double in, double& out0, double& out1) noexcept
    {
        double even = in, odd = in;
        for (int i = 0; i + 1 < numCoefs; i += 2)
        {
            const double t0 = (even - upY[i]) * coef[i] + upX[i];
            upX[i] = even;  upY[i] = t0;  even = t0;

            const double t1 = (odd - upY[i + 1]) * coef[i + 1] + upX[i + 1];
            upX[i + 1] = odd;  upY[i + 1] = t1;  odd = t1;
        }
        if (numCoefs & 1)
        {
            const int i = numCoefs - 1;
            const double t0 = (even - upY[i]) * coef[i] + upX[i];
            upX[i] = even;  upY[i] = t0;  even = t0;
        }
        out0 = even;
        out1 = odd;
    }

    // iki giris ornegi -> bir cikis ornegi
    inline double down (double in0, double in1) noexcept
    {
        double even = in1, odd = in0;
        for (int i = 0; i + 1 < numCoefs; i += 2)
        {
            const double t0 = (even - dnY[i]) * coef[i] + dnX[i];
            dnX[i] = even;  dnY[i] = t0;  even = t0;

            const double t1 = (odd - dnY[i + 1]) * coef[i + 1] + dnX[i + 1];
            dnX[i + 1] = odd;  dnY[i + 1] = t1;  odd = t1;
        }
        if (numCoefs & 1)
        {
            const int i = numCoefs - 1;
            const double t0 = (even - dnY[i]) * coef[i] + dnX[i];
            dnX[i] = even;  dnY[i] = t0;  even = t0;
        }
        return 0.5 * (even + odd);
    }

private:
    int    numCoefs = 2;
    double coef[kMaxCoefs] = {};
    double upX[kMaxCoefs] = {}, upY[kMaxCoefs] = {};
    double dnX[kMaxCoefs] = {}, dnY[kMaxCoefs] = {};
};

//------------------------------------------------------------------------------
//  1x / 2x / 4x oversampler, 2 kanala kadar
//------------------------------------------------------------------------------
class Oversampler
{
public:
    static constexpr int kMaxFactor = 4;

    // Kademe tasarimlari (olculen bastirma: her iki kademe de > 100 dB)
    static constexpr int    kStage1Coefs      = 8;
    static constexpr double kStage1Transition = 0.045;    // 44.1 kHz'de gecis: 20.07 .. 24.03 kHz
    static constexpr int    kStage2Coefs      = 4;
    static constexpr double kStage2Transition = 0.22;

    // Taban ornekleme hizina gore uygun kat sayisi: ic hiz en az ~176 kHz olsun
    static int factorForSampleRate (double sampleRate) noexcept
    {
        return sampleRate < 70000.0 ? 4 : 2;
    }

    void prepare (int oversamplingFactor) noexcept
    {
        fac = (oversamplingFactor >= 4) ? 4 : (oversamplingFactor >= 2 ? 2 : 1);
        for (int c = 0; c < 2; ++c)
        {
            s1[c].design (kStage1Coefs, kStage1Transition);
            s2[c].design (kStage2Coefs, kStage2Transition);
        }
    }

    void reset() noexcept
    {
        for (int c = 0; c < 2; ++c) { s1[c].reset(); s2[c].reset(); }
    }

    int factor() const noexcept { return fac; }

    // in: n ornek (taban hiz)  ->  out: n * factor ornek
    void upsample (int ch, const float* in, int n, double* out) noexcept
    {
        HalfbandStage& a = s1[ch];
        HalfbandStage& b = s2[ch];

        if (fac == 4)
        {
            for (int i = 0; i < n; ++i)
            {
                double m0, m1;
                a.up ((double) in[i], m0, m1);
                b.up (m0, out[4 * i],     out[4 * i + 1]);
                b.up (m1, out[4 * i + 2], out[4 * i + 3]);
            }
        }
        else if (fac == 2)
        {
            for (int i = 0; i < n; ++i)
                a.up ((double) in[i], out[2 * i], out[2 * i + 1]);
        }
        else
        {
            for (int i = 0; i < n; ++i)
                out[i] = (double) in[i];
        }
    }

    // in: n * factor ornek  ->  out: n ornek (taban hiz)
    void downsample (int ch, const double* in, int n, float* out) noexcept
    {
        HalfbandStage& a = s1[ch];
        HalfbandStage& b = s2[ch];

        if (fac == 4)
        {
            for (int i = 0; i < n; ++i)
            {
                const double m0 = b.down (in[4 * i],     in[4 * i + 1]);
                const double m1 = b.down (in[4 * i + 2], in[4 * i + 3]);
                out[i] = (float) a.down (m0, m1);
            }
        }
        else if (fac == 2)
        {
            for (int i = 0; i < n; ++i)
                out[i] = (float) a.down (in[2 * i], in[2 * i + 1]);
        }
        else
        {
            for (int i = 0; i < n; ++i)
                out[i] = (float) in[i];
        }
    }

private:
    int fac = 4;
    HalfbandStage s1[2], s2[2];
};

} // namespace scc

#endif // SCC_OVERSAMPLER_H_INCLUDED
