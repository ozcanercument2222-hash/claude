//==============================================================================
//  SCCDsp.h  -  Single Knob Compressor : DSP cekirdegi (JUCE'dan bagimsiz)
//
//  Fikir:
//    * Tek knob (AMOUNT 0..100 %) sadece THRESHOLD'u surer:
//          %0   -> -1 dB
//          %100 -> -32 dB
//    * ATTACK / RELEASE knob'dan BAGIMSIZDIR. Sinyalin dalga formuna bakilarak
//      surekli otomatik ayarlanir. Nominal degerler: attack 20 ms, release 100 ms.
//        - Attack  : keskin transient (zirve >> ortalama)  -> kisalir
//                    duragan / dolgun ses                   -> uzar
//        - Release : seviye hizla dalgaliniyorsa (hece, bosluk) -> kisalir
//                    seviye duragansa (uzun nota)               -> uzar (pompalama olmaz)
//    * RATIO : kullanici secer (2/4/8/12/16/18 veya serbest deger).
//    * Karakter (Smooth / Balanced / Hard) : diz (knee) genisligi ve adaptasyon araligi.
//      Diz genisligi ayrica KNOB'a gore de degisir: knob arttikca diz genisler
//      (kompresyon derinlestikce gecis yumusar, "bogma" azalir).
//    * Auto Gain : giris ve cikis enerjisini uzun vadede esitler (ayni yuksek ses).
//==============================================================================
#ifndef SCC_DSP_H_INCLUDED
#define SCC_DSP_H_INCLUDED

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace scc
{

//------------------------------------------------------------------------------
//  SENIN TARIFIN - davranisi degistirmek istersen sabitler burada
//------------------------------------------------------------------------------
constexpr float kThresholdAt0Pct   = -1.0f;    // knob %0   -> threshold  -1 dB
constexpr float kThresholdAt100Pct = -32.0f;   // knob %100 -> threshold -32 dB
constexpr float kNominalAttackMs   = 20.0f;    // adaptif attack bu degerin cevresinde gezer
constexpr float kNominalReleaseMs  = 100.0f;   // adaptif release bu degerin cevresinde gezer
constexpr float kMaxMakeupDb       = 24.0f;    // auto-gain ust siniri
constexpr float kLowCutHz          = 100.0f;   // 'LOW CUT' acikken dedektorun gordugu yuksek geciren filtre

// Diz (knee) genisligi: karakterin TAM genisligi knob %100'de gecerli.
//   knob %0'da  : tam genisligin kKneeAt0PctFraction kadari
//   arasi       : kKneeCurve ile yumusak gecis (1'den kucuk -> alt yarida daha hizli acilir)
//   Yonu tersine cevirmek istersen kKneeAt0PctFraction'i 1'den buyuk yap (ornek 2.0:
//   knob arttikca diz daralir / daha "sert" olur).
constexpr float kKneeAt0PctFraction = 0.35f;
constexpr float kKneeCurve          = 0.70f;

// Adaptasyon hassasiyeti
constexpr float kCrestLoDb = 3.0f;     // zirve/ortalama farki bu kadarsa -> "duragan"   (D = 0)
constexpr float kCrestHiDb = 18.0f;    // bu kadar veya fazlasi             -> "transient" (D = 1)
constexpr float kFluxLo    = 10.0f;    // seviye degisim hizi (dB/sn)  duragan   (F = 0)  ornek: uzun nota
constexpr float kFluxHi    = 110.0f;   //                              dalgali   (F = 1)  ornek: hizli hece dizisi
constexpr float kGatePower = 3.16e-6f; // -55 dB: bunun altinda sinyal yok sayilir
constexpr float kGateDb    = -55.0f;

constexpr float kLn10Over20 = 0.11512925465f;   // ln(10)/20

enum Mode { Smooth = 0, Balanced = 1, Hard = 2 };

struct Settings
{
    float amountPct = 50.0f;   // 0..100
    float ratio     = 4.0f;    // 1..100
    int   mode      = Smooth;
    bool  autoGain  = true;
    bool  lowCut    = false;   // sadece dedektor (yan zincir) 100 Hz alti gormez; ses degismez
    bool  bypass    = false;   // yumusak gecisli bypass
};

struct CharacterParams
{
    float kneeDb;                 // yumusak diz genisligi (dB) - knob %100'deki TAM deger
    float atkSlow, atkFast;       // ms : duragan sinyalde / transient'te
    float relSlow, relFast;       // ms : duragan sinyalde / dalgali sinyalde
};

inline const CharacterParams& characterFor (int mode) noexcept
{
    static const CharacterParams table[3] = {
        //  knee   atkSlow atkFast  relSlow relFast
        { 12.0f,   36.0f,   9.0f,   220.0f,  55.0f },   // Smooth
        {  6.0f,   30.0f,   6.0f,   190.0f,  45.0f },   // Balanced
        {  1.5f,   24.0f,   2.5f,   150.0f,  35.0f },   // Hard
    };
    return table[std::clamp (mode, 0, 2)];
}

// Karaktere VE knob'a gore diz genisligi (dB)
inline float kneeFor (const CharacterParams& cp, float amountPct) noexcept
{
    const float a = std::clamp (amountPct, 0.0f, 100.0f) * 0.01f;
    const float t = std::pow (a, kKneeCurve);
    return cp.kneeDb * (kKneeAt0PctFraction + (1.0f - kKneeAt0PctFraction) * t);
}

//------------------------------------------------------------------------------
//  Audio thread -> GUI thread veri akisi (kilitsiz)
//------------------------------------------------------------------------------
struct Telemetry
{
    static constexpr int   kScopeSize       = 512;      // 2'nin kuvveti olmali
    static constexpr float kScopeColsPerSec = 100.0f;

    std::atomic<float> grDb      { 0.0f };
    std::atomic<float> attackMs  { kNominalAttackMs };
    std::atomic<float> releaseMs { kNominalReleaseMs };
    std::atomic<float> makeupDb  { 0.0f };

    std::atomic<float> inPeak[2];      // lineer; "son okumadan beri en buyuk"
    std::atomic<float> outPeak[2];

    std::atomic<float> scopeIn[kScopeSize];   // her kolon: giris tepe degeri (lineer)
    std::atomic<float> scopeGr[kScopeSize];   // her kolon: gain reduction (dB)
    std::atomic<std::uint32_t> scopeHead { 0 };

    Telemetry()
    {
        for (auto& a : inPeak)  a.store (0.0f);
        for (auto& a : outPeak) a.store (0.0f);
        for (auto& a : scopeIn) a.store (0.0f);
        for (auto& a : scopeGr) a.store (0.0f);
    }

    static void atomicMax (std::atomic<float>& a, float v) noexcept
    {
        float cur = a.load (std::memory_order_relaxed);
        while (v > cur && ! a.compare_exchange_weak (cur, v, std::memory_order_relaxed)) {}
    }
};

//------------------------------------------------------------------------------
//  Yardimcilar
//------------------------------------------------------------------------------
inline float smooth01 (float t) noexcept
{
    t = std::clamp (t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// t=0 -> v0, t=0.5 -> vMid, t=1 -> v1  (logaritmik interpolasyon)
inline float pivotMap (float t, float v0, float vMid, float v1) noexcept
{
    t = std::clamp (t, 0.0f, 1.0f);
    const float a = (t < 0.5f) ? v0 : vMid;
    const float b = (t < 0.5f) ? vMid : v1;
    const float u = (t < 0.5f) ? t * 2.0f : (t - 0.5f) * 2.0f;
    return a * std::pow (b / a, u);
}

// Yumusak dizli statik egri: verilen giris seviyesi icin gain reduction (dB, >= 0)
inline float grCurve (float levelDb, float thresholdDb, float ratio, float kneeDb) noexcept
{
    const float over  = levelDb - thresholdDb;
    const float slope = 1.0f - 1.0f / std::max (ratio, 1.0f);

    if (kneeDb < 0.01f)
        return over > 0.0f ? over * slope : 0.0f;

    if (2.0f * over < -kneeDb)
        return 0.0f;

    if (2.0f * std::abs (over) <= kneeDb)
    {
        const float t = over + kneeDb * 0.5f;
        return slope * t * t / (2.0f * kneeDb);
    }
    return over * slope;
}

//==============================================================================
class Compressor
{
public:
    Telemetry telemetry;

    static float thresholdForAmount (float amountPct) noexcept
    {
        const float a = std::clamp (amountPct, 0.0f, 100.0f) * 0.01f;
        return kThresholdAt0Pct + (kThresholdAt100Pct - kThresholdAt0Pct) * a;
    }

    //--------------------------------------------------------------------------
    void prepare (double sampleRate)
    {
        fs           = (float) std::max (8000.0, sampleRate);
        fsD          = std::max (8000.0, sampleRate);
        ctrlInterval = std::max (8, (int) std::lround (fs * 0.0007f));      // ~0.7 ms
        ctrlDt       = (float) ctrlInterval / fs;

        peakRelCoef  = pole (12.0f);        // tepe izleyici: aninda attack, 12 ms release
        midCoef      = pole (80.0f);        // 80 ms RMS (orta vadeli ortalama)
        paramCoef    = pole (15.0f);        // parametre yumusatma
        makeupCoef   = pole (150.0f);       // makeup gain yumusatma
        agUpdate     = 1.0 - std::exp (-1.0 / (1.5 * fsD));     // 1.5 sn enerji ortalamasi

        dRiseCoef    = ctrlPole (1.5f);
        dFallCoef    = ctrlPole (40.0f);
        fluxCoef     = ctrlPole (60.0f);

        bypassCoef   = pole (4.0f);         // bypass gecisi ~ 25 ms

        // yan zincir low-cut: 2. derece Butterworth yuksek geciren (sadece dedektor icin)
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

        samplesPerColumn = std::max (1, (int) std::lround (fs / Telemetry::kScopeColsPerSec));
        reset();
    }

    void reset()
    {
        pk = 0.0f;  ms = 0.0f;
        y1 = 0.0f;  gr = 0.0f;
        dSm = 0.5f; fluxSm = 0.5f * (kFluxLo + kFluxHi);
        prevMidDb = kGateDb;
        attackMs  = kNominalAttackMs;
        releaseMs = kNominalReleaseMs;
        attackCoef  = pole (attackMs);
        releaseCoef = pole (releaseMs);
        inE = 0.0;  outE = 0.0;
        makeupLin = 1.0f;  makeupTargetLin = 1.0f;
        hpZ1[0] = hpZ1[1] = hpZ2[0] = hpZ2[1] = 0.0;
        bypassMix = 0.0f;
        ctrlCount = 0;
        colCount  = 0;  colIn = 0.0f;
        snapParams = true;
    }

    void setSettings (const Settings& s) noexcept { settings = s; }

    //--------------------------------------------------------------------------
    //  io : kanal basina bir isaretci dizisi (yerinde isler). 1 veya 2 kanal.
    //--------------------------------------------------------------------------
    void process (float* const* io, int numCh, int numSamples) noexcept
    {
        numCh = std::min (numCh, 2);
        if (numCh <= 0 || numSamples <= 0)
            return;

        const CharacterParams& cp = characterFor (settings.mode);
        curChar = &cp;

        const float thrTarget   = thresholdForAmount (settings.amountPct);
        const float ratioTarget = std::clamp (settings.ratio, 1.0f, 100.0f);
        const float kneeTarget  = kneeFor (cp, settings.amountPct);

        if (snapParams) { thr = thrTarget; ratio = ratioTarget; knee = kneeTarget; snapParams = false; }

        const float paramStep  = 1.0f - paramCoef;
        const float makeupStep = 1.0f - makeupCoef;
        const bool  lowCutOn   = settings.lowCut;
        const float bypassTarget = settings.bypass ? 1.0f : 0.0f;
        const float bypassStep   = 1.0f - bypassCoef;

        float inPk[2]  = { 0.0f, 0.0f };
        float outPk[2] = { 0.0f, 0.0f };

        for (int n = 0; n < numSamples; ++n)
        {
            thr   += (thrTarget   - thr)   * paramStep;
            ratio += (ratioTarget - ratio) * paramStep;
            knee  += (kneeTarget  - knee)  * paramStep;

            // ---- yan zincir: stereo bagli tepe degeri
            float x[2] = { 0.0f, 0.0f };
            float det = 0.0f, detRaw = 0.0f, inPow = 0.0f;
            for (int c = 0; c < numCh; ++c)
            {
                float s = io[c][n];
                if (! std::isfinite (s)) s = 0.0f;
                x[c] = s;
                const float a = std::abs (s);
                inPk[c] = std::max (inPk[c], a);
                detRaw  = std::max (detRaw, a);
                inPow  += s * s;

                // low-cut filtresi hep calisir (durum surekli kalsin), kullanimi LOW CUT'a bagli
                const double yf = hpB0 * (double) s + hpZ1[c];
                hpZ1[c] = hpB1 * (double) s - hpA1 * yf + hpZ2[c];
                hpZ2[c] = hpB2 * (double) s - hpA2 * yf;
                det = std::max (det, lowCutOn ? (float) std::abs (yf) : a);
            }
            inPow /= (float) numCh;

            // tepe izleyici + orta vadeli RMS
            pk = (det > pk) ? det : det + peakRelCoef * (pk - det);
            ms = det * det + midCoef * (ms - det * det);

            if (++ctrlCount >= ctrlInterval)
            {
                ctrlCount = 0;
                updateControl();
            }

            // ---- kazanc hesabi (dB alani)
            const float levelDb = 20.0f * std::log10 (std::max (pk, 1.0e-9f));
            const float target  = grCurve (levelDb, thr, ratio, knee);

            // "decoupled" yumusatma: once release (tepe tutma + ussel dusus), sonra attack
            y1 = std::max (target, target + releaseCoef * (y1 - target));
            gr = y1 + attackCoef * (gr - y1);
            if (y1 < 1.0e-9f) y1 = 0.0f;
            if (gr < 1.0e-9f) gr = 0.0f;
            gr = std::min (gr, 80.0f);

            const float gComp = std::exp (-gr * kLn10Over20);
            makeupLin += (makeupTargetLin - makeupLin) * makeupStep;

            bypassMix += (bypassTarget - bypassMix) * bypassStep;
            if (std::abs (bypassTarget - bypassMix) < 1.0e-4f) bypassMix = bypassTarget;

            float outPow = 0.0f;
            for (int c = 0; c < numCh; ++c)
            {
                const float yc  = x[c] * gComp;
                outPow += yc * yc;
                const float wet = yc * makeupLin;
                const float yo  = wet + (x[c] - wet) * bypassMix;      // bypass: islenmis -> kuru gecis
                io[c][n] = yo;
                outPk[c] = std::max (outPk[c], std::abs (yo));
            }
            outPow /= (float) numCh;

            // ---- auto gain enerji ortalamalari (sadece sinyal varken)
            if (ms > kGatePower)
            {
                inE  += agUpdate * ((double) inPow  - inE);
                outE += agUpdate * ((double) outPow - outE);
            }

            // ---- osiloskop kolonu
            colIn = std::max (colIn, detRaw);
            if (++colCount >= samplesPerColumn)
            {
                colCount = 0;
                const std::uint32_t h = telemetry.scopeHead.load (std::memory_order_relaxed);
                const std::uint32_t i = h & (std::uint32_t) (Telemetry::kScopeSize - 1);
                telemetry.scopeIn[i].store (colIn, std::memory_order_relaxed);
                telemetry.scopeGr[i].store (gr,    std::memory_order_relaxed);
                telemetry.scopeHead.store (h + 1u, std::memory_order_release);
                colIn = 0.0f;
            }
        }

        if (numCh == 1) { inPk[1] = inPk[0]; outPk[1] = outPk[0]; }

        telemetry.grDb.store      (gr,        std::memory_order_relaxed);
        telemetry.attackMs.store  (attackMs,  std::memory_order_relaxed);
        telemetry.releaseMs.store (releaseMs, std::memory_order_relaxed);
        telemetry.makeupDb.store  (20.0f * std::log10 (std::max (makeupLin, 1.0e-6f)), std::memory_order_relaxed);
        for (int c = 0; c < 2; ++c)
        {
            Telemetry::atomicMax (telemetry.inPeak[c],  inPk[c]);
            Telemetry::atomicMax (telemetry.outPeak[c], outPk[c]);
        }
    }

    // testler / GUI icin okuma
    float currentGrDb()     const noexcept { return gr; }
    float currentAttackMs() const noexcept { return attackMs; }
    float currentReleaseMs()const noexcept { return releaseMs; }
    float currentFluxDbPerSec() const noexcept { return fluxSm; }

private:
    float pole     (float ms_)  const noexcept { return std::exp (-1.0f / (0.001f * ms_ * fs)); }
    float ctrlPole (float ms_)  const noexcept { return std::exp (-ctrlDt / (0.001f * ms_)); }

    //--------------------------------------------------------------------------
    //  Kontrol hizinda (~0.7 ms) calisan "dalga formu analizi"
    //--------------------------------------------------------------------------
    void updateControl() noexcept
    {
        const CharacterParams& cp = *curChar;

        const float fastDb = 20.0f * std::log10 (std::max (pk, 1.0e-9f));
        const float midDb  = 10.0f * std::log10 (ms + 1.0e-12f);
        const bool  present = ms > kGatePower;

        // D : transient endeksi (zirve ortalamadan ne kadar yuksek?)
        const float crest = fastDb - midDb;
        const float dRaw  = present ? smooth01 ((crest - kCrestLoDb) / (kCrestHiDb - kCrestLoDb)) : 0.5f;
        const float dCo   = (dRaw > dSm) ? dRiseCoef : dFallCoef;
        dSm = dRaw + dCo * (dSm - dRaw);

        // F : dalgalanma endeksi (seviye ne kadar hizli degisiyor?)
        const float midDbC = std::max (midDb, kGateDb);        // sessizlige dogru dB ucurumu olcumu bozmasin
        const float rate = present ? std::min (std::abs (midDbC - prevMidDb) / ctrlDt, 600.0f)
                                   : 0.5f * (kFluxLo + kFluxHi);
        prevMidDb = midDbC;
        fluxSm = rate + fluxCoef * (fluxSm - rate);
        const float fIdx = smooth01 ((fluxSm - kFluxLo) / (kFluxHi - kFluxLo));

        attackMs    = pivotMap (dSm,  cp.atkSlow, kNominalAttackMs,  cp.atkFast);
        releaseMs   = pivotMap (fIdx, cp.relSlow, kNominalReleaseMs, cp.relFast);
        attackCoef  = pole (attackMs);
        releaseCoef = pole (releaseMs);

        // auto gain hedefi: uzun vadeli giris/cikis enerji orani
        float mk = 0.0f;
        if (inE > 1.0e-9 && outE > 1.0e-12)
            mk = (float) (10.0 * std::log10 (inE / outE));
        mk = std::clamp (mk, 0.0f, kMaxMakeupDb);
        makeupTargetLin = settings.autoGain ? std::exp (mk * kLn10Over20) : 1.0f;
    }

    //--------------------------------------------------------------------------
    Settings settings;
    const CharacterParams* curChar = &characterFor (Smooth);

    float  fs = 48000.0f;
    double fsD = 48000.0;
    int    ctrlInterval = 34;
    float  ctrlDt = 0.0007f;
    int    ctrlCount = 0;

    // katsayilar
    float peakRelCoef = 0.0f, midCoef = 0.0f, paramCoef = 0.0f, makeupCoef = 0.0f;
    float dRiseCoef = 0.0f, dFallCoef = 0.0f, fluxCoef = 0.0f;
    float attackCoef = 0.0f, releaseCoef = 0.0f;
    double agUpdate = 0.0;

    // durum
    bool  snapParams = true;
    float thr = kThresholdAt0Pct, ratio = 4.0f, knee = 12.0f;
    float pk = 0.0f, ms = 0.0f;
    float y1 = 0.0f, gr = 0.0f;
    float dSm = 0.5f, fluxSm = 105.0f, prevMidDb = kGateDb;
    float attackMs = kNominalAttackMs, releaseMs = kNominalReleaseMs;
    double inE = 0.0, outE = 0.0;
    float makeupLin = 1.0f, makeupTargetLin = 1.0f;

    // yan zincir low-cut (Butterworth 2. derece) ve bypass
    double hpB0 = 1.0, hpB1 = 0.0, hpB2 = 0.0, hpA1 = 0.0, hpA2 = 0.0;
    double hpZ1[2] = { 0.0, 0.0 }, hpZ2[2] = { 0.0, 0.0 };
    float  bypassCoef = 0.0f, bypassMix = 0.0f;

    // osiloskop
    int   samplesPerColumn = 480;
    int   colCount = 0;
    float colIn = 0.0f;
};

} // namespace scc

#endif // SCC_DSP_H_INCLUDED
