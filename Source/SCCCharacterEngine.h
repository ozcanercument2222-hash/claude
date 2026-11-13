//==============================================================================
//  SCCCharacterEngine.h  -  Karakter motoru (JUCE'dan bagimsiz)
//
//  Kompresorun kazanc azaltmasindan TAMAMEN bagimsiz bir renk katmanidir;
//  kompresorden sonra calisir ve hem Adaptive hem Analog modda kullanilir.
//
//  Yedi tip, yedi farkli yapi (ayni egriye farkli isim verilmis DEGILDIR):
//
//    CLEAN        Dogrusal bolgeli tepe yuvarlayici. Egrinin altinda sinyal bit
//                 duzeyinde degismez; yalnizca tepeler yumusakca yuvarlanir.
//    WARM         Kaydirilmis (bias'li) asimetrik egri -> 2. harmonik baskin.
//                 Alt-orta vurgulanarak doyurulur; tizler hafifce yumusatilir.
//    TAPE         Simetrik yumusak egri -> 3. harmonik baskin. Tizler vurgulanip
//                 doyurulur (tizlerde kendiliginden sikisma), kafa tumsegi (75 Hz)
//                 ve ust tiz kaybi.
//    TUBE         Iki egimli asimetrik egri + DINAMIK bias: calisma noktasi sinyal
//                 zarfiyla kayar, harmonik dengesi seviyeyle ve zamanla degisir.
//    TRANSFORMER  Aki alaninda doygunluk: sinyal once "integre edilir" (bas agirlikli),
//                 doyurulur, sonra tam tersi uygulanir. Baslar doyar, tizler temiz kalir.
//    DRIVE        Iki kademe: asimetrik yumusak kat -> sert kirpici. Bas onceden
//                 sikilastirilir. Belirgin, agresif bozulma.
//    CUSTOM       Character Designer zinciri:
//                 Transient -> Density -> Tone(on) -> HarmonicGenerator ->
//                 SaturationProcessor -> Tone(arka) -> Thickness
//
//  Bu tipler birer TASARIMDIR; belirli bir donanimin emulasyonu olduklari iddia
//  edilmez.
//
//  Ortak ozellikler
//    * 4x (>= 88.2 kHz'de 2x) minimum fazli oversampling + ADAA. Gecikme bildirilmez.
//    * CHARACTER knob'u (intensity) butun yogunlugu olcekler; %0'da veya OFF iken
//      motor tamamen devre disidir (sinyal bit duzeyinde aynen gecer, islemci harcamaz).
//    * Cikis telafisi: her ayarda referans seviyedeki ses yuksekligi korunur.
//      LEVEL MATCH acikken buna ek olarak giris / cikis enerjisi dinamik esitlenir
//      (A ve B icin ayri ayri ogrenilir -> A/B gecisinde aninda dogru seviye).
//    * Asimetrik egriler sinyalin zarfini izleyen bir DC kaymasi (ses alti "gumleme")
//      uretir. Bu, her tipin sonunda yalnizca dogrusal olmayan KALINTIDAN
//      ( cikis - giris ) 30 Hz'de suzulur; dogrusal yolun bas tepkisi hic degismez.
//    * Butun parametreler ornek basina yumusatilir; tip degisimi (ve tipleri farkli
//      A/B gecisi) iki serit arasinda capraz gecisle yapilir -> tik / sicrama yok.
//    * Ses is parcaciginda bellek ayirma, kilit, dosya islemi yoktur.
//==============================================================================
#ifndef SCC_CHARACTER_ENGINE_H_INCLUDED
#define SCC_CHARACTER_ENGINE_H_INCLUDED

#include "SCCCharacterTypes.h"
#include "SCCCharacterToneProcessor.h"
#include "SCCFilters.h"
#include "SCCHarmonicGenerator.h"
#include "SCCOversampler.h"
#include "SCCSaturationProcessor.h"
#include "SCCShaper.h"
#include "SCCTransientProcessor.h"

