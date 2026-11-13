//==============================================================================
//  SCCCharacterToneProcessor.h  -  Karakterin tonu (JUCE'dan bagimsiz)
//
//  Bu bir EQ DEGILDIR. LOW / HIGH CHARACTER, harmonik ureteci ve doyurucunun
//  ONUNDE bir bandi yukseltir (veya alcaltir), ARKASINDA tam tersini uygular:
//
//      +  : o bant dogrusal olmayan katlara daha yuksek girer -> daha cok doyar,
//           daha cok harmonik uretir (kalin baslar / yumusak, "bant" gibi tizler)
//      -  : o bant daha dusuk girer -> temiz kalir (siki bas / acik, temiz tiz)
//
//  Dogrusal olmayan katlar kapaliyken on ve arka filtre birbirini tam goturur;
//  frekans tepkisi duz kalir. Yani kontrol "ses rengini" degil, karakterin
//  HANGI FREKANSLARDA olusacagini belirler.
//==============================================================================
#ifndef SCC_CHARACTER_TONE_PROCESSOR_H_INCLUDED
#define SCC_CHARACTER_TONE_PROCESSOR_H_INCLUDED

#include "SCCFilters.h"

namespace scc
{

struct ToneParams
{
    double kLow  = 0.0;      // alt raf vurgusu:  H = 1 + kLow  * LP(200 Hz)
    double kHigh = 0.0;      // ust raf vurgusu:  H = 1 + kHigh * HP(3.5 kHz)
};

class CharacterToneProcessor
{
public:
    static constexpr double kLowHz  = 200.0;
    static constexpr double kHighHz = 3500.0;

    // -1 .. +1 kontrolunden vurgu katsayisi:  +1 -> +12 dB,  -1 -> -12 dB
    static double emphasisFor (double control) noexcept
    {
        const double c = std::clamp (control, -1.0, 1.0);
        return c >= 0.0 ? 3.0 * c : 0.75 * c;
    }

    void prepare (double sampleRate) noexcept
    {
        aLow  = onePoleCoef (kLowHz, sampleRate);
        aHigh = onePoleCoef (kHighHz, sampleRate);
        reset();
    }

    void reset() noexcept
    {
        for (int c = 0; c < 2; ++c) { lo[c].reset(); hi[c].reset(); }
    }

    inline double pre (int ch, double x, const ToneParams& p) noexcept
    {
        return hi[ch].pre (lo[ch].pre (x, p.kLow, aLow), p.kHigh, aHigh);
    }

    inline double post (int ch, double y, const ToneParams& p) noexcept
    {
        return lo[ch].post (hi[ch].post (y, p.kHigh, aHigh), p.kLow, aLow);
    }

private:
    double aLow = 0.01, aHigh = 0.1;
    LowEmphasis  lo[2];
    HighEmphasis hi[2];
};

} // namespace scc

#endif // SCC_CHARACTER_TONE_PROCESSOR_H_INCLUDED
