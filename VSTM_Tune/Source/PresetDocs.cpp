#include "PresetDocs.h"
#include "Parameters.h"
#include "dsp/Scales.h"

#ifndef VSTM_TUNE_VERSION
 #define VSTM_TUNE_VERSION "unknown"
#endif

namespace vstm
{
namespace
{
using drp::text::formatNumber;

juce::String num (float v) { return formatNumber (v); }

template <size_t N>
juce::String table (const float (&t)[N], const char* unit)
{
    juce::StringArray s;
    for (size_t i = 0; i < N; ++i)
        s.add (juce::String ((int) i) + " -> " + num (t[i]) + unit);
    return s.joinIntoString (", ");
}

/** What a parameter does and how the engine uses its value (PluginProcessor::readParams, PitchCorrector). */
juce::StringArray purpose (const juce::String& id)
{
    using namespace params;
    const auto deg = [] (const juce::String& prefix, const juce::String& pid)
    {
        const int d = pid.substring (prefix.length()).getIntValue();
        return juce::String (degreeName (d)) + " (" + juce::String (d) + " semitones above the key)";
    };
    if (id == ids::key)         return { "The tonic the scale is built on. 0 = Auto (estimated from the voice), 1 .. 12 = C .. B.",
                                         "Engine: key = round(value) - 1 (-1 = Auto)." };
    if (id == ids::scale)       return { "Which notes the voice is tuned to (ignored while the custom scale is on).",
                                         "Engine: scale = round(value)." };
    if (id == ids::correction)  return { "Pitch Correction: how much of the distance to the target note is corrected.",
                                         "Engine: amount = value / 100." };
    if (id == ids::retune)      return { "Retune Speed: how fast the pitch is pulled to the note (0 ms = instant).",
                                         "Engine: retune time constant = value x speedMul x (1 + 6 x humanizeEff x sustain), where",
                                         "  speedMul = (1 - r)(1 - r/2)(1 + 3h), humanizeEff = clamp(humanize x (1 - r) + 0.4 x h x (1 - humanize), 0, 1),",
                                         "  sustain = smoothstep(0.08 s, 0.40 s, note age); r, h: see robotism. Below 0.5 ms the correction is instant." };
    if (id == ids::humanize)    return { "Humanize: relaxes the correction on long, sustained notes so they keep their natural movement.",
                                         "Engine: humanize = value / 100 (used in the retune formula above)." };
    if (id == ids::robotism)    return { "ROBOTISM: the character, 0 = Robotic ... 50 = neutral ... 100 = Human.",
                                         "Engine: robotism = value / 100; robotic half r = clamp((0.5 - robotism) x 2, 0, 1),",
                                         "  human half h = clamp((robotism - 0.5) x 2, 0, 1)^1.6; note hysteresis = 15 + 20h cents;",
                                         "  automatic glide = 110h ms (it follows the singer's own transition, 1.6); natural pitch centre",
                                         "  (1.6): weight smoothstep(0.25, 0.5, robotism) x vibrato detected; octave guard from robotism > 0.25." };
    if (id == ids::vibrato)     return { "Vibrato Scale: depth of the vibrato and fast pitch movement (100 % = as sung).",
                                         "Engine: vibratoScale = value / 100; the note decision sees min(scale, 1); the output scales",
                                         "  the band above a 2-pole 1.5 Hz low-pass of the pitch by the scale." };
    if (id == ids::formant)     return { "Formant: shifts the vocal character (throat size) in semitones without changing the pitch.",
                                         "Engine: formant = value semitones (clamped to +-24)." };
    if (id == ids::transpose)   return { "Pitch Range: transposes the corrected voice.",
                                         "Engine: transpose = round(value) semitones." };
    if (id == ids::mix)         return { "Mix: blend between the original (dry) and the tuned voice.",
                                         "Engine: mix = value / 100." };
    if (id == ids::customScale) return { "Custom scale on: the 12 custom notes below replace the Scale choice.",
                                         "Engine: on when value > 0.5." };
    if (id.startsWith ("scalenote"))   return { "Custom scale note " + deg ("scalenote", id) + ": 1 = allowed target, 0 = not.",
                                                "Engine: bit set when value > 0.5." };
    if (id.startsWith ("scalebypass")) return { "Note bypass " + deg ("scalebypass", id) + ": 1 = a voice near this degree passes uncorrected.",
                                                "Engine: bit set when value > 0.5 (relative to the key, like the custom notes)." };
    if (id.startsWith ("tunecents"))   return { "Custom tuning, degree " + deg ("tunecents", id) + ": cents off the equal grid (used when Tuning system = Custom).",
                                                "Engine: degreeCents = value (only with tuningsys = Custom)." };
    if (id == ids::adaptive)    return { "Adaptive: follows the performance within fixed limits (vibrato and slides kept, fast runs quicker).",
                                         "Engine: on when value > 0.5." };
    if (id == ids::protectCons) return { "Protect consonants from formant: unvoiced sounds are not formant-shifted.",
                                         "Engine: on when value > 0.5." };
    if (id == ids::formantFollow) return { "Formant follows transpose: the formant moves with the transpose (one creative shift).",
                                           "Engine: on when value > 0.5 (formant = formant + transpose, clamped to +-24)." };
    if (id == ids::transition)  return { "Note transition: glide between notes; Auto = follows ROBOTISM.",
                                         "Engine: index -> ms: " + table (kTransitionMs, " ms") + " (-1 = Auto)." };
    if (id == ids::tolerance)   return { "Tolerance: the note centre may stay this close to the target as sung.",
                                         "Engine: index -> cents: " + table (kToleranceCt, " ct") + "." };
    if (id == ids::confidence)  return { "Confidence threshold: below this detector clarity the correction fades out (20 ms).",
                                         "Engine: index -> clarity: " + table (kConfidence, "") + " (0 = off)." };
    if (id == ids::synthVib)    return { "Synthetic vibrato added to the output (starts 250 ms into a note, fades in over 200 ms).",
                                         "Engine: index -> rate: " + table (kSynthVibRate, " Hz") + "; depth: " + table (kSynthVibDepth, " ct") + "." };
    if (id == ids::octave)      return { "Octave shift on top of Pitch Range.",
                                         "Engine: octave = round(value) - 2 (index 2 = 0), transpose + 12 x octave clamped to +-24." };
    if (id == ids::tuningSys)   return { "Tuning system: cents per degree from the key's tonic (Equal, Just, Pythagorean, Rast, Bayati, Custom).",
                                         "Engine: degreeCents = the table of the chosen system (Custom: the tunecents values)." };
    if (id == ids::scaleTr)     return { "Scale transpose: moves the voice by steps of the scale (C major +2: C -> E, D -> F).",
                                         "Engine: steps = round(value) - 7 (index 7 = 0)." };
    if (id == ids::attack)      return { "Attack: correction fade-in at the start of a phrase; Auto = 70h ms (follows ROBOTISM).",
                                         "Engine: index -> ms: " + table (kAttackMs, " ms") + " (-1 = Auto)." };
    if (id == ids::release)     return { "Release: correction fade-out once the note has decayed 12 dB below its recent peak.",
                                         "Engine: index -> ms: " + table (kReleaseMs, " ms") + " (0 = off)." };
    if (id == ids::autoCharacter) return { "Auto Character: 1 = ROBOTISM also sets Retune Speed, Humanize and Vibrato Scale (see the top",
                                           "  of this file); 0 = those three are manual. Engine: on when value > 0.5." };
    return { "No explanation for this parameter in this version." };
}

/** Range, step, default, unit, choices and automation, read from the parameter itself. */
juce::StringArray describeRange (juce::RangedAudioParameter& p)
{
    juce::StringArray s;
    const auto def = p.convertFrom0to1 (p.getDefaultValue());
    if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (&p))
    {
        juce::StringArray opts;
        for (int i = 0; i < c->choices.size(); ++i)
            opts.add (juce::String (i) + " = " + c->choices[i]);
        s.add ("Choice: " + opts.joinIntoString (", ") + ". Default " + juce::String (juce::roundToInt (def)) + ".");
    }
    else if (dynamic_cast<juce::AudioParameterBool*> (&p) != nullptr)
        s.add ("Switch: 0 = off, 1 = on. Default " + juce::String (def > 0.5f ? 1 : 0) + ".");
    else
    {
        const auto& r = p.getNormalisableRange();
        auto line = "Range " + num (r.start) + " .. " + num (r.end) + (p.getLabel().isNotEmpty() ? " " + p.getLabel() : juce::String())
                  + ", step " + num (r.interval) + ", default " + num (def) + ".";
        if (std::abs (r.skew - 1.0f) > 1.0e-6f)
            line << " (The control is skewed: skew factor " << num (r.skew) << "; the value here is the real value.)";
        s.add (line);
    }
    s.add (juce::String ("Host automation: ") + (p.isAutomatable() ? "yes." : "no (a setting, saved with presets)."));
    s.add ("Values outside the range are clamped to it when loaded; a missing value takes the default.");
    return s;
}
} // namespace

