#pragma once

/** Parameter IDs of VEX Stutter.

    PERMANENT once a build has been released: hosts store automation, presets and sessions by these
    strings. Never rename an ID, never reuse one for a different meaning, never change its type or
    the order / count of a choice list. New controls get new IDs (docs/ARCHITECTURE.md, section 3).
*/
namespace vex::ids
{
    // host bypass; the power icon shows the same state inverted
    inline constexpr const char* bypass        = "bypass";

    // core
    inline constexpr const char* mode          = "mode";
    inline constexpr const char* amount        = "amount";
    inline constexpr const char* sync          = "sync";
    inline constexpr const char* rate          = "rate";            // musical division (sync on)
    inline constexpr const char* rateMs        = "rate_ms";         // free time (sync off)
    inline constexpr const char* length        = "length";
    inline constexpr const char* style         = "style";
    inline constexpr const char* shape         = "shape";
    inline constexpr const char* smoothness    = "smoothness";
    inline constexpr const char* offsetMs      = "offset_ms";
    inline constexpr const char* mix           = "mix";
    inline constexpr const char* glitchType    = "glitch_type";
    inline constexpr const char* intensity     = "intensity";
    inline constexpr const char* freeze        = "freeze";

    // FX chain (effect signal only)
    inline constexpr const char* fxEnabled     = "fx_enabled";
    inline constexpr const char* delaySync     = "delay_sync";
    inline constexpr const char* delayRate     = "delay_rate";
    inline constexpr const char* delayTimeMs   = "delay_time_ms";
    inline constexpr const char* delayFeedback = "delay_feedback";
    inline constexpr const char* delayMix      = "delay_mix";
    inline constexpr const char* reverbSize    = "reverb_size";
    inline constexpr const char* reverbDamping = "reverb_damping";
    inline constexpr const char* reverbMix     = "reverb_mix";

    /** Every ID, in the order the parameters are created (the host's parameter order). */
    inline constexpr const char* all[] = { bypass, mode, amount, sync, rate, rateMs, length, style, shape, smoothness,
                                           offsetMs, mix, glitchType, intensity, freeze, fxEnabled, delaySync, delayRate,
                                           delayTimeMs, delayFeedback, delayMix, reverbSize, reverbDamping, reverbMix };
    inline constexpr int count = (int) (sizeof (all) / sizeof (all[0]));
}
