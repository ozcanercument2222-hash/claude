//==============================================================================
//  VSTM Tune - denetim olcumleri (VSTM_TUNE_AUDIT.md, bolum 6), JUCE gerektirmez
//
//    VSTMTune_AuditProbe <cikis_klasoru> [S1 ... S17]
//    g++ -std=c++17 -O2 -I Source tests/audit_probe.cpp -o audit_probe
//
//  Sentetik vokal senaryolari: buyuk sicramalar (S1), NaN/Inf giris (S2), REALTIME
//  gecisi (S3), ornekleme hizlari (S4), blok boyutlari (S5), stereo (S6), otomasyon
//  stresi (S7), CPU (S8), 10 dk (S9), unvoiced (S10), Auto key (S11), formant /
//  transpoze (S12), MIX (S13), ROBOTISM taramasi (S14), NaN parametre (S15), ROBOTISM'in
//  dort ses tipinde (bas, tenor, alto, soprano) taramasi (S16), 30 dakika + bellek, otomasyon,
//  ton istekleri ve REALTIME gecisleriyle (S17; VSTM_AUDIT_S17_MINUTES ile kisaltilabilir).
//  ROBOTISM noktalari (final quality pass): 0 / 10 / 25 / 35 / 50 / 65 / 80 / 90 / 100.
//  Her senaryo <cikis_klasoru>/measurements.jsonl dosyasina bir JSON satiri ekler,
//  bazilari WAV / CSV yazar. Gecti / kaldi karari vermez: olcum aracidir; karar
//  veren kontroller tests/engine_test.cpp icindedir. Kanit etiketi: MEASURED.
//==============================================================================
#include "dsp/TuneEngine.h"

#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <numeric>
#include <random>
#include <string>
#include <vector>
#if defined (__linux__)
 #include <unistd.h>
#endif

using namespace vstm;

namespace
{
std::string OUT = ".";
FILE* J = nullptr;

std::string fmt (const char* f, ...)
{
    char b[8192];
    va_list a;
    va_start (a, f);
    std::vsnprintf (b, sizeof b, f, a);
    va_end (a);
    return b;
}

void jline (const std::string& s)
{
    std::fprintf (J, "%s\n", s.c_str());
    std::fflush (J);
    std::printf ("%s\n", s.c_str());
    std::fflush (stdout);
}

//------------------------------------------------------------------ wav
void writeWav (const std::string& path, const std::vector<const std::vector<float>*>& ch, double sr)
{
    FILE* f = std::fopen (path.c_str(), "wb");
    if (! f) { std::perror (path.c_str()); return; }
    const uint32_t frames = (uint32_t) ch[0]->size();
    const uint16_t nc = (uint16_t) ch.size();
    const uint32_t dataBytes = frames * nc * 4;
    auto w32 = [&] (uint32_t v) { std::fwrite (&v, 4, 1, f); };
    auto w16 = [&] (uint16_t v) { std::fwrite (&v, 2, 1, f); };
    std::fwrite ("RIFF", 1, 4, f); w32 (36 + dataBytes); std::fwrite ("WAVE", 1, 4, f);
    std::fwrite ("fmt ", 1, 4, f); w32 (16); w16 (3); w16 (nc); w32 ((uint32_t) sr); w32 ((uint32_t) sr * nc * 4);
    w16 ((uint16_t) (nc * 4)); w16 (32);
    std::fwrite ("data", 1, 4, f); w32 (dataBytes);
    for (uint32_t i = 0; i < frames; ++i)
        for (auto* c : ch) { const float v = (*c)[i]; std::fwrite (&v, 4, 1, f); }
    std::fclose (f);
}

void writeMono (const std::string& name, const std::vector<float>& x, double sr) { writeWav (OUT + "/" + name, { &x }, sr); }

//------------------------------------------------------------------ synthetic voice
struct Res
{
    double b0 = 0, a1 = 0, a2 = 0, y1 = 0, y2 = 0;
    void set (double sr, double f, double bw)
    {
        const double r = std::exp (-kPi * bw / sr);
        a1 = -2.0 * r * std::cos (2.0 * kPi * f / sr);
        a2 = r * r;
        b0 = (1.0 - r) * 2.0;
    }
    double run (double x) { const double y = b0 * x - a1 * y1 - a2 * y2; y2 = y1; y1 = y; return y; }
};

struct VoiceSpec
{
    double F1 = 730, F2 = 1090, F3 = 2440, B1 = 90, B2 = 110, B3 = 170, g2 = 0.7, g3 = 0.35;
    double tilt = 2.0;       // harmonic amplitude ~ 1 / h^tilt (before the +6 dB/oct differentiation)
    double fund = 1.0;       // extra gain of the fundamental (engine_test: 1.0 vs 1.6 for the others)
    double noise = 0.0005;
    double level = 0.3;
    unsigned seed = 7;
};

VoiceSpec vowelA() { return {}; }
VoiceSpec vowelO() { VoiceSpec v; v.F1 = 570; v.F2 = 840; v.F3 = 2410; v.B1 = 80; v.B2 = 100; v.B3 = 160; return v; }
VoiceSpec vowelI() { VoiceSpec v; v.F1 = 300; v.F2 = 2300; v.F3 = 3000; v.B1 = 70; v.B2 = 120; v.B3 = 180; v.g2 = 0.5; v.g3 = 0.4; return v; }

std::vector<float> synth (double sr, double secs, const std::function<double (double)>& f0At, const VoiceSpec& v = {},
                          const std::function<double (double)>& ampAt = nullptr)
{
    const int n = (int) (sr * secs);
    std::vector<float> out ((size_t) n);
    Res r1, r2, r3;
    r1.set (sr, v.F1, v.B1); r2.set (sr, v.F2, v.B2); r3.set (sr, v.F3, v.B3);
    std::mt19937 rng (v.seed);
    std::normal_distribution<double> nd (0.0, 1.0);
    double ph = 0.0, prev = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double t = i / sr;
        const double f0 = f0At (t);
        ph += f0 / sr;
        if (ph >= 1.0) ph -= std::floor (ph);
        double s = 0.0;
        const int H = std::min (40, (int) (0.45 * sr / std::max (40.0, f0)));
        for (int h = 1; h <= H; ++h)
            s += std::sin (2.0 * kPi * h * ph) / std::pow ((double) h, v.tilt) * (h == 1 ? v.fund : 1.6);
        const double p = s - prev;
        prev = s;
        double y = r1.run (p) + v.g2 * r2.run (p) + v.g3 * r3.run (p);
        y += v.noise * nd (rng);
        if (ampAt) y *= ampAt (t);
        out[(size_t) i] = (float) y;
    }
    float pk = 1.0e-9f;
    for (float x : out) pk = std::max (pk, std::abs (x));
    for (auto& x : out) x *= (float) (v.level / pk);
    return out;
}

/** Note list with a linear slide at the start of each note (MIDI, semitone offset, optional vibrato). */
std::function<double (double)> melody (std::vector<double> midi, double noteLen, double slide, double offset = 0.0,
                                      double vibDepth = 0.0, double vibRate = 5.5)
{
    return [=] (double t)
    {
        int i = (int) std::floor (t / noteLen);
        i = std::clamp (i, 0, (int) midi.size() - 1);
        double m = midi[(size_t) i];
        const double tin = t - i * noteLen;
        if (i > 0 && tin < slide)
            m = midi[(size_t) i - 1] + (midi[(size_t) i] - midi[(size_t) i - 1]) * (tin / slide);
        m += offset + vibDepth * std::sin (2.0 * kPi * vibRate * t);
        return midiToHz (m);
    };
}

//------------------------------------------------------------------ engine runner
struct Run
{
    std::vector<float> y;
    std::vector<TuneEngine::DisplayPoint> d;
    int lat = 0;
    double hopS = 0.0;
};

using BlockHook = std::function<void (TuneEngine&, size_t, TuneEngine::Params&)>;

