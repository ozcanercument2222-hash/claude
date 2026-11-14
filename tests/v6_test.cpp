// SCC V6 DSP testi - JUCE gerektirmez. PLUGIN DERLEMESINE DAHIL DEGILDIR.
//
//   macOS / Linux : g++ -O2 -std=c++17 -DSCC_BUILD_DSP_TEST tests/v6_test.cpp -o v6_test && ./v6_test
//   Windows       : cmake -B build -DSCC_BUILD_TESTS=ON
//                   cmake --build build --config Release --target SCC_V6Test
//                   build\Release\SCC_V6Test.exe
//
// V5 testi (tests/dsp_test.cpp) degismeden durur ve V5 davranisinin korundugunu
// dogrular. Bu dosya V6 ile gelen her seyi olcer:
//   oversampling, Analog Mode, mod gecisleri, Character Engine (7 tip),
//   Character Designer'in 15 kontrolu, aliasing, tik / zipper, seviye esitleme,
//   A/B, dayaniklilik, gercek zaman guvenligi (bellek ayirma yok) ve islemci yuku.
#ifdef SCC_BUILD_DSP_TEST

#include "../Source/SCCEngine.h"
#include "../Source/SCCCharacterAnalysis.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <vector>

using namespace scc;

//==============================================================================
//  Bellek ayirma sayaci: ses yolunda new / malloc cagrilmadigini kanitlamak icin
//==============================================================================
static long gAllocCount = 0;

void* operator new (std::size_t n)                { ++gAllocCount; if (void* p = std::malloc (n ? n : 1)) return p; throw std::bad_alloc(); }
void* operator new[] (std::size_t n)              { ++gAllocCount; if (void* p = std::malloc (n ? n : 1)) return p; throw std::bad_alloc(); }
void  operator delete (void* p) noexcept          { std::free (p); }
void  operator delete[] (void* p) noexcept        { std::free (p); }
void  operator delete (void* p, std::size_t) noexcept   { std::free (p); }
void  operator delete[] (void* p, std::size_t) noexcept { std::free (p); }

//==============================================================================
static int failures = 0;
#define CHECK(cond, ...)                                          \
    do { if (cond) std::printf ("  [OK]   "); else { std::printf ("  [FAIL] "); ++failures; } \
         std::printf (__VA_ARGS__); std::printf ("\n"); } while (0)

static const double kFs = 48000.0;
static const double kPi = 3.14159265358979323846;

struct Rng
{
    std::uint32_t s = 12345u;
    float next() { s = s * 1664525u + 1013904223u; return (float) ((s >> 8) & 0xFFFF) / 32768.0f - 1.0f; }
};

static double rmsDb (const std::vector<float>& v, size_t a, size_t b)
{
    double e = 0; for (size_t i = a; i < b; ++i) e += (double) v[i] * v[i];
    return 10.0 * std::log10 (e / (double) (b - a) + 1e-20);
}

static std::vector<float> sine (double freq, double amp, double seconds, double fs = kFs)
{
    std::vector<float> v ((size_t) (seconds * fs));
    for (size_t i = 0; i < v.size(); ++i) v[i] = (float) (amp * std::sin (2.0 * kPi * freq * (double) i / fs));
    return v;
}

// Tek frekansta genlik (Blackman-Harris pencereli korelasyon)
static double ampAt (const std::vector<float>& x, size_t a, size_t b, double f, double fs = kFs)
{
    double re = 0, im = 0, wsum = 0;
    const double N = (double) (b - a);
    for (size_t i = a; i < b; ++i)
    {
        const double t = (double) (i - a) / N;
        const double w = 0.35875 - 0.48829 * std::cos (2 * kPi * t) + 0.14128 * std::cos (4 * kPi * t) - 0.01168 * std::cos (6 * kPi * t);
        re += x[i] * w * std::cos (2 * kPi * f * (double) i / fs);
        im += x[i] * w * std::sin (2 * kPi * f * (double) i / fs);
        wsum += w;
    }
    return 2.0 * std::sqrt (re * re + im * im) / wsum;
}

// En buyuk ikinci fark: yumusak bir sinyalde kucuktur, tik (sicrama) varsa buyur
static double maxSecondDiff (const std::vector<float>& x, size_t a, size_t b)
{
    double m = 0;
    for (size_t i = a + 1; i + 1 < b; ++i)
        m = std::max (m, std::abs ((double) x[i + 1] - 2.0 * x[i] + x[i - 1]));
    return m;
}

// Ses benzeri sinyal (V5 testiyle ayni): glottal darbe + 3 formant, hece zarflari, sessizlikler
static std::vector<float> makeVoice (double seconds, std::uint32_t seed = 1)
{
    const size_t N = (size_t) (seconds * kFs);
    std::vector<float> out (N, 0.0f);
    Rng rng; rng.s = seed;

    struct Res { double a1, a2, y1 = 0, y2 = 0; };
    auto mk = [] (double f, double bw) {
        const double r = std::exp (-kPi * bw / kFs);
        return Res { 2.0 * r * std::cos (2.0 * kPi * f / kFs), -r * r };
    };
    Res f1 = mk (700, 90), f2 = mk (1250, 110), f3 = mk (2600, 160);

    struct Seg { size_t a, b; double amp; };
    std::vector<Seg> segs;

    size_t n = 0;
    double phase = 0.0;
    while (n < N)
    {
        const double sylSec = 0.16 + 0.12 * (rng.next() * 0.5 + 0.5);
        const double gapSec = 0.03 + 0.12 * (rng.next() * 0.5 + 0.5);
        const double peakDb = -24.0 + 18.0 * (rng.next() * 0.5 + 0.5);
        const double f0     = 110.0 + 60.0 * (rng.next() * 0.5 + 0.5);
        const size_t sylN = (size_t) (sylSec * kFs), gapN = (size_t) (gapSec * kFs);
        const size_t start = n;

        for (size_t i = 0; i < sylN && n < N; ++i, ++n)
        {
            const double t = (double) i / (double) sylN;
            const double env = std::sin (kPi * t) * std::sin (kPi * t);
            phase += f0 / kFs;
            double exc = 0.0;
            if (phase >= 1.0) { phase -= 1.0; exc = 1.0; }
            const double v1 = exc + f1.a1 * f1.y1 + f1.a2 * f1.y2; f1.y2 = f1.y1; f1.y1 = v1;
            const double v2 = v1 * 0.5 + f2.a1 * f2.y1 + f2.a2 * f2.y2; f2.y2 = f2.y1; f2.y1 = v2;
            const double v3 = v2 * 0.5 + f3.a1 * f3.y1 + f3.a2 * f3.y2; f3.y2 = f3.y1; f3.y1 = v3;
            out[n] = (float) ((v1 + v2 + v3) * env);
        }
        segs.push_back ({ start, n, std::pow (10.0, peakDb / 20.0) });
        for (size_t i = 0; i < gapN && n < N; ++i, ++n) out[n] = 0.0005f * rng.next();
    }

    for (auto& sg : segs)
    {
        float m = 1e-12f;
        for (size_t i = sg.a; i < sg.b; ++i) m = std::max (m, std::abs (out[i]));
        const float g = (float) sg.amp / m;
        for (size_t i = sg.a; i < sg.b; ++i) out[i] *= g;
        const size_t burst = (size_t) (0.012 * kFs);
        for (size_t i = 0; i < burst && sg.a + i < sg.b; ++i)
            out[sg.a + i] += (float) (0.5 * sg.amp * rng.next() * (1.0 - (double) i / (double) burst));
    }
    return out;
}

//------------------------------------------------------------------------------
template <typename Proc>
static void runStereo (Proc& p, std::vector<float>& L, std::vector<float>& R, int block = 256)
{
    for (size_t i = 0; i < L.size(); i += (size_t) block)
    {
        const int len = (int) std::min ((size_t) block, L.size() - i);
        float* ch[2] = { L.data() + i, R.data() + i };
        p.process (ch, 2, len);
    }
}

template <typename Proc>
static void runMono (Proc& p, std::vector<float>& x, int block = 256)
{
    for (size_t i = 0; i < x.size(); i += (size_t) block)
    {
        const int len = (int) std::min ((size_t) block, x.size() - i);
        float* ch[1] = { x.data() + i };
        p.process (ch, 1, len);
    }
}

static CharacterSettings charOn (int type, double intensity)
{
    CharacterSettings cs;
    cs.on = true;
    cs.intensity = (float) intensity;
    cs.spec[0].type = type;
    cs.spec[1].type = type;
    return cs;
}

// Designer'in butun kontrolleri notr (hicbir sey yapmaz) olan CUSTOM karakter
static CharacterSpec blankCustom()
{
    CharacterSpec s;
    s.type = kCharCustom;
    for (int i = 0; i < kNumCustomParams; ++i) s.custom[i] = 0.0f;
    s.custom[kCpSatMix] = 100.0f;
    s.custom[kCpAttackChar] = 50.0f;
    return s;
}

// Karakter motorundan mono sinyal gecir
static std::vector<float> throughCharacter (const CharacterSettings& cs, std::vector<float> x, double fs = kFs, int block = 256)
{
    CharacterEngine e;
    e.prepare (fs);
    e.setSettings (cs);
    runMono (e, x, block);
    return x;
}

static std::vector<float> throughCustom (const CharacterSpec& spec, std::vector<float> x, double intensity = 1.0)
{
    CharacterSettings cs;
    cs.on = true;
    cs.intensity = (float) intensity;
    cs.spec[0] = spec;
    return throughCharacter (cs, std::move (x));
}

