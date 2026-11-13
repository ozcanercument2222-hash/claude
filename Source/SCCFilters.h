//==============================================================================
//  SCCFilters.h  -  Character Engine'in kullandigi kucuk filtre parcalari
//                   (JUCE'dan bagimsiz, hepsi ornek basina, bellek ayirmaz)
//==============================================================================
#ifndef SCC_FILTERS_H_INCLUDED
#define SCC_FILTERS_H_INCLUDED

#include <algorithm>
#include <cmath>

#if defined (__SSE__) || defined (_M_X64) || (defined (_M_IX86_FP) && _M_IX86_FP >= 1)
 #include <xmmintrin.h>
 #define SCC_HAS_SSE_CSR 1
#endif

namespace scc
{

constexpr double kTwoPi = 6.28318530717958647692;

// tek kutuplu filtre katsayisi:  z += a * (x - z)
inline double onePoleCoef (double cutoffHz, double sampleRate) noexcept
{
    return 1.0 - std::exp (-kTwoPi * cutoffHz / sampleRate);
}

// zaman sabitinden (ms) yumusatma katsayisi:  z += a * (x - z)
inline double smoothCoef (double timeMs, double sampleRate) noexcept
{
    return 1.0 - std::exp (-1.0 / (0.001 * timeMs * sampleRate));
}

inline double dbToLin (double db) noexcept { return std::exp (db * 0.11512925464970229); }
inline double linToDb (double lin) noexcept { return 8.685889638065035 * std::log (std::max (lin, 1.0e-12)); }

//------------------------------------------------------------------------------
//  Denormal korumasi: islemci "flush-to-zero" kipine alinir, cikista geri yuklenir.
//  (Plugin'de juce::ScopedNoDenormals de var; cekirdek JUCE'suz kullanildiginda bu yeter.)
//------------------------------------------------------------------------------
class ScopedFlushDenormals
{
public:
    ScopedFlushDenormals() noexcept
    {
       #if SCC_HAS_SSE_CSR
        saved = _mm_getcsr();
        _mm_setcsr (saved | 0x8040u);                 // FTZ | DAZ
       #elif defined (__aarch64__)
        __asm__ __volatile__ ("mrs %0, fpcr" : "=r" (saved));
        const unsigned long long v = saved | (1ull << 24);      // FZ
        __asm__ __volatile__ ("msr fpcr, %0" : : "r" (v));
       #endif
    }

    ~ScopedFlushDenormals() noexcept
    {
       #if SCC_HAS_SSE_CSR
        _mm_setcsr (saved);
       #elif defined (__aarch64__)
        __asm__ __volatile__ ("msr fpcr, %0" : : "r" (saved));
       #endif
    }

    ScopedFlushDenormals (const ScopedFlushDenormals&) = delete;
    ScopedFlushDenormals& operator= (const ScopedFlushDenormals&) = delete;

private:
   #if SCC_HAS_SSE_CSR
    unsigned int saved = 0;
   #elif defined (__aarch64__)
    unsigned long long saved = 0;
   #endif
};

//------------------------------------------------------------------------------
struct OnePole
{
    double z = 0.0;
    inline double lp (double x, double a) noexcept { z += a * (x - z); return z; }
    inline double hp (double x, double a) noexcept { z += a * (x - z); return x - z; }
    void reset() noexcept { z = 0.0; }
};

// DC engelleyici (1. derece yuksek geciren):  y = x - x1 + r * y1
struct DcBlocker
{
    double x1 = 0.0, y1 = 0.0;
    inline double process (double x, double r) noexcept
    {
        const double y = x - x1 + r * y1;
        x1 = x;
        y1 = y;
        return y;
    }
    void reset() noexcept { x1 = y1 = 0.0; }
};

inline double dcBlockerCoef (double cutoffHz, double sampleRate) noexcept
{
    return std::exp (-kTwoPi * cutoffHz / sampleRate);
}

//------------------------------------------------------------------------------
//  Tutmali tepe zarfi: tepeyi aninda yakalar, "hold" suresi boyunca sabit tutar,
//  sonra ussel olarak birakir. Periyodu hold'dan kisa olan surekli bir ton icin
//  zarf TAM OLARAK tepe genligine esit ve sabittir (dalgalanma yok) - Chebyshev
//  tabanli harmonik uretecinin saf harmonik verebilmesi icin gereklidir.
//------------------------------------------------------------------------------
struct PeakHoldEnvelope
{
    static constexpr double kFloor = 1.0e-6;

