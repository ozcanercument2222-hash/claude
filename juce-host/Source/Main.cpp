#include <juce_audio_utils/juce_audio_utils.h>
#include <iostream>
#include <memory>

using namespace juce;

static void fail(const String& m) { std::cerr << m << std::endl; }

int main(int argc, char* argv[])
{
    ScopedJuceInitialiser_GUI juceInit;
    if (argc < 4)
    {
        fail("Usage: VSTMOfflineHost <plugin.vst3> <input.wav> <output.wav> [sampleRate] [blockSize] [parameterId=normalizedValue ...]");
        return 2;
    }

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

    AudioPluginFormatManager pluginFormats;
    pluginFormats.addDefaultFormats();
    KnownPluginList known;
    OwnedArray<PluginDescription> matches;

    for (int i = 0; i < pluginFormats.getNumFormats(); ++i)
    {
        auto* f = pluginFormats.getFormat(i);
        if (f->fileMightContainThisPluginType(pluginFile.getFullPathName()))
            f->findAllTypesForFile(matches, pluginFile.getFullPathName());
    }
    if (matches.isEmpty()) { fail("No plugin type found in file. Is this a supported plugin bundle?"); return 4; }

    String error;
    std::unique_ptr<AudioPluginInstance> plugin(
        pluginFormats.createPluginInstance(*matches[0], sampleRate, blockSize, error));
    if (!plugin) { fail("Plugin load failed: " + error); return 5; }

    // Set parameters by JUCE AudioProcessorParameterWithID::paramID.
    // Values are normalized 0..1, not engineering units.
    for (int i = 6; i < argc; ++i)
    {
        const String arg(argv[i]);
        const int eq = arg.indexOfChar('=');
        if (eq <= 0) { fail("Ignoring malformed parameter: " + arg); continue; }
        const auto id = arg.substring(0, eq);
        const float value = jlimit(0.0f, 1.0f, (float) arg.substring(eq + 1).getDoubleValue());
        bool found = false;
        for (auto* parameter : plugin->getParameters())
        {
            if (auto* withId = dynamic_cast<AudioProcessorParameterWithID*>(parameter))
            {
                if (withId->paramID == id)
                {
                    parameter->setValueNotifyingHost(value);
                    found = true;
                    break;
                }
            }
        }
        if (!found) { fail("Parameter ID not found: " + id); return 6; }
    }

    plugin->setRateAndBufferSizeDetails(sampleRate, blockSize);
    plugin->setNonRealtime(true);
    plugin->prepareToPlay(sampleRate, blockSize);

    const int inChannels = (int) reader->numChannels;
    const int totalSamples = (int) reader->lengthInSamples;
    const int outChannels = jmax(1, plugin->getTotalNumOutputChannels());
    AudioBuffer<float> buffer(jmax(inChannels, outChannels), blockSize);
    MidiBuffer midi;

    outputFile.getParentDirectory().createDirectory();
    std::unique_ptr<FileOutputStream> stream(outputFile.createOutputStream());
    if (!stream) { fail("Could not create output file."); return 7; }
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
