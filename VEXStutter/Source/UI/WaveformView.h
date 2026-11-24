#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"

#include <vector>

namespace vex::ui
{
/** The capture as the engine holds it (real data, never decorative):
      LIVE    the last 8 s of the dry input, scrolling; the selection previews the part a freeze
              would use; the fragment being repeated right now is marked underneath.
      FROZEN  the frozen capture; the selection (drag its handles, drag inside to move it, drag
              elsewhere for a new one, double-click for all) is what the repeats play.
    Slice boundaries of the current grid are drawn over the waveform. Reads only the capture's
    atomic peak columns and the engine status; the selection goes to the processor. */
class WaveformView final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit WaveformView (VEXStutterAudioProcessor&);

    /** Called by the editor's timer: re-reads the columns; repaints only when something changed. */
    void refresh();

    void paint (juce::Graphics&) override;
    void resized() override { lastLatest = -1; }
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    enum class Drag { none, start, end, move, create };

    juce::Rectangle<float> plotArea() const;
    float xForFraction (float f) const;
    float fractionForX (float x) const;
    Drag dragKindAt (float x) const;

    VEXStutterAudioProcessor& processor;
    std::vector<float> lo, hi;                  // one min / max pair per plot column
    int64_t spanStart = 0, spanEnd = 0;         // capture span in waveform columns (what a selection maps to)
    int64_t viewStart = 0, viewEnd = 0;         // columns shown
    int64_t lastLatest = -1;
    bool frozen = false;
    float selStart = 0.0f, selEnd = 1.0f;
    int64_t srcStartCol = -1, srcEndCol = -1;
    int64_t sliceStartCol = 0;
    float sliceCols = 50.0f;
    int sliceStep = 0;
    Drag drag = Drag::none;
    float dragAnchor = 0.0f, dragOffset = 0.0f;
};
}
