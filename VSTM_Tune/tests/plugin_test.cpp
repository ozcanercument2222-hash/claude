//==============================================================================
//  VSTM Tune - plugin katmani testi + arayuz ekran goruntuleri
//
//    VSTMTune_PluginTest [goruntu_klasoru] [--vst3 yol/VSTM Tune.vst3]
//
//  Linux'ta ekran olmadan:  xvfb-run -a VSTMTune_PluginTest goruntuler
//==============================================================================
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Parameters.h"

#include <cstdio>

namespace
{
int failures = 0;
constexpr double kFs = 48000.0;

#define CHECK(cond, ...) do { const bool ok_ = (cond); std::printf ("  [%s] ", ok_ ? " OK " : "FAIL"); std::printf (__VA_ARGS__); std::printf ("\n"); if (! ok_) ++failures; } while (0)

void pump (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }

bool savePng (juce::Component& c, const juce::File& file, float scale)
{
    const auto image = c.createComponentSnapshot (c.getLocalBounds(), true, scale);
    file.deleteFile();
    juce::FileOutputStream out (file);
    const bool ok = out.openedOk() && juce::PNGImageFormat().writeImageToStream (image, out);
    if (ok) std::printf ("  wrote %s\n", file.getFullPathName().toRawUTF8());
    return ok;
}

//------------------------------------------------------------------------------
/** Synthetic sung vowel with a melody, slides, vibrato and a little detune. */
struct Voice
{
    struct Res
    {
        double b0 = 0, a1 = 0, a2 = 0, y1 = 0, y2 = 0;
        void set (double f, double bw) { const double r = std::exp (-juce::MathConstants<double>::pi * bw / kFs);
                                         a1 = -2.0 * r * std::cos (2.0 * juce::MathConstants<double>::pi * f / kFs); a2 = r * r; b0 = (1.0 - r) * 2.0; }
        double run (double x) { const double y = b0 * x - a1 * y1 - a2 * y2; y2 = y1; y1 = y; return y; }
    } f1, f2, f3;

    std::vector<double> notes;      // MIDI
    double noteLen = 0.5, detune = 0.0, vibrato = 0.0;
    double phase = 0.0, prev = 0.0, t = 0.0;
    float level = 0.25f;

    Voice() { f1.set (730.0, 90.0); f2.set (1090.0, 110.0); f3.set (2440.0, 170.0); }

    double f0At (double time) const
    {
        if (notes.empty()) return 220.0;
        const double pos = time / noteLen;
        const int i = (int) pos % (int) notes.size();
        const int j = (i + 1) % (int) notes.size();
        const double frac = pos - std::floor (pos);
        double m = notes[(size_t) i];
        if (frac > 0.9) m += (notes[(size_t) j] - m) * juce::jlimit (0.0, 1.0, (frac - 0.9) / 0.1);   // short slide
        m += detune * std::sin (i * 2.1 + 0.4) + vibrato * std::sin (2.0 * juce::MathConstants<double>::pi * 5.5 * time);
        return 440.0 * std::pow (2.0, (m - 69.0) / 12.0);
    }

