#include <juce_audio_formats/juce_audio_formats.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Parameters/ParameterIDs.h"
#include "Parameters/ParameterLayout.h"
#include "Parameters/StateUtils.h"

#include <cmath>

namespace
{
    const juce::Identifier kStateVersionKey  { "stateVersion" };
    const juce::Identifier kPluginVersionKey { "pluginVersion" };
    const juce::Identifier kSelStartKey      { "selectionStart" };
    const juce::Identifier kSelEndKey        { "selectionEnd" };
    const juce::Identifier kUiScaleKey       { "uiScale" };

    template <typename Enum>
    Enum choice (const std::atomic<float>* v, int count) noexcept
    {
        return (Enum) juce::jlimit (0, count - 1, (int) std::lround (v->load (std::memory_order_relaxed)));
    }

    float percent (const std::atomic<float>* v) noexcept { return v->load (std::memory_order_relaxed) * 0.01f; }
    bool on (const std::atomic<float>* v) noexcept      { return v->load (std::memory_order_relaxed) > 0.5f; }
    float real (const std::atomic<float>* v) noexcept   { return v->load (std::memory_order_relaxed); }

    /** A stored selection edge: a finite number in 0..1, otherwise the fallback. */
    float selectionValue (const juce::var& stored, float fallback)
    {
        const double v = vex::state::parseStoredNumber (stored);
        return std::isfinite (v) ? juce::jlimit (0.0f, 1.0f, (float) v) : fallback;
    }
}

VEXStutterAudioProcessor::VEXStutterAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "VEXStutterState", vex::params::createLayout()),
      presetManager (apvts)
{
    bypassParam = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter (vex::ids::bypass));
    jassert (bypassParam != nullptr);

    auto get = [this] (const char* id)
    {
        auto* p = apvts.getRawParameterValue (id);
        jassert (p != nullptr);
        return p;
    };
    raw.bypass = get (vex::ids::bypass);           raw.mode = get (vex::ids::mode);
    raw.amount = get (vex::ids::amount);           raw.sync = get (vex::ids::sync);
    raw.rate = get (vex::ids::rate);               raw.rateMs = get (vex::ids::rateMs);
    raw.length = get (vex::ids::length);           raw.style = get (vex::ids::style);
    raw.shape = get (vex::ids::shape);             raw.smoothness = get (vex::ids::smoothness);
    raw.offsetMs = get (vex::ids::offsetMs);       raw.mix = get (vex::ids::mix);
    raw.glitchType = get (vex::ids::glitchType);   raw.intensity = get (vex::ids::intensity);
    raw.freeze = get (vex::ids::freeze);           raw.fxEnabled = get (vex::ids::fxEnabled);
    raw.delaySync = get (vex::ids::delaySync);     raw.delayRate = get (vex::ids::delayRate);
    raw.delayTimeMs = get (vex::ids::delayTimeMs); raw.delayFeedback = get (vex::ids::delayFeedback);
    raw.delayMix = get (vex::ids::delayMix);       raw.reverbSize = get (vex::ids::reverbSize);
    raw.reverbDamping = get (vex::ids::reverbDamping);
    raw.reverbMix = get (vex::ids::reverbMix);
}

void VEXStutterAudioProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    // Every buffer (capture banks, delay lines, reverb) is allocated here, sized from these values,
    // never inside processBlock(). The engine works with any block size the host sends.
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    prepared.store (false);
    engine.prepare (currentSampleRate, juce::jmax (1, maximumExpectedSamplesPerBlock));
    prepared.store (true);
}

bool VEXStutterAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();

    // mono -> mono or stereo -> stereo: the stereo image is never collapsed or invented
    return in == out && (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo());
}