    void prepare (double holdMs, double releaseMs, double sampleRate) noexcept
    {
        holdSamples = (int) (0.001 * holdMs * sampleRate);
        release     = std::exp (-1.0 / (0.001 * releaseMs * sampleRate));
        reset();
    }

    void reset() noexcept { env = kFloor; hold = 0; }

    inline double process (double peak) noexcept
    {
        if (peak >= env)              { env = peak; hold = holdSamples; }
        else if (peak >= env * 0.995) { hold = holdSamples; }       // tepeye yakin: tutmayi yenile
        else if (hold > 0)            { --hold; }
        else                          { env = std::max (env * release, std::max (peak, kFloor)); }
        return env;
    }

    double env = kFloor, release = 0.999;
    int    hold = 0, holdSamples = 0;
};

//------------------------------------------------------------------------------
//  Tersinir "vurgu" (emphasis) ciftleri.
//
//  pre()  : dogrusal olmayan kattan ONCE bir bandi yukseltir / alcaltir
//  post() : sonra TAM tersini uygular
//
//  Arada dogrusal olmayan bir kat yoksa post(pre(x)) == x (sayisal hassasiyetle birebir).
//  Yani bu ciftler ton (EQ) degistirmez; yalnizca satürasyonun HANGI frekanslarda
//  olusacagini belirler ("frekansa bagli satürasyon").
//------------------------------------------------------------------------------
struct LowEmphasis                    // H(z) = 1 + k * LP(z)
{
    double lpPre = 0.0, lpPost = 0.0;

    inline double pre (double x, double k, double a) noexcept
    {
        lpPre += a * (x - lpPre);
        return x + k * lpPre;
    }

    inline double post (double u, double k, double a) noexcept
    {
        const double x = (u - k * (1.0 - a) * lpPost) / (1.0 + k * a);
        lpPost += a * (x - lpPost);
        return x;
    }

    void reset() noexcept { lpPre = lpPost = 0.0; }
};

struct HighEmphasis                   // H(z) = 1 + k * (1 - LP(z))
{
    double lpPre = 0.0, lpPost = 0.0;

    inline double pre (double x, double k, double a) noexcept
    {
        lpPre += a * (x - lpPre);
        return x + k * (x - lpPre);
    }

    inline double post (double u, double k, double a) noexcept
    {
        const double x = (u + k * (1.0 - a) * lpPost) / (1.0 + k * (1.0 - a));
        lpPost += a * (x - lpPost);
        return x;
    }

    void reset() noexcept { lpPre = lpPost = 0.0; }
};

//------------------------------------------------------------------------------
//  Sabit katsayili bant geciren (RBJ, tepe kazanci 0 dB). "x + g * BP(x)" seklinde
//  kullanilir: kazanc g yumusatilir, katsayilar hic degismez (zipper olmaz).
//------------------------------------------------------------------------------
struct BandPass
{
    double b0 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    double z1 = 0.0, z2 = 0.0;

    void design (double centreHz, double q, double sampleRate) noexcept
    {
        const double w0    = kTwoPi * centreHz / sampleRate;
        const double alpha = std::sin (w0) / (2.0 * q);
        const double a0    = 1.0 + alpha;
        b0 =  alpha / a0;
        b2 = -alpha / a0;
        a1 = -2.0 * std::cos (w0) / a0;
        a2 = (1.0 - alpha) / a0;
    }

    inline double process (double x) noexcept
    {
        const double y = b0 * x + z1;
        z1 = -a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    void reset() noexcept { z1 = z2 = 0.0; }
};

} // namespace scc

#endif // SCC_FILTERS_H_INCLUDED