namespace scc
{

//------------------------------------------------------------------------------
//  Voicing: bir karakterin (tip + intensity + Designer degerleri) DSP'ye
//  cevrilmis hali. Ses isleme, egri gostergesi ve cikis telafisi ayni yapiyi
//  kullanir; yani ekranda gorunen egri calisan algoritmanin kendisidir.
//------------------------------------------------------------------------------
enum VoicingIndex
{
    vBlend = 0,      // kuru -> islenmis gecis (intensity'den)
    vComp,           // cikis telafisi (lineer)
    vD1, vD2,        // kademe 1 / 2 giris kazanci (lineer)
    vT1, vH1, vA1,   // kademe 1 egrisi: dogrusal bolge, sertlik, asimetri
    vT2, vH2,        // kademe 2 egrisi
    vBias,           // calisma noktasi kaymasi
    vBiasEnv,        // zarfa bagli ek kayma (TUBE)
    vKPre,           // tipe ozel vurgu miktari
    vPost1, vPost2,  // tipe ozel son islemler
    vEven, vOdd, vHBal,          // CUSTOM: harmonik ureteci
    vSatMix,                     // CUSTOM: doyurucu karisimi
    vKLow, vKHigh,               // CUSTOM: ton vurgulari
    vRoundMix,                   // CUSTOM: vurus yuvarlama karisimi
    vThick,                      // CUSTOM: alt bant harmonikleri
    kNumVoicing
};

struct Voicing
{
    int    type = kCharWarm;
    double v[kNumVoicing] = {};

    // taban hizda kullanilanlar (CUSTOM)
    TransientParams transient;
    double density = 0.0;

    // egri gostergesi icin: tipin en cok doydugu frekanstaki on-vurgu kazanci
    double displayGain = 1.0;
};

namespace character
{
    constexpr double kReferenceLevel = 0.25;      // cikis telafisinin referansi (-12 dBFS tepe)

    inline ShaperShape shape1 (const double* v) noexcept { ShaperShape s; s.T = v[vT1]; s.h = v[vH1]; s.alpha = v[vA1]; return s; }
    inline ShaperShape shape2 (const double* v) noexcept { ShaperShape s; s.T = v[vT2]; s.h = v[vH2]; s.alpha = 1.0;    return s; }

    //--------------------------------------------------------------------------
    //  Statik (belleksiz) cekirdek: tipin dogrusal olmayan kismi, filtreler ve
    //  zarflar haric. level = sinyalin o anki tepe seviyesi (zarfa bagli kisimlar icin).
    //  preGain = dogrusal olmayan kata giristeki vurgu kazanci (orta bantta 1).
    //--------------------------------------------------------------------------
    inline double staticCore (const Voicing& o, double x, double level, double preGain) noexcept
    {
        const double* v = o.v;
        const ShaperShape s1 = shape1 (v);
        const double d1 = v[vD1] * preGain;

        switch (o.type)
        {
            case kCharClean:
            case kCharTape:
                return shaper::S (d1 * x, s1) / d1;

            case kCharWarm:
            case kCharTransformer:
            {
                const double b = v[vBias];
                return (shaper::S (d1 * x + b, s1) - shaper::S (b, s1)) / (d1 * shaper::SD (b, s1));
            }

            case kCharTube:
            {
                const double b = v[vBias] + v[vBiasEnv] * std::min (d1 * level, 1.5);
                return (shaper::S (d1 * x + b, s1) - shaper::S (b, s1)) / (d1 * shaper::SD (b, s1));
            }

            case kCharDrive:
            {
                const double b = v[vBias];
                const double w = (shaper::S (d1 * x + b, s1) - shaper::S (b, s1)) / shaper::SD (b, s1);
                return shaper::S (v[vD2] * w, shape2 (v)) / (d1 * v[vD2]);
            }

            default:    // kCharCustom
            {
                HarmonicParams hp;
                hp.even = v[vEven];  hp.odd = v[vOdd];  hp.balance = v[vHBal];
                double e, od;
                harmonicsFor (x, std::max (std::max (level, std::abs (x)), 1.0e-6), hp, e, od);

                SaturationParams sp;
                sp.drive = v[vD1];  sp.mix = v[vSatMix];  sp.shape = s1;
                return saturateStatic (x + e + od, sp);
            }
        }
    }

    // Referans seviyedeki sinusun RMS'ini koruyan cikis kazanci
    inline double compensationFor (const Voicing& o) noexcept
    {
        constexpr int    N = 48;
        constexpr double A = kReferenceLevel;

        double sum = 0.0, sum2 = 0.0;
        for (int i = 0; i < N; ++i)
        {
            const double x = A * std::sin (kTwoPi * ((double) i + 0.5) / (double) N);
            const double y = staticCore (o, x, A, 1.0);
            sum  += y;
            sum2 += y * y;
        }
        const double mean = sum / (double) N;
        const double rms  = std::sqrt (std::max (sum2 / (double) N - mean * mean, 1.0e-12));
        return std::clamp ((A * 0.70710678118654752440) / rms, 0.25, 8.0);
    }