vex::dsp::EngineParams VEXStutterAudioProcessor::readParams() const noexcept
{
    using namespace vex::choices;
    vex::dsp::EngineParams p;
    p.bypass = on (raw.bypass);
    p.mode = choice<Mode> (raw.mode, numModes);
    p.amount = percent (raw.amount);
    p.sync = on (raw.sync);
    p.rateIndex = (int) std::lround (real (raw.rate));
    p.rateMs = real (raw.rateMs);
    p.length = percent (raw.length);
    p.style = choice<Style> (raw.style, numStyles);
    p.shape = choice<Shape> (raw.shape, numShapes);
    p.smoothness = percent (raw.smoothness);
    p.offsetMs = real (raw.offsetMs);
    p.mix = percent (raw.mix);
    p.glitchType = choice<GlitchType> (raw.glitchType, numGlitchTypes);
    p.intensity = percent (raw.intensity);
    p.freeze = on (raw.freeze);
    p.selectionStart = selectionStart.load (std::memory_order_relaxed);
    p.selectionEnd = selectionEnd.load (std::memory_order_relaxed);
    p.fxEnabled = on (raw.fxEnabled);
    p.delaySync = on (raw.delaySync);
    p.delayRateIndex = (int) std::lround (real (raw.delayRate));
    p.delayTimeMs = real (raw.delayTimeMs);
    p.delayFeedback = percent (raw.delayFeedback);
    p.delayMix = percent (raw.delayMix);
    p.reverbSize = percent (raw.reverbSize);
    p.reverbDamping = percent (raw.reverbDamping);
    p.reverbMix = percent (raw.reverbMix);
    return p;
}

bool VEXStutterAudioProcessor::renderSelection (juce::AudioBuffer<float>& out, juce::String& error) const
{
    const auto& capture = engine.getCapture();
    const auto len = capture.uiFrozenLength();
    if (! capture.uiFrozen() || len <= 0)
    {
        error = "Freeze the waveform first, then select the part to export.";
        return false;
    }
    const float s0 = juce::jlimit (0.0f, 1.0f, getSelectionStart()), s1 = juce::jlimit (0.0f, 1.0f, getSelectionEnd());
    const auto from = (int64_t) std::floor (std::min (s0, s1) * (double) len);
    const auto to = std::max (from + 1, (int64_t) std::ceil (std::max (s0, s1) * (double) len));
    const int n = (int) std::min<int64_t> (to - from, len);
    juce::AudioBuffer<float> src (2, n);
    if (! capture.copyFrozen (from, from + n, src.getWritePointer (0), src.getWritePointer (1)))
    {
        error = "The capture changed while exporting; try again.";
        return false;
    }

    // a separate engine with the current settings, playing the selection as live input (freeze off)
    auto offline = std::make_unique<vex::dsp::StutterEngine>();
    constexpr int kBlock = 512;
    offline->prepare (currentSampleRate, kBlock);
    auto params = readParams();
    params.freeze = false;
    params.bypass = false;
    vex::dsp::Transport t;
    t.hasTempo = true;
    t.bpm = juce::jlimit (20.0, 999.0, (double) engine.status.bpm.load (std::memory_order_relaxed));
    t.hasPosition = true;
    t.playing = true;
    const int tail = (int) std::ceil (std::min (10.0, getTailLengthSeconds()) * currentSampleRate);
    out.setSize (2, n + tail, false, true, false);
    out.clear();
    for (int ch = 0; ch < 2; ++ch)
        out.copyFrom (ch, 0, src, ch, 0, n);
    float* chans[2] = { nullptr, nullptr };
    for (int pos = 0; pos < out.getNumSamples(); pos += kBlock)
    {
        const int m = std::min (kBlock, out.getNumSamples() - pos);
        chans[0] = out.getWritePointer (0, pos);
        chans[1] = out.getWritePointer (1, pos);
        t.ppq = (double) pos / currentSampleRate * t.bpm / 60.0;
        offline->process (chans, 2, m, params, t);
    }
    // trailing silence (below -90 dBFS) is not part of the clip
    int last = out.getNumSamples();
    while (last > n && std::max (std::abs (out.getSample (0, last - 1)), std::abs (out.getSample (1, last - 1))) < 3.2e-5f)
        --last;
    out.setSize (2, last, true, true, false);
    return true;
}

