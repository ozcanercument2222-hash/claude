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

    //--------------------------------------------------------------------------
    std::printf ("\n=== 1) Parameters ===\n");
    {
        VSTMTuneProcessor p;
        auto text = [&p] (const juce::String& id) { return p.apvts.getParameter (id)->getCurrentValueAsText(); };
        CHECK (text (vstm::ids::key) == "Auto" && text (vstm::ids::scale) == "Major", "defaults: Key Auto, Scale Major");
        CHECK (text (vstm::ids::correction) == "100%" && text (vstm::ids::retune) == "15 ms" && text (vstm::ids::humanize) == "35%",
               "defaults: %s / %s / %s", text (vstm::ids::correction).toRawUTF8(), text (vstm::ids::retune).toRawUTF8(), text (vstm::ids::humanize).toRawUTF8());
        CHECK (text (vstm::ids::robotism) == "Dengeli", "robotism default text: %s", text (vstm::ids::robotism).toRawUTF8());
        setParam (p, vstm::ids::robotism, 0.0f);
        const auto robotText = text (vstm::ids::robotism);
        setParam (p, vstm::ids::robotism, 80.0f);
        const auto humanText = text (vstm::ids::robotism);
        CHECK (robotText == "Robotik 100%" && humanText.endsWith ("60%"), "robotism text: \"%s\" / \"%s\"", robotText.toRawUTF8(), humanText.toRawUTF8());
        auto* r = p.apvts.getParameter (vstm::ids::robotism);
        CHECK (std::abs (r->convertFrom0to1 (r->getValueForText ("Robotik 40%")) - 30.0f) < 0.01f, "robotism parses its own text");
        CHECK (text (vstm::ids::formant) == "0.0" && text (vstm::ids::transpose) == "0 st" && text (vstm::ids::mix) == "100%" && text (vstm::ids::vibrato) == "100%",
               "PARAMETERS row: %s / %s / %s / %s", text (vstm::ids::vibrato).toRawUTF8(), text (vstm::ids::formant).toRawUTF8(),
               text (vstm::ids::transpose).toRawUTF8(), text (vstm::ids::mix).toRawUTF8());
        CHECK (p.getBypassParameter() == p.apvts.getParameter (vstm::ids::bypass), "host bypass is the AUTOTUNE ON switch");
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
