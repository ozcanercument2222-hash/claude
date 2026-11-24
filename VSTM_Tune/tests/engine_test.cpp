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

/** A sung line: notes (MIDI, seconds), each with its own detune and vibrato, joined by short slides. */
struct Note { double midi, len, detune = 0.0, vibCt = 0.0; };
std::function<double (double)> melodyF0 (std::vector<Note> notes, double slide = 0.025)
{
    std::vector<double> starts;
    double t0 = 0.0;
    for (const auto& n : notes) { starts.push_back (t0); t0 += n.len; }
    return [notes, starts, slide] (double t)
    {
        size_t i = 0;
        while (i + 1 < notes.size() && t >= starts[i + 1]) ++i;
        const auto& n = notes[i];
        const double tin = t - starts[i];
        double m = n.midi + n.detune + 0.01 * n.vibCt * std::sin (2.0 * kPi * 5.6 * tin) * std::min (1.0, tin / 0.15);
        if (i > 0 && tin < slide)
        {
            const auto& p = notes[i - 1];
            m = p.midi + p.detune + (m - p.midi - p.detune) * tin / slide;
        }
        return midiToHz (m);
    };
}
double totalLen (const std::vector<Note>& notes) { double s = 0.0; for (const auto& n : notes) s += n.len; return s; }

/** Runs a key-detection performance in 100 ms blocks; the trace holds detectedKey after every block. */
std::vector<int> keyTrace (TuneEngine& e, std::vector<float> x, double sr)
{
    std::vector<int> trace;
    const size_t blk = (size_t) (0.1 * sr);
    for (size_t pos = 0; pos < x.size(); pos += blk)
    {
        const int n = (int) std::min (blk, x.size() - pos);
        float* chans[2] = { x.data() + pos, nullptr };
        e.process (chans, 1, n);
        trace.push_back (e.detectedKey.load());
    }
    return trace;
}

/** Changes of the detected key after it was first locked, and the first lock time (s, -1 = never). */
std::pair<int, double> keyChangesAfterLock (const std::vector<int>& trace)
{
    int changes = 0, last = -1;
    double first = -1.0;
    for (size_t i = 0; i < trace.size(); ++i)
    {
        if (trace[i] < 0 && last < 0) continue;
        if (last < 0) { first = 0.1 * (double) (i + 1); last = trace[i]; continue; }
        if (trace[i] != last) { ++changes; last = trace[i]; }
    }
    return { changes, first };
}

/** 48 s performance in C major that leans elsewhere on purpose: a G major stretch with many F#,
    wrong notes and chromatic passing notes, an A harmonic minor stretch (G#), a fast melisma and
    wide vibrato. A singer's notes, not the beat: the key must stay C once it is locked. */