juce::File VEXStutterAudioProcessor::renderSelectionClip (juce::String& error) const
{
    juce::AudioBuffer<float> clip;
    if (! renderSelection (clip, error))
        return {};
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("VEX Stutter Clips");
    if (! dir.createDirectory())
    {
        error = "Cannot create " + dir.getFullPathName();
        return {};
    }
    auto file = dir.getNonexistentChildFile ("VEX_" + juce::Time::getCurrentTime().formatted ("%Y%m%d_%H%M%S"), ".wav", false);
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::FileOutputStream> stream (file.createOutputStream());
    std::unique_ptr<juce::AudioFormatWriter> writer;
    if (stream != nullptr)
        writer.reset (wav.createWriterFor (stream.get(), currentSampleRate, 2, 24, {}, 0));
    if (writer != nullptr)
        stream.release();                                   // the writer owns the stream now
    if (writer == nullptr || ! writer->writeFromAudioSampleBuffer (clip, 0, clip.getNumSamples()))
    {
        error = "Cannot write " + file.getFullPathName();
        return {};
    }
    return file;
}

vex::dsp::Transport VEXStutterAudioProcessor::readTransport() const
{
    vex::dsp::Transport t;
    if (auto* hostPlayHead = getPlayHead())
        if (const auto pos = hostPlayHead->getPosition())
        {
            if (const auto bpm = pos->getBpm())                         { t.hasTempo = true; t.bpm = *bpm; }
            if (const auto ppq = pos->getPpqPosition())                 { t.hasPosition = true; t.ppq = *ppq; }
            if (const auto bar = pos->getPpqPositionOfLastBarStart())   { t.hasBarStart = true; t.barStartPpq = *bar; }
            if (const auto sig = pos->getTimeSignature())               { t.numerator = sig->numerator; t.denominator = sig->denominator; }
            t.playing = pos->getIsPlaying();
        }
    return t;
}

void VEXStutterAudioProcessor::runEngine (juce::AudioBuffer<float>& buffer, bool hostBypass)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0)
        return;

    // The supported layouts have as many outputs as inputs; never leave stale data in an extra output.
    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    const int channels = juce::jmin (2, getTotalNumInputChannels(), getTotalNumOutputChannels(), buffer.getNumChannels());
    if (channels <= 0 || ! prepared.load (std::memory_order_acquire))
        return;                                         // not prepared yet: dry pass-through

    auto params = readParams();
    params.bypass = params.bypass || hostBypass;
    engine.process (buffer.getArrayOfWritePointers(), channels, numSamples, params, readTransport());
}

void VEXStutterAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused (midi);
    runEngine (buffer, false);
}

void VEXStutterAudioProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    // a host bypassing the plug-in without its bypass parameter: the same 20 ms crossfade, and the
    // capture and timing keep running so un-bypassing continues in time (specification section 12)
    juce::ignoreUnused (midi);
    runEngine (buffer, true);
}

double VEXStutterAudioProcessor::getTailLengthSeconds() const
{
    // repeats can continue until the end of the phrase, then the FX tails ring out
    const double phrase = (double) engine.status.sliceSeconds.load() * (double) engine.status.steps.load();
    double tail = juce::jlimit (0.0, 16.0, phrase);
    if (on (raw.fxEnabled))
    {
        if (real (raw.delayMix) > 0.0f)
        {
            const double bpm = juce::jlimit (20.0, 999.0, (double) engine.status.bpm.load());
            const double time = on (raw.delaySync) ? vex::divisions::seconds ((int) std::lround (real (raw.delayRate)), bpm)
                                                   : real (raw.delayTimeMs) * 0.001;
            const double fb = juce::jlimit (0.01, 0.85, (double) percent (raw.delayFeedback));
            tail += time * (1.0 + std::log (0.001) / std::log (fb));          // repeats until -60 dB
        }
        if (real (raw.reverbMix) > 0.0f)
        {
            const double g = 0.70 + 0.28 * juce::jlimit (0.0, 1.0, (double) percent (raw.reverbSize));
            tail += 3.0 * 0.031 / -std::log10 (g);                             // comb RT60 estimate
        }
    }
    return juce::jlimit (0.0, 60.0, tail);
}