    //--------------------------------------------------------------------------
    //  Karakter -> Voicing
    //--------------------------------------------------------------------------
    inline void computeVoicing (const CharacterSpec& spec, double intensity, Voicing& o) noexcept
    {
        const double I = std::clamp (intensity, 0.0, 1.0);

        o = Voicing();
        o.type = std::clamp (spec.type, 0, (int) kNumCharacterTypes - 1);

        double* v = o.v;
        v[vBlend] = 1.0 - (1.0 - I) * (1.0 - I) * (1.0 - I);
        v[vComp]  = 1.0;
        v[vD1] = v[vD2] = 1.0;
        v[vA1] = 1.0;

        switch (o.type)
        {
            case kCharClean:
                v[vD1] = dbToLin (4.0 * I);
                v[vT1] = 1.0 - 0.85 * I;              // %100'de -16 dBFS'in altinda tamamen dogrusal
                break;

            case kCharWarm:
                v[vD1]    = dbToLin (5.0 * I);
                v[vBias]  = 0.22 * I;                 // asimetri -> 2. harmonik
                v[vKPre]  = 1.2 * I;                  // 240 Hz altina +7 dB'ye kadar vurgu
                v[vPost1] = 0.22 * I;                 // tizlerde hafif yumusama
                break;

            case kCharTape:
                v[vD1]    = dbToLin (10.0 * I);
                v[vH1]    = 0.08;
                v[vKPre]  = 1.2 * I;                  // 2.8 kHz ustune +7 dB'ye kadar vurgu
                v[vPost1] = 0.8 * I;                  // ust tiz kaybi
                v[vPost2] = dbToLin (2.4 * I) - 1.0;  // kafa tumsegi
                break;

            case kCharTube:
                v[vD1]      = dbToLin (8.0 * I);
                v[vA1]      = 1.0 - 0.30 * I;         // negatif yarim dalga daha gec doyar
                v[vBias]    = 0.12 * I;
                v[vBiasEnv] = 0.22 * I;               // bias zarfla kayar
                break;

            case kCharTransformer:
                v[vD1]   = dbToLin (3.5 * I);
                v[vH1]   = 0.12;
                v[vBias] = 0.03 * I;                  // kalici miknatislanma: iz miktarda cift harmonik
                v[vKPre] = 2.4 * I;                   // aki alani: DC'de +10.6 dB, 85 Hz'den itibaren
                o.displayGain = 1.0 + 0.8 * v[vKPre]; // ~60 Hz'deki vurgu
                break;

            case kCharDrive:
                v[vD1]    = dbToLin (12.0 * I);
                v[vA1]    = 1.0 - 0.25 * I;
                v[vBias]  = 0.18 * I;
                v[vD2]    = dbToLin (9.0 * I);
                v[vT2]    = 0.15;
                v[vH2]    = 0.70;                     // 2. kademe: sert kirpici
                v[vKPre]  = 0.75 * I;                 // 180 Hz alti kirpicilara daha dusuk girer
                v[vPost1] = 0.5 * I;                  // cizirtiyi yumusat
                break;

            default:    // kCharCustom
            {
                auto pct = [&spec] (int i) { return 0.01 * (double) spec.custom[i]; };

                const double harm = std::clamp (pct (kCpHarmAmount), 0.0, 1.0) * I;
                v[vEven] = 0.30 * harm * std::clamp (pct (kCpEven), 0.0, 1.0);
                v[vOdd]  = 0.30 * harm * std::clamp (pct (kCpOdd),  0.0, 1.0);
                v[vHBal] = std::clamp (pct (kCpHarmBalance), 0.0, 1.0);

                const double sat = std::clamp (pct (kCpSaturation), 0.0, 1.0) * I;
                v[vT1]     = std::min (1.0 - sat, 0.98);
                v[vH1]     = std::clamp (pct (kCpHardness), 0.0, 1.0);
                v[vD1]     = dbToLin (24.0 * std::clamp (pct (kCpDrive), 0.0, 1.0) * I);
                v[vSatMix] = std::clamp (pct (kCpSatMix), 0.0, 1.0) * std::min (1.0, 10.0 * sat);

                v[vKLow]  = CharacterToneProcessor::emphasisFor (pct (kCpLowChar)  * I);
                v[vKHigh] = CharacterToneProcessor::emphasisFor (pct (kCpHighChar) * I);

                const double rounding = std::clamp (pct (kCpRounding), 0.0, 1.0) * I;
                v[vRoundMix] = std::min (1.0, 8.0 * rounding);
                v[vThick]    = 0.35 * std::clamp (pct (kCpThickness), 0.0, 1.0) * I;

                o.transient.amount        = std::clamp (pct (kCpTransient), -1.0, 1.0) * I;
                o.transient.windowMs      = TransientProcessor::windowMsFor (pct (kCpAttackChar));
                o.transient.roundHeadroom = TransientProcessor::headroomFor (rounding);
                o.density                 = std::clamp (pct (kCpDensity), 0.0, 1.0) * I;
                break;
            }
        }

        v[vComp] = compensationFor (o);
    }

