#include <juce_audio_utils/juce_audio_utils.h>
#include <iostream>
#include <memory>

using namespace juce;

static void fail(const String& m) { std::cerr << m << std::endl; }

static void usage()
{
    fail("Usage:\n"
         "  VSTMOfflineHost <plugin.vst3> <input.wav> <output.wav> [sampleRate] [blockSize] [param ...]\n"
         "  VSTMOfflineHost --list <plugin.vst3> [sampleRate] [blockSize]\n"
         "param forms:\n"
         "  id=0.25      normalized value 0..1\n"
         "  id:=15       value as the plugin's own text/units (uses the parameter's text parser)\n"
         "id may be the JUCE parameter ID (e.g. retune), the parameter name, or the VST3 numeric ID.");
}

static std::unique_ptr<AudioPluginInstance> loadPlugin(AudioPluginFormatManager& pluginFormats,
                                                       const File& pluginFile, double sampleRate, int blockSize)
{
    OwnedArray<PluginDescription> matches;
    for (int i = 0; i < pluginFormats.getNumFormats(); ++i)
    {
        auto* f = pluginFormats.getFormat(i);
        if (f->fileMightContainThisPluginType(pluginFile.getFullPathName()))
            f->findAllTypesForFile(matches, pluginFile.getFullPathName());
    }
    if (matches.isEmpty()) { fail("No plugin type found in file. Is this a supported plugin bundle?"); return {}; }

    String error;
    auto plugin = pluginFormats.createPluginInstance(*matches[0], sampleRate, blockSize, error);
    if (!plugin) fail("Plugin load failed: " + error);
    return plugin;
}

// JUCE-built VST3s publish parameters under a hash of the JUCE parameter ID
// (with the sign bit cleared for Studio One compatibility), not the ID itself.
static String juceVst3IdFor(const String& paramID)
{
    return String((uint32) paramID.hashCode() & 0x7fffffffu);
}

static String idOf(AudioProcessorParameter* p)
{
    if (auto* withId = dynamic_cast<AudioProcessorParameterWithID*>(p)) return withId->paramID;
    if (auto* hosted = dynamic_cast<HostedAudioProcessorParameter*>(p)) return hosted->getParameterID();
    return String(p->getParameterIndex());
}

static AudioProcessorParameter* findParameter(AudioPluginInstance& plugin, const String& id)
{
    const auto hashed = juceVst3IdFor(id);
    for (auto* p : plugin.getParameters())
    {
        const auto pid = idOf(p);
        if (pid == id || pid == hashed) return p;
    }
    for (auto* p : plugin.getParameters())
        if (p->getName(256).equalsIgnoreCase(id)) return p;
    return nullptr;
}

static int listParameters(AudioPluginInstance& plugin)
{
    std::cout << "name\tid\tnormalized\ttext\tsteps\n";
    for (auto* p : plugin.getParameters())
        std::cout << p->getName(256) << '\t' << idOf(p) << '\t' << p->getValue() << '\t'
                  << p->getText(p->getValue(), 64) << '\t' << p->getNumSteps() << '\n';
    std::cout << "latency_samples\t" << plugin.getLatencySamples() << '\n'
              << "tail_seconds\t" << plugin.getTailLengthSeconds() << '\n'
              << "inputs\t" << plugin.getTotalNumInputChannels() << '\n'
              << "outputs\t" << plugin.getTotalNumOutputChannels() << std::endl;
    std::cout << "note\tJUCE-built VST3 ids are hashes; pass the JUCE paramID or the name instead." << std::endl;
    return 0;
}