void VEXStutterAudioProcessor::setSelection (float start, float end) noexcept
{
    if (! std::isfinite (start) || ! std::isfinite (end))
        return;
    start = juce::jlimit (0.0f, 1.0f, start);
    end = juce::jlimit (0.0f, 1.0f, end);
    if (end < start)
        std::swap (start, end);
    selectionStart.store (start, std::memory_order_relaxed);
    selectionEnd.store (end, std::memory_order_relaxed);
}

juce::AudioProcessorEditor* VEXStutterAudioProcessor::createEditor()
{
    return new VEXStutterAudioProcessorEditor (*this);
}

void VEXStutterAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // Canonical output: the parameters in their permanent order (vex::ids::all), each value written from
    // its parameter (an atomic, the same float computation every time). The APVTS tree itself keeps its
    // children in whatever order it was built or last loaded, so it is not written as it is. An unchanged
    // restored session then saves to the same bytes, and hosts see no change where there is none.
    juce::ValueTree state (apvts.state.getType());
    state.setProperty (kStateVersionKey, kStateVersion, nullptr);
    state.setProperty (kPluginVersionKey, VEX_VERSION_STRING, nullptr);
    state.setProperty (kSelStartKey, (double) getSelectionStart(), nullptr);
    state.setProperty (kSelEndKey, (double) getSelectionEnd(), nullptr);
    state.setProperty (kUiScaleKey, (double) getUiScale(), nullptr);
    presetManager.writeToState (state);

    for (const char* id : vex::ids::all)
        if (auto* param = apvts.getParameter (id))
        {
            juce::ValueTree node (vex::state::kParamType);
            node.setProperty (vex::state::kIdKey, id, nullptr);
            node.setProperty (vex::state::kValueKey, vex::state::realValue (*param), nullptr);
            state.appendChild (node, nullptr);
        }

    if (const auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void VEXStutterAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // Untrusted input: anything that is not our own state tree is ignored and the current state kept.
    if (data == nullptr || sizeInBytes <= 0)
        return;

    const std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType().toString()))
        return;

    const auto loaded = juce::ValueTree::fromXml (*xml);
    if (! loaded.isValid())
        return;

    // Older states (lower or missing stateVersion) load with defaults for what they lack; newer ones
    // load every parameter this build knows. Migrations go here once kStateVersion > 1.
    apvts.replaceState (sanitiseState (loaded));
    setSelection (selectionValue (loaded.getProperty (kSelStartKey), kDefaultSelectionStart),
                  selectionValue (loaded.getProperty (kSelEndKey), kDefaultSelectionEnd));
    presetManager.readFromState (loaded);
    const double scale = vex::state::parseStoredNumber (loaded.getProperty (kUiScaleKey));
    if (std::isfinite (scale))
        setUiScale ((float) scale);
}

juce::ValueTree VEXStutterAudioProcessor::sanitiseState (const juce::ValueTree& loaded) const
{
    juce::ValueTree clean (apvts.state.getType());
    clean.setProperty (kStateVersionKey, kStateVersion, nullptr);

    for (const char* id : vex::ids::all)
    {
        auto* param = apvts.getParameter (id);
        if (param == nullptr)
            continue;

        float value = param->convertFrom0to1 (param->getDefaultValue());

        // the first PARAM child with this id wins
        for (int i = 0; i < loaded.getNumChildren(); ++i)
        {
            const auto child = loaded.getChild (i);
            if (child.hasType (vex::state::kParamType) && child.getProperty (vex::state::kIdKey).toString() == id)
            {
                value = vex::state::legalValue (*param, vex::state::parseStoredNumber (child.getProperty (vex::state::kValueKey)));
                break;
            }
        }

        juce::ValueTree node (vex::state::kParamType);
        node.setProperty (vex::state::kIdKey, id, nullptr);
        node.setProperty (vex::state::kValueKey, value, nullptr);
        clean.appendChild (node, nullptr);
    }
    return clean;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VEXStutterAudioProcessor();
}
