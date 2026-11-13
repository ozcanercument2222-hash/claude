//==============================================================================
//  SCCTransientProcessor.h  -  Vurus (transient) ve yogunluk (density) isleme
//                              (JUCE'dan bagimsiz)
//
//  TransientProcessor
//    TRANSIENT       : hizli ve yavas iki zarfin farki "vurus" olcusudur. Pozitif
//                      deger vuruslari one cikarir, negatif deger yumusatir.
//                      Seviyeden bagimsizdir (zarflarin ORANI kullanilir).
//    ATTACK CHARACTER: yavas zarfin attack suresi = vurus penceresinin uzunlugu.
//                      Kisa -> yalnizca ilk "tik",  uzun -> vurusun govdesi de dahil.
//    ROUNDING        : tavani yavas zarfi izleyen bir yumusak doyurucu. Surekli
//                      seviyeye dokunmaz; yalnizca zarfin ustune cikan vurus tepesini
//                      yuvarlar (kazanc azaltmak yerine tepeyi DOYURUR -> harmonik uretir).
//
//  DensityProcessor
//    DENSITY         : yukari kompresyon. Sesin kendi uzun vadeli seviyesinin altinda
//                      kalan kisimlar yukari tasinir (bosluklar dolar, ses "sikilasir").
//                      Programin cok altindaki icerik (nefes / dip gurultusu) yukseltilmez.
//
//  Kazanclar taban ornekleme hizinda hesaplanir (zarflar yavastir, oversampling
//  gerekmez); yuvarlama doyurucusu yuksek hizda calisir.
//==============================================================================
#ifndef SCC_TRANSIENT_PROCESSOR_H_INCLUDED
#define SCC_TRANSIENT_PROCESSOR_H_INCLUDED

#include "SCCFilters.h"
#include "SCCShaper.h"

namespace scc
{

struct TransientParams
{
    double amount       = 0.0;     // -1 .. +1
    double windowMs     = 8.0;     // 1.5 .. 40 ms
    double roundHeadroom = 6.0;    // tavan = 2 * yavas zarf * (1 + headroom)
};

//------------------------------------------------------------------------------
class TransientProcessor
{
public:
    static constexpr double kMaxGainDb = 12.0;

    void prepare (double baseSampleRate) noexcept
    {
        fs       = baseSampleRate;
        fastAtk  = smoothCoef (0.15, fs);
        release  = smoothCoef (60.0, fs);
        gainSm   = smoothCoef (0.4, fs);
        reset();
    }

    void reset() noexcept
    {
        envFast = envSlow = 0.0;
        gainDb = 0.0;
        lastWindowMs = -1.0;
    }

    // Vurus penceresini 0..1 kontrolunden hesaplar (1.5 ms .. 40 ms, logaritmik)
    static double windowMsFor (double attackChar01) noexcept
    {
        return 1.5 * std::pow (40.0 / 1.5, std::clamp (attackChar01, 0.0, 1.0));
    }

    // Yuvarlama tavaninin zarfa gore payi: 0 -> cok yuksek (etkisiz), 1 -> zarfin hemen ustu
    static double headroomFor (double rounding01) noexcept
    {
        const double r = 1.0 - std::clamp (rounding01, 0.0, 1.0);
        return 6.0 * r * r + 0.05;
    }

    //--------------------------------------------------------------------------
    //  Taban hizda, kare basina bir kez. peak = iki kanalin en buyuk mutlak degeri.
    //  Donus: vurus kazanci (dB). ceilingOut: yuvarlama doyurucusunun tavani (lineer).
    //--------------------------------------------------------------------------
    inline double tick (double peak, const TransientParams& p, double& ceilingOut) noexcept
    {
        if (std::abs (p.windowMs - lastWindowMs) > 1.0e-9)
        {
            lastWindowMs = p.windowMs;
            slowAtk = smoothCoef (p.windowMs, fs);
        }

        envFast += (peak > envFast ? fastAtk : release) * (peak - envFast);
        envSlow += (peak > envSlow ? slowAtk : release) * (peak - envSlow);

        // Doyurucu tavanin YARISINA kadar tamamen dogrusal (TransientRounder::shape).
        // Tavan = 2 x yavas zarf x (1 + pay): surekli seviye dogrusal bolgede kalir,
        // yalnizca yavas zarfin ustune cikan vurus tepesi yuvarlanir.
        ceilingOut = 2.0 * envSlow * (1.0 + p.roundHeadroom) + kFloor;

        double target = 0.0;
        if (std::abs (p.amount) > 0.0)
        {
            // vurus olcusu (dB): hizli zarf yavas zarfin ne kadar ustunde?
            const double t = 8.685889638065035 * std::log ((envFast + kFloor) / (envSlow + kFloor));
            target = std::clamp (p.amount * 1.2 * std::clamp (t, 0.0, 15.0), -kMaxGainDb, kMaxGainDb);
        }
        gainDb += gainSm * (target - gainDb);
        return gainDb;
    }