//==============================================================================
int main()
{
    //--------------------------------------------------------------------------
    std::printf ("=== 1) Oversampler (minimum fazli polifaz IIR, 4x) ===\n");
    {
        const int N = 1 << 15;
        double worstPass = 0, worstImage = -300, worstFold = -300;

        for (double f : { 50.0, 1000.0, 5000.0, 10000.0, 15000.0, 18000.0, 20000.0 })
        {
            Oversampler os; os.prepare (4);
            std::vector<float> in ((size_t) N), out ((size_t) N);
            std::vector<double> up ((size_t) (4 * N));
            for (int i = 0; i < N; ++i) in[(size_t) i] = (float) std::sin (2 * kPi * f * i / kFs);
            os.upsample (0, in.data(), N, up.data());
            os.downsample (0, up.data(), N, out.data());

            worstPass = std::max (worstPass, std::abs (20 * std::log10 (ampAt (out, (size_t) N / 2, (size_t) N, f))));

            std::vector<float> upf (up.begin(), up.end());
            for (double fi : { kFs - f, kFs + f, 2 * kFs - f })
                worstImage = std::max (worstImage, 20 * std::log10 (ampAt (upf, (size_t) (2 * N), (size_t) (4 * N), fi, 4 * kFs) + 1e-30));
        }
        for (double f : { 30000.0, 50000.0, 70000.0, 90000.0 })
        {
            Oversampler os; os.prepare (4);
            std::vector<double> up ((size_t) (4 * N));
            std::vector<float> out ((size_t) N);
            for (int i = 0; i < 4 * N; ++i) up[(size_t) i] = std::sin (2 * kPi * f * i / (4 * kFs));
            os.downsample (0, up.data(), N, out.data());
            worstFold = std::max (worstFold, rmsDb (out, (size_t) N / 2, (size_t) N) + 3.01);
        }

        Oversampler os; os.prepare (4);
        std::vector<float> imp ((size_t) N, 0.0f), out ((size_t) N);
        std::vector<double> up ((size_t) (4 * N));
        imp[0] = 1.0f;
        os.upsample (0, imp.data(), N, up.data());
        os.downsample (0, up.data(), N, out.data());
        double s0 = 0, s1 = 0;
        for (int i = 0; i < 2000; ++i) { s0 += out[(size_t) i]; s1 += i * (double) out[(size_t) i]; }
        const double delay = s1 / s0;

        std::printf ("  gecis bandi duzlugu (20 Hz - 20 kHz) : +-%.4f dB\n", worstPass);
        std::printf ("  yukari orneklemede en buyuk goruntu   : %.1f dB\n", worstImage);
        std::printf ("  asagi orneklemede bant disi katlanma  : %.1f dB\n", worstFold);
        std::printf ("  dusuk frekans gecikmesi               : %.2f ornek (%.3f ms)\n", delay, 1000.0 * delay / kFs);
        CHECK (worstPass < 0.01,   "20 kHz'e kadar duz (< 0.01 dB)");
        CHECK (worstImage < -95.0, "goruntu bastirmasi > 95 dB");
        CHECK (worstFold < -95.0,  "katlanma bastirmasi > 95 dB");
        CHECK (delay < 6.0,        "gecikme < 6 ornek (bildirilmez; canli kullanima uygun)");
        CHECK (Oversampler::factorForSampleRate (44100.0) == 4 && Oversampler::factorForSampleRate (96000.0) == 2,
               "44.1/48 kHz'de 4x, 88.2 kHz ve ustunde 2x");
    }

    //--------------------------------------------------------------------------
    std::printf ("\n=== 2) V5 korunuyor: V6 motoru (Adaptive, Character kapali) == V5 kompresor ===\n");
    {
        auto v = makeVoice (8.0);
        for (int mode = 0; mode < 3; ++mode)
        {
            Settings s; s.amountPct = 70; s.ratio = 6; s.mode = mode; s.autoGain = true; s.lowCut = (mode == 1);

            std::vector<float> L5 = v, R5 = v;
            Compressor c5; c5.prepare (kFs); c5.setSettings (s);
            runStereo (c5, L5, R5);

            std::vector<float> L6 = v, R6 = v;
            Engine e; e.prepare (kFs);
            EngineSettings es; es.adaptive = s;
            e.setSettings (es);
            runStereo (e, L6, R6);

            double maxErr = 0;
            for (size_t i = 0; i < v.size(); ++i) maxErr = std::max (maxErr, (double) std::abs (L5[i] - L6[i]));
            const char* names[3] = { "Smooth", "Balanced", "Hard" };
            CHECK (maxErr < 2.0e-6, "%-8s : V5 ile en buyuk fark %.2e (ayni ses)", names[mode], maxErr);

            // Fark hangi adimda olusuyor? (bilgi amacli: V5 process -> processGain -> + flush-to-zero -> Engine)
            auto gainPath = [&v, &s] (bool flush)
            {
                Compressor c; c.prepare (kFs); c.setSettings (s);
                std::vector<float> out (v.size()), g (256);
                for (size_t i = 0; i < v.size(); i += 256)
                {
                    const int len = (int) std::min ((size_t) 256, v.size() - i);
                    const float* in[2] = { v.data() + i, v.data() + i };
                    if (flush) { const ScopedFlushDenormals fz; c.processGain (in, 2, len, g.data()); }
                    else       {                                c.processGain (in, 2, len, g.data()); }
                    for (int k = 0; k < len; ++k) out[i + (size_t) k] = v[i + (size_t) k] * g[(size_t) k];
                }
                return out;
            };
            auto diff = [] (const std::vector<float>& a, const std::vector<float>& b, long& first)
            {
                double m = 0; first = -1;
                for (size_t i = 0; i < a.size(); ++i)
                {
                    const double d = std::abs ((double) a[i] - (double) b[i]);
                    if (d > 1.0e-6 && first < 0) first = (long) i;
                    m = std::max (m, d);
                }
                return m;
            };
            const auto G = gainPath (false), Gz = gainPath (true);
            long f1, f2, f3;
            const double d1 = diff (L5, G, f1), d2 = diff (G, Gz, f2), d3 = diff (Gz, L6, f3);
            std::printf ("           adimlar: process->processGain %.2e (ilk %ld), +flush-to-zero %.2e (ilk %ld), ->Engine %.2e (ilk %ld)\n",
                         d1, f1, d2, f2, d3, f3);
        }
    }

    //--------------------------------------------------------------------------
    std::printf ("\n=== 3) Analog Mode: statik egri (DC seviye, diz 0) ===\n");
    {
        auto grFor = [] (float levelDb, float thr, float ratio, float knee)
        {
            ManualCompressor m; m.prepare (kFs);
            ManualSettings s; s.thresholdDb = thr; s.ratio = ratio; s.kneeDb = knee; s.attackMs = 1.0f; s.releaseMs = 50.0f;
            m.setSettings (s);
            const int N = (int) kFs;
            std::vector<float> x ((size_t) N, std::pow (10.0f, levelDb / 20.0f)), g ((size_t) N), gr ((size_t) N);
            const float* in[1] = { x.data() };
            m.processGain (in, 1, N, g.data(), gr.data());
            return gr.back();
        };

        struct Case { float level, thr, ratio; };
        const Case cases[] = { { -10, -20, 1.5f }, { -10, -20, 2 }, { -10, -30, 4 }, { -6, -40, 8 }, { 0, -60, 20 }, { -10, -20, kManualLimitRatio } };
        for (const auto& c : cases)
        {
            const float slope    = c.ratio >= kManualLimitRatio ? 1.0f : 1.0f - 1.0f / c.ratio;
            const float expected = (c.level - c.thr) * slope;
            const float got      = grFor (c.level, c.thr, c.ratio, 0.0f);
            if (c.ratio >= kManualLimitRatio)
                CHECK (std::abs (got - expected) < 0.05f, "LIMIT       thr %5.0f : GR beklenen %.2f dB, olculen %.2f dB", c.thr, expected, got);
            else
                CHECK (std::abs (got - expected) < 0.05f, "ratio %4.1f  thr %5.0f : GR beklenen %.2f dB, olculen %.2f dB", c.ratio, c.thr, expected, got);
        }

        // diz: sinyal tam esikteyken GR = egim * W / 8
        for (float knee : { 6.0f, 12.0f, 24.0f })
        {
            const float got = grFor (-20.0f, -20.0f, 4.0f, knee), expected = 0.75f * knee / 8.0f;
            CHECK (std::abs (got - expected) < 0.03f, "knee %4.1f dB, esikte : GR beklenen %.2f dB, olculen %.2f dB", knee, expected, got);
        }
        CHECK (grFor (-30.0f, -20.0f, 4.0f, 0.0f) < 1.0e-4f, "esigin altinda kompresyon yok");
    }

    //--------------------------------------------------------------------------
    std::printf ("\n=== 4) Analog Mode: attack / release sureleri ayarla birebir ===\n");
    {
        auto measure = [] (float attackMs, float releaseMs, double& atkOut, double& relOut)
        {
            ManualCompressor m; m.prepare (kFs);
            ManualSettings s; s.thresholdDb = -20; s.ratio = 4; s.kneeDb = 0; s.attackMs = attackMs; s.releaseMs = releaseMs;
            m.setSettings (s);

            const int pre = (int) (0.2 * kFs), on = (int) (3.0 * kFs), off = (int) (4.0 * kFs);
            std::vector<float> x ((size_t) (pre + on + off), 0.001f);
            for (int i = pre; i < pre + on; ++i) x[(size_t) i] = 1.0f;            // -60 dB -> 0 dB -> -60 dB
            std::vector<float> g (x.size()), gr (x.size());
            const float* in[1] = { x.data() };
            m.processGain (in, 1, (int) x.size(), g.data(), gr.data());

            const double full = 15.0;                                            // (0 - -20) * 0.75
            atkOut = relOut = -1;
            for (int i = pre; i < pre + on; ++i)
                if (gr[(size_t) i] >= full * (1.0 - std::exp (-1.0))) { atkOut = 1000.0 * (i - pre) / kFs; break; }
            for (int i = pre + on; i < (int) x.size(); ++i)
                if (gr[(size_t) i] <= full * std::exp (-1.0)) { relOut = 1000.0 * (i - pre - on) / kFs; break; }
        };

        struct Case { float a, r; };
        for (const auto& c : { Case { 0.1f, 10.0f }, Case { 1.0f, 50.0f }, Case { 10.0f, 120.0f }, Case { 30.0f, 400.0f }, Case { 100.0f, 1000.0f } })
        {
            double a, r; measure (c.a, c.r, a, r);
            // release, attack yumusaticisindan da gectigi icin ayarlanan + attack kadar surer
            const bool ok = std::abs (a - c.a) <= std::max (0.05, 0.12 * c.a) && std::abs (r - (c.r + c.a)) <= 0.12 * (c.r + c.a);
            CHECK (ok, "attack %6.1f ms -> olculen %6.2f ms    release %6.0f ms -> olculen %7.1f ms", c.a, a, c.r, r);
        }
    }

    //--------------------------------------------------------------------------
    std::printf ("\n=== 5) Analog Mode: makeup, auto gain, low cut, stereo baglanti ===\n");
    {
        // elle makeup: esigin altinda kazanc = makeup
        {
            ManualCompressor m; m.prepare (kFs);
            ManualSettings s; s.thresholdDb = 0; s.ratio = 4; s.makeupDb = 9.0f; m.setSettings (s);
            auto x = sine (1000, 0.05, 1.0);
            std::vector<float> g (x.size());
            const float* in[1] = { x.data() };
            m.processGain (in, 1, (int) x.size(), g.data());
            CHECK (std::abs (20.0f * std::log10 (g.back()) - 9.0f) < 0.02f, "makeup 9 dB -> uygulanan %.2f dB", 20.0f * std::log10 (g.back()));
        }
        // auto gain (istege bagli): cikis yuksekligi girisle ayni
        {
            auto v = makeVoice (10.0);
            Engine e; e.prepare (kFs);
            EngineSettings es; es.analogMode = true;
            es.manual.thresholdDb = -30; es.manual.ratio = 8; es.manual.kneeDb = 6; es.manual.attackMs = 5; es.manual.releaseMs = 120; es.manual.autoGain = true;
            e.setSettings (es);
            std::vector<float> L = v, R = v;
            runStereo (e, L, R);
            const double in = rmsDb (v, (size_t) (4 * kFs), v.size()), out = rmsDb (L, (size_t) (4 * kFs), L.size());
            CHECK (std::abs (out - in) < 1.5, "auto gain acik: giris %.1f dB, cikis %.1f dB (ayni yukseklik)", in, out);

            es.manual.autoGain = false;
            Engine e2; e2.prepare (kFs); e2.setSettings (es);
            std::vector<float> L2 = v, R2 = v;
            runStereo (e2, L2, R2);
            const double out2 = rmsDb (L2, (size_t) (4 * kFs), L2.size());
            CHECK (out2 < in - 3.0, "auto gain kapali: cikis %.1f dB (kompresyon kadar dusuk)", out2);
        }
        // low cut: 40 Hz dedektoru tetiklemez
        {
            auto grAt = [] (double f, bool lowCut)
            {
                ManualCompressor m; m.prepare (kFs);
                ManualSettings s; s.thresholdDb = -20; s.ratio = 4; s.kneeDb = 0; s.attackMs = 5; s.releaseMs = 200; s.lowCut = lowCut;
                m.setSettings (s);
                auto x = sine (f, 0.316, 2.0);
                std::vector<float> g (x.size()), gr (x.size());
                const float* in[1] = { x.data() };
                m.processGain (in, 1, (int) x.size(), g.data(), gr.data());
                return gr.back();
            };
            CHECK (grAt (40, false) > 5.0f && grAt (40, true) < 0.5f, "low cut: 40 Hz kapali %.2f dB, acik %.2f dB", grAt (40, false), grAt (40, true));
            CHECK (std::abs (grAt (1000, false) - grAt (1000, true)) < 0.3f, "low cut 1 kHz'i etkilemiyor");
        }
        // stereo baglanti: sadece sol kanalda yuksek sinyal -> sag kanal da ayni kazanci alir
        {
            Engine e; e.prepare (kFs);
            EngineSettings es; es.analogMode = true;
            es.manual.thresholdDb = -30; es.manual.ratio = 8; es.manual.kneeDb = 0;
            e.setSettings (es);
            std::vector<float> L = sine (500, 0.5, 1.0), R = sine (700, 0.01, 1.0);
            const auto L0 = L, R0 = R;
            runStereo (e, L, R);
            const double gL = rmsDb (L, 24000, 48000) - rmsDb (L0, 24000, 48000);
            const double gR = rmsDb (R, 24000, 48000) - rmsDb (R0, 24000, 48000);
            CHECK (std::abs (gL - gR) < 0.1 && gL < -10.0, "stereo bagli: sol %.2f dB, sag %.2f dB (goruntu kaymaz)", gL, gR);
        }
    }

    //--------------------------------------------------------------------------
    std::printf ("\n=== 6) Mod gecisi (Adaptive <-> Analog) ve bypass: tik yok ===\n");
    {
        const auto ref = sine (440, 0.5, 2.0);
        Engine e; e.prepare (kFs);
        EngineSettings es;
        es.adaptive.amountPct = 100; es.adaptive.ratio = 18; es.adaptive.mode = Hard; es.adaptive.autoGain = false;
        es.manual.thresholdDb = -6; es.manual.ratio = 2; es.manual.kneeDb = 0;      // cok farkli iki ayar
        std::vector<float> L = ref, R = ref;
        const size_t sw1 = (size_t) (0.8 * kFs), sw2 = (size_t) (1.4 * kFs);
        for (size_t i = 0; i < L.size(); i += 64)
        {
            es.analogMode = (i >= sw1 && i < sw2);
            e.setSettings (es);
            float* ch[2] = { L.data() + i, R.data() + i };
            e.process (ch, 2, 64);
        }
        const double steady = maxSecondDiff (ref, 0, ref.size());
        const double d1 = maxSecondDiff (L, sw1 - 200, sw1 + 3000), d2 = maxSecondDiff (L, sw2 - 200, sw2 + 3000);
        const double before = rmsDb (L, sw1 - 4800, sw1), during = rmsDb (L, sw2 - 4800, sw2);
        std::printf ("  Adaptive cikis %.1f dB, Analog cikis %.1f dB (gecis gercekten farkli iki kompresor arasinda)\n", before, during);
        CHECK (std::abs (before - during) > 6.0, "iki mod farkli kazanc uretiyor");
        CHECK (d1 < steady * 1.5 && d2 < steady * 1.5, "gecislerde sicrama yok (ikinci fark %.5f / %.5f, sinusun kendisi %.5f)", d1, d2, steady);

        // bypass (karakter acikken)
        Engine b; b.prepare (kFs);
        EngineSettings bs; bs.adaptive.amountPct = 80; bs.character = charOn (kCharTube, 1.0);
        std::vector<float> A = ref, B = ref;
        const size_t by1 = (size_t) (1.0 * kFs), by2 = (size_t) (1.5 * kFs);
        for (size_t i = 0; i < A.size(); i += 64)
        {
            bs.bypass = (i >= by1 && i < by2);
            b.setSettings (bs);
            float* ch[2] = { A.data() + i, B.data() + i };
            b.process (ch, 2, 64);
        }
        double maxErr = 0;
        for (size_t i = by1 + (size_t) (0.3 * kFs); i < by2; ++i) maxErr = std::max (maxErr, (double) std::abs (A[i] - ref[i]));
        CHECK (maxErr < 1.0e-6, "bypass acikken cikis = giris (fark %.1e)", maxErr);
        // islenmis sinyal harmonik icerdigi icin kendi ikinci farki sinusunkinden buyuk: onu olc
        const double wetSteady = maxSecondDiff (A, by1 - 9600, by1 - 100);
        const double db1 = maxSecondDiff (A, by1 - 100, by1 + 4000), db2 = maxSecondDiff (A, by2 - 100, by2 + 4000);
        CHECK (db1 < wetSteady * 1.5 + 1e-4 && db2 < wetSteady * 1.5 + 1e-4, "bypass gecisleri tiksiz (%.5f / %.5f, islenmis sinyal %.5f)", db1, db2, wetSteady);
    }

    //--------------------------------------------------------------------------
    std::printf ("\n=== 7) Character OFF / %%0: hicbir renklendirme yok (bit duzeyinde ayni) ===\n");
    {
        auto v = makeVoice (3.0);
        auto runEngine = [&v] (const CharacterSettings& cs)
        {
            Engine e; e.prepare (kFs);
            EngineSettings es; es.adaptive.amountPct = 60; es.character = cs;
            e.setSettings (es);
            std::vector<float> L = v, R = v;
            runStereo (e, L, R);
            return L;
        };
        const auto base = runEngine (CharacterSettings());

        CharacterSettings off = charOn (kCharDrive, 1.0); off.on = false;
        CharacterSettings zero = charOn (kCharDrive, 0.0);
        CharacterSettings on = charOn (kCharDrive, 0.5);

        auto maxDiff = [&base] (const std::vector<float>& x) { double m = 0; for (size_t i = 0; i < x.size(); ++i) m = std::max (m, (double) std::abs (x[i] - base[i])); return m; };
        CHECK (maxDiff (runEngine (off))  == 0.0, "Character OFF  : cikis birebir ayni");
        CHECK (maxDiff (runEngine (zero)) == 0.0, "Character %%0   : cikis birebir ayni");
        CHECK (maxDiff (runEngine (on))   > 0.01, "Character acik : ses gercekten degisiyor (fark %.3f)", maxDiff (runEngine (on)));

        // Dogrusal bolgede (CLEAN, dizin altinda) motor acikken bile genlik ve harmonikler degismez
        CharacterProbe probe;
        CharacterSpec clean; clean.type = kCharClean;
        const auto sig = probe.measure (clean, 0.5, kFs, 1000.0, 0.1);
        CHECK (sig.thdPercent < 1.0e-4 && std::abs (sig.fundamentalGainDb) < 0.01, "CLEAN, dizin altinda: THD %.6f %%, kazanc %+.4f dB (seffaf)", sig.thdPercent, sig.fundamentalGainDb);
    }

    //--------------------------------------------------------------------------
    std::printf ("\n=== 8) Karakter tipleri: her biri farkli bir DSP davranisi ===\n");
    {
        CharacterProbe probe;
        auto sig = [&probe] (int type, double I, double f, double level)
        {
            CharacterSpec s; s.type = type;
            return probe.measure (s, I, kFs, f, level);
        };

        std::printf ("  tip           THD%%   H2 dBc  H3 dBc   (220 Hz, -12 dBFS, CHARACTER %%100)\n");
        for (int t = 0; t < kCharCustom; ++t)
        {
            const auto s = sig (t, 1.0, 220, 0.25);
            std::printf ("  %-12s %6.2f  %6.1f  %6.1f\n", characterTypeName (t), s.thdPercent, std::max (s.levelDb[1], -120.0), std::max (s.levelDb[2], -120.0));
        }

        // CLEAN: dizin altinda sifir bozulma, ustunde tepe yuvarlama
        CHECK (sig (kCharClean, 1.0, 220, 0.1).thdPercent < 1.0e-4 && sig (kCharClean, 1.0, 220, 0.7).thdPercent > 1.0,
               "CLEAN       : alcak seviyede THD %.5f %%, tepede %.2f %% (yalnizca tepeleri yuvarlar)",
               sig (kCharClean, 1.0, 220, 0.1).thdPercent, sig (kCharClean, 1.0, 220, 0.7).thdPercent);

        // WARM: cift harmonik baskin, alt frekanslarda daha yogun
        {
            const auto lo = sig (kCharWarm, 0.6, 150, 0.25), hi = sig (kCharWarm, 0.6, 3000, 0.25);
            CHECK (lo.levelDb[1] > lo.levelDb[2] + 2.0 && lo.thdPercent > hi.thdPercent * 1.3,
                   "WARM        : H2 %.1f > H3 %.1f dBc;  150 Hz THD %.2f %% > 3 kHz THD %.2f %%", lo.levelDb[1], lo.levelDb[2], lo.thdPercent, hi.thdPercent);
        }
        // TAPE: yalnizca tek harmonikler, tizlerde daha yogun, kafa tumsegi
        {
            const auto lo = sig (kCharTape, 1.0, 300, 0.25), hi = sig (kCharTape, 1.0, 6000, 0.25), bump = sig (kCharTape, 1.0, 75, 0.02), mid = sig (kCharTape, 1.0, 1000, 0.02);
            CHECK (lo.evenPercent < 0.001 && lo.oddPercent > 2.0 && hi.thdPercent > lo.thdPercent * 1.3,
                   "TAPE        : cift %.4f %%, tek %.2f %%;  6 kHz THD %.2f %% > 300 Hz THD %.2f %%", lo.evenPercent, lo.oddPercent, hi.thdPercent, lo.thdPercent);
            CHECK (bump.fundamentalGainDb > mid.fundamentalGainDb + 1.0,
                   "TAPE        : kafa tumsegi 75 Hz'de 1 kHz'e gore %+.2f dB", bump.fundamentalGainDb - mid.fundamentalGainDb);
        }
        // TUBE: cift baskin, frekanstan bagimsiz, harmonik dengesi seviyeyle degisir (dinamik bias)
        {
            const auto a = sig (kCharTube, 1.0, 220, 0.25), b = sig (kCharTube, 1.0, 5000, 0.25);
            const auto q = sig (kCharTube, 1.0, 220, 0.05), l = sig (kCharTube, 1.0, 220, 0.5);
            CHECK (a.levelDb[1] > a.levelDb[2] + 4.0 && std::abs (a.thdPercent - b.thdPercent) < 0.15 * a.thdPercent,
                   "TUBE        : H2 %.1f > H3 %.1f dBc;  220 Hz THD %.2f %% ~ 5 kHz THD %.2f %%", a.levelDb[1], a.levelDb[2], a.thdPercent, b.thdPercent);
            (void) q; (void) l;

            // DINAMIK bias: yuksek bir bolumden hemen sonra gelen alcak ton, bias henuz geri donmedigi
            // icin daha cok 2. harmonik tasir; ~0.5 sn sonra normale doner. Statik bir egri bunu yapamaz.
            const double f = 250.0;
            std::vector<float> x ((size_t) (1.6 * kFs));
            const size_t edge = (size_t) (0.6 * kFs);
            for (size_t i = 0; i < x.size(); ++i)
                x[i] = (float) ((i < edge ? 0.6 : 0.05) * std::sin (2 * kPi * f * (double) i / kFs));
            const auto y = throughCharacter (charOn (kCharTube, 1.0), x);
            auto h2 = [&y, f] (size_t from) { return 20.0 * std::log10 (ampAt (y, from, from + 1920, 2 * f) / ampAt (y, from, from + 1920, f)); };
            const double early = h2 (edge + 480), late = h2 (edge + 43200);
            CHECK (early > late + 3.0, "TUBE        : yuksek bolumden hemen sonra H2 %.1f dBc, 0.9 sn sonra %.1f dBc (bias zarfla kayiyor)", early, late);
        }
        // TRANSFORMER: tek baskin, baslar doyar, tizler temiz
        {
            const auto lo = sig (kCharTransformer, 1.0, 60, 0.25), hi = sig (kCharTransformer, 1.0, 3000, 0.25);
            CHECK (lo.levelDb[2] > lo.levelDb[1] + 10.0 && lo.thdPercent > hi.thdPercent * 4.0,
                   "TRANSFORMER : H3 %.1f >> H2 %.1f dBc;  60 Hz THD %.2f %% >> 3 kHz THD %.2f %%", lo.levelDb[2], lo.levelDb[1], lo.thdPercent, hi.thdPercent);
        }
        // DRIVE: en agresif, ust harmonikler guclu
        {
            const auto d = sig (kCharDrive, 1.0, 1000, 0.25);
            double others = 0;
            for (int t = 0; t < kCharDrive; ++t) others = std::max (others, sig (t, 1.0, 1000, 0.25).thdPercent);
            CHECK (d.thdPercent > others * 1.5 && d.levelDb[4] > -30.0,
                   "DRIVE       : THD %.1f %% (digerlerinin en buyugu %.1f %%), H5 %.1f dBc", d.thdPercent, others, d.levelDb[4]);
        }
        // Tipler birbirinden olculebilir sekilde farkli (imza vektorleri arasi uzaklik)
        {
            double minDist = 1e9; int ma = 0, mb = 0;
            for (int a = 0; a < kCharCustom; ++a)
                for (int b = a + 1; b < kCharCustom; ++b)
                {
                    double dist = 0;
                    for (double f : { 60.0, 220.0, 3000.0 })
                        for (double lv : { 0.1, 0.5 })
                        {
                            const auto sa = sig (a, 1.0, f, lv), sb = sig (b, 1.0, f, lv);
                            for (int h = 1; h < 5; ++h)
                                dist += std::abs (std::max (sa.levelDb[h], -80.0) - std::max (sb.levelDb[h], -80.0));
                        }
                    if (dist < minDist) { minDist = dist; ma = a; mb = b; }
                }
            CHECK (minDist > 60.0, "en benzer iki tip (%s / %s) bile toplam %.0f dB farkli harmonik imzaya sahip", characterTypeName (ma), characterTypeName (mb), minDist);
        }
        // CHARACTER knob'u yogunlugu duzgun olcekler
        {
            bool mono = true;
            for (int t = kCharWarm; t < kCharCustom; ++t)
            {
                double prev = -1;
                for (double I : { 0.1, 0.25, 0.5, 0.75, 1.0 })
                {
                    const double thd = sig (t, I, 220, 0.25).thdPercent;
                    if (thd <= prev) mono = false;
                    prev = thd;
                }
            }
            CHECK (mono, "CHARACTER knob'u arttikca butun tiplerde bozulma monoton artiyor");
        }
    }

    //--------------------------------------------------------------------------
    std::printf ("\n=== 9) Character Designer: 15 kontrolun her biri gercek DSP'yi degistiriyor ===\n");
    {
        CharacterProbe probe;
        auto sig = [&probe] (const CharacterSpec& s, double f = 220, double level = 0.25) { return probe.measure (s, 1.0, kFs, f, level); };

        // bos (notr) karakter: hicbir sey yapmaz
        {
            const auto s = sig (blankCustom());
            CHECK (s.thdPercent < 0.001 && std::abs (s.fundamentalGainDb) < 0.01, "notr karakter      : THD %.5f %%, kazanc %+.3f dB (sifirdan baslanabilir)", s.thdPercent, s.fundamentalGainDb);
        }

        // --- HARMONIC
        {
            auto c = blankCustom(); c.custom[kCpHarmAmount] = 100; c.custom[kCpEven] = 100; c.custom[kCpOdd] = 0;
            const auto e = sig (c);
            CHECK (e.levelDb[1] > -12.0 && e.levelDb[2] < -80.0, "EVEN               : H2 %.1f dBc, H3 %.1f dBc (yalnizca cift)", e.levelDb[1], e.levelDb[2]);

            c.custom[kCpEven] = 0; c.custom[kCpOdd] = 100;
            const auto o = sig (c);
            CHECK (o.levelDb[2] > -12.0 && o.levelDb[1] < -80.0, "ODD                : H3 %.1f dBc, H2 %.1f dBc (yalnizca tek)", o.levelDb[2], o.levelDb[1]);

            c.custom[kCpEven] = 100; c.custom[kCpHarmBalance] = 100;
            const auto hi = sig (c);
            CHECK (hi.levelDb[3] > -12.0 && hi.levelDb[4] > -12.0 && hi.levelDb[1] < -70.0 && hi.levelDb[2] < -70.0,
                   "HARMONIC BALANCE   : %%100'de H4 %.1f / H5 %.1f dBc, H2 %.1f / H3 %.1f dBc (ust dereceler)", hi.levelDb[3], hi.levelDb[4], hi.levelDb[1], hi.levelDb[2]);

            c.custom[kCpHarmBalance] = 0; c.custom[kCpHarmAmount] = 50;
            const auto half = sig (c);
            c.custom[kCpHarmAmount] = 100;
            const auto full = sig (c);
            CHECK (std::abs ((full.levelDb[1] - half.levelDb[1]) - 6.02) < 0.3, "HARMONIC AMOUNT    : %%50 -> %%100, H2 %+.2f dB (dogrusal olcek)", full.levelDb[1] - half.levelDb[1]);

            // seviyeden bagimsiz: -30 dBFS ve -6 dBFS'te ayni oran
            const auto q = sig (c, 220, 0.03), l = sig (c, 220, 0.5);
            CHECK (std::abs (q.levelDb[1] - l.levelDb[1]) < 0.5 && std::abs (q.levelDb[2] - l.levelDb[2]) < 0.5,
                   "harmonik ureteci   : -30 dBFS'te H2 %.1f, -6 dBFS'te %.1f dBc (seviyeden bagimsiz)", q.levelDb[1], l.levelDb[1]);
        }

        // --- SATURATION
        {
            auto c = blankCustom(); c.custom[kCpSaturation] = 100; c.custom[kCpDrive] = 0;
            const auto d0 = sig (c);
            c.custom[kCpDrive] = 50;
            const auto d50 = sig (c);
            c.custom[kCpDrive] = 100;
            const auto d100 = sig (c);
            CHECK (d50.thdPercent > d0.thdPercent * 2 && d100.thdPercent > d50.thdPercent * 1.5 && d0.evenPercent < 0.001,
                   "DRIVE              : THD %.2f %% -> %.2f %% -> %.2f %% (yalnizca tek harmonik)", d0.thdPercent, d50.thdPercent, d100.thdPercent);

            // doyurucu seviyeye bagli: sessizde temiz, yuksekte bozuk
            c.custom[kCpDrive] = 50;
            const auto q = sig (c, 220, 0.02), l = sig (c, 220, 0.7);
            CHECK (l.thdPercent > q.thdPercent * 20, "doyurucu           : -34 dBFS'te THD %.3f %%, -3 dBFS'te %.2f %% (seviyeye bagli)", q.thdPercent, l.thdPercent);

            // SATURATION = egrinin bukulmeye basladigi seviye
            auto s = blankCustom(); s.custom[kCpDrive] = 25;
            s.custom[kCpSaturation] = 0;
            const auto s0 = sig (s, 220, 0.4);
            s.custom[kCpSaturation] = 30;
            const auto s30 = sig (s, 220, 0.4);
            s.custom[kCpSaturation] = 100;
            const auto s100 = sig (s, 220, 0.4);
            CHECK (s0.thdPercent < 0.001 && s30.thdPercent > 0.01 && s100.thdPercent > s30.thdPercent * 2,
                   "SATURATION         : %%0 -> %.4f %%,  %%30 -> %.2f %%,  %%100 -> %.2f %% THD", s0.thdPercent, s30.thdPercent, s100.thdPercent);

            // SOFT / HARD: sert kirpma ust harmonikleri arttirir
            auto h = blankCustom(); h.custom[kCpSaturation] = 100; h.custom[kCpDrive] = 60;
            h.custom[kCpHardness] = 0;
            const auto soft = sig (h, 220, 0.4);
            h.custom[kCpHardness] = 100;
            const auto hard = sig (h, 220, 0.4);
            // sert diz: kirpma noktasina kadar TEMIZ, sonrasinda daha sert. Yumusak diz: kademeli.
            h.custom[kCpHardness] = 0;
            const auto softLow = sig (h, 220, 0.08), softHigh = sig (h, 220, 0.5);
            h.custom[kCpHardness] = 100;
            const auto hardLow = sig (h, 220, 0.08), hardHigh = sig (h, 220, 0.5);
            CHECK (hardLow.thdPercent < 0.001 && softLow.thdPercent > 1.0 && hardHigh.thdPercent > softHigh.thdPercent * 1.3,
                   "SOFT / HARD        : alcak seviye THD yumusak %.2f %% / sert %.4f %%;  yuksek seviye yumusak %.1f %% / sert %.1f %%",
                   softLow.thdPercent, hardLow.thdPercent, softHigh.thdPercent, hardHigh.thdPercent);
            (void) soft;

            // MIX: 0 -> doyurucu yok
            h.custom[kCpSatMix] = 0;
            const auto dry = sig (h, 220, 0.4);
            h.custom[kCpSatMix] = 50;
            const auto mid = sig (h, 220, 0.4);
            CHECK (dry.thdPercent < 0.001 && mid.thdPercent > 1.0 && mid.thdPercent < hard.thdPercent,
                   "SATURATION MIX     : %%0 -> %.4f %%,  %%50 -> %.2f %%,  %%100 -> %.2f %% THD", dry.thdPercent, mid.thdPercent, hard.thdPercent);
        }

        // --- TRANSIENT (vuruslu test sinyali: sessizlikten aniden baslayan tonlar)
        {
            auto bursts = [] ()
            {
                std::vector<float> x ((size_t) (3.0 * kFs), 0.0f);
                for (int k = 0; k < 6; ++k)
                {
                    const size_t a = (size_t) ((0.2 + 0.45 * k) * kFs), len = (size_t) (0.25 * kFs);
                    for (size_t i = 0; i < len; ++i)
                        x[a + i] = (float) (0.3 * std::sin (2 * kPi * 330.0 * (double) i / kFs));
                }
                return x;
            };
            // vurusun ilk 5 ms'sinin, notanin surekli kisminin RMS'ine orani (dB)
            auto attackVsSustain = [] (const std::vector<float>& y)
            {
                double acc = 0;
                for (int k = 2; k < 6; ++k)
                {
                    const size_t a = (size_t) ((0.2 + 0.45 * k) * kFs);
                    acc += rmsDb (y, a, a + 240) - rmsDb (y, a + 4800, a + 9600);
                }
                return acc / 4.0;
            };

            const double base = attackVsSustain (throughCustom (blankCustom(), bursts()));

            auto c = blankCustom(); c.custom[kCpTransient] = 100; c.custom[kCpAttackChar] = 50;
            const double plus = attackVsSustain (throughCustom (c, bursts()));
            c.custom[kCpTransient] = -100;
            const double minus = attackVsSustain (throughCustom (c, bursts()));
            CHECK (plus > base + 3.0 && minus < base - 3.0, "TRANSIENT          : vurus/govde orani -100 -> %+.1f dB, 0 -> %+.1f dB, +100 -> %+.1f dB", minus, base, plus);

            // ATTACK CHARACTER: uzun pencere vurusun daha gec kismini da etkiler (10 - 30 ms arasi)
            auto later = [] (const std::vector<float>& y)
            {
                double acc = 0;
                for (int k = 2; k < 6; ++k)
                {
                    const size_t a = (size_t) ((0.2 + 0.45 * k) * kFs);
                    acc += rmsDb (y, a + 480, a + 1440) - rmsDb (y, a + 4800, a + 9600);
                }
                return acc / 4.0;
            };
            c.custom[kCpTransient] = 100; c.custom[kCpAttackChar] = 0;
            const double shortW = later (throughCustom (c, bursts()));
            c.custom[kCpAttackChar] = 100;
            const double longW = later (throughCustom (c, bursts()));
            CHECK (longW > shortW + 2.0, "ATTACK CHARACTER   : 10-30 ms bolgesi kisa pencere %+.1f dB, uzun pencere %+.1f dB", shortW, longW);

            // ROUNDING: vurus tepesini yuvarlar, surekli kisma dokunmaz
            auto r = blankCustom(); r.custom[kCpRounding] = 100; r.custom[kCpAttackChar] = 60;
            const auto y0 = throughCustom (blankCustom(), bursts());
            const auto y1 = throughCustom (r, bursts());
            double atk0 = 0, atk1 = 0;
            const size_t at = (size_t) ((0.2 + 0.45 * 3) * kFs);
            for (size_t i = at; i < at + 240; ++i) { atk0 = std::max (atk0, (double) std::abs (y0[i])); atk1 = std::max (atk1, (double) std::abs (y1[i])); }
            const double sus = rmsDb (y1, at + 7200, at + 9600) - rmsDb (y0, at + 7200, at + 9600);
            CHECK (atk1 < atk0 * 0.75 && std::abs (sus) < 0.3, "TRANSIENT ROUNDING : vurus tepesi %.3f -> %.3f, surekli kisim %+.2f dB", atk0, atk1, sus);
        }

        // --- DENSITY
        {
            // yuksek ve alcak bolumleri olan sinyal: alcak bolum yukari tasinmali, yuksek bolum ayni kalmali
            std::vector<float> x ((size_t) (8.0 * kFs));
            for (size_t i = 0; i < x.size(); ++i)
            {
                const bool loud = ((i / (size_t) (0.5 * kFs)) % 2) == 0;
                x[i] = (float) ((loud ? 0.4 : 0.05) * std::sin (2 * kPi * 300.0 * (double) i / kFs));
            }
            auto c = blankCustom(); c.custom[kCpDensity] = 100;
            const auto y = throughCustom (c, x);
            const size_t loudA = (size_t) (6.2 * kFs), loudB = (size_t) (6.45 * kFs), softA = (size_t) (6.7 * kFs), softB = (size_t) (6.95 * kFs);
            const double dLoud = rmsDb (y, loudA, loudB) - rmsDb (x, loudA, loudB);
            const double dSoft = rmsDb (y, softA, softB) - rmsDb (x, softA, softB);
            CHECK (dSoft > 2.5 && std::abs (dLoud) < 0.7, "DENSITY            : alcak bolum %+.1f dB, yuksek bolum %+.1f dB (yukari kompresyon)", dSoft, dLoud);

            // cok alcak icerik (dip gurultusu) yukseltilmez
            std::vector<float> z ((size_t) (8.0 * kFs));
            for (size_t i = 0; i < z.size(); ++i)
            {
                const bool loud = ((i / (size_t) (0.5 * kFs)) % 2) == 0;
                z[i] = (float) ((loud ? 0.4 : 0.0008) * std::sin (2 * kPi * 300.0 * (double) i / kFs));
            }
            const auto yz = throughCustom (c, z);
            const double dNoise = rmsDb (yz, softA, softB) - rmsDb (z, softA, softB);
            CHECK (dNoise < 1.0, "DENSITY            : programin 54 dB altindaki icerik %+.2f dB (dip gurultusu yukselmez)", dNoise);
        }

        // --- HARMONIC THICKNESS: yalnizca alt bandin harmonikleri
        {
            auto c = blankCustom(); c.custom[kCpThickness] = 100;
            const auto lo = sig (c, 100, 0.25), hi = sig (c, 2000, 0.25);
            CHECK (lo.levelDb[1] > -30.0 && hi.thdPercent < 0.05, "HARMONIC THICKNESS : 100 Hz'de H2 %.1f dBc, 2 kHz'de THD %.4f %% (yalnizca govde)", lo.levelDb[1], hi.thdPercent);
        }

        // --- CHARACTER TONE: doygunlugun hangi frekansta olusacagi (EQ degil)
        {
            auto c = blankCustom(); c.custom[kCpSaturation] = 100; c.custom[kCpDrive] = 40;
            const auto lo0 = sig (c, 80, 0.25), hi0 = sig (c, 8000, 0.25);

            c.custom[kCpLowChar] = 100;
            const auto loP = sig (c, 80, 0.25);
            c.custom[kCpLowChar] = -100;
            const auto loM = sig (c, 80, 0.25);
            CHECK (loP.thdPercent > lo0.thdPercent * 2 && loM.thdPercent < lo0.thdPercent * 0.5,
                   "LOW CHARACTER      : 80 Hz THD  -100 -> %.2f %%,  0 -> %.2f %%,  +100 -> %.2f %%", loM.thdPercent, lo0.thdPercent, loP.thdPercent);

            c.custom[kCpLowChar] = 0; c.custom[kCpHighChar] = 100;
            const auto hiP = sig (c, 8000, 0.25);
            c.custom[kCpHighChar] = -100;
            const auto hiM = sig (c, 8000, 0.25);
            // 8 kHz'de H3 = 24 kHz (bant disi): temelin sikismasina (kazanc dususu) bak
            CHECK (hiP.fundamentalGainDb < hi0.fundamentalGainDb - 1.0 && hiM.fundamentalGainDb > hi0.fundamentalGainDb + 0.3,
                   "HIGH CHARACTER     : 8 kHz sikismasi  -100 -> %+.2f dB,  0 -> %+.2f dB,  +100 -> %+.2f dB", hiM.fundamentalGainDb, hi0.fundamentalGainDb, hiP.fundamentalGainDb);

            // dogrusal olmayan katlar kapaliyken ton kontrolleri frekans tepkisini DEGISTIRMEZ
            auto flat = blankCustom(); flat.custom[kCpLowChar] = 100; flat.custom[kCpHighChar] = -100;
            double worst = 0;
            for (double f : { 40.0, 100.0, 200.0, 1000.0, 3500.0, 10000.0, 18000.0 })
                worst = std::max (worst, std::abs (sig (flat, f, 0.25).fundamentalGainDb));
            CHECK (worst < 0.02, "ton kontrolleri    : doygunluk yokken frekans tepkisi duz (en buyuk sapma %.4f dB) -> EQ degil", worst);
        }
    }

    //--------------------------------------------------------------------------
    std::printf ("\n=== 10) Aliasing (6217 Hz, -6 dBFS; harmonik olmayan en buyuk bilesen) ===\n");
    {
        const double f0 = 6217.0, level = 0.5;
        const size_t N = 1 << 16;

        // harmonik olmayan (katlanmis) en buyuk bilesen, temele gore dB
        auto aliasLevel = [f0] (const std::vector<float>& y)
        {
            const size_t n = (size_t) 1 << 15, a = y.size() - n;
            std::vector<float> re (n), im (n, 0.0f);
            for (size_t i = 0; i < n; ++i)
            {
                const double t = (double) i / (double) n;
                const double w = 0.35875 - 0.48829 * std::cos (2 * kPi * t) + 0.14128 * std::cos (4 * kPi * t) - 0.01168 * std::cos (6 * kPi * t);
                re[i] = (float) (y[a + i] * w);
            }
            fftInPlace (re.data(), im.data(), (int) n);
            const double binHz = kFs / (double) n;
            double fund = 0, worst = 0;
            for (size_t k = 4; k < n / 2; ++k)
            {
                const double f = (double) k * binHz, mag = std::sqrt ((double) re[k] * re[k] + (double) im[k] * im[k]);
                bool harmonic = false;
                for (int h = 1; h <= 3; ++h) if (std::abs (f - h * f0) < 12 * binHz) harmonic = true;
                if (std::abs (f - f0) < 12 * binHz) fund = std::max (fund, mag);
                if (! harmonic && f < 20000.0) worst = std::max (worst, mag);
            }
            return 20.0 * std::log10 (worst / fund + 1e-30);
        };

        std::printf ("  tip           oversampling'siz   SCC V6 (4x + ADAA)\n");
        bool allOk = true;
        double worstV6 = -300;
        for (int t = kCharWarm; t <= kCharCustom; ++t)
        {
            CharacterSettings cs = charOn (t, 1.0);
            if (t == kCharCustom)
            {
                cs.spec[0] = blankCustom();
                cs.spec[0].custom[kCpSaturation] = 100; cs.spec[0].custom[kCpDrive] = 80; cs.spec[0].custom[kCpHardness] = 100;
                cs.spec[0].custom[kCpHarmAmount] = 100; cs.spec[0].custom[kCpEven] = 100; cs.spec[0].custom[kCpOdd] = 100; cs.spec[0].custom[kCpHarmBalance] = 50;
            }
            const auto y = throughCharacter (cs, sine (f0, level, (double) N / kFs));

            // karsilastirma: ayni statik egri, taban hizda, oversampling ve ADAA olmadan
            Voicing vo; character::computeVoicing (cs.spec[0], 1.0, vo);
            std::vector<float> naive (N);
            for (size_t i = 0; i < N; ++i)
                naive[i] = (float) character::displayCurve (vo, level * std::sin (2 * kPi * f0 * (double) i / kFs), level);

            const double a6 = aliasLevel (y), a0 = aliasLevel (naive);
            worstV6 = std::max (worstV6, a6);
            if (a6 > -70.0) allOk = false;
            std::printf ("  %-12s %10.1f dB      %10.1f dB   %s\n", t == kCharCustom ? "Custom (sert)" : characterTypeName (t), a0, a6, a6 < a0 - 20 ? "" : "(egri zaten bant sinirli)");
        }
        CHECK (allOk, "butun tiplerde katlanma < -70 dBc (en kotusu %.1f dBc)", worstV6);
    }

    //--------------------------------------------------------------------------
    std::printf ("\n=== 11) Tik / zipper: tip degisimi, A/B, ac-kapa, knob hareketi ===\n");
    {
        const auto ref = sine (330, 0.4, 3.0);

        // tip gezintisi: her 0.3 sn'de bir sonraki tip
        {
            CharacterEngine e; e.prepare (kFs);
            auto x = ref;
            double worst = 0;
            std::vector<size_t> switches;
            for (size_t i = 0; i < x.size(); i += 64)
            {
                const int type = (int) ((i / (size_t) (0.3 * kFs)) % (size_t) kNumCharacterTypes);
                if (i > 0 && type != (int) (((i - 64) / (size_t) (0.3 * kFs)) % (size_t) kNumCharacterTypes)) switches.push_back (i);
                e.setSettings (charOn (type, 1.0));
                float* ch[1] = { x.data() + i };
                e.process (ch, 1, 64);
            }
            // en "sert" durumun (DRIVE) kendi ikinci farki referans
            const double steady = maxSecondDiff (throughCharacter (charOn (kCharDrive, 1.0), ref), 24000, 48000);
            for (size_t s : switches) worst = std::max (worst, maxSecondDiff (x, s - 32, s + 2400));
            CHECK (worst < steady * 1.6, "7 tip arasinda gezinti: en buyuk ikinci fark %.5f (DRIVE'in kendisi %.5f) -> tik yok", worst, steady);
        }

        // A/B: A = TAPE, B = CUSTOM (farkli tip -> capraz gecis), sonra A = B tipi ayni farkli ayar (-> yumusatma)
        {
            CharacterSettings cs = charOn (kCharTape, 1.0);
            cs.spec[1] = CharacterSpec(); cs.spec[1].type = kCharCustom; cs.spec[1].custom[kCpDrive] = 80; cs.spec[1].custom[kCpSaturation] = 90;
            CharacterEngine e; e.prepare (kFs);
            auto x = ref;
            for (size_t i = 0; i < x.size(); i += 64)
            {
                cs.slot = (int) ((i / (size_t) (0.25 * kFs)) % 2);
                e.setSettings (cs);
                float* ch[1] = { x.data() + i };
                e.process (ch, 1, 64);
            }
            double worst = 0;
            for (size_t s = (size_t) (0.25 * kFs); s + 2400 < x.size(); s += (size_t) (0.25 * kFs)) worst = std::max (worst, maxSecondDiff (x, s - 32, s + 2400));
            cs.slot = 1;
            const double steady = maxSecondDiff (throughCharacter (cs, ref), 24000, 48000);
            CHECK (worst < steady * 1.6 + 1e-4, "A/B (TAPE <-> CUSTOM): ikinci fark %.5f (B'nin kendisi %.5f) -> tik yok", worst, steady);
        }

        // Character ON/OFF
        {
            CharacterEngine e; e.prepare (kFs);
            auto x = ref;
            for (size_t i = 0; i < x.size(); i += 64)
            {
                CharacterSettings cs = charOn (kCharTube, 1.0);
                cs.on = ((i / (size_t) (0.4 * kFs)) % 2) == 1;
                e.setSettings (cs);
                float* ch[1] = { x.data() + i };
                e.process (ch, 1, 64);
            }
            double worst = 0;
            for (size_t s = (size_t) (0.4 * kFs); s + 2400 < x.size(); s += (size_t) (0.4 * kFs)) worst = std::max (worst, maxSecondDiff (x, s - 32, s + 2400));
            const double steady = maxSecondDiff (throughCharacter (charOn (kCharTube, 1.0), ref), 24000, 48000);
            CHECK (worst < steady * 1.6 + 1e-4, "Character ON/OFF: ikinci fark %.5f (acik halin kendisi %.5f) -> tik yok", worst, steady);
        }

        // knob hareketi (otomasyon): CHARACTER 0 -> 100 -> 0 ve Designer knob'lari, blok basina sicrayarak
        {
            CharacterEngine e; e.prepare (kFs);
            auto x = ref;
            CharacterSettings cs = charOn (kCharCustom, 1.0);
            Rng rng;
            for (size_t i = 0; i < x.size(); i += 512)                   // buyuk blok = kaba adimli otomasyon
            {
                const double t = (double) i / (double) x.size();
                cs.intensity = (float) (0.5 + 0.5 * std::sin (2 * kPi * 3.0 * t));
                for (int p = 0; p < kNumCustomParams; ++p)
                {
                    const auto& info = customParamInfo (p);
                    cs.spec[0].custom[p] = info.minValue + (info.maxValue - info.minValue) * (0.5f + 0.5f * rng.next());
                }
                e.setSettings (cs);
                float* ch[1] = { x.data() + i };
                e.process (ch, 1, (int) std::min<size_t> (512, x.size() - i));
            }
            bool finite = true; double peak = 0;
            for (float s : x) { if (! std::isfinite (s)) finite = false; peak = std::max (peak, (double) std::abs (s)); }
            // butun kontroller her 10 ms'de rastgele sicrarken bile ornekler arasi adim sinirli kalmali
            double maxStep = 0; for (size_t i = 1; i < x.size(); ++i) maxStep = std::max (maxStep, (double) std::abs (x[i] - x[i - 1]));
            CHECK (finite && peak < 4.0 && maxStep < 0.35, "15 kontrol + CHARACTER rastgele sicrarken: tepe %.2f, en buyuk ornek adimi %.3f (zipper / patlama yok)", peak, maxStep);
        }
    }

    //--------------------------------------------------------------------------
    std::printf ("\n=== 12) Cikis telafisi ve LEVEL MATCH ===\n");
    {
        auto v = makeVoice (12.0);
        // sesi tipik bir "kompresorden cikmis" seviyeye getir
        for (auto& s : v) s *= 1.6f;
        const double inDb = rmsDb (v, (size_t) (6 * kFs), v.size());

        std::printf ("  tip           telafi (statik)   LEVEL MATCH acik\n");
        double worstStatic = 0, worstMatch = 0;
        for (int t = 0; t < kNumCharacterTypes; ++t)
        {
            CharacterSettings cs = charOn (t, 1.0);
            const double a = rmsDb (throughCharacter (cs, v), (size_t) (6 * kFs), v.size()) - inDb;
            cs.levelMatch = true;
            const double b = rmsDb (throughCharacter (cs, v), (size_t) (6 * kFs), v.size()) - inDb;
            worstStatic = std::max (worstStatic, std::abs (a));
            worstMatch  = std::max (worstMatch, std::abs (b));
            std::printf ("  %-12s %+9.2f dB      %+9.2f dB\n", characterTypeName (t), a, b);
        }
        CHECK (worstStatic < 3.0, "statik telafi: %%100'de bile ses yuksekligi +-3 dB icinde (en buyuk %.2f dB)", worstStatic);
        CHECK (worstMatch < 0.6,  "LEVEL MATCH: giris ve cikis ayni yukseklikte (en buyuk fark %.2f dB)", worstMatch);

        // A/B'de her slot kendi seviyesini hatirlar: geri donuste aninda dogru seviye
        {
            CharacterSettings cs = charOn (kCharClean, 1.0);
            cs.levelMatch = true;
            cs.spec[1].type = kCharDrive;
            CharacterEngine e; e.prepare (kFs);
            auto x = v;
            const size_t swap1 = (size_t) (5 * kFs), swap2 = (size_t) (9 * kFs);
            for (size_t i = 0; i < x.size(); i += 64)
            {
                cs.slot = (i >= swap1 && i < swap2) ? 0 : 1;             // B (ogren) -> A -> B (hatirla)
                e.setSettings (cs);
                float* ch[1] = { x.data() + i };
                e.process (ch, 1, (int) std::min<size_t> (64, x.size() - i));
            }
            const double first = rmsDb (x, swap2 + 2400, swap2 + 24000) - rmsDb (v, swap2 + 2400, swap2 + 24000);
            CHECK (std::abs (first) < 1.0, "B'ye geri donuste ilk yarim saniye: %+.2f dB (seviye hatirlandi, kayma yok)", first);
        }
    }

    //--------------------------------------------------------------------------
    std::printf ("\n=== 12b) Ses alti temizlik: asimetrik tipler zarfi izleyen DC / gumleme birakmaz ===\n");
    {
        // 25 Hz altindaki enerji (4 kutuplu alcak geciren), toplam enerjiye gore dB
        auto subsonicDb = [] (const std::vector<float>& x)
        {
            const double a = 1.0 - std::exp (-2 * kPi * 25.0 / kFs);
            double z[4] = { 0, 0, 0, 0 }, es = 0, et = 0;
            for (size_t i = 0; i < x.size(); ++i)
            {
                double v = x[i];
                for (auto& st : z) { st += a * (v - st); v = st; }
                if (i > x.size() / 4) { es += v * v; et += (double) x[i] * x[i]; }
            }
            return 10.0 * std::log10 (es / et + 1e-30);
        };

        // Kendi ses alti icerigi olmayan (simetrik dalga formlu) hece dizisi: cikista 25 Hz
        // altinda ne varsa karakter katinin urettigi seydir.
        std::vector<float> v ((size_t) (12.0 * kFs), 0.0f);
        {
            Rng rng; size_t n = 0; double phase = 0;
            while (n < v.size())
            {
                const size_t sylN = (size_t) ((0.16 + 0.12 * (rng.next() * 0.5 + 0.5)) * kFs);
                const size_t gapN = (size_t) ((0.03 + 0.12 * (rng.next() * 0.5 + 0.5)) * kFs);
                const double amp = std::pow (10.0, (-18.0 + 14.0 * (rng.next() * 0.5 + 0.5)) / 20.0);
                const double f0  = 110.0 + 60.0 * (rng.next() * 0.5 + 0.5);
                for (size_t i = 0; i < sylN && n < v.size(); ++i, ++n)
                {
                    const double env = std::pow (std::sin (kPi * (double) i / (double) sylN), 0.6);
                    phase += f0 / kFs;
                    double h = 0;
                    for (int k = 1; k <= 20; ++k) h += std::sin (2 * kPi * k * phase) / (k * (1.0 + 0.02 * k * k));
                    v[n] = (float) (0.7 * amp * env * h);
                }
                n += gapN;
            }
        }
        const double inSub = subsonicDb (v);
        double worst = -300; int worstType = 0;
        for (int t = 0; t < kNumCharacterTypes; ++t)
        {
            CharacterSettings cs = charOn (t, 1.0);
            if (t == kCharCustom)
                for (int p = 0; p < kNumCustomParams; ++p) cs.spec[0].custom[p] = 70.0f;
            const double d = subsonicDb (throughCharacter (cs, v));
            if (d > worst) { worst = d; worstType = t; }
        }
        std::printf ("  giris: 25 Hz alti %.1f dB;  en kotu tip (%s, %%100): %.1f dB\n", inSub, characterTypeName (worstType), worst);
        CHECK (worst < -40.0, "butun tiplerde 25 Hz alti icerik sinyalin 40 dB'den fazla altinda (en kotu %.1f dB)", worst);

        // dogrusal yol etkilenmez: cok alcak seviyeli 20 Hz sinus, TUBE %100 -> kazanc 0 dB
        CharacterProbe probe;
        CharacterSpec tube; tube.type = kCharTube;
        const auto low = probe.measure (tube, 1.0, kFs, 20.0, 0.001);
        CharacterSpec custom = blankCustom(); custom.custom[kCpLowChar] = 100; custom.custom[kCpDensity] = 0;
        const auto low2 = probe.measure (custom, 1.0, kFs, 20.0, 0.25);
        CHECK (std::abs (low2.fundamentalGainDb) < 0.01, "suzgec yalnizca kalintida: 20 Hz'de dogrusal yolun kazanci %+.4f dB (bas kaybi yok)", low2.fundamentalGainDb);
        (void) low;
    }

    //--------------------------------------------------------------------------
    std::printf ("\n=== 13) Dayaniklilik ===\n");
    {
        // NaN / Inf + tam olcek gurultu, butun tipler, Analog mod
        bool finite = true;
        for (int t = 0; t < kNumCharacterTypes; ++t)
        {
            Engine e; e.prepare (kFs);
            EngineSettings es; es.analogMode = (t % 2) == 1;
            es.adaptive.amountPct = 100; es.adaptive.ratio = 100; es.adaptive.mode = Hard;
            es.manual.thresholdDb = -60; es.manual.ratio = kManualLimitRatio; es.manual.attackMs = 0.1f; es.manual.releaseMs = 10; es.manual.makeupDb = 24;
            es.character = charOn (t, 1.0); es.character.levelMatch = true;
            for (int p = 0; p < kNumCustomParams; ++p) es.character.spec[0].custom[p] = customParamInfo (p).maxValue;
            e.setSettings (es);
            Rng rng; const size_t N = (size_t) kFs;
            std::vector<float> L (N), R (N);
            for (size_t i = 0; i < N; ++i) { L[i] = rng.next(); R[i] = rng.next(); }
            L[1000] = NAN; R[2000] = INFINITY; L[3000] = -INFINITY; L[5000] = 1.0e30f;
            runStereo (e, L, R, 61);
            for (size_t i = 0; i < N; ++i) if (! std::isfinite (L[i]) || ! std::isfinite (R[i])) finite = false;
        }
        CHECK (finite, "butun tipler: tam olcek gurultu + NaN/Inf + asiri ayarlarda cikis sonlu");

        // blok boyutundan bagimsiz: 1, 7, 64, 333, 4096 ornekli bloklarla ayni cikis
        {
            auto v = makeVoice (2.0);
            std::vector<float> first;
            double worst = 0;
            for (int block : { 64, 1, 7, 333, 4096 })
            {
                Engine e; e.prepare (kFs);
                EngineSettings es; es.adaptive.amountPct = 70; es.character = charOn (kCharCustom, 0.8);
                es.character.spec[0].custom[kCpTransient] = 60; es.character.spec[0].custom[kCpThickness] = 50; es.character.spec[0].custom[kCpRounding] = 40;
                e.setSettings (es);
                std::vector<float> L = v, R = v;
                runStereo (e, L, R, block);
                if (first.empty()) first = L;
                else for (size_t i = 0; i < L.size(); ++i) worst = std::max (worst, (double) std::abs (L[i] - first[i]));
            }
            CHECK (worst < 1.0e-5, "blok boyutundan bagimsiz (1 / 7 / 64 / 333 / 4096): en buyuk fark %.1e", worst);
        }

        // ornekleme hizlari ve mono
        for (double sr : { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 })
        {
            Engine e; e.prepare (sr);
            EngineSettings es; es.adaptive.amountPct = 60; es.character = charOn (kCharTape, 0.8);
            e.setSettings (es);
            auto x = sine (300, 0.4, 0.5, sr);
            runMono (e, x);
            bool ok = true; for (float s : x) if (! std::isfinite (s)) ok = false;

            // karakterin imzasi ornekleme hizindan bagimsiz olmali
            CharacterProbe probe; CharacterSpec sp; sp.type = kCharTape;
            const auto sg = probe.measure (sp, 0.8, sr, 300, 0.25);
            CHECK (ok && std::abs (sg.levelDb[2] - (-29.9)) < 1.5, "mono @ %6.0f Hz: sonlu, %dx oversampling, TAPE H3 %.1f dBc", sr, e.characterEngine().oversamplingFactor(), sg.levelDb[2]);
        }

        // stereo butunluk: L = R girisi -> L = R cikisi
        {
            auto v = makeVoice (2.0);
            Engine e; e.prepare (kFs);
            EngineSettings es; es.adaptive.amountPct = 70; es.character = charOn (kCharCustom, 1.0); es.character.spec[0].custom[kCpTransient] = 80;
            e.setSettings (es);
            std::vector<float> L = v, R = v;
            runStereo (e, L, R);
            double worst = 0; for (size_t i = 0; i < L.size(); ++i) worst = std::max (worst, (double) std::abs (L[i] - R[i]));
            CHECK (worst == 0.0, "stereo butunluk: ayni giris -> iki kanal birebir ayni");
        }

        // sessizlik: cikis sifira iner, denormal kalmaz
        {
            Engine e; e.prepare (kFs);
            EngineSettings es; es.character = charOn (kCharWarm, 1.0);
            e.setSettings (es);
            auto x = sine (200, 0.5, 0.5);
            x.resize ((size_t) (6.0 * kFs), 0.0f);
            std::vector<float> R = x;
            runStereo (e, x, R);
            double tail = 0; bool denormal = false;
            for (size_t i = x.size() - 4800; i < x.size(); ++i)
            {
                tail = std::max (tail, (double) std::abs (x[i]));
                if (x[i] != 0.0f && std::abs (x[i]) < 1.0e-37f) denormal = true;
            }
            CHECK (tail < 1.0e-6 && ! denormal, "sinyal bittikten sonra cikis susuyor (kuyruk %.1e), denormal yok", tail);
        }
    }

    //--------------------------------------------------------------------------
    std::printf ("\n=== 14) Gercek zaman guvenligi: ses yolunda bellek ayirma yok ===\n");
    {
        auto v = makeVoice (2.0);
        Engine* e = new Engine();
        e->prepare (kFs);
        std::vector<float> L = v, R = v;
        EngineSettings es;
        Rng rng;

        const long before = gAllocCount;
        for (size_t i = 0; i < L.size(); i += 128)
        {
            // her blokta her seyi degistir: mod, tip, A/B, bypass, butun knob'lar
            es.analogMode = (rng.next() > 0.0f);
            es.bypass = (rng.next() > 0.8f);
            es.adaptive.amountPct = 50.0f + 50.0f * rng.next();
            es.manual.attackMs = 0.1f + 50.0f * (0.5f + 0.5f * rng.next());
            es.character.on = (rng.next() > -0.6f);
            es.character.intensity = 0.5f + 0.5f * rng.next();
            es.character.slot = (rng.next() > 0.0f) ? 1 : 0;
            es.character.levelMatch = (rng.next() > 0.0f);
            for (int s = 0; s < 2; ++s)
            {
                es.character.spec[s].type = (int) ((rng.next() * 0.5f + 0.5f) * 6.99f);
                for (int p = 0; p < kNumCustomParams; ++p)
                {
                    const auto& info = customParamInfo (p);
                    es.character.spec[s].custom[p] = info.minValue + (info.maxValue - info.minValue) * (0.5f + 0.5f * rng.next());
                }
            }
            e->setSettings (es);
            float* ch[2] = { L.data() + i, R.data() + i };
            e->process (ch, 2, (int) std::min<size_t> (128, L.size() - i));
        }
        const long allocs = gAllocCount - before;
        bool finite = true; for (float s : L) if (! std::isfinite (s)) finite = false;
        CHECK (allocs == 0, "2 sn boyunca her blokta butun ayarlar degisirken process() icinde %ld bellek ayirma", allocs);
        CHECK (finite, "bu sirada cikis sonlu");
        delete e;
    }

    //--------------------------------------------------------------------------
    std::printf ("\n=== 15) Spektrum analizi (arayuz tarafi) ===\n");
    {
        Engine* e = new Engine();
        e->prepare (kFs);
        EngineSettings es; es.adaptive.amountPct = 0; es.adaptive.autoGain = false;
        es.character = charOn (kCharTube, 1.0);
        e->setSettings (es);
        // bin merkezine oturan frekans: 85 * 48000 / 4096 = 996.09 Hz
        const double f = 85.0 * kFs / (double) SpectrumAnalyzer::kFftSize;
        auto L = sine (f, 0.25, 1.0); auto R = L;
        runStereo (*e, L, R);

        SpectrumAnalyzer* an = new SpectrumAnalyzer();
        std::vector<float> pre ((size_t) SpectrumAnalyzer::kNumBins), post ((size_t) SpectrumAnalyzer::kNumBins);
        an->compute (e->analyzerPre, pre.data());
        an->compute (e->analyzerPost, post.data());

        std::printf ("  giris : temel %.2f dBFS, 2. harmonik %.1f dBFS\n", pre[85], pre[170]);
        std::printf ("  cikis : temel %.2f dBFS, 2. harmonik %.1f dBFS\n", post[85], post[170]);
        CHECK (std::abs (pre[85] - (-12.04f)) < 0.1f, "-12.04 dBFS'lik sinus spektrumda %.2f dBFS okunuyor", pre[85]);
        CHECK (pre[170] < -100.0f && post[170] > -50.0f, "karakterin urettigi 2. harmonik yalnizca CIKIS spektrumunda gorunuyor");
        delete an;
        delete e;
    }

    //--------------------------------------------------------------------------
    std::printf ("\n=== 16) Islemci yuku (48 kHz stereo, tek cekirdek, gercek zamana gore) ===\n");
    {
        auto v = makeVoice (10.0);
        auto bench = [&v] (const EngineSettings& es)
        {
            Engine e; e.prepare (kFs); e.setSettings (es);
            std::vector<float> L = v, R = v;
            const auto t0 = std::chrono::steady_clock::now();
            runStereo (e, L, R, 256);
            const double sec = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
            return 100.0 * sec / 10.0;
        };

        EngineSettings es; es.adaptive.amountPct = 70;
        const double off = bench (es);
        std::printf ("  Character kapali (V5 esdegeri) : %5.2f %%\n", off);
        double worst = 0;
        for (int t = 0; t < kNumCharacterTypes; ++t)
        {
            es.character = charOn (t, 1.0);
            if (t == kCharCustom)
                for (int p = 0; p < kNumCustomParams; ++p) es.character.spec[0].custom[p] = 70.0f;      // butun moduller acik
            const double c = bench (es);
            worst = std::max (worst, c);
            std::printf ("  %-12s%s : %5.2f %%\n", characterTypeName (t), t == kCharCustom ? " (hepsi acik)    " : "                 ", c);
        }
        CHECK (worst < 25.0, "en agir durumda bile gercek zamanin %.1f %%'si (optimizasyonsuz derlemede daha yuksek cikar)", worst);
    }

    std::printf ("\n%s (%d hata)\n", failures == 0 ? "TUM V6 TESTLERI GECTI" : "BAZI TESTLER BASARISIZ", failures);
    return failures == 0 ? 0 : 1;
}

#endif // SCC_BUILD_DSP_TEST