drp::text::Document describePreset (const juce::String& name, const std::vector<std::pair<juce::String, float>>& values,
                                    juce::AudioProcessorValueTreeState& apvts)
{
    drp::text::Document doc;
    doc.name = name;
    doc.pluginVersion = VSTM_TUNE_VERSION;

    float robot = 50.0f, autoOn = 0.0f;
    for (const auto& [id, v] : values)
    {
        if (id == ids::robotism) robot = v;
        if (id == ids::autoCharacter) autoOn = v;
    }

    auto& h = doc.header;
    h.add ("VSTM Tune preset - readable .drp (text format 2)");
    h.add ("Preset name: " + name);
    h.add ("Plug-in version: " + juce::String (VSTM_TUNE_VERSION));
    h.add ("");
    h.add ("This file holds a preset's parameter values and an explanation of each. Lines starting with # are");
    h.add ("comments: they are not read. Each value is in the parameter's own units. It can be edited in any text");
    h.add ("editor; a value outside its range is clamped when the preset is loaded, a missing one takes its default,");
    h.add ("a damaged line is skipped. Readable is not protected: the file has no checksum and no signature.");
    h.add ("Older binary .drp presets keep loading; this file is only written when authorized and chosen in the");
    h.add ("preset menu.");
    h.add ("");
    h.add ("AUTO CHARACTER (params::characterFor). With autochar = 1, ROBOTISM (r = robotism / 100) sets:");
    h.add ("  retune (ms)   r <= 0.5: 15 x (r / 0.5)^2              r > 0.5: 15 + 10 x ((r - 0.5) / 0.5)^1.5");
    h.add ("  humanize (%)  r <= 0.5: 35 x smoothstep(0.2, 0.5, r)  r > 0.5: 35 + 15 x (r - 0.5) / 0.5");
    h.add ("  vibscale (%)  r <= 0.5: 60 + 40 x smoothstep(0, 0.5, r)  r > 0.5: 100");
    h.add ("  smoothstep(a, b, x) = t^2 (3 - 2t), t = clamp((x - a) / (b - a), 0, 1). Each value then goes through its");
    h.add ("  control's range and step, as if set on the control.");
    h.add ("  Examples (computed now by the plug-in's own function):");
    for (const float r : { 0.0f, 25.0f, 50.0f, 75.0f, 100.0f })
    {
        const auto c = params::characterFor (r);
        h.add ("    robotism " + num (r) + " -> retune " + juce::String (c.retuneMs, 2) + " ms, humanize " + juce::String (c.humanizePct, 2)
               + " %, vibscale " + juce::String (c.vibratoPct, 2) + " %");
    }
    if (autoOn > 0.5f)
    {
        const auto c = params::characterFor (robot);
        h.add ("  THIS PRESET: Auto Character is ON. ROBOTISM " + num (robot) + " gives retune " + juce::String (c.retuneMs, 2)
               + " ms, humanize " + juce::String (c.humanizePct, 2) + " %, vibscale " + juce::String (c.vibratoPct, 2) + " %.");
        h.add ("  The retune / humanize / vibscale values stored below are the manual values: not used while Auto");
        h.add ("  Character is on. Switching it off in the plug-in (or by automation) copies the Auto values into them");
        h.add ("  first, so the sound does not change; loading a preset or a session uses the stored values as they are.");
    }
    else
        h.add ("  THIS PRESET: Auto Character is OFF. ROBOTISM keeps only its own effect; every stored value is used as it is.");
    h.add ("");
    h.add ("Not in presets (they belong to the session): bypass, realtime, range (vocal range), tuningref (A4),");
    h.add ("miditarget, midiout, midioctave.");

    for (const auto& [id, v] : values)
    {
        drp::text::Line line;
        line.id = id;
        line.value = v;
        if (auto* p = apvts.getParameter (id))
        {
            line.comment.add (p->getName (64) + " (" + id + ")");
            line.comment.addArray (purpose (id));
            line.comment.addArray (describeRange (*p));
            if (id == ids::retune || id == ids::humanize || id == ids::vibrato)
                line.comment.add (autoOn > 0.5f ? "Managed by Auto Character in this preset (the value here is the manual value)."
                                                : "Manual in this preset (Auto Character is off).");
            line.comment.add ("Shown in the plug-in as: " + p->getText (p->convertTo0to1 (v), 64) + (p->getLabel().isNotEmpty() ? " " + p->getLabel() : juce::String()));
        }
        else
            line.comment.add ("Unknown to VSTM Tune " + juce::String (VSTM_TUNE_VERSION) + ": kept in the file, not used.");
        doc.params.push_back (std::move (line));
    }
    return doc;
}
} // namespace vstm
