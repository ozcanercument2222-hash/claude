#pragma once
//==============================================================================
//  VSTM Tune - ortak DSP yardimcilari (JUCE'dan bagimsiz)
//==============================================================================
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

namespace vstm
{
constexpr double kPi = 3.14159265358979323846;

inline float clamp01 (float x) noexcept               { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }
inline float smoothstep (float x) noexcept            { const float t = clamp01 (x); return t * t * (3.0f - 2.0f * t); }
inline float smoothstep (float e0, float e1, float x) noexcept { return smoothstep ((x - e0) / (e1 - e0)); }

inline double hzToMidi (double hz) noexcept           { return 69.0 + 12.0 * std::log2 (hz / 440.0); }
inline double midiToHz (double midi) noexcept         { return 440.0 * std::pow (2.0, (midi - 69.0) / 12.0); }

/** RBJ biquad (Direct Form II transposed). Used for the detector's anti-alias low-pass. */
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1 = 0, z2 = 0;

    void setLowpass (double sampleRate, double cutoff, double q) noexcept
    {
        const double w0 = 2.0 * kPi * cutoff / sampleRate;
        const double cs = std::cos (w0), sn = std::sin (w0);
        const double alpha = sn / (2.0 * q);
        const double a0 = 1.0 + alpha;
        b0 = (1.0 - cs) * 0.5 / a0;
        b1 = (1.0 - cs) / a0;
        b2 = b0;
        a1 = -2.0 * cs / a0;
        a2 = (1.0 - alpha) / a0;
    }

    void reset() noexcept { z1 = z2 = 0.0; }

    float process (float x) noexcept
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return (float) y;
    }
};

/** 4-point, 3rd-order Hermite interpolation. frac in [0, 1) between y1 and y2. */
inline float hermite (float y0, float y1, float y2, float y3, float frac) noexcept
{
    const float c0 = y1;
    const float c1 = 0.5f * (y2 - y0);
    const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * frac + c2) * frac + c1) * frac + c0;
}

/** Single-producer / single-consumer queue without locks or allocation after construction.
    Audio thread pushes, UI thread pops. Drops new items when full (never blocks audio). */
template <typename T, int Capacity>
class SpscQueue
{
public:
    bool push (const T& item) noexcept
    {
        const int w = write.load (std::memory_order_relaxed);
        const int next = (w + 1) % Capacity;
        if (next == read.load (std::memory_order_acquire))
            return false;
        data[(size_t) w] = item;
        write.store (next, std::memory_order_release);
        return true;
    }

    bool pop (T& item) noexcept
    {
        const int r = read.load (std::memory_order_relaxed);
        if (r == write.load (std::memory_order_acquire))
            return false;
        item = data[(size_t) r];
        read.store ((r + 1) % Capacity, std::memory_order_release);
        return true;
    }

    void clear() noexcept { read.store (write.load()); }

private:
    T data[(size_t) Capacity] {};
    std::atomic<int> write { 0 }, read { 0 };
};
} // namespace vstm