    float next()
    {
        const double f0 = f0At (t);
        t += 1.0 / kFs;
        phase += f0 / kFs;
        double s = 0.0;
        const int harmonics = juce::jmin (36, (int) (0.45 * kFs / f0));
        for (int h = 1; h <= harmonics; ++h)
            s += std::sin (2.0 * juce::MathConstants<double>::pi * h * phase) / (double) (h * h) * (h == 1 ? 1.0 : 1.6);
        const double p = s - prev;
        prev = s;
        return (float) ((f1.run (p) + 0.7 * f2.run (p) + 0.35 * f3.run (p)) * 9.0) * level;
    }
};

double processSeconds (juce::AudioProcessor& p, Voice& v, double seconds, std::vector<float>* out = nullptr, int block = 512)
{
    juce::AudioBuffer<float> buf (2, block);
    juce::MidiBuffer midi;
    double energy = 0.0;
    const int total = (int) (seconds * kFs);
    for (int pos = 0; pos < total; pos += block)
    {
        for (int i = 0; i < block; ++i)
        {
            const float s = v.next();
            buf.setSample (0, i, s);
            buf.setSample (1, i, s);
        }
        p.processBlock (buf, midi);
        for (int i = 0; i < block; ++i)
        {
            energy += (double) buf.getSample (0, i) * buf.getSample (0, i);
            if (out) out->push_back (buf.getSample (0, i));
        }
        if ((pos / block) % 8 == 0)
            pump (1);
    }
    return 10.0 * std::log10 (energy / total + 1.0e-20);
}

double measureHz (const std::vector<float>& x, int start, int length, double expectHz)
{
    const double expectT = kFs / expectHz;
    const int lo = (int) (expectT * 0.8), hi = (int) (expectT * 1.25) + 2;
    std::vector<double> r ((size_t) hi + 2, 0.0);
    for (int lag = lo - 1; lag <= hi + 1; ++lag)
    {
        double num = 0, ea = 0, eb = 0;
        for (int j = 0; j < length; ++j)
        {
            const double a = x[(size_t) (start + j)], b = x[(size_t) (start + j + lag)];
            num += a * b; ea += a * a; eb += b * b;
        }
        r[(size_t) lag] = num / std::sqrt (ea * eb + 1e-20);
    }
    int best = lo;
    for (int lag = lo; lag <= hi; ++lag) if (r[(size_t) lag] > r[(size_t) best]) best = lag;
    const double a = r[(size_t) best - 1], b = r[(size_t) best], c = r[(size_t) best + 1];
    const double den = a - 2 * b + c;
    return kFs / (best + (std::abs (den) > 1e-12 ? 0.5 * (a - c) / den : 0.0));
}

void setParam (juce::AudioProcessor& p, const juce::String& id, float plainValue)
{
    auto& vp = dynamic_cast<VSTMTuneProcessor&> (p);
    auto* prm = vp.apvts.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (plainValue));
}

Voice referenceMelody()
{
    Voice v;
    // B3 D4 B3 E4 A4 F4 E4 D4 A3 A4 C4 ... sung a little out of tune, with vibrato
    v.notes = { 59, 62, 61, 59, 64, 69, 69, 65, 66, 64, 57, 57, 69, 69, 64, 60 };
    v.noteLen = 0.42;
    v.detune = 0.22;
    v.vibrato = 0.12;
    return v;
}

/** Every parameter holds a number, and one second of singing comes out as finite samples. */
bool paramsAndAudioFinite (VSTMTuneProcessor& p)
{
    for (auto* prm : p.getParameters())
        if (! std::isfinite (prm->getValue()))
            return false;
    p.setPlayConfigDetails (2, 2, kFs, 512);
    p.prepareToPlay (kFs, 512);
    Voice v;
    v.notes = { 57.35 };
    std::vector<float> out;
    processSeconds (p, v, 1.0, &out);
    p.releaseResources();
    for (float s : out)
        if (! std::isfinite (s))
            return false;
    return true;
}

/** A saved session of a fresh processor, with its XML edited. */
juce::MemoryBlock editedState (const std::function<void (juce::XmlElement&)>& edit)
{
    VSTMTuneProcessor a;
    juce::MemoryBlock mb;
    a.getStateInformation (mb);
    auto xml = juce::AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize());
    edit (*xml);
    juce::MemoryBlock out;
    juce::AudioProcessor::copyXmlToBinary (*xml, out);
    return out;
}

void setStateValue (juce::XmlElement& state, const char* id, const char* value)
{
    for (auto* e : state.getChildWithTagNameIterator ("PARAM"))
        if (e->getStringAttribute ("id") == id)
            e->setAttribute ("value", value);
}
} // namespace