Run runMono (const std::vector<float>& x, double sr, const TuneEngine::Params& p, bool rt = true, int block = 256,
             const BlockHook& hook = nullptr)
{
    Run r;
    r.y = x;
    auto e = std::make_unique<TuneEngine>();
    e->prepare (sr, rt);
    auto pp = p;
    e->setParams (pp);
    for (size_t pos = 0; pos < x.size(); pos += (size_t) block)
    {
        const int n = (int) std::min ((size_t) block, x.size() - pos);
        if (hook) { hook (*e, pos, pp); e->setParams (pp); }
        float* ch[2] = { r.y.data() + pos, nullptr };
        e->process (ch, 1, n);
        TuneEngine::DisplayPoint d;
        while (e->display.pop (d)) r.d.push_back (d);
    }
    r.lat = e->getLatencySamples();
    r.hopS = e->getHopSeconds();
    return r;
}

void writeCsv (const std::string& name, const Run& r)
{
    FILE* f = std::fopen ((OUT + "/" + name).c_str(), "w");
    if (! f) return;
    std::fprintf (f, "time_s,voiced,in_midi,out_midi,note,deviation,note_change,settled\n");
    for (size_t i = 0; i < r.d.size(); ++i)
    {
        const auto& d = r.d[i];
        std::fprintf (f, "%.5f,%d,%.4f,%.4f,%d,%.4f,%d,%d\n", (double) (i + 1) * r.hopS, (d.flags & TuneEngine::kVoiced) ? 1 : 0,
                      d.inMidi, d.outMidi, (int) d.note, d.deviation, (d.flags & TuneEngine::kNoteChange) ? 1 : 0,
                      (d.flags & TuneEngine::kSettled) ? 1 : 0);
    }
    std::fclose (f);
}

//------------------------------------------------------------------ measurement
/** Normalised autocorrelation around an expected frequency (same method as tests/engine_test.cpp).
    Blind to octave errors by design: only used where the expected pitch is known. */
double measureHz (const std::vector<float>& x, double sr, long start, int len, double expectHz)
{
    const double T = sr / expectHz;
    const int lo = std::max (2, (int) (T * 0.80));
    const int hi = (int) (T * 1.25) + 2;
    if (start < 0 || start + len + hi + 2 >= (long) x.size()) return 0.0;
    std::vector<double> r ((size_t) (hi + 2), 0.0);
    for (int lag = lo - 1; lag <= hi + 1; ++lag)
    {
        double num = 0, ea = 0, eb = 0;
        for (int j = 0; j < len; ++j)
        {
            const double a = x[(size_t) (start + j)], b = x[(size_t) (start + j + lag)];
            num += a * b; ea += a * a; eb += b * b;
        }
        r[(size_t) lag] = num / std::sqrt (ea * eb + 1e-20);
    }
    int best = lo;
    for (int lag = lo; lag <= hi; ++lag)
        if (r[(size_t) lag] > r[(size_t) best]) best = lag;
    const double a = r[(size_t) best - 1], b = r[(size_t) best], c = r[(size_t) best + 1];
    const double den = a - 2 * b + c;
    return sr / (best + (std::abs (den) > 1e-12 ? 0.5 * (a - c) / den : 0.0));
}

double cents (double hz, double ref) { return (hz > 0 && ref > 0) ? 1200.0 * std::log2 (hz / ref) : 1e9; }

double rmsDb (const std::vector<float>& x, size_t a, size_t b)
{
    b = std::min (b, x.size());
    if (b <= a) return -200.0;
    double s = 0;
    for (size_t i = a; i < b; ++i) s += (double) x[i] * x[i];
    return 10.0 * std::log10 (s / (double) (b - a) + 1e-30);
}

bool allFinite (const std::vector<float>& y)
{
    for (float v : y) if (! std::isfinite (v)) return false;
    return true;
}

double peakAbs (const std::vector<float>& y, size_t a = 0, size_t b = SIZE_MAX)
{
    b = std::min (b, y.size());
    double p = 0;
    for (size_t i = a; i < b; ++i) p = std::max (p, (double) std::abs (y[i]));
    return p;
}

TuneEngine::Params hardParams()
{
    TuneEngine::Params p;
    p.key = 0; p.scale = kChromatic; p.retuneMs = 0.0f; p.humanize = 0.0f; p.robotism = 0.0f;
    return p;
}

TuneEngine::Params defaultParams()      // plugin defaults (Default Preset), but chromatic / key C
{
    TuneEngine::Params p;
    p.key = 0; p.scale = kChromatic; p.retuneMs = 15.0f; p.humanize = 0.35f; p.robotism = 0.5f;
    return p;
}

//==============================================================================
//  S1  big leaps (octave / fifth / sixth), legato, three voices
//==============================================================================
void leaps()
{
    const double sr = 48000.0;
    struct Case { const char* name; VoiceSpec v; std::vector<double> notes; int range; };
    VoiceSpec bass = vowelO(); bass.fund = 0.25; bass.tilt = 1.6;        // weak fundamental, darker voice
    const Case cases[] = {
        { "tenor",   vowelA(), { 57, 69, 57, 64, 55 }, 1 },
        { "bass",    bass,     { 43, 55, 43, 50, 40 }, 1 },
        { "bass_rB", bass,     { 43, 55, 43, 50, 40 }, 2 },
        { "soprano", vowelI(), { 69, 81, 69, 76, 67 }, 1 },
        { "sopr_rS", vowelI(), { 69, 81, 69, 76, 67 }, 0 },
    };
    const double offset = 0.30;           // every note sung 30 ct sharp
    const double noteLen = 1.0, slide = 0.025;

    for (const auto& c : cases)
    {
        auto x = synth (sr, noteLen * (double) c.notes.size(), melody (c.notes, noteLen, slide, offset), c.v);
        writeMono (std::string ("leap_") + c.name + "_in.wav", x, sr);

        for (int mode = 0; mode < 2; ++mode)
        {
            auto p = mode == 0 ? hardParams() : defaultParams();
            p.range = c.range;
            const auto r = runMono (x, sr, p, true, 256);
            const std::string tag = std::string ("leap_") + c.name + (mode == 0 ? "_hard" : "_default");
            writeMono (tag + "_out.wav", r.y, sr);
            writeCsv (tag + ".csv", r);

            // per note: engine's own input pitch (octave errors) and the output pitch (narrow measure around the target)
            std::string notesJson;
            int octaveErrHops = 0, voicedHops = 0;
            for (size_t k = 0; k < c.notes.size(); ++k)
            {
                const double truth = c.notes[k] + offset;
                // engine input pitch, skipping the first 120 ms after the note start (input timeline ~ hop index * hop
                // minus the detector's centre delay; we only use the steady middle part of the note)
                std::vector<double> ins;
                for (size_t h = 0; h < r.d.size(); ++h)
                {
                    const double t = (double) (h + 1) * r.hopS - 0.024;          // approx. centre of analysis window
                    if (t < k * noteLen + 0.12 || t > (k + 1) * noteLen - 0.05) continue;
                    if (! (r.d[h].flags & TuneEngine::kVoiced)) continue;
                    ++voicedHops;
                    ins.push_back (r.d[h].inMidi);
                    if (std::abs (r.d[h].inMidi - truth) > 6.0) ++octaveErrHops;
                }
                double inMed = 0;
                if (! ins.empty()) { std::nth_element (ins.begin(), ins.begin() + (long) ins.size() / 2, ins.end()); inMed = ins[ins.size() / 2]; }

                // output: settle time after the note start + steady error, target = the note itself
                const double target = midiToHz (c.notes[k]);
                double settleMs = -1.0;
                int inside = 0;
                for (double t = k * noteLen; t < (k + 1) * noteLen - 0.03; t += 0.002)
                {
                    const double hz = measureHz (r.y, sr, (long) (t * sr) + r.lat, (int) (sr * 0.010), target);
                    if (std::abs (cents (hz, target)) < 20.0) { if (++inside >= 25 && settleMs < 0) settleMs = (t - k * noteLen - 0.048) * 1000.0; }
                    else inside = 0;
                }
                const double steady = cents (measureHz (r.y, sr, (long) ((k * noteLen + 0.5) * sr) + r.lat, (int) (sr * 0.2), target), target);
                notesJson += fmt ("%s{\"note\":%.0f,\"engine_in_median\":%.2f,\"engine_in_err_st\":%.2f,\"out_steady_ct\":%.2f,\"settle_ms\":%.1f}",
                                  k ? "," : "", c.notes[k], inMed, inMed - truth, steady, settleMs);
            }
            jline (fmt ("{\"scenario\":\"S1_leaps\",\"voice\":\"%s\",\"range\":%d,\"mode\":\"%s\",\"latency\":%d,\"voiced_hops\":%d,"
                        "\"engine_octave_error_hops\":%d,\"notes\":[%s]}",
                        c.name, c.range, mode == 0 ? "hard" : "default", r.lat, voicedHops, octaveErrHops, notesJson.c_str()));
        }
    }
}

