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
#include "DrpText.h"
#include "PresetDocs.h"
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Parameters.h"
#include "Drp.h"
#include "Authorization.h"
#include "third_party/monocypher/monocypher-ed25519.h"

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

//------------------------------------------------------------------------------ 1.4 test keys (never a real key)
struct TestKey
{
    uint8_t secret[64] {};
    uint8_t pub[32] {};
    vstm::AuthorizationManager::TrustedKey trusted {};
    explicit TestKey (int seedValue)
    {
        uint8_t seed[32];
        juce::Random r (seedValue);
        for (auto& b : seed) b = (uint8_t) r.nextInt (256);
        crypto_ed25519_key_pair (secret, pub, seed);            // wipes the seed
        std::memcpy (trusted.publicKey.data(), pub, 32);
        trusted.id = vstm::AuthorizationManager::keyIdFor (pub);
    }
};

juce::MemoryBlock signAuth (const vstm::Authorization& a, const TestKey& k)
{
    auto mb = vstm::AuthorizationManager::buildSignedPart (a);
    uint8_t sig[64];
    crypto_ed25519_sign (sig, k.secret, static_cast<const uint8_t*> (mb.getData()), mb.getSize());
    mb.append (sig, 64);
    return mb;
}

vstm::Authorization adminAuth (const TestKey& k, uint32_t serial = 1)
{
    vstm::Authorization a;
    a.role = vstm::Authorization::Role::admin;
    a.entitlements = vstm::AuthorizationManager::kAllEntitlements;
    a.issuedAt = 1'780'000'000;
    a.serial = serial;
    a.keyId = k.trusted.id;
    a.label = "test admin";
    return a;
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

    // 1.4: authorization storage in a throw-away folder (never the user's real authorization file)
    const auto authFolder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("VSTMTune_PluginTest_Auth");
    authFolder.deleteRecursively();
    vstm::AuthorizationManager::setStorageFolderOverride (authFolder);

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
        CHECK (p.getLatencySamples() == 720, "latency REALTIME, Tenor (the default range): %d samples (15 ms; 20 ms before 1.7)", p.getLatencySamples());
        setParam (p, vstm::ids::realtime, 0.0f);
        CHECK (p.getLatencySamples() == 1920, "latency HQ: %d samples (40 ms)", p.getLatencySamples());
        // 1.7: the REALTIME latency follows the vocal range (HQ does not); the host is told at once, in either order
        setParam (p, vstm::ids::range, 0.0f);
        const int hqSop = p.getLatencySamples();
        setParam (p, vstm::ids::realtime, 1.0f);
        const int rtSop = p.getLatencySamples();
        setParam (p, vstm::ids::range, 2.0f);
        const int rtBass = p.getLatencySamples();
        setParam (p, vstm::ids::range, 1.0f);
        CHECK (hqSop == 1920 && rtSop == 624 && rtBass == 816 && p.getLatencySamples() == 720
               && rtSop == vstm::TuneEngine::latencyFor (kFs, true, 0) && rtBass == vstm::TuneEngine::latencyFor (kFs, true, 2),
               "latency by vocal range (REALTIME): Soprano / Alto %d (13 ms), Tenor 720 (15 ms), Bass %d (17 ms); HQ %d (40 ms) in any range",
               rtSop, rtBass, hqSop);

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
        // 1.1 appended the custom scale, its 12 notes and Adaptive (14); 1.5 appends the A4 reference, consonant
        // protection, Formant follows Transpose, Note Transition and the 12 note-bypass flags, in that order
        bool sameOrder = a.getParameters().size() == 13 + 54;
        for (int i = 0; i < 13 && sameOrder; ++i)
            sameOrder = dynamic_cast<juce::RangedAudioParameter*> (a.getParameters()[i])->paramID == order10[i];
        const char* order15[] = { "tuningref", "protectcons", "formantfollow", "transition" };
        std::vector<juce::RangedAudioParameter*> added15;
        for (int i = 0; i < 4 && sameOrder; ++i)
        {
            added15.push_back (dynamic_cast<juce::RangedAudioParameter*> (a.getParameters()[27 + i]));
            sameOrder = added15.back() != nullptr && added15.back()->paramID == order15[i];
        }
        for (int i = 0; i < 12 && sameOrder; ++i)
        {
            added15.push_back (dynamic_cast<juce::RangedAudioParameter*> (a.getParameters()[31 + i]));
            sameOrder = added15.back() != nullptr && added15.back()->paramID == vstm::ids::scaleBypass (i);
        }
        const char* order15b[] = { "tolerance", "confidence", "synthvib", "octave", "miditarget", "midiout", "midioctave", "tuningsys" };
        for (int i = 0; i < 8 && sameOrder; ++i)
        {
            added15.push_back (dynamic_cast<juce::RangedAudioParameter*> (a.getParameters()[43 + i]));
            sameOrder = added15.back() != nullptr && added15.back()->paramID == order15b[i];
        }
        for (int i = 0; i < 12 && sameOrder; ++i)
        {
            added15.push_back (dynamic_cast<juce::RangedAudioParameter*> (a.getParameters()[51 + i]));
            sameOrder = added15.back() != nullptr && added15.back()->paramID == vstm::ids::tuneCents (i);
        }
        const char* order15c[] = { "scaletranspose", "attack", "release" };
        for (int i = 0; i < 3 && sameOrder; ++i)
        {
            added15.push_back (dynamic_cast<juce::RangedAudioParameter*> (a.getParameters()[63 + i]));
            sameOrder = added15.back() != nullptr && added15.back()->paramID == order15c[i];
        }
        // 1.6: Auto Character, last, off, automatable (host automation is part of its spec)
        auto* autoChar16 = sameOrder ? dynamic_cast<juce::RangedAudioParameter*> (a.getParameters()[66]) : nullptr;
        sameOrder = sameOrder && autoChar16 != nullptr && autoChar16->paramID == "autochar" && autoChar16->getValue() < 0.5f
                 && autoChar16->isAutomatable();
        sameOrder = sameOrder && a.getParameters().size() == 13 + 54;
        CHECK (sameOrder, "1.0 parameter IDs and order unchanged, 54 new ones appended, the 1.5 ones last (%d in all)",
               a.getParameters().size());
        bool defaults15 = sameOrder && std::abs (added15[0]->convertFrom0to1 (added15[0]->getValue()) - 440.0f) < 0.01f
                       && added15[1]->getValue() < 0.5f && added15[2]->getValue() < 0.5f && added15[3]->getValue() < 0.01f;
        for (size_t i = 4; i < 16 && i < added15.size(); ++i)
            defaults15 = defaults15 && added15[i]->getValue() < 0.5f;      // no note bypassed
        defaults15 = defaults15 && added15.size() == 39 && added15[16]->getValue() < 0.01f && added15[17]->getValue() < 0.01f
                  && added15[18]->getValue() < 0.01f && std::abs (added15[19]->getValue() - 0.5f) < 0.01f   // off, off, off, octave 0
                  && added15[20]->getValue() < 0.5f && added15[21]->getValue() < 0.5f && added15[22]->getValue() < 0.5f  // MIDI target / out / octave off
                  && added15[23]->getValue() < 0.01f;                                                            // tuning: equal
        for (size_t i = 24; i < 36 && i < added15.size(); ++i)
            defaults15 = defaults15 && std::abs (added15[i]->convertFrom0to1 (added15[i]->getValue())) < 1.0e-6f; // custom cents 0
        defaults15 = defaults15 && added15.size() == 39 && added15[36]->getCurrentValueAsText() == "0"           // scale transpose 0
                  && added15[37]->getValue() < 0.01f && added15[38]->getValue() < 0.01f;                         // attack Auto, release Off
        for (auto* p15 : added15)
            defaults15 = defaults15 && ! p15->isAutomatable();
        CHECK (defaults15, "1.5 settings by default: A4 %s, consonant protection off, formant follows transpose off, note transition %s, no note bypassed, tolerance / confidence / synthetic vibrato off, octave %s, MIDI target, MIDI out and octave as played off, equal tuning (custom cents 0), scale transpose 0, attack Auto, release Off; none automatable",
               sameOrder ? added15[0]->getCurrentValueAsText().toRawUTF8() : "-", sameOrder ? added15[3]->getCurrentValueAsText().toRawUTF8() : "-",
               sameOrder && added15.size() == 39 ? added15[19]->getCurrentValueAsText().toRawUTF8() : "-");

        // 1.5 (roadmap 88): a 64-bit host gets exactly what the 32-bit path gives
        {
            VSTMTuneProcessor pf, pd;
            for (auto* q : { &pf, &pd })
            {
                q->setRateAndBufferSizeDetails (48000.0, 512);
                q->prepareToPlay (48000.0, 512);
            }
            juce::AudioBuffer<float> bf (2, 512 * 8);
            juce::AudioBuffer<double> bd (2, 512 * 8);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < bf.getNumSamples(); ++i)
                {
                    const float v = 0.3f * std::sin (2.0f * juce::MathConstants<float>::pi * 220.0f * (float) i / 48000.0f + (float) ch);
                    bf.setSample (ch, i, v);
                    bd.setSample (ch, i, (double) v);
                }
            juce::MidiBuffer none;
            bool same = true;
            for (int blk = 0; blk < 8; ++blk)
            {
                juce::AudioBuffer<float> f (bf.getArrayOfWritePointers(), 2, blk * 512, 512);
                juce::AudioBuffer<double> d (bd.getArrayOfWritePointers(), 2, blk * 512, 512);
                pf.processBlock (f, none);
                pd.processBlock (d, none);
            }
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < bf.getNumSamples(); ++i)
                    same = same && std::abs ((double) bf.getSample (ch, i) - bd.getSample (ch, i)) < 1.0e-12;   // exact for float values
            CHECK (pd.supportsDoublePrecisionProcessing() && same, "64-bit processing: the double path gives exactly the 32-bit output (%d samples)",
                   bf.getNumSamples() * 2);
        }

        // 1.5 (roadmap 42 / 46 / 52 / 33): MIDI target notes. A3 +35 ct with C4 held goes to C (any octave: C4);
        // with the mode off the same MIDI is ignored (A3); 1.5 MIDI perform (roadmap 49): with "octave as
        // played", C5 held takes it to C5
        {
            auto run = [] (bool mode, bool asPlayed = false)
            {
                VSTMTuneProcessor q;
                q.setRateAndBufferSizeDetails (kFs, 512);
                q.prepareToPlay (kFs, 512);
                setParam (q, vstm::ids::scale, (float) vstm::kChromatic);
                setParam (q, vstm::ids::retune, 0.0f);
                setParam (q, vstm::ids::midiTarget, mode ? 1.0f : 0.0f);
                setParam (q, vstm::ids::midiOctave, asPlayed ? 1.0f : 0.0f);
                Voice v;
                v.notes = { 57.35 };
                v.noteLen = 100.0;
                juce::AudioBuffer<float> buf (2, 512);
                std::vector<float> out;
                for (int pos = 0; pos < (int) (1.5 * kFs); pos += 512)
                {
                    juce::MidiBuffer m;
                    if (pos == 0)
                        m.addEvent (juce::MidiMessage::noteOn (1, asPlayed ? 72 : 60, (juce::uint8) 100), 64);
                    for (int i = 0; i < 512; ++i)
                    {
                        const float s = v.next();
                        buf.setSample (0, i, s);
                        buf.setSample (1, i, s);
                    }
                    q.processBlock (buf, m);
                    for (int i = 0; i < 512; ++i)
                        out.push_back (buf.getSample (0, i));
                }
                return measureHz (out, (int) (kFs * 1.1), (int) (kFs * 0.2), asPlayed ? 523.25 : (mode ? 261.63 : 220.0));
            };
            const double withMidi = run (true), ignored = run (false), played = run (true, true);
            CHECK (std::abs (1200.0 * std::log2 (withMidi / 261.626)) < 6.0 && std::abs (1200.0 * std::log2 (ignored / 220.0)) < 6.0
                   && std::abs (1200.0 * std::log2 (played / 523.251)) < 6.0,
                   "MIDI target notes: A3 +35 ct with C4 held goes to %.2f Hz (C4); with the mode off the MIDI is ignored (%.2f Hz); octave as played, C5 held: %.2f Hz",
                   withMidi, ignored, played);
        }

        // 1.5 (roadmap 49 / 53): notes as MIDI. A3 for 1 s then silence: one note-on (57) where the output plays it
        // and one note-off after the voice; the incoming MIDI (a C4 note-on) is not passed through. Off, or
        // bypassed: no MIDI at all. Switched off while a note sounds: its note-off at the start of that block.
        {
            struct Ev { int64_t at; bool on; int note; };
            auto run = [] (bool send, bool bypassed, int switchOffBlock)
            {
                VSTMTuneProcessor q;
                q.setRateAndBufferSizeDetails (kFs, 512);
                q.prepareToPlay (kFs, 512);
                setParam (q, vstm::ids::scale, (float) vstm::kChromatic);
                setParam (q, vstm::ids::retune, 0.0f);
                setParam (q, vstm::ids::midiOut, send ? 1.0f : 0.0f);
                setParam (q, vstm::ids::bypass, bypassed ? 1.0f : 0.0f);
                Voice v;
                v.notes = { 57.0 };
                v.noteLen = 100.0;
                juce::AudioBuffer<float> buf (2, 512);
                std::vector<Ev> evs;
                int blk = 0;
                for (int pos = 0; pos < (int) (1.6 * kFs); pos += 512, ++blk)
                {
                    if (blk == switchOffBlock)
                        setParam (q, vstm::ids::midiOut, 0.0f);
                    juce::MidiBuffer m;
                    if (pos == 0)
                        m.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 10);
                    for (int i = 0; i < 512; ++i)
                    {
                        const float s = pos + i < (int) kFs ? v.next() : 0.0f;
                        buf.setSample (0, i, s);
                        buf.setSample (1, i, s);
                    }
                    q.processBlock (buf, m);
                    for (const auto meta : m)
                    {
                        const auto msg = meta.getMessage();
                        evs.push_back ({ pos + meta.samplePosition, msg.isNoteOn(), msg.getNoteNumber() });
                    }
                }
                return std::make_pair (evs, q.getLatencySamples());
            };
            const auto [evOn, lat] = run (true, false, -1);
            const auto evOff = run (false, false, -1).first, evByp = run (true, true, -1).first;
            const auto evSwitch = run (true, false, 40).first;           // block 40 = 0.43 s, the voice still sounding
            const bool onRight = evOn.size() == 2 && evOn[0].on && evOn[0].note == 57 && ! evOn[1].on && evOn[1].note == 57
                              && std::abs ((double) evOn[0].at - lat) < 0.08 * kFs          // the voice starts at 0
                              // ... and ends at 1 s; 1.7: the note-off comes when the analysis has seen the end, so
                              // with the shorter REALTIME latency (15 ms, 20 ms before) it is that much later vs the audio
                              && std::abs ((double) evOn[1].at - (kFs + lat)) < 0.04 * kFs + std::max (0, (int) (0.020 * kFs) - lat);
            const bool switchRight = evSwitch.size() == 2 && evSwitch[0].on && ! evSwitch[1].on && evSwitch[1].at == 40 * 512;
            CHECK (onRight && evOff.empty() && evByp.empty() && switchRight,
                   "notes as MIDI: note-on 57 at %.1f ms, note-off at %.1f ms (latency %.1f ms, voice 0 - 1000 ms), input MIDI not passed through; off / bypassed: %d / %d events; switched off mid-note: note-off at the block start (%s)",
                   evOn.size() == 2 ? 1000.0 * (double) evOn[0].at / kFs : -1.0, evOn.size() == 2 ? 1000.0 * (double) evOn[1].at / kFs : -1.0,
                   1000.0 * lat / kFs, (int) evOff.size(), (int) evByp.size(), switchRight ? "yes" : "no");
        }

        // 1.5 (roadmap 47 / 48): MIDI Learn. RETUNE learns CC 21 from the next controller (any channel); CC 21
        // then moves it, other CCs do not; one CC per parameter; the assignments come back with the session,
        // an older session (none saved) clears them, damaged entries are skipped; forget / clear.
        {
            VSTMTuneProcessor q;
            q.setRateAndBufferSizeDetails (kFs, 512);
            q.prepareToPlay (kFs, 512);
            auto* retuneP = q.apvts.getParameter (vstm::ids::retune);
            auto* mixP = q.apvts.getParameter (vstm::ids::mix);
            juce::AudioBuffer<float> buf (2, 512);
            auto block = [&] (std::initializer_list<juce::MidiMessage> msgs)
            {
                buf.clear();
                juce::MidiBuffer m;
                int pos = 0;
                for (const auto& msg : msgs)
                    m.addEvent (msg, pos++);
                q.processBlock (buf, m);
            };
            q.midiLearn.startLearning (*retuneP);
            block ({ juce::MidiMessage::controllerEvent (3, 21, 64) });
            const bool learnt = q.midiLearn.ccFor (*retuneP) == 21 && std::abs (retuneP->getValue() - retuneP->convertTo0to1 (retuneP->convertFrom0to1 (64.0f / 127.0f))) < 1e-6f   // snapped to the step
                             && ! q.midiLearn.isLearningAny();
            const float mixBefore = mixP->getValue();
            block ({ juce::MidiMessage::controllerEvent (1, 21, 127), juce::MidiMessage::controllerEvent (1, 22, 0) });
            const bool moves = std::abs (retuneP->getValue() - 1.0f) < 1e-6f && std::abs (mixP->getValue() - mixBefore) < 1e-6f;
            q.midiLearn.startLearning (*retuneP);
            block ({ juce::MidiMessage::controllerEvent (1, 30, 10) });
            const bool single = q.midiLearn.ccFor (*retuneP) == 30 && q.midiLearn.numAssigned() == 1;
            q.midiLearn.startLearning (*mixP);
            block ({ juce::MidiMessage::controllerEvent (1, 7, 100) });
            juce::MemoryBlock st;
            q.getStateInformation (st);
            VSTMTuneProcessor r;
            r.setStateInformation (st.getData(), (int) st.getSize());
            const bool restored = r.midiLearn.ccFor (*r.apvts.getParameter (vstm::ids::retune)) == 30
                               && r.midiLearn.ccFor (*r.apvts.getParameter (vstm::ids::mix)) == 7 && r.midiLearn.numAssigned() == 2;
            bool cleared = false;
            if (auto xml = juce::AudioProcessor::getXmlFromBinary (st.getData(), (int) st.getSize()))
            {
                xml->removeAttribute ("midiCC");                    // what a 1.4 session looks like
                juce::MemoryBlock old;
                juce::AudioProcessor::copyXmlToBinary (*xml, old);
                r.setStateInformation (old.getData(), (int) old.getSize());
                cleared = r.midiLearn.numAssigned() == 0;
            }
            r.midiLearn.fromString ("x=retune,999=mix,5=nosuch,6=realtime,8=formant,9=formant,8=mix");
            const bool damaged = r.midiLearn.numAssigned() == 1 && r.midiLearn.ccFor (*r.apvts.getParameter (vstm::ids::formant)) == 8;
            q.midiLearn.forget (*retuneP);
            const bool forgot = q.midiLearn.ccFor (*retuneP) < 0 && q.midiLearn.numAssigned() == 1;
            q.midiLearn.clear();
            CHECK (learnt && moves && single && restored && cleared && damaged && forgot && q.midiLearn.numAssigned() == 0,
                   "MIDI Learn: learn %s, CC moves only its parameter %s, one CC per parameter %s, session round trip %s, older session clears %s, damaged entries skipped %s, forget / clear %s",
                   learnt ? "yes" : "NO", moves ? "yes" : "NO", single ? "yes" : "NO", restored ? "yes" : "NO", cleared ? "yes" : "NO",
                   damaged ? "yes" : "NO", forgot ? "yes" : "NO");
        }

        // 1.5 (roadmap 45): tuning systems. Key C, major, E3 sung 10 ct sharp: just -> 13.7 ct flat (5/4),
        // Pythagorean -> 7.8 ct sharp (81/64), custom E -30 ct; equal -> on the grid
        {
            auto tunedE = [] (int system, float customE)
            {
                VSTMTuneProcessor q;
                q.setRateAndBufferSizeDetails (kFs, 512);
                q.prepareToPlay (kFs, 512);
                setParam (q, vstm::ids::key, 0.0f);
                setParam (q, vstm::ids::scale, (float) vstm::kMajor);
                setParam (q, vstm::ids::retune, 0.0f);
                setParam (q, vstm::ids::tuningSys, (float) system);
                setParam (q, vstm::ids::tuneCents (4), customE);
                Voice v;
                v.notes = { 52.10 };
                v.noteLen = 100.0;
                juce::AudioBuffer<float> buf (2, 512);
                std::vector<float> out;
                for (int pos = 0; pos < (int) (1.2 * kFs); pos += 512)
                {
                    for (int i = 0; i < 512; ++i)
                    {
                        const float s = v.next();
                        buf.setSample (0, i, s);
                        buf.setSample (1, i, s);
                    }
                    juce::MidiBuffer none;
                    q.processBlock (buf, none);
                    for (int i = 0; i < 512; ++i)
                        out.push_back (buf.getSample (0, i));
                }
                const double hz = measureHz (out, (int) (kFs * 0.9), (int) (kFs * 0.2), 164.81);
                return 1200.0 * std::log2 (hz / 164.814);          // cents from equal-tempered E3
            };
            using namespace vstm::params;
            const double eq = tunedE (kEqualTuning, 0.0f), just = tunedE (kJustTuning, 0.0f), py = tunedE (kPythagoreanTuning, 0.0f),
                         custom = tunedE (kCustomTuning, -30.0f);
            CHECK (std::abs (eq) < 2.0 && std::abs (just + 13.69) < 2.0 && std::abs (py - 7.82) < 2.0 && std::abs (custom + 30.0) < 2.0,
                   "tuning systems: E in C major from E +10 ct: equal %+.1f ct, just %+.1f (5/4: -13.7), Pythagorean %+.1f (81/64: +7.8), custom -30: %+.1f",
                   eq, just, py, custom);
        }

        // 1.6 Auto Character: ROBOTISM drives Retune Speed, Humanize and Vibrato Scale while it is on
        {
            using vstm::params::characterFor;
            // the curves: the factory defaults at 50 %, monotonic and continuous over the whole range
            const auto at50 = characterFor (50.0f);
            bool curves = std::abs (at50.retuneMs - 15.0f) < 1.0e-5f && std::abs (at50.humanizePct - 35.0f) < 1.0e-5f
                       && std::abs (at50.vibratoPct - 100.0f) < 1.0e-5f;
            float maxStep = 0.0f;
            auto prev = characterFor (0.0f);
            for (float r = 0.5f; r <= 100.0f; r += 0.5f)
            {
                const auto v = characterFor (r);
                curves = curves && v.retuneMs >= prev.retuneMs - 1.0e-5f && v.humanizePct >= prev.humanizePct - 1.0e-5f
                      && v.vibratoPct >= prev.vibratoPct - 1.0e-5f;
                maxStep = std::max ({ maxStep, v.retuneMs - prev.retuneMs, v.humanizePct - prev.humanizePct, v.vibratoPct - prev.vibratoPct });
                prev = v;
            }
            curves = curves && maxStep < 1.0f && characterFor (0.0f).retuneMs < 1.0e-6f;

            auto render = [] (VSTMTuneProcessor& q, double seconds, const std::function<void (int)>& beforeBlock = {})
            {
                Voice v;
                v.notes = { 57.0, 60.0, 62.0, 59.0 };
                v.noteLen = 0.4;
                v.vibrato = 0.3;
                juce::AudioBuffer<float> buf (2, 512);
                std::vector<float> out;
                int blk = 0;
                for (int pos = 0; pos < (int) (seconds * kFs); pos += 512, ++blk)
                {
                    if (beforeBlock) beforeBlock (blk);
                    for (int i = 0; i < 512; ++i)
                    {
                        const float s = v.next();
                        buf.setSample (0, i, s);
                        buf.setSample (1, i, s);
                    }
                    juce::MidiBuffer none;
                    q.processBlock (buf, none);
                    for (int i = 0; i < 512; ++i)
                        out.push_back (buf.getSample (0, i));
                }
                return out;
            };
            auto fresh = [] (std::unique_ptr<VSTMTuneProcessor>& q, double rate = kFs)
            {
                q = std::make_unique<VSTMTuneProcessor>();
                q->setRateAndBufferSizeDetails (rate, 512);
                q->prepareToPlay (rate, 512);
            };
            std::unique_ptr<VSTMTuneProcessor> q1, q2;
            // switching it on at the default setting changes nothing (bit for bit)
            fresh (q1); fresh (q2);
            setParam (*q2, vstm::ids::autoCharacter, 1.0f);
            const bool sameAtDefault = render (*q1, 1.0) == render (*q2, 1.0);
            // off: ROBOTISM leaves the other controls alone
            fresh (q1);
            const float rt0 = q1->apvts.getParameter (vstm::ids::retune)->getValue(), hu0 = q1->apvts.getParameter (vstm::ids::humanize)->getValue(),
                        vi0 = q1->apvts.getParameter (vstm::ids::vibrato)->getValue();
            setParam (*q1, vstm::ids::robotism, 20.0f);
            const bool offIndependent = std::abs (q1->apvts.getParameter (vstm::ids::retune)->getValue() - rt0) < 1.0e-7f
                                     && std::abs (q1->apvts.getParameter (vstm::ids::humanize)->getValue() - hu0) < 1.0e-7f
                                     && std::abs (q1->apvts.getParameter (vstm::ids::vibrato)->getValue() - vi0) < 1.0e-7f;
            // auto -> manual (as host automation does it, between two blocks): the sound goes on bit for bit, and the
            // manual controls now hold the auto values
            fresh (q1); fresh (q2);
            for (auto* q : { q1.get(), q2.get() })
            {
                setParam (*q, vstm::ids::robotism, 20.0f);
                setParam (*q, vstm::ids::retune, 80.0f);           // manual values far from the auto ones
                setParam (*q, vstm::ids::humanize, 5.0f);
                setParam (*q, vstm::ids::autoCharacter, 1.0f);
            }
            const auto stayAuto = render (*q1, 2.0);
            const auto handOff = render (*q2, 2.0, [&q2] (int blk) { if (blk == 94) setParam (*q2, vstm::ids::autoCharacter, 0.0f); });
            const auto c20 = characterFor (20.0f);
            auto plainOf = [] (VSTMTuneProcessor& q, const juce::String& id) { auto* prm = q.apvts.getParameter (id); return prm->convertFrom0to1 (prm->getValue()); };
            const bool handedOver = stayAuto == handOff && std::abs (plainOf (*q2, vstm::ids::retune) - c20.retuneMs) < 0.06f
                                 && std::abs (plainOf (*q2, vstm::ids::humanize) - c20.humanizePct) < 0.06f
                                 && std::abs (plainOf (*q2, vstm::ids::vibrato) - c20.vibratoPct) < 0.06f;
            // manual -> auto: the manual values do not matter (different ones, same output)
            fresh (q1); fresh (q2);
            setParam (*q2, vstm::ids::retune, 120.0f);
            setParam (*q2, vstm::ids::humanize, 90.0f);
            setParam (*q2, vstm::ids::vibrato, 150.0f);
            for (auto* q : { q1.get(), q2.get() })
            {
                setParam (*q, vstm::ids::robotism, 30.0f);
                setParam (*q, vstm::ids::autoCharacter, 1.0f);
            }
            const bool deterministic = render (*q1, 1.0) == render (*q2, 1.0);
            // a restored session / a loaded preset sets every value itself: no hand-over then
            fresh (q1);
            setParam (*q1, vstm::ids::retune, 80.0f);
            setParam (*q1, vstm::ids::robotism, 20.0f);         // Auto Character off, manual retune 80 ms
            juce::MemoryBlock manualState;
            q1->getStateInformation (manualState);
            setParam (*q1, vstm::ids::autoCharacter, 1.0f);
            juce::MemoryBlock autoState;
            q1->getStateInformation (autoState);
            q1->setStateInformation (manualState.getData(), (int) manualState.getSize());
            const bool restoreKeeps = std::abs (plainOf (*q1, vstm::ids::retune) - 80.0f) < 0.06f
                                   && q1->apvts.getParameter (vstm::ids::autoCharacter)->getValue() < 0.5f;
            fresh (q2);
            q2->setStateInformation (autoState.getData(), (int) autoState.getSize());
            const bool autoRestored = q2->apvts.getParameter (vstm::ids::autoCharacter)->getValue() > 0.5f
                                   && std::abs (plainOf (*q2, vstm::ids::retune) - 80.0f) < 0.06f;   // its manual value waits
            // a factory preset (no Auto Character in it: off) gives the same values with Auto Character on before
            std::unique_ptr<VSTMTuneProcessor> q3;
            fresh (q3);
            q3->presets.loadPreset (1);
            q2->presets.loadPreset (1);
            bool presetKeeps = q2->apvts.getParameter (vstm::ids::autoCharacter)->getValue() < 0.5f;
            for (const auto* id : { &vstm::ids::retune, &vstm::ids::humanize, &vstm::ids::vibrato, &vstm::ids::robotism })
                presetKeeps = presetKeeps && std::abs (plainOf (*q2, *id) - plainOf (*q3, *id)) < 1.0e-5f;
            // 96 / 192 kHz: on, robotic side, finite and in range
            bool highRates = true;
            for (const double rate : { 96000.0, 192000.0 })
            {
                fresh (q1, rate);
                setParam (*q1, vstm::ids::robotism, 20.0f);
                setParam (*q1, vstm::ids::autoCharacter, 1.0f);
                for (float s : render (*q1, 0.5))
                    highRates = highRates && std::isfinite (s) && std::abs (s) < 4.0f;
            }
            CHECK (curves && sameAtDefault && offIndependent && handedOver && deterministic && restoreKeeps && autoRestored && presetKeeps && highRates,
                   "Auto Character: curves (defaults at 50 %%, monotonic, largest step %.2f per 0.5 %%) %s, on at default = off bit for bit %s, off: ROBOTISM leaves the others %s, auto -> manual bit for bit + values handed over %s, manual values ignored when on %s, session restore keeps manual values %s / restores Auto %s, preset load no hand-over %s, 96 / 192 kHz %s",
                   maxStep, curves ? "yes" : "NO", sameAtDefault ? "yes" : "NO", offIndependent ? "yes" : "NO", handedOver ? "yes" : "NO",
                   deterministic ? "yes" : "NO", restoreKeeps ? "yes" : "NO", autoRestored ? "yes" : "NO", presetKeeps ? "yes" : "NO", highRates ? "yes" : "NO");
        }

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
        // 1.4: user presets are .drp files; the name inside is read back from the binary file
        juce::String nameInside;
        {
            juce::MemoryBlock mb;
            vstm::drp::PresetData pd;
            if (vstm::drp::readFile (folder.getChildFile ("Renamed One.drp"), mb) == vstm::drp::Error::none
                && vstm::drp::readPreset (mb.getData(), mb.getSize(), pd) == vstm::drp::Error::none)
                nameInside = pd.name;
        }
        CHECK (renamed && renamedIdx >= 0 && pm.getCurrentName() == "Renamed One" && folder.getChildFile ("Renamed One.drp").existsAsFile()
                   && ! folder.getChildFile ("Test Save.drp").exists() && refusedUser && refusedFactory && nameInside == "Renamed One",
               "rename a user preset (.drp file, name inside, shown name); an existing name or a factory name is refused");
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

        CHECK (pm.deleteUserPreset (saved) && ! folder.getChildFile ("Renamed One.drp").exists(), "delete it");

        vstm::PresetManager::setUserPresetFolderOverride ({});
        folder.deleteRecursively();
    }


    //--------------------------------------------------------------------------
    std::printf ("\n=== 3h) .drp preset files (1.4) ===\n");
    {
        using namespace vstm::drp;
        const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("VSTMTune_PluginTest_Drp");
        folder.deleteRecursively();
        folder.createDirectory();
        vstm::PresetManager::setUserPresetFolderOverride (folder);

        VSTMTuneProcessor p;
        auto& pm = p.presets;
        pm.setKeepTonal (false);
        juce::Random rnd (1404);
        auto randomise = [&]
        {
            for (auto* prm : p.getParameters())
                if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
                    if (vstm::PresetManager::isPresetParameter (rp->paramID))
                        rp->setValueNotifyingHost (rp->convertTo0to1 (rp->convertFrom0to1 (rnd.nextFloat())));   // snapped to legal values
        };
        auto snapshot = [&]
        {
            std::vector<float> v;
            for (auto* prm : p.getParameters())
                if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
                    if (vstm::PresetManager::isPresetParameter (rp->paramID))
                        v.push_back (rp->getValue());
            return v;
        };
        randomise();
        const auto before = snapshot();
        CHECK (pm.saveUserPreset ("Round Trip"), "save a preset (.drp)");
        const auto file = folder.getChildFile ("Round Trip.drp");
        juce::MemoryBlock good;
        readFile (file, good);
        const auto text = juce::String::fromUTF8 ((const char*) good.getData(), (int) good.getSize());
        bool readable = false;
        for (const auto* id : { "robotism", "humanize", "retune", "correction", "transpose", "formant", "Round Trip" })
            readable = readable || text.contains (id);
        CHECK (file.existsAsFile() && ! folder.getChildFile ("Round Trip.vstmpreset").exists() && good.getSize() > 20
                   && std::memcmp (good.getData(), "VSTMDRP\x1a", 8) == 0 && ! readable,
               "the file is .drp, starts with the magic and shows no parameter names or values as text (%d bytes)", (int) good.getSize());
        randomise();
        int idx = -1;
        for (int i = 0; i < pm.getNumPresets(); ++i)
            if (pm.getPresetName (i) == "Round Trip") idx = i;
        pm.loadPreset (idx);
        const auto after = snapshot();
        float worst = 0.0f;
        for (size_t i = 0; i < before.size(); ++i)
            worst = std::max (worst, std::abs (before[i] - after[i]));
        CHECK (idx >= 0 && before.size() == after.size() && worst < 1.0e-6f,
               "every preset parameter comes back (%d parameters, largest normalised difference %.2g)", (int) before.size(), (double) worst);

        // 1.0 - 1.3 presets still load; a .drp of the same name wins in the list
        folder.getChildFile ("Legacy.vstmpreset").replaceWithText ("<VSTMTunePreset name=\"Legacy\" version=\"1\"><PARAM id=\"robotism\" value=\"33\"/></VSTMTunePreset>");
        folder.getChildFile ("Round Trip.vstmpreset").replaceWithText ("<VSTMTunePreset name=\"Round Trip\" version=\"1\"><PARAM id=\"robotism\" value=\"1\"/></VSTMTunePreset>");
        pm.refreshUserPresets();
        int legacy = -1, roundTrips = 0;
        for (int i = 0; i < pm.getNumPresets(); ++i)
        {
            if (pm.getPresetName (i) == "Legacy") legacy = i;
            if (pm.getPresetName (i) == "Round Trip") ++roundTrips;
        }
        pm.loadPreset (legacy);
        CHECK (legacy >= 0 && std::abs (plain (p, vstm::ids::robotism) - 33.0f) < 1.0e-3f && roundTrips == 1,
               "a 1.0 - 1.3 .vstmpreset still loads; the .drp of the same name is listed once");
        const bool migrated = pm.renameUserPreset (legacy, "Legacy Renamed");
        CHECK (migrated && folder.getChildFile ("Legacy Renamed.drp").existsAsFile() && ! folder.getChildFile ("Legacy.vstmpreset").exists(),
               "renaming an old preset stores it as .drp");

        // import: a copy under a free name, never overwriting
        const auto outside = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("VSTMTune_PluginTest_Outside");
        outside.deleteRecursively();
        outside.createDirectory();
        const auto ext = outside.getChildFile ("Shared.drp");
        ext.replaceWithData (good.getData(), good.getSize());
        juce::String n1, n2, n3;
        const auto i1 = pm.importPreset (ext, n1);
        const auto i2 = pm.importPreset (ext, n2);
        CHECK (i1 == Error::none && i2 == Error::none && n1 == "Round Trip (2)" && n2 == "Round Trip (3)"
                   && folder.getChildFile ("Round Trip (2).drp").existsAsFile() && folder.getChildFile ("Round Trip.drp").getSize() == (int64_t) good.getSize(),
               "import copies under a free name, never over an existing preset (%s, %s)", n1.toRawUTF8(), n2.toRawUTF8());
        const auto oldFmt = outside.getChildFile ("Old One.vstmpreset");
        oldFmt.replaceWithText ("<VSTMTunePreset name=\"Old One\" version=\"1\"><PARAM id=\"humanize\" value=\"42\"/></VSTMTunePreset>");
        const auto i3 = pm.importPreset (oldFmt, n3);
        CHECK (i3 == Error::none && folder.getChildFile ("Old One.drp").existsAsFile() && std::abs (plain (p, vstm::ids::humanize) - 42.0f) < 1.0e-3f,
               "an old .vstmpreset imports as .drp and loads");

        // damaged files: every truncation, every single-byte change, random data
        PresetData pd;
        int acceptedTrunc = 0, acceptedFlip = 0, acceptedRandom = 0;
        for (size_t n = 0; n < good.getSize(); ++n)
            acceptedTrunc += readPreset (good.getData(), n, pd) == Error::none;
        for (size_t i = 0; i < good.getSize(); ++i)
        {
            juce::MemoryBlock m (good);
            static_cast<uint8_t*> (m.getData())[i] ^= 0x5a;
            acceptedFlip += readPreset (m.getData(), m.getSize(), pd) == Error::none;
        }
        for (int t = 0; t < 2000; ++t)
        {
            juce::MemoryBlock m ((size_t) rnd.nextInt (400));
            for (size_t i = 0; i < m.getSize(); ++i) static_cast<uint8_t*> (m.getData())[i] = (uint8_t) rnd.nextInt (256);
            if (m.getSize() >= 8 && t % 2 == 0) std::memcpy (m.getData(), "VSTMDRP\x1a", 8);    // half of them with the magic
            acceptedRandom += readPreset (m.getData(), m.getSize(), pd) == Error::none;
        }
        CHECK (acceptedTrunc == 0 && acceptedFlip == 0 && acceptedRandom == 0,
               "rejected: %d truncations, %d single-byte changes, 2000 random files (accepted %d / %d / %d)",
               (int) good.getSize(), (int) good.getSize(), acceptedTrunc, acceptedFlip, acceptedRandom);

        // a gzip bomb with a correct header, a foreign plug-in, bad ids, nan values
        auto makeFile = [] (const juce::MemoryBlock& raw)
        {
            juce::MemoryBlock payload;
            {
                juce::MemoryOutputStream mo (payload, false);
                juce::GZIPCompressorOutputStream gz (mo, 9, juce::GZIPCompressorOutputStream::windowBitsGZIP);
                gz.write (raw.getData(), raw.getSize());
            }
            juce::MemoryBlock f;
            writeHeader (f, FileType::preset, payload);
            f.append (payload.getData(), payload.getSize());
            return f;
        };
        juce::MemoryBlock zeros (2 * 1024 * 1024, true);
        const auto bomb = makeFile (zeros);
        juce::MemoryBlock foreignRaw;
        { Writer w { foreignRaw }; w.u32 (0x41424344); w.u32 (kPluginCode); w.u16 (2); w.u16 (0); w.u16 (0); }
        juce::MemoryBlock badIdRaw;
        { Writer w { badIdRaw }; w.u32 (kManufacturerCode); w.u32 (kPluginCode); w.u16 (2); w.u16 (0); w.u16 (1); w.u8 (8); w.bytes ("Robotism", 8); w.u32 (0); }
        CHECK (bomb.getSize() < 8192 && readPreset (bomb.getData(), bomb.getSize(), pd) == Error::badPayload
                   && readPreset (makeFile (foreignRaw).getData(), makeFile (foreignRaw).getSize(), pd) == Error::wrongPlugin
                   && readPreset (makeFile (badIdRaw).getData(), makeFile (badIdRaw).getSize(), pd) == Error::badPayload,
               "refused: a %d-byte file that expands to 2 MB, another plug-in's file, an invalid parameter id", (int) bomb.getSize());
        PresetData nanPreset;
        nanPreset.name = "Nan";
        nanPreset.values = { { "robotism", std::numeric_limits<float>::quiet_NaN() }, { "mix", std::numeric_limits<float>::infinity() },
                             { "humanize", 1.0e9f }, { "unknown_id", 5.0f }, { "retune", 30.0f } };
        const auto nanFile = writePreset (nanPreset);
        outside.getChildFile ("Nan.drp").replaceWithData (nanFile.getData(), nanFile.getSize());
        juce::String n4;
        const auto i4 = pm.importPreset (outside.getChildFile ("Nan.drp"), n4);
        CHECK (i4 == Error::none && paramsAndAudioFinite (p) && std::abs (plain (p, vstm::ids::retune) - 30.0f) < 1.0e-3f
                   && std::abs (plain (p, vstm::ids::humanize) - 100.0f) < 1.0e-3f,
               "nan / inf / huge / unknown values: defaults, clamped or ignored; valid values load; audio stays finite");

        // an authorization file is not a preset
        TestKey k (7);
        const auto authFile = signAuth (adminAuth (k), k);
        outside.getChildFile ("Admin.drp").replaceWithData (authFile.getData(), authFile.getSize());
        const int countBefore = pm.getNumPresets();
        juce::String n5;
        CHECK (pm.importPreset (outside.getChildFile ("Admin.drp"), n5) == Error::wrongType && pm.getNumPresets() == countBefore,
               "an authorization file is refused by the preset import and nothing is written");
        juce::MemoryBlock huge ((size_t) kMaxFileSize + 10, true);
        outside.getChildFile ("Huge.drp").replaceWithData (huge.getData(), huge.getSize());
        juce::MemoryBlock tmp;
        CHECK (readFile (outside.getChildFile ("Huge.drp"), tmp) == Error::tooLarge, "a file larger than %d bytes is not read", kMaxFileSize);

        vstm::PresetManager::setUserPresetFolderOverride ({});
        folder.deleteRecursively();
        outside.deleteRecursively();
    }

    //--------------------------------------------------------------------------
    std::printf ("\n=== 3i) Authorization files (1.4) ===\n");
    {
        using namespace vstm::drp;
        using vstm::AuthorizationManager;
        using vstm::Authorization;
        const TestKey kA (11), kB (12);
        AuthorizationManager am;                                   // storage: the test folder (main)
        am.setTrustedKeysForTesting ({ kA.trusted });
        int64_t now = 1'800'000'000;
        am.setClockForTesting ([&now] { return now; });
        am.remove();

        const auto valid = signAuth (adminAuth (kA), kA);
        Authorization a;
        CHECK (am.verify (valid.getData(), valid.getSize(), a) == Error::none && a.role == Authorization::Role::admin && a.label == "test admin",
               "a valid admin file signed by a trusted key verifies (%d bytes)", (int) valid.getSize());
        CHECK (! am.isAdmin() && am.importData (valid.getData(), valid.getSize()) == Error::none && am.isAdmin()
                   && AuthorizationManager::getStoredFile().existsAsFile(),
               "import: authorized, stored");
        {
            juce::MemoryBlock stored;
            AuthorizationManager::getStoredFile().loadFileAsData (stored);
            AuthorizationManager again;
            again.setTrustedKeysForTesting ({ kA.trusted });
            again.setClockForTesting ([&now] { return now; });
            again.refresh();
            CHECK (stored == valid && again.isAdmin(), "the stored copy is byte for byte the imported file and verifies again in a new instance");
        }

        int accepted = 0, flips = 0;
        for (size_t i = 0; i < valid.getSize(); ++i)
            for (int bit = 0; bit < 8; ++bit, ++flips)
            {
                juce::MemoryBlock m (valid);
                static_cast<uint8_t*> (m.getData())[i] ^= (uint8_t) (1u << bit);
                accepted += am.verify (m.getData(), m.getSize(), a) == Error::none;
            }
        int acceptedTrunc = 0;
        for (size_t n = 0; n < valid.getSize(); ++n)
            acceptedTrunc += am.verify (valid.getData(), n, a) == Error::none;
        CHECK (accepted == 0 && acceptedTrunc == 0, "every single-bit change (%d bits of %d bytes) and every truncation (%d) is rejected",
               flips, (int) valid.getSize(), (int) valid.getSize());

        auto with = [&] (std::function<void (Authorization&)> change, const TestKey& signer)
        {
            auto x = adminAuth (kA);
            change (x);
            return signAuth (x, signer);
        };
        auto check = [&] (const juce::MemoryBlock& f) { return am.verify (f.getData(), f.getSize(), a); };
        juce::MemoryBlock unsignedFile (valid);
        std::memset (static_cast<uint8_t*> (unsignedFile.getData()) + unsignedFile.getSize() - 64, 0, 64);
        const auto otherKey = with ([&] (Authorization& x) { x.keyId = kB.trusted.id; }, kB);
        const auto forged = with ([] (Authorization&) {}, kB);              // claims key A, signed by B
        const auto user = with ([] (Authorization& x) { x.role = Authorization::Role::none; }, kA);
        const auto partial = with ([] (Authorization& x) { x.entitlements = 1; }, kA);
        const auto expired = with ([&] (Authorization& x) { x.expiresAt = now - 1; }, kA);
        const auto future = with ([&] (Authorization& x) { x.notBefore = now + 3600; }, kA);
        const auto revoked = with ([] (Authorization& x) { x.serial = 77; }, kA);
        am.setRevokedSerialsForTesting ({ 77 });
        juce::MemoryBlock wrongPlugin;
        {
            juce::MemoryBlock signedPart = AuthorizationManager::buildSignedPart (adminAuth (kA));
            static_cast<uint8_t*> (signedPart.getData())[kHeaderSize + 4] ^= 0xff;    // plug-in code
            juce::MemoryBlock payload (static_cast<uint8_t*> (signedPart.getData()) + kHeaderSize, signedPart.getSize() - kHeaderSize);
            writeHeader (wrongPlugin, FileType::authorization, payload);
            wrongPlugin.append (payload.getData(), payload.getSize());
            uint8_t sig[64];
            crypto_ed25519_sign (sig, kA.secret, static_cast<const uint8_t*> (wrongPlugin.getData()), wrongPlugin.getSize());
            wrongPlugin.append (sig, 64);
        }
        PresetData pd;
        pd.name = "x";
        const auto presetFile = writePreset (pd);
        CHECK (check (unsignedFile) == Error::badSignature && check (otherKey) == Error::unknownKey && check (forged) == Error::badSignature
                   && check (user) == Error::unsupportedRole && check (partial) == Error::unsupportedRole && check (expired) == Error::expired
                   && check (future) == Error::notYetValid && check (revoked) == Error::revoked && check (wrongPlugin) == Error::wrongPlugin
                   && check (presetFile) == Error::wrongType,
               "refused: unsigned, untrusted key, forged signature, unknown role, partial rights, expired, not yet valid, revoked, "
               "another plug-in, a preset");
        CHECK (am.importData (forged.getData(), forged.getSize()) == Error::badSignature && am.isAdmin(),
               "a rejected import changes nothing (the valid authorization stays)");

        // the stored copy is checked again: tampering on disk and expiry end the authorization
        {
            AuthorizationManager timed;
            timed.setTrustedKeysForTesting ({ kA.trusted });
            timed.setClockForTesting ([&now] { return now; });
            const auto shortLived = with ([&] (Authorization& x) { x.expiresAt = now + 100; }, kA);
            const bool okNow = timed.importData (shortLived.getData(), shortLived.getSize()) == Error::none && timed.isAdmin();
            now += 200;
            timed.refresh();
            const bool endedByTime = ! timed.isAdmin() && timed.storedStatus() == Error::expired;
            now -= 200;
            timed.importData (valid.getData(), valid.getSize());
            juce::MemoryBlock onDisk;
            AuthorizationManager::getStoredFile().loadFileAsData (onDisk);
            static_cast<uint8_t*> (onDisk.getData())[30] ^= 0x10;
            AuthorizationManager::getStoredFile().replaceWithData (onDisk.getData(), onDisk.getSize());
            timed.refresh();
            const bool endedByChange = ! timed.isAdmin();
            CHECK (okNow && endedByTime && endedByChange, "the stored copy stops working when it expires or is changed on disk");
            timed.importData (valid.getData(), valid.getSize());
            CHECK (timed.remove() && ! timed.isAdmin() && ! AuthorizationManager::getStoredFile().exists(), "remove: the stored file is deleted");
        }

        // the build's own key: consistent id, and a file from any other key is not accepted
        {
            AuthorizationManager real;
            CHECK (real.verify (valid.getData(), valid.getSize(), a) == Error::unknownKey && ! real.isAdmin(),
                   "the shipped build trusts only its own public key (a test key is refused)");
            if (const char* path = std::getenv ("VSTM_TEST_AUTH_FILE"))
            {
                juce::MemoryBlock mb;
                const auto er = readFile (juce::File (path), mb);
                const auto ev = er == Error::none ? real.verify (mb.getData(), mb.getSize(), a) : er;
                CHECK (ev == Error::none && a.role == Authorization::Role::admin,
                       "VSTM_TEST_AUTH_FILE (issued with tools/drp_auth.py): %s, key %s", toString (ev),
                       juce::String::toHexString (a.keyId.data(), 8, 0).toRawUTF8());
            }
        }

        // the expert switches follow the authorization
        {
            VSTMTuneProcessor p;
            p.auth.setTrustedKeysForTesting ({ kA.trusted });
            p.auth.remove();
            const bool lockedOut = ! p.setExpert (VSTMTuneProcessor::Expert::peakGuard, false) && p.getExpert (VSTMTuneProcessor::Expert::peakGuard);
            p.auth.importData (valid.getData(), valid.getSize());
            const bool admin = p.setExpert (VSTMTuneProcessor::Expert::peakGuard, false) && ! p.getExpert (VSTMTuneProcessor::Expert::peakGuard);
            juce::MemoryBlock state;
            p.getStateInformation (state);
            VSTMTuneProcessor q;
            q.setStateInformation (state.getData(), (int) state.getSize());
            const bool notSaved = q.getExpert (VSTMTuneProcessor::Expert::peakGuard);
            p.auth.remove();
            p.resetExpert();
            CHECK (lockedOut && admin && notSaved && p.getExpert (VSTMTuneProcessor::Expert::peakGuard),
                   "expert switches: refused without authorization, work with it, are not saved in the session, reset when it ends");
        }
        am.remove();
    }

    //--------------------------------------------------------------------------
    std::printf ("\n=== 3j) Readable .drp presets when authorized (1.6) ===\n");
    {
        namespace txt = vstm::drp::text;
        using vstm::drp::Error;
        const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("VSTMTune_PluginTest_DrpText");
        folder.deleteRecursively();
        folder.createDirectory();
        vstm::PresetManager::setUserPresetFolderOverride (folder);
        auto bytesOf = [] (const juce::File& f) { juce::MemoryBlock mb; f.loadFileAsData (mb); return mb; };
        auto isBinary = [&] (const juce::File& f) { const auto mb = bytesOf (f); return mb.getSize() > 8 && std::memcmp (mb.getData(), "VSTMDRP", 7) == 0; };
        auto plainOf = [] (VSTMTuneProcessor& q, const juce::String& id) { auto* prm = q.apvts.getParameter (id); return prm->convertFrom0to1 (prm->getValue()); };

        // without authorization: the binary .drp, exactly as before (the readable choice has no effect)
        VSTMTuneProcessor p;
        p.presets.setKeepTonal (false);
        p.auth.remove();
        CHECK (! p.auth.isAdmin() && ! p.savesReadablePresets() && p.presets.saveUserPreset ("Binary One") && isBinary (folder.getChildFile ("Binary One.drp")),
               "not authorized: presets are saved as the binary .drp");

        // authorized (a test key): readable text
        TestKey k (31);
        p.auth.setTrustedKeysForTesting ({ k.trusted });
        const auto authFile = signAuth (adminAuth (k), k);
        CHECK (p.auth.importData (authFile.getData(), authFile.getSize()) == Error::none && p.auth.isAdmin() && p.savesReadablePresets(),
               "authorized + readable on (default): presets will be saved as readable text");
        juce::Random rnd (1606);
        for (auto* prm : p.getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
                if (vstm::PresetManager::isPresetParameter (rp->paramID))
                    rp->setValueNotifyingHost (rp->convertTo0to1 (rp->convertFrom0to1 (rnd.nextFloat())));
        setParam (p, vstm::ids::autoCharacter, 0.0f);
        CHECK (p.presets.saveUserPreset ("Readable"), "save a readable preset");
        const auto file1 = folder.getChildFile ("Readable.drp");
        const auto text1 = bytesOf (file1);
        const auto str = juce::String::fromUTF8 ((const char*) text1.getData(), (int) text1.getSize());
        bool everyParam = true, explained = true;
        for (auto* prm : p.getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
                if (vstm::PresetManager::isPresetParameter (rp->paramID))
                {
                    everyParam = everyParam && str.contains ("\n" + rp->paramID + " = ");
                    explained = explained && str.contains (rp->getName (64) + " (" + rp->paramID + ")");
                }
        const bool header = str.startsWith ("# VSTM Tune preset") && str.contains ("[Preset]\nformat = VSTM-DRP-TEXT\nformat_version = 2\n")
                         && str.contains ("[Parameters]") && juce::CharPointer_UTF8::isValidString ((const char*) text1.getData(), (int) text1.getSize())
                         && ! isBinary (file1) && (unsigned char) str[0] == '#';
        const bool noSecrets = ! str.contains ("test admin") && ! str.containsIgnoreCase ("signature:") && ! str.containsIgnoreCase ("serial");
        const bool plainNumbers = ! str.contains ("e+0") && ! str.contains ("e-0");      // "80", not "8e+01"
        CHECK (header && everyParam && explained && noSecrets && plainNumbers && str.contains ("Auto Character is OFF"),
               "readable file: UTF-8 text with the format / version header, every preset parameter with its explanation, no authorization data (%d bytes)",
               (int) text1.getSize());

        // float noise of the parameter's own snapping (macOS on Apple silicon fuses start + interval * n into one
        // multiply-add: the default 0 of tunecents is held as 7.4505806e-07, -5.8 as -5.7999997): written as the
        // short decimal that loads back to the same held value, on every platform
        {
            auto held = [&p] (const juce::String& id, float x) { auto* rp = p.apvts.getParameter (id); return rp->convertFrom0to1 (rp->convertTo0to1 (x)); };
            auto sameBits = [] (float a, float b) { return std::memcmp (&a, &b, sizeof (float)) == 0; };
            const std::vector<std::pair<juce::String, float>> noisy { { vstm::ids::tuneCents (0), 7.4505806e-07f },
                                                                      { vstm::ids::formant, -5.7999997f },
                                                                      { vstm::ids::formant, -5.8f } };
            const auto doc = vstm::describePreset ("Noise", noisy, p.apvts);
            const auto noiseText = juce::String::fromUTF8 ((const char*) vstm::drp::text::write (doc).getData(), (int) vstm::drp::text::write (doc).getSize());
            bool shortSame = doc.params.size() == 3;
            for (size_t i = 0; shortSame && i < 3; ++i)
                shortSame = sameBits (held (noisy[i].first, doc.params[i].value), held (noisy[i].first, noisy[i].second));
            shortSame = shortSame && noiseText.contains ("\ntunecents0 = 0\n") && noiseText.contains ("\nformant = -5.8\n")
                     && ! noiseText.contains ("-5.79") && ! noiseText.contains ("e-0") && noiseText.contains ("default 0.");
            // tiny values (typed into a file): still plain decimals, still exact
            bool tinyPlain = true;
            juce::Random tinyRnd (77);
            for (int i = 0; i < 2000; ++i)
            {
                const float v = (tinyRnd.nextBool() ? 1.0f : -1.0f) * std::pow (10.0f, -11.9f + 10.8f * tinyRnd.nextFloat());
                const auto t = vstm::drp::text::formatNumber (v);
                tinyPlain = tinyPlain && ! t.containsAnyOf ("eE") && sameBits (std::strtof (t.toRawUTF8(), nullptr), v);
            }
            CHECK (shortSame && tinyPlain && vstm::drp::text::formatNumber (1.0e-7f) == "0.0000001",
                   "values with float noise are written short and load back to the same value (tunecents0 = 0, formant = -5.8); "
                   "1e-12 .. 0.1 written as plain, exact decimals (2000 values)");
        }

        // save -> load -> save: same values, same bytes
        std::vector<float> before;
        for (auto* prm : p.getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
                if (vstm::PresetManager::isPresetParameter (rp->paramID))
                    before.push_back (rp->getValue());
        VSTMTuneProcessor q;
        q.presets.setKeepTonal (false);
        q.auth.setTrustedKeysForTesting ({ k.trusted });
        q.auth.refresh();
        int idx = -1;
        for (int i = 0; i < q.presets.getNumPresets(); ++i)
            if (q.presets.getPresetName (i) == "Readable") idx = i;
        if (idx >= 0) q.presets.loadPreset (idx);
        std::vector<float> after;
        for (auto* prm : q.getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
                if (vstm::PresetManager::isPresetParameter (rp->paramID))
                    after.push_back (rp->getValue());
        bool sameValues = idx >= 0 && after.size() == before.size();
        for (size_t i = 0; sameValues && i < before.size(); ++i)
            sameValues = std::abs (after[i] - before[i]) < 1.0e-7f;
        const bool savedAgain = q.auth.isAdmin() && q.presets.saveUserPreset ("Readable");
        CHECK (sameValues && savedAgain && bytesOf (file1) == text1, "save -> load -> save: every value identical, the file byte for byte the same");

        // Auto Character on / off round trips (ROBOTISM and the manual values kept; the file says which)
        setParam (p, vstm::ids::robotism, 30.0f);
        setParam (p, vstm::ids::retune, 80.0f);
        setParam (p, vstm::ids::humanize, 12.0f);
        setParam (p, vstm::ids::autoCharacter, 1.0f);
        p.presets.saveUserPreset ("Auto On");
        const auto autoText = juce::String::fromUTF8 ((const char*) bytesOf (folder.getChildFile ("Auto On.drp")).getData(),
                                                      (int) folder.getChildFile ("Auto On.drp").getSize());
        if (const auto* example = std::getenv ("VSTM_DRP_EXAMPLE"))        // docs: a real file written by the plug-in
            folder.getChildFile ("Auto On.drp").copyFileTo (juce::File (juce::String (example)));
        VSTMTuneProcessor r;
        r.presets.setKeepTonal (false);
        for (int i = 0; i < r.presets.getNumPresets(); ++i)
            if (r.presets.getPresetName (i) == "Auto On") r.presets.loadPreset (i);
        const auto c30 = vstm::params::characterFor (30.0f);
        const bool autoKept = r.apvts.getParameter (vstm::ids::autoCharacter)->getValue() > 0.5f && std::abs (plainOf (r, vstm::ids::robotism) - 30.0f) < 1.0e-4f
                           && std::abs (plainOf (r, vstm::ids::retune) - 80.0f) < 1.0e-3f && std::abs (plainOf (r, vstm::ids::humanize) - 12.0f) < 1.0e-3f
                           && autoText.contains ("Auto Character is ON") && autoText.contains ("retune " + juce::String (c30.retuneMs, 2) + " ms");
        setParam (p, vstm::ids::autoCharacter, 0.0f);            // hand-over: retune / humanize take the auto values of ROBOTISM 30
        p.presets.saveUserPreset ("Auto Off");
        VSTMTuneProcessor r2;
        r2.presets.setKeepTonal (false);
        for (int i = 0; i < r2.presets.getNumPresets(); ++i)
            if (r2.presets.getPresetName (i) == "Auto Off") r2.presets.loadPreset (i);
        const bool offKept = r2.apvts.getParameter (vstm::ids::autoCharacter)->getValue() < 0.5f
                          && std::abs (plainOf (r2, vstm::ids::retune) - plainOf (p, vstm::ids::retune)) < 1.0e-4f
                          && std::abs (plainOf (r2, vstm::ids::retune) - c30.retuneMs) < 0.06f;
        CHECK (autoKept && offKept, "Auto Character round trips: on (ROBOTISM 30, manual retune 80 / humanize 12 kept, the file shows the auto values); off after the hand-over (retune %.2f ms)",
               (double) plainOf (r2, vstm::ids::retune));

        // the documented curves are the code's: the formula written in the file, evaluated here, equals characterFor
        auto smooth = [] (double a, double b, double x) { const double t = std::clamp ((x - a) / (b - a), 0.0, 1.0); return t * t * (3.0 - 2.0 * t); };
        double worst = 0.0;
        for (int i = 0; i <= 200; ++i)
        {
            const double rr = i / 200.0;
            const double rt = rr <= 0.5 ? 15.0 * std::pow (rr / 0.5, 2.0) : 15.0 + 10.0 * std::pow ((rr - 0.5) / 0.5, 1.5);
            const double hu = rr <= 0.5 ? 35.0 * smooth (0.2, 0.5, rr) : 35.0 + 15.0 * (rr - 0.5) / 0.5;
            const double vb = rr <= 0.5 ? 60.0 + 40.0 * smooth (0.0, 0.5, rr) : 100.0;
            const auto c = vstm::params::characterFor ((float) (rr * 100.0));
            worst = std::max ({ worst, std::abs (rt - c.retuneMs), std::abs (hu - c.humanizePct), std::abs (vb - c.vibratoPct) });
        }
        CHECK (worst < 1.0e-3 && str.contains ("retune (ms)   r <= 0.5: 15 x (r / 0.5)^2"),
               "the Auto Character formula written in the file matches params::characterFor at 201 points (largest difference %.2g)", worst);

        // tolerant reading: unknown fields kept, damaged lines skipped, BOM + CRLF; refused: no format line, newer version,
        // another plug-in, binary bytes, empty
        const juce::String good = juce::String::fromUTF8 ("\xef\xbb\xbf# hand edited\r\n[Preset]\r\nformat = VSTM-DRP-TEXT\r\nformat_version = 2\r\nplugin = VSTM Tune\r\nname = Edited\r\n"
                                   "[Parameters]\r\nretune = 5000\r\nhumanize = abc\r\nfuture_param = 1.5\r\nmix = 50\r\n[Extra]\r\nfoo = 1\r\n");
        vstm::drp::PresetData pd;
        txt::ReadReport rep;
        const auto e1 = txt::read (good.toRawUTF8(), good.getNumBytesAsUTF8(), pd, &rep);
        bool hasFuture = false;
        for (const auto& v : pd.values) hasFuture = hasFuture || v.first == "future_param";
        const bool tolerant = e1 == Error::none && pd.name == "Edited" && pd.values.size() == 3 && hasFuture && rep.skippedLines == 1 && rep.unknownSections == 1;
        auto variant = [&good] (const char* from, const char* to) { return good.replace (from, to); };
        auto readErr = [&pd] (const juce::String& s) { return txt::read (s.toRawUTF8(), s.getNumBytesAsUTF8(), pd); };
        const bool refused = readErr (variant ("format = VSTM-DRP-TEXT", "format = OTHER")) == Error::badMagic
                          && readErr (variant ("format_version = 2", "format_version = 3")) == Error::unsupportedVersion
                          && readErr (variant ("plugin = VSTM Tune", "plugin = Other Plug-in")) == Error::wrongPlugin
                          && readErr (variant ("format_version = 2\r\n", "")) == Error::badPayload
                          && txt::read ("", 0, pd) == Error::truncated;
        juce::MemoryBlock binary;
        binary.setSize (64, true);
        std::memcpy (binary.getData(), "VSTMDRP\x1a", 8);
        const bool notText = ! txt::isTextPreset (binary.getData(), binary.getSize()) && txt::isTextPreset (good.toRawUTF8(), good.getNumBytesAsUTF8());
        folder.getChildFile ("Edited.drp").replaceWithText (good, false, false, nullptr);
        juce::String importedName;
        VSTMTuneProcessor t;
        t.presets.setKeepTonal (false);
        const auto ei = t.presets.importPreset (folder.getChildFile ("Edited.drp"), importedName);
        const bool clamped = ei == Error::none && std::abs (plainOf (t, vstm::ids::retune) - 200.0f) < 1.0e-3f && std::abs (plainOf (t, vstm::ids::mix) - 50.0f) < 1.0e-3f
                          && std::abs (plainOf (t, vstm::ids::humanize) - 35.0f) < 1.0e-3f
                          && ! isBinary (folder.getChildFile (importedName + ".drp"));
        CHECK (tolerant && refused && notText && clamped,
               "reading: BOM + CRLF ok, unknown field kept, damaged line skipped (%d), unknown section ignored; refused: no format line, version 3, another plug-in, missing version, empty; binary never taken for text; import: out-of-range clamped, damaged value default, imported copy stays text",
               rep.skippedLines);

        // the format is kept on rename; old binary presets keep loading; readable off -> binary again
        int binIdx = -1;
        for (int i = 0; i < p.presets.getNumPresets(); ++i)
            if (p.presets.getPresetName (i) == "Binary One") binIdx = i;
        if (binIdx >= 0) p.presets.loadPreset (binIdx);
        bool renamedKeeps = false;
        for (int i = 0; i < p.presets.getNumPresets(); ++i)
            if (p.presets.getPresetName (i) == "Readable")
            {
                p.presets.loadPreset (i);
                renamedKeeps = p.presets.renameUserPreset (i, "Readable Renamed") && ! isBinary (folder.getChildFile ("Readable Renamed.drp"));
            }
        p.readablePresets = false;
        const bool offBinary = ! p.savesReadablePresets() && p.presets.saveUserPreset ("Binary Two") && isBinary (folder.getChildFile ("Binary Two.drp"));
        CHECK (binIdx >= 0 && renamedKeeps && offBinary, "binary presets load as before; a renamed readable preset stays readable; readable off (authorized): binary again");

        // the session (host state) is separate from presets: only the readable choice is in it
        juce::MemoryBlock st;
        p.getStateInformation (st);
        auto xml = juce::AudioProcessor::getXmlFromBinary (st.getData(), (int) st.getSize());
        VSTMTuneProcessor s2;
        s2.setStateInformation (st.getData(), (int) st.getSize());
        CHECK (xml != nullptr && ! xml->toString().contains ("VSTM-DRP-TEXT") && xml->getStringAttribute ("readablePresets") == "0" && ! s2.readablePresets.load(),
               "session state: holds the readable choice only, never preset text; restored");

        p.auth.remove();
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
            // 1.6 Auto Character on, ROBOTISM 25 %: RETUNE / HUMANIZE / SCALE marked AUTO, showing the values it sets
            setParam (p, vstm::ids::robotism, 25.0f);
            setParam (p, vstm::ids::autoCharacter, 1.0f);
            pump (80);
            savePng (*ed, shotDir.getChildFile ("05b_auto_character.png"), 1.0f);
            setParam (p, vstm::ids::autoCharacter, 0.0f);
            setParam (p, vstm::ids::robotism, 50.0f);
            setParam (p, vstm::ids::retune, 15.0f);
            setParam (p, vstm::ids::humanize, 35.0f);
            setParam (p, vstm::ids::vibrato, 100.0f);

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
            setParam (p, vstm::ids::scaleBypass (10), 1.0f);     // 1.5: one bypassed note (b7) in the editor shot
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

            // 1.4: the developer panel opens only with a valid admin authorization
            ed->closeScaleEditor();
            {
                const TestKey k (21);
                p.auth.setTrustedKeysForTesting ({ k.trusted });
                p.auth.remove();
                ed->showDeveloperPanel (true);
                const bool hiddenWithout = ! ed->isDeveloperPanelShown();
                const auto file = signAuth (adminAuth (k), k);
                p.auth.importData (file.getData(), file.getSize());
                ed->showDeveloperPanel (true);
                processSeconds (p, melody, 1.0);
                pump (120);
                const bool shownWith = ed->isDeveloperPanelShown();
                savePng (*ed, shotDir.getChildFile ("09_developer_panel.png"), 1.0f);
                p.auth.remove();
                p.resetExpert();
                ed->showDeveloperPanel (false);
                pump (40);
                CHECK (hiddenWithout && shownWith && ! ed->isDeveloperPanelShown(),
                       "developer panel: not shown without authorization, shown with an admin authorization (09_developer_panel.png)");
            }

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
                CHECK (plugin->getLatencySamples() == 720, "latency reported to the host: %d samples (REALTIME, Tenor: 15 ms)", plugin->getLatencySamples());

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

    authFolder.deleteRecursively();
    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
