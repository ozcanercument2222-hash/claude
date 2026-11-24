#include "UI/WaveformView.h"
#include "UI/Theme.h"

#include <cmath>

namespace vex::ui
{
namespace
{
    constexpr int64_t kViewColumns = (int64_t) (dsp::CaptureBuffer::kSeconds * dsp::CaptureBuffer::kColumnsPerSecond);
    constexpr float kColumnsPerSecond = (float) dsp::CaptureBuffer::kColumnsPerSecond;
    constexpr float kHandleGrab = 7.0f;
}

WaveformView::WaveformView (VEXStutterAudioProcessor& p) : processor (p)
{
    setOpaque (false);
    setRepaintsOnMouseActivity (false);
    setTooltip ("Live: the last 8 s of the input; the cyan bar marks the fragment being repeated, the blue selection is "
                "what Freeze will play. Frozen: drag the selection's edges, drag inside to move it, double-click for all.");
}

juce::Rectangle<float> WaveformView::plotArea() const
{
    return getLocalBounds().toFloat().reduced (2.0f, 18.0f);
}

float WaveformView::xForFraction (float f) const
{
    // the capture span inside the view
    const auto area = plotArea();
    const double viewCols = (double) std::max<int64_t> (1, viewEnd - viewStart);
    const double col = (double) spanStart + (double) f * (double) (spanEnd - spanStart);
    return area.getX() + (float) ((col - (double) viewStart) / viewCols) * area.getWidth();
}

float WaveformView::fractionForX (float x) const
{
    const auto area = plotArea();
    const double viewCols = (double) std::max<int64_t> (1, viewEnd - viewStart);
    const double col = (double) viewStart + (double) ((x - area.getX()) / area.getWidth()) * viewCols;
    const double span = (double) std::max<int64_t> (1, spanEnd - spanStart);
    return juce::jlimit (0.0f, 1.0f, (float) ((col - (double) spanStart) / span));
}

void WaveformView::refresh()
{
    const auto& cap = processor.getCapture();
    auto& st = processor.getEngineStatus();
    const bool nowFrozen = cap.uiFrozen();
    const int64_t latest = cap.liveColumnsWritten();
    const float s0 = processor.getSelectionStart(), s1 = processor.getSelectionEnd();
    const int64_t fpc = std::max<int64_t> (1, cap.uiFramesPerColumn());
    const int64_t src0 = st.sourceStart.load(), src1 = st.sourceEnd.load();
    const int64_t newSrc0 = src0 >= 0 ? src0 / fpc : -1, newSrc1 = src1 >= 0 ? src1 / fpc : -1;
    const int64_t newSliceStart = st.sliceStartFrame.load() / fpc;
    const float newSliceCols = st.sliceFrames.load() / (float) fpc;
    const int newStep = st.step.load();

    const bool changed = nowFrozen != frozen || (! nowFrozen && latest != lastLatest) || ! juce::exactlyEqual (s0, selStart)
                         || ! juce::exactlyEqual (s1, selEnd) || newSrc0 != srcStartCol || newSrc1 != srcEndCol
                         || newSliceStart != sliceStartCol || newStep != sliceStep || lo.empty();
    if (! changed)
        return;

    frozen = nowFrozen;
    selStart = s0;
    selEnd = s1;
    srcStartCol = newSrc0;
    srcEndCol = newSrc1;
    sliceStartCol = newSliceStart;
    sliceCols = std::max (1.0f, newSliceCols);
    sliceStep = newStep;

    if (frozen)
    {
        spanStart = viewStart = cap.uiFrozenColumnStart();
        spanEnd = viewEnd = std::max (spanStart + 1, cap.uiFrozenColumnEnd());
    }
    else
    {
        viewEnd = latest;
        viewStart = latest - kViewColumns;
        spanEnd = latest;
        spanStart = std::max ({ viewStart, cap.uiLiveBankStartColumn(), (int64_t) 0 });
        if (spanEnd <= spanStart)
            spanEnd = spanStart + 1;
    }

    // one min / max per plot column
    const auto area = plotArea();
    const int n = std::max (1, (int) area.getWidth());
    lo.assign ((size_t) n, 0.0f);
    hi.assign ((size_t) n, 0.0f);
    const double perPixel = (double) (viewEnd - viewStart) / (double) n;
    const int64_t oldestHeld = latest - (dsp::CaptureBuffer::kColumns - 8);
    for (int x = 0; x < n; ++x)
    {
        const auto c0 = viewStart + (int64_t) std::floor ((double) x * perPixel);
        const auto c1 = std::max (c0 + 1, viewStart + (int64_t) std::floor ((double) (x + 1) * perPixel));
        float mn = 0.0f, mx = 0.0f;
        for (int64_t c = c0; c < c1; ++c)
        {
            if (c < 0 || (! frozen && (c >= latest || c < oldestHeld)))
                continue;
            float a = 0.0f, b = 0.0f;
            cap.column (frozen, c, a, b);
            mn = std::min (mn, a);
            mx = std::max (mx, b);
        }
        lo[(size_t) x] = mn;
        hi[(size_t) x] = mx;
    }
    lastLatest = latest;
    repaint();
}

void WaveformView::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto area = plotArea();
    const float midY = area.getCentreY();
    const float halfH = area.getHeight() * 0.5f;
    const int n = (int) lo.size();
    const auto& st = processor.getEngineStatus();