//==============================================================================
//  S2  NaN / Inf in the input
//==============================================================================
void nanInf()
{
    const double sr = 48000.0;
    auto x = synth (sr, 4.0, [] (double) { return midiToHz (57.40); });
    for (const char* kind : { "clean", "nan", "inf" })
    {
        auto xi = x;
        if (std::strcmp (kind, "nan") == 0) xi[(size_t) sr] = std::numeric_limits<float>::quiet_NaN();
        if (std::strcmp (kind, "inf") == 0) xi[(size_t) sr] = std::numeric_limits<float>::infinity();
        const auto r = runMono (xi, sr, hardParams());
        size_t bad = 0, firstBad = SIZE_MAX, lastBad = 0;
        for (size_t i = 0; i < r.y.size(); ++i)
            if (! std::isfinite (r.y[i])) { ++bad; firstBad = std::min (firstBad, i); lastBad = i; }
        // the input ring (65536 samples) has long forgotten the bad sample by 3.0 s: is the tuning back?
        const double c3 = cents (measureHz (r.y, sr, (long) (3.2 * sr) + r.lat, (int) (sr * 0.2), 220.0), 220.0);
        int voicedAfter = 0, hopsAfter = 0;
        for (size_t h = 0; h < r.d.size(); ++h)
            if ((double) (h + 1) * r.hopS > 2.5) { ++hopsAfter; if (r.d[h].flags & TuneEngine::kVoiced) ++voicedAfter; }
        jline (fmt ("{\"scenario\":\"S2_nan_inf\",\"input\":\"%s\",\"nonfinite_out_samples\":%zu,\"first_bad_s\":%.3f,\"last_bad_s\":%.3f,"
                    "\"tuning_at_3.2s_ct\":%.2f,\"expected_ct\":0,\"input_ct\":40,\"voiced_hops_after_2.5s\":%d,\"hops_after_2.5s\":%d}",
                    kind, bad, firstBad == SIZE_MAX ? -1.0 : firstBad / sr, bad ? lastBad / sr : -1.0, c3, voicedAfter, hopsAfter));
    }
}

//==============================================================================
//  S3  REALTIME <-> HQ switch while audio runs (what processBlock does)
//==============================================================================
void realtimeSwitch()
{
    const double sr = 48000.0;
    auto x = synth (sr, 3.0, [] (double) { return midiToHz (57.40); });
    const auto r = runMono (x, sr, hardParams(), true, 256, [&] (TuneEngine& e, size_t pos, TuneEngine::Params&)
    {
        if (pos == (size_t) (sr * 1.0) / 256 * 256) e.setRealtime (false);
        if (pos == (size_t) (sr * 2.0) / 256 * 256) e.setRealtime (true);
    });
    writeMono ("rtswitch_out.wav", r.y, sr);
    // longest run of exact zeros after 0.5 s, and the largest sample step around the switches vs. elsewhere
    size_t run = 0, longest = 0, longestAt = 0;
    for (size_t i = (size_t) (0.5 * sr); i < r.y.size(); ++i)
    {
        if (r.y[i] == 0.0f) { if (++run > longest) { longest = run; longestAt = i - run + 1; } }
        else run = 0;
    }
    auto maxStep = [&] (double a, double b)
    {
        double m = 0;
        for (size_t i = (size_t) (a * sr) + 1; i < (size_t) (b * sr) && i < r.y.size(); ++i) m = std::max (m, (double) std::abs (r.y[i] - r.y[i - 1]));
        return m;
    };
    jline (fmt ("{\"scenario\":\"S3_realtime_switch\",\"longest_zero_run_ms\":%.2f,\"at_s\":%.3f,\"max_step_near_switch1\":%.4f,"
                "\"max_step_near_switch2\":%.4f,\"max_step_steady\":%.4f,\"input_peak\":%.3f}",
                longest * 1000.0 / sr, longestAt / sr, maxStep (0.98, 1.10), maxStep (1.98, 2.10), maxStep (0.6, 0.95), peakAbs (x)));
}

//==============================================================================
//  S4  sample rates
//==============================================================================
void sampleRates()
{
    for (const double sr : { 22050.0, 32000.0, 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 })
    {
        auto x = synth (sr, 2.0, [] (double) { return midiToHz (57.40); });
        const auto r = runMono (x, sr, hardParams(), true, 512);
        const double hz = measureHz (r.y, sr, (long) (1.2 * sr), (int) (sr * 0.2), 220.0);
        auto p0 = hardParams(); p0.amount = 0.0f;
        const auto t = runMono (x, sr, p0, true, 512);
        double err = 0, sig = 0;
        for (size_t i = (size_t) (0.6 * sr); i < (size_t) (1.8 * sr); ++i)
        {
            const double d = t.y[i] - x[i - (size_t) t.lat];
            err += d * d; sig += (double) x[i - (size_t) t.lat] * x[i - (size_t) t.lat];
        }
        int voiced = 0;
        for (const auto& d : r.d) if (d.flags & TuneEngine::kVoiced) ++voiced;
        jline (fmt ("{\"scenario\":\"S4_sample_rate\",\"sr\":%.0f,\"latency_samples\":%d,\"latency_ms\":%.3f,\"hop_ms\":%.3f,"
                    "\"hard_tune_ct\":%.2f,\"transparent_snr_db\":%.1f,\"finite\":%s,\"voiced_fraction\":%.3f}",
                    sr, r.lat, r.lat * 1000.0 / sr, r.hopS * 1000.0, cents (hz, 220.0), 10 * std::log10 (sig / (err + 1e-20)),
                    allFinite (r.y) && allFinite (t.y) ? "true" : "false", r.d.empty() ? 0.0 : voiced / (double) r.d.size()));
    }
}

//==============================================================================
//  S5  block sizes (incl. 1 sample and random sizes) against 512
//==============================================================================
void blockSizes()
{
    const double sr = 48000.0;
    auto x = synth (sr, 3.0, melody ({ 57, 60, 62, 64, 59, 57 }, 0.5, 0.03, 0.3, 0.2));
    auto p = defaultParams(); p.formant = 1.5f; p.scale = kMajor;
    const auto ref = runMono (x, sr, p, true, 512);
    std::string res;
    for (const int b : { 1, 2, 7, 32, 64, 128, 333, 1024, 4096, 8192, -1 })
    {
        std::vector<float> y = x;
        auto e = std::make_unique<TuneEngine>();
        e->prepare (sr, true);
        e->setParams (p);
        std::mt19937 rng (11);
        std::uniform_int_distribution<int> ud (1, 2048);
        for (size_t pos = 0; pos < x.size();)
        {
            const int want = b > 0 ? b : ud (rng);
            const int n = (int) std::min ((size_t) want, x.size() - pos);
            float* ch[2] = { y.data() + pos, nullptr };
            e->process (ch, 1, n);
            TuneEngine::DisplayPoint d;
            while (e->display.pop (d)) {}
            pos += (size_t) n;
        }
        double m = 0;
        for (size_t i = 0; i < y.size(); ++i) m = std::max (m, (double) std::abs (y[i] - ref.y[i]));
        res += fmt ("%s{\"block\":%s,\"max_abs_diff\":%.3g}", res.empty() ? "" : ",", b > 0 ? std::to_string (b).c_str() : "\"random1-2048\"", m);
    }
    jline (fmt ("{\"scenario\":\"S5_block_size\",\"reference_block\":512,\"results\":[%s]}", res.c_str()));
}

