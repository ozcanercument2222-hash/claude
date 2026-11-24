//==============================================================================
//  VSTM Tune - CPU / latency / real-time safety benchmark (JUCE gerektirmez; MEASURED)
//
//    bench_probe matrix OUT.csv [--seconds 20]        sample rates x blocks x channels x modes x settings
//    bench_probe longrun OUT.csv --minutes 60          48 kHz stereo, 256 blocks, automation, mode switches
//    bench_probe instances OUT.csv                     1 / 4 / 8 / 16 engines, one thread and 4 threads
//    bench_probe denormal OUT.csv                      loud vowel then 10 s silence, FTZ off / on
//
//  Per configuration: block processing time (steady_clock) average / p95 / p99 / max in microseconds,
//  the block's real-time budget, CPU % of one core (time spent / audio time), the worst block as a share
//  of its budget, heap allocations made inside process() (operator new is counted while it runs: must
//  be 0), output finite. Signal: a sung vowel melody with vibrato; settings "default" (Auto key, Major,
//  ROBOTISM 50 %) and "heavy" (hard tune, transpose +7, formant -3, Adaptive on, MIX 70 %).
//  A shared virtual machine is noisy: max values are worst cases of this machine, not of a DAW host.
//==============================================================================
#include "dsp/TuneEngine.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <new>
#include <random>
#include <string>
#include <thread>
#include <vector>
#if defined(__SSE__) || defined(_M_X64)
 #include <xmmintrin.h>
 #include <pmmintrin.h>
#endif