    // capture span (live: what a freeze would hold); outside it is dimmed
    const float spanX0 = xForFraction (0.0f);
    if (! frozen && spanX0 > area.getX() + 1.0f)
    {
        g.setColour (juce::Colours::black.withAlpha (0.25f));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (area.getX(), area.getY(), spanX0, area.getBottom()));
    }

    // slice grid (current Rate), phrase starts brighter
    if (! frozen && sliceCols > 1.0f && viewEnd > viewStart)
    {
        const double cols = (double) (viewEnd - viewStart);
        const int steps = std::max (1, st.steps.load());
        for (int k = 0; k < 400; ++k)
        {
            const double col = (double) sliceStartCol - (double) k * sliceCols;
            if (col < (double) viewStart)
                break;
            if (col > (double) viewEnd)
                continue;
            const float x = area.getX() + (float) ((col - (double) viewStart) / cols) * area.getWidth();
            const bool phraseStart = ((sliceStep - k) % steps + steps) % steps == 0;
            g.setColour (col::accent.withAlpha (phraseStart ? 0.28f : 0.09f));
            g.fillRect (juce::Rectangle<float> (x, area.getY(), phraseStart ? 1.5f : 1.0f, area.getHeight()));
        }
    }

    // centre line
    g.setColour (col::edge.withAlpha (0.8f));
    g.fillRect (juce::Rectangle<float> (area.getX(), midY - 0.5f, area.getWidth(), 1.0f));

    // waveform: outside the selection grey-blue, inside blue with a glow
    const float selX0 = xForFraction (selStart), selX1 = xForFraction (selEnd);
    // square-root amplitude scale: full scale = full height, -20 dBFS still a third of it, so quiet
    // vocals stay readable without a gain that would flatten loud ones
    auto scale = [] (float v) { return std::sqrt (juce::jlimit (0.0f, 1.0f, std::abs (v))); };
    juce::Path outside, inside;
    for (int x = 0; x < n; ++x)
    {
        const float px = area.getX() + (float) x;
        const float top = midY - (hi[(size_t) x] > 0.0f ? scale (hi[(size_t) x]) : 0.0f) * halfH;
        const float bot = midY + (lo[(size_t) x] < 0.0f ? scale (lo[(size_t) x]) : 0.0f) * halfH;
        const auto bar = juce::Rectangle<float> (px, std::min (top, midY - 0.5f), 1.0f, std::max (1.0f, bot - top));
        (px >= selX0 && px <= selX1 ? inside : outside).addRectangle (bar);
    }
    g.setColour ((frozen ? col::wave.darker (0.3f) : col::wave).withAlpha (0.75f));
    g.fillPath (outside);
    g.setColour ((frozen ? col::ice : col::waveSel).withAlpha (0.35f));
    g.fillPath (inside, juce::AffineTransform::scale (1.0f, 1.08f, area.getCentreX(), midY));
    g.setColour (frozen ? col::ice : col::waveSel);
    g.fillPath (inside);

    // the fragment being repeated right now (live)
    if (! frozen && srcStartCol >= 0 && srcEndCol > srcStartCol)
    {
        const double cols = (double) std::max<int64_t> (1, viewEnd - viewStart);
        const float x0 = area.getX() + (float) ((double) (srcStartCol - viewStart) / cols) * area.getWidth();
        const float x1 = area.getX() + (float) ((double) (srcEndCol - viewStart) / cols) * area.getWidth();
        if (x1 > area.getX() && x0 < area.getRight())
        {
            const auto band = juce::Rectangle<float>::leftTopRightBottom (std::max (x0, area.getX()), area.getBottom() + 4.0f,
                                                                          std::min (x1, area.getRight()), area.getBottom() + 8.0f);
            g.setColour (col::cyan);
            g.fillRoundedRectangle (band, 2.0f);
        }
    }

    // selection
    const auto sel = juce::Rectangle<float>::leftTopRightBottom (selX0, area.getY() - 6.0f, selX1, area.getBottom() + 6.0f);
    const auto selColour = frozen ? col::ice : col::accentHi;
    g.setColour (selColour.withAlpha (frozen ? 0.10f : 0.07f));
    g.fillRect (sel);
    for (float x : { selX0, selX1 })
    {
        g.setColour (selColour.withAlpha (0.9f));
        g.fillRect (juce::Rectangle<float> (x - 0.75f, sel.getY(), 1.5f, sel.getHeight()));
        g.fillRoundedRectangle (juce::Rectangle<float> (7.0f, 9.0f).withCentre ({ x, sel.getY() + 3.0f }), 2.0f);
        g.fillRoundedRectangle (juce::Rectangle<float> (7.0f, 9.0f).withCentre ({ x, sel.getBottom() - 3.0f }), 2.0f);
    }

    // length label: seconds, and beats when the tempo is known
    const float spanSeconds = (float) (spanEnd - spanStart) / kColumnsPerSecond;
    const float seconds = (selEnd - selStart) * spanSeconds;
    juce::String label = seconds < 1.0f ? juce::String (seconds, 2) + "s" : juce::String (seconds, 1) + "s";
    if (st.syncState.load() != dsp::EngineStatus::freeTime)
        label << "  " << juce::String (seconds * st.bpm.load() / 60.0f, 1) << " beats";
    const float labelW = juce::GlyphArrangement::getStringWidth (font (12.5f, Weight::semibold), label) + 18.0f;
    const auto pill = juce::Rectangle<float> (labelW, 18.0f).withCentre ({ juce::jlimit (bounds.getX() + labelW * 0.5f, bounds.getRight() - labelW * 0.5f,
                                                                                         (selX0 + selX1) * 0.5f), bounds.getY() + 10.0f });
    if (frozen)
    {
        g.setColour (col::ice.withAlpha (0.92f));
        g.fillRoundedRectangle (pill, 6.0f);
        drawText (g, label, pill, 12.5f, col::well, juce::Justification::centred, Weight::semibold);
    }
    else
    {
        // live: a preview of what Freeze would play, so the label is outlined, not filled
        g.setColour (col::well.withAlpha (0.85f));
        g.fillRoundedRectangle (pill, 6.0f);
        g.setColour (col::accentHi.withAlpha (0.8f));
        g.drawRoundedRectangle (pill.reduced (0.5f), 6.0f, 1.0f);
        drawText (g, label, pill, 12.5f, col::accentHi, juce::Justification::centred, Weight::semibold);
    }
}

