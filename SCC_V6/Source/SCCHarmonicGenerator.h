//==============================================================================
//  SCCHarmonicGenerator.h  -  Harmonik ureteci (JUCE'dan bagimsiz)
//
//  Doyurucudan FARKLI calisir: seviyeden bagimsizdir.
//    * Sinyal, stereo bagli bir tepe zarfina bolunerek [-1, 1] araligina getirilir.
//    * Chebyshev polinomlari T2..T5 uygulanir: birim genlikli bir sinus icin
//      Tn TAM OLARAK n. harmonigi uretir. Sonuc zarfla geri carpilir.
//    * Boylece 2./3./4./5. harmoniklerin orani sesin yuksekligiyle degismez ve
//      EVEN / ODD / BALANCE kontrolleri harmonikleri dogrudan, ayri ayri surer.
//
//  En yuksek derece 5 oldugu icin 4x oversampling'de uretilen bilesenlerin hicbiri
//  duyulabilir banda katlanmaz (polinom bant sinirlidir -> aliasing yok).
//
//  ThicknessGenerator ayni fikri yalnizca ALT BANDA uygular (govde kalinligi).
//==============================================================================
#ifndef SCC_HARMONIC_GENERATOR_H_INCLUDED
#define SCC_HARMONIC_GENERATOR_H_INCLUDED

#include "SCCFilters.h"

namespace scc
{

namespace cheby
{
    inline double t2 (double w) noexcept { return 2.0 * w * w - 1.0; }
    inline double t3 (double w) noexcept { return (4.0 * w * w - 3.0) * w; }
    inline double t4 (double w) noexcept { const double w2 = w * w; return (8.0 * w2 - 8.0) * w2 + 1.0; }
    inline double t5 (double w) noexcept { const double w2 = w * w; return ((16.0 * w2 - 20.0) * w2 + 5.0) * w; }
}

struct HarmonicParams
{
    double even    = 0.0;     // cift harmoniklerin kazanci (temele gore, lineer)
    double odd     = 0.0;     // tek harmoniklerin kazanci
    double balance = 0.0;     // 0 = 2./3. harmonik ... 1 = 4./5. harmonik
};

//------------------------------------------------------------------------------
//  Statik (belleksiz) kisim: verilen zarf icin uretilen harmonik sinyali.
//  Hem ses isleme hem de arayuzdeki egri / harmonik gostergesi bunu kullanir.
//------------------------------------------------------------------------------
inline void harmonicsFor (double u, double env, const HarmonicParams& p, double& evenOut, double& oddOut) noexcept
{
    const double w = std::clamp (u / env, -1.0, 1.0);
    const double b = p.balance;

    // isaretler dogal doygunlukla ayni: 3. harmonik tepeleri yassiltir
    evenOut = env * p.even * ((1.0 - b) * cheby::t2 (w) - b * cheby::t4 (w));
    oddOut  = env * p.odd  * (b * cheby::t5 (w) - (1.0 - b) * cheby::t3 (w));
}

//------------------------------------------------------------------------------
class HarmonicGenerator
{
public:
    void prepare (double sampleRate) noexcept
    {
        env.prepare (8.0, 80.0, sampleRate);                      // 8 ms tut, 80 ms birak
        hpCoef = onePoleCoef (20.0, sampleRate);                  // cift harmoniklerin DC bileseni icin
        reset();
    }

    void reset() noexcept
    {
        env.reset();
        hp[0].reset();
        hp[1].reset();
    }

    // stereo bagli zarf: her karede bir kez, iki kanalin en buyugu ile
    inline double updateEnvelope (double peak) noexcept { return env.process (peak); }

    // bir kanal icin eklenecek harmonik sinyal
    inline double process (int ch, double u, const HarmonicParams& p) noexcept
    {
        double e, o;
        harmonicsFor (u, env.env, p, e, o);
        return hp[ch].hp (e, hpCoef) + o;
    }

    double envelope() const noexcept { return env.env; }

private:
    PeakHoldEnvelope env;
    double  hpCoef = 0.001;
    OnePole hp[2];
};

//------------------------------------------------------------------------------
//  Alt bant harmonikleri: ~200 Hz altindaki icerigin 2. ve 3. harmonigi
//  (govdeye / alt-ortaya eklenir). Tam bant harmonik uretecinden bagimsizdir.
//  Bant ayirici 24 dB/oktav: orta ve tiz frekanslar uretece sizmaz.
//------------------------------------------------------------------------------
class ThicknessGenerator
{
public:
    void prepare (double sampleRate) noexcept
    {
        lowCoef = onePoleCoef (330.0, sampleRate);                // 4 kutup: 100 Hz -1.5 dB, 500 Hz -21 dB, 2 kHz -63 dB
        hpCoef  = onePoleCoef (80.0, sampleRate);
        lpCoef  = onePoleCoef (1500.0, sampleRate);
        env.prepare (12.0, 100.0, sampleRate);
        reset();
    }

    void reset() noexcept
    {
        env.reset();
        for (int c = 0; c < 2; ++c)
        {
            for (auto& f : split4[c]) f.reset();
            hp[c].reset(); lp3[c].reset(); low[c] = 0.0;
        }
    }

    // 1) alt bandi ayir (kanal basina)
    inline double split (int ch, double y) noexcept
    {
        double v = y;
        for (auto& f : split4[ch]) v = f.lp (v, lowCoef);
        low[ch] = v;
        return std::abs (v);
    }

    // 2) stereo bagli zarf
    inline void updateEnvelope (double peak) noexcept { env.process (peak); }

    // 3) eklenecek sinyal (kanal basina)
    inline double process (int ch) noexcept
    {
        const double e = env.env;
        const double w = std::clamp (low[ch] / e, -1.0, 1.0);
        const double h = e * (0.8 * cheby::t2 (w) - 0.6 * cheby::t3 (w));
        return lp3[ch].lp (hp[ch].hp (h, hpCoef), lpCoef);
    }

private:
    PeakHoldEnvelope env;
    double  lowCoef = 0.01, hpCoef = 0.003, lpCoef = 0.05;
    double  low[2] = { 0.0, 0.0 };
    OnePole split4[2][4], hp[2], lp3[2];
};

} // namespace scc

#endif // SCC_HARMONIC_GENERATOR_H_INCLUDED
