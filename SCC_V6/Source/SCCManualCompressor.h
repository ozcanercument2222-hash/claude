//==============================================================================
//  SCCManualCompressor.h  -  ANALOG MODE: tamamen manuel kompresor
//                            (JUCE'dan bagimsiz)
//
//  "Analog Mode" klasik, elle ayarlanan kompresor is akisidir: threshold, ratio,
//  attack, release, knee ve makeup dogrudan kullanicidadir; adaptif kararlar yoktur.
//  Bir donanimin devre emulasyonu DEGILDIR ve oyle bir iddia tasimaz.
//
//  Yapi (ileri beslemeli, dB alaninda):
//     stereo bagli tepe dedektoru (istege bagli 100 Hz low-cut)
//       -> yumusak dizli statik egri
//       -> "decoupled" yumusatma: once release (tepe tutma + ussel dusus), sonra attack
//  Attack / release zaman sabitleri ayarlanan degerlerle birebir ortusur.
//
//  Sinyale dokunmaz: ornek basina kazanc uretir (processGain). Kazanci uygulamak,
//  Adaptive <-> Analog gecisini yumusatmak ve bypass ust katmanin (SCCEngine) isidir.
//==============================================================================
#ifndef SCC_MANUAL_COMPRESSOR_H_INCLUDED
#define SCC_MANUAL_COMPRESSOR_H_INCLUDED

#include "SCCDsp.h"

namespace scc
{

constexpr float kManualLimitRatio = 1000.0f;    // bu deger ve ustu: LIMIT (egim = 1)

// Analog Mode ratio secenekleri (parametre sirasi; son eleman LIMIT)
constexpr int   kNumAnalogRatios = 10;
constexpr float kAnalogRatios[kNumAnalogRatios] = { 1.5f, 2.0f, 3.0f, 4.0f, 6.0f, 8.0f, 10.0f, 12.0f, 20.0f, kManualLimitRatio };

struct ManualSettings
{
    float thresholdDb = -18.0f;   // -60 .. 0 dB
    float ratio       = 4.0f;     // 1 .. ; kManualLimitRatio ve ustu = LIMIT
    float kneeDb      = 6.0f;     // 0 .. 24 dB
    float attackMs    = 10.0f;    // 0.1 .. 100 ms
    float releaseMs   = 120.0f;   // 10 .. 1000 ms
    float makeupDb    = 0.0f;     // 0 .. 24 dB (elle)
    bool  autoGain    = false;    // istege bagli: elle makeup'a EK olarak otomatik seviye esitleme
    bool  lowCut      = false;    // sadece dedektor 100 Hz altini gormez
};

//==============================================================================
class ManualCompressor
{
public:
    void prepare (double sampleRate)
    {
        fs  = (float) std::max (8000.0, sampleRate);
        fsD = std::max (8000.0, sampleRate);

        ctrlInterval = std::max (8, (int) std::lround (fs * 0.0007f));
        midCoef      = pole (80.0f);
        paramCoef    = pole (15.0f);
        makeupCoef   = pole (150.0f);
        agUpdate     = 1.0 - std::exp (-1.0 / (1.5 * fsD));

        // yan zincir low-cut: 2. derece Butterworth yuksek geciren (adaptif kompresorle ayni)
        {
            const double w0    = 2.0 * 3.14159265358979323846 * (double) kLowCutHz / fsD;
            const double cw    = std::cos (w0);
            const double sw    = std::sin (w0);
            const double alpha = sw / (2.0 * 0.70710678118654752440);
            const double a0    = 1.0 + alpha;
            hpB0 = ((1.0 + cw) * 0.5) / a0;
            hpB1 = (-(1.0 + cw)) / a0;
            hpB2 = hpB0;
            hpA1 = (-2.0 * cw) / a0;
            hpA2 = (1.0 - alpha) / a0;
        }
        reset();
    }

    void reset()
    {
        ms = 0.0f;  y1 = 0.0f;  gr = 0.0f;
        inE = 0.0;  outE = 0.0;
        autoLin = 1.0f;  autoTargetLin = 1.0f;
        hpZ1[0] = hpZ1[1] = hpZ2[0] = hpZ2[1] = 0.0;
        ctrlCount = 0;
        snapParams = true;
    }

    void setSettings (const ManualSettings& s) noexcept { settings = s; }

    static float slopeForRatio (float ratio) noexcept
    {
        return ratio >= kManualLimitRatio ? 1.0f : 1.0f - 1.0f / std::max (ratio, 1.0f);
    }

