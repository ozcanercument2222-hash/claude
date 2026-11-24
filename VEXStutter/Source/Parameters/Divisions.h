#pragma once

/** Musical divisions shared by the Rate and Delay Rate parameters and by the DSP (JUCE-free).

    The list is ordered by note length, longest first, so an automation sweep or the previous / next
    arrows always move monotonically. The ORDER AND COUNT ARE PERMANENT after release: a choice
    parameter is stored as index / (count - 1).
*/
namespace vex::divisions
{
    struct Division
    {
        const char* name;
        double beats;           // length in quarter notes (1/4 = 1 beat)
    };

    inline constexpr Division table[] =
    {
        { "1/2D",  3.0 },
        { "1/2",   2.0 },
        { "1/4D",  1.5 },
        { "1/2T",  4.0 / 3.0 },
        { "1/4",   1.0 },
        { "1/8D",  0.75 },
        { "1/4T",  2.0 / 3.0 },
        { "1/8",   0.5 },
        { "1/16D", 0.375 },
        { "1/8T",  1.0 / 3.0 },
        { "1/16",  0.25 },
        { "1/32D", 0.1875 },
        { "1/16T", 1.0 / 6.0 },
        { "1/32",  0.125 },
        { "1/32T", 1.0 / 12.0 },
    };

    inline constexpr int count = (int) (sizeof (table) / sizeof (table[0]));
    inline constexpr int defaultIndex = 7;     // 1/8 (specification section 13)

    /** Length of a division in seconds at a tempo; invalid input falls back to 120 BPM and 1/8. */
    inline double seconds (int index, double bpm) noexcept
    {
        const auto& d = table[(index >= 0 && index < count) ? index : defaultIndex];
        const double tempo = (bpm > 0.0 && bpm < 1000.0) ? bpm : 120.0;
        return d.beats * 60.0 / tempo;
    }
}