int main(int argc, char* argv[])
{
    ScopedJuceInitialiser_GUI juceInit;
    AudioPluginFormatManager pluginFormats;
    pluginFormats.addDefaultFormats();

    if (argc >= 3 && String(argv[1]) == "--list")
    {
        const double sr = argc > 3 ? String(argv[3]).getDoubleValue() : 48000.0;
        const int bs = argc > 4 ? jmax(16, String(argv[4]).getIntValue()) : 512;
        auto plugin = loadPlugin(pluginFormats, File(argv[2]), sr, bs);
        if (!plugin) return 5;
        plugin->prepareToPlay(sr, bs);
        const int rc = listParameters(*plugin);
        plugin->releaseResources();
        return rc;
    }

    if (argc < 4) { usage(); return 2; }

    const File pluginFile(argv[1]), inputFile(argv[2]), outputFile(argv[3]);
    const double sampleRate = argc > 4 ? String(argv[4]).getDoubleValue() : 48000.0;
    const int blockSize = argc > 5 ? jmax(16, String(argv[5]).getIntValue()) : 512;
    if (!pluginFile.exists() || !inputFile.existsAsFile())
    {
        fail("Plugin or input WAV does not exist."); return 2;
    }

    AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<AudioFormatReader> reader(formats.createReaderFor(inputFile));
    if (!reader) { fail("Could not open input audio."); return 3; }
    if (std::abs(reader->sampleRate - sampleRate) > 0.5)
        fail("WARNING: input WAV is " + String(reader->sampleRate) + " Hz but rendering at "
             + String(sampleRate) + " Hz; audio is not resampled, so pitch/time will be wrong.");

    auto plugin = loadPlugin(pluginFormats, pluginFile, sampleRate, blockSize);
    if (!plugin) return 5;

    for (int i = 6; i < argc; ++i)
    {
        const String arg(argv[i]);
        const bool asText = arg.contains(":=");
        const int eq = asText ? arg.indexOf(":=") : arg.indexOfChar('=');
        if (eq <= 0) { fail("Malformed parameter: " + arg); return 6; }
        const auto id = arg.substring(0, eq);
        const auto valueText = arg.substring(eq + (asText ? 2 : 1));
        auto* parameter = findParameter(*plugin, id);
        if (parameter == nullptr) { fail("Parameter not found: " + id + " (use --list)"); return 6; }
        const float value = asText ? parameter->getValueForText(valueText)
                                   : jlimit(0.0f, 1.0f, (float) valueText.getDoubleValue());
        parameter->setValueNotifyingHost(value);
        std::cerr << "param " << parameter->getName(64) << " = " << value << " ("
                  << parameter->getText(value, 64) << ")" << std::endl;
    }

    plugin->setRateAndBufferSizeDetails(sampleRate, blockSize);
    plugin->setNonRealtime(true);
    plugin->prepareToPlay(sampleRate, blockSize);

    const int inChannels = (int) reader->numChannels;
    const int totalSamples = (int) reader->lengthInSamples;
    const int outChannels = jmax(1, plugin->getTotalNumOutputChannels());
    const int pluginInChannels = plugin->getTotalNumInputChannels();
    AudioBuffer<float> buffer(jmax(inChannels, outChannels, pluginInChannels), blockSize);
    MidiBuffer midi;
    std::cerr << "latency_samples " << plugin->getLatencySamples() << std::endl;

    outputFile.getParentDirectory().createDirectory();
    std::unique_ptr<FileOutputStream> stream(outputFile.createOutputStream());
    if (!stream) { fail("Could not create output file."); return 7; }
    stream->setPosition(0);
    stream->truncate();
    WavAudioFormat wav;
    std::unique_ptr<AudioFormatWriter> writer(wav.createWriterFor(
        stream.get(), sampleRate, (unsigned int) outChannels, 24, {}, 0));
    if (!writer) { fail("Could not create WAV writer."); return 8; }
    stream.release(); // writer owns stream

    int position = 0;
    while (position < totalSamples)
    {
        const int count = jmin(blockSize, totalSamples - position);
        buffer.clear();
        reader->read(&buffer, 0, count, position, true, inChannels > 1);
        if (inChannels == 1) // mono source: feed the same signal to every plugin input
            for (int ch = 1; ch < buffer.getNumChannels(); ++ch)
                buffer.copyFrom(ch, 0, buffer, 0, 0, count);
        else
            for (int ch = inChannels; ch < buffer.getNumChannels(); ++ch)
                buffer.clear(ch, 0, count);
        midi.clear();
        plugin->processBlock(buffer, midi);
        if (!writer->writeFromAudioSampleBuffer(buffer, 0, count))
        {
            fail("WAV write failed."); plugin->releaseResources(); return 9;
        }
        position += count;
    }

    writer.reset();
    plugin->releaseResources();
    std::cout << "Rendered " << totalSamples << " samples to " << outputFile.getFullPathName() << std::endl;
    return 0;
}
