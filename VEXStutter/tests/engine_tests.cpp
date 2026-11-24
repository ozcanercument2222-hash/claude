//==============================================================================
//  VEX Stutter - DSP engine tests (JUCE-free; evidence: MEASURED). Not part of the plug-in.
//
//    g++ -O2 -std=c++17 -ISource tests/engine_tests.cpp -o engine_tests && ./engine_tests
//    (also built by CMake as VEXStutter_EngineTests; works with -fsanitize=address,undefined)
//==============================================================================
#include "DSP/StutterEngine.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <random>
#include <vector>

using namespace vex;
using namespace vex::dsp;

namespace
{
int failures = 0, checks = 0;
#define CHECK(cond, ...) do { const bool ok_ = (cond); ++checks; std::printf ("  [%s] ", ok_ ? " OK " : "FAIL"); \
                              std::printf (__VA_ARGS__); std::printf ("\n"); if (! ok_) ++failures; } while (0)

constexpr double kRate = 48000.0;
constexpr double kPi = 3.14159265358979;

/** A sung-vowel-like test signal: harmonic tone with a slow pitch drift and a syllable envelope. */
std::vector<float> voice (double seconds, double rate, unsigned seed = 1)
{
    std::vector<float> x ((size_t) (seconds * rate));
    std::mt19937 rng (seed);
    std::normal_distribution<float> n (0.0f, 1.0f);
    double ph = 0.0;
    for (size_t i = 0; i < x.size(); ++i)
    {
        const double t = (double) i / rate;
        const double f0 = 196.0 * std::pow (2.0, 0.25 * std::sin (2.0 * kPi * 0.7 * t));
        ph += f0 / rate;
        double s = 0.0;
        for (int h = 1; h <= 8; ++h)
            s += std::sin (2.0 * kPi * h * ph) / h;
        const double syl = 0.55 + 0.45 * std::sin (2.0 * kPi * 2.3 * t);
        x[i] = (float) (0.25 * syl * s) + 0.002f * n (rng);
    }
    return x;
}

struct Render
{
    std::vector<float> l, r;
};

using PerBlock = std::function<void (int64_t, EngineParams&, Transport&)>;

/** Renders a stereo signal (right = 0.8 x left) through a fresh engine; the host plays from ppq 0 at
    `bpm` unless `playing` is false. `perBlock` may change parameters / transport before each block. */
Render render (const std::vector<float>& in, EngineParams p, int block = 256, double bpm = 120.0, bool playing = true,
               double rate = kRate, const PerBlock& perBlock = nullptr, bool rightCopy = false)
{
    StutterEngine e;
    e.prepare (rate, block);
    Render out { in, in };
    if (! rightCopy)
        for (auto& v : out.r) v *= 0.8f;
    Transport t;
    t.hasTempo = true;
    t.bpm = bpm;
    t.playing = playing;
    t.hasPosition = true;
    for (int64_t pos = 0; pos < (int64_t) in.size(); pos += block)
    {
        const int n = (int) std::min<int64_t> (block, (int64_t) in.size() - pos);
        t.ppq = (double) pos * bpm / (60.0 * rate);
        if (perBlock) perBlock (pos, p, t);
        float* ch[2] = { out.l.data() + pos, out.r.data() + pos };
        e.process (ch, 2, n, p, t);
    }
    return out;
}

/** Largest second difference |x[n] - 2x[n-1] + x[n-2]|: a fade gives a small value, a cut a large one. */
double maxCurvature (const std::vector<float>& x, size_t* where = nullptr)
{
    double m = 0.0;
    for (size_t i = 2; i < x.size(); ++i)
    {
        const double d = std::abs ((double) x[i] - 2.0 * x[i - 1] + x[i - 2]);
        if (d > m) { m = d; if (where) *where = i; }
    }
    return m;
}

double rms (const std::vector<float>& x, size_t a, size_t b)
{
    double s = 0.0;
    for (size_t i = a; i < b && i < x.size(); ++i) s += (double) x[i] * x[i];
    return std::sqrt (s / (double) std::max<size_t> (1, b - a));
}

double rmsDiff (const std::vector<float>& x, const std::vector<float>& y, size_t a, size_t b)
{
    double s = 0.0;
    for (size_t i = a; i < b && i < x.size(); ++i) s += ((double) x[i] - y[i]) * ((double) x[i] - y[i]);
    return std::sqrt (s / (double) std::max<size_t> (1, b - a));
}

/** Normalised correlation of out[a..a+n) with in[b..b+n). */
double corr (const std::vector<float>& out, size_t a, const std::vector<float>& in, size_t b, size_t n)
{
    double xy = 0, xx = 0, yy = 0;
    for (size_t i = 0; i < n; ++i)
    {
        const double x = out[a + i], y = in[b + i];
        xy += x * y; xx += x * x; yy += y * y;
    }
    return xy / std::sqrt (xx * yy + 1e-30);
}

