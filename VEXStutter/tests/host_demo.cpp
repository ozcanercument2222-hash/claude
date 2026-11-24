//==============================================================================
//  VEX Stutter - VST3 host demo (test tool, not part of the plug-in)
//
//    VEXStutter_HostDemo "<path>/VEX Stutter.vst3" [seconds]
//
//  Scans and loads the built VST3 the way a DAW does, runs audio through it on its own thread in
//  real time (48 kHz, 512-sample blocks, stereo) with a playing host transport (128 BPM, 4/4), opens
//  the plug-in's editor and shows the host-side measurements under it: input / output level and how
//  much the effect changes the signal (output - input). Quits after `seconds` (default 8).
//  Linux without a display:  xvfb-run -a VEXStutter_HostDemo ...
//==============================================================================
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <memory>

namespace
{
constexpr double kRate = 48000.0;
constexpr int kBlock = 512;

/** Synthetic sung vowel: a pulse train through three formant resonators, a short melody with vibrato. */
struct Voice
{
    struct Res
    {
        double b0 = 0, a1 = 0, a2 = 0, y1 = 0, y2 = 0;
        void set (double f, double bw)
        {
            const double r = std::exp (-juce::MathConstants<double>::pi * bw / kRate);
            a1 = -2.0 * r * std::cos (juce::MathConstants<double>::twoPi * f / kRate);
            a2 = r * r;
            b0 = (1.0 - r) * 2.0;
        }
        double run (double x) { const double y = b0 * x - a1 * y1 - a2 * y2; y2 = y1; y1 = y; return y; }
    } f1, f2, f3;

    double phase = 0.0, t = 0.0;

    Voice() { f1.set (730.0, 90.0); f2.set (1090.0, 110.0); f3.set (2440.0, 170.0); }

    float next()
    {
        static const double notes[] = { 57, 60, 62, 64, 62, 60 };
        const int i = (int) (t / 0.45) % 6;
        const double inNote = std::fmod (t, 0.45);
        const double f0 = 440.0 * std::pow (2.0, (notes[i] - 69.0 + 0.3 * std::sin (juce::MathConstants<double>::twoPi * 5.5 * t)) / 12.0);
        phase += f0 / kRate;
        double pulse = 0.0;
        if (phase >= 1.0) { phase -= 1.0; pulse = 1.0; }
        const double gate = inNote < 0.38 ? std::min (1.0, inNote / 0.02) : std::max (0.0, 1.0 - (inNote - 0.38) / 0.03);
        t += 1.0 / kRate;
        return (float) (0.35 * gate * (f1.run (pulse) + 0.6 * f2.run (pulse) + 0.3 * f3.run (pulse)));
    }
};

/** The host transport: playing from bar 1 at 128 BPM, advanced by the audio thread. */
struct DemoPlayHead final : public juce::AudioPlayHead
{
    static constexpr double bpm = 128.0;
    std::atomic<double> ppq { 0.0 };

    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        const double q = ppq.load();
        info.setBpm (bpm);
        info.setIsPlaying (true);
        info.setPpqPosition (q);
        info.setPpqPositionOfLastBarStart (std::floor (q / 4.0) * 4.0);
        info.setTimeSignature (TimeSignature { 4, 4 });
        info.setTimeInSamples ((juce::int64) (q * 60.0 / bpm * kRate));
        info.setTimeInSeconds (q * 60.0 / bpm);
        return info;
    }
};

/** What the audio thread measured, read by the UI (atomics only). */
struct Meters
{
    std::atomic<float> inDb { -120.0f }, outDb { -120.0f }, maxDiff { 0.0f };
    std::atomic<long> samples { 0 };
};

/** The "host": calls processBlock() from its own thread at the real-time pace. */
class AudioThread final : public juce::Thread
{
public:
    AudioThread (juce::AudioPluginInstance& p, Meters& m, DemoPlayHead& h) : juce::Thread ("host audio"), plugin (p), meters (m), head (h) {}

    void run() override
    {
        juce::AudioBuffer<float> buffer (2, kBlock), dry (2, kBlock);
        juce::MidiBuffer midi;
        double inPow = 0.0, outPow = 0.0;
        int n = 0;
        const auto blockMs = 1000.0 * kBlock / kRate;
        auto next = juce::Time::getMillisecondCounterHiRes();

        while (! threadShouldExit())
        {
            for (int i = 0; i < kBlock; ++i)
            {
                const float s = voice.next();
                dry.setSample (0, i, s);
                dry.setSample (1, i, 0.8f * s);
            }
            buffer.makeCopyOf (dry, true);
            plugin.processBlock (buffer, midi);
            head.ppq = head.ppq.load() + kBlock * DemoPlayHead::bpm / (60.0 * kRate);

            float diff = meters.maxDiff.load();
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < kBlock; ++i)
                {
                    const float a = dry.getSample (ch, i), b = buffer.getSample (ch, i);
                    inPow += (double) a * a;
                    outPow += (double) b * b;
                    diff = std::max (diff, std::abs (b - a));
                }
            meters.maxDiff = diff;
            n += 2 * kBlock;
            if (n >= (int) (0.3 * kRate) * 2)
            {
                meters.inDb  = (float) (10.0 * std::log10 (inPow / n + 1e-12));
                meters.outDb = (float) (10.0 * std::log10 (outPow / n + 1e-12));
                inPow = outPow = 0.0;
                n = 0;
            }
            meters.samples += kBlock;

            next += blockMs;
            const auto wait = next - juce::Time::getMillisecondCounterHiRes();
            if (wait > 0.0)
                juce::Thread::sleep ((int) wait);
        }
    }

private:
    juce::AudioPluginInstance& plugin;
    Meters& meters;
    DemoPlayHead& head;
    Voice voice;
};