//==============================================================================
//  S6  stereo: linked processing (analysis on the mono sum)
//==============================================================================
void stereo()
{
    const double sr = 48000.0;
    auto L = synth (sr, 2.5, [] (double) { return midiToHz (57.40); });                 // A3 + 40 ct
    auto R = synth (sr, 2.5, [] (double) { return midiToHz (64.10); }, vowelO());       // E4 + 10 ct
    std::vector<float> Z (L.size(), 0.0f);
    for (int c = 0; c < 2; ++c)
    {
        std::vector<float> l = L, r = c == 0 ? Z : R;
        auto e = std::make_unique<TuneEngine>();
        e->prepare (sr, true);
        e->setParams (hardParams());
        for (size_t pos = 0; pos < l.size(); pos += 256)
        {
            const int n = (int) std::min ((size_t) 256, l.size() - pos);
            float* ch[2] = { l.data() + pos, r.data() + pos };
            e->process (ch, 2, n);
            TuneEngine::DisplayPoint d;
            while (e->display.pop (d)) {}
        }
        const int lat = e->getLatencySamples();
        const double lc = cents (measureHz (l, sr, (long) (1.5 * sr) + lat, (int) (sr * 0.2), 220.0), 220.0);
        if (c == 0)
            jline (fmt ("{\"scenario\":\"S6_stereo\",\"case\":\"L=voice,R=silence\",\"L_ct\":%.2f,\"R_rms_dbfs\":%.1f}", lc, rmsDb (r, 0, r.size())));
        else
        {
            const double rc = cents (measureHz (r, sr, (long) (1.5 * sr) + lat, (int) (sr * 0.2), midiToHz (64.0)), midiToHz (64.0));
            jline (fmt ("{\"scenario\":\"S6_stereo\",\"case\":\"L=A3+40ct,R=E4+10ct\",\"L_ct_vs_A3\":%.2f,\"R_ct_vs_E4\":%.2f,"
                        "\"note\":\"R alone would be tuned to ~0 ct; linked stereo applies the same grains to both channels\"}", lc, rc));
        }
    }
}

//==============================================================================
//  S7  automation stress: random parameter jumps every block
//==============================================================================
void automation()
{
    const double sr = 48000.0;
    auto x = synth (sr, 6.0, melody ({ 57, 60, 62, 64, 59, 57, 55, 57, 60, 64, 62, 57 }, 0.5, 0.03, 0.25, 0.15));
    const auto base = runMono (x, sr, defaultParams());
    std::mt19937 rng (5);
    std::uniform_real_distribution<float> u (0.0f, 1.0f);
    const auto r = runMono (x, sr, defaultParams(), true, 256, [&] (TuneEngine&, size_t, TuneEngine::Params& p)
    {
        p.transpose = std::round (u (rng) * 24.0f - 12.0f);
        p.formant = u (rng) * 24.0f - 12.0f;
        p.mix = u (rng);
        p.robotism = u (rng);
        p.amount = u (rng);
        p.retuneMs = u (rng) * 200.0f;
        p.vibratoScale = u (rng) * 2.0f;
        p.scale = (int) (u (rng) * 12.99f);
        p.key = (int) (u (rng) * 12.99f) - 1;
        p.active = u (rng) > 0.1f;
        p.range = (int) (u (rng) * 2.99f);
    });
    writeMono ("automation_stress_out.wav", r.y, sr);
    auto bigSteps = [&] (const std::vector<float>& y)
    {
        int n = 0;
        for (size_t i = (size_t) (0.1 * sr); i < y.size(); ++i) if (std::abs (y[i] - y[i - 1]) > 0.2f) ++n;
        return n;
    };
    jline (fmt ("{\"scenario\":\"S7_automation_stress\",\"finite\":%s,\"peak_in\":%.3f,\"peak_out\":%.3f,\"peak_out_no_automation\":%.3f,"
                "\"steps_gt_0.2_with_automation\":%d,\"steps_gt_0.2_without\":%d}",
                allFinite (r.y) ? "true" : "false", peakAbs (x), peakAbs (r.y), peakAbs (base.y), bigSteps (r.y), bigSteps (base.y)));
}

//==============================================================================
//  S8  CPU (this container; noisy shared machine)
//==============================================================================
void cpu()
{
    for (const double sr : { 44100.0, 48000.0, 96000.0 })
    {
        auto mono = synth (sr, 20.0, melody ({ 57, 60, 62, 64, 59, 57, 55, 57, 60, 64 }, 2.0, 0.04, 0.2, 0.2));
        for (int nc : { 1, 2 })
            for (int rt : { 1, 0 })
                for (float formant : { 0.0f, 3.0f })
                {
                    if (nc == 2 && formant != 0.0f) continue;
                    std::vector<float> l = mono, r = mono;
                    auto e = std::make_unique<TuneEngine>();
                    e->prepare (sr, rt != 0);
                    auto p = defaultParams(); p.formant = formant;
                    e->setParams (p);
                    const int block = 64;
                    std::vector<double> blockUs;
                    blockUs.reserve (l.size() / block + 1);
                    const auto t0 = std::chrono::steady_clock::now();
                    for (size_t pos = 0; pos < l.size(); pos += block)
                    {
                        const int n = (int) std::min ((size_t) block, l.size() - pos);
                        float* ch[2] = { l.data() + pos, r.data() + pos };
                        const auto b0 = std::chrono::steady_clock::now();
                        e->process (ch, nc, n);
                        const auto b1 = std::chrono::steady_clock::now();
                        blockUs.push_back (std::chrono::duration<double, std::micro> (b1 - b0).count());
                        TuneEngine::DisplayPoint d;
                        while (e->display.pop (d)) {}
                    }
                    const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
                    std::sort (blockUs.begin(), blockUs.end());
                    const double p99 = blockUs[(size_t) (0.99 * (double) blockUs.size())];
                    const double mx = blockUs.back();
                    const double budgetUs = block / sr * 1e6;
                    jline (fmt ("{\"scenario\":\"S8_cpu\",\"sr\":%.0f,\"channels\":%d,\"realtime\":%s,\"formant\":%.0f,\"block\":%d,"
                                "\"cpu_pct_of_realtime\":%.2f,\"x_realtime\":%.1f,\"block_p99_us\":%.1f,\"block_max_us\":%.1f,\"block_budget_us\":%.1f}",
                                sr, nc, rt ? "true" : "false", formant, block, 100.0 * secs / 20.0, 20.0 / secs, p99, mx, budgetUs));
                }
    }
}

