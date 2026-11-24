#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace vex::dsp
{
/** Stereo delay for the FX chain (specification section 11).

    - Time changes never click and never sweep the pitch: a new time starts a second read head and
      the two are crossfaded over 40 ms.
    - Feedback is capped at kMaxFeedback, and the loop has a gentle low-pass (repeats darken) and a
      soft clipper, so it cannot run away even at the cap.
    - All memory is allocated in prepare(). process() is real-time safe.
*/
class StereoDelay
{
public:
    static constexpr float kMaxFeedback = 0.85f;
    static constexpr double kMaxSeconds = 2.5;

    void prepare (double sampleRate)
    {
        rate = sampleRate > 0.0 ? sampleRate : 44100.0;
        size = (int) std::ceil (kMaxSeconds * rate) + 4;
        for (auto& l : line)
            l.assign ((size_t) size, 0.0f);
        xfadeLength = std::max (1, (int) (0.04 * rate));
        const float cutoff = 6000.0f;
        lpCoef = 1.0f - std::exp (-2.0f * 3.14159265f * cutoff / (float) rate);
        reset();
    }

    void reset()
    {
        for (auto& l : line)
            std::fill (l.begin(), l.end(), 0.0f);
        writePos = 0;
        lp[0] = lp[1] = 0.0f;
        currentDelay = targetDelay = -1.0;
        xfade = 0;
        energy = 0.0f;
    }

    /** Adds the delayed signal (scaled by `mix`) to l / r. `delaySamples` may change every call. */
    void process (float& l, float& r, double delaySamples, float feedback, float mix) noexcept
    {
        delaySamples = std::clamp (delaySamples, 1.0, (double) size - 3.0);
        feedback = std::clamp (feedback, 0.0f, kMaxFeedback);

        if (currentDelay < 0.0)
            currentDelay = targetDelay = delaySamples;
        else if (xfade == 0 && std::abs (delaySamples - currentDelay) > 0.5)
        {
            targetDelay = delaySamples;
            xfade = xfadeLength;
        }

        float out[2];
        for (int ch = 0; ch < 2; ++ch)
        {
            float d = read (ch, currentDelay);
            if (xfade > 0)
            {
                const float t = 1.0f - (float) xfade / (float) xfadeLength;
                d = d * (1.0f - t) + read (ch, targetDelay) * t;
            }
            out[ch] = d;
        }
        if (xfade > 0 && --xfade == 0)
            currentDelay = targetDelay;

        const float in[2] = { l, r };
        for (int ch = 0; ch < 2; ++ch)
        {
            lp[ch] += lpCoef * (out[ch] - lp[ch]);
            const float fb = softClip (lp[ch] * feedback);
            line[(size_t) ch][(size_t) writePos] = in[ch] + fb;
        }
        writePos = (writePos + 1) % size;

        energy = std::max (std::abs (out[0]), std::max (std::abs (out[1]), energy * 0.9995f));
        l += mix * out[0];
        r += mix * out[1];
    }

    /** True while repeats are still audible (used to skip work when everything is silent). */
    bool isRinging() const noexcept { return energy > 1.0e-6f; }

private:
    static float softClip (float x) noexcept { return std::tanh (x); }

    float read (int ch, double delay) const noexcept
    {
        double pos = (double) writePos - delay;
        while (pos < 0.0)
            pos += size;
        const int i0 = (int) pos;
        const int i1 = (i0 + 1) % size;
        const float frac = (float) (pos - i0);
        const auto& l = line[(size_t) ch];
        return l[(size_t) i0] + frac * (l[(size_t) i1] - l[(size_t) i0]);
    }

    std::vector<float> line[2];
    double rate = 44100.0;
    int size = 1, writePos = 0, xfadeLength = 1, xfade = 0;
    double currentDelay = -1.0, targetDelay = -1.0;
    float lpCoef = 0.5f, lp[2] { 0.0f, 0.0f }, energy = 0.0f;
};

//==============================================================================
/** Stereo reverb (Freeverb topology: 8 parallel combs and 4 series allpasses per channel, the right
    channel's delays spread by 23 samples). Size sets the comb feedback, damping their low-pass.
    All delay lines are allocated in prepare() and scaled to the sample rate. */
class Reverb
{
public:
    void prepare (double sampleRate)
    {
        const double scale = (sampleRate > 0.0 ? sampleRate : 44100.0) / 44100.0;
        static constexpr int combTuning[8] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
        static constexpr int allTuning[4]  = { 556, 441, 341, 225 };
        for (int ch = 0; ch < 2; ++ch)
        {
            const int spread = ch == 0 ? 0 : 23;
            for (int i = 0; i < 8; ++i)
                comb[ch][(size_t) i].buf.assign ((size_t) std::max (1, (int) ((combTuning[i] + spread) * scale)), 0.0f);
            for (int i = 0; i < 4; ++i)
                all[ch][(size_t) i].buf.assign ((size_t) std::max (1, (int) ((allTuning[i] + spread) * scale)), 0.0f);
        }
        reset();
    }

    void reset()
    {
        for (auto& chn : comb)
            for (auto& c : chn) { std::fill (c.buf.begin(), c.buf.end(), 0.0f); c.pos = 0; c.store = 0.0f; }
        for (auto& chn : all)
            for (auto& a : chn) { std::fill (a.buf.begin(), a.buf.end(), 0.0f); a.pos = 0; }
        energy = 0.0f;
    }

    /** size / damping 0..1. Adds the reverb (scaled by `mix`) to l / r. */
    void process (float& l, float& r, float size, float damping, float mix) noexcept
    {
        const float feedback = 0.70f + 0.28f * std::clamp (size, 0.0f, 1.0f);
        const float damp = 0.05f + 0.55f * std::clamp (damping, 0.0f, 1.0f);
        const float input = (l + r) * 0.015f;
        float out[2] = { 0.0f, 0.0f };
        for (int ch = 0; ch < 2; ++ch)
        {
            float acc = 0.0f;
            for (auto& c : comb[ch])
            {
                const float y = c.buf[(size_t) c.pos];
                c.store = y * (1.0f - damp) + c.store * damp;
                c.buf[(size_t) c.pos] = input + c.store * feedback;
                if (++c.pos >= (int) c.buf.size()) c.pos = 0;
                acc += y;
            }
            for (auto& a : all[ch])
            {
                const float b = a.buf[(size_t) a.pos];
                a.buf[(size_t) a.pos] = acc + b * 0.5f;
                if (++a.pos >= (int) a.buf.size()) a.pos = 0;
                acc = b - acc;
            }
            out[ch] = acc;
        }
        energy = std::max (std::abs (out[0]), std::max (std::abs (out[1]), energy * 0.9998f));
        l += mix * out[0];
        r += mix * out[1];
    }

    bool isRinging() const noexcept { return energy > 1.0e-6f; }

private:
    struct Comb { std::vector<float> buf; int pos = 0; float store = 0.0f; };
    struct AllPass { std::vector<float> buf; int pos = 0; };
    std::array<Comb, 8> comb[2];
    std::array<AllPass, 4> all[2];
    float energy = 0.0f;
};
}
