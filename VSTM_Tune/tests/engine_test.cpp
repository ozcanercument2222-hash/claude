//==============================================================================
//  VSTM Tune - ses motoru testi (JUCE gerektirmez)
//
//    g++ -std=c++17 -O2 -I../Source tests/engine_test.cpp -o engine_test && ./engine_test
//
//  Yapay bir "aaa" vokali uretilir (glottal darbe dizisi + uc formant rezonatoru,
//  istenirse vibrato ve melodi) ve motordan gecirilir. Cikisin perdesi motordan
//  bagimsiz bir otokorelasyon olcumuyle dogrulanir.
//==============================================================================
#include "dsp/TuneEngine.h"

#include <cstdio>
#include <functional>
#include <random>
#include <string>

using namespace vstm;

namespace
{
int failures = 0;

void check (bool ok, const std::string& what)
{
    std::printf ("  [%s] %s\n", ok ? " OK " : "FAIL", what.c_str());
    if (! ok) ++failures;
}

//------------------------------------------------------------------------------
struct Resonator
{
    double b0 = 0, a1 = 0, a2 = 0, y1 = 0, y2 = 0;
    void set (double sr, double f, double bw)
    {
        const double r = std::exp (-kPi * bw / sr);
        a1 = -2.0 * r * std::cos (2.0 * kPi * f / sr);
        a2 = r * r;
        b0 = (1.0 - r) * 2.0;
    }
    double process (double x) { const double y = b0 * x - a1 * y1 - a2 * y2; y2 = y1; y1 = y; return y; }
};

/** Synthetic sung vowel. f0At(t) gives the fundamental at time t (seconds). */
std::vector<float> makeVoice (double sr, double seconds, const std::function<double (double)>& f0At, float level = 0.3f)
{
    const int n = (int) (sr * seconds);
    std::vector<float> out ((size_t) n);
    Resonator f1, f2, f3;
    f1.set (sr, 730.0, 90.0);
    f2.set (sr, 1090.0, 110.0);
    f3.set (sr, 2440.0, 170.0);
    std::mt19937 rng (7);
    std::normal_distribution<double> noise (0.0, 1.0);

    double phase = 0.0;
    double prevPulse = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double t = i / sr;
        const double f0 = f0At (t);
        phase += f0 / sr;
        // band-limited-ish glottal source: sum of harmonics with -12 dB/oct tilt
        double src = 0.0;
        const int harmonics = std::min (40, (int) (0.45 * sr / std::max (40.0, f0)));
        for (int h = 1; h <= harmonics; ++h)
            src += std::sin (2.0 * kPi * h * phase) / (double) (h * h) * (h == 1 ? 1.0 : 1.6);
        const double pulse = src - prevPulse;      // differentiate: spectral shape of voiced speech
        prevPulse = src;
        double y = f1.process (pulse) + 0.7 * f2.process (pulse) + 0.35 * f3.process (pulse);
        y += 0.0005 * noise (rng);
        out[(size_t) i] = (float) y;
    }

    float peak = 1.0e-9f;
    for (float v : out) peak = std::max (peak, std::abs (v));
    for (auto& v : out) v *= level / peak;
    return out;
}

/** Independent pitch measurement: normalised autocorrelation around an expected frequency. */
double measureHz (const std::vector<float>& x, double sr, int start, int length, double expectHz)
{
    const double expectT = sr / expectHz;
    const int lo = std::max (2, (int) (expectT * 0.80));
    const int hi = (int) (expectT * 1.25) + 2;
    std::vector<double> r ((size_t) (hi + 2), 0.0);
    for (int lag = lo - 1; lag <= hi + 1; ++lag)
    {
        double num = 0.0, ea = 0.0, eb = 0.0;
        for (int j = 0; j < length; ++j)
        {
            const double a = x[(size_t) (start + j)], b = x[(size_t) (start + j + lag)];
            num += a * b; ea += a * a; eb += b * b;
        }
        r[(size_t) lag] = num / std::sqrt (ea * eb + 1.0e-20);
    }
    int best = lo;
    for (int lag = lo; lag <= hi; ++lag)
        if (r[(size_t) lag] > r[(size_t) best]) best = lag;
    const double a = r[(size_t) best - 1], b = r[(size_t) best], c = r[(size_t) best + 1];
    const double den = a - 2.0 * b + c;
    const double p = best + (std::abs (den) > 1.0e-12 ? 0.5 * (a - c) / den : 0.0);
    return sr / p;
}

double cents (double hz, double refHz) { return 1200.0 * std::log2 (hz / refHz); }

std::vector<float> run (TuneEngine& e, std::vector<float> x, int block)
{
    for (size_t pos = 0; pos < x.size(); pos += (size_t) block)
    {
        const int n = (int) std::min ((size_t) block, x.size() - pos);
        float* chans[2] = { x.data() + pos, nullptr };
        e.process (chans, 1, n);
    }
    return x;
}