//==============================================================================
//  S9  long run: 10 minutes
//==============================================================================
void longRun()
{
    const double sr = 48000.0;
    auto tile = synth (sr, 20.0, melody ({ 57, 59, 60, 62, 64, 62, 60, 59, 57, 55 }, 2.0, 0.04, 0.3));
    std::vector<float> x;
    x.reserve (tile.size() * 30);
    for (int i = 0; i < 30; ++i) x.insert (x.end(), tile.begin(), tile.end());
    const auto t0 = std::chrono::steady_clock::now();
    const auto r = runMono (x, sr, hardParams(), true, 512);
    const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    auto heldCents = [&] (double tileStart)
    {
        double worst = 0;
        const double notes[] = { 57, 59, 60, 62, 64, 62, 60, 59, 57, 55 };
        for (int k = 0; k < 10; ++k)
        {
            const double target = midiToHz (notes[k]);
            const double c = cents (measureHz (r.y, sr, (long) ((tileStart + k * 2.0 + 1.0) * sr) + r.lat, (int) (sr * 0.2), target), target);
            worst = std::max (worst, std::abs (c));
        }
        return worst;
    };
    jline (fmt ("{\"scenario\":\"S9_long_run\",\"minutes\":10,\"finite\":%s,\"worst_held_note_ct_first_tile\":%.2f,"
                "\"worst_held_note_ct_last_tile\":%.2f,\"render_seconds\":%.1f}",
                allFinite (r.y) ? "true" : "false", heldCents (20.0), heldCents (580.0), secs));
}

//==============================================================================
//  S17  30 minutes (final quality pass): stereo, every 2 s a random parameter set, every minute
//       an Auto key request (lock / re-analyze), REALTIME switched every 5 minutes. Output finite,
//       held notes still tuned at the end, block time, and the process memory (Linux: resident
//       set from /proc/self/statm) after 1 minute and at the end: the engine must not grow.
//==============================================================================
long residentKb()
{
#if defined (__linux__)
    FILE* f = std::fopen ("/proc/self/statm", "r");
    if (! f) return -1;
    long pages = 0, resident = 0;
    const int got = std::fscanf (f, "%ld %ld", &pages, &resident);
    std::fclose (f);
    return got == 2 ? resident * (sysconf (_SC_PAGESIZE) / 1024) : -1;
#else
    return -1;
#endif
}

void longRunMemory()
{
    const double sr = 48000.0;
    const char* env = std::getenv ("VSTM_AUDIT_S17_MINUTES");
    const double minutes = env != nullptr ? std::clamp (std::atof (env), 1.0, 120.0) : 30.0;
    const auto tile = synth (sr, 20.0, melody ({ 57, 59, 60, 62, 64, 62, 60, 59, 57, 55 }, 2.0, 0.04, 0.3));
    auto e = std::make_unique<TuneEngine>();
    e->prepare (sr, true);
    auto p = hardParams();
    e->setParams (p);
    std::mt19937 rng (17);
    std::uniform_real_distribution<float> u (0.0f, 1.0f);
    const int block = 256;
    std::vector<float> l ((size_t) block), r ((size_t) block);
    const size_t total = (size_t) (minutes * 60.0 * sr);
    const size_t tailFrom = total - std::min (total, (size_t) (20.0 * sr));   // the last 20 s: hard tune, no automation
    bool finite = true;
    double worstUs = 0.0, sumUs = 0.0;
    long kb1 = -1;
    size_t blocks = 0, tilePos = 0;
    std::vector<float> lastTile;
    lastTile.reserve (tile.size() + (size_t) block);
    const size_t autoEvery = (size_t) (2.0 * sr / block), keyEvery = (size_t) (60.0 * sr / block), rtEvery = (size_t) (300.0 * sr / block);
    bool tailStarted = false;
    const auto t0 = std::chrono::steady_clock::now();
    for (size_t pos = 0; pos < total; pos += (size_t) block, ++blocks)
    {
        const bool tail = pos >= tailFrom;
        if (tail && ! tailStarted)
        {
            tailStarted = true;
            p = hardParams();
            tilePos = 0;
            if (! e->isRealtime()) e->setRealtime (true);
        }
        if (! tail && blocks % autoEvery == 0)
        {
            p.amount = u (rng); p.retuneMs = 200.0f * u (rng); p.humanize = u (rng); p.robotism = u (rng);
            p.vibratoScale = 2.0f * u (rng); p.formant = 24.0f * u (rng) - 12.0f; p.transpose = std::round (24.0f * u (rng) - 12.0f);
            p.mix = u (rng); p.scale = (int) (u (rng) * ((float) kNumScales - 0.01f)); p.key = (int) (u (rng) * 12.99f) - 1;
            p.adaptive = u (rng) > 0.5f; p.followKeyChanges = u (rng) > 0.5f; p.range = (int) (u (rng) * 2.99f);
        }
        e->setParams (p);
        if (! tail && blocks > 0 && blocks % keyEvery == 0)
        {
            if ((blocks / keyEvery) % 2 == 0) e->requestKeyReanalyze();
            else e->requestKeyLock ((int) (u (rng) * 11.99f), u (rng));
        }
        if (! tail && blocks > 0 && blocks % rtEvery == 0)
            e->setRealtime (! e->isRealtime());
        for (int i = 0; i < block; ++i)
        {
            l[(size_t) i] = tile[tilePos];
            r[(size_t) i] = 0.5f * tile[tilePos];
            tilePos = (tilePos + 1) % tile.size();
        }
        float* ch[2] = { l.data(), r.data() };
        const auto b0 = std::chrono::steady_clock::now();
        e->process (ch, 2, block);
        const double us = std::chrono::duration<double, std::micro> (std::chrono::steady_clock::now() - b0).count();
        worstUs = std::max (worstUs, us);
        sumUs += us;
        TuneEngine::DisplayPoint d;
        while (e->display.pop (d)) {}
        for (int i = 0; i < block; ++i)
            finite = finite && std::isfinite (l[(size_t) i]) && std::isfinite (r[(size_t) i]);
        if (tail) lastTile.insert (lastTile.end(), l.begin(), l.end());
        if (kb1 < 0 && (double) pos / sr >= 60.0) kb1 = residentKb();
    }
    const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    const long kbEnd = residentKb();
    // the last 20 s are the melody from its start, hard-tuned 30 ct sharp notes: every note centre
    // (2 s notes) against its target, after the latency
    const double notes[] = { 57, 59, 60, 62, 64, 62, 60, 59, 57, 55 };
    double worst = 0.0;
    const int lat = e->getLatencySamples();
    for (int k = 1; k < 9; ++k)
    {
        const double target = midiToHz (notes[k]);
        const double c = cents (measureHz (lastTile, sr, (long) ((k * 2.0 + 1.0) * sr) + lat, (int) (sr * 0.2), target), target);
        worst = std::max (worst, std::abs (c));
    }
    jline (fmt ("{\"scenario\":\"S17_long_run_memory\",\"minutes\":%.0f,\"finite\":%s,\"rss_kb_after_1min\":%ld,\"rss_kb_end\":%ld,"
                "\"block_mean_us\":%.1f,\"block_max_us\":%.1f,\"block_budget_us\":%.1f,\"render_seconds\":%.1f,\"worst_held_note_ct_end\":%.2f}",
                minutes, finite ? "true" : "false", kb1, kbEnd, sumUs / (double) blocks, worstUs, block / sr * 1e6, secs, worst));
}

