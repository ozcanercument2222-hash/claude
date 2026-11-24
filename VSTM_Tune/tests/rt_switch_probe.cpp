//==============================================================================
//  VSTM Tune - REALTIME <-> HQ switch integrity probe (JUCE gerektirmez; MEASURED)
//
//    rt_switch_probe OUT_DIR          (then: python3 tests/rt_switch_eval.py OUT_DIR)
//
//  Switches the latency mode while audio runs (what processBlock does at a block boundary) and checks
//  that the content stays continuous: no input repeated or skipped, no click, the delay ends exactly
//  on the reported latency, the processed pitch does not jump. Works with every engine version that
//  has prepare / setParams / setRealtime / process / getLatencySamples (1.0 - 1.2), so the same
//  program measures the old and the new switch.
//
//  Signals : impulses (every 37 ms, alternating sign), sine 440 Hz, white noise, sung vowel melody,
//            plosive bursts + vowel, a sustained note
//  Paths   : dry (MIX 0), bypass, transparent wet (correction 0, MIX 100), hard tune (MIX 100), MIX 50
//  Switches: RT->HQ and HQ->RT at 1.0 s; rapid toggles every 10 / 20 / 50 / 100 / 500 ms; block sizes
//            32 / 256 / 1024 / random 1-2048; a switch inside a sustained note
//  Writes OUT_DIR/in_<signal>.wav, OUT_DIR/<case>.wav and OUT_DIR/cases.jsonl; rt_switch_eval.py
//  measures the delay track (local delay of the output against the input, FFT cross-correlation in
//  20 ms windows every 2 ms: a 20 ms content jump shows as a ~960-sample step at 48 kHz), its
//  continuity and monotonicity, and the final alignment against the reported latency.
//  Measured here (cheap):
//    impulses_in / impulses_out / impulses_unmatched   (impulse signal, dry paths: every input impulse
//                   must come out exactly once, in order)
//    max_step / max_step_steady   largest sample-to-sample step near the switch vs. in steady state
//    pitch_dev_ct   (hard tune) largest deviation of the output pitch from the target note in 10 ms
//                   windows during the switch
//==============================================================================
#include "dsp/TuneEngine.h"

#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <random>
#include <string>
#include <vector>

using namespace vstm;

namespace
{
std::string OUT;
bool WRITE_WAV = true;

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

std::vector<float> vowel (double sr, double secs, const std::function<double (double)>& f0At, float level = 0.3f)
{
    const int n = (int) (sr * secs);
    std::vector<float> out ((size_t) n);
    Resonator f1, f2, f3;
    f1.set (sr, 730, 90); f2.set (sr, 1090, 110); f3.set (sr, 2440, 170);
    double ph = 0.0, prev = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double f0 = f0At (i / sr);
        ph += f0 / sr;
        double src = 0.0;
        const int H = std::min (40, (int) (0.45 * sr / std::max (40.0, f0)));
        for (int h = 1; h <= H; ++h)
            src += std::sin (2.0 * kPi * h * ph) / (double) (h * h) * (h == 1 ? 1.0 : 1.6);
        const double p = src - prev;
        prev = src;
        out[(size_t) i] = (float) (f1.process (p) + 0.7 * f2.process (p) + 0.35 * f3.process (p));
    }
    float pk = 1e-9f;
    for (float v : out) pk = std::max (pk, std::abs (v));
    for (auto& v : out) v *= level / pk;
    return out;
}

void writeWav (const std::string& name, const std::vector<float>& x, double sr)
{
    if (! WRITE_WAV) return;
    FILE* f = std::fopen ((OUT + "/" + name).c_str(), "wb");
    if (! f) return;
    const uint32_t n = (uint32_t) x.size(), bytes = n * 4;
    auto w32 = [&] (uint32_t v) { std::fwrite (&v, 4, 1, f); };
    auto w16 = [&] (uint16_t v) { std::fwrite (&v, 2, 1, f); };
    std::fwrite ("RIFF", 1, 4, f); w32 (36 + bytes); std::fwrite ("WAVE", 1, 4, f);
    std::fwrite ("fmt ", 1, 4, f); w32 (16); w16 (3); w16 (1); w32 ((uint32_t) sr); w32 ((uint32_t) sr * 4); w16 (4); w16 (32);
    std::fwrite ("data", 1, 4, f); w32 (bytes);
    std::fwrite (x.data(), 4, n, f);
    std::fclose (f);
}

