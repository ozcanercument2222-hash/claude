#pragma once
//==============================================================================
//  Notalar, dizi tipleri ve "en yakin izinli nota" aramasi
//==============================================================================
#include <cmath>
#include <cstdint>
#include <initializer_list>

namespace vstm
{
enum ScaleType
{
    kChromatic = 0,
    kMajor,
    kMinor,
    kHarmonicMinor,
    kMelodicMinor,
    kMajorPentatonic,
    kMinorPentatonic,
    kBlues,
    kDorian,
    kPhrygian,
    kLydian,
    kMixolydian,
    kHicaz,
    kNumScales
};

inline const char* scaleName (int s) noexcept
{
    static const char* names[] = { "Chromatic", "Major", "Minor", "Harmonic Minor", "Melodic Minor",
                                   "Major Pentatonic", "Minor Pentatonic", "Blues", "Dorian",
                                   "Phrygian", "Lydian", "Mixolydian", "Hicaz" };
    return (s >= 0 && s < kNumScales) ? names[s] : "Chromatic";
}

inline const char* noteName (int pitchClass) noexcept
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return names[((pitchClass % 12) + 12) % 12];
}

/** Bit i set = the note i semitones above the tonic belongs to the scale. */
inline uint16_t scaleMask (int s) noexcept
{
    auto make = [] (std::initializer_list<int> steps)
    {
        uint16_t m = 0;
        for (int st : steps) m = (uint16_t) (m | (1u << st));
        return m;
    };

    switch (s)
    {
        case kMajor:            return make ({ 0, 2, 4, 5, 7, 9, 11 });
        case kMinor:            return make ({ 0, 2, 3, 5, 7, 8, 10 });
        case kHarmonicMinor:    return make ({ 0, 2, 3, 5, 7, 8, 11 });
        case kMelodicMinor:     return make ({ 0, 2, 3, 5, 7, 9, 11 });
        case kMajorPentatonic:  return make ({ 0, 2, 4, 7, 9 });
        case kMinorPentatonic:  return make ({ 0, 3, 5, 7, 10 });
        case kBlues:            return make ({ 0, 3, 5, 6, 7, 10 });
        case kDorian:           return make ({ 0, 2, 3, 5, 7, 9, 10 });
        case kPhrygian:         return make ({ 0, 1, 3, 5, 7, 8, 10 });
        case kLydian:           return make ({ 0, 2, 4, 6, 7, 9, 11 });
        case kMixolydian:       return make ({ 0, 2, 4, 5, 7, 9, 10 });
        case kHicaz:            return make ({ 0, 1, 4, 5, 7, 8, 10 });
        default:                return 0x0fff;
    }
}

inline bool isAllowed (int midiNote, int key, uint16_t mask) noexcept
{
    const int degree = (((midiNote - key) % 12) + 12) % 12;
    return (mask >> degree) & 1u;
}

/** Nearest note (integer MIDI) to `midi` that belongs to the scale. */
inline int nearestAllowedNote (float midi, int key, uint16_t mask) noexcept
{
    if (mask == 0)
        mask = 0x0fff;

    const int centre = (int) std::lround (midi);
    int best = centre;
    float bestDist = 1.0e9f;
    for (int n = centre - 7; n <= centre + 7; ++n)
    {
        if (! isAllowed (n, key, mask))
            continue;
        const float d = std::abs (midi - (float) n);
        if (d < bestDist)
        {
            bestDist = d;
            best = n;
        }
    }
    return best;
}
} // namespace vstm