//==============================================================================
//  S10  noise, breath, consonant-like bursts: false voicing and level
//==============================================================================
void unvoiced()
{
    const double sr = 48000.0;
    std::mt19937 rng (9);
    std::normal_distribution<double> nd (0.0, 1.0);
    // 0-1 s white "s"-like noise (-30 dBFS rms, high-passed), 1-2 s breath (-48 dBFS, low-passed), 2-3 s silence,
    // 3-4 s vowel, 4-4.08 s "t"-like burst, then vowel again
    std::vector<float> x ((size_t) (sr * 5.5), 0.0f);
    double prev = 0, lp = 0;
    for (size_t i = 0; i < (size_t) sr; ++i) { const double w = nd (rng); x[i] = (float) (0.0316 * (w - prev) / 1.41); prev = w; }
    for (size_t i = (size_t) sr; i < (size_t) (2 * sr); ++i) { lp += 0.05 * (nd (rng) - lp); x[i] = (float) (0.004 * lp / 0.16); }
    auto v = synth (sr, 2.5, [] (double) { return midiToHz (57.4); });
    for (size_t i = 0; i < (size_t) sr; ++i) x[(size_t) (3 * sr) + i] = v[i];
    for (size_t i = 0; i < (size_t) (0.08 * sr); ++i) x[(size_t) (4 * sr) + i] = (float) (0.05 * nd (rng) * std::exp (-(double) i / (0.01 * sr)));
    for (size_t i = (size_t) (1.08 * sr); i < (size_t) (2.5 * sr) && (size_t) (3 * sr) + i < x.size(); ++i) x[(size_t) (3 * sr) + i] = v[i];
    writeMono ("unvoiced_in.wav", x, sr);
    const auto r = runMono (x, sr, hardParams());
    writeMono ("unvoiced_hard_out.wav", r.y, sr);
    auto voicedIn = [&] (double a, double b)
    {
        int v2 = 0, n = 0;
        for (size_t h = 0; h < r.d.size(); ++h)
        {
            const double t = (double) (h + 1) * r.hopS - 0.024;
            if (t < a || t >= b) continue;
            ++n;
            if (r.d[h].flags & TuneEngine::kVoiced) ++v2;
        }
        return n ? (double) v2 / n : 0.0;
    };
    auto lvl = [&] (double a, double b) { return rmsDb (r.y, (size_t) (a * sr) + (size_t) r.lat, (size_t) (b * sr) + (size_t) r.lat) - rmsDb (x, (size_t) (a * sr), (size_t) (b * sr)); };
    jline (fmt ("{\"scenario\":\"S10_unvoiced\",\"voiced_frac_white_noise\":%.3f,\"voiced_frac_breath\":%.3f,\"voiced_frac_silence\":%.3f,"
                "\"voiced_frac_vowel\":%.3f,\"level_change_db_white_noise\":%.2f,\"level_change_db_breath\":%.2f,\"level_change_db_vowel\":%.2f,"
                "\"level_change_db_t_burst\":%.2f}",
                voicedIn (0.05, 0.95), voicedIn (1.05, 1.95), voicedIn (2.05, 2.95), voicedIn (3.1, 3.95),
                lvl (0.05, 0.95), lvl (1.05, 1.95), lvl (3.1, 3.95), lvl (4.0, 4.06)));
}

//==============================================================================
//  S11  automatic key
//==============================================================================
void autoKey()
{
    const double sr = 48000.0;
    auto keyTrace = [&] (const std::vector<float>& x, int scale, bool follow = false)
    {
        auto p = defaultParams(); p.key = -1; p.scale = scale; p.followKeyChanges = follow;
        auto e = std::make_unique<TuneEngine>();
        e->prepare (sr, true);
        e->setParams (p);
        std::vector<float> y = x;
        std::string trace;
        int last = -2;
        double firstKeyAt = -1;
        for (size_t pos = 0; pos < y.size(); pos += 480)
        {
            const int n = (int) std::min ((size_t) 480, y.size() - pos);
            float* ch[2] = { y.data() + pos, nullptr };
            e->process (ch, 1, n);
            TuneEngine::DisplayPoint d;
            while (e->display.pop (d)) {}
            const int k = e->detectedKey.load();
            if (k != last)
            {
                trace += fmt ("%s[%.2f,\"%s\"]", trace.empty() ? "" : ",", (double) (pos + (size_t) n) / sr, k < 0 ? "none" : noteName (k));
                if (k >= 0 && firstKeyAt < 0) firstKeyAt = (double) (pos + (size_t) n) / sr;
                last = k;
            }
        }
        return fmt ("\"first_key_s\":%.2f,\"trace\":[%s],\"end_state\":%d,\"end_candidate\":\"%s\",\"end_confidence\":%.3f",
                    firstKeyAt, trace.c_str(), e->keyState.load(), e->keyCandidate.load() < 0 ? "none" : noteName (e->keyCandidate.load()),
                    e->keyConfidence.load());
    };
    // a) one note only (C4) for 5 s, Major
    {
        auto x = synth (sr, 5.0, [] (double) { return midiToHz (60.1); });
        jline (fmt ("{\"scenario\":\"S11_auto_key\",\"case\":\"single note C4 only, scale Major\",%s}", keyTrace (x, kMajor).c_str()));
    }
    // b) two notes (C4, G4 alternating) for 6 s
    {
        auto x = synth (sr, 6.0, melody ({ 60, 67, 60, 67, 60, 67, 60, 67, 60, 67, 60, 67 }, 0.5, 0.03, 0.1));
        jline (fmt ("{\"scenario\":\"S11_auto_key\",\"case\":\"two notes C4/G4, scale Major\",%s}", keyTrace (x, kMajor).c_str()));
    }
    // c) C major melody 8 s, then G major melody 8 s (modulation)
    {
        std::vector<double> m;
        const double cMaj[] = { 60, 62, 64, 65, 67, 69, 71, 72, 67, 64, 65, 62, 60, 64, 67, 60 };
        const double gMaj[] = { 67, 69, 71, 72, 74, 76, 78, 79, 74, 71, 72, 69, 67, 71, 74, 66 };
        for (double n : cMaj) m.push_back (n);
        for (double n : gMaj) m.push_back (n);
        auto x = synth (sr, 16.0, melody (m, 0.5, 0.03, 0.1));
        jline (fmt ("{\"scenario\":\"S11_auto_key\",\"case\":\"C major 0-8 s then G major 8-16 s, scale Major\",%s}", keyTrace (x, kMajor).c_str()));
    }
    // d) A natural-minor melody, scale set to Major (relative major ambiguity)
    {
        const double aMin[] = { 57, 59, 60, 62, 64, 65, 67, 69, 64, 60, 62, 59, 57, 60, 64, 57 };
        auto x = synth (sr, 8.0, melody (std::vector<double> (aMin, aMin + 16), 0.5, 0.03, 0.1));
        jline (fmt ("{\"scenario\":\"S11_auto_key\",\"case\":\"A minor melody, scale Major selected\",%s}", keyTrace (x, kMajor).c_str()));
        jline (fmt ("{\"scenario\":\"S11_auto_key\",\"case\":\"A minor melody, scale Minor selected\",%s}", keyTrace (x, kMinor).c_str()));
    }
    // e) final quality pass (one-shot lock): 24 s of C major, then 8 s in G major; follow key changes off / on
    {
        std::vector<double> m;
        const double cMaj[] = { 60, 62, 64, 65, 67, 69, 71, 72, 67, 64, 65, 62, 60, 64, 67, 60 };
        const double gMaj[] = { 67, 69, 71, 72, 74, 76, 78, 79, 74, 71, 72, 69, 67, 71, 74, 66 };
        for (int r = 0; r < 3; ++r) for (double n : cMaj) m.push_back (n);
        for (double n : gMaj) m.push_back (n);
        auto x = synth (sr, 32.0, melody (m, 0.5, 0.03, 0.1));
        jline (fmt ("{\"scenario\":\"S11_auto_key\",\"case\":\"C major 0-24 s then G major 24-32 s, follow off\",%s}", keyTrace (x, kMajor).c_str()));
        jline (fmt ("{\"scenario\":\"S11_auto_key\",\"case\":\"C major 0-24 s then G major 24-32 s, follow ON\",%s}", keyTrace (x, kMajor, true).c_str()));
    }
    // f) pentatonic only (C D E G A), 20 s: no note tells C, F and G major apart
    {
        std::vector<double> m;
        for (int r = 0; r < 5; ++r) for (double n : { 60.0, 62.0, 64.0, 67.0, 69.0, 67.0, 64.0, 62.0, 60.0, 64.0, 67.0, 72.0, 69.0 }) m.push_back (n);
        auto x = synth (sr, 20.0, melody (m, 0.3, 0.03, 0.1));
        jline (fmt ("{\"scenario\":\"S11_auto_key\",\"case\":\"pentatonic C D E G A 20 s, scale Major\",%s}", keyTrace (x, kMajor).c_str()));
    }
}