    //--------------------------------------------------------------------------
    //  Arayuzdeki SATURATION CURVE: giris -> cikis (telafi ve intensity dahil).
    //  level: sinyalin o anki tepe seviyesi.
    //--------------------------------------------------------------------------
    inline double displayCurve (const Voicing& o, double x, double level) noexcept
    {
        const double y = staticCore (o, x, level, o.displayGain);
        return x + o.v[vBlend] * (o.v[vComp] * y - x);
    }
}

//==============================================================================
class CharacterEngine
{
public:
    static constexpr int kMaxBlock = 64;

    //--------------------------------------------------------------------------
    void prepare (double sampleRate)
    {
        fs   = std::max (8000.0, sampleRate);
        os.prepare (Oversampler::factorForSampleRate (fs));
        fsOs = fs * (double) os.factor();

        co.smooth  = smoothCoef (12.0, fsOs);
        co.warmLow = onePoleCoef (240.0, fsOs);
        co.warmHf  = onePoleCoef (7000.0, fsOs);
        co.tapeHf  = onePoleCoef (2800.0, fsOs);
        co.tapeLp  = onePoleCoef (11000.0, fsOs);
        co.xfLow   = onePoleCoef (85.0, fsOs);
        co.drvLow  = onePoleCoef (180.0, fsOs);
        co.drvLp   = onePoleCoef (6500.0, fsOs);
        co.dcR2    = dcBlockerCoef (15.0, fsOs);
        co.resHp   = onePoleCoef (30.0, fsOs);
        co.tubeAtk = smoothCoef (8.0, fsOs);
        co.tubeRel = smoothCoef (150.0, fsOs);

        xfInc      = 1.0 / (0.040 * fsOs);          // tip gecisi: 40 ms
        onStep     = 1.0 / (0.020 * fs);            // ac / kapa: 20 ms
        matchCoef  = smoothCoef (800.0, fs);
        matchSmooth = smoothCoef (60.0, fs);

        for (auto& L : lanes)
        {
            for (int c = 0; c < 2; ++c)
                L.bump[c].design (75.0, 0.8, fsOs);
            L.transient.prepare (fs);
            L.density.prepare (fs);
            L.tone.prepare (fsOs);
            L.harm.prepare (fsOs);
            L.thick.prepare (fsOs);
        }
        reset();
    }

    void reset()
    {
        os.reset();
        for (auto& L : lanes) resetLane (L);
        active = 0;
        fading = false;
        xfade  = 0.0;
        onMix  = 0.0;
        idle   = true;
        matchLin = 1.0;
        for (int s = 0; s < 2; ++s) { matchIn[s] = matchOut[s] = kMatchInit; }
    }

    void setSettings (const CharacterSettings& s) noexcept { settings = s; }

    //--------------------------------------------------------------------------
    //  io: 1 veya 2 kanal, yerinde isler.
    //--------------------------------------------------------------------------
    void process (float* const* io, int numCh, int numSamples) noexcept
    {
        numCh = std::min (numCh, 2);
        if (numCh <= 0 || numSamples <= 0)
            return;

        for (int done = 0; done < numSamples; )
        {
            const int n = std::min (kMaxBlock, numSamples - done);
            float* p[2] = { io[0] + done, numCh > 1 ? io[1] + done : nullptr };
            processBlock (p, numCh, n);
            done += n;
        }
    }

    // Ust katman motoru bir sure calistirmayacaksa (bypass) cagrilir: bir sonraki
    // process() temiz durumdan, yumusak gecisle baslar. Ogrenilen seviyeler korunur.
    void suspend() noexcept
    {
        idle  = true;
        onMix = 0.0;
    }

    // okuma (GUI / test)
    bool   isActive() const noexcept { return ! idle; }
    float  matchGainDb() const noexcept { return (float) linToDb (matchLin); }
    int    oversamplingFactor() const noexcept { return os.factor(); }
    int    activeType() const noexcept { return lanes[active].type; }

private:
    //--------------------------------------------------------------------------
    struct Coefs
    {
        double smooth = 0.001;
        double warmLow = 0, warmHf = 0, tapeHf = 0, tapeLp = 0, xfLow = 0, drvLow = 0, drvLp = 0;
        double dcR2 = 0.999, resHp = 0.001, tubeAtk = 0, tubeRel = 0;
    };

