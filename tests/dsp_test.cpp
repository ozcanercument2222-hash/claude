// SCC DSP testi - JUCE gerektirmez. PLUGIN DERLEMESINE DAHIL DEGILDIR.
//
//   macOS / Linux : g++ -O2 -std=c++17 -DSCC_BUILD_DSP_TEST tests/dsp_test.cpp -o dsp_test && ./dsp_test
//   Windows       : cmake -B build -DSCC_BUILD_TESTS=ON
//                   cmake --build build --config Release --target SCC_DspTest
//                   build\Release\SCC_DspTest.exe
//
// Bu dosya yanlislikla bir plugin / Visual Studio / Projucer projesine eklense bile,
// asagidaki #ifdef yuzunden hicbir sey derlenmez (plugin'i bozmaz).
#ifdef SCC_BUILD_DSP_TEST

#include "../Source/SCCDsp.h"

#include <cstdio>
#include <utility>
#include <cstdlib>
#include <vector>

using namespace scc;

static int failures = 0;
#define CHECK(cond, ...)                                          \
    do { if (cond) std::printf ("  [OK]   "); else { std::printf ("  [FAIL] "); ++failures; } \
         std::printf (__VA_ARGS__); std::printf ("\n"); } while (0)

static const double kFs = 48000.0;
static const double kPi = 3.14159265358979323846;   // MSVC'de M_PI varsayilan olarak yok

struct Rng
{
    std::uint32_t s = 12345u;
    float next() { s = s * 1664525u + 1013904223u; return (float) ((s >> 8) & 0xFFFF) / 32768.0f - 1.0f; }
};

static float rmsDb (const std::vector<float>& v, size_t a, size_t b)
{
    double e = 0; for (size_t i = a; i < b; ++i) e += (double) v[i] * v[i];
    return (float) (10.0 * std::log10 (e / (double) (b - a) + 1e-20));
}

// Ses benzeri sinyal: glottal darbe + 3 formant rezonatoru, hece zarflari, sessizlikler, tisirti.
// Her hece, onceden secilen tepe seviyesine (-24 .. -6 dBFS) normalize edilir.
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
        const double peakDb = -24.0 + 18.0 * (rng.next() * 0.5 + 0.5);          // -24 .. -6 dBFS
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
        const size_t burst = (size_t) (0.012 * kFs);                              // sessiz harf patlamasi
        for (size_t i = 0; i < burst && sg.a + i < sg.b; ++i)
            out[sg.a + i] += (float) (0.5 * sg.amp * rng.next() * (1.0 - (double) i / (double) burst));
    }
    return out;
}

static void run (Compressor& c, std::vector<float>& L, std::vector<float>& R, int block = 256)
{
    for (size_t i = 0; i < L.size(); i += (size_t) block)
    {
        const int len = (int) std::min ((size_t) block, L.size() - i);
        float* ch[2] = { L.data() + i, R.data() + i };
        c.process (ch, 2, len);
    }
}