double measureHz (const std::vector<float>& x, double sr, long start, int len, double expectHz)
{
    const double T = sr / expectHz;
    const int lo = std::max (2, (int) (T * 0.8)), hi = (int) (T * 1.25) + 2;
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
    for (int lag = lo; lag <= hi; ++lag) if (r[(size_t) lag] > r[(size_t) best]) best = lag;
    const double a = r[(size_t) best - 1], b = r[(size_t) best], c = r[(size_t) best + 1];
    const double den = a - 2 * b + c;
    return sr / (best + (std::abs (den) > 1e-12 ? 0.5 * (a - c) / den : 0.0));
}

enum Path { kDry, kBypass, kWetTransparent, kHard, kMix50 };
const char* pathName (Path p)
{
    switch (p) { case kDry: return "dry_mix0"; case kBypass: return "bypass"; case kWetTransparent: return "wet_transparent";
                 case kHard: return "hard_tune"; default: return "mix50"; }
}

TuneEngine::Params paramsFor (Path p)
{
    TuneEngine::Params q;
    q.key = 0; q.scale = kChromatic; q.robotism = 0.0f; q.retuneMs = 0.0f; q.humanize = 0.0f;
    if (p == kDry) q.mix = 0.0f;
    if (p == kBypass) q.active = false;
    if (p == kWetTransparent) q.amount = 0.0f;
    if (p == kMix50) q.mix = 0.5f;
    return q;
}

struct Case
{
    std::string signal;
    Path path;
    bool startRealtime = true;
    std::vector<double> switches;      // seconds; each toggles the mode
    int block = 256;                   // <= 0: random 1..2048
};

