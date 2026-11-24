#pragma once

#include "DSP/CaptureBuffer.h"
#include "DSP/Fx.h"
#include "Parameters/Choices.h"
#include "Parameters/Divisions.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace vex::dsp
{
using choices::GlitchType;
using choices::Mode;
using choices::Shape;
using choices::Style;

/** Everything the engine needs from the parameters, already in DSP units (0..1, ms, indices). */
struct EngineParams
{
    bool bypass = false;
    Mode mode = Mode::stutter;
    float amount = 0.75f;
    bool sync = true;
    int rateIndex = divisions::defaultIndex;
    float rateMs = 250.0f;
    float length = 0.5f;
    Style style = Style::sharp;
    Shape shape = Shape::hard;
    float smoothness = 0.4f;
    float offsetMs = 0.0f;
    float mix = 1.0f;
    GlitchType glitchType = GlitchType::pixel;
    float intensity = 0.7f;
    bool freeze = false;
    float selectionStart = 0.0f, selectionEnd = 1.0f;      // part of the frozen capture, 0..1

    bool fxEnabled = true;
    bool delaySync = true;
    int delayRateIndex = divisions::defaultIndex;
    float delayTimeMs = 250.0f, delayFeedback = 0.3f, delayMix = 0.0f;
    float reverbSize = 0.5f, reverbDamping = 0.5f, reverbMix = 0.0f;
};

/** Host transport for one block (from the AudioPlayHead; anything missing is marked invalid). */
struct Transport
{
    bool hasTempo = false;
    double bpm = 120.0;
    bool playing = false;
    bool hasPosition = false;
    double ppq = 0.0;               // quarter notes at the first sample of the block
    bool hasBarStart = false;
    double barStartPpq = 0.0;       // quarter notes at the start of the current bar
    int numerator = 4, denominator = 4;
};

/** What the UI may show. Written by the audio thread, read by the UI (atomics only). */
struct EngineStatus
{
    enum SyncState { freeTime = 0, hostLocked = 1, hostStopped = 2 };

    // peaks since the UI last took them with take()
    std::atomic<float> inPeak[2] { { 0.0f }, { 0.0f } }, outPeak[2] { { 0.0f }, { 0.0f } };
    std::atomic<int> syncState { freeTime };
    std::atomic<float> bpm { 120.0f }, sliceSeconds { 0.25f };
    std::atomic<int> step { 0 }, steps { 8 };                   // slice position in the phrase
    // the phrase's slices as the engine plans them (bit k = slice k): repeats, and the slices whose
    // audio the repeats play (capture). Gate: every slice is gated. Recomputed at every slice start.
    std::atomic<uint64_t> repeatMask[2] { { 0 }, { 0 } }, captureMask[2] { { 0 }, { 0 } };
    std::atomic<bool> gating { false };
    std::atomic<int64_t> sliceStartFrame { 0 };                 // live-timeline frame where this slice began
    std::atomic<float> sliceFrames { 12000.0f };                // its nominal length in frames
    std::atomic<bool> effectActive { false };
    std::atomic<int64_t> sourceStart { -1 }, sourceEnd { -1 };  // live-timeline frames being repeated
    std::atomic<bool> playingFrozen { false };

    static_assert (std::atomic<float>::is_always_lock_free, "the meters need lock-free atomics");

    static float take (std::atomic<float>& a) noexcept { return a.exchange (0.0f, std::memory_order_relaxed); }

    static void raise (std::atomic<float>& a, float value) noexcept
    {
        float old = a.load (std::memory_order_relaxed);
        while (value > old && ! a.compare_exchange_weak (old, value, std::memory_order_relaxed)) {}
    }
};

/** VEX Stutter's DSP (specification sections 1-12). JUCE-free and real-time safe: prepare() allocates
    everything; process() never allocates, locks or does I/O, and clamps every buffer position.
    Behaviour is documented in docs/DSP.md; the short version:

      Grid     Slices of the Rate length. Locked to the host's PPQ while it plays; with the transport
               stopped (or in Free mode) an internal clock runs on, at the host tempo (or 120 BPM) or
               the Free time. Offset shifts the grid (+ = later). A phrase is a whole number of bars
               (at least 4 slices) and restarts on every phrase boundary, so dotted rates stay on the
               bar (the last slice is shorter). Free mode: phrases of 16 slices.
      Length   Stutter / Repeat / Reverse / Glitch: the first round(Length x slices) slices of a phrase
               (at least 2) are the effect window; the rest passes dry. Gate: open part of each slice.
      Amount   0 % = clean, bit-exact. In the window round(Amount x candidates) slices (at least 1)
               become repeats, spread evenly. Gate: depth of the closed part.
      Modes    Stutter: the window's first slice is captured (it plays live) and repeated.
               Repeat: a new fragment is captured every beat and repeated until the next one.
               Gate: the live voice is chopped. Reverse: Stutter with the repeats played backwards.
               Glitch: Stutter, a repeat varied with probability Intensity: Pixel (one micro-slice
               retriggered), Burst (a burst of tiny repeats, then the slice), Shuffle (micro-slices
               reordered); sometimes a rhythmic gap. Seeded per slice: renders repeat exactly.
      Freeze   Repeats read the selected part of the frozen capture instead of the live input
               (Stutter, Repeat, Reverse, Glitch; Gate always gates the live voice).
      Shape    Envelope of each repeat / gate opening: Hard, Ramp Up, Ramp Down, Smooth (half sine).
      Smooth.  Edge-fade and crossfade length 0.5 ms (0 %) to 40 ms, never above half a slice.
      Style    Sharp (half fades), Smooth (double fades), Tight (repeats 70 % long), Loose (repeats start
               0-15 ms late), Tape (repeats low-passed 5 kHz, gentle saturation at unity small-signal
               gain), Random (per repeat gain -4..0 dB, 15 % of repeats skipped), Digital Glitch
               (repeats sample-held x3, 10 bit).
      FX       Delay then reverb on the effect signal only (the repeats; in Gate mode the gated pulses).
               The live voice passing through stays clean; tails continue after the repeats stop.
      Mix      Linear: out = dry + Mix x (wet - dry), no level jump where wet = dry.
      Bypass   20 ms crossfade to the dry input; capture and timing keep running underneath.
*/
class StutterEngine
{
public:
    static constexpr int kMaxSlicesPerPhrase = 128;
    static constexpr int kMinSlicesPerPhrase = 4;
    static constexpr int kFreePhraseSlices = 16;
    static constexpr float kTapeDrive = 1.5f;

    void prepare (double sampleRate, int maximumBlockSize)
    {
        (void) maximumBlockSize;
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;
        capture.prepare (sr);
        delay.prepare (sr);
        reverb.prepare (sr);
        tapeCoef = 1.0f - std::exp (-2.0f * 3.14159265f * 5000.0f / (float) sr);
        minFade = (float) std::max (8.0, 0.003 * sr);
        reset();
    }

    void reset()
    {
        capture.reset();
        delay.reset();
        reverb.reset();
        for (auto& v : voices) v = Voice {};
        freeBeat = lastBeatPos = expectedPpq = phraseOrigin = 0.0;
        haveGrid = hostWasRunning = unfreezePending = fxWasOn = false;
        lastPhrase = curPhrase = 0;
        lastK = -1;
        lastSync = true;
        lastRateIndex = -1;
        for (auto& a : phraseStartAbs) a = -1;
        liveBase = Follower {};
        livePulse = Follower {};
        livePulse.value = livePulse.from = 0.0f;
        mixSmoothed = bypassGain = depthS = -1.0f;
        delayMixS = reverbMixS = delayFbS = reverbSizeS = reverbDampS = -1.0f;
        fxGain = 0.0f;
        haveMode = sliceStarted = false;
        cur = Decision {};
        curEffect = prevEffect = nextEffect = false;
        curSliceLen = 1.0;
        curSliceId = 0;
        phraseSlices = 8;
    }

    const CaptureBuffer& getCapture() const noexcept { return capture; }
    EngineStatus status;

    /** Every value made finite and in range (NaN / Inf from a host or a corrupt preset never reach
        the DSP); anything unusable falls back to its default. */
    static EngineParams sanitise (const EngineParams& in) noexcept
    {
        const EngineParams def;
        EngineParams p = in;
        auto unit = [] (float x, float fallback) { return std::isfinite (x) ? std::clamp (x, 0.0f, 1.0f) : fallback; };
        auto range = [] (float x, float lo, float hi, float fallback) { return std::isfinite (x) ? std::clamp (x, lo, hi) : fallback; };
        p.amount = unit (in.amount, 0.0f);
        p.length = unit (in.length, def.length);
        p.smoothness = unit (in.smoothness, def.smoothness);
        p.mix = unit (in.mix, def.mix);
        p.intensity = unit (in.intensity, def.intensity);
        p.selectionStart = unit (in.selectionStart, 0.0f);
        p.selectionEnd = unit (in.selectionEnd, 1.0f);
        p.delayMix = unit (in.delayMix, 0.0f);
        p.reverbMix = unit (in.reverbMix, 0.0f);
        p.reverbSize = unit (in.reverbSize, def.reverbSize);
        p.reverbDamping = unit (in.reverbDamping, def.reverbDamping);
        p.rateMs = range (in.rateMs, 5.0f, 5000.0f, def.rateMs);
        p.offsetMs = range (in.offsetMs, -100.0f, 100.0f, 0.0f);
        p.delayTimeMs = range (in.delayTimeMs, 1.0f, 2500.0f, def.delayTimeMs);
        p.delayFeedback = range (in.delayFeedback, 0.0f, StereoDelay::kMaxFeedback, def.delayFeedback);
        p.rateIndex = clampIndex (in.rateIndex);
        p.delayRateIndex = clampIndex (in.delayRateIndex);
        if ((int) in.mode < 0 || (int) in.mode >= choices::numModes) p.mode = def.mode;
        if ((int) in.style < 0 || (int) in.style >= choices::numStyles) p.style = def.style;
        if ((int) in.shape < 0 || (int) in.shape >= choices::numShapes) p.shape = def.shape;
        if ((int) in.glitchType < 0 || (int) in.glitchType >= choices::numGlitchTypes) p.glitchType = def.glitchType;
        return p;
    }

    /** In-place processing of 1 or 2 channels. */
    void process (float* const* io, int numChannels, int numSamples, const EngineParams& rawParams, const Transport& t) noexcept
    {
        if (numSamples <= 0 || numChannels <= 0 || io == nullptr || io[0] == nullptr)
            return;
        const EngineParams p = sanitise (rawParams);
        const bool stereo = numChannels > 1 && io[1] != nullptr;
        if (! std::isfinite (freeBeat) || std::abs (freeBeat) > 1.0e12)
            freeBeat = 0.0;
        if (mixSmoothed < 0.0f) mixSmoothed = clamp01 (p.mix);
        if (bypassGain < 0.0f)  bypassGain = p.bypass ? 1.0f : 0.0f;
        if (depthS < 0.0f)      depthS = clamp01 (p.amount);
        if (delayMixS < 0.0f)
        {
            delayMixS = clamp01 (p.delayMix);
            reverbMixS = clamp01 (p.reverbMix);
            delayFbS = std::clamp (p.delayFeedback, 0.0f, StereoDelay::kMaxFeedback);
            reverbSizeS = clamp01 (p.reverbSize);
            reverbDampS = clamp01 (p.reverbDamping);
        }

        // ---- clock and grid for this block ---------------------------------------------------
        const bool hostValid = t.hasTempo && std::isfinite (t.bpm) && t.bpm >= 20.0 && t.bpm <= 999.0;
        const double bpm = hostValid ? t.bpm : 120.0;
        const bool freeMode = ! p.sync;
        const bool hostRunning = p.sync && t.playing && t.hasPosition && hostValid && std::isfinite (t.ppq)
                                 && std::abs (t.ppq) < 1.0e9;
        const int rateIndex = clampIndex (p.rateIndex);

        double sliceSamples = freeMode ? (double) std::clamp (p.rateMs, 5.0f, 5000.0f) * 0.001 * sr
                                       : divisions::table[rateIndex].beats * 60.0 / bpm * sr;
        sliceSamples = std::clamp (sliceSamples, 0.005 * sr, (double) capture.capacity() / 2.0);

        // the clock counts beats: host quarter notes, or (Free) one beat per slice
        const double bps = freeMode ? 1.0 / sliceSamples : bpm / (60.0 * sr);
        const double sliceBeats = freeMode ? 1.0 : sliceSamples * bps;
        double phraseBeats = kFreePhraseSlices;
        if (! freeMode)
        {
            const bool sigValid = t.numerator >= 1 && t.numerator <= 64 && t.denominator >= 1 && t.denominator <= 64;
            const double barBeats = sigValid ? t.numerator * 4.0 / t.denominator : 4.0;
            const double bars = std::max (1.0, std::ceil (kMinSlicesPerPhrase * sliceBeats / barBeats - 1.0e-9));
            phraseBeats = std::min (bars * barBeats, kMaxSlicesPerPhrase * sliceBeats);
            if (hostRunning && t.hasBarStart && std::isfinite (t.barStartPpq) && std::abs (t.barStartPpq) < 1.0e9)
            {
                // phrases start on a bar the phrase length divides (bar 1 = ppq 0 for a constant metre)
                const double barIndex = std::round (t.barStartPpq / barBeats);
                double origin = t.barStartPpq - (barIndex - bars * std::floor (barIndex / bars)) * barBeats;
                origin -= phraseBeats * std::floor (origin / phraseBeats);
                double diff = origin - phraseOrigin;
                diff -= phraseBeats * std::round (diff / phraseBeats);
                if (std::abs (diff) > 1.0e-3)             // ignore jitter in the host's bar position
                    phraseOrigin = origin;
            }
        }
        const int N = std::clamp ((int) std::floor (phraseBeats / sliceBeats + 0.75), 1, kMaxSlicesPerPhrase);
        const double offsetBeats = (double) std::clamp (p.offsetMs, -100.0f, 100.0f) * 0.001 * sr * bps;

        bool abrupt = false, restart = false;
        if (haveGrid)
        {
            if (hostRunning && hostWasRunning && std::abs (t.ppq - expectedPpq) > 0.002)
                abrupt = true;                                  // loop, seek or jump in the host
            if (hostRunning && ! hostWasRunning)
                abrupt = true;                                  // transport started: lock to its grid
            if (p.sync != lastSync || (p.sync && rateIndex != lastRateIndex))
            {
                abrupt = true;                                  // Rate changed: a new phrase starts now
                restart = ! hostRunning;
            }
        }
        if (restart)
        {
            phraseOrigin = 0.0;
            freeBeat = (double) (lastPhrase + 1) * phraseBeats + offsetBeats + 1.0e-9;
        }
        else if (! hostRunning && hostWasRunning)
            freeBeat = lastBeatPos + bps + offsetBeats;         // transport stopped: carry on smoothly
        lastSync = p.sync;
        lastRateIndex = rateIndex;

        status.syncState.store (freeMode ? EngineStatus::freeTime : (hostRunning ? EngineStatus::hostLocked : EngineStatus::hostStopped),
                                std::memory_order_relaxed);
        status.bpm.store ((float) bpm, std::memory_order_relaxed);
        status.sliceSeconds.store ((float) (sliceSamples / sr), std::memory_order_relaxed);
        status.sliceFrames.store ((float) sliceSamples, std::memory_order_relaxed);
        status.steps.store (N, std::memory_order_relaxed);

        handleFreeze (p);

        // ---- fades, smoothing and FX settings -------------------------------------------------
        float fade = (float) ((0.0005 + (double) clamp01 (p.smoothness) * 0.0395) * sr);
        if (p.style == Style::sharp)  fade *= 0.5f;
        if (p.style == Style::smooth) fade *= 2.0f;
        fade = std::clamp (fade, (float) (0.0005 * sr), (float) (0.5 * sliceSamples));
        blockFade = fade;
        const int transition = (int) minFade;                                // 3 ms smooth transitions
        if (haveMode && p.mode != lastMode)
        {
            liveBase.begin (transition);                                     // mode change: glide, never jump
            livePulse.begin (transition);
        }
        haveMode = true;
        lastMode = p.mode;
        const float mixStep = 1.0f / (float) std::max (1.0, 0.02 * sr);
        const float paramStep = 1.0f / (float) std::max (1.0, 0.03 * sr);   // FX / depth smoothing: 30 ms
        const float fxStep = 1.0f / (float) std::max (1.0, 0.05 * sr);
        const double delaySamples = p.delaySync ? divisions::table[clampIndex (p.delayRateIndex)].beats * 60.0 / bpm * sr
                                                : (double) std::clamp (p.delayTimeMs, 1.0f, 2500.0f) * 0.001 * sr;
        const float delayFbT = std::clamp (p.delayFeedback, 0.0f, StereoDelay::kMaxFeedback);
        const float delayMixT = clamp01 (p.delayMix), reverbMixT = clamp01 (p.reverbMix);
        const float reverbSizeT = clamp01 (p.reverbSize), reverbDampT = clamp01 (p.reverbDamping);
        const float depthT = clamp01 (p.amount);
        const float mixTarget = clamp01 (p.mix), bypassTarget = p.bypass ? 1.0f : 0.0f, fxTarget = p.fxEnabled ? 1.0f : 0.0f;
        const bool gate = p.mode == Mode::gate;

        float inPk[2] = { 0.0f, 0.0f }, outPk[2] = { 0.0f, 0.0f };
        bool anyEffect = false;

        for (int i = 0; i < numSamples; ++i)
        {
            float dryL = io[0][i], dryR = stereo ? io[1][i] : dryL;
            if (! std::isfinite (dryL)) dryL = 0.0f;
            if (! std::isfinite (dryR)) dryR = 0.0f;
            inPk[0] = std::max (inPk[0], std::abs (dryL));
            inPk[1] = std::max (inPk[1], std::abs (dryR));

            capture.write (dryL, dryR);
            const int64_t nowAbs = capture.written() - 1;

            // ---- position on the grid --------------------------------------------------------
            const double beatPos = (hostRunning ? t.ppq : freeBeat) + i * bps - offsetBeats;
            lastBeatPos = beatPos;
            const double rel = beatPos - phraseOrigin + 1.0e-9;          // exact boundaries: never a sample late
            const double phraseFloor = std::floor (rel / phraseBeats);
            const auto phrase = (int64_t) phraseFloor;
            const double inPhrase = std::max (0.0, rel - phraseFloor * phraseBeats);
            const int k = std::min (N - 1, (int) (inPhrase / sliceBeats));
            const double kStart = k * sliceBeats;
            const double age = (inPhrase - kStart) / bps;               // samples since this slice began

            if (! haveGrid || abrupt || phrase != lastPhrase || k != lastK)
            {
                const bool next = haveGrid && ! abrupt
                                  && ((phrase == lastPhrase && k == lastK + 1) || (phrase == lastPhrase + 1 && k == 0));
                const double beats = k == N - 1 ? std::max (0.25 * sliceBeats, phraseBeats - kStart) : sliceBeats;
                startSlice (phrase, k, N, nowAbs - (int64_t) age, beats / bps, age, p, ! next);
                haveGrid = true;
                abrupt = false;
                lastPhrase = phrase;
                lastK = k;
            }

            // ---- repeats, and the live voice (gated in Gate mode) -----------------------------
            float effL = 0.0f, effR = 0.0f;
            for (auto& v : voices)
                if (v.active)
                {
                    if (gate)
                        release (v, minFade);                   // repeats of the previous mode fade out
                    float vl = 0.0f, vr = 0.0f;
                    renderVoice (v, vl, vr);
                    effL += vl;
                    effR += vr;
                    anyEffect = true;
                }

            slewTo (depthS, depthT, paramStep);
            float baseTarget = 1.0f, pulseTarget = 0.0f, maxStep = 0.0f;
            if (gate)
            {
                baseTarget = 1.0f - depthS;                     // the closed level
                pulseTarget = depthS * gAccent * gateEnv ((float) age);   // the gate's pulses above it
                maxStep = 4.5f / gFade + 0.002f;
                anyEffect = anyEffect || depthS > 0.0f;
            }
            else
            {
                baseTarget = liveGainAt ((float) age, (float) curSliceLen);
                maxStep = 4.5f / curFade + 0.002f;
            }
            // intended fades are followed exactly; a jump (mode change, host jump, a boundary that
            // came out differently than predicted) becomes a 3 ms glide
            const float checkStep = sliceStarted ? 0.01f : maxStep;
            sliceStarted = false;
            const float liveGain = liveBase.next (baseTarget, checkStep, transition) + livePulse.next (pulseTarget, checkStep, transition);
            const float liveL = dryL * liveGain, liveR = dryR * liveGain;

            // ---- FX on the effect signal -----------------------------------------------------
            const float fxInL = gate ? dryL * livePulse.value : effL;
            const float fxInR = gate ? dryR * livePulse.value : effR;
            slewTo (fxGain, fxTarget, fxStep);
            slewTo (delayMixS, delayMixT, paramStep);
            slewTo (reverbMixS, reverbMixT, paramStep);
            slewTo (delayFbS, delayFbT, paramStep);
            slewTo (reverbSizeS, reverbSizeT, paramStep);
            slewTo (reverbDampS, reverbDampT, paramStep);
            float extraL = 0.0f, extraR = 0.0f;
            if (fxGain > 0.0f)
            {
                float a = fxInL, b = fxInR;
                if (delayMixS > 0.0f || delay.isRinging())
                    delay.process (a, b, delaySamples, delayFbS, delayMixS);
                if (reverbMixS > 0.0f || reverb.isRinging())
                    reverb.process (a, b, reverbSizeS, reverbDampS, reverbMixS);
                extraL = (a - fxInL) * fxGain;
                extraR = (b - fxInR) * fxGain;
                fxWasOn = true;
            }
            else if (fxWasOn)
            {
                delay.reset();          // the tails have faded out completely: never let stale ones return
                reverb.reset();
                fxWasOn = false;
            }

            const float wetL = liveL + effL + extraL;
            const float wetR = liveR + effR + extraR;

            // ---- mix and bypass --------------------------------------------------------------
            slewTo (mixSmoothed, mixTarget, mixStep);
            float outL = dryL + mixSmoothed * (wetL - dryL);
            float outR = dryR + mixSmoothed * (wetR - dryR);

            slewTo (bypassGain, bypassTarget, mixStep);
            if (bypassGain >= 1.0f)      { outL = dryL; outR = dryR; }
            else if (bypassGain > 0.0f)  { outL += bypassGain * (dryL - outL); outR += bypassGain * (dryR - outR); }

            if (! std::isfinite (outL)) outL = 0.0f;
            if (! std::isfinite (outR)) outR = 0.0f;
            io[0][i] = outL;
            if (stereo) io[1][i] = outR;
            outPk[0] = std::max (outPk[0], std::abs (outL));
            outPk[1] = std::max (outPk[1], std::abs (stereo ? outR : outL));
        }

        if (hostRunning)
            expectedPpq = t.ppq + numSamples * bps;
        else
            freeBeat += numSamples * bps;
        hostWasRunning = hostRunning;

        for (int ch = 0; ch < 2; ++ch)
        {
            EngineStatus::raise (status.inPeak[ch], stereo ? inPk[ch] : inPk[0]);
            EngineStatus::raise (status.outPeak[ch], outPk[ch]);
        }
        status.effectActive.store (anyEffect, std::memory_order_relaxed);
        status.step.store (std::max (0, lastK), std::memory_order_relaxed);
    }

private:
    //------------------------------------------------------------------ types --
    struct Decision
    {
        bool effect = false;            // a repeat plays in this slice (the live voice is muted)
        bool frozen = false;            // the repeat reads the frozen capture
        int64_t src = 0;                // first frame (live: absolute frame; frozen: index in the capture)
        int64_t srcLimit = 0;           // frozen: end of the selection (exclusive)
        bool reverse = false;
        int micro = 1;                  // micro-slices (Glitch)
        uint8_t map[16] {};             // micro-slice order
        float gain = 1.0f;
        float startDelay = 0.0f;        // Loose: samples before the repeat starts
        float playFraction = 1.0f;      // Tight: part of the slice that sounds
        bool gap = false;               // Glitch: the second half is silent
    };

    struct ColorState { float lp[2] { 0.0f, 0.0f }, held[2] { 0.0f, 0.0f }; int holdCount = 0; };

    struct Voice
    {
        bool active = false;
        Decision d;
        double length = 1.0;            // slice length in samples
        double age = 0.0;               // samples since the slice start
        float sinceStart = 0.0f;        // samples since the voice started (ramp for mid-slice starts)
        bool releasing = false;
        float releaseLeft = 0.0f, releaseLength = 1.0f;
        float fade = 24.0f;             // fade, shape and style are fixed when the repeat starts, so a
        Shape shape = Shape::hard;      // parameter change mid-repeat applies from the next one
        Style style = Style::sharp;
        ColorState color;
    };

    /** A gain that follows its target exactly, except after a jump: then it glides there over
        `samples` with a raised-cosine curve (no corner at either end). */
    struct Follower
    {
        float value = 1.0f, from = 1.0f;
        int left = 0, length = 1;

        void begin (int samples) noexcept
        {
            from = value;
            length = std::max (1, samples);
            left = length;
        }

        float next (float target, float maxStep, int samples) noexcept
        {
            if (left == 0 && std::abs (target - value) > maxStep)
                begin (samples);
            if (left > 0)
            {
                --left;
                value = from + (target - from) * smooth01 (1.0f - (float) left / (float) length);
            }
            else
                value = target;
            return value;
        }
    };

    static float clamp01 (float x) noexcept { return std::clamp (x, 0.0f, 1.0f); }
    static int clampIndex (int i) noexcept  { return std::clamp (i, 0, divisions::count - 1); }

    /** Moves `value` towards `target` by at most `step`, landing on it exactly. */
    static void slewTo (float& value, float target, float step) noexcept
    {
        const float d = target - value;
        if (std::abs (d) <= step) value = target;
        else value += d > 0.0f ? step : -step;
    }

    /** Deterministic hash -> [0, 1): the same slice always gets the same variation (spec section 7). */
    static float hash01 (int64_t a, int salt) noexcept
    {
        constexpr uint64_t k1 = 0x9E3779B97F4A7C15u, k2 = 0xBF58476D1CE4E5B9u, k3 = 0x94D049BB133111EBu;
        const auto saltBits = static_cast<uint64_t> (static_cast<int64_t> (salt) + 1);
        uint64_t z = static_cast<uint64_t> (a) * k1 + saltBits * k2;
        z ^= z >> 30; z *= k2;
        z ^= z >> 27; z *= k3;
        z ^= z >> 31;
        return (float) (z >> 40) * (1.0f / 16777216.0f);
    }

    static float smooth01 (float x) noexcept       // raised cosine 0..1, zero slope at both ends
    {
        if (x <= 0.0f) return 0.0f;
        if (x >= 1.0f) return 1.0f;
        return 0.5f - 0.5f * std::cos (x * 3.14159265f);
    }

    /** Equal-power fade-in 0..1 (fadeCurve(x)^2 + fadeCurve(1 - x)^2 = 1) whose slope is zero at both
        ends, so a fade has no corner: the spectral splatter falls off much faster than a plain sine. */
    static float fadeCurve (float x) noexcept
    {
        if (x <= 0.0f) return 0.0f;
        if (x >= 1.0f) return 1.0f;
        return std::sin (smooth01 (x) * 1.5707963f);
    }

    static float shapeCurve (Shape s, float x) noexcept
    {
        x = std::clamp (x, 0.0f, 1.0f);
        switch (s)
        {
            case Shape::rampUp:   return x;
            case Shape::rampDown: return 1.0f - x;
            case Shape::smooth:   return std::sin (x * 3.14159265f);
            case Shape::hard:     break;
        }
        return 1.0f;
    }

    //---------------------------------------------------------------- freeze --
    void handleFreeze (const EngineParams& p) noexcept
    {
        // Freeze needs 1 s of audio: a session loaded with Freeze on records 1 s first
        if (p.freeze && ! capture.isFrozen() && ! unfreezePending
            && (double) (capture.written() - capture.oldestLive()) >= 1.0 * sr)
            capture.freeze();

        if (! p.freeze && capture.isFrozen() && ! unfreezePending)
        {
            unfreezePending = true;                     // repeats of frozen audio fade out first
            for (auto& v : voices)
                if (v.active && v.d.frozen)
                    release (v, minFade);
        }
        if (unfreezePending)
        {
            bool busy = false;
            for (auto& v : voices)
                busy = busy || (v.active && v.d.frozen);
            if (! busy)
            {
                capture.unfreeze();
                unfreezePending = false;
            }
        }

        // selection (0..1 of the frozen capture) -> frames, at least 20 ms
        const auto len = capture.frozenLength();
        float a = clamp01 (std::min (p.selectionStart, p.selectionEnd));
        float b = clamp01 (std::max (p.selectionStart, p.selectionEnd));
        if (! std::isfinite (a) || ! std::isfinite (b)) { a = 0.0f; b = 1.0f; }
        const float minSpan = len > 0 ? (float) std::min (1.0, 0.02 * sr / (double) len) : 1.0f;
        if (b - a < minSpan)
        {
            const float c = std::clamp ((a + b) * 0.5f, minSpan * 0.5f, 1.0f - minSpan * 0.5f);
            a = c - minSpan * 0.5f;
            b = c + minSpan * 0.5f;
        }
        selStart = std::clamp<int64_t> ((int64_t) ((double) a * (double) len), 0, std::max<int64_t> (0, len - 1));
        selEnd = std::clamp<int64_t> ((int64_t) ((double) b * (double) len), selStart + 1, std::max<int64_t> (selStart + 1, len));
        status.playingFrozen.store (capture.isFrozen() && ! unfreezePending, std::memory_order_relaxed);
    }

    //---------------------------------------------------------- slice start --
    void startSlice (int64_t phrase, int k, int N, int64_t startAbs, double sliceLen, double age,
                     const EngineParams& p, bool jump) noexcept
    {
        if (phrase != curPhrase || jump)
        {
            curPhrase = phrase;
            for (auto& a : phraseStartAbs) a = -1;
        }
        // a slice entered at its first sample captures cleanly; one entered part-way (a jump) does not
        phraseStartAbs[(size_t) k] = (! jump || age < 1.0) ? startAbs : -1;

        const bool liveWasMuted = ! jump && curEffect && nextEffect;     // live silent at this boundary
        phraseSlices = N;
        curSliceLen = sliceLen;
        curSliceId = phrase * kMaxSlicesPerPhrase + k;
        curFade = std::max (1.0f, std::min (blockFade, (float) sliceLen * 0.5f));

        // the gate's shape for this slice (fixed for the slice: parameter changes apply from the next)
        float duty = 0.05f + 0.95f * clamp01 (p.length);
        if (p.style == Style::tight) duty *= 0.7f;
        gOpen = std::max (1.0f, duty * (float) sliceLen);
        gDelay = p.style == Style::loose ? hash01 (curSliceId, 13) * std::min ((float) (0.015 * sr), gOpen / 6.0f) : 0.0f;
        gFade = std::max (1.0f, std::min (blockFade, gOpen * 0.5f));
        gShape = p.shape;
        gAccent = p.style == Style::random ? 0.6f + 0.4f * hash01 (curSliceId, 14) : 1.0f;   // Random: accents

        sliceStarted = true;
        if (jump)
        {
            liveBase.begin ((int) minFade);
            livePulse.begin ((int) minFade);
        }
        cur = decide (curSliceId, k, startAbs, sliceLen, p);
        const bool lastInPhrase = k + 1 >= N;
        const Decision upcoming = decide (lastInPhrase ? (phrase + 1) * kMaxSlicesPerPhrase : curSliceId + 1,
                                          lastInPhrase ? 0 : k + 1, startAbs + (int64_t) sliceLen, sliceLen, p);
        prevEffect = liveWasMuted;
        curEffect = cur.effect;
        nextEffect = upcoming.effect;
        publishPattern (phrase, N, startAbs, sliceLen, p);
        status.sliceStartFrame.store (startAbs, std::memory_order_relaxed);

        // a clean boundary lets the previous repeat end by its own fade; a jump releases it now
        for (auto& v : voices)
            if (v.active && (jump || v.age < v.length - 1.0))
                release (v, minFade);

        if (cur.effect)
        {
            Voice* slot = nullptr;
            for (auto& v : voices)
                if (! v.active) { slot = &v; break; }
            if (slot == nullptr)
            {
                slot = &voices[0];                     // all busy (rare): replace the one closest to silent
                for (auto& v : voices)
                    if (v.releasing && (! slot->releasing || v.releaseLeft < slot->releaseLeft))
                        slot = &v;
            }
            *slot = Voice {};
            slot->active = true;
            slot->d = cur;
            slot->length = sliceLen;
            slot->age = jump ? std::max (0.0, std::floor (age)) : 0.0;
            slot->sinceStart = (jump && age >= 1.0) ? 0.0f : 1.0e9f;
            slot->fade = blockFade;
            slot->shape = p.shape;
            slot->style = p.style;
            status.sourceStart.store (cur.frozen ? -1 : cur.src, std::memory_order_relaxed);
            status.sourceEnd.store (cur.frozen ? -1 : cur.src + (int64_t) (sliceLen * cur.playFraction), std::memory_order_relaxed);
        }
        else
        {
            status.sourceStart.store (-1, std::memory_order_relaxed);
            status.sourceEnd.store (-1, std::memory_order_relaxed);
        }
    }

    /** The capture slice whose audio a repeat at slice k plays (live material). */
    static int captureSliceOf (int k, const EngineParams& p) noexcept
    {
        if (p.mode != Mode::repeat)
            return 0;
        const int beatSlices = beatSlicesOf (p);
        return k - k % beatSlices;
    }

    static int beatSlicesOf (const EngineParams& p) noexcept
    {
        return ! p.sync ? 4 : std::max (2, (int) std::lround (1.0 / divisions::table[clampIndex (p.rateIndex)].beats));
    }

    /** The plan of the whole phrase for the UI (atomics; a few decide() calls per slice). */
    void publishPattern (int64_t phrase, int N, int64_t startAbs, double sliceLen, const EngineParams& p) noexcept
    {
        uint64_t rep[2] = { 0, 0 }, cap[2] = { 0, 0 };
        const bool gating = p.mode == Mode::gate && p.amount > 0.0f;
        const bool frozen = capture.isFrozen() && ! unfreezePending && capture.frozenLength() > 0;
        for (int s = 0; s < N && s < kMaxSlicesPerPhrase; ++s)
        {
            if (gating)
                rep[s >> 6] |= 1ull << (s & 63);
            else if (decide (phrase * kMaxSlicesPerPhrase + s, s, startAbs, sliceLen, p).effect)
            {
                rep[s >> 6] |= 1ull << (s & 63);
                if (! frozen)
                {
                    const int c = captureSliceOf (s, p);
                    cap[c >> 6] |= 1ull << (c & 63);
                }
            }
        }
        for (int w = 0; w < 2; ++w)
        {
            status.repeatMask[w].store (rep[w], std::memory_order_relaxed);
            status.captureMask[w].store (cap[w], std::memory_order_relaxed);
        }
        status.gating.store (gating, std::memory_order_relaxed);
    }

    /** What slice `sliceId` (position k in its phrase) does. */
    Decision decide (int64_t sliceId, int k, int64_t startAbs, double sliceLen, const EngineParams& p) const noexcept
    {
        Decision d;
        if (p.mode == Mode::gate || ! (p.amount > 0.0f))
            return d;

        const int N = phraseSlices;
        const bool frozen = capture.isFrozen() && ! unfreezePending && capture.frozenLength() > 0;
        const int minWindow = std::min (N, frozen ? 1 : 2);
        const int window = std::clamp ((int) std::lround (clamp01 (p.length) * (float) N), minWindow, N);
        if (k >= window)
            return d;

        const bool repeatMode = p.mode == Mode::repeat;
        const int beatSlices = beatSlicesOf (p);

        // candidate slices for a repeat, and this slice's place among them
        int count = 0, j = -1;
        if (frozen)
        {
            count = window;
            j = k;
        }
        else if (repeatMode)
        {
            if (k % beatSlices == 0)
                return d;                                      // capture slice: plays live
            count = window - (window + beatSlices - 1) / beatSlices;
            j = k - k / beatSlices - 1;
        }
        else
        {
            if (k == 0)
                return d;                                      // capture slice: plays live
            count = window - 1;
            j = k - 1;
        }
        if (count <= 0 || j < 0 || j >= count)
            return d;

        const int M = std::clamp ((int) std::lround (clamp01 (p.amount) * (float) count), 1, count);
        if ((int64_t) (j + 1) * M / count <= (int64_t) j * M / count)
            return d;                                          // not selected by Amount
        if (p.style == Style::random && hash01 (sliceId, 11) < 0.15f)
            return d;                                          // Random: some repeats are skipped

        d.effect = true;
        d.reverse = p.mode == Mode::reverse;
        d.playFraction = p.style == Style::tight ? 0.7f : 1.0f;
        const double play = std::max (1.0, sliceLen * d.playFraction);
        if (p.style == Style::random)
            d.gain = std::pow (10.0f, -4.0f * hash01 (sliceId, 12) / 20.0f);
        if (p.style == Style::loose)
            d.startDelay = (float) (hash01 (sliceId, 13) * std::min (0.015 * sr, play / 6.0));

        // material
        if (frozen)
        {
            d.frozen = true;
            const int64_t regionLen = std::max<int64_t> (1, selEnd - selStart);
            int64_t start = selStart;
            if (repeatMode)
            {
                const int64_t group = k / beatSlices;
                const int64_t room = std::max<int64_t> (1, regionLen - (int64_t) play);
                start = selStart + (group * (int64_t) play) % room;
            }
            d.src = start;
            d.srcLimit = selEnd;
        }
        else
        {
            // the capture slice of this window (Repeat: of this beat). Reading [src, src + play) in any
            // order must only touch frames written before they are read and still held at the end.
            const int captureK = captureSliceOf (k, p);
            int64_t anchor = phraseStartAbs[(size_t) std::clamp (captureK, 0, kMaxSlicesPerPhrase - 1)];
            const auto playLen = (int64_t) std::ceil (play);
            const int64_t latest = startAbs + 1 - playLen;
            const int64_t oldestSafe = std::max (capture.oldestLive(), startAbs + playLen - capture.capacity() + 1);
            if (anchor < 0 || anchor > latest || anchor < oldestSafe)
                anchor = latest;                               // fall back to the audio just before
            d.src = anchor;                                    // (< 0 at the very start: reads silence)
        }

        // Glitch variations
        if (p.mode == Mode::glitch && hash01 (sliceId, 21) < clamp01 (p.intensity))
        {
            const float in = clamp01 (p.intensity);
            switch (p.glitchType)
            {
                case GlitchType::pixel:
                    d.micro = 2 + (int) std::lround (in * 6.0f);          // 2..8 micro-slices, all micro 0
                    for (auto& m : d.map) m = 0;
                    break;
                case GlitchType::burst:
                {
                    d.micro = 8;
                    const int burst = 2 + (int) std::lround (in * 4.0f);  // 2..6 of 8 micro-slices burst
                    for (int m = 0; m < 16; ++m) d.map[m] = (uint8_t) (m < burst ? 0 : m);
                    break;
                }
                case GlitchType::shuffle:
                    d.micro = in > 0.5f ? 8 : 4;
                    for (int m = 0; m < 16; ++m) d.map[m] = (uint8_t) m;
                    for (int m = d.micro - 1; m > 0; --m)                 // Fisher-Yates, seeded per slice
                    {
                        const int r = std::min (m, (int) (hash01 (sliceId, 30 + m) * (float) (m + 1)));
                        std::swap (d.map[m], d.map[r]);
                    }
                    break;
            }
            d.gap = hash01 (sliceId, 22) < 0.25f * in;
        }
        return d;
    }

    //---------------------------------------------------------------- voices --
    static void release (Voice& v, float samples) noexcept
    {
        if (v.releasing)
            return;
        v.releasing = true;
        v.releaseLength = std::max (1.0f, samples);
        v.releaseLeft = v.releaseLength;
    }

    float readSource (const Decision& d, int ch, double offset) const noexcept
    {
        const auto o = (int64_t) offset;
        if (d.frozen)
            return capture.readFrozen (ch, std::clamp<int64_t> (d.src + o, 0, std::max<int64_t> (0, d.srcLimit - 1)));
        return capture.readLive (ch, d.src + o);
    }

    void renderVoice (Voice& v, float& outL, float& outR) noexcept
    {
        const auto& d = v.d;
        double play = std::max (1.0, v.length * d.playFraction);
        if (d.frozen)
            play = std::min (play, (double) std::max<int64_t> (1, d.srcLimit - d.src));   // selection shorter than the slice
        const double a = v.age - d.startDelay;
        float env = 0.0f;
        double srcOffset = 0.0;

        if (a >= 0.0 && a < play)
        {
            env = shapeCurve (v.shape, (float) (a / play));
            const float f = std::max (1.0f, std::min (v.fade, (float) (play * 0.5)));
            env *= fadeCurve ((float) a / f) * fadeCurve ((float) (play - a) / f);

            srcOffset = a;
            if (d.micro > 1)
            {
                const double ml = play / d.micro;
                const int m = std::clamp ((int) (a / ml), 0, d.micro - 1);
                const double within = a - m * ml;
                srcOffset = d.map[m] * ml + within;
                const float mf = std::max (1.0f, std::min ((float) (ml * 0.5), minFade));
                env *= fadeCurve ((float) within / mf) * fadeCurve ((float) (ml - within) / mf);
            }
            if (d.gap && a >= play * 0.5)
                env *= 1.0f - fadeCurve ((float) ((a - play * 0.5) / minFade));
            if (d.reverse)
                srcOffset = std::max (0.0, play - 1.0 - srcOffset);
        }

        env *= d.gain;
        if (v.sinceStart < minFade)
            env *= fadeCurve (v.sinceStart / minFade);
        if (v.releasing)
        {
            env *= fadeCurve (v.releaseLeft / v.releaseLength);     // equal-power, no corners
            v.releaseLeft -= 1.0f;
        }

        if (std::abs (env) > 0.0f)
        {
            outL = readSource (d, 0, srcOffset) * env;
            outR = readSource (d, 1, srcOffset) * env;
        }
        colorEffect (v.color, outL, outR, v.style);

        v.age += 1.0;
        v.sinceStart += 1.0f;
        if ((v.releasing && v.releaseLeft <= 0.0f) || v.age > v.length + d.startDelay + 2.0)
            v.active = false;
    }

    /** Live voice gain inside the current slice: equal-power crossfades where a repeat meets live audio. */
    float liveGainAt (float age, float sliceLen) const noexcept
    {
        if (! curEffect)
            return 1.0f;
        const float f = curFade;
        float g = 0.0f;
        if (! prevEffect)
            g = std::max (g, fadeCurve (1.0f - age / f));                    // live -> repeat
        if (! nextEffect)
            g = std::max (g, fadeCurve ((age - (sliceLen - f)) / f));         // repeat -> live
        return g;
    }

    /** Gate opening (0..1) at `age` from the values fixed at the slice start. */
    float gateEnv (float age) const noexcept
    {
        const float a = age - gDelay;
        if (a < 0.0f || a >= gOpen)
            return 0.0f;
        return shapeCurve (gShape, a / gOpen) * fadeCurve (a / gFade) * fadeCurve ((gOpen - a) / gFade);
    }

    /** Tape and Digital Glitch colour a repeat (never the live voice). */
    void colorEffect (ColorState& c, float& l, float& r, Style style) const noexcept
    {
        float* s[2] = { &l, &r };
        if (style == Style::tape)
        {
            for (int ch = 0; ch < 2; ++ch)
            {
                c.lp[ch] += tapeCoef * (*s[ch] - c.lp[ch]);
                *s[ch] = std::tanh (kTapeDrive * c.lp[ch]) / kTapeDrive;      // unity gain for small signals
            }
        }
        else if (style == Style::digital)
        {
            if (c.holdCount <= 0)
            {
                for (int ch = 0; ch < 2; ++ch)
                    c.held[ch] = std::round (*s[ch] * 512.0f) / 512.0f;       // 10 bit
                c.holdCount = 3;                                               // held for 3 samples
            }
            --c.holdCount;
            l = c.held[0];
            r = c.held[1];
        }
    }

    double sr = 44100.0;
    CaptureBuffer capture;
    StereoDelay delay;
    Reverb reverb;
    Voice voices[3];
    Decision cur;
    bool curEffect = false, prevEffect = false, nextEffect = false, sliceStarted = false, haveMode = false;
    Mode lastMode = Mode::stutter;
    float minFade = 132.0f, tapeCoef = 0.5f, blockFade = 24.0f, curFade = 24.0f;
    float gOpen = 1.0f, gDelay = 0.0f, gFade = 1.0f, gAccent = 1.0f;
    Shape gShape = Shape::hard;

    double freeBeat = 0.0, lastBeatPos = 0.0, expectedPpq = 0.0, phraseOrigin = 0.0, curSliceLen = 1.0;
    bool haveGrid = false, hostWasRunning = false, unfreezePending = false, fxWasOn = false, lastSync = true;
    int lastRateIndex = -1, lastK = -1, phraseSlices = 8;
    int64_t lastPhrase = 0, curPhrase = 0, curSliceId = 0;
    int64_t phraseStartAbs[kMaxSlicesPerPhrase] {};
    int64_t selStart = 0, selEnd = 1;

    Follower liveBase, livePulse;
    float mixSmoothed = -1.0f, bypassGain = -1.0f, fxGain = 0.0f, depthS = -1.0f;
    float delayMixS = -1.0f, reverbMixS = -1.0f, delayFbS = -1.0f, reverbSizeS = -1.0f, reverbDampS = -1.0f;
};
}
