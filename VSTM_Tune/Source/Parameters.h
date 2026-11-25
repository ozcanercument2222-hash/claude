#pragma once
//==============================================================================
//  Parametre kimlikleri, araliklari ve host'ta gorunen deger yazilari
//
//  Kimlikler (ID) hic degismez; adlar ve deger yazilari arayuz dilindedir (Lang.h).
//  Yazilan deger her dilde okunur (or. "Robotik 40%" Ingilizce arayuzde de gecerli).
//==============================================================================
#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/Scales.h"
#include "Lang.h"

namespace vstm::ids
{
inline const juce::String key        { "key" };
inline const juce::String scale      { "scale" };
inline const juce::String correction { "correction" };
inline const juce::String retune     { "retune" };
inline const juce::String humanize   { "humanize" };
inline const juce::String robotism   { "robotism" };
inline const juce::String vibrato    { "vibscale" };
inline const juce::String formant    { "formant" };
inline const juce::String transpose  { "transpose" };
inline const juce::String mix        { "mix" };
inline const juce::String bypass     { "bypass" };
inline const juce::String realtime   { "realtime" };
inline const juce::String range      { "range" };
// added after 1.0 (version hint 2): appended after the 1.0 parameters, 1.0 IDs and order unchanged
inline const juce::String customScale { "customscale" };
inline const juce::String adaptive    { "adaptive" };
inline const juce::String tuningRef   { "tuningref" };     // 1.5 (version hint 3): A4 reference in Hz
inline const juce::String protectCons { "protectcons" };   // 1.5 (version hint 3): consonants keep their spectrum under Formant
inline const juce::String formantFollow { "formantfollow" }; // 1.5 (version hint 3): the formant moves with the transpose
inline const juce::String transition  { "transition" };    // 1.5 (version hint 3): Note Transition (Auto or a time)
inline const juce::String tolerance   { "tolerance" };     // 1.5 (version hint 3): tolerance around the target
inline const juce::String confidence  { "confidence" };    // 1.5 (version hint 3): confidence threshold
inline const juce::String synthVib    { "synthvib" };      // 1.5 (version hint 3): synthetic vibrato
inline const juce::String octave      { "octave" };        // 1.5 (version hint 3): octave shift
inline const juce::String midiTarget  { "miditarget" };    // 1.5 (version hint 3): held MIDI notes are the targets
inline const juce::String midiOut     { "midiout" };       // 1.5 (version hint 3): the target notes are sent as MIDI
inline const juce::String midiOctave  { "midioctave" };    // 1.5 (version hint 3): MIDI target: octave as played (perform)
inline const juce::String tuningSys   { "tuningsys" };     // 1.5 (version hint 3): tuning system (roadmap 45)
inline const juce::String scaleTr     { "scaletranspose" }; // 1.5 (version hint 3): diatonic transpose (roadmap 41)
inline const juce::String attack      { "attack" };        // 1.5 (version hint 3): correction attack (roadmap 27)
inline const juce::String release     { "release" };       // 1.5 (version hint 3): correction release (roadmap 27)
inline const juce::String autoCharacter { "autochar" };    // 1.6 (version hint 4): ROBOTISM drives retune / humanize / vibrato
/** Custom scale note i = i semitones above the key (0 = the key itself). */
inline juce::String scaleNote (int i)  { return "scalenote" + juce::String (i); }
inline juce::String scaleBypass (int i) { return "scalebypass" + juce::String (i); }   // 1.5 (version hint 3)
inline juce::String tuneCents (int i)   { return "tunecents" + juce::String (i); }     // 1.5 (version hint 3): custom tuning
}

