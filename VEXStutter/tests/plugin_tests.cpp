//==============================================================================
//  VEX Stutter - plug-in tests (evidence: MEASURED). Not part of the plug-in.
//
//    VEXStutter_Tests [screenshot_folder] [--vst3 "<path>/VEX Stutter.vst3"]
//
//  Linux without a display:  xvfb-run -a VEXStutter_Tests shots --vst3 "build/.../VEX Stutter.vst3"
//  Exit code 0 = every check passed. The DSP itself is tested by VEXStutter_EngineTests.
//==============================================================================
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "Parameters/Choices.h"
#include "Parameters/Divisions.h"
#include "Parameters/ParameterIDs.h"
#include "Parameters/ParameterLayout.h"
#include "PluginEditor.h"
#include "PluginProcessor.h"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <set>
#include <thread>

namespace
{
int failures = 0;
int checks = 0;

#define CHECK(cond, ...) do { const bool ok_ = (cond); ++checks; std::printf ("  [%s] ", ok_ ? " OK " : "FAIL"); \
                              std::printf (__VA_ARGS__); std::printf ("\n"); if (! ok_) ++failures; } while (0)

namespace ids = vex::ids;
constexpr double kPi = 3.14159265358979;

juce::RangedAudioParameter* param (VEXStutterAudioProcessor& p, const char* id) { return p.getState().getParameter (id); }
juce::String text (VEXStutterAudioProcessor& p, const char* id) { return param (p, id)->getCurrentValueAsText(); }

void setReal (VEXStutterAudioProcessor& p, const char* id, float realValue)
{
    auto* prm = param (p, id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (realValue));
}

juce::MemoryBlock saveState (VEXStutterAudioProcessor& p)
{
    juce::MemoryBlock mb;
    p.getStateInformation (mb);
    return mb;
}

std::vector<float> values (VEXStutterAudioProcessor& p)
{
    std::vector<float> v;
    for (const char* id : ids::all)
        v.push_back (param (p, id)->convertFrom0to1 (param (p, id)->getValue()));
    return v;
}

void loadXml (VEXStutterAudioProcessor& p, const juce::String& xmlText)
{
    juce::MemoryBlock mb;
    if (auto xml = juce::parseXML (xmlText))
        juce::AudioProcessor::copyXmlToBinary (*xml, mb);
    p.setStateInformation (mb.getData(), (int) mb.getSize());
}

/** Random test signal: noise, full-scale samples, tiny values, exact zeros. */
void fill (juce::AudioBuffer<float>& b, std::mt19937& rng)
{
    std::uniform_real_distribution<float> u (-1.0f, 1.0f);
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const int k = (int) (rng() % 16);
            b.setSample (ch, i, k == 0 ? 1.0f : k == 1 ? -1.0f : k == 2 ? 1.0e-30f : k == 3 ? 0.0f : u (rng));
        }
}

/** A sung-vowel-like synthetic signal (harmonics, slow pitch drift, syllables). */
std::vector<float> voice (double seconds, double rate)
{
    std::vector<float> x ((size_t) (seconds * rate));
    double ph = 0.0;
    for (size_t i = 0; i < x.size(); ++i)
    {
        const double t = (double) i / rate;
        ph += 196.0 * std::pow (2.0, 0.25 * std::sin (2.0 * kPi * 0.7 * t)) / rate;
        double s = 0.0;
        for (int h = 1; h <= 8; ++h)
            s += std::sin (2.0 * kPi * h * ph) / h;
        const double syl = 0.5 + 0.5 * std::sin (2.0 * kPi * 2.3 * t);
        x[i] = (float) (0.3 * syl * syl * s);
    }
    return x;
}

double corr (const float* a, const float* b, int n)
{
    double xy = 0, xx = 0, yy = 0;
    for (int i = 0; i < n; ++i) { xy += (double) a[i] * b[i]; xx += (double) a[i] * a[i]; yy += (double) b[i] * b[i]; }
    return xy / std::sqrt (xx * yy + 1e-30);
}

/** A host transport playing from bar 1 at a fixed tempo, advanced by the test. */
struct TestPlayHead final : public juce::AudioPlayHead
{
    double bpm = 120.0, ppq = 0.0;
    bool playing = true;

    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setBpm (bpm);
        info.setPpqPosition (ppq);
        info.setIsPlaying (playing);
        info.setTimeSignature (TimeSignature { 4, 4 });
        info.setPpqPositionOfLastBarStart (std::floor (ppq / 4.0) * 4.0);
        return info;
    }
};

/** Runs `in` (mono, copied to both channels) through `p` in blocks with the play head advancing. */
std::vector<float> run (VEXStutterAudioProcessor& p, TestPlayHead& head, const std::vector<float>& in, double rate, int block)
{
    std::vector<float> out (in.size());
    juce::AudioBuffer<float> buf (2, block);
    juce::MidiBuffer midi;
    for (size_t pos = 0; pos < in.size(); pos += (size_t) block)
    {
        const int n = (int) std::min<size_t> ((size_t) block, in.size() - pos);
        buf.setSize (2, n, false, false, true);
        for (int ch = 0; ch < 2; ++ch)
            buf.copyFrom (ch, 0, in.data() + pos, n);
        head.ppq = (double) pos * head.bpm / (60.0 * rate);
        p.processBlock (buf, midi);
        std::memcpy (out.data() + pos, buf.getReadPointer (0), sizeof (float) * (size_t) n);
    }
    return out;
}

bool savePng (juce::Component& c, const juce::File& file, float scale)
{
    const auto image = c.createComponentSnapshot (c.getLocalBounds(), true, scale);
    file.deleteFile();
    juce::FileOutputStream out (file);
    const bool ok = out.openedOk() && juce::PNGImageFormat().writeImageToStream (image, out);
    if (ok) std::printf ("  wrote %s\n", file.getFullPathName().toRawUTF8());
    return ok;
}

