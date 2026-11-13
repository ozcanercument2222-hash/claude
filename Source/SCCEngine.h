//==============================================================================
//  SCCEngine.h  -  SCC V6 sinyal zinciri (JUCE'dan bagimsiz)
//
//      giris ──► [ AdaptiveCompressor ]──┐
//            │                           ├─► kazanc ─► [ CharacterEngine ] ─► bypass ─► cikis
//            └─► [ ManualCompressor   ]──┘   (capraz gecis)                     ▲
//            └──────────────────────── kuru ────────────────────────────────────┘
//
//    * Iki kompresor de sinyale dokunmadan KAZANC uretir ve ikisi de surekli
//      calisir. Adaptive <-> Analog gecisi iki kazanc arasinda 15 ms'lik capraz
//      gecistir: tik / sicrama olmaz, geri donuldugunde adaptif analiz ve auto
//      gain "sicak"tir.
//    * CharacterEngine kompresyondan bagimsizdir ve her iki modda calisir.
//    * Bypass yumusak gecislidir (V5 ile ayni); bypass'tayken karakter motoru
//      islemci harcamaz.
//    * Spektrum analizi icin karakter motorunun giris ve cikisi kilitsiz
//      tamponlara yazilir (FFT arayuz is parcaciginda).
//
//  Ses is parcaciginda bellek ayirma, kilit, dosya islemi yoktur.
//==============================================================================
#ifndef SCC_ENGINE_H_INCLUDED
#define SCC_ENGINE_H_INCLUDED

#include "SCCDsp.h"
#include "SCCManualCompressor.h"
#include "SCCCharacterEngine.h"
#include "SCCSpectrumAnalyzer.h"

namespace scc
{

struct EngineSettings
{
    Settings          adaptive;             // amount, ratio, mode, autoGain, lowCut (bypass alani burada kullanilmaz)
    bool              analogMode = false;   // false = Adaptive, true = Analog (manuel)
    ManualSettings    manual;
    CharacterSettings character;
    bool              bypass = false;
};

//------------------------------------------------------------------------------
//  Ses is parcacigi -> arayuz. V5 telemetrisi + V6 ekleri.
//------------------------------------------------------------------------------
struct EngineTelemetry : Telemetry
{
    std::atomic<float> kneeDb       { 0.0f };    // etkin diz genisligi
    std::atomic<float> thresholdDb  { 0.0f };    // etkin threshold
    std::atomic<float> modeMix      { 0.0f };    // 0 = Adaptive ... 1 = Analog
    std::atomic<float> charInPeak   { 0.0f };    // karakter motoruna giren tepe (lineer, "son okumadan beri en buyuk")
    std::atomic<float> charMatchDb  { 0.0f };    // LEVEL MATCH'in uyguladigi kazanc
    std::atomic<bool>  charActive   { false };   // karakter motoru su an isliyor mu
};

//==============================================================================
class Engine
{
public:
    static constexpr int kChunk = CharacterEngine::kMaxBlock;

    EngineTelemetry telemetry;
    AnalyzerFifo    analyzerPre;      // karakter motorunun girisi
    AnalyzerFifo    analyzerPost;     // karakter motorunun cikisi

    //--------------------------------------------------------------------------
    void prepare (double sampleRate)
    {
        fs = std::max (8000.0, sampleRate);

        adaptive.prepare (fs);
        manual.prepare (fs);
        character.prepare (fs);

        modeStep   = (float) (1.0 / (0.015 * fs));                      // mod gecisi: 15 ms
        bypassCoef = (float) std::exp (-1.0 / (0.004 * fs));            // bypass gecisi ~25 ms (V5 ile ayni)
        samplesPerColumn = std::max (1, (int) std::lround (fs / (double) Telemetry::kScopeColsPerSec));
        reset();
    }