TuneEngine::Params baseParams()
{
    TuneEngine::Params p;
    p.key = 0;
    p.scale = kChromatic;
    p.retuneMs = 0.0f;
    p.humanize = 0.0f;
    p.robotism = 0.5f;
    return p;
}

double rmsDb (const std::vector<float>& x, int start, int len)
{
    double s = 0.0;
    for (int i = 0; i < len; ++i) s += (double) x[(size_t) (start + i)] * x[(size_t) (start + i)];
    return 10.0 * std::log10 (s / len + 1.0e-20);
}
} // namespace

int main()
{
    for (const double sr : { 44100.0, 48000.0, 96000.0 })
    {
        std::printf ("\n=== %.0f Hz ===\n", sr);

        // 1) hard tune: steady A3 sung 40 cents sharp
        {
            const double sung = midiToHz (57.40);
            auto x = makeVoice (sr, 2.0, [&] (double) { return sung; });
            TuneEngine e;
            e.prepare (sr, true);
            e.setParams (baseParams());
            auto y = run (e, x, 256);
            const double hz = measureHz (y, sr, (int) (sr * 1.2), (int) (sr * 0.2), 220.0);
            check (std::abs (cents (hz, 220.0)) < 4.0, "hard tune: A3 +40 ct -> " + std::to_string (cents (hz, 220.0)) + " ct");
            const double inDb = rmsDb (x, (int) (sr * 1.0), (int) (sr * 0.5));
            const double outDb = rmsDb (y, (int) (sr * 1.0), (int) (sr * 0.5));
            check (std::abs (outDb - inDb) < 1.5, "level kept: " + std::to_string (outDb - inDb) + " dB");
        }

        // 2) identity when correction is off: output == input delayed by the reported latency
        {
            const double f = midiToHz (55.2);
            auto x = makeVoice (sr, 1.5, [&] (double t) { return f * (1.0 + 0.01 * std::sin (2 * kPi * 5.0 * t)); });
            TuneEngine e;
            e.prepare (sr, true);
            auto p = baseParams();
            p.amount = 0.0f;
            e.setParams (p);
            auto y = run (e, x, 333);
            const int L = e.getLatencySamples();
            double err = 0.0, sig = 0.0;
            for (int i = (int) (sr * 0.6); i < (int) (sr * 1.4); ++i)
            {
                const double d = y[(size_t) i] - x[(size_t) (i - L)];
                err += d * d; sig += (double) x[(size_t) (i - L)] * x[(size_t) (i - L)];
            }
            const double snr = 10.0 * std::log10 (sig / (err + 1.0e-20));
            check (snr > 20.0, "correction off is transparent and latency-aligned: SNR " + std::to_string (snr) + " dB");
        }

        // 3) transpose +12 and -12
        for (const float tr : { 12.0f, -12.0f, 7.0f })
        {
            const double f = midiToHz (57.0);
            auto x = makeVoice (sr, 1.6, [&] (double) { return f; });
            TuneEngine e;
            e.prepare (sr, false);
            auto p = baseParams();
            p.transpose = tr;
            e.setParams (p);
            auto y = run (e, x, 512);
            const double expect = f * std::pow (2.0, tr / 12.0);
            const double hz = measureHz (y, sr, (int) (sr * 1.0), (int) (sr * 0.2), expect);
            check (std::abs (cents (hz, expect)) < 6.0, "transpose " + std::to_string ((int) tr) + " st: " + std::to_string (cents (hz, expect)) + " ct");
        }

        // 4) formant shift keeps the pitch
        {
            const double f = midiToHz (60.0);
            auto x = makeVoice (sr, 1.6, [&] (double) { return f; });
            TuneEngine e;
            e.prepare (sr, true);
            auto p = baseParams();
            p.formant = 3.0f;
            e.setParams (p);
            auto y = run (e, x, 128);
            const double hz = measureHz (y, sr, (int) (sr * 1.0), (int) (sr * 0.2), f);
            check (std::abs (cents (hz, f)) < 8.0, "formant +3 keeps pitch: " + std::to_string (cents (hz, f)) + " ct");
        }

        // 5) block-size independence
        {
            auto x = makeVoice (sr, 1.2, [&] (double t) { return t < 0.6 ? 196.0 : 233.0; });
            auto run2 = [&] (int block)
            {
                TuneEngine e;
                e.prepare (sr, true);
                auto p = baseParams();
                p.retuneMs = 20.0f; p.humanize = 0.4f; p.robotism = 0.7f; p.formant = 1.5f;
                e.setParams (p);
                return run (e, x, block);
            };
            const auto a = run2 (64), b = run2 (1013);
            float diff = 0.0f;
            for (size_t i = 0; i < a.size(); ++i) diff = std::max (diff, std::abs (a[i] - b[i]));
            check (diff < 1.0e-6f, "block size independent (max diff " + std::to_string (diff) + ")");
        }
    }

    const double sr = 48000.0;
    std::printf ("\n=== Robotism / humanize / key (48 kHz) ===\n");

    // 6) robotism: note transition time, robotic vs human
    {
        // singer jumps from A3 to C4 at t = 1.0 s (fast, 20 ms slide)
        auto f0 = [] (double t)
        {
            const double a = 57.0, c = 60.0;
            const double k = std::clamp ((t - 1.0) / 0.02, 0.0, 1.0);
            return midiToHz (a + (c - a) * k);
        };
        auto x = makeVoice (sr, 2.0, f0);

        auto transitionMs = [&] (float robotism)
        {
            TuneEngine e;
            e.prepare (sr, true);
            auto p = baseParams();
            p.scale = kMajor;
            p.key = 0;
            p.retuneMs = 15.0f;
            p.robotism = robotism;
            e.setParams (p);
            auto y = run (e, x, 256);
            const int L = e.getLatencySamples();
            // track output pitch every 2 ms between 0.9 and 1.4 s (input time)
            double t10 = -1.0, t90 = -1.0;
            for (double t = 0.9; t < 1.4; t += 0.002)
            {
                const int start = (int) (t * sr) + L;
                const double hz = measureHz (y, sr, start, (int) (sr * 0.008), 240.0);
                const double m = hzToMidi (hz);
                if (t10 < 0 && m > 57.3) t10 = t;
                if (t90 < 0 && m > 59.7) t90 = t;
            }
            return (t90 - t10) * 1000.0;
        };

        const double robotMs = transitionMs (0.0f);
        const double humanMs = transitionMs (1.0f);
        check (robotMs >= 0.0 && robotMs < 25.0, "Robotik: note change is a step (" + std::to_string (robotMs) + " ms)");
        check (humanMs > 45.0, "Insansi: note change glides (" + std::to_string (humanMs) + " ms)");
    }

    // 7) vibrato: flattened when robotic, kept when human
    {
        auto f0 = [] (double t) { return midiToHz (57.0 + 0.5 * std::sin (2.0 * kPi * 5.5 * t)); };
        auto x = makeVoice (sr, 2.0, f0);
        auto vibDepth = [&] (float robotism, float retune)
        {
            TuneEngine e;
            e.prepare (sr, false);
            auto p = baseParams();
            p.retuneMs = retune;
            p.robotism = robotism;
            p.humanize = 0.35f;
            e.setParams (p);
            auto y = run (e, x, 256);
            const int L = e.getLatencySamples();
            double lo = 1e9, hi = -1e9;
            for (double t = 1.0; t < 1.6; t += 0.004)
            {
                const double m = hzToMidi (measureHz (y, sr, (int) (t * sr) + L, (int) (sr * 0.01), 220.0));
                lo = std::min (lo, m); hi = std::max (hi, m);
            }
            return (hi - lo) * 100.0;      // peak-to-peak cents
        };
        const double robot = vibDepth (0.0f, 15.0f);
        const double human = vibDepth (1.0f, 60.0f);
        check (robot < 25.0, "Robotik flattens a +-50 ct vibrato to " + std::to_string (robot) + " ct p-p");
        check (human > 60.0, "Insansi keeps the vibrato: " + std::to_string (human) + " ct p-p");
    }

    // 8) automatic key: a C major melody sung slightly out of tune
    {
        const double notes[] = { 60, 62, 64, 65, 67, 69, 71, 72, 67, 64, 60, 62, 64, 65, 67, 72 };
        auto f0 = [&] (double t)
        {
            const int i = std::min (15, (int) (t / 0.35));
            return midiToHz (notes[i] + 0.18 * std::sin (i * 1.7));
        };
        auto x = makeVoice (sr, 5.6, f0);
        TuneEngine e;
        e.prepare (sr, true);
        auto p = baseParams();
        p.key = -1;
        p.scale = kMajor;
        e.setParams (p);
        run (e, x, 512);
        check (e.detectedKey.load() == 0, "Auto key finds C major (got " + std::to_string (e.detectedKey.load()) + ")");
    }

    // 9) silence in, silence out; no NaN
    {
        std::vector<float> x ((size_t) sr, 0.0f);
        TuneEngine e;
        e.prepare (sr, true);
        e.setParams (baseParams());
        auto y = run (e, x, 256);
        bool clean = true;
        for (float v : y) if (! std::isfinite (v) || std::abs (v) > 1.0e-6f) clean = false;
        check (clean, "silence stays silent");
    }

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