    struct Lane
    {
        int    type = kCharWarm;
        double sm[kNumVoicing] = {};          // yumusatilmis
        double tg[kNumVoicing] = {};          // hedef
        TransientParams transientParams;
        double densityAmount = 0.0;

        CharacterSpec lastSpec;
        double lastIntensity = -1.0;

        // hazir tiplerin durumlari
        AdaaShaper   sh1[2], sh2[2];
        LowEmphasis  lowE[2];
        HighEmphasis highE[2];
        OnePole      lpA[2];
        OnePole      resHp1[2], resHp2[2];    // kalinti uzerinde 2 kutuplu yuksek geciren
        DcBlocker    dc[2];
        BandPass     bump[2];
        double       tubeEnv = 0.0;

        // CUSTOM zinciri
        TransientProcessor     transient;
        DensityProcessor       density;
        TransientRounder       rounder;
        CharacterToneProcessor tone;
        HarmonicGenerator      harm;
        SaturationProcessor    sat;
        ThicknessGenerator     thick;

        double gainPrev = 1.0, ceilPrev = 1.0;
        double gainBuf[kMaxBlock] = {};
        double ceilBuf[kMaxBlock] = {};
    };

    static constexpr double kMatchInit = 1.0e-9;

    void resetLane (Lane& L) noexcept
    {
        for (int c = 0; c < 2; ++c)
        {
            L.sh1[c].reset();  L.sh2[c].reset();
            L.lowE[c].reset(); L.highE[c].reset();
            L.lpA[c].reset();  L.dc[c].reset();
            L.resHp1[c].reset();  L.resHp2[c].reset();
            L.bump[c].reset();
        }
        L.tubeEnv = 0.0;
        L.transient.reset();
        L.density.reset();
        L.rounder.reset();
        L.tone.reset();
        L.harm.reset();
        L.sat.reset();
        L.thick.reset();
        L.gainPrev = 1.0;
        L.ceilPrev = 1.0;
        L.lastIntensity = -1.0;
    }

    void applyVoicing (Lane& L, const CharacterSpec& spec, double intensity, bool snap) noexcept
    {
        Voicing vo;
        character::computeVoicing (spec, intensity, vo);

        for (int i = 0; i < kNumVoicing; ++i)
        {
            L.tg[i] = vo.v[i];
            if (snap) L.sm[i] = vo.v[i];
        }
        L.transientParams = vo.transient;
        L.densityAmount   = vo.density;
        L.lastSpec        = spec;
        L.lastIntensity   = intensity;
    }

    //--------------------------------------------------------------------------
    //  Istenen karaktere gore seritleri guncelle. Tip degisirse capraz gecis baslat.
    //--------------------------------------------------------------------------
    void updateLanes (const CharacterSpec& want, double intensity) noexcept
    {
        Lane& cur = lanes[active];

        if (! fading && want.type != cur.type)
        {
            Lane& nxt = lanes[1 - active];
            nxt.type = std::clamp (want.type, 0, (int) kNumCharacterTypes - 1);
            resetLane (nxt);
            applyVoicing (nxt, want, intensity, true);
            fading = true;
            xfade  = 0.0;
            return;                               // eski serit kendi hedefleriyle sonuna kadar calar
        }

        Lane& tgt = fading ? lanes[1 - active] : cur;
        if (want.type == tgt.type && (std::abs (intensity - tgt.lastIntensity) > 0.0 || want != tgt.lastSpec))
            applyVoicing (tgt, want, intensity, false);
    }

    //--------------------------------------------------------------------------
    //  CUSTOM: vurus + yogunluk kazanci ve yuvarlama tavani (taban hizda)
    //--------------------------------------------------------------------------
    void computePreGain (Lane& L, const float* const* x, int numCh, int n) noexcept
    {
        const double invCh = 1.0 / (double) numCh;

        for (int i = 0; i < n; ++i)
        {
            double peak = 0.0, power = 0.0;
            for (int c = 0; c < numCh; ++c)
            {
                const double s = (double) x[c][i];
                peak   = std::max (peak, std::abs (s));
                power += s * s;
            }

            double ceiling;
            const double tDb = L.transient.tick (peak, L.transientParams, ceiling);
            const double dDb = L.density.tick (power * invCh, L.densityAmount);
            const double db  = tDb + dDb;

            L.gainBuf[i] = (std::abs (db) > 1.0e-7) ? dbToLin (db) : 1.0;
            L.ceilBuf[i] = ceiling;
        }
    }

