//==============================================================================
//  ScreenshotHarness.template.cpp
//
//  Renders the plugin editor to PNG files so a design can be *looked at* (and
//  reviewed with scripts/zoom_review.py) instead of judged from code.
//  Works headless: on Linux run it under xvfb-run.
//
//  Setup (CMake, next to the plugin target; adjust names):
//
//      juce_add_console_app(MyPlugin_Shots PRODUCT_NAME "MyPlugin_Shots")
//      target_sources(MyPlugin_Shots PRIVATE tests/ui_shots.cpp ${MYPLUGIN_SOURCES})
//      target_include_directories(MyPlugin_Shots PRIVATE Source)
//      target_compile_definitions(MyPlugin_Shots PRIVATE
//          JUCE_STANDALONE_APPLICATION=1 JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
//      target_link_libraries(MyPlugin_Shots PRIVATE
//          juce::juce_audio_utils juce::juce_recommended_config_flags)
//
//  Run:
//      cmake --build build --target MyPlugin_Shots
//      xvfb-run -a build/MyPlugin_Shots_artefacts/Release/MyPlugin_Shots ui_shots
//
//  If the project already has a test with createComponentSnapshot (this repo:
//  tests/plugin_test.cpp, run as `SCC_PluginTest <folder>`), extend that instead.
//==============================================================================
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"            // <-- your processor header

namespace
{
    // Let timers, async updates and layout settle before a snapshot.
    void pump (int milliseconds)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (milliseconds);
    }

    // scale 2.0 = what a HiDPI display shows; use 1.0 for a 1x check.
    bool savePng (juce::Component& c, const juce::File& file, float scale)
    {
        const auto image = c.createComponentSnapshot (c.getLocalBounds(), true, scale);
        file.deleteFile();
        juce::FileOutputStream out (file);
        return out.openedOk() && juce::PNGImageFormat().writeImageToStream (image, out);
    }

    // Feed the plugin some audio so meters, scopes and analysers show life instead of silence.
    void playSomeAudio (juce::AudioProcessor& p, int blocks)
    {
        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer midi;
        double phase = 0.0;
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < 512; ++i)
            {
                // two-tone test signal with a slow envelope: harmonics + dynamics to display
                const double env = 0.25 + 0.2 * std::sin (juce::MathConstants<double>::twoPi * (double) (b * 512 + i) / 22050.0);
                const float s = (float) (env * (std::sin (phase) + 0.4 * std::sin (phase * 3.01)));
                phase += juce::MathConstants<double>::twoPi * 220.0 / 44100.0;
                buffer.setSample (0, i, s);
                buffer.setSample (1, i, s);
            }
            p.processBlock (buffer, midi);
            if ((b % 4) == 0)
                pump (16);
        }
    }
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI gui;

    const auto outDir = juce::File::getCurrentWorkingDirectory().getChildFile (argc > 1 ? argv[1] : "ui_shots");
    outDir.createDirectory();

    MyAudioProcessor processor;                       // <-- your processor class
    processor.prepareToPlay (44100.0, 512);

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    if (editor == nullptr)
        return 1;

    // Each entry: file name, editor width, editor height. Cover the default size, the
    // minimum, the maximum, and every distinct screen / mode (set parameters or call
    // your own "show screen X" methods before the shot; see the repo's plugin_test.cpp).
    struct Shot { const char* name; int w, h; };
    const Shot shots[] = {
        { "01_default", editor->getWidth(), editor->getHeight() },
        { "02_min",     600, 460 },                   // <-- your minimum size
        { "03_max",     1440, 1104 },                 // <-- your maximum size
    };

    int written = 0;
    for (const auto& s : shots)
    {
        editor->setSize (s.w, s.h);
        playSomeAudio (processor, 40);
        pump (100);

        for (const float scale : { 1.0f, 2.0f })
        {
            const auto file = outDir.getChildFile (juce::String (s.name) + (scale > 1.5f ? "@2x.png" : ".png"));
            if (savePng (*editor, file, scale))
            {
                std::printf ("wrote %s\n", file.getFullPathName().toRawUTF8());
                ++written;
            }
        }
    }

    editor.reset();                                   // destroy the editor before the processor
    return written > 0 ? 0 : 2;
}
