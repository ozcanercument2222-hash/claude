#pragma once

#include <cstddef>

/** Choice lists shared by the parameters, the DSP and the UI (JUCE-free).

    ORDER AND COUNT ARE PERMANENT after release: a choice parameter is stored as index / (count - 1),
    so inserting, removing or reordering an entry would move saved automation. Each enum value is the
    index of its name in the matching array.
*/
namespace vex::choices
{
    enum class Mode { stutter, repeat, gate, reverse, glitch };
    inline constexpr const char* modeNames[] = { "Stutter", "Repeat", "Gate", "Reverse", "Glitch" };

    enum class Style { sharp, smooth, tight, loose, tape, random, digital };
    inline constexpr const char* styleNames[] = { "Sharp", "Smooth", "Tight", "Loose", "Tape", "Random", "Digital Glitch" };

    enum class Shape { hard, rampUp, rampDown, smooth };
    inline constexpr const char* shapeNames[] = { "Hard", "Ramp Up", "Ramp Down", "Smooth" };

    enum class GlitchType { pixel, burst, shuffle };
    inline constexpr const char* glitchTypeNames[] = { "Pixel", "Burst", "Shuffle" };

    template <typename T, std::size_t N> constexpr int countOf (const T (&)[N]) { return (int) N; }

    inline constexpr int numModes       = countOf (modeNames);
    inline constexpr int numStyles      = countOf (styleNames);
    inline constexpr int numShapes      = countOf (shapeNames);
    inline constexpr int numGlitchTypes = countOf (glitchTypeNames);
}