    void reset()
    {
        adaptive.reset();
        manual.reset();
        character.reset();
        modeMix   = -1.0f;        // ilk blokta hedefe oturt
        bypassMix = -1.0f;
        colCount  = 0;
        colIn     = 0.0f;
        lastGr    = 0.0f;
        characterSuspended = false;
    }

    void setSettings (const EngineSettings& s) noexcept { settings = s; }

    double sampleRate() const noexcept { return fs; }

    //--------------------------------------------------------------------------
    //  io : kanal basina bir isaretci dizisi (yerinde isler). 1 veya 2 kanal.
    //--------------------------------------------------------------------------
    void process (float* const* io, int numCh, int numSamples) noexcept
    {
        numCh = std::min (numCh, 2);
        if (numCh <= 0 || numSamples <= 0)
            return;

        const ScopedFlushDenormals flushDenormals;

        Settings a = settings.adaptive;
        a.bypass = false;
        adaptive.setSettings (a);
        manual.setSettings (settings.manual);
        character.setSettings (settings.character);

        const float modeTarget   = settings.analogMode ? 1.0f : 0.0f;
        const float bypassTarget = settings.bypass ? 1.0f : 0.0f;
        const float bypassStep   = 1.0f - bypassCoef;
        if (modeMix   < 0.0f) modeMix   = modeTarget;
        if (bypassMix < 0.0f) bypassMix = bypassTarget;

        float inPk[2]  = { 0.0f, 0.0f };
        float outPk[2] = { 0.0f, 0.0f };
        float charPk   = 0.0f;

        const float* dryPtr[2] = { dry[0], dry[1] };
        float*       wetPtr[2] = { wet[0], wet[1] };

        for (int done = 0; done < numSamples; )
        {
            const int n = std::min (kChunk, numSamples - done);

            // ---- giris: temizle + kuru kopya
            for (int c = 0; c < numCh; ++c)
            {
                const float* src = io[c] + done;
                for (int i = 0; i < n; ++i)
                {
                    float s = src[i];
                    if (! std::isfinite (s)) s = 0.0f;
                    dry[c][i] = s;
                    inPk[c] = std::max (inPk[c], std::abs (s));
                }
            }

            // ---- iki kompresor de kazancini hesaplar
            adaptive.processGain (dryPtr, numCh, n, gainA, grA);
            manual.processGain   (dryPtr, numCh, n, gainM, grM);

            // ---- mod capraz gecisi + kazanci uygula
            for (int i = 0; i < n; ++i)
            {
                modeMix = (modeTarget > modeMix) ? std::min (modeTarget, modeMix + modeStep)
                                                 : std::max (modeTarget, modeMix - modeStep);

                const float g = gainA[i] + (gainM[i] - gainA[i]) * modeMix;
                lastGr = grA[i] + (grM[i] - grA[i]) * modeMix;

                float det = 0.0f;
                for (int c = 0; c < numCh; ++c)
                {
                    const float w = dry[c][i] * g;
                    wet[c][i] = w;
                    charPk = std::max (charPk, std::abs (w));
                    det    = std::max (det, std::abs (dry[c][i]));
                }

                // osiloskop kolonu
                colIn = std::max (colIn, det);
                if (++colCount >= samplesPerColumn)
                {
                    colCount = 0;
                    const std::uint32_t h   = telemetry.scopeHead.load (std::memory_order_relaxed);
                    const std::uint32_t idx = h & (std::uint32_t) (Telemetry::kScopeSize - 1);
                    telemetry.scopeIn[idx].store (colIn,  std::memory_order_relaxed);
                    telemetry.scopeGr[idx].store (lastGr, std::memory_order_relaxed);
                    telemetry.scopeHead.store (h + 1u, std::memory_order_release);
                    colIn = 0.0f;
                }
            }

            // ---- karakter motoru (bypass tamamen oturmussa calistirma)
            analyzerPre.push (wetPtr, numCh, n);
            const bool fullyBypassed = (bypassTarget >= 1.0f && bypassMix >= 1.0f);
            if (fullyBypassed)
            {
                if (! characterSuspended) { character.suspend(); characterSuspended = true; }
            }
            else
            {
                characterSuspended = false;
                character.process (wetPtr, numCh, n);
            }
            analyzerPost.push (wetPtr, numCh, n);

            // ---- bypass: islenmis -> kuru yumusak gecis
            for (int i = 0; i < n; ++i)
            {
                bypassMix += (bypassTarget - bypassMix) * bypassStep;
                if (std::abs (bypassTarget - bypassMix) < 1.0e-4f) bypassMix = bypassTarget;

                for (int c = 0; c < numCh; ++c)
                {
                    const float yo = wet[c][i] + (dry[c][i] - wet[c][i]) * bypassMix;
                    io[c][done + i] = yo;
                    outPk[c] = std::max (outPk[c], std::abs (yo));
                }
            }

            done += n;
        }

        if (numCh == 1) { inPk[1] = inPk[0]; outPk[1] = outPk[0]; }

        // ---- telemetri
        const bool analog = modeMix >= 0.5f;
        telemetry.grDb.store      (lastGr, std::memory_order_relaxed);
        telemetry.attackMs.store  (analog ? manual.currentAttackMs()  : adaptive.currentAttackMs(),  std::memory_order_relaxed);
        telemetry.releaseMs.store (analog ? manual.currentReleaseMs() : adaptive.currentReleaseMs(), std::memory_order_relaxed);
        telemetry.makeupDb.store  (adaptive.currentMakeupDb() + (manual.currentMakeupDb() - adaptive.currentMakeupDb()) * modeMix,
                                   std::memory_order_relaxed);
        telemetry.modeMix.store   (modeMix, std::memory_order_relaxed);
        telemetry.kneeDb.store    (analog ? manual.currentKneeDb()
                                          : kneeFor (characterFor (settings.adaptive.mode), settings.adaptive.amountPct),
                                   std::memory_order_relaxed);
        telemetry.thresholdDb.store (analog ? std::clamp (settings.manual.thresholdDb, -60.0f, 0.0f)
                                            : Compressor::thresholdForAmount (settings.adaptive.amountPct),
                                     std::memory_order_relaxed);
        telemetry.charActive.store  (character.isActive(), std::memory_order_relaxed);
        telemetry.charMatchDb.store (character.matchGainDb(), std::memory_order_relaxed);
        Telemetry::atomicMax (telemetry.charInPeak, charPk);
        for (int c = 0; c < 2; ++c)
        {
            Telemetry::atomicMax (telemetry.inPeak[c],  inPk[c]);
            Telemetry::atomicMax (telemetry.outPeak[c], outPk[c]);
        }
    }

    // testler icin okuma
    float currentGrDb() const noexcept { return lastGr; }
    const AdaptiveCompressor& adaptiveCompressor() const noexcept { return adaptive; }
    const ManualCompressor&   manualCompressor()   const noexcept { return manual; }
    const CharacterEngine&    characterEngine()    const noexcept { return character; }

private:
    EngineSettings settings;

    AdaptiveCompressor adaptive;
    ManualCompressor   manual;
    CharacterEngine    character;

    double fs = 48000.0;
    float  modeMix = -1.0f, modeStep = 0.001f;
    float  bypassMix = -1.0f, bypassCoef = 0.0f;
    float  lastGr = 0.0f;
    bool   characterSuspended = false;

    int    samplesPerColumn = 480, colCount = 0;
    float  colIn = 0.0f;

    float  dry[2][kChunk] = {};
    float  wet[2][kChunk] = {};
    float  gainA[kChunk] = {}, gainM[kChunk] = {};
    float  grA[kChunk] = {},   grM[kChunk] = {};
};

} // namespace scc

#endif // SCC_ENGINE_H_INCLUDED