void runCase (const Case& cs, const std::vector<float>& x, double sr, bool impulses, double targetHz)
{
    auto e = std::make_unique<TuneEngine>();
    bool rt = cs.startRealtime;
    e->prepare (sr, rt);
    const auto p = paramsFor (cs.path);
    e->setParams (p);
    std::vector<float> y = x;
    std::mt19937 rng (11);
    size_t pos = 0, nextSwitch = 0;
    while (pos < x.size())
    {
        int n = cs.block > 0 ? cs.block : (int) (1 + rng() % 2048);
        n = (int) std::min ((size_t) n, x.size() - pos);
        // a switch happens at the block boundary at or after its time (as in processBlock)
        while (nextSwitch < cs.switches.size() && (double) pos >= cs.switches[nextSwitch] * sr)
        {
            rt = ! rt;
            e->setRealtime (rt);
            ++nextSwitch;
        }
        float* ch[2] = { y.data() + pos, nullptr };
        e->setParams (p);
        e->process (ch, 1, n);
        TuneEngine::DisplayPoint d;
        while (e->display.pop (d)) {}
        pos += (size_t) n;
    }
    const int latEnd = e->getLatencySamples();
    const int latRT = TuneEngine::latencyFor (sr, true), latHQ = TuneEngine::latencyFor (sr, false);

    const int lo = std::min (latRT, latHQ) - 40, hi = std::max (latRT, latHQ) + 40;

    // impulses: every input impulse once in the output, in order (dry paths)
    int nIn = 0, nOut = 0, unmatched = 0;
    if (impulses)
    {
        std::vector<long> ti, to;
        for (size_t i = 1; i + (size_t) hi + 2 < x.size(); ++i)
            if (std::abs (x[i]) > 0.25f) { ti.push_back ((long) i); i += (size_t) (0.004 * sr); }
        for (size_t i = 1; i + 1 < y.size(); ++i)
            if (std::abs (y[i]) > 0.25f * 0.5f)
            {
                size_t b = i;                              // the strongest sample of this cluster
                for (size_t k = i; k < std::min (y.size(), i + (size_t) (0.0015 * sr)); ++k)
                    if (std::abs (y[k]) > std::abs (y[b])) b = k;
                to.push_back ((long) b);
                i = b + (size_t) (0.004 * sr);
            }
        nIn = (int) ti.size(); nOut = (int) to.size();
        // match in order with delays inside the possible range
        size_t j = 0;
        for (long a : ti)
        {
            while (j < to.size() && to[j] - a < lo - 2) { ++unmatched; ++j; }     // an output impulse with no source
            if (j < to.size() && to[j] - a <= hi + 2) ++j;
            else ++unmatched;
        }
        unmatched += (int) (to.size() - j);
    }

    // clicks: largest sample step near the switches vs. steady state
    double stepNear = 0.0, stepSteady = 0.0;
    for (size_t i = 1; i < y.size(); ++i)
    {
        const double s = std::abs ((double) y[i] - y[i - 1]);
        bool near = false;
        for (double sw : cs.switches)
            near = near || std::abs ((double) i / sr - sw) < 0.2 || ((double) i / sr > sw && (double) i / sr < sw + 2.3);
        if (cs.switches.empty() || near) stepNear = std::max (stepNear, s);
        if (! near && i > (size_t) (0.5 * sr)) stepSteady = std::max (stepSteady, s);
    }

    // processed pitch during the switch (hard tune on the vowel)
    double pitchDev = -1.0;
    if (targetHz > 0.0 && cs.path == kHard && ! cs.switches.empty())
    {
        pitchDev = 0.0;
        const double sw = cs.switches.front();
        for (double t = sw - 0.1; t < sw + 2.4 && (long) (t * sr) + 2000 < (long) y.size(); t += 0.01)
        {
            const double hz = measureHz (y, sr, (long) (t * sr), (int) (0.01 * sr), targetHz);
            if (hz > 0) pitchDev = std::max (pitchDev, std::abs (1200.0 * std::log2 (hz / targetHz)));
        }
    }

    std::string sws;
    for (double sw : cs.switches) sws += (sws.empty() ? "" : ",") + std::to_string (sw).substr (0, 5);
    const std::string file = cs.signal + "_" + pathName (cs.path) + "_" + (cs.startRealtime ? "RT" : "HQ") + "_"
                           + std::to_string (cs.switches.size()) + "sw_b" + std::to_string (cs.block) + ".wav";
    writeWav (file, y, sr);
    FILE* jl = std::fopen ((OUT + "/cases.jsonl").c_str(), "a");
    if (jl)
    {
        std::fprintf (jl, "{\"signal\":\"%s\",\"path\":\"%s\",\"start\":\"%s\",\"switches\":[%s],\"block\":%d,"
                     "\"latency_rt\":%d,\"latency_hq\":%d,\"latency_reported_end\":%d,\"file\":\"%s\",\"input\":\"in_%s.wav\","
                     "\"impulses_in\":%d,\"impulses_out\":%d,\"impulses_unmatched\":%d,"
                     "\"max_step_near\":%.4f,\"max_step_steady\":%.4f,\"pitch_dev_ct\":%.2f,\"finite\":%s}\n",
                     cs.signal.c_str(), pathName (cs.path), cs.startRealtime ? "RT" : "HQ", sws.c_str(), cs.block,
                     latRT, latHQ, latEnd, file.c_str(), cs.signal.c_str(), nIn, nOut, unmatched, stepNear, stepSteady, pitchDev,
                     std::all_of (y.begin(), y.end(), [] (float v) { return std::isfinite (v); }) ? "true" : "false");
        std::fclose (jl);
    }
}
} // namespace