int main()
{
    std::printf ("=== 1) Threshold haritasi ===\n");
    CHECK (std::abs (Compressor::thresholdForAmount (0.0f)   - (-1.0f))  < 1e-4f, "knob %%0   -> %.2f dB", Compressor::thresholdForAmount (0.0f));
    CHECK (std::abs (Compressor::thresholdForAmount (100.0f) - (-32.0f)) < 1e-4f, "knob %%100 -> %.2f dB", Compressor::thresholdForAmount (100.0f));
    CHECK (std::abs (Compressor::thresholdForAmount (10.0f)  - (-4.1f))  < 1e-3f, "knob %%10  -> %.2f dB", Compressor::thresholdForAmount (10.0f));

    std::printf ("\n=== 2) Statik egri dogrulugu (sinus, Hard/dizsiz, autogain kapali) ===\n");
    const float ratioList[3] = { 2.0f, 4.0f, 18.0f };
    for (float ratioT : ratioList)
    {
        Compressor c; c.prepare (kFs);
        Settings s; s.amountPct = (20.0f - 1.0f) / 0.31f; s.ratio = ratioT; s.mode = Hard; s.autoGain = false;
        c.setSettings (s);
        const size_t N = (size_t) (3 * kFs);
        std::vector<float> L (N), R (N);
        const float peak = std::pow (10.0f, -10.0f / 20.0f);                     // -10 dBFS tepe
        for (size_t i = 0; i < N; ++i) L[i] = R[i] = peak * std::sin (2.0f * (float) kPi * 1000.0f * (float) i / (float) kFs);
        run (c, L, R);
        const float expected = (-10.0f - (-20.0f)) * (1.0f - 1.0f / ratioT);      // knee 1.5 dB ihmal
        const float got = c.currentGrDb();
        CHECK (std::abs (got - expected) < 0.8f, "ratio %5.1f : GR beklenen %.2f dB, olculen %.2f dB", ratioT, expected, got);
    }

    std::printf ("\n=== 3) Adaptif attack / release ===\n");
    {
        // (a) duragan sinus: attack uzamali, release uzamali
        Compressor c; c.prepare (kFs);
        Settings s; s.amountPct = 80; s.ratio = 8; s.mode = Smooth; c.setSettings (s);
        const size_t N = (size_t) (2 * kFs);
        std::vector<float> L (N), R (N);
        for (size_t i = 0; i < N; ++i) L[i] = R[i] = 0.3f * std::sin (2.0f * (float) kPi * 220.0f * (float) i / (float) kFs);
        run (c, L, R);
        std::printf ("  duragan sinus   : attack %.1f ms, release %.1f ms\n", c.currentAttackMs(), c.currentReleaseMs());
        CHECK (c.currentAttackMs() > 28.0f,  "duragan sinyalde attack uzadi (> 28 ms)");
        CHECK (c.currentReleaseMs() > 130.0f, "duragan sinyalde release uzadi (> 130 ms)");

        // (b) sessizlik -> nominal
        Compressor c2; c2.prepare (kFs); c2.setSettings (s);
        std::vector<float> Z (N, 0.0f), Z2 (N, 0.0f);
        run (c2, Z, Z2);
        std::printf ("  sessizlik       : attack %.1f ms, release %.1f ms\n", c2.currentAttackMs(), c2.currentReleaseMs());
        CHECK (std::abs (c2.currentAttackMs() - 20.0f) < 0.5f && std::abs (c2.currentReleaseMs() - 100.0f) < 1.0f,
               "sessizlikte 20 ms / 100 ms'ye doner");

        // (c) ani baslangic: attack ilk milisaniyelerde kisalmali
        Compressor c3; c3.prepare (kFs); c3.setSettings (s);
        std::vector<float> A ((size_t) (0.3 * kFs), 0.0f), B (A.size(), 0.0f);
        for (size_t i = (size_t) (0.1 * kFs); i < A.size(); ++i) A[i] = B[i] = 0.5f * std::sin (2.0f * (float) kPi * 440.0f * (float) i / (float) kFs);
        float minAtk = 1e9f, atEnd = 0;
        for (size_t i = 0; i < A.size(); i += 32)
        {
            float* ch[2] = { A.data() + i, B.data() + i };
            c3.process (ch, 2, (int) std::min<size_t> (32, A.size() - i));
            if (i > (size_t) (0.1 * kFs)) minAtk = std::min (minAtk, c3.currentAttackMs());
            atEnd = c3.currentAttackMs();
        }
        std::printf ("  ani baslangic   : en kisa attack %.1f ms -> oturunca %.1f ms\n", minAtk, atEnd);
        CHECK (minAtk < 14.0f && atEnd > minAtk + 8.0f, "transient'te kisaldi, sonra uzadi");
    }

    std::printf ("\n=== 4) Ses benzeri sinyal (10 sn, 100%% knob, 8:1, Smooth) ===\n");
    {
        auto v = makeVoice (10.0);
        std::vector<float> L = v, R = v;
        Compressor c; c.prepare (kFs);
        Settings s; s.amountPct = 100; s.ratio = 8; s.mode = Smooth; s.autoGain = true; c.setSettings (s);

        // blok blok isle, attack/release dagilimini topla
        double sumA = 0, sumR = 0; float minA = 1e9f, maxA = 0, minR = 1e9f, maxR = 0; int cnt = 0; bool bad = false; float maxGr = 0;
        for (size_t i = 0; i < L.size(); i += 256)
        {
            const int len = (int) std::min<size_t> (256, L.size() - i);
            float* ch[2] = { L.data() + i, R.data() + i };
            c.process (ch, 2, len);
            const float a = c.currentAttackMs(), r = c.currentReleaseMs();
            if (i > (size_t) kFs)
            {
                sumA += a; sumR += r; ++cnt;
                minA = std::min (minA, a); maxA = std::max (maxA, a);
                minR = std::min (minR, r); maxR = std::max (maxR, r);
            }
            maxGr = std::max (maxGr, c.currentGrDb());
            for (int k = 0; k < len; ++k) if (! std::isfinite (L[i + (size_t) k])) bad = true;
        }
        std::printf ("  attack  : ort %.1f ms  (min %.1f, max %.1f)\n", sumA / cnt, minA, maxA);
        std::printf ("  release : ort %.1f ms  (min %.1f, max %.1f)\n", sumR / cnt, minR, maxR);
        std::printf ("  en yuksek GR: %.1f dB\n", maxGr);
        CHECK (! bad, "cikista NaN/Inf yok");
        CHECK (maxA > minA + 8.0f && maxR > minR + 40.0f, "attack ve release sese gore gercekten degisiyor");

        // dinamik araligi olc: 100 ms pencerelerin RMS (dB) standart sapmasi, sadece sinyal olan pencereler
        auto spread = [] (const std::vector<float>& x, double from)
        {
            std::vector<float> w; const size_t win = 4800;
            for (size_t i = (size_t) (from * kFs); i + win < x.size(); i += win)
            { const float r = rmsDb (x, i, i + win); if (r > -55.0f) w.push_back (r); }
            double m = 0; for (float r : w) m += r; m /= (double) w.size();
            double sd = 0; for (float r : w) sd += (r - m) * (r - m); sd = std::sqrt (sd / (double) w.size());
            return std::make_pair (m, sd);
        };
        auto in  = spread (v, 4.0);
        auto out = spread (L, 4.0);
        std::printf ("  giris : ort %.1f dB, sapma %.1f dB\n", in.first,  in.second);
        std::printf ("  cikis : ort %.1f dB, sapma %.1f dB\n", out.first, out.second);
        CHECK (out.second < in.second * 0.8, "ses seviyesi dalgalanmasi azaldi");
        CHECK (std::abs (out.first - in.first) < 3.0, "auto gain: ortalama ses yuksekligi ayni (+-3 dB)");
    }

    std::printf ("\n=== 5) Dayaniklilik ===\n");
    {
        Compressor c; c.prepare (kFs);
        Settings s; s.amountPct = 100; s.ratio = 100; s.mode = Hard; c.setSettings (s);
        Rng rng; const size_t N = (size_t) (2 * kFs);
        std::vector<float> L (N), R (N);
        for (size_t i = 0; i < N; ++i) { L[i] = rng.next(); R[i] = rng.next(); }
        L[1000] = NAN; R[2000] = INFINITY;
        run (c, L, R, 64);
        bool bad = false; for (size_t i = 0; i < N; ++i) if (! std::isfinite (L[i]) || ! std::isfinite (R[i])) bad = true;
        CHECK (! bad, "tam olcek gurultu + NaN/Inf girisinde cikis sonlu");

        // mono, farkli ornekleme hizlari
        const double rateList[3] = { 44100.0, 96000.0, 192000.0 };
        for (double sr : rateList)
        {
            Compressor m; m.prepare (sr);
            Settings ms_; ms_.amountPct = 60; ms_.ratio = 4; m.setSettings (ms_);
            std::vector<float> x ((size_t) (sr * 0.5));
            for (size_t i = 0; i < x.size(); ++i) x[i] = 0.4f * std::sin (2.0f * (float) kPi * 300.0f * (float) i / (float) sr);
            float* ch[1] = { x.data() };
            m.process (ch, 1, (int) x.size());
            bool ok = true; for (float v : x) if (! std::isfinite (v)) ok = false;
            CHECK (ok, "mono @ %.0f Hz sonlu (GR %.1f dB)", sr, m.currentGrDb());
        }
    }

    std::printf ("\n=== 6) Bypass ===\n");
    {
        const size_t N = (size_t) (1.0 * kFs);
        std::vector<float> ref (N);
        for (size_t i = 0; i < N; ++i) ref[i] = 0.5f * std::sin (2.0f * (float) kPi * 440.0f * (float) i / (float) kFs);

        Compressor c; c.prepare (kFs);
        Settings s; s.amountPct = 100; s.ratio = 18; s.mode = Hard; s.autoGain = false; s.bypass = true;
        c.setSettings (s);
        std::vector<float> L = ref, R = ref;
        run (c, L, R);
        float maxErr = 0; for (size_t i = (size_t) (0.3 * kFs); i < N; ++i) maxErr = std::max (maxErr, std::abs (L[i] - ref[i]));
        CHECK (maxErr < 1e-6f, "bypass acikken cikis = giris (en buyuk fark %.2e)", maxErr);

        s.bypass = false; c.setSettings (s);
        std::vector<float> L2 = ref, R2 = ref;
        run (c, L2, R2);
        float maxDiff = 0; for (size_t i = (size_t) (0.5 * kFs); i < N; ++i) maxDiff = std::max (maxDiff, std::abs (L2[i] - ref[i]));
        CHECK (maxDiff > 0.05f, "bypass kapaninca kompresyon devreye giriyor (fark %.2f)", maxDiff);

        // gecis tiksiz mi: ardisik orneklerin farki, sinyalin kendi egimine yakin kalmali
        Compressor c2; c2.prepare (kFs);
        Settings s2 = s; s2.bypass = false; c2.setSettings (s2);
        std::vector<float> A = ref, B = ref;
        for (size_t i = 0; i < N; i += 64)
        {
            if (i == N / 2) { s2.bypass = true; c2.setSettings (s2); }
            float* ch[2] = { A.data() + i, B.data() + i };
            c2.process (ch, 2, 64);
        }
        float maxStep = 0; for (size_t i = 1; i < N; ++i) maxStep = std::max (maxStep, std::abs (A[i] - A[i - 1]));
        CHECK (maxStep < 0.2f, "islenmisten bypass'a gecis tiksiz (en buyuk adim %.3f)", maxStep);
    }

    std::printf ("\n=== 7) Low cut (sadece dedektor, 100 Hz) ===\n");
    {
        auto grFor = [] (float freq, bool lowCut)
        {
            Compressor c; c.prepare (kFs);
            Settings s; s.amountPct = (20.0f - 1.0f) / 0.31f; s.ratio = 4; s.mode = Hard; s.autoGain = false; s.lowCut = lowCut;
            c.setSettings (s);
            const size_t N = (size_t) (3 * kFs);
            std::vector<float> L (N), R (N);
            const float peak = std::pow (10.0f, -10.0f / 20.0f);
            for (size_t i = 0; i < N; ++i) L[i] = R[i] = peak * std::sin (2.0f * (float) kPi * freq * (float) i / (float) kFs);
            run (c, L, R);
            return c.currentGrDb();
        };
        const float loOff = grFor (40.0f, false),   loOn = grFor (40.0f, true);
        const float hiOff = grFor (1000.0f, false), hiOn = grFor (1000.0f, true);
        std::printf ("  40 Hz  : kapali %.2f dB, acik %.2f dB\n", loOff, loOn);
        std::printf ("  1 kHz  : kapali %.2f dB, acik %.2f dB\n", hiOff, hiOn);
        CHECK (loOff > loOn + 2.0f && loOn < 1.0f, "low cut acikken 40 Hz dedektoru tetiklemiyor");
        CHECK (std::abs (hiOff - hiOn) < 0.5f, "1 kHz'de low cut acik/kapali ayni");
    }

    std::printf ("\n=== 8) Diz (knee): karaktere ve knob'a gore ===\n");
    {
        const char* names[3] = { "Smooth", "Balanced", "Hard" };
        std::printf ("  knob%%    Smooth  Balanced  Hard\n");
        for (float a : { 0.0f, 25.0f, 50.0f, 75.0f, 100.0f })
            std::printf ("  %4.0f   %7.2f  %8.2f  %5.2f dB\n", a, kneeFor (characterFor (Smooth), a),
                         kneeFor (characterFor (Balanced), a), kneeFor (characterFor (Hard), a));

        bool monotonic = true, ordered = true;
        float prev[3] = { -1, -1, -1 };
        for (int a = 0; a <= 100; ++a)
        {
            float k[3];
            for (int m = 0; m < 3; ++m) { k[m] = kneeFor (characterFor (m), (float) a); if (k[m] < prev[m] - 1e-6f) monotonic = false; prev[m] = k[m]; }
            if (! (k[0] > k[1] && k[1] > k[2])) ordered = false;
        }
        CHECK (monotonic, "knob arttikca diz her karakterde genisliyor (monoton)");
        CHECK (ordered,   "her knob konumunda Smooth > Balanced > Hard");
        CHECK (std::abs (kneeFor (characterFor (Smooth), 100.0f) - characterFor (Smooth).kneeDb) < 1e-3f, "knob %%100'de tam genislik");
        CHECK (std::abs (kneeFor (characterFor (Smooth), 0.0f) - 0.35f * characterFor (Smooth).kneeDb) < 1e-3f, "knob %%0'da tam genisligin %%35'i");
        (void) names;

        // statik egri: sinyal tam esikteyken diz ne kadar bastiriyor?
        auto grAtThreshold = [] (int mode, float amountPct, float ratioT)
        {
            Compressor c; c.prepare (kFs);
            Settings st; st.amountPct = amountPct; st.ratio = ratioT; st.mode = mode; st.autoGain = false;
            c.setSettings (st);
            const float thr  = Compressor::thresholdForAmount (amountPct);
            const float peak = std::pow (10.0f, thr / 20.0f);
            const size_t N = (size_t) (3 * kFs);
            std::vector<float> L (N), R (N);
            for (size_t i = 0; i < N; ++i) L[i] = R[i] = peak * std::sin (2.0f * (float) kPi * 1000.0f * (float) i / (float) kFs);
            run (c, L, R);
            return c.currentGrDb();
        };
        const float gS = grAtThreshold (Smooth, 61.3f, 4.0f), gB = grAtThreshold (Balanced, 61.3f, 4.0f), gH = grAtThreshold (Hard, 61.3f, 4.0f);
        std::printf ("  esikteki sinyal (knob %%61, 4:1): Smooth %.2f dB, Balanced %.2f dB, Hard %.2f dB\n", gS, gB, gH);
        CHECK (gS > gB && gB > gH, "esikte: genis diz daha cok, sert diz daha az bastiriyor");

        // ayni karakter, farkli knob: esige gore AYNI konumda genis diz daha cok bastirir
        const float gLow = grAtThreshold (Smooth, 10.0f, 4.0f), gHigh = grAtThreshold (Smooth, 100.0f, 4.0f);
        std::printf ("  Smooth, esikte: knob %%10 -> %.2f dB, knob %%100 -> %.2f dB\n", gLow, gHigh);
        CHECK (gHigh > gLow + 0.2f, "knob arttikca esik civarindaki gecis daha yumusak (daha genis)");
    }

    std::printf ("\n%s (%d hata)\n", failures == 0 ? "TUM TESTLER GECTI" : "BAZI TESTLER BASARISIZ", failures);
    return failures == 0 ? 0 : 1;
}

#endif // SCC_BUILD_DSP_TEST
