//==============================================================================
//  VSTM Tune - plugin katmani testi + arayuz ekran goruntuleri
//
//    VSTMTune_PluginTest [goruntu_klasoru] [--vst3 yol/VSTM Tune.vst3]
//
//  Linux'ta ekran olmadan:  xvfb-run -a VSTMTune_PluginTest goruntuler
//==============================================================================
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <thread>
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

float plain (VSTMTuneProcessor& p, const juce::String& id) { return p.apvts.getRawParameterValue (id)->load(); }

/** A C major melody, a little out of tune: enough for the automatic key to lock C. */
Voice cMajorMelody()
{
    Voice v;
    v.notes = { 60, 62, 64, 65, 67, 69, 71, 72, 67, 64, 65, 62, 60, 64, 67, 60 };
    v.noteLen = 0.35;
    v.detune = 0.1;
    return v;
}

/** The session XML a processor would save. */
std::unique_ptr<juce::XmlElement> savedXml (VSTMTuneProcessor& p)
{
    juce::MemoryBlock mb;
    p.getStateInformation (mb);
    return juce::AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize());
}

void restoreXml (VSTMTuneProcessor& p, const juce::XmlElement& xml)
{
    juce::MemoryBlock mb;
    juce::AudioProcessor::copyXmlToBinary (xml, mb);
    p.setStateInformation (mb.getData(), (int) mb.getSize());
}

/** Largest sample step in [a, b) seconds of a mono render. */
double maxStep (const std::vector<float>& y, double a, double b)
{
    double m = 0.0;
    for (size_t i = (size_t) std::max (1.0, a * kFs); i < (size_t) std::min ((double) y.size(), b * kFs); ++i)
        m = std::max (m, (double) std::abs (y[i] - y[i - 1]));
    return m;
}