    double slowEnvelope() const noexcept { return envSlow; }

private:
    static constexpr double kFloor = 1.0e-4;      // -80 dB: bunun altinda vurus aranmaz

    double fs = 48000.0;
    double fastAtk = 0.1, slowAtk = 0.01, release = 0.001, gainSm = 0.1;
    double envFast = 0.0, envSlow = 0.0, gainDb = 0.0;
    double lastWindowMs = -1.0;
};

//------------------------------------------------------------------------------
//  Yuvarlama doyurucusu (yuksek hizda, kanal basina). Tavan disaridan verilir.
//------------------------------------------------------------------------------
class TransientRounder
{
public:
    void reset() noexcept { sh[0].reset(); sh[1].reset(); }

    static ShaperShape shape() noexcept
    {
        ShaperShape s;
        s.T = 0.5;          // tavanin yarisina kadar tamamen dogrusal
        s.h = 0.0;
        s.alpha = 1.0;
        return s;
    }

    // statik egri (gosterge / test icin)
    static double roundStatic (double x, double ceiling) noexcept
    {
        return ceiling * shaper::S (x / ceiling, shape());
    }

    inline double process (int ch, double x, double ceiling, double invCeiling) noexcept
    {
        return ceiling * sh[ch].process (x * invCeiling, shape());
    }

private:
    AdaaShaper sh[2];
};

//------------------------------------------------------------------------------
class DensityProcessor
{
public:
    static constexpr double kMaxBoostDb = 12.0;

    void prepare (double baseSampleRate) noexcept
    {
        atk     = smoothCoef (10.0, baseSampleRate);
        rel     = smoothCoef (40.0, baseSampleRate);
        fastCo  = smoothCoef (4.0, baseSampleRate);        // kapi icin hizli seviye olcusu
        longCo  = smoothCoef (1500.0, baseSampleRate);
        gainSm  = smoothCoef (25.0, baseSampleRate);
        reset();
    }

    void reset() noexcept
    {
        msShort = msFast = 0.0;
        msLong  = 0.0;
        haveLong = false;
        boostDb = 0.0;
    }

    //--------------------------------------------------------------------------
    //  Taban hizda, kare basina bir kez. power = kanallarin ortalama karesi.
    //  amount: 0 .. 1. Donus: yukari kompresyon kazanci (dB, >= 0).
    //--------------------------------------------------------------------------
    inline double tick (double power, double amount) noexcept
    {
        msShort += (power > msShort ? atk : rel) * (power - msShort);
        msFast  += fastCo * (power - msFast);

        if (msShort > kGate)
        {
            if (! haveLong) { msLong = msShort; haveLong = true; }
            msLong += longCo * (msShort - msLong);
        }

        double target = 0.0;
        if (amount > 0.0 && haveLong)
        {
            // program seviyesinin ne kadar altindayiz? (dB, pozitif = daha sessiz)
            const double below = 4.342944819032518 * std::log ((msLong + 1.0e-12) / (msShort + 1.0e-12));
            target = std::clamp (0.5 * below, 0.0, kMaxBoostDb) * amount;

            // Kapi: programin 24 dB'den fazla altindaki icerik (nefes, dip gurultusu) yukseltilmez.
            // Hizli olcuyle karar verilir ki ses aniden kesildiginde kazanc acik kalmasin.
            const double belowFast = 4.342944819032518 * std::log ((msLong + 1.0e-12) / (msFast + 1.0e-12));
            if (belowFast > 24.0)
                target *= std::max (0.0, 1.0 - (belowFast - 24.0) / 12.0);
        }
        boostDb += gainSm * (target - boostDb);
        return boostDb;
    }

private:
    static constexpr double kGate = 1.0e-6;       // -60 dB

    double atk = 0.01, rel = 0.001, fastCo = 0.01, longCo = 0.0001, gainSm = 0.001;
    double msShort = 0.0, msFast = 0.0, msLong = 0.0, boostDb = 0.0;
    bool   haveLong = false;
};

} // namespace scc

#endif // SCC_TRANSIENT_PROCESSOR_H_INCLUDED
