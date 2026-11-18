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
#include <limits>
#include <memory>
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

/** Formants of the synthetic vowel (default "a"; "i" has its first formant below the 2nd harmonic
    of a soprano and two strong formants near the top of the detector's band). */
struct Vowel { double F1 = 730.0, F2 = 1090.0, F3 = 2440.0, B1 = 90.0, B2 = 110.0, B3 = 170.0, g2 = 0.7, g3 = 0.35; };
const Vowel vowelI { 300.0, 2300.0, 3000.0, 70.0, 120.0, 180.0, 0.5, 0.4 };

/** Synthetic sung vowel. f0At(t) gives the fundamental at time t (seconds). */
std::vector<float> makeVoice (double sr, double seconds, const std::function<double (double)>& f0At, float level = 0.3f,
                              const Vowel& vowel = {})
{
    const int n = (int) (sr * seconds);
    std::vector<float> out ((size_t) n);
    Resonator f1, f2, f3;
    f1.set (sr, vowel.F1, vowel.B1);
    f2.set (sr, vowel.F2, vowel.B2);
    f3.set (sr, vowel.F3, vowel.B3);
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
        double y = f1.process (pulse) + vowel.g2 * f2.process (pulse) + vowel.g3 * f3.process (pulse);
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

/** Largest Hann-windowed DFT magnitude within +-3 % of a frequency (an independent spectral probe). */
double peakNear (const std::vector<float>& x, double sr, int start, int length, double hz)
{
    double best = 0.0;
    for (int k = -6; k <= 6; ++k)
    {
        const double f = hz * (1.0 + 0.005 * k);
        double re = 0.0, im = 0.0;
        for (int i = 0; i < length; ++i)
        {
            const double w = 0.5 - 0.5 * std::cos (2.0 * kPi * i / (length - 1));
            const double ph = 2.0 * kPi * f * i / sr;
            re += w * x[(size_t) (start + i)] * std::cos (ph);
            im -= w * x[(size_t) (start + i)] * std::sin (ph);
        }
        best = std::max (best, std::sqrt (re * re + im * im));
    }
    return best;
}

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

        // 3b) level with large transpose / formant shifts (VSTM_TUNE_AUDIT.md R8): before, the
        //     shifted voice came out -7.4 ... +2.6 dB off (real voices: -7.9 ... +2.4 dB)
        {
            const double f = midiToHz (57.40);
            auto x = makeVoice (sr, 2.0, [&] (double) { return f; });
            const double inDb = rmsDb (x, (int) (sr * 1.0), (int) (sr * 0.5));
            double worst = 0.0, worstPeak = -99.0;
            std::string detail;
            for (const auto& tf : std::vector<std::pair<float, float>> { { 0.f, -12.f }, { 0.f, 12.f }, { -12.f, 0.f }, { 12.f, 0.f },
                                                                         { 3.f, 0.f }, { -5.f, -3.f } })
            {
                TuneEngine e;
                e.prepare (sr, true);
                auto p = baseParams();
                p.transpose = tf.first;
                p.formant = tf.second;
                e.setParams (p);
                const auto y = run (e, x, 256);
                const int L = e.getLatencySamples();
                const double d = rmsDb (y, (int) (sr * 1.0) + L, (int) (sr * 0.5)) - inDb;
                double pk = 0.0;
                for (float v : y) pk = std::max (pk, (double) std::abs (v));
                worst = std::max (worst, std::abs (d));
                worstPeak = std::max (worstPeak, 20.0 * std::log10 (pk / 0.3));
                detail += (detail.empty() ? "" : ", ") + std::to_string ((int) tf.first) + "/" + std::to_string ((int) tf.second)
                          + ": " + std::to_string (d).substr (0, 5);
            }
            check (worst <= 1.0 && worstPeak <= 6.0, "level kept with transpose / formant (worst " + std::to_string (worst) + " dB, peak +"
                                                         + std::to_string (worstPeak) + " dB; " + detail + ")");
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

        // 5b) unvoiced -> voiced handover: no level dip at vowel onsets. Consonant noise, then a
        //     vowel sung 40 ct sharp, hard-tuned. The old grain hand-over left one period at
        //     ~65 % level (-3.5 dB) after the onset: half a glottal pulse missing = a thump.
        {
            const double f = midiToHz (57.4);
            std::vector<float> x;
            std::mt19937 rng (3);
            std::normal_distribution<double> noise (0.0, 1.0);
            std::vector<int> onsets;
            for (int r = 0; r < 6; ++r)
            {
                for (int i = 0; i < (int) (sr * 0.06); ++i)
                    x.push_back ((float) (0.01 * noise (rng)));
                onsets.push_back ((int) x.size());
                const auto v = makeVoice (sr, 0.25 + 0.013 * r, [&] (double) { return f; });
                x.insert (x.end(), v.begin(), v.end());
            }
            x.resize (x.size() + (size_t) (sr * 0.1), 0.0f);

            TuneEngine e;
            e.prepare (sr, true);
            auto p = baseParams();
            p.robotism = 0.0f;
            e.setParams (p);
            const auto y = run (e, x, 256);
            const int L = e.getLatencySamples();
            const int w = (int) (sr / f);                       // one period
            double worst = 1.0e9;
            for (const int o : onsets)
                for (int k = o + w; k < o + (int) (0.03 * sr); k += w / 4)
                {
                    double ei = 0.0, eo = 0.0;
                    for (int j = 0; j < w; ++j)
                    {
                        ei += (double) x[(size_t) (k + j)] * x[(size_t) (k + j)];
                        eo += (double) y[(size_t) (k + j + L)] * y[(size_t) (k + j + L)];
                    }
                    worst = std::min (worst, std::sqrt (eo / ei));
                }
            check (worst > 0.85, "no level dip after vowel onsets (worst one-period ratio "
                                 + std::to_string (20.0 * std::log10 (worst)) + " dB)");
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

    // 6b) no note chatter: a singer hovering right between E4 and F4 (64.5 +- 10 ct of
    //     small, fast wobble) must not make the target flip every few milliseconds
    {
        auto f0 = [] (double t)
        {
            return midiToHz (64.5 + 0.06 * std::sin (2.0 * kPi * 7.3 * t) + 0.04 * std::sin (2.0 * kPi * 13.1 * t + 1.0));
        };
        auto x = makeVoice (sr, 2.5, f0);
        for (const float robotism : { 0.0f, 0.5f })
        {
            TuneEngine e;
            e.prepare (sr, true);
            auto p = baseParams();
            p.scale = kMajor;
            p.key = 0;
            p.retuneMs = 0.0f;
            p.robotism = robotism;
            e.setParams (p);
            int changes = 0, hops = 0;
            for (size_t pos = 0; pos < x.size(); pos += 256)
            {
                const int n = (int) std::min ((size_t) 256, x.size() - pos);
                float* chans[2] = { x.data() + pos, nullptr };
                e.process (chans, 1, n);
                TuneEngine::DisplayPoint d;
                while (e.display.pop (d))
                    if (++hops * e.getHopSeconds() > 0.5 && (d.flags & TuneEngine::kNoteChange))
                        ++changes;
            }
            // the engine before the dwell rule flipped 28 (Robotik) / 37 (Dengeli) times here
            check (changes <= 8, std::string (robotism < 0.25f ? "Robotik" : "Dengeli")
                                 + ": no note chatter between E and F (" + std::to_string (changes) + " changes in 2 s)");
        }
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

    // 8b) automatic key when the melody sits on the 2nd degree (D minor sung mostly on E)
    {
        const double notes[] = { 64, 64, 65, 64, 62, 64, 67, 65, 64, 64, 69, 64, 62, 70, 64, 72, 64, 65, 64, 62 };
        auto f0 = [&] (double t)
        {
            const int i = std::min (19, (int) (t / 0.3));
            return midiToHz (notes[i] - 12.0 + 0.15 * std::sin (i * 1.3));
        };
        auto x = makeVoice (sr, 6.0, f0);
        TuneEngine e;
        e.prepare (sr, true);
        auto p = baseParams();
        p.key = -1;
        p.scale = kMinor;
        e.setParams (p);
        run (e, x, 512);
        check (e.detectedKey.load() == 2, "Auto key finds D minor even when E is the most sung note (got " + std::to_string (e.detectedKey.load()) + ")");
    }

    // 8e) Auto key confidence (VSTM_TUNE_AUDIT.md R7). Before: one sung note or two alternating notes
    //     gave a "certain" key after 2 s, an A minor melody with Scale = Major was first called G,
    //     and nothing told the user how sure the guess was.
    {
        auto keyRun = [&] (const std::vector<double>& notes, double noteLen, int scale, std::vector<int>* trace = nullptr)
        {
            auto x = makeVoice (sr, noteLen * (double) notes.size(), [&] (double t)
            {
                const int i = std::min ((int) notes.size() - 1, (int) (t / noteLen));
                const double tin = t - i * noteLen;
                double m = notes[(size_t) i];
                if (i > 0 && tin < 0.03) m = notes[(size_t) i - 1] + (m - notes[(size_t) i - 1]) * tin / 0.03;
                return midiToHz (m + 0.1);
            });
            auto e = std::make_unique<TuneEngine>();
            e->prepare (sr, true);
            auto p = baseParams();
            p.key = -1;
            p.scale = scale;
            e->setParams (p);
            for (size_t pos = 0; pos < x.size(); pos += 4800)              // one look per 100 ms
            {
                const int n = (int) std::min ((size_t) 4800, x.size() - pos);
                float* chans[2] = { x.data() + pos, nullptr };
                e->process (chans, 1, n);
                if (trace) trace->push_back (e->detectedKey.load());
            }
            return e;
        };
        auto none = [] (const std::vector<int>& tr) { for (int k : tr) if (k >= 0) return false; return true; };
        auto only = [] (const std::vector<int>& tr, int key) { for (int k : tr) if (k >= 0 && k != key) return false; return true; };
        auto firstAt = [] (const std::vector<int>& tr, int key) { for (size_t i = 0; i < tr.size(); ++i) if (tr[i] == key) return 0.1 * (double) (i + 1); return -1.0; };

        std::vector<int> tr;
        auto e1 = keyRun (std::vector<double> (10, 60.0), 0.5, kMajor, &tr);
        check (none (tr) && e1->keyConfidence.load() < 0.5f,
               "Auto key: one note (C4, 5 s) is not a key: none locked, confidence " + std::to_string (e1->keyConfidence.load()));

        tr.clear();
        keyRun ({ 60, 67, 60, 67, 60, 67, 60, 67, 60, 67, 60, 67 }, 0.5, kMajor, &tr);
        check (none (tr), "Auto key: two alternating notes (C4 / G4, 6 s) are not a key: none locked");

        const std::vector<double> aMin { 57, 59, 60, 62, 64, 65, 67, 69, 64, 60, 62, 59, 57, 60, 64, 57 };
        tr.clear();
        keyRun (aMin, 0.5, kMajor, &tr);
        check (only (tr, 0) && tr.back() == 0, "Auto key: A minor melody with Scale = Major: only C (relative major) is ever shown, from "
                                                    + std::to_string (firstAt (tr, 0)) + " s");

        const std::vector<double> cMaj { 60, 62, 64, 65, 67, 69, 71, 72, 67, 64, 65, 62, 60, 64, 67, 60 };
        auto e2 = keyRun (cMaj, 0.35, kMajor);
        check (e2->detectedKey.load() == 0 && e2->keyConfidence.load() >= 0.6f,
               "Auto key: C major melody: C with confidence " + std::to_string (e2->keyConfidence.load()));

        // modulation to G major (F# four times in 8 s): followed; chromatic passing notes (F#, Bb) in C: ignored
        std::vector<double> mod = cMaj;
        for (double n : { 67, 69, 71, 72, 74, 76, 78, 79, 78, 74, 71, 66, 67, 69, 71, 78 }) mod.push_back (n);
        tr.clear();
        keyRun (mod, 0.5, kMajor, &tr);
        const double gAt = firstAt (tr, 7);
        check (tr.back() == 7 && gAt > 8.0 && gAt < 15.0, "Auto key: C major -> G major at 8 s is followed at " + std::to_string (gAt) + " s");

        std::vector<double> passing = cMaj;
        passing.insert (passing.end(), cMaj.begin(), cMaj.end());
        passing[10] = 66; passing[16] = 70; passing[22] = 66; passing[28] = 70;
        tr.clear();
        keyRun (passing, 0.5, kMajor, &tr);
        check (only (tr, 0) && tr.back() == 0, "Auto key: chromatic passing notes (F#, Bb) in a C major melody do not move the key");
    }

    // 8g) custom scale: the note set counts from the key like a scale; Auto finds the key with it.
    //     A minor pentatonic as a custom set (1 b3 4 5 b7), a melody in A minor pentatonic, key Auto.
    {
        const double notes[] = { 57, 60, 62, 64, 67, 69, 67, 64, 62, 60, 57, 60, 64, 62, 57, 69, 67, 64, 60, 57 };
        auto x = makeVoice (sr, 6.0, [&] (double t) { return midiToHz (notes[std::min (19, (int) (t / 0.3))] + 0.2); });
        TuneEngine e;
        e.prepare (sr, true);
        auto p = baseParams();
        p.key = -1;
        p.scale = kMajor;                           // ignored while the custom set is on
        p.customScale = true;
        p.customMask = (uint16_t) ((1u << 0) | (1u << 3) | (1u << 5) | (1u << 7) | (1u << 10));
        e.setParams (p);
        auto y = run (e, x, 512);
        const int L = e.getLatencySamples();
        const double hz = measureHz (y, sr, (int) (sr * 5.47) + L, (int) (sr * 0.15), midiToHz (60.0));  // note 18: C4 sung 20 ct sharp
        check (e.detectedKey.load() == 9 && std::abs (cents (hz, midiToHz (60.0))) < 5.0,
               "custom scale (A minor pentatonic set): Auto finds A (got " + std::to_string (e.detectedKey.load()) + "), C4 +20 ct -> "
                   + std::to_string (cents (hz, midiToHz (60.0))) + " ct");
    }

    // 8f) Adaptive (off by default; off = the engine without it). It keeps a sung vibrato and lets
    //     a slow slide through on the balanced / human side, leaves the robotic end alone, and helps
    //     short notes of fast runs land on the human side. Engine output pitch (display points).
    {
        struct Track { std::vector<double> t, in, out; };
        auto runA = [&] (const std::function<double (double)>& f0, double secs, float robotism, float retune, bool adaptive)
        {
            auto x = makeVoice (sr, secs, [&] (double t) { return midiToHz (f0 (t)); });
            TuneEngine e;
            e.prepare (sr, true);
            auto p = baseParams();
            p.robotism = robotism;
            p.retuneMs = retune;
            p.humanize = 0.35f;
            p.adaptive = adaptive;
            e.setParams (p);
            Track tr;
            int hops = 0;
            for (size_t pos = 0; pos < x.size(); pos += 256)
            {
                const int n = (int) std::min ((size_t) 256, x.size() - pos);
                float* chans[2] = { x.data() + pos, nullptr };
                e.process (chans, 1, n);
                TuneEngine::DisplayPoint d;
                while (e.display.pop (d))
                {
                    const double t = ++hops * e.getHopSeconds() - 0.024;
                    if (d.flags & TuneEngine::kVoiced) { tr.t.push_back (t); tr.in.push_back (d.inMidi); tr.out.push_back (d.outMidi); }
                }
            }
            return tr;
        };
        auto pp = [] (const Track& tr, double a, double b)
        {
            double lo = 1e9, hi = -1e9;
            for (size_t k = 0; k < tr.t.size(); ++k) if (tr.t[k] > a && tr.t[k] < b) { lo = std::min (lo, tr.out[k]); hi = std::max (hi, tr.out[k]); }
            return (hi - lo) * 100.0;
        };
        auto vib = [] (double t) { return 57.1 + 0.4 * std::sin (2.0 * kPi * 5.5 * t); };
        const double balancedOff = pp (runA (vib, 3.0, 0.5f, 15.0f, false), 1.5, 2.5);
        const double balancedOn = pp (runA (vib, 3.0, 0.5f, 15.0f, true), 1.5, 2.5);
        const double robotOn = pp (runA (vib, 3.0, 0.0f, 0.0f, true), 1.5, 2.5);
        check (balancedOn > 1.25 * balancedOff && robotOn < 5.0,
               "Adaptive keeps a sung vibrato when balanced (" + std::to_string (balancedOff) + " -> " + std::to_string (balancedOn)
                   + " ct p-p), the robotic end still flattens it (" + std::to_string (robotOn) + " ct)");

        auto slide = [] (double t) { return 57.0 + 3.0 * std::clamp ((t - 1.0) / 0.4, 0.0, 1.0); };
        auto dev = [] (const Track& tr)
        {
            double s = 0.0; int n = 0;
            for (size_t k = 0; k < tr.t.size(); ++k) if (tr.t[k] > 1.05 && tr.t[k] < 1.35) { s += std::abs (tr.out[k] - tr.in[k]); ++n; }
            return n ? 100.0 * s / n : 0.0;
        };
        const double slideOff = dev (runA (slide, 2.0, 0.5f, 15.0f, false)), slideOn = dev (runA (slide, 2.0, 0.5f, 15.0f, true));
        check (slideOn < 0.8 * slideOff, "Adaptive lets a slow slide through (mean pull " + std::to_string (slideOff) + " -> "
                                              + std::to_string (slideOn) + " ct)");

        const double runNotes[] = { 57, 59, 60, 62, 64, 62, 60, 59, 57, 59, 60, 62, 64, 65, 64, 62, 60, 59, 57, 55 };
        auto fastRun = [&] (double t) { return runNotes[std::min (19, (int) (t / 0.1))] + 0.3; };
        auto landing = [] (const Track& tr)
        {
            double s = 0.0; int n = 0;
            for (size_t k = 0; k < tr.t.size(); ++k)
            {
                const double ph = std::fmod (tr.t[k], 0.1);
                if (tr.t[k] > 0.3 && ph > 0.05 && ph < 0.095) { s += std::abs (tr.out[k] - std::round (tr.out[k])); ++n; }
            }
            return n ? 100.0 * s / n : 0.0;
        };
        const double runOff = landing (runA (fastRun, 2.0, 0.9f, 15.0f, false)), runOn = landing (runA (fastRun, 2.0, 0.9f, 15.0f, true));
        check (runOn < runOff - 5.0, "Adaptive lands the short notes of a fast run on the human side (" + std::to_string (runOff)
                                         + " -> " + std::to_string (runOn) + " ct off the note)");
    }

    // 8c) pitch tracker: short harmonic-ratio errors are ignored, real leaps accepted
    {
        PitchTracker tr;
        auto est = [] (double hz) { PitchDetector::Result r; r.voiced = true; r.period = 48000.0 / hz; r.clarity = 0.9f; return r; };
        bool onsetOk = ! tr.process (est (220.0)).voiced && ! tr.process (est (220.0)).voiced && tr.process (est (220.0)).voiced;
        bool spikeHeld = true;
        for (double hz : { 440.0, 440.0, 660.0, 110.0, 440.0 })            // 5-hop burst of octave / fifth errors
            spikeHeld = spikeHeld && std::abs (tr.process (est (hz)).period - 48000.0 / 220.0) < 1e-6;
        PitchDetector::Result last;
        for (int i = 0; i < 8; ++i) last = tr.process (est (440.0));     // a real octave leap
        const bool leapAccepted = std::abs (last.period - 48000.0 / 440.0) < 1e-6;
        PitchDetector::Result gap; gap.voiced = false;
        const bool bridged = tr.process (gap).voiced;
        check (onsetOk && spikeHeld && leapAccepted && bridged, "pitch tracker: 3-hop onset, spikes held, 20 ms leap accepted, dropouts bridged");
    }

    // 8d) legato octave leaps (25 ms slide), sung 30 ct sharp: the detector follows the sung octave
    //     and the output has no sub-octave. Before: going up an octave, the detector stayed an
    //     octave low for the whole note (the lower octave's CMND dip reads deeper on the decimated
    //     lag grid): 2-period grains, f0/2 in the output 31 dB above the dry signal (soprano A5).
    {
        struct Case { const char* name; double lo, hi; int range; Vowel v; };
        const Case cases[] = { { "soprano i A4-A5-A4", 69.3, 81.3, 1, vowelI }, { "soprano i A4-A5-A4 (Soprano range)", 69.3, 81.3, 0, vowelI },
                               { "tenor a A3-A4-A3", 57.3, 69.3, 1, Vowel {} }, { "bass a G2-G3-G2", 43.3, 55.3, 1, Vowel {} } };
        for (const auto& c : cases)
        {
            const double noteLen = 0.8, slide = 0.025;
            auto f0 = [&] (double t)
            {
                const double notes[] = { c.lo, c.hi, c.lo };
                const int i = std::min (2, (int) (t / noteLen));
                const double tin = t - i * noteLen;
                double m = notes[i];
                if (i > 0 && tin < slide) m = notes[i - 1] + (notes[i] - notes[i - 1]) * tin / slide;
                return midiToHz (m);
            };
            auto x = makeVoice (sr, 3 * noteLen, f0, 0.3f, c.v);
            TuneEngine e;
            e.prepare (sr, true);
            auto p = baseParams();
            p.robotism = 0.0f;
            p.range = c.range;
            e.setParams (p);
            auto y = x;
            int onOctave = 0, voiced = 0;
            int hops = 0;
            for (size_t pos = 0; pos < y.size(); pos += 256)
            {
                const int n = (int) std::min ((size_t) 256, y.size() - pos);
                float* chans[2] = { y.data() + pos, nullptr };
                e.process (chans, 1, n);
                TuneEngine::DisplayPoint d;
                while (e.display.pop (d))
                {
                    const double t = ++hops * e.getHopSeconds() - 0.024;          // ~ centre of the analysis window
                    if (t < noteLen + slide + 0.030 || t > 2.0 * noteLen - 0.05 || ! (d.flags & TuneEngine::kVoiced))
                        continue;
                    ++voiced;
                    if (std::abs (d.inMidi - c.hi) < 1.0) ++onOctave;
                }
            }
            const int L = e.getLatencySamples();
            const int a = (int) ((noteLen + 0.3) * sr), len = (int) (0.4 * sr);
            const double target = midiToHz (std::round (c.hi)), sung = midiToHz (c.hi);
            const double subOut = 20.0 * std::log10 (peakNear (y, sr, a + L, len, target / 2) / peakNear (y, sr, a + L, len, target));
            const double subDry = 20.0 * std::log10 (peakNear (x, sr, a, len, sung / 2) / peakNear (x, sr, a, len, sung));
            const double frac = voiced ? (double) onOctave / voiced : 0.0;
            // f0/2 within 6 dB of the dry signal, or inaudibly low (60 dB under f0; the synthetic dry
            // signal's own floor is about -79 dB, where two spectral probes differ by several dB)
            check (frac > 0.99 && subOut < std::max (subDry + 6.0, -60.0),
                   std::string ("octave leap ") + c.name + ": detector on the sung octave " + std::to_string ((int) std::lround (100.0 * frac))
                       + " % of the upper note (from 30 ms after the slide), f0/2 " + std::to_string (subOut) + " dB vs dry "
                       + std::to_string (subDry) + " dB");
        }
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

    // 10) a NaN and an Inf sample in the input (a broken plugin before this one): the output stays
    //     finite and the tuning comes back. The engine before this test kept NaN in the detector's
    //     filter state for good: no correction any more until the next reset.
    {
        const double f = midiToHz (57.40);
        auto x = makeVoice (sr, 4.0, [&] (double) { return f; });
        x[(size_t) (1.0 * sr)] = std::numeric_limits<float>::quiet_NaN();
        x[(size_t) (1.5 * sr)] = std::numeric_limits<float>::infinity();
        x[(size_t) (1.6 * sr)] = 1.0e30f;                     // finite, but absurd
        TuneEngine e;
        e.prepare (sr, true);
        e.setParams (baseParams());
        const auto y = run (e, x, 256);
        bool finite = true;
        for (float v : y) finite = finite && std::isfinite (v);
        const double c = cents (measureHz (y, sr, (int) (sr * 3.0), (int) (sr * 0.2), 220.0), 220.0);
        check (finite && std::abs (c) < 1.0, "NaN / Inf / huge input samples: output finite, tuning back (" + std::to_string (c) + " ct)");
    }

    // 11) NaN parameter values for half a second (a damaged preset or session): the output stays
    //     finite and is normal again afterwards. Before: amount / formant / transpose silenced the
    //     output for good, mix made it NaN for good.
    {
        const double f = midiToHz (57.40);
        const auto x = makeVoice (sr, 4.0, [&] (double) { return f; });
        bool finite = true, normal = true;
        std::string detail;
        const char* names[] = { "amount", "formant", "transpose", "mix", "retune", "robotism" };
        for (int which = 0; which < 6; ++which)
        {
            TuneEngine e;
            e.prepare (sr, true);
            auto y = x;
            for (size_t pos = 0; pos < y.size(); pos += 256)
            {
                auto p = baseParams();
                if (pos >= (size_t) sr && pos < (size_t) (1.5 * sr))
                {
                    const float nan = std::numeric_limits<float>::quiet_NaN();
                    if (which == 0) p.amount = nan;
                    if (which == 1) p.formant = nan;
                    if (which == 2) p.transpose = nan;
                    if (which == 3) p.mix = nan;
                    if (which == 4) p.retuneMs = nan;
                    if (which == 5) p.robotism = nan;
                }
                e.setParams (p);
                const int n = (int) std::min ((size_t) 256, y.size() - pos);
                float* chans[2] = { y.data() + pos, nullptr };
                e.process (chans, 1, n);
            }
            for (float v : y) finite = finite && std::isfinite (v);
            const double c = cents (measureHz (y, sr, (int) (sr * 3.0), (int) (sr * 0.2), 220.0), 220.0);
            const double lvl = rmsDb (y, (int) (sr * 2.8), (int) (sr * 0.5)) - rmsDb (x, (int) (sr * 2.8), (int) (sr * 0.5));
            normal = normal && std::abs (c) < 1.0 && std::abs (lvl) < 1.0;
            detail += std::string (which ? ", " : "") + names[which] + " " + std::to_string ((int) std::lround (c)) + " ct";
        }
        check (finite && normal, "NaN parameters for 0.5 s: output finite, normal afterwards (" + detail + ")");
    }

    // 12) REALTIME <-> HQ while audio runs (what processBlock does at a block boundary): no stretch of
    //     silence, no click, the tuning and the automatic key are kept, the new latency is in effect.
    //     Before: a full reset = 40 ms of exact zeros, a 5x sample step, the detected key forgotten.
    {
        const double f = midiToHz (57.40);
        auto x = makeVoice (sr, 3.0, [&] (double) { return f; });
        TuneEngine e;
        e.prepare (sr, true);
        e.setParams (baseParams());
        auto y = x;
        int latHQ = 0, latRT = 0;
        for (size_t pos = 0; pos < y.size(); pos += 256)
        {
            if (pos == (size_t) (1.0 * sr) / 256 * 256) e.setRealtime (false);
            if (pos == (size_t) (2.0 * sr) / 256 * 256) e.setRealtime (true);
            if (pos == (size_t) (1.5 * sr) / 256 * 256) latHQ = e.getLatencySamples();
            if (pos == (size_t) (2.5 * sr) / 256 * 256) latRT = e.getLatencySamples();
            const int n = (int) std::min ((size_t) 256, y.size() - pos);
            float* chans[2] = { y.data() + pos, nullptr };
            e.process (chans, 1, n);
        }
        size_t run0 = 0, longest = 0;
        bool finite = true;
        for (size_t i = (size_t) (0.5 * sr); i < y.size(); ++i)
        {
            finite = finite && std::isfinite (y[i]);
            run0 = y[i] == 0.0f ? run0 + 1 : 0;
            longest = std::max (longest, run0);
        }
        auto maxStep = [&] (double a, double b)
        {
            double m = 0.0;
            for (size_t i = (size_t) (a * sr) + 1; i < (size_t) (b * sr); ++i) m = std::max (m, (double) std::abs (y[i] - y[i - 1]));
            return m;
        };
        const double steady = maxStep (0.6, 0.95);
        const double atSwitch = std::max (maxStep (0.98, 1.10), maxStep (1.98, 2.10));
        const double c1 = cents (measureHz (y, sr, (int) (sr * 1.5), (int) (sr * 0.2), 220.0), 220.0);
        const double c2 = cents (measureHz (y, sr, (int) (sr * 2.6), (int) (sr * 0.2), 220.0), 220.0);
        check (finite && longest < (size_t) (0.001 * sr) && atSwitch <= 1.5 * steady && latHQ == TuneEngine::latencyFor (sr, false)
                   && latRT == TuneEngine::latencyFor (sr, true) && std::abs (c1) < 1.0 && std::abs (c2) < 1.0,
               "REALTIME switch: longest zero run " + std::to_string (longest) + " samples, step " + std::to_string (atSwitch)
                   + " vs steady " + std::to_string (steady) + ", latency " + std::to_string (latHQ) + " / " + std::to_string (latRT)
                   + ", tuning " + std::to_string (c1) + " / " + std::to_string (c2) + " ct");

        // the automatic key survives the switch (C major melody, switched at 3.0 s, after the key is found)
        const double notes[] = { 60, 62, 64, 65, 67, 69, 71, 72, 67, 64, 60, 62, 64, 65, 67, 72 };
        auto mel = makeVoice (sr, 5.6, [&] (double t)
        {
            const int i = std::min (15, (int) (t / 0.35));
            return midiToHz (notes[i] + 0.18 * std::sin (i * 1.7));
        });
        TuneEngine k;
        k.prepare (sr, true);
        auto p = baseParams();
        p.key = -1;
        p.scale = kMajor;
        k.setParams (p);
        bool lost = false;
        int before = -2;
        for (size_t pos = 0; pos < mel.size(); pos += 512)
        {
            if (pos == (size_t) (3.0 * sr) / 512 * 512) { before = k.detectedKey.load(); k.setRealtime (false); }
            const int n = (int) std::min ((size_t) 512, mel.size() - pos);
            float* chans[2] = { mel.data() + pos, nullptr };
            k.process (chans, 1, n);
            if (before >= 0 && k.detectedKey.load() != before) lost = true;
        }
        check (before == 0 && ! lost, "REALTIME switch keeps the automatic key (C before, "
                                          + std::to_string (k.detectedKey.load()) + " after)");
    }

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