    //--------------------------------------------------------------------------
    //  in      : temiz (NaN/Inf icermeyen) giris, 1 veya 2 kanal
    //  gainOut : ornek basina lineer kazanc (kompresyon * makeup)
    //  grOut   : ornek basina gain reduction (dB, >= 0)  - nullptr olabilir
    //--------------------------------------------------------------------------
    void processGain (const float* const* in, int numCh, int numSamples,
                      float* gainOut, float* grOut = nullptr) noexcept
    {
        numCh = std::min (numCh, 2);
        if (numCh <= 0 || numSamples <= 0)
            return;

        const float thrTarget   = std::clamp (settings.thresholdDb, -60.0f, 0.0f);
        const float slopeTarget = slopeForRatio (settings.ratio);
        const float kneeTarget  = std::clamp (settings.kneeDb, 0.0f, 24.0f);
        const float mkTarget    = std::exp (std::clamp (settings.makeupDb, 0.0f, 24.0f) * kLn10Over20);

        if (snapParams) { thr = thrTarget; slope = slopeTarget; knee = kneeTarget; manualLin = mkTarget; snapParams = false; }

        attackMs  = std::clamp (settings.attackMs,  0.1f,  100.0f);
        releaseMs = std::clamp (settings.releaseMs, 10.0f, 1000.0f);
        const float attackCoef  = pole (attackMs);
        const float releaseCoef = pole (releaseMs);

        const float paramStep  = 1.0f - paramCoef;
        const float makeupStep = 1.0f - makeupCoef;
        const bool  lowCutOn   = settings.lowCut;

        for (int n = 0; n < numSamples; ++n)
        {
            thr       += (thrTarget   - thr)       * paramStep;
            slope     += (slopeTarget - slope)     * paramStep;
            knee      += (kneeTarget  - knee)      * paramStep;
            manualLin += (mkTarget    - manualLin) * paramStep;

            // ---- yan zincir: stereo bagli tepe degeri
            float det = 0.0f, inPow = 0.0f;
            for (int c = 0; c < numCh; ++c)
            {
                const float s = in[c][n];
                const float a = std::abs (s);
                inPow += s * s;

                const double yf = hpB0 * (double) s + hpZ1[c];
                hpZ1[c] = hpB1 * (double) s - hpA1 * yf + hpZ2[c];
                hpZ2[c] = hpB2 * (double) s - hpA2 * yf;
                det = std::max (det, lowCutOn ? (float) std::abs (yf) : a);
            }
            inPow /= (float) numCh;
            ms = det * det + midCoef * (ms - det * det);

            // ---- kazanc hesabi (dB alani)
            const float levelDb = 20.0f * std::log10 (std::max (det, 1.0e-9f));
            const float target  = grCurveSlope (levelDb, thr, slope, knee);

            y1 = std::max (target, target + releaseCoef * (y1 - target));
            gr = y1 + attackCoef * (gr - y1);
            if (y1 < 1.0e-9f) y1 = 0.0f;
            if (gr < 1.0e-9f) gr = 0.0f;
            gr = std::min (gr, 80.0f);

            const float gComp = std::exp (-gr * kLn10Over20);

            // ---- istege bagli auto gain (uzun vadeli giris / cikis enerji orani)
            if (ms > kGatePower)
            {
                inE  += agUpdate * ((double) inPow - inE);
                outE += agUpdate * ((double) (inPow * gComp * gComp) - outE);
            }
            if (++ctrlCount >= ctrlInterval)
            {
                ctrlCount = 0;
                float mk = 0.0f;
                if (inE > 1.0e-9 && outE > 1.0e-12)
                    mk = (float) (10.0 * std::log10 (inE / outE));
                mk = std::clamp (mk, 0.0f, kMaxMakeupDb);
                autoTargetLin = settings.autoGain ? std::exp (mk * kLn10Over20) : 1.0f;
            }
            autoLin += (autoTargetLin - autoLin) * makeupStep;

            gainOut[n] = gComp * autoLin * manualLin;
            if (grOut != nullptr) grOut[n] = gr;
        }
    }

    float currentGrDb()      const noexcept { return gr; }
    float currentAttackMs()  const noexcept { return attackMs; }
    float currentReleaseMs() const noexcept { return releaseMs; }
    float currentKneeDb()    const noexcept { return knee; }
    float currentMakeupDb()  const noexcept { return 20.0f * std::log10 (std::max (autoLin * manualLin, 1.0e-6f)); }

private:
    float pole (float ms_) const noexcept { return std::exp (-1.0f / (0.001f * ms_ * fs)); }

    ManualSettings settings;

    float  fs = 48000.0f;
    double fsD = 48000.0;
    int    ctrlInterval = 34, ctrlCount = 0;

    float  midCoef = 0.0f, paramCoef = 0.0f, makeupCoef = 0.0f;
    double agUpdate = 0.0;

    bool   snapParams = true;
    float  thr = -18.0f, slope = 0.75f, knee = 6.0f, manualLin = 1.0f;
    float  attackMs = 10.0f, releaseMs = 120.0f;
    float  ms = 0.0f, y1 = 0.0f, gr = 0.0f;
    double inE = 0.0, outE = 0.0;
    float  autoLin = 1.0f, autoTargetLin = 1.0f;

    double hpB0 = 1.0, hpB1 = 0.0, hpB2 = 0.0, hpA1 = 0.0, hpA2 = 0.0;
    double hpZ1[2] = { 0.0, 0.0 }, hpZ2[2] = { 0.0, 0.0 };
};

} // namespace scc

#endif // SCC_MANUAL_COMPRESSOR_H_INCLUDED