//==============================================================================
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

    // the checks below are written against the English texts; section 1b covers the other languages
    using vstm::i18n::Lang;
    vstm::i18n::setCurrent (Lang::english);

    //--------------------------------------------------------------------------
    std::printf ("\n=== 1) Parameters ===\n");
    {
        VSTMTuneProcessor p;
        auto text = [&p] (const juce::String& id) { return p.apvts.getParameter (id)->getCurrentValueAsText(); };
        CHECK (text (vstm::ids::key) == "Auto" && text (vstm::ids::scale) == "Major", "defaults: Key Auto, Scale Major");
        CHECK (text (vstm::ids::correction) == "100%" && text (vstm::ids::retune) == "15 ms" && text (vstm::ids::humanize) == "35%",
               "defaults: %s / %s / %s", text (vstm::ids::correction).toRawUTF8(), text (vstm::ids::retune).toRawUTF8(), text (vstm::ids::humanize).toRawUTF8());
        CHECK (text (vstm::ids::robotism) == "Balanced", "robotism default text: %s", text (vstm::ids::robotism).toRawUTF8());
        setParam (p, vstm::ids::robotism, 0.0f);
        const auto robotText = text (vstm::ids::robotism);
        setParam (p, vstm::ids::robotism, 80.0f);
        const auto humanText = text (vstm::ids::robotism);
        CHECK (robotText == "Robotic 100%" && humanText == "Human 60%", "robotism text: \"%s\" / \"%s\"", robotText.toRawUTF8(), humanText.toRawUTF8());
        auto* r = p.apvts.getParameter (vstm::ids::robotism);
        CHECK (std::abs (r->convertFrom0to1 (r->getValueForText ("Robotic 40%")) - 30.0f) < 0.01f, "robotism parses its own text");
        CHECK (text (vstm::ids::formant) == "0.0" && text (vstm::ids::transpose) == "0 st" && text (vstm::ids::mix) == "100%" && text (vstm::ids::vibrato) == "100%",
               "PARAMETERS row: %s / %s / %s / %s", text (vstm::ids::vibrato).toRawUTF8(), text (vstm::ids::formant).toRawUTF8(),
               text (vstm::ids::transpose).toRawUTF8(), text (vstm::ids::mix).toRawUTF8());
        CHECK (p.getBypassParameter() == p.apvts.getParameter (vstm::ids::bypass), "host bypass is the AUTOTUNE ON switch");
    }

    //--------------------------------------------------------------------------
    std::printf ("\n=== 1b) Languages ===\n");
    {
        namespace i18n = vstm::i18n;
        const auto missing = i18n::firstIncompleteEntry();
        CHECK (missing.isEmpty(), "%d texts, every one in all 7 languages %s", i18n::numEntries(), missing.toRawUTF8());

        struct Case { const char* code; Lang expect; };
        const Case cases[] = { { "tr-TR", Lang::turkish }, { "de_DE", Lang::german }, { "fr-CA", Lang::french }, { "it", Lang::italian },
                               { "ru-RU", Lang::russian }, { "zh-Hans-CN", Lang::chinese }, { "zh_TW", Lang::chinese }, { "en-GB", Lang::english },
                               { "pt-BR", Lang::english }, { "ja-JP", Lang::english }, { "", Lang::english } };
        bool mapped = true;
        for (const auto& c : cases)
            mapped = mapped && i18n::fromCode (c.code) == c.expect;
        CHECK (mapped, "OS language codes: tr/de/fr/it/ru/zh map to themselves, anything else to English");

        // every language: parameter texts come out in that language and are read back in any language
        bool texts = true, parse = true, distinct = true;
        juce::StringArray titles;
        for (int l = 0; l < i18n::kNumLangs; ++l)
        {
            i18n::setCurrent ((Lang) l);
            VSTMTuneProcessor p;
            auto* rb = p.apvts.getParameter (vstm::ids::robotism);
            texts = texts && rb->getCurrentValueAsText() == i18n::tr ("Balanced")
                          && p.apvts.getParameter (vstm::ids::scale)->getCurrentValueAsText() == i18n::tr ("Major")
                          && p.apvts.getParameter (vstm::ids::key)->getCurrentValueAsText() == i18n::tr ("Auto");
            const auto robotic40 = rb->getText (rb->convertTo0to1 (30.0f), 64);   // "Robotik 40%", "机械 40%", ...
            const auto human60 = rb->getText (rb->convertTo0to1 (80.0f), 64);
            for (int other = 0; other < i18n::kNumLangs; ++other)
            {
                i18n::setCurrent ((Lang) other);
                parse = parse && std::abs (rb->convertFrom0to1 (rb->getValueForText (robotic40)) - 30.0f) < 0.01f
                              && std::abs (rb->convertFrom0to1 (rb->getValueForText (human60)) - 80.0f) < 0.01f;
            }
            titles.addIfNotAlreadyThere (i18n::tr ("PITCH CORRECTION", (Lang) l));
        }
        distinct = titles.size() == i18n::kNumLangs;
        i18n::setCurrent (Lang::english);
        CHECK (texts, "defaults read Balanced / Major / Auto in each language");
        CHECK (parse, "robotism text typed in any language is understood in every language");
        CHECK (distinct, "card labels really are translated (%d different texts for PITCH CORRECTION)", titles.size());
        CHECK (i18n::tr ("no such key") == "no such key" && i18n::tr ("knob.vibrato") == "SCALE", "unknown keys fall back to their text");
    }

    //--------------------------------------------------------------------------
    std::printf ("\n=== 2) Audio through the processor ===\n");
    {
        VSTMTuneProcessor p;
        p.setPlayConfigDetails (2, 2, kFs, 512);
        p.prepareToPlay (kFs, 512);
        CHECK (p.getLatencySamples() == 960, "latency REALTIME: %d samples (20 ms)", p.getLatencySamples());
        setParam (p, vstm::ids::realtime, 0.0f);
        CHECK (p.getLatencySamples() == 1920, "latency HQ: %d samples (40 ms)", p.getLatencySamples());
        setParam (p, vstm::ids::realtime, 1.0f);

        setParam (p, vstm::ids::scale, (float) vstm::kChromatic);
        setParam (p, vstm::ids::retune, 0.0f);
        Voice v;
        v.notes = { 57.35 };               // A3, 35 cents sharp
        std::vector<float> out;
        processSeconds (p, v, 1.5, &out);
        const double hz = measureHz (out, (int) (kFs * 1.0), (int) (kFs * 0.2), 220.0);
        CHECK (std::abs (1200.0 * std::log2 (hz / 220.0)) < 5.0, "A3 +35 ct is tuned to %.2f Hz (%.2f ct)", hz, 1200.0 * std::log2 (hz / 220.0));

        // bypass: output = input delayed by the latency
        setParam (p, vstm::ids::bypass, 1.0f);
        Voice v2;
        v2.notes = { 57.35 };
        std::vector<float> outB;
        processSeconds (p, v2, 1.0, &outB);
        Voice ref;
        ref.notes = { 57.35 };
        std::vector<float> in;
        for (int i = 0; i < (int) outB.size(); ++i) in.push_back (ref.next());
        double err = 0, sig = 0;
        for (int i = (int) (kFs * 0.5); i < (int) (kFs * 0.9); ++i)
        {
            const double d = outB[(size_t) i] - in[(size_t) (i - p.getLatencySamples())];
            err += d * d; sig += (double) in[(size_t) i] * in[(size_t) i];
        }
        CHECK (10.0 * std::log10 (sig / (err + 1e-20)) > 60.0, "bypassed: clean, latency-compensated dry signal");
        setParam (p, vstm::ids::bypass, 0.0f);
    }

    //--------------------------------------------------------------------------
    std::printf ("\n=== 3) State and presets ===\n");
    {
        VSTMTuneProcessor a;
        a.presets.loadPreset (1);      // Hard Tune
        CHECK (a.presets.getCurrentName() == "Hard Tune" && a.apvts.getParameter (vstm::ids::robotism)->getValue() < 0.001f,
               "factory preset 'Hard Tune' -> Robotik");
        CHECK (! a.presets.isDirty(), "freshly loaded preset is clean");
        setParam (a, vstm::ids::humanize, 77.0f);
        CHECK (a.presets.isDirty(), "editing marks the preset as modified");
        setParam (a, vstm::ids::realtime, 0.0f);
        a.uiScale = 0.9f;

        juce::MemoryBlock state;
        a.getStateInformation (state);
        VSTMTuneProcessor b;
        b.setStateInformation (state.getData(), (int) state.getSize());
        float worst = 0.0f;
        for (int i = 0; i < a.getParameters().size(); ++i)
            worst = std::max (worst, std::abs (a.getParameters()[i]->getValue() - b.getParameters()[i]->getValue()));
        CHECK (worst < 1e-5f && b.presets.getCurrentName() == "Hard Tune" && std::abs (b.uiScale.load() - 0.9f) < 1e-3f,
               "session state round trip (params, preset name, window size)");
        CHECK (b.getLatencySamples() == vstm::TuneEngine::latencyFor (44100.0, false), "restored HQ mode reports its latency");

        a.presets.loadNext();
        const auto n1 = a.presets.getCurrentName();
        a.presets.loadPrevious();
        CHECK (n1 == "Robot Voice" && a.presets.getCurrentName() == "Hard Tune", "next / previous preset");
    }

    //--------------------------------------------------------------------------
    std::printf ("\n=== 3b) Damaged or older sessions ===\n");
    {
        {
            VSTMTuneProcessor p;
            juce::Random rng (1234);
            juce::MemoryBlock junk (4096);
            for (size_t i = 0; i < junk.getSize(); ++i)
                junk[i] = (char) rng.nextInt (256);
            p.setStateInformation (junk.getData(), (int) junk.getSize());
            p.setStateInformation (nullptr, 0);
            CHECK (paramsAndAudioFinite (p) && p.apvts.getParameter (vstm::ids::robotism)->getCurrentValueAsText() == "Balanced",
                   "random bytes / an empty block as session: ignored");
        }
        {
            // a session from an older version: one known parameter, one that no longer exists, no version field
            juce::XmlElement x ("VSTMTune");
            auto* known = x.createNewChildElement ("PARAM");
            known->setAttribute ("id", "robotism");
            known->setAttribute ("value", 20.0);
            auto* gone = x.createNewChildElement ("PARAM");
            gone->setAttribute ("id", "paramFromAnOldVersion");
            gone->setAttribute ("value", 3.0);
            juce::MemoryBlock mb;
            juce::AudioProcessor::copyXmlToBinary (x, mb);
            VSTMTuneProcessor p;
            setParam (p, vstm::ids::retune, 99.0f);
            p.setStateInformation (mb.getData(), (int) mb.getSize());
            CHECK (std::abs (p.apvts.getRawParameterValue (vstm::ids::robotism)->load() - 20.0f) < 1.0e-3f
                       && std::abs (p.apvts.getRawParameterValue (vstm::ids::retune)->load() - 15.0f) < 1.0e-3f
                       && paramsAndAudioFinite (p),
                   "older session: known value restored, missing value back to default, unknown id ignored");
        }
        {
            const auto mb = editedState ([] (juce::XmlElement& x)
            {
                setStateValue (x, "correction", "nan");
                setStateValue (x, "key", "nan");
                setStateValue (x, "bypass", "nan");
                setStateValue (x, "mix", "inf");
                setStateValue (x, "transpose", "99");
                setStateValue (x, "formant", "-1e9");
            });
            VSTMTuneProcessor p;
            p.setStateInformation (mb.getData(), (int) mb.getSize());
            CHECK (paramsAndAudioFinite (p), "damaged session (nan / inf / out of range): every parameter and the audio stay finite");
            auto text = [&p] (const juce::String& id) { return p.apvts.getParameter (id)->getCurrentValueAsText(); };
            CHECK (text (vstm::ids::correction) == "100%" && text (vstm::ids::key) == "Auto" && text (vstm::ids::bypass) == "Off"
                       && text (vstm::ids::mix) == "100%" && text (vstm::ids::transpose) == "+12 st" && text (vstm::ids::formant) == "-12.0",
                   "nan -> default, inf / out of range -> limit (%s, %s, %s, %s, %s, %s)", text (vstm::ids::correction).toRawUTF8(),
                   text (vstm::ids::key).toRawUTF8(), text (vstm::ids::bypass).toRawUTF8(), text (vstm::ids::mix).toRawUTF8(),
                   text (vstm::ids::transpose).toRawUTF8(), text (vstm::ids::formant).toRawUTF8());
        }
    }

    //--------------------------------------------------------------------------
    std::printf ("\n=== 3c) User preset files ===\n");
    {
        // a throw-away folder: the test never touches the real user presets
        const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("VSTMTune_PluginTest_Presets");
        folder.deleteRecursively();
        folder.createDirectory();
        vstm::PresetManager::setUserPresetFolderOverride (folder);

        folder.getChildFile ("A truncated.vstmpreset").replaceWithText ("<VSTMTunePreset name=\"A\"><PARAM id=\"robotism\" val");
        folder.getChildFile ("B nan.vstmpreset").replaceWithText ("<VSTMTunePreset name=\"B\" version=\"1\"><PARAM id=\"correction\" value=\"nan\"/>"
                                                                  "<PARAM id=\"key\" value=\"nan\"/><PARAM id=\"mix\" value=\"inf\"/>"
                                                                  "<PARAM id=\"robotism\" value=\"10\"/></VSTMTunePreset>");
        folder.getChildFile ("C range.vstmpreset").replaceWithText ("<VSTMTunePreset name=\"C\"><PARAM id=\"transpose\" value=\"99\"/>"
                                                                    "<PARAM id=\"formant\" value=\"-99\"/><PARAM id=\"key\" value=\"57\"/></VSTMTunePreset>");
        folder.getChildFile ("D empty.vstmpreset").replaceWithText ({});

        VSTMTuneProcessor p;
        auto& pm = p.presets;
        pm.refreshUserPresets();
        const int factory = pm.getNumFactoryPresets();
        CHECK (pm.getNumPresets() == factory + 4, "%d user preset files listed", pm.getNumPresets() - factory);

        bool finite = true;
        for (int i = factory; i < pm.getNumPresets(); ++i)
        {
            pm.loadPreset (i);
            finite = paramsAndAudioFinite (p) && finite;
        }
        CHECK (finite, "truncated / nan / out of range / empty preset files: parameters and audio stay finite");

        int nanPreset = -1;
        for (int i = 0; i < pm.getNumPresets(); ++i)
            if (pm.getPresetName (i) == "B nan") nanPreset = i;
        pm.loadPreset (nanPreset);
        CHECK (std::abs (p.apvts.getRawParameterValue (vstm::ids::robotism)->load() - 10.0f) < 1.0e-3f
                   && p.apvts.getParameter (vstm::ids::correction)->getCurrentValueAsText() == "100%"
                   && p.apvts.getParameter (vstm::ids::key)->getCurrentValueAsText() == "Auto",
               "preset with nan values: good values loaded, nan values at their defaults");

        setParam (p, vstm::ids::humanize, 77.0f);
        CHECK (pm.saveUserPreset ("Test Save"), "save a user preset");
        setParam (p, vstm::ids::humanize, 11.0f);
        CHECK (pm.saveUserPreset ("Test Save"), "overwrite it (same name)");
        pm.loadPreset (0);
        int saved = -1;
        for (int i = 0; i < pm.getNumPresets(); ++i)
            if (pm.getPresetName (i) == "Test Save") saved = i;
        pm.loadPreset (saved);
        CHECK (saved >= 0 && std::abs (p.apvts.getRawParameterValue (vstm::ids::humanize)->load() - 11.0f) < 0.06f && ! pm.isDirty(),
               "reload the overwritten preset (humanize %s)", p.apvts.getParameter (vstm::ids::humanize)->getCurrentValueAsText().toRawUTF8());
        CHECK (pm.deleteUserPreset (saved) && ! folder.getChildFile ("Test Save.vstmpreset").exists(), "delete it");

        vstm::PresetManager::setUserPresetFolderOverride ({});
        folder.deleteRecursively();
    }

    //--------------------------------------------------------------------------
    std::printf ("\n=== 4) Editor ===\n");
    {
        VSTMTuneProcessor p;
        p.setPlayConfigDetails (2, 2, kFs, 512);
        p.prepareToPlay (kFs, 512);

        for (int round = 0; round < 2; ++round)
        {
            std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
            CHECK (ed != nullptr && ed->getWidth() == 1152 && ed->getHeight() == 768, "editor %d opens at 1152 x 768", round + 1);
        }

        if (shotDir != juce::File())
        {
            shotDir.createDirectory();
            std::unique_ptr<juce::AudioProcessorEditor> base (p.createEditor());
            auto* ed = dynamic_cast<VSTMTuneEditor*> (base.get());

            auto melody = referenceMelody();
            processSeconds (p, melody, 9.1);
            ed->getDisplay().pump();
            pump (120);

            savePng (*ed, shotDir.getChildFile ("01_default.png"), 1.0f);
            savePng (*ed, shotDir.getChildFile ("01_default@2x.png"), 2.0f);

            ed->setSize (1536, 1024);
            pump (80);
            savePng (*ed, shotDir.getChildFile ("02_design_1536.png"), 1.0f);

            ed->setSize (768, 512);
            pump (80);
            savePng (*ed, shotDir.getChildFile ("03_min_768.png"), 1.0f);
            savePng (*ed, shotDir.getChildFile ("03_min_768@2x.png"), 2.0f);

            ed->setSize (1536, 1024);
            ed->getRobotKnob().showReadout = true;
            setParam (p, vstm::ids::robotism, 8.0f);
            pump (80);
            savePng (*ed, shotDir.getChildFile ("04_robotik.png"), 1.0f);
            setParam (p, vstm::ids::robotism, 92.0f);
            pump (80);
            savePng (*ed, shotDir.getChildFile ("05_insansi.png"), 1.0f);
            ed->getRobotKnob().showReadout = false;
            setParam (p, vstm::ids::robotism, 50.0f);

            setParam (p, vstm::ids::bypass, 1.0f);
            pump (150);
            savePng (*ed, shotDir.getChildFile ("06_bypass.png"), 1.0f);
            setParam (p, vstm::ids::bypass, 0.0f);
            pump (80);

            // the editor in every UI language (07_lang_tr.png, ...)
            for (int l = 0; l < vstm::i18n::kNumLangs; ++l)
            {
                vstm::i18n::setCurrent ((Lang) l);
                VSTMTuneProcessor q;
                q.setPlayConfigDetails (2, 2, kFs, 512);
                q.prepareToPlay (kFs, 512);
                auto voice = referenceMelody();
                processSeconds (q, voice, 9.1);
                std::unique_ptr<juce::AudioProcessorEditor> le (q.createEditor());
                auto* led = dynamic_cast<VSTMTuneEditor*> (le.get());
                led->getDisplay().pump();
                le->setSize (1536, 1024);
                pump (120);
                savePng (*le, shotDir.getChildFile (juce::String ("07_lang_") + vstm::i18n::code ((Lang) l) + ".png"), 1.0f);
            }
            vstm::i18n::setCurrent (Lang::english);
        }
    }

    //--------------------------------------------------------------------------
    if (vst3File != juce::File())
    {
        std::printf ("\n=== 5) Built VST3, loaded like a host ===\n");
        juce::VST3PluginFormat format;
        juce::OwnedArray<juce::PluginDescription> found;
        format.findAllTypesForFile (found, vst3File.getFullPathName());
        CHECK (found.size() == 1, "scan: %d plugin(s) in %s", found.size(), vst3File.getFileName().toRawUTF8());
        if (found.size() == 1)
        {
            juce::AudioPluginFormatManager manager;
            manager.addFormat (new juce::VST3PluginFormat());
            juce::String error;
            auto plugin = manager.createPluginInstance (*found[0], kFs, 512, error);
            CHECK (plugin != nullptr, "instance created %s", error.toRawUTF8());
            if (plugin != nullptr)
            {
                CHECK (found[0]->name == "VSTM Tune" && found[0]->manufacturerName == "VSTM Audio", "identity: %s / %s",
                       found[0]->name.toRawUTF8(), found[0]->manufacturerName.toRawUTF8());
                plugin->setPlayConfigDetails (2, 2, kFs, 512);
                plugin->prepareToPlay (kFs, 512);
                CHECK (plugin->getLatencySamples() == 960, "latency reported to the host: %d samples", plugin->getLatencySamples());

                int automatable = 0;
                for (auto* prm : plugin->getParameters())
                    if (prm->isAutomatable()) ++automatable;
                CHECK (automatable >= 11, "%d automatable parameters", automatable);

                Voice v;
                v.notes = { 57.35 };
                std::vector<float> out;
                juce::AudioBuffer<float> buf (2, 512);
                juce::MidiBuffer midi;
                for (int pos = 0; pos < (int) (kFs * 1.5); pos += 512)
                {
                    for (int i = 0; i < 512; ++i) { const float s = v.next(); buf.setSample (0, i, s); buf.setSample (1, i, s); }
                    plugin->processBlock (buf, midi);
                    for (int i = 0; i < 512; ++i) out.push_back (buf.getSample (0, i));
                }
                bool finite = true;
                for (float s : out) finite = finite && std::isfinite (s);
                const double hz = measureHz (out, (int) (kFs * 1.0), (int) (kFs * 0.2), 220.0);
                CHECK (finite && std::abs (1200.0 * std::log2 (hz / 220.0)) < 6.0, "VST3 tunes A3 +35 ct to %.2f Hz", hz);

                std::unique_ptr<juce::AudioProcessorEditor> ed (plugin->createEditorIfNeeded());
                CHECK (ed != nullptr && ed->getWidth() > 700, "VST3 editor opens (%d x %d)", ed ? ed->getWidth() : 0, ed ? ed->getHeight() : 0);
                ed.reset();
                plugin->releaseResources();
            }
        }
    }

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