int main (int argc, char** argv)
{
    if (argc < 2) { std::fprintf (stderr, "usage: rt_switch_probe OUT_DIR\n"); return 2; }
    OUT = argv[1];
    const double sr = 48000.0;
    const double secs = 6.5;                                 // every ramp (<= 2 s, last switch at 3 s) completes
    const int n = (int) (sr * secs);
    std::mt19937 rng (3);
    std::normal_distribution<double> g (0.0, 1.0);

    std::vector<float> imp ((size_t) n, 0.0f), sine ((size_t) n), noise ((size_t) n);
    // irregular spacing (6 - 9 ms): dense enough to track the delay through a glide, aperiodic so the
    // detector keeps it unvoiced (time-aligned grains, no pitch correction moves the impulses)
    {
        std::uniform_int_distribution<int> gap ((int) (0.006 * sr), (int) (0.009 * sr));
        for (int i = (int) (0.05 * sr), k = 0; i < n; i += gap (rng), ++k) imp[(size_t) i] = (k % 2) ? -0.5f : 0.5f;
    }
    for (int i = 0; i < n; ++i) sine[(size_t) i] = (float) (0.3 * std::sin (2.0 * kPi * 440.0 * i / sr));
    for (int i = 0; i < n; ++i) noise[(size_t) i] = (float) (0.1 * g (rng));
    const std::vector<int> mel { 57, 59, 60, 62, 64, 62, 60, 59, 57, 60, 64, 62 };
    auto melody = vowel (sr, secs, [&] (double t) {
        const int i = std::min ((int) (t / 0.375), (int) mel.size() - 1);
        return midiToHz (mel[(size_t) i] + 0.3 + 0.15 * std::sin (2 * kPi * 5.5 * t)); });
    auto sustained = vowel (sr, secs, [] (double) { return midiToHz (57.3); });
    std::vector<float> plosive = vowel (sr, secs, [] (double t) { return midiToHz (55.0 + 2.0 * std::floor (t / 0.5)); });
    for (int k = 0; k < 9; ++k)                               // a 15 ms burst before every new syllable
    {
        const int a = (int) (k * 0.5 * sr);
        for (int i = a; i < std::min (n, a + (int) (0.015 * sr)); ++i)
            plosive[(size_t) i] = (float) (0.25 * g (rng) * std::exp (-(i - a) / (0.004 * sr)));
        for (int i = a + (int) (0.015 * sr); i < std::min (n, a + (int) (0.04 * sr)); ++i)
            plosive[(size_t) i] *= (float) ((i - a - 0.015 * sr) / (0.025 * sr));
    }

    {
        FILE* jl = std::fopen ((OUT + "/cases.jsonl").c_str(), "w");     // start a new list
        if (jl) std::fclose (jl);
    }
    struct Sig { const char* name; const std::vector<float>* x; bool impulses; double hz; };
    const Sig sigs[] = { { "impulses", &imp, true, 0.0 }, { "sine", &sine, false, 0.0 }, { "noise", &noise, false, 0.0 },
                         { "vowel_melody", &melody, false, 0.0 }, { "plosives", &plosive, false, 0.0 },
                         { "sustained", &sustained, false, midiToHz (57.0) } };
    for (const auto& s : sigs)
        writeWav (std::string ("in_") + s.name + ".wav", *s.x, sr);
    const Path paths[] = { kDry, kBypass, kWetTransparent, kHard, kMix50 };
    for (const auto& s : sigs)
        for (Path pth : paths)
            for (bool startRt : { true, false })
                runCase ({ s.name, pth, startRt, { 1.0 }, 256 }, *s.x, sr, s.impulses, s.hz);
    // rapid toggles and block sizes (noise and the melody, dry and wet)
    for (const auto& s : { sigs[2], sigs[3], sigs[0] })
        for (Path pth : { kDry, kHard, kWetTransparent })
        {
            for (double every : { 0.010, 0.020, 0.050, 0.100, 0.500 })
            {
                std::vector<double> sw;
                for (double t = 1.0; t < 3.0; t += every) sw.push_back (t);
                runCase ({ s.name, pth, true, sw, 256 }, *s.x, sr, s.impulses, s.hz);
            }
            for (int b : { 32, 1024, 0 })
                runCase ({ s.name, pth, true, { 1.0, 3.0 }, b }, *s.x, sr, s.impulses, s.hz);
        }
    return 0;
}
