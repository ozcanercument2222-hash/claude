//==============================================================================
//  SCCSaturationProcessor.h  -  Doyurucu (JUCE'dan bagimsiz)
//
//  Harmonik uretecinin aksine SEVIYEYE BAGLIDIR: sessiz kisimlar temiz kalir,
//  yuksek seviyeler egriye girdikce bozulur (klasik doygunluk).
//
//  Dort kontrol, dort ayri is:
//    SATURATION : egrinin bukulmeye basladigi seviye (dogrusal bolge T = 1 - s)
//    DRIVE      : sinyalin egriye ne kadar itildigi (giris kazanci, cikis telafili)
//    SOFT/HARD  : bukulmenin sekli (yumusak diz  <->  sert kirpma)
//    MIX        : kuru / islak karisim
//
//  Kucuk sinyal kazanci her ayarda 1'dir (egri d ile itilir, 1/d ile geri cekilir).
//  Anti-aliasing: oversampling + ADAA (SCCShaper.h).
//==============================================================================
#ifndef SCC_SATURATION_PROCESSOR_H_INCLUDED
#define SCC_SATURATION_PROCESSOR_H_INCLUDED

#include "SCCShaper.h"

namespace scc
{

struct SaturationParams
{
    double      drive = 1.0;     // lineer giris kazanci (>= 1)
    double      mix   = 0.0;     // 0 .. 1
    ShaperShape shape;           // T (dogrusal bolge), h (sertlik), alpha (asimetri)
};

// Statik (belleksiz) egri: arayuzdeki egri gostergesi ve cikis telafisi icin
inline double saturateStatic (double v, const SaturationParams& p) noexcept
{
    const double s = shaper::S (v * p.drive, p.shape) / p.drive;
    return v + p.mix * (s - v);
}

class SaturationProcessor
{
public:
    void reset() noexcept { sh[0].reset(); sh[1].reset(); }

    inline double process (int ch, double v, const SaturationParams& p) noexcept
    {
        const double s = sh[ch].process (v * p.drive, p.shape) / p.drive;
        return v + p.mix * (s - v);
    }

private:
    AdaaShaper sh[2];
};

} // namespace scc

#endif // SCC_SATURATION_PROCESSOR_H_INCLUDED