/** Plug-in editor on top, host information below. */
class HostContent final : public juce::Component, private juce::Timer
{
public:
    HostContent (juce::AudioPluginInstance& p, const juce::PluginDescription& d, Meters& m)
        : plugin (p), desc (d), meters (m)
    {
        editor.reset (plugin.createEditorIfNeeded());
        if (editor != nullptr)
            addAndMakeVisible (*editor);
        const int w = editor != nullptr ? editor->getWidth() : 600;
        const int h = editor != nullptr ? editor->getHeight() : 300;
        setSize (w, h + kInfoHeight);
        startTimerHz (10);
    }

    ~HostContent() override
    {
        editor.reset();     // the editor tells its processor itself (AudioProcessorEditor destructor)
    }

    void resized() override
    {
        if (editor != nullptr)
            editor->setTopLeftPosition (0, 0);
    }

    void paint (juce::Graphics& g) override
    {
        auto bar = getLocalBounds().removeFromBottom (kInfoHeight);
        g.setColour (juce::Colour (0xff1e2128));
        g.fillRect (bar);
        g.setColour (juce::Colour (0xff3a404c));
        g.fillRect (bar.removeFromTop (1));
        bar = bar.reduced (14, 8);

        const float diff = meters.maxDiff.load();
        const auto diffText = juce::exactlyEqual (diff, 0.0f) ? juce::String ("0 (bit-exact, no effect)")
                                           : juce::String (20.0 * std::log10 (diff), 1) + " dBFS (the effect)";
        const juce::String lines[] =
        {
            "HOST  JUCE VST3 host (test tool) | plug-in: " + desc.name + " " + desc.version + " by " + desc.manufacturerName
                + " | format " + desc.pluginFormatName + " | " + juce::String (plugin.getParameters().size()) + " parameters",
            "AUDIO " + juce::String ((int) kRate) + " Hz | " + juce::String (kBlock) + " samples | stereo | latency "
                + juce::String (plugin.getLatencySamples()) + " samples | played " + juce::String (meters.samples.load() / kRate, 1)
                + " s | transport playing, " + juce::String (DemoPlayHead::bpm, 1) + " BPM 4/4",
            "LEVEL in " + juce::String (meters.inDb.load(), 1) + " dBFS | out " + juce::String (meters.outDb.load(), 1)
                + " dBFS | max |out - in| = " + diffText,
        };
        g.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain));
        for (const auto& line : lines)
        {
            g.setColour (juce::Colour (0xffd5dbe5));
            g.drawText (line, bar.removeFromTop (20), juce::Justification::centredLeft);
        }
    }

private:
    static constexpr int kInfoHeight = 76;
    void timerCallback() override { repaint (getLocalBounds().removeFromBottom (kInfoHeight)); }

    juce::AudioPluginInstance& plugin;
    juce::PluginDescription desc;
    Meters& meters;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
};

class HostWindow final : public juce::DocumentWindow
{
public:
    HostWindow (const juce::String& title, juce::Component* content)
        : DocumentWindow (title, juce::Colour (0xff1e2128), DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar (false);
        setContentOwned (content, true);
        setTopLeftPosition (40, 40);
        setVisible (true);
    }
    void closeButtonPressed() override { juce::JUCEApplicationBase::quit(); }
};
}

int main (int argc, char* argv[])
{
    if (argc < 2)
    {
        std::printf ("usage: VEXStutter_HostDemo \"<path>/VEX Stutter.vst3\" [seconds]\n");
        return 2;
    }
    juce::ScopedJuceInitialiser_GUI gui;
    const auto vst3 = juce::File::getCurrentWorkingDirectory().getChildFile (juce::String (argv[1]));
    const int seconds = argc > 2 ? juce::jlimit (1, 600, juce::String (argv[2]).getIntValue()) : 8;

    juce::AudioPluginFormatManager formats;
    formats.addFormat (new juce::VST3PluginFormat());
    juce::OwnedArray<juce::PluginDescription> found;
    juce::VST3PluginFormat().findAllTypesForFile (found, vst3.getFullPathName());
    if (found.isEmpty())
    {
        std::printf ("scan: no plug-in found in %s\n", vst3.getFullPathName().toRawUTF8());
        return 1;
    }
    std::printf ("scan: %s %s by %s (%s)\n", found[0]->name.toRawUTF8(), found[0]->version.toRawUTF8(),
                 found[0]->manufacturerName.toRawUTF8(), found[0]->pluginFormatName.toRawUTF8());

    juce::String error;
    auto plugin = formats.createPluginInstance (*found[0], kRate, kBlock, error);
    if (plugin == nullptr)
    {
        std::printf ("load failed: %s\n", error.toRawUTF8());
        return 1;
    }
    plugin->setPlayConfigDetails (2, 2, kRate, kBlock);
    plugin->prepareToPlay (kRate, kBlock);

    Meters meters;
    DemoPlayHead head;
    plugin->setPlayHead (&head);
    AudioThread audio (*plugin, meters, head);
    audio.startThread (juce::Thread::Priority::high);

    {
        HostWindow window (found[0]->name + " (VST3) - host test", new HostContent (*plugin, *found[0], meters));
        juce::MessageManager::getInstance()->runDispatchLoopUntil (seconds * 1000);
        std::printf ("played %.1f s, in %.1f dBFS, out %.1f dBFS, max |out - in| %g\n", meters.samples.load() / kRate,
                     (double) meters.inDb.load(), (double) meters.outDb.load(), (double) meters.maxDiff.load());
    }

    audio.stopThread (2000);
    plugin->setPlayHead (nullptr);
    plugin->releaseResources();
    plugin.reset();
    return 0;
}