    //--------------------------------------------------------------------------
    //  Bir seridi yuksek hizda calistir:  osIn -> out
    //--------------------------------------------------------------------------
    void runLane (Lane& L, int numCh, int n, double (*out)[kMaxBlock * Oversampler::kMaxFactor]) noexcept
    {
        using namespace shaper;

        const int    F      = os.factor();
        const double invF   = 1.0 / (double) F;
        const bool   custom = (L.type == kCharCustom);

        for (int b = 0; b < n; ++b)
        {
            const double g1    = custom ? L.gainBuf[b] : 1.0;
            const double c1    = custom ? L.ceilBuf[b] : 1.0;
            const double gStep = (g1 - L.gainPrev) * invF;
            const double cStep = (c1 - L.ceilPrev) * invF;

            for (int k = 0; k < F; ++k)
            {
                const int i = b * F + k;

                double* v = L.sm;
                for (int q = 0; q < kNumVoicing; ++q)
                    v[q] += co.smooth * (L.tg[q] - v[q]);

                const double x[2] = { osIn[0][i], numCh > 1 ? osIn[1][i] : 0.0 };
                double y[2] = { 0.0, 0.0 };

                const ShaperShape s1 = character::shape1 (v);
                const double d1 = v[vD1];

                switch (L.type)
                {
                    //----------------------------------------------------------
                    case kCharClean:
                    {
                        const double inv = 1.0 / d1;
                        for (int c = 0; c < numCh; ++c)
                            y[c] = L.sh1[c].process (d1 * x[c], s1) * inv;
                        break;
                    }

                    //----------------------------------------------------------
                    case kCharWarm:
                    {
                        const double b0  = v[vBias];
                        const double sb  = S (b0, s1);
                        const double inv = 1.0 / (d1 * SD (b0, s1));
                        for (int c = 0; c < numCh; ++c)
                        {
                            const double u = L.lowE[c].pre (x[c], v[vKPre], co.warmLow);
                            double w = (L.sh1[c].process (d1 * u + b0, s1) - sb) * inv;
                            w = L.lowE[c].post (w, v[vKPre], co.warmLow);
                            w -= v[vPost1] * (w - L.lpA[c].lp (w, co.warmHf));
                            y[c] = w;
                        }
                        break;
                    }

                    //----------------------------------------------------------
                    case kCharTape:
                    {
                        const double inv = 1.0 / d1;
                        for (int c = 0; c < numCh; ++c)
                        {
                            const double u = L.highE[c].pre (x[c], v[vKPre], co.tapeHf);
                            double w = L.sh1[c].process (d1 * u, s1) * inv;
                            w = L.highE[c].post (w, v[vKPre], co.tapeHf);
                            w += v[vPost2] * L.bump[c].process (w);
                            w += v[vPost1] * (L.lpA[c].lp (w, co.tapeLp) - w);
                            y[c] = w;
                        }
                        break;
                    }

                    //----------------------------------------------------------
                    case kCharTube:
                    {
                        double peak = 0.0;
                        for (int c = 0; c < numCh; ++c)
                            peak = std::max (peak, std::abs (d1 * x[c]));
                        L.tubeEnv += (peak > L.tubeEnv ? co.tubeAtk : co.tubeRel) * (peak - L.tubeEnv);

                        const double b0  = v[vBias] + v[vBiasEnv] * std::min (L.tubeEnv, 1.5);
                        const double sb  = S (b0, s1);
                        const double inv = 1.0 / (d1 * SD (b0, s1));
                        for (int c = 0; c < numCh; ++c)
                        {
                            y[c] = (L.sh1[c].process (d1 * x[c] + b0, s1) - sb) * inv;
                        }
                        break;
                    }

                    //----------------------------------------------------------
                    case kCharTransformer:
                    {
                        const double b0  = v[vBias];
                        const double sb  = S (b0, s1);
                        const double inv = 1.0 / (d1 * SD (b0, s1));
                        for (int c = 0; c < numCh; ++c)
                        {
                            const double u = L.lowE[c].pre (x[c], v[vKPre], co.xfLow);
                            const double w = (L.sh1[c].process (d1 * u + b0, s1) - sb) * inv;
                            y[c] = L.lowE[c].post (w, v[vKPre], co.xfLow);
                        }
                        break;
                    }

                    //----------------------------------------------------------
                    case kCharDrive:
                    {
                        const ShaperShape s2 = character::shape2 (v);
                        const double b0   = v[vBias];
                        const double sb   = S (b0, s1);
                        const double inv1 = 1.0 / SD (b0, s1);
                        const double d2   = v[vD2];
                        const double inv  = 1.0 / (d1 * d2);
                        const double kPre = -v[vKPre];
                        for (int c = 0; c < numCh; ++c)
                        {
                            const double u = L.lowE[c].pre (x[c], kPre, co.drvLow);
                            const double lin = d1 * u;
                            double w = (L.sh1[c].process (lin + b0, s1) - sb) * inv1;
                            w = lin + L.dc[c].process (w - lin, co.dcR2);       // 2. kademe ortali calissin
                            w = L.sh2[c].process (d2 * w, s2) * inv;
                            w = L.lowE[c].post (w, kPre, co.drvLow);
                            w += v[vPost1] * (L.lpA[c].lp (w, co.drvLp) - w);
                            y[c] = w;
                        }
                        break;
                    }

                    //----------------------------------------------------------
                    default:    // kCharCustom
                    {
                        const double g       = L.gainPrev + gStep * (double) (k + 1);
                        const double ceiling = L.ceilPrev + cStep * (double) (k + 1);
                        double w[2] = { x[0] * g, x[1] * g };

                        // vurus yuvarlama
                        if (v[vRoundMix] > 1.0e-6 || L.tg[vRoundMix] > 0.0)
                        {
                            const double invCeil = 1.0 / ceiling;
                            for (int c = 0; c < numCh; ++c)
                                w[c] += v[vRoundMix] * (L.rounder.process (c, w[c], ceiling, invCeil) - w[c]);
                        }

                        // ton: on vurgu
                        ToneParams tp;
                        tp.kLow = v[vKLow];  tp.kHigh = v[vKHigh];
                        double peak = 0.0;
                        for (int c = 0; c < numCh; ++c)
                        {
                            w[c] = L.tone.pre (c, w[c], tp);
                            peak = std::max (peak, std::abs (w[c]));
                        }

                        // harmonik ureteci (seviyeden bagimsiz)
                        L.harm.updateEnvelope (peak);
                        if (v[vEven] + v[vOdd] > 1.0e-7 || L.tg[vEven] + L.tg[vOdd] > 0.0)
                        {
                            HarmonicParams hp;
                            hp.even = v[vEven];  hp.odd = v[vOdd];  hp.balance = v[vHBal];
                            for (int c = 0; c < numCh; ++c)
                                w[c] += L.harm.process (c, w[c], hp);
                        }

                        // doyurucu (seviyeye bagli)
                        if (v[vSatMix] > 1.0e-7 || L.tg[vSatMix] > 0.0)
                        {
                            SaturationParams sp;
                            sp.drive = d1;  sp.mix = v[vSatMix];  sp.shape = s1;
                            for (int c = 0; c < numCh; ++c)
                                w[c] = L.sat.process (c, w[c], sp);
                        }

                        // ton: arka (ters) vurgu
                        for (int c = 0; c < numCh; ++c)
                            w[c] = L.tone.post (c, w[c], tp);

                        // alt bant harmonikleri
                        if (v[vThick] > 1.0e-7 || L.tg[vThick] > 0.0)
                        {
                            double lowPeak = 0.0;
                            for (int c = 0; c < numCh; ++c)
                                lowPeak = std::max (lowPeak, L.thick.split (c, w[c]));
                            L.thick.updateEnvelope (lowPeak);
                            for (int c = 0; c < numCh; ++c)
                                w[c] += v[vThick] * L.thick.process (c);
                        }

                        y[0] = w[0];
                        y[1] = w[1];
                        break;
                    }
                }

                // Ses alti temizlik: yalnizca dogrusal olmayan kalinti ( y - x ) suzulur.
                // y == x oldugunda (dogrusal bolge) sinyal bit duzeyinde degismez.
                const double blend = v[vBlend];
                const double comp  = v[vComp];
                for (int c = 0; c < numCh; ++c)
                {
                    const double residual = L.resHp2[c].hp (L.resHp1[c].hp (y[c] - x[c], co.resHp), co.resHp);
                    out[c][i] = x[c] + blend * (comp * (x[c] + residual) - x[c]);
                }
            }

            L.gainPrev = g1;
            L.ceilPrev = c1;
        }
    }