void pumpUi (int ms)
{
    juce::MessageManager::getInstance()->runDispatchLoopUntil (ms);
}
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI gui;

    juce::File shotDir, vst3File;
    for (int i = 1; i < argc; ++i)
    {
        const juce::String arg (argv[i]);
        if (arg == "--vst3" && i + 1 < argc) vst3File = juce::File::getCurrentWorkingDirectory().getChildFile (juce::String (argv[++i]));
        else shotDir = juce::File::getCurrentWorkingDirectory().getChildFile (arg);
    }
    const auto tempPresets = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("VEXStutterTestPresets");
    tempPresets.deleteRecursively();

    //--------------------------------------------------------------------------
    std::printf ("\n=== 1) Parameters ===\n");
    {
        VEXStutterAudioProcessor p;
        const auto& params = p.getParameters();
        CHECK (params.size() == ids::count && params.size() == 24, "%d parameters (expected 24)", params.size());

        bool orderOk = params.size() == ids::count;
        std::set<juce::String> idSet, nameSet;
        bool allAutomatable = true, versionOk = true;
        for (int i = 0; i < params.size(); ++i)
        {
            auto* rp = dynamic_cast<juce::RangedAudioParameter*> (params[i]);
            orderOk = orderOk && rp != nullptr && rp->getParameterID() == ids::all[i];
            if (rp != nullptr)
            {
                idSet.insert (rp->getParameterID());
                versionOk = versionOk && rp->getVersionHint() == vex::params::kParameterVersion;
            }
            nameSet.insert (params[i]->getName (64));
            allAutomatable = allAutomatable && params[i]->isAutomatable();
        }
        CHECK (orderOk, "host order follows vex::ids::all (bypass, mode, amount, ... reverb_mix)");
        CHECK ((int) idSet.size() == params.size(), "IDs unique (%d)", (int) idSet.size());
        CHECK ((int) nameSet.size() == params.size(), "names unique in automation lists (%d)", (int) nameSet.size());
        CHECK (allAutomatable && versionOk, "every parameter automatable, version hint %d", vex::params::kParameterVersion);

        struct Expect { const char* id; const char* text; };
        const Expect expected[] =
        {
            { ids::bypass, "Off" }, { ids::mode, "Stutter" }, { ids::amount, "75.0" }, { ids::sync, "Host" },
            { ids::rate, "1/8" }, { ids::rateMs, "250.0" }, { ids::length, "50.0" }, { ids::style, "Sharp" },
            { ids::shape, "Hard" }, { ids::smoothness, "40.0" }, { ids::offsetMs, "0.0" }, { ids::mix, "100.0" },
            { ids::glitchType, "Pixel" }, { ids::intensity, "70.0" }, { ids::freeze, "Off" },
            { ids::fxEnabled, "On" }, { ids::delaySync, "Host" }, { ids::delayRate, "1/8" },
            { ids::delayTimeMs, "250.0" }, { ids::delayFeedback, "30.0" }, { ids::delayMix, "0.0" },
            { ids::reverbSize, "50.0" }, { ids::reverbDamping, "50.0" }, { ids::reverbMix, "0.0" },
        };
        bool defaultsOk = true;
        for (const auto& e : expected)
            if (text (p, e.id) != e.text)
            {
                defaultsOk = false;
                std::printf ("       %s = '%s' (expected '%s')\n", e.id, text (p, e.id).toRawUTF8(), e.text);
            }
        CHECK (defaultsOk, "defaults: Stutter, Amount 75 %%, Rate 1/8 host-synced, Length 50 %%, Smoothness 40 %%, Mix 100 %%, Pixel 70 %%, FX on, delay / reverb mix 0 %%");
        CHECK (param (p, ids::amount)->getLabel() == "%" && param (p, ids::offsetMs)->getLabel() == "ms", "units shown to the host: %% and ms");

        bool choicesOk = true;
        auto names = [] (juce::RangedAudioParameter* prm) { return dynamic_cast<juce::AudioParameterChoice*> (prm)->choices.joinIntoString ("|"); };
        choicesOk = choicesOk && names (param (p, ids::mode)) == "Stutter|Repeat|Gate|Reverse|Glitch";
        choicesOk = choicesOk && names (param (p, ids::style)) == "Sharp|Smooth|Tight|Loose|Tape|Random|Digital Glitch";
        choicesOk = choicesOk && names (param (p, ids::shape)) == "Hard|Ramp Up|Ramp Down|Smooth";
        choicesOk = choicesOk && names (param (p, ids::glitchType)) == "Pixel|Burst|Shuffle";
        CHECK (choicesOk, "choice lists: 5 modes, 7 styles, 4 shapes, 3 glitch types, in their permanent order");

        bool monotonic = true;
        for (int i = 1; i < vex::divisions::count; ++i)
            monotonic = monotonic && vex::divisions::table[i].beats < vex::divisions::table[i - 1].beats;
        CHECK (vex::divisions::count == 15 && monotonic && vex::params::divisionChoices()[vex::divisions::defaultIndex] == "1/8",
               "Rate list: 15 divisions, longest first, default 1/8");

        bool roundTrip = true;
        for (auto* prm : params)
            if (auto* fpd = dynamic_cast<juce::AudioParameterFloat*> (prm))
                for (int k = 0; k <= 10; ++k)
                {
                    const juce::AudioProcessorParameter* fp = fpd;
                    const auto t = fp->getText ((float) k / 10.0f, 64);
                    if (fp->getText (fp->getValueForText (t), 64) != t)
                        roundTrip = false;
                }
        CHECK (roundTrip, "typed values: text -> value -> text is stable for every float parameter");
        const juce::AudioProcessorParameter* fbBase = param (p, ids::delayFeedback);
        CHECK (std::abs (param (p, ids::delayFeedback)->convertFrom0to1 (fbBase->getValueForText ("150")) - 85.0f) < 1e-3f,
               "Delay Feedback cannot exceed the 85 %% safety cap, even when typed");
    }

    //--------------------------------------------------------------------------
    std::printf ("\n=== 2) Plug-in identity, buses, tail ===\n");
    {
        VEXStutterAudioProcessor p;
        CHECK (p.getName() == "VEX Stutter" && ! p.acceptsMidi() && ! p.producesMidi() && ! p.isMidiEffect(), "name and MIDI flags");
        CHECK (p.getLatencySamples() == 0, "latency 0 samples");
        CHECK (p.getBypassParameter() == param (p, ids::bypass), "the host's bypass parameter is the Bypass parameter");

        using Set = juce::AudioChannelSet;
        auto layout = [] (Set in, Set out) { juce::AudioProcessor::BusesLayout l; l.inputBuses.add (in); l.outputBuses.add (out); return l; };
        CHECK (p.checkBusesLayoutSupported (layout (Set::mono(), Set::mono())) && p.checkBusesLayoutSupported (layout (Set::stereo(), Set::stereo()))
               && ! p.checkBusesLayoutSupported (layout (Set::mono(), Set::stereo())) && ! p.checkBusesLayoutSupported (layout (Set::stereo(), Set::mono()))
               && ! p.checkBusesLayoutSupported (layout (Set::create5point1(), Set::create5point1())),
               "mono -> mono and stereo -> stereo only");

        const double tail0 = p.getTailLengthSeconds();
        setReal (p, ids::reverbMix, 50.0f);
        setReal (p, ids::reverbSize, 100.0f);
        setReal (p, ids::delayMix, 50.0f);
        setReal (p, ids::delayFeedback, 85.0f);
        const double tail1 = p.getTailLengthSeconds();
        CHECK (std::isfinite (tail0) && tail0 >= 0.0 && tail0 <= 60.0 && tail1 > tail0 + 5.0 && tail1 <= 60.0,
               "tail: %.1f s with the FX off, %.1f s with long delay feedback and a large reverb (capped at 60 s)", tail0, tail1);
    }

    //--------------------------------------------------------------------------
    std::printf ("\n=== 3) Processing through the plug-in ===\n");
    {
        std::mt19937 rng (7);
        long blocks = 0;
        bool exact = true;
        for (double rate : { 44100.0, 48000.0, 96000.0 })
            for (int channels : { 1, 2 })
                for (int block : { 1, 37, 256, 1024 })
                {
                    VEXStutterAudioProcessor p;
                    p.setPlayConfigDetails (channels, channels, rate, block);
                    p.prepareToPlay (rate, block);
                    setReal (p, ids::amount, 0.0f);
                    juce::AudioBuffer<float> buf (channels, block), ref (channels, block);
                    juce::MidiBuffer midi;
                    for (int n = 0; n < 30; ++n)
                    {
                        // everything except Amount moves while audio runs: Amount 0 must stay bit-exact
                        for (const char* id : ids::all)
                            if (std::strcmp (id, ids::amount) != 0)
                                param (p, id)->setValueNotifyingHost (std::uniform_real_distribution<float> (0.0f, 1.0f) (rng));
                        fill (buf, rng);
                        ref.makeCopyOf (buf, true);
                        p.processBlock (buf, midi);
                        for (int ch = 0; ch < channels; ++ch)
                            exact = exact && std::memcmp (buf.getReadPointer (ch), ref.getReadPointer (ch), sizeof (float) * (size_t) block) == 0;
                        ++blocks;
                    }
                }
        CHECK (exact, "Amount 0 %%: output = input bit for bit (44.1 / 48 / 96 kHz, mono / stereo, blocks 1 ... 1024, every other parameter "
                      "randomised every block; %ld blocks)", blocks);

        // the host's own bypass (processBlockBypassed) and the Bypass parameter
        VEXStutterAudioProcessor b;
        b.setPlayConfigDetails (2, 2, 48000.0, 256);
        b.prepareToPlay (48000.0, 256);
        setReal (b, ids::amount, 100.0f);
        setReal (b, ids::bypass, 1.0f);
        juce::AudioBuffer<float> buf (2, 256), ref (2, 256);
        juce::MidiBuffer midi;
        bool byp = true;
        for (int n = 0; n < 40; ++n)
        {
            fill (buf, rng);
            ref.makeCopyOf (buf, true);
            if (n % 2) b.processBlock (buf, midi); else b.processBlockBypassed (buf, midi);
            for (int ch = 0; ch < 2; ++ch)
                byp = byp && std::memcmp (buf.getReadPointer (ch), ref.getReadPointer (ch), sizeof (float) * 256) == 0;
        }
        CHECK (byp, "Bypass parameter and host bypass: output = input bit for bit");

        // random parameters, transport and garbage input: finite and bounded
        VEXStutterAudioProcessor r;
        TestPlayHead head;
        r.setPlayHead (&head);
        r.setPlayConfigDetails (2, 2, 48000.0, 512);
        r.prepareToPlay (48000.0, 512);
        bool finite = true;
        double peak = 0.0;
        for (int n = 0; n < 600; ++n)
        {
            for (const char* id : ids::all)
                param (r, id)->setValueNotifyingHost (std::uniform_real_distribution<float> (0.0f, 1.0f) (rng));
            r.setSelection (std::uniform_real_distribution<float> (0.0f, 1.0f) (rng), std::uniform_real_distribution<float> (0.0f, 1.0f) (rng));
            head.playing = (rng() % 5) != 0;
            head.ppq += (rng() % 7 == 0) ? 3.7 : 512.0 * 120.0 / (60.0 * 48000.0);
            const int len = 1 + (int) (rng() % 512);
            juce::AudioBuffer<float> blk (2, len);
            fill (blk, rng);
            r.processBlock (blk, midi);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < len; ++i)
                {
                    const float v = blk.getSample (ch, i);
                    finite = finite && std::isfinite (v);
                    peak = std::max (peak, (double) std::abs (v));
                }
        }
        CHECK (finite && peak < 8.0, "every parameter, selection, transport and block size randomised (600 blocks): finite, peak %.2f", peak);
        r.setPlayHead (nullptr);

        // the host's tempo and position reach the DSP: 1/8 at 120 BPM repeats slice 0 in slice 1
        VEXStutterAudioProcessor s;
        TestPlayHead h2;
        s.setPlayHead (&h2);
        s.setPlayConfigDetails (2, 2, 48000.0, 480);
        s.prepareToPlay (48000.0, 480);
        setReal (s, ids::amount, 100.0f);
        setReal (s, ids::length, 100.0f);
        setReal (s, ids::smoothness, 0.0f);
        const auto in = voice (3.0, 48000.0);
        const auto out = run (s, h2, in, 48000.0, 480);
        const int slice = 12000, guard = 600;
        const double c1 = corr (out.data() + slice + guard, in.data() + guard, slice - 2 * guard);
        const double c8 = corr (out.data() + 8 * slice + guard, in.data() + 8 * slice + guard, slice - 2 * guard);
        CHECK (c1 > 0.999 && c8 > 0.999, "host at 120 BPM: slice 1 repeats slice 0 (%.4f), the next bar starts live (%.4f)", c1, c8);
        h2.bpm = 90.0;
        VEXStutterAudioProcessor s2;
        s2.setPlayHead (&h2);
        s2.setPlayConfigDetails (2, 2, 48000.0, 480);
        s2.prepareToPlay (48000.0, 480);
        setReal (s2, ids::amount, 100.0f);
        setReal (s2, ids::length, 100.0f);
        setReal (s2, ids::smoothness, 0.0f);
        const auto out90 = run (s2, h2, in, 48000.0, 480);
        const int slice90 = 16000;
        CHECK (corr (out90.data() + slice90 + guard, in.data() + guard, slice90 - 2 * guard) > 0.999,
               "host at 90 BPM: the slice follows the tempo (333 ms)");
        s.setPlayHead (nullptr);
        s2.setPlayHead (nullptr);

        // never prepared / zero-length blocks
        VEXStutterAudioProcessor u;
        juce::AudioBuffer<float> raw (2, 64), rawRef (2, 64);
        fill (raw, rng);
        rawRef.makeCopyOf (raw, true);
        u.processBlock (raw, midi);
        bool unprepared = std::memcmp (raw.getReadPointer (0), rawRef.getReadPointer (0), sizeof (float) * 64) == 0;
        u.setPlayConfigDetails (2, 2, 48000.0, 64);
        u.prepareToPlay (48000.0, 64);
        juce::AudioBuffer<float> empty (2, 0);
        u.processBlock (empty, midi);
        juce::AudioBuffer<float> big (2, 8192);
        fill (big, rng);
        u.processBlock (big, midi);                   // larger than announced
        CHECK (unprepared, "before prepareToPlay: dry pass-through; zero-length and oversized blocks are accepted");
    }

    //--------------------------------------------------------------------------
    std::printf ("\n=== 4) State save / load ===\n");
    {
        VEXStutterAudioProcessor a;
        const auto saved0 = saveState (a);
        CHECK (saveState (a) == saved0, "saving twice gives the same bytes (%d bytes)", (int) saved0.getSize());

        std::mt19937 rng (11);
        for (const char* id : ids::all)
            param (a, id)->setValueNotifyingHost (std::uniform_real_distribution<float> (0.0f, 1.0f) (rng));
        a.setSelection (0.3f, 0.55f);
        a.setUiScale (1.25f);
        const auto changed = values (a);
        const auto saved1 = saveState (a);

        VEXStutterAudioProcessor b;
        b.setStateInformation (saved1.getData(), (int) saved1.getSize());
        CHECK (values (b) == changed, "all 24 changed values come back exactly in a new instance");
        CHECK (juce::exactlyEqual (b.getSelectionStart(), 0.3f) && juce::exactlyEqual (b.getSelectionEnd(), 0.55f)
               && juce::exactlyEqual (b.getUiScale(), 1.25f), "the waveform selection and the interface size are restored");
        CHECK (saveState (b) == saved1, "restore + save gives the same bytes");

        const auto xml = juce::AudioProcessor::getXmlFromBinary (saved1.getData(), (int) saved1.getSize());
        CHECK (xml != nullptr && (int) xml->getIntAttribute ("stateVersion") == VEXStutterAudioProcessor::kStateVersion
               && xml->getStringAttribute ("pluginVersion") == VEX_VERSION_STRING,
               "state carries stateVersion %d and pluginVersion %s", VEXStutterAudioProcessor::kStateVersion, VEX_VERSION_STRING);

        VEXStutterAudioProcessor c;
        c.setStateInformation (saved1.getData(), (int) saved1.getSize());
        c.setStateInformation (nullptr, 100);
        c.setStateInformation (saved1.getData(), 0);
        std::vector<char> garbage (4096);
        for (auto& g : garbage) g = (char) (rng() & 0xff);
        c.setStateInformation (garbage.data(), (int) garbage.size());
        c.setStateInformation (saved1.getData(), (int) saved1.getSize() / 2);
        loadXml (c, "<OtherPlugin><PARAM id=\"amount\" value=\"3\"/></OtherPlugin>");
        CHECK (values (c) == changed, "null, empty, random bytes, truncated data and another plug-in's state are ignored");

        loadXml (c, "<VEXStutterState stateVersion=\"1\" selectionStart=\"nan\" selectionEnd=\"7\" uiScale=\"99\">"
                    "<PARAM id=\"amount\" value=\"nan\"/><PARAM id=\"mix\" value=\"inf\"/>"
                    "<PARAM id=\"length\" value=\"250\"/><PARAM id=\"smoothness\" value=\"-40\"/>"
                    "<PARAM id=\"rate\" value=\"99\"/><PARAM id=\"mode\" value=\"3.6\"/>"
                    "<PARAM id=\"offset_ms\" value=\"hello\"/><PARAM id=\"delay_feedback\" value=\"100\"/>"
                    "<PARAM id=\"glitch_type\" value=\"-4\"/><PARAM id=\"no_such_param\" value=\"1\"/>"
                    "<PARAM id=\"style\" value=\"4\"/><PARAM id=\"style\" value=\"1\"/>"
                    "</VEXStutterState>");
        CHECK (text (c, ids::amount) == "75.0" && text (c, ids::length) == "100.0" && text (c, ids::smoothness) == "0.0"
               && text (c, ids::rate) == "1/32T" && text (c, ids::delayFeedback) == "85.0" && text (c, ids::glitchType) == "Pixel",
               "NaN -> default, out of range -> clamped (length %s, smoothness %s, rate %s, feedback %s, glitch type %s)",
               text (c, ids::length).toRawUTF8(), text (c, ids::smoothness).toRawUTF8(), text (c, ids::rate).toRawUTF8(),
               text (c, ids::delayFeedback).toRawUTF8(), text (c, ids::glitchType).toRawUTF8());
        CHECK (text (c, ids::mix) == "100.0" && text (c, ids::mode) == "Glitch" && text (c, ids::offsetMs) == "0.0" && text (c, ids::style) == "Tape",
               "inf -> default, 3.6 -> nearest choice (%s), text -> default, duplicate id: the first wins (%s)",
               text (c, ids::mode).toRawUTF8(), text (c, ids::style).toRawUTF8());
        CHECK (juce::exactlyEqual (c.getSelectionStart(), 0.75f) && juce::exactlyEqual (c.getSelectionEnd(), 1.0f)
               && juce::exactlyEqual (c.getUiScale(), VEXStutterAudioProcessor::kMaxUiScale),
               "damaged selection -> default (the last quarter; an out-of-range edge is clamped), interface size clamped to %.1f",
               (double) VEXStutterAudioProcessor::kMaxUiScale);

        VEXStutterAudioProcessor d;
        loadXml (d, "<VEXStutterState><PARAM id=\"amount\" value=\"20\"/></VEXStutterState>");
        CHECK (text (d, ids::amount) == "20.0" && text (d, ids::mix) == "100.0" && text (d, ids::glitchType) == "Pixel",
               "state without a version: its value is used, everything missing takes the default");
        loadXml (d, "<VEXStutterState stateVersion=\"7\" futureFeature=\"x\"><PARAM id=\"amount\" value=\"33\"/>"
                    "<PARAM id=\"future_param\" value=\"5\"/><FUTURE_NODE a=\"1\"/></VEXStutterState>");
        CHECK (text (d, ids::amount) == "33.0" && text (d, ids::length) == "50.0",
               "state from a newer version: known values load, unknown ones are ignored");

        // session restores while audio runs on another thread (both states at Amount 0: audio must stay exact)
        VEXStutterAudioProcessor s0, s1;
        for (const char* id : ids::all)
        {
            param (s0, id)->setValueNotifyingHost (std::uniform_real_distribution<float> (0.0f, 1.0f) (rng));
            param (s1, id)->setValueNotifyingHost (std::uniform_real_distribution<float> (0.0f, 1.0f) (rng));
        }
        setReal (s0, ids::amount, 0.0f);
        setReal (s1, ids::amount, 0.0f);
        const auto stA = saveState (s0), stB = saveState (s1);
        VEXStutterAudioProcessor e;
        e.setStateInformation (stA.getData(), (int) stA.getSize());
        e.setPlayConfigDetails (2, 2, 48000.0, 64);
        e.prepareToPlay (48000.0, 64);
        std::atomic<bool> stop { false }, bad { false };
        std::atomic<long> processed { 0 };
        std::thread audio ([&]
        {
            juce::AudioBuffer<float> buf (2, 64), ref (2, 64);
            juce::MidiBuffer midi;
            std::mt19937 r (3);
            while (! stop)
            {
                fill (buf, r);
                ref.makeCopyOf (buf, true);
                e.processBlock (buf, midi);
                for (int ch = 0; ch < 2; ++ch)
                    if (std::memcmp (buf.getReadPointer (ch), ref.getReadPointer (ch), sizeof (float) * 64) != 0) bad = true;
                ++processed;
            }
        });
        for (int i = 0; i < 300; ++i)
        {
            e.setStateInformation ((i % 2 ? stB : stA).getData(), (int) (i % 2 ? stB : stA).getSize());
            e.setSelection ((float) (i % 7) / 10.0f, 0.9f);
        }
        stop = true;
        audio.join();
        CHECK (! bad && processed > 0, "300 session restores while audio runs on another thread: audio exact, no crash (%ld blocks)", processed.load());
    }

    //--------------------------------------------------------------------------
    std::printf ("\n=== 5) Presets ===\n");
    {
        VEXStutterAudioProcessor p;
        auto& pm = p.getPresetManager();
        pm.setUserDirectory (tempPresets);
        const auto& list = pm.getPresets();
        int factory = 0;
        std::set<juce::String> names;
        for (const auto& pr : list) { factory += pr.factory ? 1 : 0; names.insert (pr.name); }
        CHECK (factory == 15 && (int) names.size() == (int) list.size() && list[0].name == "Init",
               "%d factory presets, names unique, the first is Init", factory);

        setReal (p, ids::bypass, 1.0f);
        setReal (p, ids::freeze, 1.0f);
        bool allLegal = true, keepsPerformance = true, notModified = true;
        for (int i = 0; i < factory; ++i)
        {
            pm.load (i);
            for (auto* prm : p.getParameters())
                allLegal = allLegal && std::isfinite (prm->getValue()) && prm->getValue() >= 0.0f && prm->getValue() <= 1.0f;
            keepsPerformance = keepsPerformance && text (p, ids::bypass) == "On" && text (p, ids::freeze) == "On";
            notModified = notModified && ! pm.isModified() && pm.getCurrentIndex() == i;
        }
        CHECK (allLegal && notModified, "every factory preset loads with legal values and reads as unmodified");
        CHECK (keepsPerformance, "presets never change Bypass or Freeze");
        pm.load (0);
        bool initDefaults = true;
        for (const char* id : ids::all)
            if (vex::presets::PresetManager::isPresetParameter (id))
                initDefaults = initDefaults && juce::exactlyEqual (param (p, id)->getValue(), param (p, id)->getDefaultValue());
        CHECK (initDefaults, "Init = every parameter at its default");
        setReal (p, ids::amount, 42.0f);
        CHECK (pm.isModified(), "changing a value marks the preset as modified");

        // user presets in a temporary folder
        const auto r1 = pm.saveAs ("My Test: Preset/1", false);
        CHECK (r1.wasOk() && tempPresets.getChildFile ("My Test Preset1.vexpreset").existsAsFile() && pm.getCurrentName() == "My Test Preset1"
               && ! pm.isModified(), "Save As: file name cleaned, saved, selected (%s)", r1.getErrorMessage().toRawUTF8());
        CHECK (pm.saveAs ("My Test Preset1", false).failed() && pm.saveAs ("My Test Preset1", true).wasOk(), "an existing name needs explicit overwrite");
        pm.load (1);
        const int user = pm.getCurrentIndex();
        juce::ignoreUnused (user);
        int idx = -1;
        for (int i = 0; i < (int) pm.getPresets().size(); ++i)
            if (! pm.getPresets()[(size_t) i].factory) idx = i;
        pm.load (idx);
        CHECK (text (p, ids::amount) == "42.0", "loading the user preset restores its values");
        CHECK (pm.rename (idx, "Renamed").wasOk() && tempPresets.getChildFile ("Renamed.vexpreset").existsAsFile()
               && ! tempPresets.getChildFile ("My Test Preset1.vexpreset").exists() && pm.getCurrentName() == "Renamed", "rename");
        CHECK (pm.rename (0, "x").failed() && pm.remove (0).failed(), "factory presets cannot be renamed or deleted");

        // the session remembers the preset
        const auto st = saveState (p);
        VEXStutterAudioProcessor q;
        q.getPresetManager().setUserDirectory (tempPresets);
        q.setStateInformation (st.getData(), (int) st.getSize());
        CHECK (q.getPresetManager().getCurrentName() == "Renamed" && ! q.getPresetManager().isModified(), "a restored session shows its preset name");

        // malformed files never load and never crash
        tempPresets.getChildFile ("garbage.vexpreset").replaceWithText ("<<<this is not xml");
        tempPresets.getChildFile ("wrongtag.vexpreset").replaceWithText ("<OtherPreset formatVersion=\"1\" name=\"x\"/>");
        tempPresets.getChildFile ("noversion.vexpreset").replaceWithText ("<VEXStutterPreset name=\"y\"/>");
        juce::String huge ("<VEXStutterPreset formatVersion=\"1\" name=\"huge\">");
        for (int i = 0; i < 20000; ++i) huge << "<PARAM id=\"amount\" value=\"1\"/>";
        huge << "</VEXStutterPreset>";
        tempPresets.getChildFile ("huge.vexpreset").replaceWithText (huge);
        tempPresets.getChildFile ("nan.vexpreset").replaceWithText ("<VEXStutterPreset formatVersion=\"1\" name=\"Bad Values\">"
                                                                    "<PARAM id=\"amount\" value=\"nan\"/><PARAM id=\"mix\" value=\"1e30\"/>"
                                                                    "<PARAM id=\"bypass\" value=\"1\"/><PARAM id=\"mode\" value=\"99\"/></VEXStutterPreset>");
        pm.rescan();
        int userCount = 0, badIndex = -1;
        for (int i = 0; i < (int) pm.getPresets().size(); ++i)
            if (! pm.getPresets()[(size_t) i].factory)
            {
                ++userCount;
                if (pm.getPresets()[(size_t) i].name == "Bad Values") badIndex = i;
            }
        CHECK (userCount == 2 && badIndex >= 0, "malformed, foreign, unversioned and oversized files are skipped (%d user presets listed)", userCount);
        setReal (p, ids::bypass, 0.0f);
        pm.load (badIndex);
        CHECK (text (p, ids::amount) == "75.0" && text (p, ids::mix) == "100.0" && text (p, ids::mode) == "Glitch" && text (p, ids::bypass) == "Off",
               "bad values in a preset: NaN -> default, out of range -> clamped, Bypass ignored");
        CHECK (pm.importFile (tempPresets.getChildFile ("garbage.vexpreset")).failed(), "importing a broken file is refused");
        tempPresets.deleteRecursively();
    }

    //--------------------------------------------------------------------------
    std::printf ("\n=== 5b) Clip export (freeze, select, drag into the DAW) ===\n");
    {
        VEXStutterAudioProcessor p;
        TestPlayHead head;
        p.setPlayHead (&head);
        p.setPlayConfigDetails (2, 2, 48000.0, 480);
        p.prepareToPlay (48000.0, 480);
        const auto in = voice (4.0, 48000.0);
        run (p, head, in, 48000.0, 480);
        juce::AudioBuffer<float> clip;
        juce::String error;
        const bool refusedLive = ! p.renderSelection (clip, error) && error.isNotEmpty();
        setReal (p, ids::freeze, 1.0f);
        run (p, head, std::vector<float> (in.begin(), in.begin() + 48000), 48000.0, 480);
        p.setSelection (0.25f, 0.5f);
        const auto frozenLen = p.getCapture().uiFrozenLength();
        error.clear();
        const bool rendered = p.renderSelection (clip, error);
        double peak = 0.0;
        bool finite = true;
        for (int ch = 0; ch < clip.getNumChannels(); ++ch)
            for (int i = 0; i < clip.getNumSamples(); ++i)
            {
                const float v = clip.getSample (ch, i);
                finite = finite && std::isfinite (v);
                peak = std::max (peak, (double) std::abs (v));
            }
        const int selFrames = (int) (0.25 * (double) frozenLen);
        const auto file = p.renderSelectionClip (error);
        bool wavOk = false;
        int64_t wavLen = 0;
        if (file.existsAsFile())
        {
            juce::AudioFormatManager fm;
            fm.registerBasicFormats();
            if (std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (file)); r != nullptr)
            {
                wavLen = r->lengthInSamples;
                wavOk = std::abs (r->sampleRate - 48000.0) < 0.5 && r->numChannels == 2 && r->bitsPerSample == 24
                     && r->lengthInSamples == clip.getNumSamples();
            }
            file.deleteFile();
        }
        CHECK (refusedLive && rendered && finite && peak > 0.01 && clip.getNumSamples() >= selFrames && wavOk,
               "clip export: refused while live (%s), frozen %.2f s, selection 25 %% -> %d frames rendered (>= %d), peak %.2f, finite %s, WAV 48 kHz / 2 ch / 24 bit / %lld frames %s",
               refusedLive ? "yes" : "NO", (double) frozenLen / 48000.0, clip.getNumSamples(), selFrames, peak, finite ? "yes" : "NO",
               (long long) wavLen, wavOk ? "ok" : "WRONG");
        // a copy across an unfreeze is reported invalid (seqlock), never torn audio
        std::vector<float> l (1000), r (1000);
        const bool copyFrozenOk = p.getCapture().copyFrozen (0, 1000, l.data(), r.data());
        setReal (p, ids::freeze, 0.0f);
        run (p, head, std::vector<float> (in.begin(), in.begin() + 4800), 48000.0, 480);
        const bool copyAfterUnfreeze = p.getCapture().copyFrozen (0, 1000, l.data(), r.data());
        CHECK (copyFrozenOk && ! copyAfterUnfreeze && ! p.renderSelection (clip, error),
               "frozen copy valid while frozen, refused after unfreeze");
    }

    //--------------------------------------------------------------------------
    std::printf ("\n=== 6) Editor ===\n");
    {
        VEXStutterAudioProcessor p;
        TestPlayHead head;
        p.setPlayHead (&head);
        p.setPlayConfigDetails (2, 2, 48000.0, 480);
        p.prepareToPlay (48000.0, 480);
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditorIfNeeded());
        auto* editor = dynamic_cast<VEXStutterAudioProcessorEditor*> (ed.get());
        CHECK (editor != nullptr && ed->getWidth() == VEXStutterAudioProcessorEditor::kWidth && ed->getHeight() == VEXStutterAudioProcessorEditor::kHeight,
               "editor opens at %d x %d", ed ? ed->getWidth() : 0, ed ? ed->getHeight() : 0);

        if (editor != nullptr)
        {
            // real audio through the plug-in so the waveform, meters and step display show real data
            const auto in = voice (6.0, 48000.0);
            setReal (p, ids::rate, 10.0f);          // 1/16
            setReal (p, ids::reverbMix, 18.0f);
            run (p, head, in, 48000.0, 480);
            editor->refreshNow();
            pumpUi (120);
            editor->refreshNow();

            if (shotDir != juce::File())
            {
                shotDir.createDirectory();
                CHECK (savePng (*ed, shotDir.getChildFile ("01_main.png"), 1.0f) && savePng (*ed, shotDir.getChildFile ("01_main@2x.png"), 2.0f),
                       "screenshots: main view (1x, 2x)");

                setReal (p, ids::mode, (float) (int) vex::choices::Mode::glitch);
                setReal (p, ids::style, (float) (int) vex::choices::Style::digital);
                run (p, head, std::vector<float> (in.begin(), in.begin() + 96000), 48000.0, 480);
                editor->refreshNow();
                pumpUi (60);
                CHECK (savePng (*ed, shotDir.getChildFile ("02_glitch.png"), 1.0f), "screenshot: Glitch mode");

                setReal (p, ids::mode, (float) (int) vex::choices::Mode::stutter);
                setReal (p, ids::style, 0.0f);
                setReal (p, ids::freeze, 1.0f);
                p.setSelection (0.38f, 0.62f);
                run (p, head, std::vector<float> (in.begin(), in.begin() + 96000), 48000.0, 480);
                editor->refreshNow();
                pumpUi (60);
                CHECK (p.getCapture().uiFrozen() && savePng (*ed, shotDir.getChildFile ("03_frozen.png"), 1.0f), "screenshot: frozen capture with a selection");
                setReal (p, ids::freeze, 0.0f);

                editor->setFxDetailsVisible (true);
                setReal (p, ids::delayMix, 25.0f);
                pumpUi (60);
                CHECK (savePng (*ed, shotDir.getChildFile ("04_fx_details.png"), 1.0f), "screenshot: FX details");
                editor->setFxDetailsVisible (false);

                setReal (p, ids::bypass, 1.0f);
                setReal (p, ids::sync, 0.0f);
                run (p, head, std::vector<float> (in.begin(), in.begin() + 24000), 48000.0, 480);
                editor->refreshNow();
                pumpUi (60);
                CHECK (savePng (*ed, shotDir.getChildFile ("05_bypassed_free.png"), 1.0f), "screenshot: bypassed, Free time");
                setReal (p, ids::bypass, 0.0f);
                setReal (p, ids::sync, 1.0f);

                editor->setUiScale (0.7f);
                pumpUi (60);
                CHECK (ed->getWidth() == 784 && savePng (*ed, shotDir.getChildFile ("06_scale70.png"), 1.0f), "70 %% size: %d x %d", ed->getWidth(), ed->getHeight());
                editor->setUiScale (1.5f);
                pumpUi (60);
                CHECK (ed->getWidth() == 1680 && savePng (*ed, shotDir.getChildFile ("07_scale150.png"), 1.0f), "150 %% size: %d x %d", ed->getWidth(), ed->getHeight());
                editor->setUiScale (1.0f);
            }
        }
        ed.reset();

        // the editor opened and closed repeatedly while audio runs
        std::atomic<bool> stop { false };
        std::thread audio ([&]
        {
            juce::AudioBuffer<float> buf (2, 480);
            juce::MidiBuffer midi;
            std::mt19937 r (1);
            while (! stop)
            {
                fill (buf, r);
                p.processBlock (buf, midi);
            }
        });
        bool opened = true;
        for (int i = 0; i < 20; ++i)
        {
            std::unique_ptr<juce::AudioProcessorEditor> e2 (p.createEditorIfNeeded());
            opened = opened && e2 != nullptr;
            pumpUi (20);
        }
        stop = true;
        audio.join();
        CHECK (opened, "editor opened and closed 20 times while audio runs: no crash");
        p.setPlayHead (nullptr);
    }

    //--------------------------------------------------------------------------
    if (vst3File != juce::File())
    {
        std::printf ("\n=== 7) Built VST3, loaded like a host ===\n");
        juce::VST3PluginFormat format;
        juce::OwnedArray<juce::PluginDescription> found;
        format.findAllTypesForFile (found, vst3File.getFullPathName());
        CHECK (found.size() == 1, "scan: %d plug-in(s) in %s", found.size(), vst3File.getFileName().toRawUTF8());
        if (found.size() == 1)
        {
            CHECK (found[0]->name == "VEX Stutter" && found[0]->manufacturerName == "VEX" && found[0]->version == VEX_VERSION_STRING
                   && ! found[0]->isInstrument,
                   "identity: %s %s by %s, effect", found[0]->name.toRawUTF8(), found[0]->version.toRawUTF8(), found[0]->manufacturerName.toRawUTF8());

            juce::AudioPluginFormatManager manager;
            manager.addFormat (new juce::VST3PluginFormat());
            for (int channels : { 2, 1 })
            {
                juce::String error;
                auto plugin = manager.createPluginInstance (*found[0], 48000.0, 512, error);
                CHECK (plugin != nullptr, "%s instance created %s", channels == 2 ? "stereo" : "mono", error.toRawUTF8());
                if (plugin == nullptr)
                    continue;

                const auto set = channels == 2 ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::mono();
                juce::AudioProcessor::BusesLayout layout;
                layout.inputBuses.add (set);
                layout.outputBuses.add (set);
                CHECK (plugin->setBusesLayout (layout), "%s layout accepted by the VST3", channels == 2 ? "stereo" : "mono");
                plugin->prepareToPlay (48000.0, 512);

                juce::AudioProcessorParameter* amount = nullptr;
                for (auto* prm : plugin->getParameters())
                    if (prm->getName (64) == "Amount") amount = prm;
                if (amount == nullptr) { CHECK (false, "Amount parameter not found in the VST3"); continue; }

                std::mt19937 rng (5);
                juce::AudioBuffer<float> buf (channels, 512), ref (channels, 512);
                juce::MidiBuffer midi;
                auto maxDiff = [&] (int blocks)
                {
                    double m = 0.0;
                    for (int n = 0; n < blocks; ++n)
                    {
                        fill (buf, rng);
                        ref.makeCopyOf (buf, true);
                        plugin->processBlock (buf, midi);
                        for (int ch = 0; ch < channels; ++ch)
                            for (int i = 0; i < 512; ++i)
                                m = std::max (m, (double) std::abs (buf.getSample (ch, i) - ref.getSample (ch, i)));
                    }
                    return m;
                };
                amount->setValueNotifyingHost (0.0f);
                maxDiff (4);
                const double nullDiff = maxDiff (200);
                CHECK (juce::exactlyEqual (nullDiff, 0.0), "%s null test through the VST3 at Amount 0: max |out - in| = %g over 200 blocks",
                       channels == 2 ? "stereo" : "mono", nullDiff);
                amount->setValueNotifyingHost (1.0f);
                const double wetDiff = maxDiff (200);
                CHECK (wetDiff > 0.1, "%s at Amount 100 %%: the effect changes the signal (max |out - in| = %.2f)", channels == 2 ? "stereo" : "mono", wetDiff);

                if (channels == 2)
                {
                    CHECK (plugin->getLatencySamples() == 0 && plugin->getParameters().size() >= 24,
                           "latency %d, %d parameters visible to the host", plugin->getLatencySamples(), plugin->getParameters().size());
                    CHECK (plugin->getBypassParameter() != nullptr && plugin->getBypassParameter()->getName (64) == "Bypass",
                           "the VST3 declares its Bypass parameter as the host bypass");

                    amount->setValueNotifyingHost (0.2f);
                    juce::MemoryBlock st;
                    plugin->getStateInformation (st);
                    amount->setValueNotifyingHost (0.9f);
                    plugin->setStateInformation (st.getData(), (int) st.getSize());
                    CHECK (std::abs (amount->getValue() - 0.2f) < 1e-4f, "state round trip through the VST3 (Amount %s)", amount->getCurrentValueAsText().toRawUTF8());

                    std::unique_ptr<juce::AudioProcessorEditor> ed (plugin->createEditorIfNeeded());
                    CHECK (ed != nullptr && ed->getWidth() == VEXStutterAudioProcessorEditor::kWidth, "VST3 editor opens (%d x %d)",
                           ed ? ed->getWidth() : 0, ed ? ed->getHeight() : 0);
                    ed.reset();
                }
                plugin->releaseResources();
            }
        }
    }

    std::printf ("\n%s: %d checks, %d failure%s\n", failures == 0 ? "ALL PASSED" : "FAILED", checks, failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
