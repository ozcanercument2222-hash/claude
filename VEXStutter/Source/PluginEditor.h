#pragma once

#include "PluginProcessor.h"
#include "UI/Theme.h"

#include <memory>

/** VEX Stutter editor: the VEX interface (specification section 14 and the approved mockup).

    Every control is bound to a real parameter through JUCE attachments; every display shows real
    data: the waveform and meters read the engine's atomic snapshots, nothing is decorative or
    simulated. The editor never touches audio buffers and never blocks the audio thread.

    Layout: designed on a 1120 x 630 canvas (vex::ui::dims) and scaled as a whole (70 - 160 %,
    fixed aspect ratio; drag the corner or use the settings menu). The UI refreshes at 30 Hz and
    repaints only what changed. */
class VEXStutterAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                             private juce::Timer
{
public:
    static constexpr int kWidth = vex::ui::dims::width, kHeight = vex::ui::dims::height;

    explicit VEXStutterAudioProcessorEditor (VEXStutterAudioProcessor&);
    ~VEXStutterAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** For screenshots and tests: show / hide the FX details panel, run one UI refresh now. */
    void setFxDetailsVisible (bool);
    void refreshNow();
    /** Sets the editor size as a factor of the design size (also remembered by the processor). */
    void setUiScale (float);

    class Content;

private:
    void timerCallback() override;

    VEXStutterAudioProcessor& processorRef;
    std::unique_ptr<juce::LookAndFeel> lookAndFeel;
    std::unique_ptr<Content> content;
    std::unique_ptr<juce::TooltipWindow> tooltips;
    double lastTick = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VEXStutterAudioProcessorEditor)
};