//------------------------------------------------------------------ allocation counter
static std::atomic<long> gAllocs { 0 };
static thread_local bool gCounting = false;
void* operator new (std::size_t n)
{
    if (gCounting) gAllocs.fetch_add (1, std::memory_order_relaxed);
    if (void* p = std::malloc (n ? n : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[] (std::size_t n)
{
    if (gCounting) gAllocs.fetch_add (1, std::memory_order_relaxed);
    if (void* p = std::malloc (n ? n : 1)) return p;
    throw std::bad_alloc();
}
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }

using namespace vstm;

namespace
{
struct Res { double b0 = 0, a1 = 0, a2 = 0, y1 = 0, y2 = 0;
    void set (double sr, double f, double bw) { const double r = std::exp (-kPi * bw / sr); a1 = -2 * r * std::cos (2 * kPi * f / sr); a2 = r * r; b0 = (1 - r) * 2; }
    double run (double x) { const double y = b0 * x - a1 * y1 - a2 * y2; y2 = y1; y1 = y; return y; } };

std::vector<float> makeVowelMelody (double sr, double secs);
std::vector<float> vowelMelody (double sr, double secs)     // cached: generating it costs more than processing it
{
    static std::vector<std::pair<std::pair<double, double>, std::vector<float>>> cache;
    for (auto& c : cache) if (c.first.first == sr && c.first.second == secs) return c.second;
    cache.push_back ({ { sr, secs }, makeVowelMelody (sr, secs) });
    return cache.back().second;
}
std::vector<float> makeVowelMelody (double sr, double secs)
{
    const int n = (int) (sr * secs);
    std::vector<float> out ((size_t) n);
    Res f1, f2, f3; f1.set (sr, 730, 90); f2.set (sr, 1090, 110); f3.set (sr, 2440, 170);
    const int notes[] = { 57, 59, 60, 62, 64, 65, 64, 62, 60, 59 };
    double ph = 0, prev = 0;
    for (int i = 0; i < n; ++i)
    {
        const double t = i / sr;
        const int k = (int) (t / 0.4) % 10;
        const bool gap = std::fmod (t, 4.0) > 3.7;            // a breath every 4 s
        const double f0 = midiToHz (notes[k] + 0.3 + 0.3 * std::sin (2 * kPi * 5.5 * t));
        ph += f0 / sr;
        double src = 0;
        const int H = std::min (40, (int) (0.45 * sr / f0));
        for (int h = 1; h <= H; ++h) src += std::sin (2 * kPi * h * ph) / (double) (h * h) * (h == 1 ? 1.0 : 1.6);
        const double p = src - prev; prev = src;
        out[(size_t) i] = gap ? 0.0f : (float) (0.06 * (f1.run (p) + 0.7 * f2.run (p) + 0.35 * f3.run (p)));
    }
    return out;
}

TuneEngine::Params setting (const std::string& s)
{
    TuneEngine::Params p;
    if (s == "heavy") { p.key = 0; p.scale = kChromatic; p.robotism = 0.0f; p.retuneMs = 0.0f; p.humanize = 0.0f;
                        p.transpose = 7.0f; p.formant = -3.0f; p.adaptive = true; p.mix = 0.7f; }
    return p;                                            // default: Auto key, Major, ROBOTISM 50 %
}

struct Stats { double avg = 0, p95 = 0, p99 = 0, mx = 0, cpu = 0, worstShare = 0; long allocs = 0; bool finite = true; };

Stats runConfig (double sr, int block, int nch, bool rt, const std::string& set, double secs,
                 const std::function<void (TuneEngine&, double)>& automate = nullptr)
{
    auto x = vowelMelody (sr, secs);
    std::vector<float> L = x, R = x;
    for (auto& v : R) v *= 0.8f;
    auto e = std::make_unique<TuneEngine>();
    e->prepare (sr, rt);
    e->setParams (setting (set));
    std::vector<double> t;
    t.reserve (x.size() / (size_t) block + 2);
    const long a0 = gAllocs.load();
    Stats s;
    for (size_t pos = 0; pos < x.size(); pos += (size_t) block)
    {
        const int n = (int) std::min ((size_t) block, x.size() - pos);
        float* ch[2] = { L.data() + pos, nch > 1 ? R.data() + pos : nullptr };
        if (automate) automate (*e, (double) pos / sr);
        const auto c0 = std::chrono::steady_clock::now();
        gCounting = true;
        e->process (ch, nch, n);
        gCounting = false;
        const auto c1 = std::chrono::steady_clock::now();
        t.push_back (std::chrono::duration<double, std::micro> (c1 - c0).count());
        TuneEngine::DisplayPoint d;
        while (e->display.pop (d)) {}
    }
    s.allocs = gAllocs.load() - a0;
    for (size_t i = 0; i < x.size(); ++i) s.finite = s.finite && std::isfinite (L[i]) && std::isfinite (R[i]);
    double sum = 0; for (double v : t) sum += v;
    auto sorted = t; std::sort (sorted.begin(), sorted.end());
    s.avg = sum / (double) t.size();
    s.p95 = sorted[(size_t) (0.95 * (double) (sorted.size() - 1))];
    s.p99 = sorted[(size_t) (0.99 * (double) (sorted.size() - 1))];
    s.mx = sorted.back();
    const double budget = 1.0e6 * block / sr;
    s.cpu = 100.0 * sum / (1.0e6 * secs);
    s.worstShare = s.mx / budget;
    return s;
}

long rssKb()
{
    FILE* f = std::fopen ("/proc/self/status", "r");
    if (! f) return -1;
    char line[256];
    long kb = -1;
    while (std::fgets (line, sizeof (line), f))
        if (std::strncmp (line, "VmRSS:", 6) == 0) { kb = std::atol (line + 6); break; }
    std::fclose (f);
    return kb;
}
} // namespace

int main (int argc, char** argv)
{
    if (argc < 3) { std::fprintf (stderr, "usage: bench_probe matrix|longrun|instances|denormal OUT.csv [--seconds S] [--minutes M]\n"); return 2; }
    const std::string mode = argv[1];
    FILE* out = std::fopen (argv[2], "w");
    if (! out) return 3;
    double seconds = 20.0, minutes = 60.0;
    for (int i = 3; i + 1 < argc; ++i)
    {
        if (std::strcmp (argv[i], "--seconds") == 0) seconds = std::atof (argv[i + 1]);
        if (std::strcmp (argv[i], "--minutes") == 0) minutes = std::atof (argv[i + 1]);
    }
#if defined(__SSE__) || defined(_M_X64)
    _MM_SET_FLUSH_ZERO_MODE (_MM_FLUSH_ZERO_ON);          // what juce::ScopedNoDenormals does in the plugin
    _MM_SET_DENORMALS_ZERO_MODE (_MM_DENORMALS_ZERO_ON);
#endif
    std::fprintf (out, "test,sample_rate,block,channels,mode,setting,instances,threads,avg_us,p95_us,p99_us,max_us,budget_us,cpu_pct_one_core,worst_block_share_of_budget,allocs_in_process,finite,latency_samples,latency_ms,note\n");
    auto row = [&] (const char* test, double sr, int block, int nch, bool rt, const std::string& set, int inst, int thr, const Stats& s, const std::string& note)
    {
        const int lat = TuneEngine::latencyFor (sr, rt);
        std::fprintf (out, "%s,%.0f,%d,%d,%s,%s,%d,%d,%.2f,%.2f,%.2f,%.2f,%.2f,%.3f,%.4f,%ld,%s,%d,%.2f,%s\n", test, sr, block, nch, rt ? "realtime" : "hq",
                      set.c_str(), inst, thr, s.avg, s.p95, s.p99, s.mx, 1.0e6 * block / sr, s.cpu, s.worstShare, s.allocs,
                      s.finite ? "yes" : "NO", lat, 1000.0 * lat / sr, note.c_str());
        std::fflush (out);
    };

    if (mode == "matrix")
    {
        for (double sr : { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 })
            for (int block : { 32, 64, 128, 256, 512, 1024 })
                for (int nch : { 1, 2 })
                    for (bool rt : { true, false })
                        for (const char* set : { "default", "heavy" })
                        {
                            if (! (nch == 2 || std::strcmp (set, "default") == 0)) continue;   // mono only with default
                            const auto s = runConfig (sr, block, nch, rt, set, seconds);
                            row ("matrix", sr, block, nch, rt, set, 1, 1, s, "");
                        }
        // parameter automation: every parameter moved every 10 ms (48 kHz, 128 blocks, stereo)
        std::mt19937 rng (4);
        std::uniform_real_distribution<float> u (0.0f, 1.0f);
        auto automate = [&] (TuneEngine& e, double)
        {
            TuneEngine::Params p;
            p.key = (int) (u (rng) * 13.0f) - 1; p.scale = (int) (u (rng) * 12.9f); p.amount = u (rng);
            p.retuneMs = 200.0f * u (rng); p.humanize = u (rng); p.robotism = u (rng); p.vibratoScale = 2.0f * u (rng);
            p.formant = -12.0f + 24.0f * u (rng); p.transpose = std::round (-12.0f + 24.0f * u (rng)); p.mix = u (rng);
            p.adaptive = u (rng) > 0.5f; p.range = (int) (u (rng) * 2.99f); p.active = u (rng) > 0.1f;
            e.setParams (p);
        };
        row ("automation", 48000.0, 128, 2, true, "random", 1, 1, runConfig (48000.0, 128, 2, true, "default", seconds, automate),
             "all parameters random every block");
        int k = 0;
        auto toggles = [&] (TuneEngine& e, double t) { const bool rt = ((int) (t / 0.5)) % 2 == 0; if (k++ % 4 == 0) e.setRealtime (rt); };
        row ("mode_toggles", 48000.0, 256, 2, true, "default", 1, 1, runConfig (48000.0, 256, 2, true, "default", seconds, toggles),
             "REALTIME/HQ toggled every 0.5 s");
    }
    else if (mode == "longrun")
    {
        const double sr = 48000.0;
        const int block = 256;
        auto x = vowelMelody (sr, 30.0);                     // 30 s looped
        auto e = std::make_unique<TuneEngine>();
        e->prepare (sr, true);
        e->setParams (setting ("default"));
        std::mt19937 rng (9);
        std::uniform_real_distribution<float> u (0.0f, 1.0f);
        std::vector<float> L ((size_t) block), R ((size_t) block);
        const long total = (long) (minutes * 60.0 * sr);
        double worst = 0, sum = 0;
        long blocks = 0, allocs0 = gAllocs.load(), nonfinite = 0;
        const long rss0 = rssKb();
        long rssMax = rss0;
        std::vector<double> perMinuteMax;
        double minuteMax = 0;
        bool rt = true;
        for (long pos = 0; pos < total; pos += block)
        {
            for (int i = 0; i < block; ++i) { const float v = x[(size_t) ((pos + i) % (long) x.size())]; L[(size_t) i] = v; R[(size_t) i] = 0.8f * v; }
            if (pos % (long) (2.0 * sr) < block)              // all parameters every 2 s
            {
                TuneEngine::Params p;
                p.key = (int) (u (rng) * 13.0f) - 1; p.scale = (int) (u (rng) * 12.9f); p.robotism = u (rng);
                p.retuneMs = 100.0f * u (rng); p.formant = -6.0f + 12.0f * u (rng); p.transpose = std::round (-7.0f + 14.0f * u (rng));
                p.mix = 0.5f + 0.5f * u (rng); p.adaptive = u (rng) > 0.5f;
                e->setParams (p);
            }
            if (pos % (long) (60.0 * sr) < block) e->requestKeyReanalyze();
            if (pos % (long) (300.0 * sr) < block && pos > 0) { rt = ! rt; e->setRealtime (rt); }
            float* ch[2] = { L.data(), R.data() };
            const auto c0 = std::chrono::steady_clock::now();
            gCounting = true;
            e->process (ch, 2, block);
            gCounting = false;
            const double us = std::chrono::duration<double, std::micro> (std::chrono::steady_clock::now() - c0).count();
            TuneEngine::DisplayPoint d;
            while (e->display.pop (d)) {}
            for (int i = 0; i < block; ++i) if (! std::isfinite (L[(size_t) i]) || ! std::isfinite (R[(size_t) i])) ++nonfinite;
            worst = std::max (worst, us); minuteMax = std::max (minuteMax, us);
            sum += us; ++blocks;
            if ((pos + block) % (long) (60.0 * sr) < block)
            {
                perMinuteMax.push_back (minuteMax); minuteMax = 0;
                rssMax = std::max (rssMax, rssKb());
            }
        }
        const long rss1 = rssKb();
        auto pm = perMinuteMax; std::sort (pm.begin(), pm.end());
        Stats s;
        s.avg = sum / (double) blocks; s.mx = worst; s.p95 = pm.empty() ? 0 : pm[pm.size() / 2]; s.p99 = pm.empty() ? 0 : pm[(size_t) (0.95 * (double) (pm.size() - 1))];
        s.cpu = 100.0 * sum / (1.0e6 * minutes * 60.0); s.worstShare = worst / (1.0e6 * block / sr);
        s.allocs = gAllocs.load() - allocs0; s.finite = nonfinite == 0;
        row ("longrun", sr, block, 2, true, "automated", 1, 1, s,
             "minutes=" + std::to_string ((int) minutes) + " rss_kb_start=" + std::to_string (rss0) + " rss_kb_end=" + std::to_string (rss1)
                 + " rss_kb_max=" + std::to_string (rssMax) + " p95_us/p99_us columns = median/p95 of the per-minute max");
    }
    else if (mode == "instances")
    {
        const double sr = 48000.0;
        const int block = 128;
        for (int inst : { 1, 4, 8, 16 })
            for (int thr : { 1, 4 })
            {
                if (thr > inst) continue;
                auto x = vowelMelody (sr, seconds);
                std::vector<std::unique_ptr<TuneEngine>> eng;
                std::vector<std::vector<float>> bufL ((size_t) inst, std::vector<float> ((size_t) block)), bufR = bufL;
                for (int k = 0; k < inst; ++k) { eng.push_back (std::make_unique<TuneEngine>()); eng.back()->prepare (sr, true); eng.back()->setParams (setting (k % 2 ? "heavy" : "default")); }
                std::vector<double> t;
                const long a0 = gAllocs.load();
                for (size_t pos = 0; pos + (size_t) block <= x.size(); pos += (size_t) block)
                {
                    auto work = [&] (int from, int to)
                    {
                        gCounting = true;
                        for (int k = from; k < to; ++k)
                        {
                            for (int i = 0; i < block; ++i) { bufL[(size_t) k][(size_t) i] = x[pos + (size_t) i]; bufR[(size_t) k][(size_t) i] = 0.8f * x[pos + (size_t) i]; }
                            float* ch[2] = { bufL[(size_t) k].data(), bufR[(size_t) k].data() };
                            eng[(size_t) k]->process (ch, 2, block);
                            TuneEngine::DisplayPoint d;
                            while (eng[(size_t) k]->display.pop (d)) {}
                        }
                        gCounting = false;
                    };
                    const auto c0 = std::chrono::steady_clock::now();
                    if (thr == 1) work (0, inst);
                    else
                    {
                        std::vector<std::thread> th;
                        const int per = (inst + thr - 1) / thr;
                        for (int k = 0; k < thr; ++k) th.emplace_back (work, k * per, std::min (inst, (k + 1) * per));
                        for (auto& h : th) h.join();
                    }
                    t.push_back (std::chrono::duration<double, std::micro> (std::chrono::steady_clock::now() - c0).count());
                }
                Stats s;
                auto sorted = t; std::sort (sorted.begin(), sorted.end());
                double sum = 0; for (double v : t) sum += v;
                s.avg = sum / (double) t.size(); s.p95 = sorted[(size_t) (0.95 * (double) (sorted.size() - 1))];
                s.p99 = sorted[(size_t) (0.99 * (double) (sorted.size() - 1))]; s.mx = sorted.back();
                s.cpu = 100.0 * sum / (1.0e6 * seconds); s.worstShare = s.mx / (1.0e6 * block / sr);
                s.allocs = gAllocs.load() - a0;                // thread start-up allocates (not the engines): see note
                row ("instances", sr, block, 2, true, "default/heavy", inst, thr, s,
                     thr > 1 ? "allocs include std::thread start-up per block (harness; not the engine)" : "");
            }
    }
    else if (mode == "denormal")
    {
        const double sr = 48000.0;
        for (int ftz = 0; ftz < 2; ++ftz)
        {
#if defined(__SSE__) || defined(_M_X64)
            _MM_SET_FLUSH_ZERO_MODE (ftz ? _MM_FLUSH_ZERO_ON : _MM_FLUSH_ZERO_OFF);
            _MM_SET_DENORMALS_ZERO_MODE (ftz ? _MM_DENORMALS_ZERO_ON : _MM_DENORMALS_ZERO_OFF);
#endif
            auto e = std::make_unique<TuneEngine>();
            e->prepare (sr, true);
            e->setParams (setting ("heavy"));
            auto x = vowelMelody (sr, 3.0);
            x.resize ((size_t) (13.0 * sr), 0.0f);                // 10 s of digital silence after the voice
            std::vector<double> loud, quiet;
            x.resize (x.size() - x.size() % 256, 0.0f);         // whole blocks only (1.2.0 ran past the end: heap overflow, ASan)
            for (size_t pos = 0; pos < x.size(); pos += 256)
            {
                float* ch[2] = { x.data() + pos, nullptr };
                const auto c0 = std::chrono::steady_clock::now();
                e->process (ch, 1, 256);
                const double us = std::chrono::duration<double, std::micro> (std::chrono::steady_clock::now() - c0).count();
                (pos < (size_t) (3.0 * sr) ? loud : quiet).push_back (us);
                TuneEngine::DisplayPoint d;
                while (e->display.pop (d)) {}
            }
            auto avg = [] (const std::vector<double>& v) { double s = 0; for (double a : v) s += a; return s / (double) v.size(); };
            Stats s; s.avg = avg (quiet); s.mx = *std::max_element (quiet.begin(), quiet.end()); s.p95 = avg (loud);
            s.cpu = 0; s.worstShare = s.mx / (1.0e6 * 256 / sr);
            row ("denormal", sr, 256, 1, true, "heavy", 1, 1, s, std::string (ftz ? "FTZ/DAZ on" : "FTZ/DAZ off")
                     + ": avg_us = silence blocks; p95_us column = average of the voiced blocks");
        }
    }
    std::fclose (out);
    return 0;
}
