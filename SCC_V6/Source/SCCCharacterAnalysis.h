//==============================================================================
//  SCCCharacterAnalysis.h  -  Karakterin "harmonik imzasi" (JUCE'dan bagimsiz)
//
//  Arayuzdeki harmonik gostergesi tahmin veya sabit bir resim DEGILDIR: buradaki
//  sonda, ses yolundakiyle AYNI CharacterEngine sinifini (ayri bir ornegini)
//  bir test sinusuyle calistirir ve cikisin harmoniklerini olcer.
//
//  Ses is parcaciginda KULLANILMAZ (arayuz is parcacigi ve testler icindir).
//==============================================================================
#ifndef SCC_CHARACTER_ANALYSIS_H_INCLUDED
#define SCC_CHARACTER_ANALYSIS_H_INCLUDED

#include "SCCCharacterEngine.h"
#include <vector>

namespace scc
{

struct HarmonicSignature
{
    static constexpr int kNumHarmonics = 8;      // H1 (temel) .. H8

    double levelDb[kNumHarmonics] = {};          // temele gore (dBc); levelDb[0] = 0
    double thdPercent   = 0.0;                   // H2..H8 toplami / temel
    double evenPercent  = 0.0;                   // cift harmoniklerin (H2,H4,H6,H8) temele orani
    double oddPercent   = 0.0;                   // tek harmoniklerin (H3,H5,H7) temele orani
    double fundamentalGainDb = 0.0;              // temelin giris seviyesine gore kazanci
};

class CharacterProbe
{
public:
    //--------------------------------------------------------------------------
    //  spec / intensity : olculecek karakter
    //  sampleRate       : ses yolunun ornekleme hizi
    //  frequencyHz      : test sinusunun frekansi
    //  level            : test sinusunun tepe genligi (lineer)
    //--------------------------------------------------------------------------
    HarmonicSignature measure (const CharacterSpec& spec, double intensity, double sampleRate,
                               double frequencyHz, double level)
    {
        const double fs = std::max (8000.0, sampleRate);
        const int total   = (int) (0.20 * fs);                 // zarflar otursun
        const int analyse = std::min (total / 2, 8192);

        if ((int) buffer.size() != total)
            buffer.assign ((size_t) total, 0.0f);

        // analiz penceresine tam sayida periyot sigdir (sizinti olmasin)
        const double cycles = std::max (4.0, std::round (frequencyHz * (double) analyse / fs));
        const double f0     = cycles * fs / (double) analyse;

        for (int i = 0; i < total; ++i)
            buffer[(size_t) i] = (float) (level * std::sin (kTwoPi * f0 * (double) i / fs));

        CharacterSettings cs;
        cs.on = true;
        cs.intensity = (float) intensity;
        cs.levelMatch = false;
        cs.slot = 0;
        cs.spec[0] = spec;

        engine.prepare (fs);
        engine.setSettings (cs);
        float* ch[1] = { buffer.data() };
        engine.process (ch, 1, total);

        HarmonicSignature sig;
        const float* x = buffer.data() + (total - analyse);
        double h1 = 1.0e-12, sumSq = 0.0, evenSq = 0.0, oddSq = 0.0;

        for (int h = 1; h <= HarmonicSignature::kNumHarmonics; ++h)
        {
            double amp = 0.0;
            if ((double) h * f0 < 0.5 * fs)
            {
                double re = 0.0, im = 0.0;
                const double w = kTwoPi * (double) h * f0 / fs;
                for (int i = 0; i < analyse; ++i)
                {
                    re += (double) x[i] * std::cos (w * (double) i);
                    im += (double) x[i] * std::sin (w * (double) i);
                }
                amp = 2.0 * std::sqrt (re * re + im * im) / (double) analyse;
            }

            if (h == 1)
            {
                h1 = std::max (amp, 1.0e-12);
                sig.levelDb[0] = 0.0;
                sig.fundamentalGainDb = linToDb (h1 / std::max (level, 1.0e-12));
            }
            else
            {
                sig.levelDb[h - 1] = linToDb (std::max (amp, 1.0e-12) / h1);
                sumSq += amp * amp;
                if ((h & 1) == 0) evenSq += amp * amp; else oddSq += amp * amp;
            }
        }

        sig.thdPercent  = 100.0 * std::sqrt (sumSq)  / h1;
        sig.evenPercent = 100.0 * std::sqrt (evenSq) / h1;
        sig.oddPercent  = 100.0 * std::sqrt (oddSq)  / h1;
        return sig;
    }

private:
    CharacterEngine    engine;
    std::vector<float> buffer;
};

} // namespace scc

#endif // SCC_CHARACTER_ANALYSIS_H_INCLUDED