/** Counts host gestures (begin / end) and value changes of the parameters. */
struct GestureCounter : juce::AudioProcessorListener
{
    int begins = 0, ends = 0, changes = 0;
    void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override { ++changes; }
    void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails&) override {}
    void audioProcessorParameterChangeGestureBegin (juce::AudioProcessor*, int) override { ++begins; }
    void audioProcessorParameterChangeGestureEnd (juce::AudioProcessor*, int) override { ++ends; }
};
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
    std::printf ("\n=== 3e) State: 1.1 sessions, determinism, duplicate ids, restore while audio runs (1.2) ===\n");
    {
        // a 1.1 session (state version 2) with every one of the 27 parameters away from its default
        // comes back with exactly those values; 1.2 adds no parameter and keeps the format
        juce::XmlElement x ("VSTMTune");
        const std::pair<const char*, double> vals[] = {
            { "key", 4 }, { "scale", 8 }, { "correction", 63.5 }, { "retune", 42.0 }, { "humanize", 12.5 }, { "robotism", 71.5 },
            { "vibscale", 140.0 }, { "formant", -2.5 }, { "transpose", 5.0 }, { "mix", 81.5 }, { "bypass", 1.0 }, { "realtime", 0.0 },
            { "range", 2.0 }, { "customscale", 1.0 }, { "adaptive", 1.0 } };
        for (const auto& v : vals)
        {
            auto* e = x.createNewChildElement ("PARAM");
            e->setAttribute ("id", v.first);
            e->setAttribute ("value", v.second);
        }
        for (int i = 0; i < 12; ++i)
        {
            auto* e = x.createNewChildElement ("PARAM");
            e->setAttribute ("id", vstm::ids::scaleNote (i));
            e->setAttribute ("value", (i % 3 == 0) ? 1.0 : 0.0);           // 1, b3, #4, 6: not the default major set
        }
        x.setAttribute ("presetName", "Default Preset");
        x.setAttribute ("uiScale", 0.8);
        x.setAttribute ("version", 2);
        x.setAttribute ("autoKeyLock", 9);
        x.setAttribute ("autoKeyConf", 0.75);
        x.setAttribute ("followKeyChanges", 1);
        x.setAttribute ("presetsKeepKey", 0);
        VSTMTuneProcessor p;
        restoreXml (p, x);
        bool all = true;
        for (const auto& v : vals)
            all = all && std::abs (plain (p, v.first) - (float) v.second) < 1.0e-3f;
        for (int i = 0; i < 12; ++i)
            all = all && (plain (p, vstm::ids::scaleNote (i)) > 0.5f) == (i % 3 == 0);
        auto again = savedXml (p);
        all = all && again->getIntAttribute ("autoKeyLock") == 9 && std::abs (again->getDoubleAttribute ("autoKeyConf") - 0.75) < 1e-6
                  && again->getIntAttribute ("followKeyChanges") == 1 && again->getIntAttribute ("presetsKeepKey") == 0
                  && p.getLatencySamples() == vstm::TuneEngine::latencyFor (44100.0, false);
        CHECK (all && paramsAndAudioFinite (p), "a 1.1 session with all 27 parameters changed comes back exactly (values, lock, options, HQ latency)");

        // the saved state is deterministic: the same settings give the same bytes, twice
        juce::MemoryBlock s1, s2;
        p.getStateInformation (s1);
        p.getStateInformation (s2);
        VSTMTuneProcessor q;
        q.setStateInformation (s1.getData(), (int) s1.getSize());
        juce::MemoryBlock s3;
        q.getStateInformation (s3);
        CHECK (s1 == s2 && s1 == s3, "saved state is deterministic (twice the same bytes; restore + save gives the same bytes: %d)", (int) (s1 == s3));

        // duplicate parameter ids in a damaged session: no crash, one of the two values, audio finite
        juce::XmlElement d ("VSTMTune");
        for (double v : { 20.0, 80.0 })
        {
            auto* e = d.createNewChildElement ("PARAM");
            e->setAttribute ("id", "robotism");
            e->setAttribute ("value", v);
        }
        VSTMTuneProcessor r;
        restoreXml (r, d);
        const float rb = plain (r, vstm::ids::robotism);
        CHECK ((std::abs (rb - 20.0f) < 1e-3f || std::abs (rb - 80.0f) < 1e-3f) && paramsAndAudioFinite (r),
               "duplicate parameter id in a session: one of the values (%.1f), audio finite", rb);

        // a host restoring a session while audio runs on another thread: no crash, no hang, finite audio
        VSTMTuneProcessor live;
        live.setPlayConfigDetails (2, 2, kFs, 256);
        live.prepareToPlay (kFs, 256);
        juce::MemoryBlock other;
        p.getStateInformation (other);
        std::atomic<bool> stop { false };
        std::atomic<long> blocks { 0 };
        std::atomic<bool> finite { true };
        std::thread audio ([&]
        {
            juce::AudioBuffer<float> buf (2, 256);
            juce::MidiBuffer midi;
            double ph = 0.0;
            while (! stop.load())
            {
                for (int i = 0; i < 256; ++i)
                {
                    ph += 220.0 / kFs;
                    const float s = 0.2f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * ph);
                    buf.setSample (0, i, s);
                    buf.setSample (1, i, s);
                }
                live.processBlock (buf, midi);
                for (int i = 0; i < 256; ++i)
                    if (! std::isfinite (buf.getSample (0, i))) finite = false;
                ++blocks;
            }
        });
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        for (int k = 0; k < 200; ++k)
        {
            if (k % 2) live.setStateInformation (other.getData(), (int) other.getSize());
            else live.setStateInformation (s1.getData(), (int) s1.getSize());
        }
        const auto ms = juce::Time::getMillisecondCounterHiRes() - t0;
        stop = true;
        audio.join();
        live.releaseResources();
        CHECK (finite.load() && blocks.load() > 0 && ms < 20000.0,
               "200 session restores while audio runs on another thread: finite audio, %ld blocks, %.0f ms, no crash", blocks.load(), ms);
    }

    //--------------------------------------------------------------------------
    std::printf ("\n=== 3d) Custom scale and Adaptive (added after 1.0) ===\n");
    {
        VSTMTuneProcessor a;
        auto value = [&a] (const juce::String& id) { return a.apvts.getParameter (id)->getValue(); };
        bool defaults = value (vstm::ids::customScale) < 0.5f && value (vstm::ids::adaptive) < 0.5f;
        for (int i = 0; i < 12; ++i)
            defaults = defaults && (value (vstm::ids::scaleNote (i)) > 0.5f) == vstm::params::customNoteDefault (i);
        CHECK (defaults, "defaults: custom scale off (notes = major scale), Adaptive off");

        // the 1.0 parameters keep their place: the first 13 are the 1.0 list, new ones follow
        const char* order10[] = { "key", "scale", "correction", "retune", "humanize", "robotism", "vibscale", "formant",
                                  "transpose", "mix", "bypass", "realtime", "range" };
        bool sameOrder = a.getParameters().size() == 13 + 14;
        for (int i = 0; i < 13 && sameOrder; ++i)
            sameOrder = dynamic_cast<juce::RangedAudioParameter*> (a.getParameters()[i])->paramID == order10[i];
        CHECK (sameOrder, "1.0 parameter IDs and order unchanged, 14 new ones appended (%d in all)", a.getParameters().size());

        // session round trip
        setParam (a, vstm::ids::customScale, 1.0f);
        setParam (a, vstm::ids::adaptive, 1.0f);
        for (int i = 0; i < 12; ++i)
            setParam (a, vstm::ids::scaleNote (i), (i == 0 || i == 3 || i == 7) ? 1.0f : 0.0f);
        juce::MemoryBlock mb;
        a.getStateInformation (mb);
        VSTMTuneProcessor b;
        b.setStateInformation (mb.getData(), (int) mb.getSize());
        bool same = true;
        for (auto* id : { &vstm::ids::customScale, &vstm::ids::adaptive })
            same = same && std::abs (b.apvts.getParameter (*id)->getValue() - value (*id)) < 1e-6f;
        for (int i = 0; i < 12; ++i)
            same = same && std::abs (b.apvts.getParameter (vstm::ids::scaleNote (i))->getValue() - value (vstm::ids::scaleNote (i))) < 1e-6f;
        CHECK (same, "session: custom notes, custom scale and Adaptive come back");

        // a 1.0 session has none of the new parameters: they come back at their defaults
        auto xml = juce::parseXML ("<VSTMTune presetName=\"Default Preset\" uiScale=\"0.75\" version=\"1\">"
                                   "<PARAM id=\"robotism\" value=\"30\"/><PARAM id=\"scale\" value=\"2\"/></VSTMTune>");
        juce::MemoryBlock old10;
        juce::AudioProcessor::copyXmlToBinary (*xml, old10);
        b.setStateInformation (old10.getData(), (int) old10.getSize());
        bool oldOk = b.apvts.getParameter (vstm::ids::customScale)->getValue() < 0.5f && b.apvts.getParameter (vstm::ids::adaptive)->getValue() < 0.5f
                  && std::abs (b.apvts.getRawParameterValue (vstm::ids::robotism)->load() - 30.0f) < 1e-3f;
        for (int i = 0; i < 12; ++i)
            oldOk = oldOk && (b.apvts.getParameter (vstm::ids::scaleNote (i))->getValue() > 0.5f) == vstm::params::customNoteDefault (i);
        CHECK (oldOk, "a 1.0 session opens with the custom scale off, default notes and Adaptive off");

        // the processor hands the custom notes to the engine: C minor triad, key C, a D sung
        // 30 ct sharp is pulled to C or D#, never to D
        VSTMTuneProcessor c;
        c.setPlayConfigDetails (2, 2, kFs, 512);
        c.prepareToPlay (kFs, 512);
        setParam (c, vstm::ids::key, 1.0f);                                  // C
        setParam (c, vstm::ids::robotism, 0.0f);
        setParam (c, vstm::ids::retune, 0.0f);
        setParam (c, vstm::ids::customScale, 1.0f);
        for (int i = 0; i < 12; ++i)
            setParam (c, vstm::ids::scaleNote (i), (i == 0 || i == 3 || i == 7) ? 1.0f : 0.0f);
        Voice d;
        d.notes = { 62.3 };
        std::vector<float> outD;
        processSeconds (c, d, 2.0, &outD);
        const double hz = measureHz (outD, (int) (kFs * 1.2), (int) (kFs * 0.2), vstm::midiToHz (62.0));
        const double toC = std::abs (1200.0 * std::log2 (hz / vstm::midiToHz (60.0))), toDs = std::abs (1200.0 * std::log2 (hz / vstm::midiToHz (63.0)));
        CHECK (std::min (toC, toDs) < 5.0, "custom scale C / D# / G: D +30 ct goes to %.1f Hz (%.1f ct from C, %.1f ct from D#)", hz, toC, toDs);
    }

    //--------------------------------------------------------------------------
    std::printf ("\n=== 3e) Auto key lock in the session (state version 2) ===\n");
    {
        VSTMTuneProcessor a;
        a.setPlayConfigDetails (2, 2, kFs, 512);
        a.prepareToPlay (kFs, 512);
        auto mel = cMajorMelody();
        processSeconds (a, mel, 9.0);
        const int lockedA = a.engine.detectedKey.load();
        const auto xa = savedXml (a);
        CHECK (lockedA == 0 && xa->getIntAttribute ("autoKeyLock", -9) == 0 && xa->getIntAttribute ("version") == VSTMTuneProcessor::kStateVersion
                   && xa->getDoubleAttribute ("autoKeyConf") > 0.49,
               "a locked automatic key is saved: autoKeyLock %d, autoKeyConf %.2f, version %d", xa->getIntAttribute ("autoKeyLock", -9),
               xa->getDoubleAttribute ("autoKeyConf"), xa->getIntAttribute ("version"));

        // reopen: the lock is back before any audio runs (a host may save again right away), and after
        VSTMTuneProcessor b;
        restoreXml (b, *xa);
        const auto xb = savedXml (b);
        b.setPlayConfigDetails (2, 2, kFs, 512);
        b.prepareToPlay (kFs, 512);
        Voice silence; silence.level = 0.0f;
        processSeconds (b, silence, 0.1);
        const bool backAfter = b.engine.detectedKey.load() == 0 && b.engine.keyState.load() == vstm::KeyDetector::kLocked
                            && std::abs (b.engine.keyConfidence.load() - (float) xa->getDoubleAttribute ("autoKeyConf")) < 1e-4f;
        b.prepareToPlay (kFs, 256);                                 // the host re-prepares (block size change)
        processSeconds (b, silence, 0.1, nullptr, 256);
        CHECK (xb->getIntAttribute ("autoKeyLock", -9) == 0 && backAfter && b.engine.detectedKey.load() == 0,
               "reopened session: the lock is saved again before any audio, restored with its confidence, kept through prepareToPlay");

        // the singing does not move a restored lock: 6 s of G major on the reopened instance
        Voice g;
        g.notes = { 67, 69, 71, 72, 74, 76, 78, 79, 74, 71, 67, 66 };
        g.noteLen = 0.35;
        processSeconds (b, g, 6.0);
        CHECK (b.engine.detectedKey.load() == 0 && b.engine.keySuggestion.load() == 7,
               "6 s of G major singing after the reopen: still C (the menu offers G: suggestion %d)", b.engine.keySuggestion.load());

        // a 1.0 session (no lock saved) loaded into this instance: the lock goes, the analysis starts over
        auto old = juce::parseXML ("<VSTMTune presetName=\"Default Preset\" uiScale=\"0.75\" version=\"1\"><PARAM id=\"robotism\" value=\"30\"/></VSTMTune>");
        restoreXml (b, *old);
        processSeconds (b, silence, 0.1);
        const auto xo = savedXml (b);
        CHECK (b.engine.detectedKey.load() == -1 && b.engine.keyState.load() == vstm::KeyDetector::kListening && ! b.getFollowKeyChanges()
                   && b.presets.getKeepTonal() && xo->getIntAttribute ("autoKeyLock", -9) == -1,
               "1.0 session: no lock (listening again), follow key changes off, presets keep the key");

        // damaged / hand-edited properties fall back to safe defaults
        int damagedOk = 0;
        for (const char* bad : { "nan", "57", "abc", "-1", "" })
        {
            auto x = savedXml (a);
            x->setAttribute ("autoKeyLock", bad);
            x->setAttribute ("autoKeyConf", "inf");
            x->setAttribute ("followKeyChanges", "maybe");
            x->setAttribute ("presetsKeepKey", "7");
            VSTMTuneProcessor d;
            restoreXml (d, *x);
            d.setPlayConfigDetails (2, 2, kFs, 512);
            d.prepareToPlay (kFs, 512);
            processSeconds (d, silence, 0.05);
            if (d.engine.detectedKey.load() == -1 && ! d.getFollowKeyChanges() && d.presets.getKeepTonal() && std::isfinite (d.engine.keyConfidence.load()))
                ++damagedOk;
        }
        CHECK (damagedOk == 5, "damaged autoKeyLock (nan / 57 / abc / -1 / empty), autoKeyConf inf, bad flags: no lock, defaults (%d / 5)", damagedOk);

        // the two options round-trip
        VSTMTuneProcessor e;
        e.setFollowKeyChanges (true);
        e.setPresetsKeepKey (false);
        VSTMTuneProcessor f;
        restoreXml (f, *savedXml (e));
        CHECK (f.getFollowKeyChanges() && ! f.presets.getKeepTonal(), "session: follow key changes and presets-keep-key options come back");
    }

    //--------------------------------------------------------------------------
    std::printf ("\n=== 3f) Presets keep the key and scale ===\n");
    {
        VSTMTuneProcessor p;
        auto& pm = p.presets;
        setParam (p, vstm::ids::key, 10.0f);                                 // A (index 10 = "A")
        setParam (p, vstm::ids::scale, (float) vstm::kMinor);
        setParam (p, vstm::ids::customScale, 1.0f);
        setParam (p, vstm::ids::scaleNote (2), 0.0f);
        bool kept = true, applied = true;
        for (int i = 0; i < pm.getNumFactoryPresets(); ++i)
        {
            pm.loadPreset (i);
            kept = kept && juce::roundToInt (plain (p, vstm::ids::key)) == 10 && juce::roundToInt (plain (p, vstm::ids::scale)) == vstm::kMinor
                        && plain (p, vstm::ids::customScale) > 0.5f && plain (p, vstm::ids::scaleNote (2)) < 0.5f && ! pm.isDirty();
            if (pm.getPresetName (i) == "Hard Tune") applied = applied && plain (p, vstm::ids::robotism) < 0.01f;
            if (pm.getPresetName (i) == "Chipmunk") applied = applied && std::abs (plain (p, vstm::ids::transpose) - 7.0f) < 1e-3f;
        }
        CHECK (kept && applied, "all %d factory presets: key A, scale Minor and the custom scale stay; their sound settings load; not 'modified'",
               pm.getNumFactoryPresets());

        setParam (p, vstm::ids::key, 3.0f);                                  // D
        CHECK (! pm.isDirty(), "changing the key does not mark the preset as modified (it is not part of it)");

        bool noScalePreset = true;
        for (int i = 0; i < pm.getNumFactoryPresets(); ++i)
            noScalePreset = noScalePreset && pm.getPresetName (i) != "Hicaz Vocal" && pm.getPresetName (i) != "Chromatic Fix";
        CHECK (noScalePreset && pm.getNumFactoryPresets() == 12 && pm.getFactoryCategory (1) == "Robotic" && pm.getFactoryCategory (11) == "Effects",
               "factory list: 12 sound presets in categories, none named after a scale");

        pm.resetAll();
        CHECK (juce::roundToInt (plain (p, vstm::ids::key)) == 0 && juce::roundToInt (plain (p, vstm::ids::scale)) == vstm::kMajor
                   && plain (p, vstm::ids::customScale) < 0.5f && plain (p, vstm::ids::scaleNote (2)) > 0.5f && pm.getCurrentName() == "Default Preset",
               "Reset all parameters: key Auto, scale Major, custom scale off and default notes (everything resets)");

        // an older session that names a factory preset which is gone: its values and its name come back
        auto x = savedXml (p);
        x->setAttribute ("presetName", "Hicaz Vocal");
        setStateValue (*x, "scale", "12");
        setStateValue (*x, "robotism", "45");
        VSTMTuneProcessor q;
        restoreXml (q, *x);
        CHECK (q.presets.getCurrentName() == "Hicaz Vocal" && juce::roundToInt (plain (q, vstm::ids::scale)) == vstm::kHicaz
                   && std::abs (plain (q, vstm::ids::robotism) - 45.0f) < 1e-3f && q.presets.getCurrentIndex() == -1,
               "a 1.0 session with the removed 'Hicaz Vocal': Hicaz scale and values restored, name shown");
    }

    //--------------------------------------------------------------------------
    std::printf ("\n=== 3g) No click when presets, key or scale change while singing ===\n");
    {
        // a held A3 (sung 30 ct sharp) through every factory preset, 0.5 s each; and a held E4 in C major
        // switched to C minor (E not allowed -> Eb / F), key D minor (E allowed again), Chromatic, back
        auto clickRatio = [] (const std::vector<float>& y, const std::vector<double>& switches)
        {
            double worst = 0.0;
            for (double t : switches)
            {
                const double around = maxStep (y, t - 0.025, t + 0.05);
                const double steady = std::max (maxStep (y, t - 0.30, t - 0.10), maxStep (y, t + 0.15, t + 0.35));
                worst = std::max (worst, around / std::max (steady, 1e-9));
            }
            return worst;
        };

        VSTMTuneProcessor p;
        p.setPlayConfigDetails (2, 2, kFs, 512);
        p.prepareToPlay (kFs, 512);
        Voice v;
        v.notes = { 57.3 };
        std::vector<float> y;
        std::vector<double> switches;
        processSeconds (p, v, 1.0, &y);
        for (int i = 1; i < p.presets.getNumFactoryPresets(); ++i)
        {
            switches.push_back ((double) y.size() / kFs);
            p.presets.loadPreset (i);
            processSeconds (p, v, 0.5, &y);
        }
        const double presetWorst = clickRatio (y, switches);

        VSTMTuneProcessor k;
        k.setPlayConfigDetails (2, 2, kFs, 512);
        k.prepareToPlay (kFs, 512);
        setParam (k, vstm::ids::key, 1.0f);                                  // C
        setParam (k, vstm::ids::robotism, 0.0f);
        setParam (k, vstm::ids::retune, 0.0f);
        setParam (k, vstm::ids::humanize, 0.0f);
        Voice e4;
        e4.notes = { 64.3 };
        std::vector<float> z;
        std::vector<double> ks;
        processSeconds (k, e4, 1.0, &z);
        const std::pair<const char*, float> steps[] = { { "scale", (float) vstm::kMinor }, { "key", 3.0f }, { "scale", (float) vstm::kChromatic },
                                                        { "scale", (float) vstm::kMajor }, { "key", 1.0f } };
        for (const auto& [id, value] : steps)
        {
            ks.push_back ((double) z.size() / kFs);
            setParam (k, id, value);
            processSeconds (k, e4, 0.6, &z);
        }
        const double keyWorst = clickRatio (z, ks);
        CHECK (presetWorst <= 1.5 && keyWorst <= 1.5,
               "largest sample step at a switch / steady state: presets %.2f, key / scale (robotic, held note) %.2f (limit 1.5)", presetWorst, keyWorst);
    }

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
        // rename: the file and the shown name follow; an existing or a factory name is refused
        pm.loadPreset (saved);
        const bool renamed = pm.renameUserPreset (saved, "Renamed One");
        int renamedIdx = -1;
        for (int i = 0; i < pm.getNumPresets(); ++i)
            if (pm.getPresetName (i) == "Renamed One") renamedIdx = i;
        const bool refusedUser = ! pm.renameUserPreset (renamedIdx, "C range");
        const bool refusedFactory = ! pm.renameUserPreset (renamedIdx, "Hard Tune");
        CHECK (renamed && renamedIdx >= 0 && pm.getCurrentName() == "Renamed One" && folder.getChildFile ("Renamed One.vstmpreset").existsAsFile()
                   && ! folder.getChildFile ("Test Save.vstmpreset").exists() && refusedUser && refusedFactory
                   && juce::XmlDocument::parse (folder.getChildFile ("Renamed One.vstmpreset"))->getStringAttribute ("name") == "Renamed One",
               "rename a user preset (file, name inside, shown name); an existing name or a factory name is refused");
        saved = renamedIdx;

        // a user preset still stores its key and scale: they load only with "presets keep key and scale" off
        setParam (p, vstm::ids::key, 3.0f);                                  // D
        setParam (p, vstm::ids::scale, (float) vstm::kDorian);
        CHECK (pm.saveUserPreset ("Song In D"), "save a preset with key D, Dorian");
        setParam (p, vstm::ids::key, 0.0f);
        setParam (p, vstm::ids::scale, (float) vstm::kMajor);
        int songIdx = -1;
        for (int i = 0; i < pm.getNumPresets(); ++i)
            if (pm.getPresetName (i) == "Song In D") songIdx = i;
        pm.loadPreset (songIdx);
        const bool keptAuto = juce::roundToInt (plain (p, vstm::ids::key)) == 0 && juce::roundToInt (plain (p, vstm::ids::scale)) == vstm::kMajor;
        pm.setKeepTonal (false);
        pm.loadPreset (songIdx);
        const bool loadedD = juce::roundToInt (plain (p, vstm::ids::key)) == 3 && juce::roundToInt (plain (p, vstm::ids::scale)) == vstm::kDorian;
        pm.setKeepTonal (true);
        pm.deleteUserPreset (songIdx);
        CHECK (keptAuto && loadedD, "the preset file keeps D Dorian: not applied while presets keep the key, applied when that is off (1.0 behaviour)");

        CHECK (pm.deleteUserPreset (saved) && ! folder.getChildFile ("Renamed One.vstmpreset").exists(), "delete it");

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

        {
            // The custom scale call-out: opened twice, then the plugin window is closed while it is open.
            // In a221b41 the editor deleted a box that JUCE's launchAsynchronously owns (it is a member of
            // JUCE's own callback object): heap corruption, the PluginTest crashed on Windows CI. Must not crash.
            std::function<void (juce::Component&, std::vector<juce::CallOutBox*>&)> findBoxes = [&] (juce::Component& c, std::vector<juce::CallOutBox*>& out)
            {
                if (auto* b = dynamic_cast<juce::CallOutBox*> (&c))
                    out.push_back (b);
                for (auto* child : c.getChildren())
                    findBoxes (*child, out);
            };
            auto boxesIn = [&] (juce::Component& c) { std::vector<juce::CallOutBox*> v; findBoxes (c, v); return v; };

            std::unique_ptr<juce::AudioProcessorEditor> base (p.createEditor());
            auto* ed = dynamic_cast<VSTMTuneEditor*> (base.get());
            ed->showCustomScaleEditor();
            pump (30);
            ed->showCustomScaleEditor();
            pump (30);
            const auto open = boxesIn (*ed);
            if (! open.empty())
                open.front()->exitModalState (0);      // what a click outside the box does
            pump (100);
            const size_t afterDismiss = boxesIn (*ed).size();
            ed->showCustomScaleEditor();
            pump (30);
            base.reset();                               // plugin window closed while the box is open
            pump (300);
            CHECK (open.size() == 1 && afterDismiss == 0,
                   "custom scale call-out: opened twice -> %d box, a click outside removes it (%d left), window closed while it is open -> no crash",
                   (int) open.size(), (int) afterDismiss);
            setParam (p, vstm::ids::customScale, 0.0f);
        }

        {
            // the ROBOTISM "i" call-out is owned by the editor now: open twice, close the window while open
            std::unique_ptr<juce::AudioProcessorEditor> base (p.createEditor());
            auto* ed = dynamic_cast<VSTMTuneEditor*> (base.get());
            ed->showRobotismInfo();
            pump (30);
            ed->showRobotismInfo();
            pump (30);
            int boxes = 0;
            std::function<void (juce::Component&)> count = [&] (juce::Component& c)
            {
                if (dynamic_cast<juce::CallOutBox*> (&c) != nullptr) ++boxes;
                for (auto* child : c.getChildren()) count (*child);
            };
            count (*ed);
            base.reset();
            pump (200);
            CHECK (boxes == 1, "ROBOTISM info call-out: opened twice -> %d box, window closed while it is open -> no crash", boxes);
        }

        {
            // the KEY box tells the truth: an unlocked guess shows the engine's own confidence; the
            // vocal-estimate note is shown for Auto only
            VSTMTuneProcessor q;
            q.setPlayConfigDetails (2, 2, kFs, 512);
            q.prepareToPlay (kFs, 512);
            Voice penta;
            penta.notes = { 60, 62, 64, 67, 69, 67, 64, 62 };            // C D E G A: no note tells C, F and G major apart
            penta.noteLen = 0.3;
            processSeconds (q, penta, 4.0);
            std::unique_ptr<juce::AudioProcessorEditor> base (q.createEditor());
            auto* ed = dynamic_cast<VSTMTuneEditor*> (base.get());
            pump (120);
            const int cand = q.engine.keyCandidate.load();
            const auto pct = juce::String (juce::roundToInt (q.engine.keyConfidence.load() * 100.0f)) + "%";
            const auto expect = juce::String (juce::CharPointer_UTF8 ("\xc2\xb7 ")) + vstm::noteName (juce::jmax (0, cand)) + "? " + pct;
            const bool guessOk = q.engine.keyState.load() == vstm::KeyDetector::kCandidate && ed->keyStatusText() == expect
                              && ! ed->keyStatusLocked() && ed->autoKeyMenuStatus().contains (pct) && ed->isKeyHintShown();
            CHECK (guessOk, "unlocked guess: KEY box \"%s\", menu \"%s\" (engine confidence %.4f)", ed->keyStatusText().toRawUTF8(),
                   ed->autoKeyMenuStatus().toRawUTF8(), q.engine.keyConfidence.load());

            auto mel = cMajorMelody();
            processSeconds (q, mel, 9.0);
            pump (120);
            const bool lockedOk = ed->keyStatusLocked() && ed->keyStatusText() == juce::String (juce::CharPointer_UTF8 ("\xc2\xb7 ")) + "C"
                               && ed->autoKeyMenuStatus().contains (juce::String (juce::roundToInt (q.engine.keyConfidence.load() * 100.0f)) + "%");
            setParam (q, vstm::ids::key, 1.0f);                                  // C chosen by hand
            pump (150);
            CHECK (lockedOk && ed->keyStatusText().isEmpty() && ! ed->keyStatusLocked() && ! ed->isKeyHintShown(),
                   "locked: \"%s\" + padlock; a key chosen by hand: no suffix, no vocal-estimate note", "C");
            base.reset();
        }

        {
            // keyboard: arrows move a knob 1 % (Shift 0.1 %), Delete resets; every key press is one host gesture
            std::unique_ptr<juce::AudioProcessorEditor> base (p.createEditor());
            GestureCounter counter;
            p.addListener (&counter);
            vui::MiniKnobSlider* formant = nullptr;
            std::function<void (juce::Component&)> find = [&] (juce::Component& c)
            {
                if (auto* k = dynamic_cast<vui::MiniKnob*> (&c))
                    if (k->getParameter().paramID == vstm::ids::formant) formant = &k->knob;
                for (auto* child : c.getChildren()) find (*child);
            };
            find (*base);
            const float before = plain (p, vstm::ids::formant);
            formant->keyPressed (juce::KeyPress (juce::KeyPress::upKey));
            const float up = plain (p, vstm::ids::formant);
            formant->keyPressed (juce::KeyPress (juce::KeyPress::downKey, juce::ModifierKeys::shiftModifier, 0));
            const float fine = plain (p, vstm::ids::formant);
            formant->keyPressed (juce::KeyPress (juce::KeyPress::deleteKey));
            const float reset = plain (p, vstm::ids::formant);
            const bool spacePassesOn = ! formant->keyPressed (juce::KeyPress (juce::KeyPress::spaceKey));
            p.removeListener (&counter);
            // 1 % of +-12 st = 0.24 st -> 0.2 (the parameter's 0.1 st steps); Shift: 0.024 st -> one step, 0.1 st
            CHECK (std::abs (up - before - 0.2f) < 1e-3f && std::abs (fine - up + 0.1f) < 1e-3f && std::abs (reset) < 1e-4f
                       && counter.begins == 3 && counter.ends == 3 && spacePassesOn,
                   "keyboard on FORMANT: up %+.2f st, Shift+down %+.3f st, Delete -> %.1f; %d gestures; Space goes on to the host",
                   up - before, fine - up, reset, counter.begins);
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

            // custom scale editor (C minor pentatonic starting from Minor) and ADAPTIVE on
            setParam (p, vstm::ids::scale, (float) vstm::kMinor);
            setParam (p, vstm::ids::adaptive, 1.0f);
            ed->showCustomScaleEditor();
            setParam (p, vstm::ids::scaleNote (2), 0.0f);
            setParam (p, vstm::ids::scaleNote (8), 0.0f);
            pump (250);
            savePng (*ed, shotDir.getChildFile ("08_custom_scale.png"), 1.0f);
            {
                // the note editor itself (it opens in a call-out box next to the scale box)
                vui::VLookAndFeel lnf;
                vui::ScaleNoteEditor se (p.apvts, [] { return 7; });
                se.setLookAndFeel (&lnf);
                pump (50);
                savePng (se, shotDir.getChildFile ("08b_scale_editor.png"), 1.0f);
                savePng (se, shotDir.getChildFile ("08b_scale_editor@2x.png"), 2.0f);
                se.setLookAndFeel (nullptr);
            }
            CHECK (p.apvts.getParameter (vstm::ids::customScale)->getValue() > 0.5f
                       && p.apvts.getParameter (vstm::ids::scaleNote (3))->getValue() > 0.5f
                       && p.apvts.getParameter (vstm::ids::scaleNote (4))->getValue() < 0.5f,
                   "\"Custom scale...\" switches the custom scale on and starts from the selected scale (Minor)");
            setParam (p, vstm::ids::customScale, 0.0f);
            setParam (p, vstm::ids::adaptive, 0.0f);
            setParam (p, vstm::ids::scale, (float) vstm::kMajor);
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