std::vector<Note> wanderingPerformance()
{
    std::vector<Note> v;
    auto add = [&v] (std::initializer_list<double> ms, double len, double vib = 0.0, double det = 0.12)
    {
        int k = 0;
        for (double m : ms) v.push_back ({ m, len, det * std::sin (1.7 * (double) (k++) + m), vib });
    };
    for (int r = 0; r < 2; ++r)                                                       // 0 - 12 s: C major
    {
        add ({ 60, 62, 64, 65, 67, 65, 64, 62 }, 0.4);
        add ({ 60, 64, 67, 72, 71, 69, 67, 65 }, 0.35);
        add ({ 64, 62, 60 }, 0.6, 35.0);
    }
    for (int r = 0; r < 2; ++r)                                                       // G major stretch, many F#
        add ({ 67, 69, 71, 72, 74, 76, 78, 79, 78, 76, 74, 71, 67, 66, 67 }, 0.27);
    for (int r = 0; r < 2; ++r)                                                       // C again, with wrong / passing notes
    {
        add ({ 60, 62 }, 0.4); add ({ 63 }, 0.12); add ({ 64, 65, 67 }, 0.4); add ({ 68 }, 0.12);
        add ({ 69, 67, 65 }, 0.4); add ({ 61 }, 0.15); add ({ 62, 60 }, 0.5, 40.0);
    }
    for (int r = 0; r < 2; ++r)                                                       // A harmonic minor (G#)
        add ({ 69, 71, 72, 74, 76, 77, 80, 81, 80, 76, 72, 71, 69 }, 0.3);
    for (int r = 0; r < 3; ++r)                                                       // fast melisma
        add ({ 60, 62, 64, 65, 67, 69, 71, 72, 71, 69, 67, 65, 64, 62 }, 0.085);
    add ({ 67 }, 2.0, 60.0);                                                          // long note, wide vibrato
    add ({ 64, 62, 60 }, 0.8, 30.0);
    return v;
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

    // 8) automatic key: a C major melody sung slightly out of tune (4 times: the one-shot lock waits
    //    for several phrases, see KeyDetector)
    {
        const double notes[] = { 60, 62, 64, 65, 67, 69, 71, 72, 67, 64, 60, 62, 64, 65, 67, 72 };
        auto f0 = [&] (double t)
        {
            const int i = std::min (63, (int) (t / 0.35));
            return midiToHz (notes[i % 16] + 0.18 * std::sin (i * 1.7));
        };
        auto x = makeVoice (sr, 22.4, f0);
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
            const int i = std::min (79, (int) (t / 0.3));
            return midiToHz (notes[i % 20] - 12.0 + 0.15 * std::sin (i * 1.3));
        };
        auto x = makeVoice (sr, 24.0, f0);
        TuneEngine e;
        e.prepare (sr, true);
        auto p = baseParams();
        p.key = -1;
        p.scale = kMinor;
        e.setParams (p);
        bool neverWrong = true;
        for (size_t pos = 0; pos < x.size(); pos += 512)
        {
            const int n = (int) std::min ((size_t) 512, x.size() - pos);
            float* chans[2] = { x.data() + pos, nullptr };
            e.process (chans, 1, n);
            neverWrong = neverWrong && (e.detectedKey.load() == -1 || e.detectedKey.load() == 2);
        }
        // E is 45 % of the singing: the minor profile leans to A minor and only one Bb per phrase says D,
        // so the one-shot lock (final quality pass) waits; before, it locked D at 5 s. The guess is D and
        // "Lock now" takes it.
        const int cand = e.keyCandidate.load();
        e.requestKeyLock (cand, e.keyConfidence.load());
        std::vector<float> z ((size_t) 256, 0.0f);
        float* zc[2] = { z.data(), nullptr };
        e.process (zc, 1, 256);
        check (neverWrong && cand == 2 && e.detectedKey.load() == 2,
               "Auto key, D minor sung mostly on E: candidate D (never A), not locked by itself on borderline evidence; Lock now -> D (got "
                   + std::to_string (e.detectedKey.load()) + ")");
    }

    // 8e) Auto key confidence (VSTM_TUNE_AUDIT.md R7). Before: one sung note or two alternating notes
    //     gave a "certain" key after 2 s, an A minor melody with Scale = Major was first called G,
    //     and nothing told the user how sure the guess was.
    {
        auto keyRun = [&] (const std::vector<double>& notes, double noteLen, int scale, std::vector<int>* trace = nullptr, bool follow = false)
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
            p.followKeyChanges = follow;
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

        auto times = [] (const std::vector<double>& v, int n) { std::vector<double> r; for (int i = 0; i < n; ++i) r.insert (r.end(), v.begin(), v.end()); return r; };
        const std::vector<double> aMin = times ({ 57, 59, 60, 62, 64, 65, 67, 69, 64, 60, 62, 59, 57, 60, 64, 57 }, 3);
        tr.clear();
        keyRun (aMin, 0.5, kMajor, &tr);
        check (only (tr, 0) && tr.back() == 0, "Auto key: A minor melody with Scale = Major: only C (relative major) is ever shown, from "
                                                    + std::to_string (firstAt (tr, 0)) + " s");

        const std::vector<double> cMaj = times ({ 60, 62, 64, 65, 67, 69, 71, 72, 67, 64, 65, 62, 60, 64, 67, 60 }, 4);
        auto e2 = keyRun (cMaj, 0.35, kMajor);
        check (e2->detectedKey.load() == 0 && e2->keyConfidence.load() >= 0.6f,
               "Auto key: C major melody: C with confidence " + std::to_string (e2->keyConfidence.load()));

        // a stretch in G major (F# four times in 8 s) after 24 s of C major. The lock stays C (final quality
        // pass: the singing never moves a locked key; before, it was followed after ~5 s). Only the opt-in
        // "follow key changes" mode follows it. Chromatic passing notes (F#, Bb) in C: ignored in both.
        std::vector<double> mod = times ({ 60, 62, 64, 65, 67, 69, 71, 72, 67, 64, 65, 62, 60, 64, 67, 60 }, 3);
        for (double n : { 67, 69, 71, 72, 74, 76, 78, 79, 78, 74, 71, 66, 67, 69, 71, 78 }) mod.push_back (n);
        tr.clear();
        keyRun (mod, 0.5, kMajor, &tr);
        const double cAt = firstAt (tr, 0);
        check (only (tr, 0) && tr.back() == 0 && cAt > 0.0 && cAt < 24.0, "Auto key: C major (locked at " + std::to_string (cAt)
                                                                              + " s), then 8 s in G major: the lock stays C (follow key changes off)");
        tr.clear();
        keyRun (mod, 0.5, kMajor, &tr, true);
        const double gAt = firstAt (tr, 7);
        check (tr.back() == 7 && gAt > 24.0 && gAt < 31.0, "Auto key, follow key changes ON: C major -> G major at 24 s is followed at " + std::to_string (gAt) + " s");

        // 16 s of C major (the key locks), then the 1.0 test's melody with four passing notes (F#, Bb) twice.
        // (With the passing notes from the very start the pitch counts tie C, F and G major: 2 F, 2 F#,
        // 1 B, 2 Bb in 32 notes. Then nothing locks, which is the safe answer, not a wrong key.)
        std::vector<double> passing = times ({ 60, 62, 64, 65, 67, 69, 71, 72, 67, 64, 65, 62, 60, 64, 67, 60 }, 4);
        passing[42] = 66; passing[48] = 70; passing[54] = 66; passing[60] = 70;
        tr.clear();
        keyRun (passing, 0.5, kMajor, &tr);
        check (only (tr, 0) && tr.back() == 0, "Auto key: chromatic passing notes (F#, Bb) in a C major melody do not move the key");
    }

    // 8h) Auto key = one analysis, then a lock (final quality pass). The singer's notes estimate the
    //     key; once it is locked, wrong notes, passing notes, a stretch in another key, melisma and
    //     vibrato never move it (before: "modulation" / "drift" votes switched it after ~1 s).
    //     Without notes that tell keys apart (pentatonic: C D E G A fit C, F and G major alike) it
    //     does not lock at all: the guess stays a candidate and notes go to the nearest semitone.
    {
        const auto perf = wanderingPerformance();
        auto x = makeVoice (sr, totalLen (perf), melodyF0 (perf));
        TuneEngine e;
        e.prepare (sr, true);
        auto p = baseParams();
        p.key = -1;
        p.scale = kMajor;
        e.setParams (p);
        const auto tr = keyTrace (e, x, sr);
        const auto [changes, firstAt] = keyChangesAfterLock (tr);
        check (tr.back() == 0 && changes == 0 && firstAt > 0.0 && firstAt < 8.0,
               "Auto key: 48 s of singing (G major stretch, wrong / passing notes, G#, melisma, vibrato): locked C at "
                   + std::to_string (firstAt) + " s, " + std::to_string (changes) + " key changes after the lock");

        std::vector<Note> penta;
        for (int r = 0; r < 4; ++r)
            for (double m : { 60.0, 62.0, 64.0, 67.0, 69.0, 67.0, 64.0, 62.0, 60.0, 64.0, 67.0, 72.0 })
                penta.push_back ({ m, 0.3, 0.1 * std::sin (m + r) });
        auto y = makeVoice (sr, totalLen (penta), melodyF0 (penta));
        TuneEngine q;
        q.prepare (sr, true);
        q.setParams (p);
        const auto tp = keyTrace (q, y, sr);
        const auto [pc, pFirst] = keyChangesAfterLock (tp);
        check (pFirst < 0.0 && q.keyCandidate.load() >= 0 && q.keyConfidence.load() < 0.5f,
               "Auto key: pentatonic melody (C D E G A only, 14 s) is not locked: candidate " + std::to_string (q.keyCandidate.load())
                   + ", confidence " + std::to_string (q.keyConfidence.load()) + (pFirst < 0.0 ? "" : ", locked at " + std::to_string (pFirst) + " s"));
        (void) pc;

        // the same 48 s with the opt-in "follow key changes": the key does move (the mode is real)
        TuneEngine f;
        f.prepare (sr, true);
        auto pf = p;
        pf.followKeyChanges = true;
        f.setParams (pf);
        const auto [fChanges, fFirst] = keyChangesAfterLock (keyTrace (f, x, sr));
        check (fChanges >= 1, "Auto key, follow key changes ON: the same 48 s move the key " + std::to_string (fChanges)
                                  + " time(s) (lock at " + std::to_string (fFirst) + " s)");
    }

    // 8i) Auto key requests from the user and the host: Re-analyze, Lock now, a restored lock, host
    //     re-prepare (prepareToPlay), scale changes and the chromatic scale.
    {
        auto sing = [&] (TuneEngine& e, std::initializer_list<double> notes, double noteLen, int repeats)
        {
            std::vector<Note> v;
            for (int r = 0; r < repeats; ++r)
                for (double m : notes) v.push_back ({ m, noteLen, 0.1 * std::sin (m * 0.7 + r) });
            keyTrace (e, makeVoice (sr, totalLen (v), melodyF0 (v)), sr);
        };
        auto tick = [&] (TuneEngine& e) { std::vector<float> z ((size_t) 256, 0.0f); float* c[2] = { z.data(), nullptr }; e.process (c, 1, 256); };
        auto params = [] (int scale) { auto p = baseParams(); p.key = -1; p.scale = scale; return p; };

        TuneEngine e;
        e.prepare (sr, true);
        e.setParams (params (kMajor));
        sing (e, { 60, 62, 64, 65, 67, 69, 71, 72, 67, 64 }, 0.35, 6);                        // C major, 21 s
        const bool lockedC = e.detectedKey.load() == 0 && e.keyState.load() == KeyDetector::kLocked;
        e.requestKeyReanalyze();
        tick (e);
        const bool listening = e.detectedKey.load() == -1 && e.keyCandidate.load() == -1 && e.keyState.load() == KeyDetector::kListening;
        sing (e, { 67, 69, 71, 72, 74, 76, 78, 79, 74, 71 }, 0.35, 6);                        // G major only now
        const int afterReanalyze = e.detectedKey.load();
        check (lockedC && listening && afterReanalyze == 7,
               "Re-analyze: C forgotten (listening, no candidate), then G major singing locks G (got " + std::to_string (afterReanalyze) + ")");

        e.requestKeyLock (9, 0.42f);
        const int pending = e.getKeyLockForState();
        tick (e);
        const bool userLock = e.detectedKey.load() == 9 && std::abs (e.keyConfidence.load() - 0.42f) < 1e-6f;
        sing (e, { 67, 69, 71, 72, 74, 76, 78, 79, 74, 71 }, 0.35, 6);                        // G major singing again
        const bool held = e.detectedKey.load() == 9;
        const int suggestion = e.keySuggestion.load();
        e.prepare (sr, false);                                                               // host re-prepares (HQ)
        tick (e);
        check (pending == 9 && userLock && held && suggestion == 7 && e.detectedKey.load() == 9,
               "Lock now (A, 42 %): saved while pending, applied, kept through 21 s of G major singing (suggests "
                   + std::to_string (suggestion) + "), kept through prepare()");

        // a scale TYPE change moves the lock only when the evidence clearly names another tonic
        TuneEngine m;
        m.prepare (sr, true);
        m.setParams (params (kMinor));
        sing (m, { 57, 59, 60, 62, 64, 65, 67, 69, 64, 60 }, 0.35, 6);                        // A natural minor
        const int minorKey = m.detectedKey.load();
        m.setParams (params (kMajor));
        tick (m);
        const int majorKey = m.detectedKey.load();
        TuneEngine r;                                                                        // restored lock, no evidence yet
        r.prepare (sr, true);
        r.setParams (params (kMinor));
        r.requestKeyLock (9, 0.8f);
        tick (r);
        r.setParams (params (kMajor));
        tick (r);
        check (minorKey == 9 && majorKey == 0 && r.detectedKey.load() == 9,
               "Scale Minor -> Major: A becomes C with the A minor evidence (" + std::to_string (minorKey) + " -> " + std::to_string (majorKey)
                   + "); a restored lock without evidence keeps A (" + std::to_string (r.detectedKey.load()) + ")");

        TuneEngine c;
        c.prepare (sr, true);
        c.setParams (params (kChromatic));
        sing (c, { 60, 62, 64, 65, 67, 69, 71, 72 }, 0.35, 2);
        check (c.keyState.load() == KeyDetector::kNotNeeded && c.detectedKey.load() == -1,
               "Chromatic scale: the key is not needed, nothing is locked or shown");
    }

    // 8g) custom scale: the note set counts from the key like a scale; Auto finds the key with it.
    //     A minor pentatonic as a custom set (1 b3 4 5 b7), a melody in A minor pentatonic, key Auto.
    {
        const double notes[] = { 57, 60, 62, 64, 67, 69, 67, 64, 62, 60, 57, 60, 64, 62, 57, 69, 67, 64, 60, 57 };
        auto x = makeVoice (sr, 24.0, [&] (double t) { return midiToHz (notes[std::min (79, (int) (t / 0.3)) % 20] + 0.2); });
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
        const double hz = measureHz (y, sr, (int) (sr * 23.47) + L, (int) (sr * 0.15), midiToHz (60.0));  // last pass, note 18: C4 sung 20 ct sharp
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

        // the automatic key survives the switch (C major melody sung five times, switched at 24 s, after the
        // key is locked; the one-shot lock of the final quality pass waits for several phrases, this test
        // used to switch at 3.0 s)
        const double notes[] = { 60, 62, 64, 65, 67, 69, 71, 72, 67, 64, 60, 62, 64, 65, 67, 72 };
        auto mel = makeVoice (sr, 28.0, [&] (double t)
        {
            const int i = std::min (79, (int) (t / 0.35));
            return midiToHz (notes[i % 16] + 0.18 * std::sin (i * 1.7));
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
            if (pos == (size_t) (24.0 * sr) / 512 * 512) { before = k.detectedKey.load(); k.setRealtime (false); }
            const int n = (int) std::min ((size_t) 512, mel.size() - pos);
            float* chans[2] = { mel.data() + pos, nullptr };
            k.process (chans, 1, n);
            if (before >= 0 && k.detectedKey.load() != before) lost = true;
        }
        check (before == 0 && ! lost, "REALTIME switch keeps the automatic key (C before, "
                                          + std::to_string (k.detectedKey.load()) + " after)");
    }

    // 13) note mapping, exhaustively (1.2): every scale and key, the sung pitch from C1 to C8 in 1-cent
    //     steps, and every non-empty custom note set (4095) for every key in 5-cent steps over two octaves:
    //     the target is an allowed note and no allowed note is closer (ties: the lower one).
    {
        long checked = 0, bad = 0;
        std::string firstBad;
        auto verify = [&] (float m, int key, uint16_t mask)
        {
            const int n = nearestAllowedNote (m, key, mask);
            ++checked;
            const uint16_t eff = mask == 0 ? (uint16_t) 0x0fff : mask;
            bool ok = isAllowed (n, key, eff);
            for (int k = (int) std::floor (m) - 12; ok && k <= (int) std::ceil (m) + 12; ++k)
                if (isAllowed (k, key, eff))
                {
                    const float dk = std::abs (m - (float) k), dn = std::abs (m - (float) n);
                    if (dk < dn - 1.0e-4f || (std::abs (dk - dn) <= 1.0e-4f && k < n)) ok = false;
                }
            if (! ok && bad++ == 0)
                firstBad = "m " + std::to_string (m) + " key " + std::to_string (key) + " mask " + std::to_string (mask) + " -> " + std::to_string (n);
        };
        for (int sc = 0; sc < (int) kNumScales; ++sc)
            for (int key = 0; key < 12; ++key)
                for (int c = 2400; c <= 10800; ++c)
                    verify ((float) c * 0.01f, key, scaleMask (sc));
        for (int mask = 1; mask < 4096; ++mask)
            for (int key = 0; key < 12; ++key)
                for (int c = 0; c <= 480; ++c)
                    verify (48.0f + (float) c * 0.05f, key, (uint16_t) mask);
        for (int c = 0; c <= 2400; ++c)
            verify (48.0f + (float) c * 0.01f, 0, 0);                    // empty set = every note
        check (bad == 0, "note mapping: " + std::to_string (checked) + " pitches x scales x keys x custom sets, wrong "
                             + std::to_string (bad) + (bad ? " (first: " + firstBad + ")" : std::string()));
    }

    // 14) no click when correction, robotism, retune, humanize, vibrato scale, formant, transpose or MIX are
    //     moved fast (a new random value every 10 ms, the knob "thrown" by automation) or correction is
    //     switched on / off every 250 ms on a held note: the largest sample step stays within 2x the
    //     largest step without automation (the voice itself), finite output (1.2, P0-E)
    {
        const double f = midiToHz (57.30);
        auto x = makeVoice (sr, 3.0, [&] (double t) { return f * std::pow (2.0, 0.25 / 12.0 * std::sin (2.0 * kPi * 5.5 * t)); });
        auto render = [&] (int which, bool toggle, float fixedFormant = 0.0f, float fixedTranspose = 0.0f)
        {
            TuneEngine e;
            e.prepare (sr, true);
            auto p = baseParams();
            p.formant = fixedFormant;
            p.transpose = fixedTranspose;
            e.setParams (p);
            std::mt19937 rng (5 + which);
            std::uniform_real_distribution<float> u (0.0f, 1.0f);
            auto y = x;
            const int block = (int) (0.010 * sr);
            for (size_t pos = 0, k = 0; pos < y.size(); pos += (size_t) block, ++k)
            {
                if (pos > (size_t) (0.5 * sr))
                {
                    const float r = u (rng);
                    switch (which)
                    {
                        case 0: p.amount = toggle ? ((k / 25) % 2 ? 0.0f : 1.0f) : r; break;
                        case 1: p.robotism = r; break;
                        case 2: p.retuneMs = 200.0f * r * r; break;
                        case 3: p.humanize = r; break;
                        case 4: p.vibratoScale = 2.0f * r; break;
                        case 5: p.formant = -12.0f + 24.0f * r; break;
                        case 6: p.transpose = std::round (-12.0f + 24.0f * r); break;
                        case 7: p.mix = r; break;
                        default: break;
                    }
                    e.setParams (p);
                }
                const int m = (int) std::min ((size_t) block, y.size() - pos);
                float* chans[2] = { y.data() + pos, nullptr };
                e.process (chans, 1, m);
            }
            double step = 0.0;
            bool fin = true;
            for (size_t i = (size_t) (0.55 * sr); i < y.size(); ++i)
            {
                fin = fin && std::isfinite (y[i]);
                step = std::max (step, (double) std::abs (y[i] - y[i - 1]));
            }
            return std::make_pair (step, fin);
        };
        const double steady = render (-1, false).first;
        // formant and transpose change the voice itself (grain length / pitch): their reference is the
        // largest step of the same voice held at the extreme settings, without automation
        double steadyFormant = steady, steadyTranspose = steady;
        for (float v : { -12.0f, 12.0f })
        {
            steadyFormant = std::max (steadyFormant, render (-1, false, v, 0.0f).first);
            steadyTranspose = std::max (steadyTranspose, render (-1, false, 0.0f, v).first);
        }
        const char* names[] = { "correction", "robotism", "retune", "humanize", "vibrato", "formant", "transpose", "mix" };
        std::string worst;
        double worstRatio = 0.0;
        bool allFinite = true;
        for (int w = 0; w < 8; ++w)
        {
            const auto r = render (w, false);
            allFinite = allFinite && r.second;
            const double ref = w == 5 ? steadyFormant : (w == 6 ? steadyTranspose : steady);
            if (r.first / ref > worstRatio) { worstRatio = r.first / ref; worst = names[w]; }
        }
        const auto tg = render (0, true);
        check (allFinite && tg.second && worstRatio < 2.0 && tg.first / steady < 2.0,
               "fast automation: largest sample step " + std::to_string (worstRatio) + " x steady (worst: " + worst
                   + "), correction on/off every 250 ms " + std::to_string (tg.first / steady) + " x; references: steady "
                   + std::to_string (steady) + ", formant +-12 " + std::to_string (steadyFormant) + ", transpose +-12 " + std::to_string (steadyTranspose));
    }

    // 15) a key change under a held, hard-tuned note: the output glides to the new target (at least
    //     ~25 ms) instead of jumping, without a click (1.0 / 1.1 jumped at once on the robotic side)
    {
        auto x = makeVoice (sr, 2.0, [] (double) { return midiToHz (57.30); });
        TuneEngine e;
        e.prepare (sr, false);                                  // HQ: the output follows the plan exactly
        auto p = baseParams();
        p.robotism = 0.0f;
        p.scale = kMajor;
        p.key = 0;                                              // C major: A allowed
        e.setParams (p);
        auto y = x;
        for (size_t pos = 0; pos < y.size(); pos += 256)
        {
            if (pos >= (size_t) (1.0 * sr) && p.key == 0) { p.key = 1; e.setParams (p); }   // C# major: A -> A#
            const int m = (int) std::min ((size_t) 256, y.size() - pos);
            float* chans[2] = { y.data() + pos, nullptr };
            e.process (chans, 1, m);
        }
        const int lat = e.getLatencySamples();
        double t10 = -1, t90 = -1;
        for (double t = 0.95; t < 1.3; t += 0.001)
        {
            const double mm = hzToMidi (measureHz (y, sr, (int) (t * sr) + lat, (int) (sr * 0.006), midiToHz (57.5)));
            if (t10 < 0 && mm > 57.1) t10 = t;
            if (t90 < 0 && mm > 57.9) t90 = t;
        }
        double step = 0.0, steady = 0.0;
        for (size_t i = (size_t) (0.5 * sr); i < y.size(); ++i)
        {
            const double d = std::abs ((double) y[i] - y[i - 1]);
            if (i > (size_t) (0.98 * sr) + (size_t) lat && i < (size_t) (1.2 * sr) + (size_t) lat) step = std::max (step, d);
            else steady = std::max (steady, d);
        }
        const double rise = (t10 >= 0 && t90 >= 0) ? (t90 - t10) * 1000.0 : -1.0;
        check (rise >= 10.0 && rise < 80.0 && step < 1.5 * steady,
               "key change under a held note: A -> A# in " + std::to_string (rise) + " ms (10-90 %), step "
                   + std::to_string (step / steady) + " x steady");
    }

    // 12b) REALTIME <-> HQ keeps the content continuous (1.2). An irregular click train (6 - 9 ms apart,
    //      aperiodic: unvoiced) gives the delay of the output at every click. Dry (MIX 0) and the
    //      processed path (correction 0): every click comes out once and in order, the delay glides
    //      monotonically and ends exactly on the reported latency. Before: the delay jumped by the full
    //      latency difference (960 samples at 48 kHz): 20 ms of input repeated (RT -> HQ) or skipped.
    {
        const int n = (int) (7.0 * sr);
        std::vector<float> x ((size_t) n, 0.0f);
        std::mt19937 rng (21);
        std::uniform_int_distribution<int> gap ((int) (0.006 * sr), (int) (0.009 * sr));
        std::vector<long> clicks;
        for (int i = (int) (0.05 * sr), c = 0; i < n; i += gap (rng), ++c)
        {
            x[(size_t) i] = (c % 2) ? -0.5f : 0.5f;
            clicks.push_back (i);
        }
        const int latRT = TuneEngine::latencyFor (sr, true), latHQ = TuneEngine::latencyFor (sr, false);
        for (int path = 0; path < 2; ++path)
        {
            TuneEngine e;
            e.prepare (sr, true);
            auto p = baseParams();
            if (path == 0) p.mix = 0.0f; else p.amount = 0.0f;
            e.setParams (p);
            auto y = x;
            for (size_t pos = 0; pos < y.size(); pos += 256)
            {
                if (pos == (size_t) (1.0 * sr) / 256 * 256) e.setRealtime (false);
                if (pos == (size_t) (4.0 * sr) / 256 * 256) e.setRealtime (true);
                const int m = (int) std::min ((size_t) 256, y.size() - pos);
                float* chans[2] = { y.data() + pos, nullptr };
                e.process (chans, 1, m);
            }
            // output clicks in order (strongest sample of each cluster), then matched to the input clicks in
            // order: an output click with no input click left to match = repeated content, an input click
            // with no output click in the possible delay range = skipped content
            std::vector<long> out;
            for (long k = 0; k < n; ++k)
                if (std::abs (y[(size_t) k]) > 0.15f)
                {
                    long b = k;
                    for (long q = k; q < std::min ((long) n, k + (long) (0.0015 * sr)); ++q)
                        if (std::abs (y[(size_t) q]) > std::abs (y[(size_t) b])) b = q;
                    out.push_back (b);
                    k = b + (long) (0.004 * sr);
                }
            // each click is looked for near where the last delay predicts (+-60 samples); clicks out of
            // place in between = repeated content; none there = skipped (then re-acquired anywhere in range).
            // The processed path may show a click's two grain copies up to 1 ms apart while it glides.
            const double tolBack = path == 0 ? 2.0 : 0.001 * sr;
            int missing = 0, extra = 0, backwards = 0;
            double prev = (double) latRT, maxStepD = 0.0, endDelay = 0.0;
            size_t j = 0;
            bool started = false;
            for (long c : clicks)
            {
                if (c + latHQ + 8 >= n) break;
                const double expect = (double) c + prev;
                size_t k = j, hit = out.size();
                while (k < out.size() && (double) out[k] < expect - 60.0) ++k;
                if (k < out.size() && (double) out[k] <= expect + 60.0) hit = k;
                if (hit == out.size())
                {
                    ++missing;
                    k = j;                                   // re-acquire anywhere in the possible range
                    while (k < out.size() && out[k] - c < latRT - 4) ++k;
                    if (k >= out.size() || out[k] - c > latHQ + 4) continue;
                    hit = k;
                }
                if (started) extra += (int) (hit - j);
                started = true;
                const double d = (double) (out[hit] - c);
                const double t = (double) c / sr;
                if ((t > 1.0 && t < 4.0 && d < prev - tolBack) || (t > 4.0 && d > prev + tolBack)) ++backwards;
                maxStepD = std::max (maxStepD, std::abs (d - prev));
                prev = d;
                endDelay = d;
                j = hit + 1;
            }
            check (missing == 0 && extra == 0 && backwards == 0 && maxStepD < 60.0 && std::abs (endDelay - latRT) <= 1.0,
                   std::string ("REALTIME switch keeps the content continuous (") + (path == 0 ? "dry" : "processed")
                       + "): skipped clicks " + std::to_string (missing) + ", repeated " + std::to_string (extra)
                       + ", backwards " + std::to_string (backwards)
                       + ", largest delay step between clicks " + std::to_string (maxStepD) + " samples (a jump: "
                       + std::to_string (latHQ - latRT) + "), end delay " + std::to_string (endDelay) + " / " + std::to_string (latRT));
        }
    }

    //--------------------------------------------------------------------------
    std::printf ("\n=== 1.3: output peak guard, backdated hard-tune switch (48 kHz) ===\n");
    {
        const double sr = 48000.0;
        auto render = [sr] (const std::vector<float>& in, bool realtime, const std::function<void (TuneEngine::Params&)>& set,
                            float* guardMin = nullptr)
        {
            TuneEngine e;
            e.prepare (sr, realtime);
            TuneEngine::Params p;
            p.key = 0; p.scale = 0;
            set (p);
            e.setParams (p);
            std::vector<float> buf = in;
            for (size_t pos = 0; pos < buf.size(); pos += 256)
            {
                float* ch[1] = { buf.data() + pos };
                e.process (ch, 1, (int) std::min<size_t> (256, buf.size() - pos));
            }
            if (guardMin) *guardMin = e.guardMin;
            return buf;
        };
        auto maxAbs = [] (const std::vector<float>& x) { float m = 0.0f; for (float v : x) m = std::max (m, std::abs (v)); return m; };

        // a voice at a normal level: the guard never acts (bit-identical to no guard)
        const auto normal = makeVoice (sr, 2.0, [] (double) { return 196.0; }, 0.25f);
        float gm = 0.0f;
        const auto a = render (normal, true, [] (TuneEngine::Params&) {}, &gm);
        const auto b = render (normal, true, [] (TuneEngine::Params& p) { p.peakGuard = false; });
        check (a == b && gm == 1.0f, "peak guard: a voice peaking at -12 dBFS (default settings) is untouched, bit for bit");

        // a hot voice, +12 st and formant +12: without the guard it leaves above the ceiling, with it never
        const auto hot = makeVoice (sr, 2.0, [] (double t) { return 150.0 * std::pow (2.0, 0.3 * std::sin (2.0 * kPi * 5.5 * t) / 12.0); }, 0.95f);
        const auto hotOff = render (hot, true, [] (TuneEngine::Params& p) { p.transpose = 12.0f; p.formant = 12.0f; p.peakGuard = false; });
        const auto hotOn = render (hot, true, [] (TuneEngine::Params& p) { p.transpose = 12.0f; p.formant = 12.0f; }, &gm);
        bool finite = true;
        for (float v : hotOn) finite = finite && std::isfinite (v);
        check (maxAbs (hotOff) > 0.8913f && maxAbs (hotOn) <= 0.8913f + 1.0e-6f && finite,
               "peak guard: hot voice, transpose +12 / formant +12: peak " + std::to_string (20.0 * std::log10 (maxAbs (hotOff)))
                   + " dBFS without the guard, " + std::to_string (20.0 * std::log10 (maxAbs (hotOn))) + " dBFS with it (ceiling -1)");

        // bypass is the pure dry (delayed), even when hot: once the 20 ms bypass ramp (which starts from
        // the engine's default "active") has died away (0.5 s), the output is the input bit for bit
        const auto byp = render (hot, true, [] (TuneEngine::Params& p) { p.active = false; p.transpose = 12.0f; });
        const auto bypNoGuard = render (hot, true, [] (TuneEngine::Params& p) { p.active = false; p.transpose = 12.0f; p.peakGuard = false; });
        const int lat = TuneEngine::latencyFor (sr, true);
        double dDry = 0.0, dGuard = 0.0;
        for (size_t i = (size_t) lat + 24000; i < byp.size(); ++i)
        {
            dDry = std::max (dDry, (double) std::abs (byp[i] - hot[i - (size_t) lat]));
            dGuard = std::max (dGuard, (double) std::abs (byp[i] - bypNoGuard[i]));
        }
        check (dDry < 1.0e-6 && dGuard < 1.0e-6,
               "peak guard: bypass with a hot input stays the delayed dry signal (max difference " + std::to_string (dDry)
                   + "; guard on vs off " + std::to_string (dGuard) + ")");

        // a sung semitone step (30 ms glide at 1.0 s), hard tune: where does the output change note?
        const double f1 = 220.0, f2 = 220.0 * std::pow (2.0, 1.0 / 12.0);
        const auto step = makeVoice (sr, 2.0, [f1, f2] (double t)
        {
            const double x = std::clamp ((t - 1.0) / 0.030, 0.0, 1.0);
            return f1 * std::pow (f2 / f1, 0.5 - 0.5 * std::cos (kPi * x));
        }, 0.25f);
        auto switchTime = [sr, f1, f2] (const std::vector<float>& x, int offset)
        {
            const double mid = std::sqrt (f1 * f2);
            const int len = (int) (2.0 * sr / f1);
            for (int c = (int) (0.95 * sr); c < (int) (1.10 * sr); c += 24)
            {
                const int start = c + offset - len / 2;
                if (start < 0 || start + len + (int) (sr / f1 * 1.3) >= (int) x.size()) continue;
                if (measureHz (x, sr, start, len, mid) > mid)
                    return (c - 1.0 * sr - 0.015 * sr) / sr * 1000.0;      // ms after the input's midpoint (glide centre)
            }
            return 1.0e9;
        };
        auto hard = [] (TuneEngine::Params& p) { p.robotism = 0.0f; p.retuneMs = 0.0f; p.humanize = 0.0f; };
        const auto rtOn = render (step, true, hard);
        const auto rtOff = render (step, true, [&] (TuneEngine::Params& p) { hard (p); p.backdateSwitch = false; });
        const auto hqOn = render (step, false, hard);
        const auto hqOff = render (step, false, [&] (TuneEngine::Params& p) { hard (p); p.backdateSwitch = false; });
        const int latHQ = TuneEngine::latencyFor (sr, false);
        const double tIn = switchTime (step, 0), tOn = switchTime (hqOn, latHQ), tOff = switchTime (hqOff, latHQ);
        check (rtOn == rtOff, "backdated switch: REALTIME output unchanged (bit for bit; no unplayed hops there)");
        check (tOn < tOff - 2.0 && tOn - tIn < 8.0,
               "backdated switch, HQ: output changes note " + std::to_string (tOn - tIn) + " ms after the input does (without backdating "
                   + std::to_string (tOff - tIn) + " ms)");
    }

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