    //--------------------------------------------------------------------------
    void processBlock (float* const* io, int numCh, int n) noexcept
    {
        const double intensity = std::clamp ((double) settings.intensity, 0.0, 1.0);
        const bool   wantOn    = settings.on && intensity > 0.0;
        const double onTarget  = wantOn ? 1.0 : 0.0;
        const int    slot      = settings.slot != 0 ? 1 : 0;
        const CharacterSpec& want = settings.spec[slot];

        // Tam kapali: sinyale dokunma, islemci harcama
        if (! wantOn && onMix <= 0.0)
        {
            idle = true;
            return;
        }

        if (idle)
        {
            // uykudan uyan: temiz durumdan basla (acilis zaten yumusak gecisle)
            os.reset();
            for (auto& L : lanes) resetLane (L);
            active = 0;
            fading = false;
            xfade  = 0.0;
            lanes[0].type = std::clamp (want.type, 0, (int) kNumCharacterTypes - 1);
            applyVoicing (lanes[0], want, intensity, true);
            idle = false;
        }

        // Kapanirken (onTarget == 0) mevcut karakter son haliyle calmaya devam eder
        if (wantOn)
            updateLanes (want, intensity);

        // ---- kuru kopya
        const float* dryPtr[2] = { dry[0], dry[1] };
        for (int c = 0; c < numCh; ++c)
            std::copy (io[c], io[c] + n, dry[c]);

        // ---- CUSTOM seritleri icin on-kazanc
        if (lanes[active].type == kCharCustom)
            computePreGain (lanes[active], dryPtr, numCh, n);
        if (fading && lanes[1 - active].type == kCharCustom)
            computePreGain (lanes[1 - active], dryPtr, numCh, n);

        // ---- yukari ornekle
        const int F  = os.factor();
        const int nO = n * F;
        for (int c = 0; c < numCh; ++c)
            os.upsample (c, dry[c], n, osIn[c]);

        // ---- serit(ler)
        runLane (lanes[active], numCh, n, osA);
        if (fading)
        {
            runLane (lanes[1 - active], numCh, n, osB);
            for (int i = 0; i < nO; ++i)
            {
                const double t = std::min (1.0, xfade + xfInc * (double) (i + 1));
                for (int c = 0; c < numCh; ++c)
                    osA[c][i] += t * (osB[c][i] - osA[c][i]);
            }
            xfade += xfInc * (double) nO;
            if (xfade >= 1.0)
            {
                active = 1 - active;
                fading = false;
                xfade  = 0.0;
            }
        }

        // ---- asagi ornekle
        for (int c = 0; c < numCh; ++c)
            os.downsample (c, osA[c], n, wet[c]);

        // ---- seviye esitleme + ac / kapa gecisi
        const double invCh = 1.0 / (double) numCh;
        double matchTarget = 1.0;
        if (settings.levelMatch && matchOut[slot] > 1.0e-12)
            matchTarget = std::clamp (std::sqrt (matchIn[slot] / matchOut[slot]), 0.25, 4.0);

        for (int i = 0; i < n; ++i)
        {
            double inP = 0.0, outP = 0.0;
            for (int c = 0; c < numCh; ++c)
            {
                inP  += (double) dry[c][i] * (double) dry[c][i];
                outP += (double) wet[c][i] * (double) wet[c][i];
            }
            inP *= invCh;  outP *= invCh;

            if (inP > 1.0e-7)                     // sadece sinyal varken ogren
            {
                matchIn[slot]  += matchCoef * (inP  - matchIn[slot]);
                matchOut[slot] += matchCoef * (outP - matchOut[slot]);
            }
            matchLin += matchSmooth * (matchTarget - matchLin);

            onMix = (onTarget > onMix) ? std::min (onTarget, onMix + onStep)
                                       : std::max (onTarget, onMix - onStep);

            for (int c = 0; c < numCh; ++c)
            {
                const double d = (double) dry[c][i];
                io[c][i] = (float) (d + onMix * ((double) wet[c][i] * matchLin - d));
            }
        }
    }

    //--------------------------------------------------------------------------
    CharacterSettings settings;
    Oversampler os;
    Coefs co;
    Lane  lanes[2];

    double fs = 48000.0, fsOs = 192000.0;
    int    active = 0;
    bool   fading = false, idle = true;
    double xfade = 0.0, xfInc = 0.0;
    double onMix = 0.0, onStep = 0.001;
    double matchLin = 1.0, matchCoef = 0.0, matchSmooth = 0.0;
    double matchIn[2] = { kMatchInit, kMatchInit }, matchOut[2] = { kMatchInit, kMatchInit };

    float  dry[2][kMaxBlock] = {};
    float  wet[2][kMaxBlock] = {};
    double osIn[2][kMaxBlock * Oversampler::kMaxFactor] = {};
    double osA[2][kMaxBlock * Oversampler::kMaxFactor] = {};
    double osB[2][kMaxBlock * Oversampler::kMaxFactor] = {};
};

} // namespace scc

#endif // SCC_CHARACTER_ENGINE_H_INCLUDED