bool allFinite (const Render& r, float limit)
{
    for (size_t i = 0; i < r.l.size(); ++i)
        if (! std::isfinite (r.l[i]) || ! std::isfinite (r.r[i]) || std::abs (r.l[i]) > limit || std::abs (r.r[i]) > limit)
            return false;
    return true;
}

bool same (const std::vector<float>& a, const std::vector<float>& b)
{
    return a.size() == b.size() && std::memcmp (a.data(), b.data(), a.size() * sizeof (float)) == 0;
}
}

int main()
{
    const auto vox = voice (6.0, kRate);
    const size_t slice8 = (size_t) (0.25 * kRate);      // 1/8 at 120 BPM
    const size_t guard = 600;                           // skip the edge fades when correlating

    //--------------------------------------------------------------------------
    std::printf ("\n=== 1) Clean paths ===\n");
    {
        bool exact = true;
        for (int m = 0; m < choices::numModes; ++m)
            for (int s = 0; s < choices::numStyles; ++s)
            {
                EngineParams p;
                p.mode = (Mode) m;
                p.style = (Style) s;
                p.amount = 0.0f;
                p.length = 1.0f;
                p.reverbMix = 0.3f;     // FX on, but nothing reaches it
                p.delayMix = 0.3f;
                exact = exact && same (render (vox, p).l, vox);
            }
        CHECK (exact, "Amount 0 %%: output = input bit for bit, in every mode and style (FX on)");

        EngineParams p;
        p.amount = 1.0f;
        p.length = 1.0f;
        p.bypass = true;
        CHECK (same (render (vox, p).l, vox), "Bypass on: output = input bit for bit");

        p.bypass = false;
        p.mix = 0.0f;
        CHECK (same (render (vox, p).l, vox), "Mix 0 %%: output = input bit for bit");
    }

    //--------------------------------------------------------------------------
    std::printf ("\n=== 2) Stutter, Repeat, Reverse play the captured audio on the grid ===\n");
    {
        EngineParams p;                       // 1/8 at 120 BPM, phrase = 1 bar = 8 slices
        p.amount = 1.0f;
        p.length = 1.0f;
        p.smoothness = 0.0f;
        const auto r = render (vox, p);
        double worst = 1.0;
        for (int k = 1; k < 8; ++k)
            worst = std::min (worst, corr (r.l, k * slice8 + guard, vox, guard, slice8 - 2 * guard));
        CHECK (worst > 0.999, "Stutter: slices 1-7 of the first bar repeat slice 0 (worst correlation %.5f)", worst);
        CHECK (corr (r.l, 8 * slice8 + guard, vox, 8 * slice8 + guard, slice8 - 2 * guard) > 0.999
               && corr (r.l, 9 * slice8 + guard, vox, 8 * slice8 + guard, slice8 - 2 * guard) > 0.999,
               "next bar: slice 0 is live again (the new capture) and slice 1 repeats it");

        p.length = 0.5f;
        const auto r2 = render (vox, p);
        CHECK (corr (r2.l, 3 * slice8 + guard, vox, guard, slice8 - 2 * guard) > 0.999
               && corr (r2.l, 5 * slice8 + guard, vox, 5 * slice8 + guard, slice8 - 2 * guard) > 0.999,
               "Length 50 %%: slices 1-3 repeat, slices 4-7 pass live");

        p.length = 0.0f;
        const auto r0 = render (vox, p);
        CHECK (corr (r0.l, 1 * slice8 + guard, vox, guard, slice8 - 2 * guard) > 0.999
               && same (std::vector<float> (r0.l.begin() + 2 * (long) slice8 + 100, r0.l.begin() + 8 * (long) slice8),
                        std::vector<float> (vox.begin() + 2 * (long) slice8 + 100, vox.begin() + 8 * (long) slice8)),
               "Length 0 %%: one repeat per bar (slice 1), the rest is the input bit for bit");

        p.length = 1.0f;
        p.amount = 0.5f;
        const auto r3 = render (vox, p);
        int repeats = 0;
        for (int k = 1; k < 8; ++k)
            repeats += corr (r3.l, k * slice8 + guard, vox, guard, slice8 - 2 * guard) > 0.999 ? 1 : 0;
        CHECK (repeats == 4, "Amount 50 %%: 4 of the 7 candidate slices repeat (%d)", repeats);

        p.amount = 0.01f;
        const auto r4 = render (vox, p);
        repeats = 0;
        for (int k = 1; k < 8; ++k)
            repeats += corr (r4.l, k * slice8 + guard, vox, guard, slice8 - 2 * guard) > 0.999 ? 1 : 0;
        CHECK (repeats == 1, "Amount 1 %%: at least one repeat per bar (%d)", repeats);

        p.amount = 1.0f;
        p.mode = Mode::repeat;
        const auto r5 = render (vox, p);       // 1/8: a new capture every beat (2 slices)
        CHECK (corr (r5.l, 1 * slice8 + guard, vox, guard, slice8 - 2 * guard) > 0.999
               && corr (r5.l, 2 * slice8 + guard, vox, 2 * slice8 + guard, slice8 - 2 * guard) > 0.999
               && corr (r5.l, 3 * slice8 + guard, vox, 2 * slice8 + guard, slice8 - 2 * guard) > 0.999,
               "Repeat: each beat is captured live and repeated once (1/8)");

        p.mode = Mode::reverse;
        const auto r6 = render (vox, p);
        std::vector<float> rev (vox.begin(), vox.begin() + (long) slice8);
        std::reverse (rev.begin(), rev.end());
        CHECK (corr (r6.l, slice8 + guard, rev, guard, slice8 - 2 * guard) > 0.999, "Reverse: the repeat is the captured slice backwards");

        p.mode = Mode::glitch;
        p.intensity = 1.0f;
        const auto r7 = render (vox, p);
        int varied = 0;
        for (int k = 1; k < 16; ++k)
            varied += corr (r7.l, k * slice8 + guard, vox, (k / 8) * 8 * slice8 + guard, slice8 - 2 * guard) < 0.99 ? 1 : 0;
        CHECK (varied >= 10, "Glitch at Intensity 100 %%: %d of 14 repeats are varied", varied);
    }

    //--------------------------------------------------------------------------
    std::printf ("\n=== 2b) What the UI is told (EngineStatus) ===\n");
    {
        StutterEngine e;
        e.prepare (kRate, 256);
        EngineParams p;
        p.amount = 0.5f;
        p.length = 1.0f;
        Transport t;
        t.hasTempo = true; t.bpm = 120.0; t.playing = true; t.hasPosition = true;
        std::vector<float> l (vox.begin(), vox.begin() + 24000), r = l;
        for (size_t pos = 0; pos < l.size(); pos += 256)
        {
            t.ppq = (double) pos * 120.0 / (60.0 * kRate);
            float* ch[2] = { l.data() + pos, r.data() + pos };
            e.process (ch, 2, (int) std::min<size_t> (256, l.size() - pos), p, t);
        }
        const uint64_t rep = e.status.repeatMask[0].load(), cap = e.status.captureMask[0].load();
        CHECK (rep == ((1u << 2) | (1u << 4) | (1u << 6) | (1u << 7)) && cap == 1u && e.status.steps.load() == 8 && e.status.step.load() == 1,
               "Amount 50 %%, 1/8: plan = repeats at slices 2, 4, 6, 7, capture slice 0, 8 steps, now at step 1 (mask %llx / %llx)",
               (unsigned long long) rep, (unsigned long long) cap);
        CHECK (e.status.syncState.load() == EngineStatus::hostLocked && std::abs (e.status.sliceSeconds.load() - 0.25f) < 1e-6f
               && e.status.sliceStartFrame.load() == 12000, "sync state, slice length and slice start frame are published");
        p.mode = Mode::gate;
        for (size_t pos = 0; pos < l.size(); pos += 256)
        {
            t.ppq = (double) (pos + 24000) * 120.0 / (60.0 * kRate);
            float* ch[2] = { l.data() + pos, r.data() + pos };
            e.process (ch, 2, (int) std::min<size_t> (256, l.size() - pos), p, t);
        }
        CHECK (e.status.gating.load() && e.status.repeatMask[0].load() == 0xffu, "Gate: every slice of the phrase is gated");
    }

    //--------------------------------------------------------------------------
    std::printf ("\n=== 3) Gate ===\n");
    {
        EngineParams p;
        p.mode = Mode::gate;
        p.amount = 1.0f;
        p.length = 0.5f;            // duty 52.5 %
        p.smoothness = 0.0f;
        const auto r = render (vox, p);
        const double open = rms (r.l, 2 * slice8 + 1000, 2 * slice8 + slice8 / 2 - 1000);
        const double closed = rms (r.l, 2 * slice8 + slice8 * 6 / 10, 3 * slice8 - 1000);
        CHECK (open > 0.05 && closed < 1e-6, "Gate 100 %%: open part passes (rms %.3f), closed part silent (rms %.2g)", open, closed);
        CHECK (same (std::vector<float> (r.l.begin() + 2 * (long) slice8 + 1000, r.l.begin() + 2 * (long) slice8 + 5000),
                     std::vector<float> (vox.begin() + 2 * (long) slice8 + 1000, vox.begin() + 2 * (long) slice8 + 5000)),
               "Gate open part is the input bit for bit");
        p.amount = 0.5f;
        const auto r2 = render (vox, p);
        const double half = rms (r2.l, 2 * slice8 + slice8 * 6 / 10, 3 * slice8 - 1000) / rms (vox, 2 * slice8 + slice8 * 6 / 10, 3 * slice8 - 1000);
        CHECK (std::abs (half - 0.5) < 0.01, "Gate depth 50 %%: closed part at %.3f of the input (-6 dB)", half);

        p.amount = 1.0f;
        p.reverbMix = 0.5f;
        const auto rf = render (vox, p);
        CHECK (rms (rf.l, 2 * slice8 + slice8 * 6 / 10, 3 * slice8 - 1000) > 1e-3,
               "Gate + reverb: the gated pulses ring into the closed part (rms %.4f)", rms (rf.l, 2 * slice8 + slice8 * 6 / 10, 3 * slice8 - 1000));
    }

    //--------------------------------------------------------------------------
    std::printf ("\n=== 4) Clicks ===\n");
    {
        // 0.3 + 0.2 sin: never near 0, so any cut makes a jump of at least 0.1. A 0.5 ms equal-power
        // crossfade (the protective minimum) gives a second difference near 0.006, a cut 0.1 or more.
        std::vector<float> tone ((size_t) (4.0 * kRate));
        for (size_t i = 0; i < tone.size(); ++i) tone[i] = 0.3f + 0.2f * (float) std::sin (2.0 * kPi * 110.0 * (double) i / kRate);
        const double limit = 0.02;
        double worst = 0.0;
        int combos = 0;
        const char* worstName = "";
        static char nameBuf[128];
        for (int m = 0; m < choices::numModes; ++m)
            for (int sh = 0; sh < choices::numShapes; ++sh)
                for (int st = 0; st < choices::numStyles; ++st)
                    for (float sm : { 0.0f, 1.0f })
                    {
                        if (st == (int) Style::digital) continue;                     // deliberately stepped
                        EngineParams p;
                        p.mode = (Mode) m; p.shape = (Shape) sh; p.style = (Style) st; p.smoothness = sm;
                        p.amount = 1.0f; p.length = 0.75f; p.rateIndex = 10;           // 1/16
                        p.intensity = 1.0f;
                        const double c = maxCurvature (render (tone, p).l);
                        if (c > worst)
                        {
                            worst = c;
                            std::snprintf (nameBuf, sizeof (nameBuf), "%s / %s / %s / smoothness %.0f %%", choices::modeNames[m],
                                           choices::shapeNames[sh], choices::styleNames[st], sm * 100.0f);
                            worstName = nameBuf;
                        }
                        ++combos;
                    }
        CHECK (worst < limit, "largest second difference %.4f (< %.2f) over %d mode / shape / style / smoothness combinations (worst: %s)",
               worst, limit, combos, worstName);

        EngineParams p;
        p.amount = 1.0f;
        p.length = 1.0f;
        p.smoothness = 0.0f;
        const auto r = render (tone, p, 256, 120.0, true, kRate, [] (int64_t pos, EngineParams& q, Transport&)
        {
            const int step = (int) (pos / 9000);
            q.rateIndex = 4 + step % 9;
            q.mode = (Mode) (step % 5);
            q.amount = (float) (step % 3) / 2.0f;
        });
        CHECK (maxCurvature (r.l) < limit, "Rate, Mode and Amount changed every ~190 ms: %.4f", maxCurvature (r.l));

        const auto rb = render (tone, p, 256, 120.0, true, kRate, [] (int64_t pos, EngineParams& q, Transport&) { q.bypass = (pos / 12000) % 2 == 1; });
        CHECK (maxCurvature (rb.l) < limit, "Bypass toggled every 250 ms while stuttering: %.4f", maxCurvature (rb.l));

        const auto rj = render (tone, p, 256, 120.0, true, kRate, [] (int64_t pos, EngineParams&, Transport& t) { if ((pos / 256) % 37 == 36) t.ppq += 1.37; });
        CHECK (maxCurvature (rj.l) < limit, "host jumps (loop / seek) every 37 blocks: %.4f", maxCurvature (rj.l));

        const auto rs = render (tone, p, 256, 120.0, true, kRate, [] (int64_t pos, EngineParams&, Transport& t) { t.playing = (pos / 30000) % 2 == 0; });
        CHECK (maxCurvature (rs.l) < limit, "transport started and stopped every 0.6 s: %.4f", maxCurvature (rs.l));

        const auto rm = render (tone, p, 256, 120.0, true, kRate, [] (int64_t pos, EngineParams& q, Transport&)
        {
            q.mix = 0.5f + 0.5f * (float) std::sin ((double) pos * 0.0004);
            q.fxEnabled = (pos / 20000) % 2 == 0;
            q.offsetMs = (float) ((pos / 7000) % 5) * 20.0f - 40.0f;
            q.freeze = (pos / 60000) % 2 == 1;
            q.sync = (pos / 50000) % 3 != 2;
        });
        CHECK (maxCurvature (rm.l) < limit, "Mix, FX, Offset, Freeze and Sync automated: %.4f", maxCurvature (rm.l));

        EngineParams q = p;
        q.smoothness = 1.0f;
        q.style = Style::smooth;
        const double smoothCurv = maxCurvature (render (tone, q).l);
        CHECK (smoothCurv < 0.25 * maxCurvature (render (tone, p).l), "Smoothness 100 %% (Smooth) is far softer than 0 %% (%.5f vs %.5f)",
               smoothCurv, maxCurvature (render (tone, p).l));
    }

    //--------------------------------------------------------------------------
    std::printf ("\n=== 5) Safety: bounds, NaN, determinism, block size, mono ===\n");
    {
        std::mt19937 rng (99);
        std::uniform_real_distribution<float> u (0.0f, 1.0f);
        const PerBlock randomise = [&] (int64_t, EngineParams& q, Transport& t)
        {
            q.mode = (Mode) (rng() % choices::numModes);
            q.style = (Style) (rng() % choices::numStyles);
            q.shape = (Shape) (rng() % choices::numShapes);
            q.glitchType = (GlitchType) (rng() % choices::numGlitchTypes);
            q.amount = u (rng); q.length = u (rng); q.smoothness = u (rng); q.intensity = u (rng);
            q.offsetMs = -100.0f + 200.0f * u (rng); q.mix = u (rng);
            q.sync = u (rng) > 0.3f; q.rateIndex = (int) (rng() % 15); q.rateMs = 10.0f + 1990.0f * u (rng);
            q.freeze = u (rng) > 0.7f; q.selectionStart = u (rng); q.selectionEnd = u (rng);
            q.fxEnabled = u (rng) > 0.3f; q.delayMix = u (rng); q.delayFeedback = 0.85f * u (rng); q.reverbMix = u (rng);
            q.delaySync = u (rng) > 0.5f; q.delayRateIndex = (int) (rng() % 15); q.delayTimeMs = 1.0f + 1999.0f * u (rng);
            q.bypass = u (rng) > 0.9f;
            if (u (rng) > 0.8f) t.ppq += 8.0 * u (rng) - 4.0;
            t.playing = u (rng) > 0.2f;
            t.bpm = 40.0 + 260.0 * u (rng);
            if (u (rng) > 0.95f) t.hasTempo = false;
            t.hasBarStart = u (rng) > 0.5f;
            t.barStartPpq = std::floor (t.ppq / 4.0) * 4.0 + (u (rng) > 0.9f ? 1.0 : 0.0);
            t.numerator = 1 + (int) (rng() % 12);
            t.denominator = 1 << (rng() % 4);
        };
        const auto loud = voice (4.0, kRate, 5);
        bool ok = true;
        for (int block : { 1, 13, 64, 512, 2048 })
            ok = ok && allFinite (render (loud, EngineParams {}, block, 120.0, true, kRate, randomise), 8.0f);
        CHECK (ok, "every parameter, the transport, the metre and Freeze randomised every block (block 1 ... 2048): finite and bounded");

        // garbage parameter values
        const PerBlock garbage = [&] (int64_t pos, EngineParams& q, Transport& t)
        {
            const float g[] = { NAN, INFINITY, -INFINITY, -1.0e9f, 1.0e9f, -1.0f, 2.0f };
            const float v = g[(pos / 256) % 7];
            q.amount = v; q.length = v; q.smoothness = v; q.intensity = v; q.mix = v; q.offsetMs = v; q.rateMs = v;
            q.selectionStart = v; q.selectionEnd = -v; q.delayFeedback = v; q.delayMix = v; q.delayTimeMs = v;
            q.reverbMix = v; q.reverbSize = v; q.reverbDamping = v; q.rateIndex = (int) (pos % 40) - 20;
            q.delayRateIndex = 99 - (int) (pos % 200);
            t.bpm = v; t.ppq = (pos / 256) % 3 == 0 ? NAN : t.ppq; t.numerator = (int) v; t.denominator = 0;
        };
        CHECK (allFinite (render (loud, EngineParams {}, 256, 120.0, true, kRate, garbage), 8.0f),
               "NaN / Inf / out-of-range parameters, tempo, position and metre: finite and bounded");

        std::vector<float> nan = vox;
        for (size_t i = 1000; i < nan.size(); i += 4799) nan[i] = std::nanf ("");
        nan[2000] = INFINITY;
        EngineParams p;
        p.amount = 1.0f; p.length = 1.0f; p.reverbMix = 0.5f; p.delayMix = 0.5f;
        CHECK (allFinite (render (nan, p), 8.0f), "NaN / Inf in the input never reach the output");

        p.mode = Mode::glitch;
        p.intensity = 1.0f;
        p.glitchType = GlitchType::shuffle;
        CHECK (same (render (vox, p).l, render (vox, p).l), "Glitch renders are repeatable (seeded)");

        bool sameAll = true;
        const auto ref = render (vox, p, 512);
        for (int block : { 1, 7, 64, 1024 })
            sameAll = sameAll && same (render (vox, p, block).l, ref.l);
        CHECK (sameAll, "the same output for blocks 1, 7, 64, 512 and 1024 (bit for bit)");

        bool rates = true;
        for (double rate : { 22050.0, 44100.0, 88200.0, 96000.0, 192000.0 })
            rates = rates && allFinite (render (voice (3.0, rate), p, 256, 120.0, true, rate), 8.0f);
        CHECK (rates, "22.05, 44.1, 88.2, 96 and 192 kHz: finite and bounded");

        // mono = the left channel of a stereo render with identical channels
        StutterEngine e;
        e.prepare (kRate, 256);
        std::vector<float> mono = vox;
        Transport t; t.hasTempo = true; t.bpm = 120.0; t.playing = true; t.hasPosition = true;
        for (size_t pos = 0; pos < mono.size(); pos += 256)
        {
            t.ppq = (double) pos * 120.0 / (60.0 * kRate);
            float* ch[1] = { mono.data() + pos };
            e.process (ch, 1, (int) std::min<size_t> (256, mono.size() - pos), p, t);
        }
        CHECK (same (mono, render (vox, p, 256, 120.0, true, kRate, nullptr, true).l), "mono processing = stereo with identical channels");

        // the meters hold the block peak until the UI takes it
        StutterEngine m;
        m.prepare (kRate, 256);
        std::vector<float> l (256, 0.0f), rr (256, 0.0f);
        l[10] = 0.9f;
        float* ch2[2] = { l.data(), rr.data() };
        m.process (ch2, 2, 256, EngineParams {}, Transport {});
        std::fill (l.begin(), l.end(), 0.1f);
        m.process (ch2, 2, 256, EngineParams {}, Transport {});
        const float pk = EngineStatus::take (m.status.inPeak[0]);
        CHECK (std::abs (pk - 0.9f) < 1e-6f && EngineStatus::take (m.status.inPeak[0]) == 0.0f,
               "meter peak is held across blocks until taken (%.2f), then cleared", pk);
    }

    //--------------------------------------------------------------------------
    std::printf ("\n=== 6) Timing: free time, offset, transport stopped, dotted rates, loops, long slices ===\n");
    {
        EngineParams p;
        p.sync = false;
        p.rateMs = 100.0f;
        p.amount = 1.0f;
        p.length = 1.0f;
        p.smoothness = 0.0f;
        const auto r = render (vox, p, 256, 120.0, false);
        const size_t s = (size_t) (0.1 * kRate);
        CHECK (corr (r.l, 3 * s + 300, vox, 300, s - 600) > 0.999 && corr (r.l, 16 * s + 300, vox, 16 * s + 300, s - 600) > 0.999,
               "Free 100 ms: slice 3 repeats slice 0; slice 16 starts a new phrase");

        EngineParams q;
        q.amount = 1.0f;
        q.length = 1.0f;
        q.smoothness = 0.0f;
        q.offsetMs = 50.0f;
        const auto ro = render (vox, q);
        const size_t off = (size_t) (0.05 * kRate);
        CHECK (corr (ro.l, off + 2 * slice8 + guard, vox, off + guard, slice8 - 2 * guard) > 0.999, "Offset +50 ms: the grid starts 50 ms later");

        const auto rs = render (vox, q, 256, 120.0, false);   // sync on, transport stopped: internal clock at 120 BPM
        CHECK (corr (rs.l, off + slice8 + guard, vox, off + guard, slice8 - 2 * guard) > 0.999,
               "transport stopped: the internal clock keeps the plug-in usable (1/8 at the host tempo)");

        // stopping the transport mid-bar keeps the grid running in time
        q.offsetMs = 0.0f;
        const auto rstop = render (vox, q, 256, 120.0, true, kRate, [] (int64_t pos, EngineParams&, Transport& t) { t.playing = pos < 100000; });
        CHECK (corr (rstop.l, 9 * slice8 + guard, vox, 8 * slice8 + guard, slice8 - 2 * guard) > 0.999
               && corr (rstop.l, 17 * slice8 + guard, vox, 16 * slice8 + guard, slice8 - 2 * guard) > 0.999,
               "transport stopped mid-bar: the internal clock carries the same grid on");

        // 1/16D: 10 2/3 slices per bar; the phrase must restart on every bar
        EngineParams d;
        d.amount = 1.0f;
        d.length = 1.0f;
        d.smoothness = 0.0f;
        d.rateIndex = 8;      // 1/16D
        const auto rd = render (vox, d);
        const size_t bar = (size_t) (2.0 * kRate), s16d = (size_t) (0.1875 * kRate);
        CHECK (corr (rd.l, bar + guard, vox, bar + guard, s16d - 2 * guard) > 0.999
               && corr (rd.l, bar + s16d + guard, vox, bar + guard, s16d - 2 * guard) > 0.999
               && corr (rd.l, 2 * bar + guard, vox, 2 * bar + guard, s16d - 2 * guard) > 0.999,
               "1/16D: every bar starts with a fresh capture (no drift against the bar)");

        // host loop of two bars: after the jump back the capture restarts cleanly
        const auto rl = render (vox, d, 256, 120.0, true, kRate, [] (int64_t, EngineParams& e, Transport& t)
        {
            e.rateIndex = 7;  // 1/8
            t.ppq = std::fmod (t.ppq, 8.0);
        });
        const size_t loop = (size_t) (4.0 * kRate);
        CHECK (corr (rl.l, loop + guard, vox, loop + guard, slice8 - 2 * guard) > 0.999
               && corr (rl.l, loop + slice8 + guard, vox, loop + guard, slice8 - 2 * guard) > 0.999,
               "host loop: after the jump to bar 1 the new audio is captured and repeated");

        // bar position from the host (6/8 time, the song starts at ppq 1.5)
        const auto rb = render (vox, q, 256, 120.0, true, kRate, [] (int64_t, EngineParams&, Transport& t)
        {
            t.numerator = 6; t.denominator = 8;
            t.ppq += 1.5;
            t.hasBarStart = true;
            t.barStartPpq = 1.5 + 3.0 * std::floor ((t.ppq - 1.5) / 3.0);
        });
        const size_t b0 = (size_t) (0.0 * kRate), b1 = (size_t) (1.5 * kRate);   // 6/8 bar = 3 beats = 1.5 s
        (void) b0;
        CHECK (corr (rb.l, b1 + guard, vox, b1 + guard, slice8 - 2 * guard) > 0.999
               && corr (rb.l, b1 + slice8 + guard, vox, b1 + guard, slice8 - 2 * guard) > 0.999,
               "6/8 with the host's bar position: phrases start on the host's bars");

        // 1/2D at 50 BPM: 3.6 s slices (longer than 1/3 of the capture)
        EngineParams lng;
        lng.amount = 1.0f;
        lng.length = 1.0f;
        lng.smoothness = 0.0f;
        lng.rateIndex = 0;
        const auto longIn = voice (12.0, kRate, 3);
        const auto rlong = render (longIn, lng, 512, 50.0);
        const size_t s2d = (size_t) (3.6 * kRate);
        CHECK (allFinite (rlong, 8.0f) && corr (rlong.l, s2d + guard, longIn, guard, s2d - 2 * guard) > 0.999,
               "1/2D at 50 BPM (3.6 s slices): stays on the grid, the first repeat is slice 0");
    }

    //--------------------------------------------------------------------------
    std::printf ("\n=== 7) Freeze and selection ===\n");
    {
        std::vector<float> in = vox;
        for (size_t i = (size_t) (3.0 * kRate); i < in.size(); ++i) in[i] = 0.0f;      // silence after 3 s
        EngineParams p;
        p.amount = 1.0f;
        p.length = 1.0f;
        const auto r = render (in, p, 256, 120.0, true, kRate, [] (int64_t pos, EngineParams& q, Transport&)
        {
            q.freeze = pos >= (int64_t) (2.0 * kRate);
            q.selectionStart = 0.25f;
            q.selectionEnd = 0.5f;
        });
        double worst = 1.0;
        for (int m = 0; m < 8; ++m)
            worst = std::min (worst, corr (r.l, (size_t) (2.0 * kRate) + (size_t) m * slice8 + guard, vox, (size_t) (0.5 * kRate) + guard, slice8 - 2 * guard));
        CHECK (worst > 0.999, "frozen at 2 s, selection 25-50 %%: every slice repeats the audio at 0.5 s (worst %.5f)", worst);
        CHECK (rms (r.l, (size_t) (4.0 * kRate), (size_t) (5.5 * kRate)) > 0.02, "frozen repeats continue over silent input (rms %.3f)",
               rms (r.l, (size_t) (4.0 * kRate), (size_t) (5.5 * kRate)));

        // unfreeze at 3 s: the next bar captures live audio again
        const auto ru = render (vox, p, 256, 120.0, true, kRate, [] (int64_t pos, EngineParams& q, Transport&)
        {
            q.freeze = pos >= (int64_t) (1.5 * kRate) && pos < (int64_t) (3.0 * kRate);
        });
        CHECK (corr (ru.l, (size_t) (4.0 * kRate) + slice8 + guard, vox, (size_t) (4.0 * kRate) + guard, slice8 - 2 * guard) > 0.999,
               "after unfreezing, the next bar repeats live audio again");

        // tiny selection (20 ms minimum) shorter than the slice: plays once with fades, then silence
        const auto rt = render (in, p, 256, 120.0, true, kRate, [] (int64_t pos, EngineParams& q, Transport&)
        {
            q.freeze = pos >= (int64_t) (2.0 * kRate);
            q.selectionStart = 0.5f;
            q.selectionEnd = 0.5f;
        });
        CHECK (allFinite (rt, 8.0f) && rms (rt.l, (size_t) (4.0 * kRate) + 1200, (size_t) (4.0 * kRate) + slice8 - 100) < 1e-6
               && rms (rt.l, (size_t) (4.0 * kRate) + 100, (size_t) (4.0 * kRate) + 800) > 1e-3,
               "zero-length selection: clamped to 20 ms, faded, silent for the rest of the slice");

        bool early = true;                       // Freeze needs 1 s of audio first
        const auto re = render (vox, p, 256, 120.0, true, kRate, [] (int64_t, EngineParams& q, Transport&) { q.freeze = true; });
        early = corr (re.l, slice8 + guard, vox, guard, slice8 - 2 * guard) > 0.999;
        CHECK (early, "Freeze on from the start: plays like Stutter until 1 s has been captured");
    }

    //--------------------------------------------------------------------------
    std::printf ("\n=== 8) FX ===\n");
    {
        std::vector<float> click ((size_t) (30.0 * kRate), 0.0f);
        click[100] = 1.0f;
        StereoDelay d;
        d.prepare (kRate);
        double early = 0.0, late = 0.0;
        for (size_t i = 0; i < click.size(); ++i)
        {
            float l = click[i], r = click[i];
            d.process (l, r, 0.25 * kRate, 1.0f /* above the cap */, 1.0f);
            if (i < (size_t) (2 * kRate)) early = std::max (early, (double) std::abs (l));
            if (i > (size_t) (25 * kRate)) late = std::max (late, (double) std::abs (l));
        }
        CHECK (late < 1e-3 * early, "delay at maximum feedback dies away (peak after 25 s %.2g vs first second %.2g)", late, early);

        Reverb rv;
        rv.prepare (kRate);
        double peak = 0.0, tail = 0.0;
        for (size_t i = 0; i < click.size(); ++i)
        {
            float l = click[i], r = click[i];
            rv.process (l, r, 1.0f, 0.0f, 1.0f);
            if (i > 200 && i < (size_t) kRate) peak = std::max (peak, (double) std::abs (l));
            if (i > (size_t) (25 * kRate)) tail = std::max (tail, (double) std::abs (l));
        }
        CHECK (peak > 1e-4 && tail < 1e-2 * peak, "reverb at maximum size decays (tail %.2g vs %.2g)", tail, peak);

        EngineParams p;
        p.amount = 1.0f;
        p.length = 0.25f;          // window: slices 0-1 (slice 1 repeats), 2-7 live
        p.delayMix = 0.6f;
        p.delayFeedback = 0.6f;
        EngineParams dry = p;
        dry.delayMix = 0.0f;
        const auto r = render (vox, p), rd = render (vox, dry);
        CHECK (rmsDiff (r.l, rd.l, 2 * slice8, 4 * slice8) > 0.01, "the delay tail continues after the repeat stops (rms %.3f added)",
               rmsDiff (r.l, rd.l, 2 * slice8, 4 * slice8));
        CHECK (same (std::vector<float> (r.l.begin(), r.l.begin() + (long) slice8), std::vector<float> (rd.l.begin(), rd.l.begin() + (long) slice8)),
               "the live voice is not sent to the FX (capture slice identical with and without the delay)");

        EngineParams off = p;
        const auto roff = render (vox, off, 256, 120.0, true, kRate, [] (int64_t pos, EngineParams& q, Transport&) { q.fxEnabled = pos < 200000; });
        CHECK (same (std::vector<float> (roff.l.begin() + 210000, roff.l.end()), std::vector<float> (rd.l.begin() + 210000, rd.l.end())),
               "FX Chain off: after a 50 ms fade the output equals the no-FX render bit for bit");
    }

    //--------------------------------------------------------------------------
    std::printf ("\n=== 9) CPU (MEASURED on this machine, informative) ===\n");
    {
        EngineParams p;
        p.mode = Mode::glitch;
        p.glitchType = GlitchType::shuffle;
        p.amount = 1.0f; p.length = 1.0f; p.intensity = 1.0f; p.rateIndex = 13;
        p.style = Style::tape;
        p.delayMix = 0.5f; p.reverbMix = 0.5f;
        const auto in = voice (20.0, kRate, 9);
        const auto t0 = std::chrono::steady_clock::now();
        const auto r = render (in, p, 512);
        const auto t1 = std::chrono::steady_clock::now();
        const double secs = std::chrono::duration<double> (t1 - t0).count();
        CHECK (allFinite (r, 8.0f) && secs < 20.0 * 0.2, "Glitch / Tape / delay / reverb, 20 s stereo at 48 kHz in %.3f s (%.2f %% of one core)",
               secs, 100.0 * secs / 20.0);
    }

    std::printf ("\n%s: %d checks, %d failure%s\n", failures == 0 ? "ALL PASSED" : "FAILED", checks, failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