namespace vstm::params
{
inline juce::StringArray keyChoices()
{
    juce::StringArray s { i18n::tr ("Auto") };
    for (int i = 0; i < 12; ++i)
        s.add (noteName (i));
    return s;
}

inline juce::StringArray scaleChoices()
{
    juce::StringArray s;
    for (int i = 0; i < kNumScales; ++i)
        s.add (i18n::tr (scaleName (i)));
    return s;
}

/** Note Transition choices (1.5): index 0 = Auto (follows ROBOTISM), then fixed glide times. */
inline constexpr float kTransitionMs[] = { -1.0f, 0.0f, 20.0f, 50.0f, 100.0f, 200.0f };
inline juce::StringArray transitionChoices()
{
    juce::StringArray s { i18n::tr ("Auto") };
    for (int i = 1; i < (int) std::size (kTransitionMs); ++i)
        s.add (juce::String ((int) kTransitionMs[i]) + " ms");
    return s;
}

/** 1.6 Auto Character: the values ROBOTISM (0 - 100 %) gives the managed controls while Auto Character is on.
    One curve per control, not a straight line, and exactly the factory defaults at 50 % (15 ms, 35 %, 100 %), so
    switching it on at the default setting changes nothing. Robotic side (0 - 50 %): the retune goes to 0 ms along
    a square (the hard region stays wide), humanize fades out between 50 % and 20 %, the vibrato is reduced to 60 %
    at 0 % (the robotic sound holds still). Natural side (50 - 100 %): a gently slower retune (up to 25 ms) and more
    humanize on long notes (up to 50 %); the vibrato is kept as sung (the 1.6 engine already keeps the vibrato and
    the singer's transitions there; a slower retune on top would leave notes off pitch). Formant, transpose and mix
    are not managed (a character control must not change the voice's timbre or pitch range). Deterministic, no
    state: the same ROBOTISM always gives the same values. */
struct CharacterValues { float retuneMs, humanizePct, vibratoPct; };
inline CharacterValues characterFor (float robotismPct) noexcept
{
    const float r = juce::jlimit (0.0f, 1.0f, std::isfinite (robotismPct) ? robotismPct * 0.01f : 0.5f);
    auto smooth = [] (float a, float b, float x) { const float t = juce::jlimit (0.0f, 1.0f, (x - a) / (b - a)); return t * t * (3.0f - 2.0f * t); };
    CharacterValues v {};
    if (r <= 0.5f)
    {
        const float k = r / 0.5f;
        v.retuneMs = 15.0f * k * k;
        v.humanizePct = 35.0f * smooth (0.2f, 0.5f, r);
        v.vibratoPct = 60.0f + 40.0f * smooth (0.0f, 0.5f, r);
    }
    else
    {
        const float k = (r - 0.5f) / 0.5f;
        v.retuneMs = 15.0f + 10.0f * std::pow (k, 1.5f);
        v.humanizePct = 35.0f + 15.0f * k;
        v.vibratoPct = 100.0f;
    }
    return v;
}

/** 1.5 (roadmap 27): correction attack (Auto = follows ROBOTISM, as before) and release (Off = as before). */
inline constexpr float kAttackMs[] = { -1.0f, 0.0f, 10.0f, 25.0f, 50.0f, 100.0f, 200.0f };
inline constexpr float kReleaseMs[] = { 0.0f, 50.0f, 100.0f, 200.0f, 400.0f };
inline juce::StringArray attackChoices()
{
    juce::StringArray s { i18n::tr ("Auto") };
    for (int i = 1; i < (int) std::size (kAttackMs); ++i)
        s.add (juce::String ((int) kAttackMs[i]) + " ms");
    return s;
}
inline juce::StringArray releaseChoices()
{
    juce::StringArray s { i18n::tr ("Off") };
    for (int i = 1; i < (int) std::size (kReleaseMs); ++i)
        s.add (juce::String ((int) kReleaseMs[i]) + " ms");
    return s;
}

/** 1.5 option lists (index 0 = off / no change, except the octave, whose default is the middle). */
inline constexpr float kToleranceCt[]  = { 0.0f, 5.0f, 10.0f, 15.0f, 25.0f, 35.0f, 50.0f };
inline constexpr float kConfidence[]   = { 0.0f, 0.5f, 0.6f, 0.7f, 0.8f };
inline constexpr float kSynthVibRate[] = { 0.0f, 5.0f, 5.5f, 6.0f };      // Off, Light, Medium, Wide
inline constexpr float kSynthVibDepth[] = { 0.0f, 15.0f, 30.0f, 50.0f };
inline juce::StringArray toleranceChoices()
{
    juce::StringArray s { i18n::tr ("Off") };
    for (int i = 1; i < (int) std::size (kToleranceCt); ++i) s.add (juce::String ((int) kToleranceCt[i]) + " ct");
    return s;
}
inline juce::StringArray confidenceChoices()
{
    juce::StringArray s { i18n::tr ("Off") };
    for (int i = 1; i < (int) std::size (kConfidence); ++i) s.add (juce::String (kConfidence[i], 2));
    return s;
}
// 1.5 (roadmap 45): tuning systems, cents per degree from the key's tonic (12-TET = 0). Just: 5-limit
// (1/1 16/15 9/8 6/5 5/4 4/3 45/32 3/2 8/5 5/3 9/5 15/8); Pythagorean: 3-limit (256/243 9/8 32/27 81/64 4/3
// 729/512 3/2 128/81 27/16 16/9 243/128); quarter-tone maqams (24-TET): Rast on the key = major with the
// 3rd and 7th 50 ct flat, Bayati on the key = the 2nd 50 ct sharp (use with a scale that has those degrees,
// e.g. Major / Phrygian). Custom: the 12 tunecents parameters.
enum TuningSystem { kEqualTuning = 0, kJustTuning, kPythagoreanTuning, kRastTuning, kBayatiTuning, kCustomTuning, kNumTunings };
inline constexpr float kTuningCents[kCustomTuning][12] = {
    {   0.0f,   0.0f,  0.0f,   0.0f,   0.0f,  0.0f,   0.0f,  0.0f,   0.0f,   0.0f,   0.0f,   0.0f },
    {   0.0f,  11.73f, 3.91f, 15.64f, -13.69f, -1.96f, -9.78f, 1.96f, 13.69f, -15.64f, 17.60f, -11.73f },
    {   0.0f,  -9.78f, 3.91f, -5.87f,   7.82f, -1.96f, 11.73f, 1.96f, -7.82f,   5.87f, -3.91f,   9.78f },
    {   0.0f,   0.0f,  0.0f,   0.0f, -50.0f,  0.0f,   0.0f,  0.0f,   0.0f,   0.0f,   0.0f, -50.0f },
    {   0.0f,  50.0f,  0.0f,   0.0f,   0.0f,  0.0f,   0.0f,  0.0f,   0.0f,   0.0f,   0.0f,   0.0f },
};
inline juce::StringArray tuningChoices()
{
    return { "Equal", "Just", "Pythagorean", "Rast (quarter-tone)", "Bayati (quarter-tone)", "Custom" };
}

inline juce::StringArray synthVibChoices()
{
    return { i18n::tr ("Off"), i18n::tr ("Light") + " (5 Hz, 15 ct)", i18n::tr ("Medium") + " (5.5 Hz, 30 ct)", i18n::tr ("Wide") + " (6 Hz, 50 ct)" };
}
inline juce::StringArray octaveChoices() { return { "-2", "-1", "0", "+1", "+2" }; }
/** 1.5 (roadmap 41): diatonic transpose, -7 .. +7 steps of the scale (index 7 = 0). */
inline juce::StringArray scaleStepChoices()
{
    juce::StringArray c;
    for (int s = -7; s <= 7; ++s)
        c.add (s == 0 ? juce::String ("0") : (s > 0 ? "+" : "") + juce::String (s));
    return c;
}

inline juce::StringArray rangeChoices()
{
    return { i18n::tr ("Soprano / Alto"), i18n::tr ("Tenor / Mid"), i18n::tr ("Bass / Baritone") };
}

/** Interval names of the custom scale notes (above the key): 1, b2, 2, b3, 3, 4, #4, 5, b6, 6, b7, 7. */
inline const char* degreeName (int i)
{
    static const char* names[] = { "1", "b2", "2", "b3", "3", "4", "#4", "5", "b6", "6", "b7", "7" };
    return names[((i % 12) + 12) % 12];
}

/** The custom scale starts out as the major scale. */
inline bool customNoteDefault (int i) { return ((scaleMask (kMajor) >> ((i % 12 + 12) % 12)) & 1u) != 0; }

//------------------------------------------------------------------ text
inline juce::String percentText (float v)             { return juce::String (juce::roundToInt (v)) + "%"; }

inline juce::String msText (float v)
{
    if (v < 9.95f)
        return juce::String (v, 1) + " ms";
    return juce::String (juce::roundToInt (v)) + " ms";
}

inline juce::String robotismText (float v)
{
    const int amount = juce::roundToInt (std::abs (v - 50.0f) * 2.0f);
    if (amount == 0)
        return i18n::tr ("Balanced");
    return i18n::tr (v < 50.0f ? "Robotic" : "Human") + " " + juce::String (amount) + "%";
}

inline float robotismFromText (const juce::String& t)
{
    const auto lower = t.toLowerCase();
    const float n = (float) lower.retainCharacters ("0123456789.").getDoubleValue();
    auto says = [&lower] (const char* key)                 // the word in any UI language
    {
        for (const auto& w : i18n::allTexts (key))
            if (w.isNotEmpty() && lower.contains (w.toLowerCase()))
                return true;
        return false;
    };
    if (says ("Balanced") || lower.contains ("deng") || lower.contains ("bal")) return 50.0f;
    if (says ("Robotic") || lower.contains ("rob"))                             return 50.0f - n * 0.5f;
    if (says ("Human") || lower.contains ("ns") || lower.contains ("hum"))      return 50.0f + n * 0.5f;
    return n;
}

inline juce::String formantText (float v)
{
    if (std::abs (v) < 0.05f)
        return "0.0";
    return (v > 0.0f ? "+" : "") + juce::String (v, 1);
}

inline juce::String semitoneText (float v)
{
    const int st = juce::roundToInt (v);
    return (st > 0 ? "+" : "") + juce::String (st) + " st";
}

inline float numberFromText (const juce::String& t)
{
    return (float) t.replace (juce::String::charToString ((juce::juce_wchar) 0x2212), "-")
                    .retainCharacters ("0123456789.,-+").replaceCharacter (',', '.').getDoubleValue();
}

//------------------------------------------------------------------ layout
inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto floatParam = [&] (const String& id, const String& name, NormalisableRange<float> range, float def,
                           std::function<String (float)> toText, std::function<float (const String&)> fromText)
    {
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { id, 1 }, name, range, def,
            AudioParameterFloatAttributes()
                .withStringFromValueFunction ([toText] (float v, int) { return toText (v); })
                .withValueFromStringFunction ([fromText] (const String& s) { return fromText (s); })));
    };

    using i18n::tr;
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::key, 1 }, tr ("Key"), keyChoices(), 0));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::scale, 1 }, tr ("Scale"), scaleChoices(), kMajor));

    floatParam (ids::correction, tr ("Pitch Correction"), { 0.0f, 100.0f, 0.1f }, 100.0f, percentText, numberFromText);

    // 0 - 200 ms; skew puts 15 ms near the middle of the slider, where most vocals live
    NormalisableRange<float> retuneRange { 0.0f, 200.0f, 0.1f };
    retuneRange.setSkewForCentre (20.0f);
    floatParam (ids::retune, tr ("Retune Speed"), retuneRange, 15.0f, msText, numberFromText);

    floatParam (ids::humanize, tr ("Humanize"), { 0.0f, 100.0f, 0.1f }, 35.0f, percentText, numberFromText);
    floatParam (ids::robotism, tr ("Robotism"), { 0.0f, 100.0f, 0.1f }, 50.0f, robotismText, robotismFromText);
    floatParam (ids::vibrato, tr ("Vibrato Scale"), { 0.0f, 200.0f, 0.1f }, 100.0f, percentText, numberFromText);
    floatParam (ids::formant, tr ("Formant"), { -12.0f, 12.0f, 0.1f }, 0.0f, formantText, numberFromText);
    floatParam (ids::transpose, tr ("Pitch Range"), { -12.0f, 12.0f, 1.0f }, 0.0f, semitoneText, numberFromText);
    floatParam (ids::mix, tr ("Mix"), { 0.0f, 100.0f, 0.1f }, 100.0f, percentText, numberFromText);

    const auto onOff = AudioParameterBoolAttributes()
                           .withStringFromValueFunction ([] (bool b, int) { return i18n::tr (b ? "On" : "Off"); })
                           .withValueFromStringFunction ([] (const String& s)
                           {
                               for (const auto& w : i18n::allTexts ("On"))
                                   if (s.trim().equalsIgnoreCase (w)) return true;
                               return s.trim() == "1" || s.trim().equalsIgnoreCase ("true");
                           });
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::bypass, 1 }, tr ("Bypass"), false, onOff));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::realtime, 1 }, tr ("Realtime"), true,
                                                      AudioParameterBoolAttributes (onOff).withAutomatable (false)));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::range, 1 }, tr ("Vocal Range"), rangeChoices(), 1,
                                                        AudioParameterChoiceAttributes().withAutomatable (false)));

    // after 1.0: custom scale (12 notes relative to the key) and Adaptive, after everything 1.0 had
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::customScale, 2 }, tr ("Custom Scale"), false, onOff));
    for (int i = 0; i < 12; ++i)
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::scaleNote (i), 2 },
                                                          tr ("Custom Note") + " " + degreeName (i), customNoteDefault (i), onOff));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::adaptive, 2 }, tr ("Adaptive"), false, onOff));

    // 1.5: A4 reference (415 - 466 Hz, 440 by default), appended after everything 1.4 had. Like the vocal
    // range it is a setup choice, not something to automate. Old sessions open at 440 Hz.
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ids::tuningRef, 3 }, tr ("Tuning (A4)"), NormalisableRange<float> { 415.0f, 466.0f, 0.1f }, 440.0f,
        AudioParameterFloatAttributes()
            .withAutomatable (false)
            .withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " Hz"; })
            .withValueFromStringFunction ([] (const String& t) { return t.retainCharacters ("0123456789.,").replaceCharacter (',', '.').getFloatValue(); })));

    // 1.5: Formant leaves consonants and breaths alone (off = as before). A sound setting, so presets keep it.
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::protectCons, 3 }, tr ("Protect consonants from formant"), false,
                                                      AudioParameterBoolAttributes (onOff).withAutomatable (false)));

    // 1.5: Formant follows Transpose (a "chipmunk" / "giant" voice) and Note Transition (the glide between notes:
    // Auto follows ROBOTISM as before, or a set time on both sides). Sound settings: presets keep them.
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::formantFollow, 3 }, tr ("Formant follows transpose"), false,
                                                      AudioParameterBoolAttributes (onOff).withAutomatable (false)));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::transition, 3 }, tr ("Note transition"), transitionChoices(), 0,
                                                        AudioParameterChoiceAttributes().withAutomatable (false)));

    // 1.5: note bypass in the custom scale (relative to the key, like the custom notes): a sung note nearest to a
    // bypassed degree passes through uncorrected. Part of the scale, so "presets keep key and scale" keeps it too.
    for (int i = 0; i < 12; ++i)
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::scaleBypass (i), 3 }, tr ("Bypass Note") + " " + degreeName (i),
                                                          false, AudioParameterBoolAttributes (onOff).withAutomatable (false)));

    // 1.5: tolerance, confidence threshold, synthetic vibrato and octave shift (all off / 0 by default). Sound
    // settings: presets keep them.
    auto setup = AudioParameterChoiceAttributes().withAutomatable (false);
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::tolerance, 3 }, tr ("Tolerance"), toleranceChoices(), 0, setup));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::confidence, 3 }, tr ("Confidence threshold"), confidenceChoices(), 0, setup));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::synthVib, 3 }, tr ("Synthetic vibrato"), synthVibChoices(), 0, setup));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::octave, 3 }, tr ("Octave shift"), octaveChoices(), 2, setup));

    // 1.5: MIDI target notes (VST3 / Standalone). A session setting, like the vocal range: presets leave it alone.
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::midiTarget, 3 }, tr ("Target notes from MIDI"), false,
                                                      AudioParameterBoolAttributes (onOff).withAutomatable (false)));
    // 1.5: the note the voice is tuned to goes out as MIDI (session setting)
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::midiOut, 3 }, tr ("Send notes as MIDI"), false,
                                                      AudioParameterBoolAttributes (onOff).withAutomatable (false)));
    // 1.5 (roadmap 49, MIDI perform): with MIDI target notes, the voice goes to the last held note, octave as played
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::midiOctave, 3 }, tr ("MIDI: octave as played"), false,
                                                      AudioParameterBoolAttributes (onOff).withAutomatable (false)));
    // 1.5 (roadmap 45): tuning system and the custom cents per degree (from the key's tonic). Part of the scale:
    // presets that keep the key keep these too.
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::tuningSys, 3 }, tr ("Tuning system"), tuningChoices(), kEqualTuning,
                                                        AudioParameterChoiceAttributes().withAutomatable (false)));
    for (int i = 0; i < 12; ++i)
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ids::tuneCents (i), 3 }, tr ("Custom tuning") + " " + degreeName (i),
                                                           NormalisableRange<float> (-50.0f, 50.0f, 0.1f), 0.0f,
                                                           AudioParameterFloatAttributes().withLabel ("ct").withAutomatable (false)));
    // 1.5 (roadmap 41): diatonic transpose (steps of the scale), a sound setting like TRANSPOSE
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::scaleTr, 3 }, tr ("Scale transpose"), scaleStepChoices(), 7,
                                                        AudioParameterChoiceAttributes().withAutomatable (false)));
    // 1.5 (roadmap 27): correction attack / release (sound settings)
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::attack, 3 }, tr ("Attack"), attackChoices(), 0,
                                                        AudioParameterChoiceAttributes().withAutomatable (false)));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::release, 3 }, tr ("Release"), releaseChoices(), 0,
                                                        AudioParameterChoiceAttributes().withAutomatable (false)));
    // 1.6 Auto Character: automatable, part of presets; off by default (older sessions and presets: as before)
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::autoCharacter, 4 }, tr ("Auto Character"), false,
                                                      AudioParameterBoolAttributes (onOff)));
    return layout;
}
} // namespace vstm::params