//==============================================================================
//  S12  formant / transpose extremes
//==============================================================================
void extremes()
{
    const double sr = 48000.0;
    auto x = synth (sr, 2.0, [] (double) { return midiToHz (57.40); });
    const double inDb = rmsDb (x, (size_t) (1.0 * sr), (size_t) (1.5 * sr));
    std::string res;
    for (const auto& fc : std::vector<std::pair<float, float>> { { 0.f, -12.f }, { 0.f, -6.f }, { 0.f, 6.f }, { 0.f, 12.f },
                                                                  { -12.f, 0.f }, { -5.f, 0.f }, { 7.f, 0.f }, { 12.f, 0.f }, { -12.f, -4.f } })
    {
        auto p = hardParams(); p.transpose = fc.first; p.formant = fc.second;
        const auto r = runMono (x, sr, p, true, 256);
        const double expect = 220.0 * std::pow (2.0, fc.first / 12.0);
        const double c = cents (measureHz (r.y, sr, (long) (1.0 * sr) + r.lat, (int) (sr * 0.2), expect), expect);
        const double outDb = rmsDb (r.y, (size_t) (1.0 * sr) + (size_t) r.lat, (size_t) (1.5 * sr) + (size_t) r.lat);
        res += fmt ("%s{\"transpose\":%.0f,\"formant\":%.0f,\"pitch_err_ct\":%.2f,\"level_change_db\":%.2f,\"peak_out\":%.3f,\"finite\":%s}",
                    res.empty() ? "" : ",", fc.first, fc.second, c, outDb - inDb, peakAbs (r.y), allFinite (r.y) ? "true" : "false");
        if (fc.first == 0.f && (fc.second == -12.f || fc.second == 12.f))
            writeMono (fmt ("formant_%+.0f_out.wav", fc.second), r.y, sr);
    }
    writeMono ("extremes_in.wav", x, sr);
    jline (fmt ("{\"scenario\":\"S12_formant_transpose\",\"input_peak\":%.3f,\"results\":[%s]}", peakAbs (x), res.c_str()));
}

//==============================================================================
//  S13  MIX 50 %: dry and tuned voices beat against each other
//==============================================================================
void mixBeating()
{
    const double sr = 48000.0;
    auto x = synth (sr, 3.0, [] (double) { return midiToHz (57.40); });
    std::string res;
    for (float mix : { 0.0f, 0.5f, 1.0f })
    {
        auto p = hardParams(); p.mix = mix;
        const auto r = runMono (x, sr, p);
        // 20 ms RMS envelope between 1.0 and 2.5 s (output timeline incl. latency)
        double lo = 1e9, hi = -1e9;
        const size_t w = (size_t) (0.02 * sr);
        for (size_t a = (size_t) (1.0 * sr) + (size_t) r.lat; a + w < (size_t) (2.5 * sr) + (size_t) r.lat; a += w)
        {
            const double d = rmsDb (r.y, a, a + w);
            lo = std::min (lo, d); hi = std::max (hi, d);
        }
        const double inDb = rmsDb (x, (size_t) sr, (size_t) (2.5 * sr));
        const double outDb = rmsDb (r.y, (size_t) sr + (size_t) r.lat, (size_t) (2.5 * sr) + (size_t) r.lat);
        res += fmt ("%s{\"mix\":%.1f,\"envelope_pp_db\":%.2f,\"level_change_db\":%.2f}", res.empty() ? "" : ",", mix, hi - lo, outDb - inDb);
        if (mix == 0.5f) writeMono ("mix50_out.wav", r.y, sr);
    }
    jline (fmt ("{\"scenario\":\"S13_mix\",\"input\":\"A3 +40 ct, hard tune\",\"expected_beat_hz\":%.2f,\"results\":[%s]}",
                220.0 * (std::pow (2.0, 40.0 / 1200.0) - 1.0), res.c_str()));
}