WaveformView::Drag WaveformView::dragKindAt (float x) const
{
    const float x0 = xForFraction (selStart), x1 = xForFraction (selEnd);
    if (std::abs (x - x0) <= kHandleGrab) return Drag::start;
    if (std::abs (x - x1) <= kHandleGrab) return Drag::end;
    if (x > x0 && x < x1) return Drag::move;
    return Drag::create;
}

void WaveformView::mouseMove (const juce::MouseEvent& e)
{
    const auto h = dragKindAt (e.position.x);
    setMouseCursor (h == Drag::start || h == Drag::end ? juce::MouseCursor::LeftRightResizeCursor
                                                       : (h == Drag::move ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::CrosshairCursor));
}

void WaveformView::mouseDown (const juce::MouseEvent& e)
{
    drag = dragKindAt (e.position.x);
    const float f = fractionForX (e.position.x);
    dragAnchor = f;
    dragOffset = f - selStart;
}

void WaveformView::mouseDrag (const juce::MouseEvent& e)
{
    if (drag == Drag::create && e.getDistanceFromDragStart() < 3)
        return;                                 // a click is not a selection
    const float f = fractionForX (e.position.x);
    float a = selStart, b = selEnd;
    switch (drag)
    {
        case Drag::start:  a = std::min (f, selEnd); break;
        case Drag::end:    b = std::max (f, selStart); break;
        case Drag::move:
        {
            const float len = selEnd - selStart;
            a = juce::jlimit (0.0f, 1.0f - len, f - dragOffset);
            b = a + len;
            break;
        }
        case Drag::create: a = std::min (dragAnchor, f); b = std::max (dragAnchor, f); break;
        case Drag::none:   return;
    }
    processor.setSelection (a, b);
    refresh();
}

void WaveformView::mouseUp (const juce::MouseEvent&)
{
    drag = Drag::none;
}

void WaveformView::mouseDoubleClick (const juce::MouseEvent&)
{
    processor.setSelection (0.0f, 1.0f);
    refresh();
}
}