//==============================================================================
//  S14  ROBOTISM sweep: transition time, vibrato kept, steady error (+ the internal values from the formulas)
//==============================================================================
void robotismSweep()
{
    const double sr = 48000.0;
    auto step = synth (sr, 2.0, [] (double t) { const double k = std::clamp ((t - 1.0) / 0.02, 0.0, 1.0); return midiToHz (57.0 + 3.0 * k); });
    auto vib = synth (sr, 2.0, [] (double t) { return midiToHz (57.0 + 0.5 * std::sin (2.0 * kPi * 5.5 * t)); });
    auto sharp = synth (sr, 2.0, [] (double) { return midiToHz (57.40); });
    for (const float rb : { 0.0f, 0.10f, 0.25f, 0.35f, 0.50f, 0.65f, 0.80f, 0.90f, 1.0f })
    {
        auto p = defaultParams(); p.scale = kMajor; p.key = 0; p.robotism = rb;      // retune 15 ms, humanize 35 %
        const auto a = runMono (step, sr, p);
        double t10 = -1, t90 = -1;
        for (double t = 0.9; t < 1.6; t += 0.002)
        {
            const double m = hzToMidi (measureHz (a.y, sr, (long) (t * sr) + a.lat, (int) (sr * 0.008), 240.0));
            if (t10 < 0 && m > 57.3) t10 = t;
            if (t90 < 0 && m > 59.7) t90 = t;
        }
        auto pc = p; pc.scale = kChromatic;
        const auto b = runMono (vib, sr, pc);
        double lo = 1e9, hi = -1e9;
        for (double t = 1.0; t < 1.6; t += 0.004)
        {
            const double m = hzToMidi (measureHz (b.y, sr, (long) (t * sr) + b.lat, (int) (sr * 0.01), 220.0));
            lo = std::min (lo, m); hi = std::max (hi, m);
        }
        const auto c = runMono (sharp, sr, pc);
        const double c05 = cents (measureHz (c.y, sr, (long) (0.5 * sr) + c.lat, (int) (sr * 0.1), 220.0), 220.0);
        const double c15 = cents (measureHz (c.y, sr, (long) (1.5 * sr) + c.lat, (int) (sr * 0.2), 220.0), 220.0);

        // the macro's internal values (PitchCorrector.h formulas, retune 15 ms, humanize 0.35; keep in sync)
        const float robot = clamp01 ((0.5f - rb) * 2.0f), human = std::pow (clamp01 ((rb - 0.5f) * 2.0f), 1.6f);
        const float hystCt = 15.0f + 20.0f * human;
        const float dwellMs = 22.0f + 30.0f * human - 8.0f * robot;
        const float glideMs = 110.0f * human;
        const float onsetMs = 70.0f * human;
        const float speedMul = (1.0f - robot) * (1.0f - 0.5f * robot) * (1.0f + 3.0f * human);
        const float hz = clamp01 (0.35f * (1.0f - robot) + 0.4f * human * (1.0f - 0.35f));
        const float tauShort = 15.0f * speedMul, tauLong = 15.0f * speedMul * (1.0f + 6.0f * hz);
        jline (fmt ("{\"scenario\":\"S14_robotism\",\"robotism_pct\":%.0f,\"transition_10_90_ms\":%.1f,\"vibrato_pp_ct_of_100\":%.1f,"
                    "\"err_ct_at_0.5s\":%.2f,\"err_ct_at_1.5s\":%.2f,\"hyst_ct\":%.0f,\"dwell_ms\":%.0f,\"glide_ms\":%.0f,\"onset_ms\":%.0f,"
                    "\"tau_short_ms\":%.1f,\"tau_sustained_ms\":%.1f}",
                    rb * 100.0f, (t10 >= 0 && t90 >= 0) ? (t90 - t10) * 1000.0 : -1.0, (hi - lo) * 100.0, c05, c15,
                    hystCt, dwellMs, glideMs, onsetMs, tauShort, tauLong));
    }
}
//==============================================================================
//  S16  ROBOTISM across voice types: the S14 measures for bass, tenor, alto and soprano
//       (step of a minor third, +-50 ct vibrato at 5.5 Hz, a note sung 40 ct sharp)
//==============================================================================
void robotismVoices()
{
    const double sr = 48000.0;
    struct Voice { const char* name; double base; VoiceSpec v; int range; };
    VoiceSpec bass = vowelO(); bass.fund = 0.25; bass.tilt = 1.6;
    VoiceSpec alto = vowelO();
    const Voice voices[] = { { "bass", 43.0, bass, 2 }, { "tenor", 57.0, vowelA(), 1 }, { "alto", 64.0, alto, 1 }, { "soprano", 69.0, vowelI(), 0 } };
    for (const auto& vc : voices)
    {
        const double b = vc.base;
        auto step = synth (sr, 2.0, [b] (double t) { const double k = std::clamp ((t - 1.0) / 0.02, 0.0, 1.0); return midiToHz (b + 3.0 * k); }, vc.v);
        auto vib = synth (sr, 2.0, [b] (double t) { return midiToHz (b + 0.5 * std::sin (2.0 * kPi * 5.5 * t)); }, vc.v);
        auto sharp = synth (sr, 2.0, [b] (double) { return midiToHz (b + 0.40); }, vc.v);
        // measurement window: at least 1.5 periods of the lower note
        const int win = std::max ((int) (sr * 0.008), (int) (1.5 * sr / midiToHz (b)));
        for (const float rb : { 0.0f, 0.10f, 0.25f, 0.35f, 0.50f, 0.65f, 0.80f, 0.90f, 1.0f })
        {
            auto p = defaultParams(); p.scale = kChromatic; p.key = 0; p.robotism = rb; p.range = vc.range;   // retune 15 ms, humanize 35 %
            const auto a = runMono (step, sr, p);
            double t10 = -1, t90 = -1, settle = -1;
            for (double t = 0.9; t < 1.7; t += 0.002)
            {
                const double m = hzToMidi (measureHz (a.y, sr, (long) (t * sr) + a.lat - win / 2, win, midiToHz (b + 1.5)));
                if (t10 < 0 && m > b + 0.3) t10 = t;
                if (t90 < 0 && m > b + 2.7) t90 = t;
                if (t90 >= 0 && settle < 0 && std::abs (m - (b + 3.0)) < 0.10)
                {
                    bool stays = true;
                    for (double u = t; u < t + 0.05; u += 0.004)
                        stays = stays && std::abs (hzToMidi (measureHz (a.y, sr, (long) (u * sr) + a.lat - win / 2, win, midiToHz (b + 3.0))) - (b + 3.0)) < 0.10;
                    if (stays) settle = t;
                }
            }
            const auto bv = runMono (vib, sr, p);
            double lo = 1e9, hi = -1e9;
            for (double t = 1.0; t < 1.6; t += 0.004)
            {
                const double m = hzToMidi (measureHz (bv.y, sr, (long) (t * sr) + bv.lat - win / 2, win, midiToHz (b)));
                lo = std::min (lo, m); hi = std::max (hi, m);
            }
            const auto c = runMono (sharp, sr, p);
            const double c03 = cents (measureHz (c.y, sr, (long) (0.3 * sr) + c.lat, (int) (sr * 0.1), midiToHz (b)), midiToHz (b));
            const double c10 = cents (measureHz (c.y, sr, (long) (1.0 * sr) + c.lat, (int) (sr * 0.2), midiToHz (b)), midiToHz (b));
            jline (fmt ("{\"scenario\":\"S16_robotism_voices\",\"voice\":\"%s\",\"base_midi\":%.0f,\"robotism_pct\":%.0f,"
                        "\"transition_10_90_ms\":%.1f,\"settle_10ct_ms\":%.1f,\"vibrato_kept_pct\":%.1f,\"err_ct_0.3s\":%.2f,\"err_ct_1.0s\":%.2f}",
                        vc.name, b, rb * 100.0f, (t10 >= 0 && t90 >= 0) ? (t90 - t10) * 1000.0 : -1.0, settle >= 0 ? (settle - 1.0) * 1000.0 : -1.0,
                        (hi - lo) * 100.0, c03, c10));
        }
    }
}

//==============================================================================
//  S15  a NaN parameter value (e.g. from a corrupted preset/session) for 0.5 s, then a valid value again
//==============================================================================
void nanParameter()
{
    const double sr = 48000.0;
    auto x = synth (sr, 4.0, [] (double) { return midiToHz (57.40); });
    for (const char* which : { "amount", "formant", "transpose", "mix" })
    {
        const auto r = runMono (x, sr, hardParams(), true, 256, [&] (TuneEngine&, size_t pos, TuneEngine::Params& p)
        {
            const bool bad = pos >= (size_t) (1.0 * sr) && pos < (size_t) (1.5 * sr);
            const float nan = std::numeric_limits<float>::quiet_NaN();
            p = hardParams();
            if (bad && std::strcmp (which, "amount") == 0) p.amount = nan;
            if (bad && std::strcmp (which, "formant") == 0) p.formant = nan;
            if (bad && std::strcmp (which, "transpose") == 0) p.transpose = nan;
            if (bad && std::strcmp (which, "mix") == 0) p.mix = nan;
        });
        size_t badDuring = 0, badAfter = 0;
        for (size_t i = 0; i < r.y.size(); ++i)
            if (! std::isfinite (r.y[i])) { if (i < (size_t) (2.0 * sr)) ++badDuring; else ++badAfter; }
        const double c = cents (measureHz (r.y, sr, (long) (3.2 * sr) + r.lat, (int) (sr * 0.2), 220.0), 220.0);
        std::vector<float> fin (r.y);
        for (auto& v : fin) if (! std::isfinite (v)) v = 0.0f;
        const double outDb = rmsDb (fin, (size_t) (2.5 * sr), (size_t) (3.5 * sr));
        const double inDb = rmsDb (x, (size_t) (2.5 * sr), (size_t) (3.5 * sr));
        jline (fmt ("{\"scenario\":\"S15_nan_parameter\",\"param\":\"%s\",\"nan_window_s\":\"1.0-1.5\",\"nonfinite_out_before_2s\":%zu,"
                    "\"nonfinite_out_after_2s\":%zu,\"finite_out_rms_2.5-3.5s_db_rel_input\":%.1f,\"tuning_at_3.2s_ct\":%.2f,\"expected_ct\":0}",
                    which, badDuring, badAfter, outDb - inDb, c));
    }
}
} // namespace

int main (int argc, char** argv)
{
    if (argc < 2 || std::strcmp (argv[1], "--help") == 0)
    {
        std::fprintf (stderr, "usage: VSTMTune_AuditProbe <output_folder> [S1 ... S17]   (no scenario = all)\n");
        return 2;
    }
    OUT = argv[1];
    J = std::fopen ((OUT + "/measurements.jsonl").c_str(), "a");
    if (! J) { std::perror ((OUT + "/measurements.jsonl").c_str()); return 1; }
    std::vector<std::string> only (argv + 2, argv + argc);
    auto want = [&] (const char* s) { return only.empty() || std::find (only.begin(), only.end(), s) != only.end(); };

    if (want ("S1")) leaps();
    if (want ("S2")) nanInf();
    if (want ("S3")) realtimeSwitch();
    if (want ("S4")) sampleRates();
    if (want ("S5")) blockSizes();
    if (want ("S6")) stereo();
    if (want ("S7")) automation();
    if (want ("S10")) unvoiced();
    if (want ("S11")) autoKey();
    if (want ("S12")) extremes();
    if (want ("S13")) mixBeating();
    if (want ("S14")) robotismSweep();
    if (want ("S15")) nanParameter();
    if (want ("S16")) robotismVoices();
    if (want ("S9")) longRun();
    if (want ("S17")) longRunMemory();
    if (want ("S8")) cpu();
    std::fclose (J);
    return 0;
}
