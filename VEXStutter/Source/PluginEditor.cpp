#include "PluginEditor.h"
#include "Parameters/Choices.h"
#include "Parameters/Divisions.h"
#include "Parameters/ParameterIDs.h"
#include "UI/Controls.h"
#include "UI/Icons.h"
#include "UI/WaveformView.h"

#include <cmath>

using namespace vex::ui;

namespace
{
    namespace ids = vex::ids;

    //==========================================================================
    /** Menus, alert windows and tooltips in the VEX colours and font. */
    class VexLookAndFeel final : public juce::LookAndFeel_V4
    {
    public:
        VexLookAndFeel()
            : juce::LookAndFeel_V4 (juce::LookAndFeel_V4::ColourScheme (
                  col::frameTop, col::panelTop, col::panelTop, col::edgeLit, col::text,
                  col::accent, juce::Colours::white, col::accent.withAlpha (0.35f), col::text))
        {
            setColour (juce::PopupMenu::backgroundColourId, col::panelTop);
            setColour (juce::PopupMenu::highlightedBackgroundColourId, col::accent.withAlpha (0.35f));
            setColour (juce::PopupMenu::textColourId, col::text);
            setColour (juce::PopupMenu::headerTextColourId, col::textDim);
            setColour (juce::TooltipWindow::backgroundColourId, col::well);
            setColour (juce::TooltipWindow::textColourId, col::text);
            setColour (juce::TooltipWindow::outlineColourId, col::edgeLit);
            setColour (juce::AlertWindow::backgroundColourId, col::panelTop);
            setColour (juce::AlertWindow::textColourId, col::text);
            setColour (juce::AlertWindow::outlineColourId, col::edgeLit);
            setColour (juce::TextButton::buttonColourId, col::panelBot);
            setColour (juce::TextButton::textColourOffId, col::text);
            setColour (juce::TextEditor::backgroundColourId, col::well);
            setColour (juce::TextEditor::outlineColourId, col::edgeLit);
            setColour (juce::TextEditor::focusedOutlineColourId, col::accent);
            setColour (juce::TextEditor::textColourId, col::text);
        }

        juce::Font getPopupMenuFont() override                 { return font (14.0f, Weight::regular); }
        juce::Font getTextButtonFont (juce::TextButton&, int) override { return font (14.0f, Weight::medium); }
        juce::Font getAlertWindowTitleFont() override          { return font (16.0f, Weight::semibold); }
        juce::Font getAlertWindowMessageFont() override        { return font (14.0f, Weight::regular); }
        juce::Font getAlertWindowFont() override               { return font (13.0f, Weight::regular); }

        juce::Rectangle<int> getTooltipBounds (const juce::String& text, juce::Point<int> pos, juce::Rectangle<int> parent) override
        {
            juce::TextLayout layout;
            layout.createLayout (tooltipText (text), 320.0f);
            const int w = (int) layout.getWidth() + 18, h = (int) layout.getHeight() + 12;
            return juce::Rectangle<int> (pos.x > parent.getCentreX() ? pos.x - (w + 12) : pos.x + 18,
                                         pos.y > parent.getCentreY() ? pos.y - (h + 6) : pos.y + 6, w, h)
                       .constrainedWithin (parent);
        }

        void drawCornerResizer (juce::Graphics& g, int w, int h, bool isMouseOver, bool isMouseDragging) override
        {
            // three short diagonal strokes in the frame colour, brighter while used
            const auto c = (isMouseOver || isMouseDragging) ? col::accentHi : col::edgeLit;
            g.setColour (c.withAlpha (0.8f));
            const float s = (float) std::min (w, h);
            for (int i = 1; i <= 3; ++i)
            {
                const float d = s * 0.28f * (float) i;
                g.drawLine ((float) w - d, (float) h - 3.0f, (float) w - 3.0f, (float) h - d, 1.2f);
            }
        }

        void drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height) override
        {
            const auto b = juce::Rectangle<float> ((float) width, (float) height);
            g.setColour (col::well);
            g.fillRoundedRectangle (b, 6.0f);
            g.setColour (col::edgeLit);
            g.drawRoundedRectangle (b.reduced (0.5f), 6.0f, 1.0f);
            juce::TextLayout layout;
            layout.createLayout (tooltipText (text), (float) width - 18.0f);
            layout.draw (g, b.reduced (9.0f, 6.0f));
        }

    private:
        static juce::AttributedString tooltipText (const juce::String& text)
        {
            juce::AttributedString s;
            s.setJustification (juce::Justification::topLeft);
            s.append (text, font (13.0f), col::text);
            return s;
        }
    };

    //==========================================================================
    /** A line of status text that repaints only when it changes. */
    class InfoText final : public juce::Component
    {
    public:
        explicit InfoText (juce::Justification j = juce::Justification::centredLeft, float size = 12.5f) : just (j), fontSize (size)
        {
            setInterceptsMouseClicks (false, false);
        }

        void set (const juce::String& t, juce::Colour c)
        {
            if (t != text || c != colour)
            {
                text = t;
                colour = c;
                repaint();
            }
        }

        void paint (juce::Graphics& g) override
        {
            drawText (g, text, getLocalBounds().toFloat(), fontSize, colour, just, Weight::medium);
        }

    private:
        juce::String text;
        juce::Colour colour { col::textDim };
        juce::Justification just;
        float fontSize;
    };

    //==========================================================================
    /** The phrase as the engine plans it: repeats, capture slices, live slices and the current step. */
    class StepStrip final : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        explicit StepStrip (vex::dsp::EngineStatus& s) : status (s)
        {
            setTooltip ("The phrase as the engine plays it: filled = repeat, outlined = captured slice, "
                        "dim = live voice. The bright step is the current one.");
        }

        void refresh()
        {
            const uint64_t r0 = status.repeatMask[0].load(), r1 = status.repeatMask[1].load();
            const uint64_t c0 = status.captureMask[0].load(), c1 = status.captureMask[1].load();
            const int n = status.steps.load(), s = status.step.load();
            const bool gate = status.gating.load(), fz = status.playingFrozen.load();
            if (r0 != rep[0] || r1 != rep[1] || c0 != cap[0] || c1 != cap[1] || n != steps || s != step || gate != gating || fz != frozen)
            {
                rep[0] = r0; rep[1] = r1; cap[0] = c0; cap[1] = c1; steps = n; step = s; gating = gate; frozen = fz;
                repaint();
            }
        }

        void paint (juce::Graphics& g) override
        {
            const int n = juce::jlimit (1, 128, steps);
            const auto b = getLocalBounds().toFloat();
            const float gap = n > 32 ? 1.0f : 2.0f;
            const float w = (b.getWidth() - gap * (float) (n - 1)) / (float) n;
            for (int k = 0; k < n; ++k)
            {
                const auto r = juce::Rectangle<float> (b.getX() + (float) k * (w + gap), b.getY(), w, b.getHeight());
                const bool isRep = ((rep[k >> 6] >> (k & 63)) & 1u) != 0;
                const bool isCap = ((cap[k >> 6] >> (k & 63)) & 1u) != 0;
                const bool now = k == step;
                const auto on = frozen ? col::ice : col::accent;
                if (isRep)
                {
                    g.setColour ((gating ? col::accent.withAlpha (0.55f) : on).withMultipliedAlpha (now ? 1.0f : 0.75f));
                    g.fillRoundedRectangle (r, 2.0f);
                }
                else if (isCap)
                {
                    g.setColour (col::cyan.withAlpha (now ? 1.0f : 0.8f));
                    g.drawRoundedRectangle (r.reduced (0.75f), 2.0f, 1.5f);
                }
                else
                {
                    g.setColour (col::edgeLit.withAlpha (now ? 0.9f : 0.45f));
                    g.fillRoundedRectangle (r, 2.0f);
                }
                if (now)
                {
                    g.setColour (juce::Colours::white.withAlpha (0.85f));
                    g.fillRoundedRectangle (r.withHeight (2.0f).translated (0.0f, b.getHeight() - 2.0f), 1.0f);
                }
            }
        }

    private:
        vex::dsp::EngineStatus& status;
        uint64_t rep[2] {}, cap[2] {};
        int steps = 0, step = -1;
        bool gating = false, frozen = false;
    };

    //==========================================================================
    /** Header preset control: < name > ; the name opens the preset browser. */
    class PresetBar final : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        PresetBar (vex::presets::PresetManager& pm) : presets (pm)
        {
            setTooltip ("Presets: arrows step through them, click the name to browse, save, rename or import.");
        }

        std::function<void()> onBrowse;

        void refresh()
        {
            const auto n = presets.getCurrentName();
            const bool m = presets.isModified();
            if (n != name || m != modified)
            {
                name = n;
                modified = m;
                repaint();
            }
        }

        void paint (juce::Graphics& g) override
        {
            const auto b = getLocalBounds().toFloat().reduced (0.5f);
            const float r = b.getHeight() * 0.5f;
            g.setGradientFill (juce::ColourGradient (col::panelTop.brighter (0.06f), b.getX(), b.getY(), col::well, b.getX(), b.getBottom(), false));
            g.fillRoundedRectangle (b, r);
            g.setColour (hover >= -1 ? col::edgeLit : col::edge.brighter (0.2f));
            g.drawRoundedRectangle (b, r, 1.0f);
            const float aw = 38.0f;
            for (int side : { -1, 1 })
            {
                const auto a = side < 0 ? b.withWidth (aw) : b.withTrimmedLeft (b.getWidth() - aw);
                icons::draw (g, side < 0 ? icons::Id::chevronLeft : icons::Id::chevronRight,
                             juce::Rectangle<float> (14.0f, 14.0f).withCentre (a.getCentre()), hover == side ? col::accentHi : col::text, 1.5f);
            }
            const auto text = name + (modified ? " *" : "");
            drawText (g, text, b.reduced (aw, 0.0f), 15.0f, hover == 0 ? col::accentHi : col::text, juce::Justification::centred, Weight::medium);
        }

        void mouseMove (const juce::MouseEvent& e) override
        {
            const int z = zone (e.position.x);
            if (z != hover) { hover = z; repaint(); }
        }

        void mouseExit (const juce::MouseEvent&) override { hover = -2; repaint(); }

        void mouseDown (const juce::MouseEvent& e) override
        {
            const int z = zone (e.position.x);
            if (z == -1) presets.loadPrevious();
            else if (z == 1) presets.loadNext();
            else if (onBrowse) onBrowse();
            refresh();
        }

    private:
        int zone (float x) const { return x < 38.0f ? -1 : (x > (float) getWidth() - 38.0f ? 1 : 0); }

        vex::presets::PresetManager& presets;
        juce::String name;
        bool modified = false;
        int hover = -2;
    };

    //==========================================================================
    void drawLogo (juce::Graphics& g, juce::Rectangle<float> area)
    {
        // "VEX" wordmark: geometric, drawn as paths (110 x 30 units)
        juce::Path p;
        p.startNewSubPath (0, 0); p.lineTo (9.5f, 0); p.lineTo (17.5f, 21); p.lineTo (25.5f, 0); p.lineTo (35, 0);
        p.lineTo (22, 30); p.lineTo (13, 30); p.closeSubPath();
        p.addRectangle (40, 0, 6.5f, 30);
        p.addRectangle (40, 0, 28, 6.5f);
        p.addRectangle (40, 11.75f, 24, 6.5f);
        p.addRectangle (40, 23.5f, 28, 6.5f);
        p.startNewSubPath (73, 0); p.lineTo (82.5f, 0); p.lineTo (110, 30); p.lineTo (100.5f, 30); p.closeSubPath();
        p.startNewSubPath (100.5f, 0); p.lineTo (110, 0); p.lineTo (82.5f, 30); p.lineTo (73, 30); p.closeSubPath();
        const auto t = p.getTransformToScaleToFit (area, true);
        const auto bounds = p.getBounds().transformedBy (t);
        g.setColour (col::accent.withAlpha (0.18f));
        g.strokePath (p, juce::PathStrokeType (4.0f), t);
        g.setGradientFill (juce::ColourGradient (juce::Colours::white, bounds.getX(), bounds.getY(), col::accentHi, bounds.getX(), bounds.getBottom(), false));
        g.fillPath (p, t);
    }

    void panelTitle (juce::Graphics& g, icons::Id icon, const juce::String& title, juce::Rectangle<float> row, float alpha = 1.0f)
    {
        icons::draw (g, icon, row.withWidth (row.getHeight()), col::accentHi.withMultipliedAlpha (alpha), 1.1f);
        drawTracked (g, title, row.withTrimmedLeft (row.getHeight() + 10.0f), 13.0f, col::text.withMultipliedAlpha (alpha * 0.92f),
                     juce::Justification::centredLeft, 0.06f, Weight::medium);
    }

    //==========================================================================
    /** Layout of the design canvas (1120 x 630), shared by the background and the controls. */
    namespace L
    {
        const juce::Rectangle<float> frame       { 6, 6, 1108, 618 };
        const juce::Rectangle<float> logo        { 36, 22, 104, 30 };
        const juce::Rectangle<float> preset      { 530, 20, 316, 36 };
        const juce::Rectangle<float> wavePanel   { 18, 70, 1084, 172 };
        const juce::Rectangle<float> modePanel   { 18, 254, 432, 180 };
        const juce::Rectangle<float> amount      { 460, 244, 200, 200 };
        const juce::Rectangle<float> lengthPanel { 672, 254, 244, 86 };
        const juce::Rectangle<float> stylePanel  { 672, 348, 244, 86 };
        const juce::Rectangle<float> mixPanel    { 926, 254, 176, 180 };
        const juce::Rectangle<float> glitchPanel { 18, 446, 280, 134 };
        const juce::Rectangle<float> ratePanel   { 308, 446, 262, 134 };
        const juce::Rectangle<float> shapePanel  { 580, 446, 250, 134 };
        const juce::Rectangle<float> fxPanel     { 840, 446, 262, 134 };
        const juce::Rectangle<float> footer      { 18, 588, 1084, 30 };
    }

    //==========================================================================
    /** Everything that does not move: frame, header, panels, titles. Cached as an image. */
    class Background final : public juce::Component
    {
    public:
        Background() { setInterceptsMouseClicks (false, false); setBufferedToImage (true); setOpaque (true); }

        bool glitchActive = true;

        void paint (juce::Graphics& g) override
        {
            g.fillAll (col::bg);

            // frame
            const auto f = L::frame;
            glow (g, f, 20.0f, col::accent, 12.0f, 2.6f);
            g.setGradientFill (juce::ColourGradient (col::frameTop, f.getX(), f.getY(), col::frameBot, f.getX(), f.getBottom(), false));
            g.fillRoundedRectangle (f, 20.0f);
            g.setGradientFill (juce::ColourGradient (col::accentHi.withAlpha (0.7f), f.getCentreX(), f.getY(), col::accent.withAlpha (0.25f), f.getX(), f.getCentreY(), true));
            g.drawRoundedRectangle (f.reduced (0.75f), 20.0f, 1.5f);

            // header
            drawLogo (g, L::logo);
            g.setColour (col::edgeLit);
            g.fillRect (juce::Rectangle<float> (160.0f, 22.0f, 1.0f, 32.0f));
            drawTracked (g, "STUTTER", { 182, 20, 260, 22 }, 20.0f, col::text, juce::Justification::centredLeft, 0.55f, Weight::regular);
            drawTracked (g, "VOCAL EFFECT", { 183, 42, 260, 14 }, 10.5f, col::textDim, juce::Justification::centredLeft, 0.42f, Weight::medium);

            // waveform panel
            drawPanel (g, L::wavePanel, 14.0f);
            drawTracked (g, "IN", { 26, 78, 30, 14 }, 11.5f, col::textDim, juce::Justification::centredLeft, 0.08f);
            drawTracked (g, "OUT", { 1064, 78, 30, 14 }, 11.5f, col::textDim, juce::Justification::centredRight, 0.08f);

            // middle row
            drawPanel (g, L::modePanel);
            drawTracked (g, "MODE SELECT", L::modePanel.withHeight (34.0f).translated (0, 6), 13.0f, col::text.withAlpha (0.92f),
                         juce::Justification::centred, 0.06f, Weight::medium);
            drawPanel (g, L::lengthPanel);
            panelTitle (g, icons::Id::clock, "LENGTH", { L::lengthPanel.getX() + 16, L::lengthPanel.getY() + 12, 160, 20 });
            drawPanel (g, L::stylePanel);
            panelTitle (g, icons::Id::sparkle, "STYLE", { L::stylePanel.getX() + 16, L::stylePanel.getY() + 12, 160, 20 });
            drawPanel (g, L::mixPanel);
            drawTracked (g, "MIX", L::mixPanel.withHeight (34.0f).translated (0, 6), 13.0f, col::text.withAlpha (0.92f),
                         juce::Justification::centred, 0.06f, Weight::medium);

            // bottom row
            const float ga = glitchActive ? 1.0f : 0.45f;
            drawPanel (g, L::glitchPanel, dims::panelCorner, glitchActive ? 1.0f : 0.6f);
            panelTitle (g, icons::Id::glitchType, "GLITCH TYPE", { L::glitchPanel.getX() + 16, L::glitchPanel.getY() + 12, 200, 20 }, ga);
            panelTitle (g, icons::Id::intensity, "INTENSITY", { L::glitchPanel.getX() + 16, L::glitchPanel.getY() + 80, 200, 18 }, ga);
            drawPanel (g, L::ratePanel);
            panelTitle (g, icons::Id::note, "RATE", { L::ratePanel.getX() + 16, L::ratePanel.getY() + 12, 120, 20 });
            panelTitle (g, icons::Id::waves, "SMOOTHNESS", { L::ratePanel.getX() + 16, L::ratePanel.getY() + 80, 200, 18 });
            drawPanel (g, L::shapePanel);
            panelTitle (g, icons::Id::shape, "SHAPE", { L::shapePanel.getX() + 16, L::shapePanel.getY() + 12, 160, 20 });
            panelTitle (g, icons::Id::shuffle, "OFFSET", { L::shapePanel.getX() + 16, L::shapePanel.getY() + 80, 160, 18 });
            drawPanel (g, L::fxPanel);
            panelTitle (g, icons::Id::layers, "FX CHAIN", { L::fxPanel.getX() + 16, L::fxPanel.getY() + 12, 150, 20 });
            drawTracked (g, "REVERB", { L::fxPanel.getX() + 20, L::fxPanel.getY() + 42, 100, 14 }, 11.5f, col::textDim, juce::Justification::centred, 0.06f);
            drawTracked (g, "DELAY", { L::fxPanel.getRight() - 120, L::fxPanel.getY() + 42, 100, 14 }, 11.5f, col::textDim, juce::Justification::centred, 0.06f);

            // footer
            icons::draw (g, icons::Id::logoWave, { L::footer.getX() + 8, L::footer.getY() + 3, 26, 24 }, col::accentHi, 1.2f);
            drawTracked (g, juce::String ("VEX STUTTER v") + VEX_VERSION_STRING, { L::footer.getX() + 46, L::footer.getY(), 260, L::footer.getHeight() },
                         12.5f, col::textDim, juce::Justification::centredLeft, 0.06f, Weight::medium);
            g.setGradientFill (juce::ColourGradient (col::accentHi.withAlpha (0.5f), 560.0f, 0.0f, col::accent.withAlpha (0.0f), 330.0f, 0.0f, true));
            g.fillRect (juce::Rectangle<float> (330.0f, L::frame.getBottom() - 2.5f, 460.0f, 1.5f));
        }
    };

    //==========================================================================
    /** FX details: delay (sync / rate or time, feedback, mix) and reverb (size, damping, mix). */
    class FxDetails final : public juce::Component
    {
    public:
        FxDetails (juce::AudioProcessorValueTreeState& s)
            : apvts (s),
              delaySync (param (ids::delaySync), "HOST SYNC", "FREE TIME", col::accent),
              delayRate (param (ids::delayRate)),
              delayTime (param (ids::delayTimeMs), { 1, 5, 10, 20, 30, 50, 75, 100, 125, 150, 200, 250, 300, 375, 400, 500, 600, 750, 1000, 1250, 1500, 2000 }),
              feedback (param (ids::delayFeedback), Knob::Size::mini), delayMix (param (ids::delayMix), Knob::Size::mini),
              size (param (ids::reverbSize), Knob::Size::mini), damping (param (ids::reverbDamping), Knob::Size::mini),
              reverbMix (param (ids::reverbMix), Knob::Size::mini),
              feedbackText (param (ids::delayFeedback), 13.0f, col::cyan, juce::Justification::centred),
              delayMixText (param (ids::delayMix), 13.0f, col::cyan, juce::Justification::centred),
              sizeText (param (ids::reverbSize), 13.0f, col::cyan, juce::Justification::centred),
              dampingText (param (ids::reverbDamping), 13.0f, col::cyan, juce::Justification::centred),
              reverbMixText (param (ids::reverbMix), 13.0f, col::cyan, juce::Justification::centred),
              close ("Close", icons::Id::close, 0.5f),
              syncAttachment (param (ids::delaySync), [this] (float) { updateSync(); }, nullptr)
        {
            for (juce::Component* c : std::initializer_list<juce::Component*> { &delaySync, &delayRate, &delayTime, &feedback, &delayMix, &size, &damping,
                                                                                &reverbMix, &feedbackText, &delayMixText, &sizeText, &dampingText,
                                                                                &reverbMixText, &close })
                addAndMakeVisible (c);
            delayTime.formatter = [] (float v) { return formatMs (v); };
            delaySync.setTooltip ("Delay time locked to the host tempo (musical division) or set in milliseconds.");
            feedback.setTooltip ("Delay feedback (capped at 85 %; the repeats darken and soft-clip, so it cannot run away).");
            size.setTooltip ("Reverb size: decay time.");
            damping.setTooltip ("Reverb damping: how fast the high frequencies decay.");
            close.onClick = [this] { setVisible (false); };
            syncAttachment.sendInitialUpdate();
        }

        void paint (juce::Graphics& g) override
        {
            const auto b = getLocalBounds().toFloat();
            g.setColour (juce::Colours::black.withAlpha (0.55f));
            g.fillRoundedRectangle (b, dims::panelCorner);
            const auto p = b.reduced (0.0f);
            glow (g, p, dims::panelCorner, col::accent, 6.0f, 1.2f);
            drawPanel (g, p);
            g.setColour (col::edgeLit);
            g.drawRoundedRectangle (p.reduced (0.5f), dims::panelCorner, 1.0f);
            panelTitle (g, icons::Id::layers, "FX CHAIN  -  DELAY", { 16, 12, 260, 20 });
            panelTitle (g, icons::Id::sparkle, "REVERB", { delaySplit + 20.0f, 12, 200, 20 });
            g.setColour (col::edge);
            g.fillRect (juce::Rectangle<float> (delaySplit, 14.0f, 1.0f, b.getHeight() - 28.0f));
            drawTracked (g, "FEEDBACK", { 300, 40, 90, 14 }, 11.0f, col::textDim, juce::Justification::centred, 0.06f);
            drawTracked (g, "DELAY MIX", { 400, 40, 90, 14 }, 11.0f, col::textDim, juce::Justification::centred, 0.06f);
            drawTracked (g, "SIZE", { delaySplit + 30.0f, 40, 90, 14 }, 11.0f, col::textDim, juce::Justification::centred, 0.06f);
            drawTracked (g, "DAMPING", { delaySplit + 140.0f, 40, 90, 14 }, 11.0f, col::textDim, juce::Justification::centred, 0.06f);
            drawTracked (g, "REVERB MIX", { delaySplit + 250.0f, 40, 90, 14 }, 11.0f, col::textDim, juce::Justification::centred, 0.06f);
            drawText (g, "Effect signal only: the live voice stays dry.", { 18, b.getHeight() - 22.0f, 270, 16 }, 11.5f, col::textFaint,
                      juce::Justification::centredLeft);
        }

        void resized() override
        {
            delaySplit = 520.0f;
            delaySync.setBounds (16, 44, 128, 26);
            delayRate.setBounds (16, 78, 268, 28);
            delayTime.setBounds (16, 78, 268, 28);
            feedback.setBounds (318, 58, 54, 54);
            feedbackText.setBounds (300, 112, 90, 16);
            delayMix.setBounds (418, 58, 54, 54);
            delayMixText.setBounds (400, 112, 90, 16);
            const int x = (int) delaySplit;
            size.setBounds (x + 48, 58, 54, 54);
            sizeText.setBounds (x + 30, 112, 90, 16);
            damping.setBounds (x + 158, 58, 54, 54);
            dampingText.setBounds (x + 140, 112, 90, 16);
            reverbMix.setBounds (x + 268, 58, 54, 54);
            reverbMixText.setBounds (x + 250, 112, 90, 16);
            close.setBounds (getWidth() - 40, 8, 30, 30);
        }

    private:
        juce::RangedAudioParameter& param (const char* id) { return *apvts.getParameter (id); }

        void updateSync()
        {
            const bool sync = param (ids::delaySync).getValue() > 0.5f;
            delayRate.setVisible (sync);
            delayTime.setVisible (! sync);
        }

        juce::AudioProcessorValueTreeState& apvts;
        PillToggle delaySync;
        Selector delayRate, delayTime;
        Knob feedback, delayMix, size, damping, reverbMix;
        ValueText feedbackText, delayMixText, sizeText, dampingText, reverbMixText;
        IconButton close;
        juce::ParameterAttachment syncAttachment;
        float delaySplit = 520.0f;
    };
}

//==============================================================================
class VEXStutterAudioProcessorEditor::Content final : public juce::Component
{
public:
    explicit Content (VEXStutterAudioProcessor& p)
        : processor (p), apvts (p.getState()),
          presetBar (p.getPresetManager()),
          saveButton ("Save preset", icons::Id::save), browseButton ("Open preset", icons::Id::folder), settingsButton ("Settings", icons::Id::gear),
          waveform (p),
          freeze (param (ids::freeze), "FROZEN", "FREEZE", col::ice, icons::Id::snowflake, true),
          steps (p.getEngineStatus()),
          modes (param (ids::mode)),
          amount (param (ids::amount), Knob::Size::hero, "AMOUNT"),
          length (param (ids::length)), lengthText (param (ids::length), 14.0f),
          style (param (ids::style)),
          mix (param (ids::mix), Knob::Size::medium), mixText (param (ids::mix), 17.0f, col::cyan, juce::Justification::centred),
          glitchType (param (ids::glitchType)), intensity (param (ids::intensity)), intensityText (param (ids::intensity), 14.0f),
          rate (param (ids::rate)),
          rateMs (param (ids::rateMs), { 10, 15, 20, 25, 30, 40, 50, 60, 75, 90, 100, 125, 150, 175, 200, 250, 300, 350, 400, 500, 600, 750,
                                         1000, 1250, 1500, 2000 }),
          sync (param (ids::sync), "HOST", "FREE", col::accent),
          smoothness (param (ids::smoothness)), smoothnessText (param (ids::smoothness), 14.0f),
          shape (param (ids::shape)), offset (param (ids::offsetMs), true), offsetText (param (ids::offsetMs), 14.0f, col::text, juce::Justification::centredRight),
          fxSwitch (param (ids::fxEnabled)), fxEdit ("FX details", icons::Id::sliders, 0.5f),
          reverbMix (param (ids::reverbMix), Knob::Size::mini), delayMix (param (ids::delayMix), Knob::Size::mini),
          reverbText (param (ids::reverbMix), 13.5f, col::text, juce::Justification::centred),
          delayText (param (ids::delayMix), 13.5f, col::text, juce::Justification::centred),
          fxDetails (p.getState()),
          bypass (param (ids::bypass), "BYPASSED", "BYPASS", col::warn),
          power (param (ids::bypass)),
          captureInfo (juce::Justification::centredLeft, 12.0f), syncInfo (juce::Justification::centredRight, 12.0f),
          footerInfo (juce::Justification::centred, 12.0f),
          modeAttachment (param (ids::mode), [this] (float) { updateModeDependent(); }, nullptr),
          syncAttachment (param (ids::sync), [this] (float) { updateModeDependent(); }, nullptr)
    {
        addAndMakeVisible (background);
        for (juce::Component* c : std::initializer_list<juce::Component*> {
                 &presetBar, &saveButton, &browseButton, &settingsButton, &waveform, &inMeter, &outMeter, &freeze, &steps,
                 &captureInfo, &syncInfo, &modes, &amount, &length, &lengthText, &style, &mix, &mixText, &glitchType, &intensity,
                 &intensityText, &rate, &rateMs, &sync, &smoothness, &smoothnessText, &shape, &offset, &offsetText, &fxSwitch,
                 &fxEdit, &reverbMix, &delayMix, &reverbText, &delayText, &footerInfo, &bypass, &power })
            addAndMakeVisible (c);
        addChildComponent (fxDetails);

        rateMs.formatter = [] (float v) { return formatMs (v); };

        // tooltips (specification section 14: non-obvious controls explain themselves)
        amount.setTooltip ("Amount: how many slices become repeats (Gate: depth). 0 % is the clean input, bit for bit.");
        length.setTooltip ("Length: the part of each phrase the effect covers (Gate: the open part of each slice).");
        style.setTooltip ("Style: Sharp, Smooth, Tight, Loose, Tape, Random or Digital Glitch character of the repeats.");
        mix.setTooltip ("Mix: dry / wet, linear. 0 % is the input bit for bit.");
        glitchType.setTooltip ("Glitch Type (Glitch mode): Pixel retriggers a micro-slice, Burst fires a burst of tiny repeats, Shuffle reorders micro-slices.");
        intensity.setTooltip ("Intensity (Glitch mode): how many repeats are varied and how strongly.");
        rate.setTooltip ("Rate: slice length as a musical division of the host tempo.");
        rateMs.setTooltip ("Rate (Free): slice length in milliseconds, independent of the host.");
        sync.setTooltip ("HOST: the grid follows the DAW tempo and bar (an internal clock keeps time while the transport is stopped). FREE: Rate in ms.");
        smoothness.setTooltip ("Smoothness: fade and crossfade length, 0.5 ms (0 %) to 40 ms; never more than half a slice.");
        shape.setTooltip ("Shape: the envelope of every repeat and gate opening.");
        offset.setTooltip ("Offset: shifts the slice grid later (+) or earlier (-), up to 100 ms.");
        fxSwitch.setTooltip ("FX Chain on / off: delay and reverb on the repeats only.");
        fxEdit.setTooltip ("Delay and reverb settings");
        reverbMix.setTooltip ("Reverb amount on the repeats.");
        delayMix.setTooltip ("Delay amount on the repeats.");
        freeze.setTooltip ("Freeze: lock the captured audio; the repeats then play the blue selection (drag its edges). "
                           "Needs 1 s of audio. Gate mode always gates the live voice.");
        bypass.setTooltip ("Bypass: 20 ms crossfade to the dry input. The same state as the power button and the host's bypass.");
        saveButton.setTooltip ("Save the current settings as a user preset");
        browseButton.setTooltip ("Browse, import and manage presets");
        settingsButton.setTooltip ("Settings: interface size, preset folder, about");
        inMeter.setTooltip ("Input peak level (dBFS). Click to clear the clip indicator.");
        outMeter.setTooltip ("Output peak level (dBFS). Click to clear the clip indicator.");

        presetBar.onBrowse = [this] { showPresetMenu (presetBar); };
        browseButton.onClick = [this] { showPresetMenu (browseButton); };
        saveButton.onClick = [this] { askSaveAs(); };
        settingsButton.onClick = [this] { showSettingsMenu(); };
        fxEdit.onClick = [this] { fxDetails.setVisible (! fxDetails.isVisible()); if (fxDetails.isVisible()) fxDetails.toFront (false); };

        modeAttachment.sendInitialUpdate();
        setSize (dims::width, dims::height);
    }

    juce::RangedAudioParameter& param (const char* id) { return *apvts.getParameter (id); }

    void resized() override
    {
        background.setBounds (getLocalBounds());
        auto at = [] (juce::Component& c, float x, float y, float w, float h) { c.setBounds (juce::Rectangle<float> (x, y, w, h).toNearestInt()); };

        // header
        at (presetBar, L::preset.getX(), L::preset.getY(), L::preset.getWidth(), L::preset.getHeight());
        at (saveButton, 966, 18, 40, 40);
        at (browseButton, 1014, 18, 40, 40);
        at (settingsButton, 1062, 18, 40, 40);

        // waveform panel
        const auto w = L::wavePanel;
        at (inMeter, w.getX() + 11, w.getY() + 28, 9, 110);
        at (outMeter, w.getRight() - 20, w.getY() + 28, 9, 110);
        at (waveform, w.getX() + 40, w.getY() + 6, w.getWidth() - 80, 134);
        at (freeze, w.getX() + 40, w.getBottom() - 30, 112, 24);
        at (captureInfo, w.getX() + 160, w.getBottom() - 30, 190, 24);
        at (steps, w.getX() + 362, w.getBottom() - 23, 360, 10);
        at (syncInfo, w.getRight() - 340, w.getBottom() - 30, 300, 24);

        // middle row
        at (modes, L::modePanel.getX() + 16, L::modePanel.getY() + 50, L::modePanel.getWidth() - 32, 108);
        at (amount, L::amount.getX(), L::amount.getY(), L::amount.getWidth(), L::amount.getHeight());
        at (lengthText, L::lengthPanel.getRight() - 96, L::lengthPanel.getY() + 12, 80, 20);
        at (length, L::lengthPanel.getX() + 12, L::lengthPanel.getY() + 46, L::lengthPanel.getWidth() - 24, 26);
        at (style, L::stylePanel.getX() + 14, L::stylePanel.getY() + 42, L::stylePanel.getWidth() - 28, 32);
        at (mix, L::mixPanel.getCentreX() - 60, L::mixPanel.getY() + 36, 120, 120);
        at (mixText, L::mixPanel.getX(), L::mixPanel.getBottom() - 28, L::mixPanel.getWidth(), 20);

        // bottom row
        const auto gp = L::glitchPanel;
        at (glitchType, gp.getX() + 14, gp.getY() + 40, gp.getWidth() - 28, 30);
        at (intensity, gp.getX() + 10, gp.getY() + 102, gp.getWidth() - 86, 24);
        at (intensityText, gp.getRight() - 76, gp.getY() + 103, 62, 22);
        const auto rp = L::ratePanel;
        at (sync, rp.getRight() - 84, rp.getY() + 10, 70, 24);
        at (rate, rp.getX() + 14, rp.getY() + 40, rp.getWidth() - 28, 30);
        at (rateMs, rp.getX() + 14, rp.getY() + 40, rp.getWidth() - 28, 30);
        at (smoothness, rp.getX() + 10, rp.getY() + 102, rp.getWidth() - 86, 24);
        at (smoothnessText, rp.getRight() - 76, rp.getY() + 103, 62, 22);
        const auto sp = L::shapePanel;
        at (shape, sp.getX() + 14, sp.getY() + 40, sp.getWidth() - 28, 30);
        at (offset, sp.getX() + 10, sp.getY() + 102, sp.getWidth() - 96, 24);
        at (offsetText, sp.getRight() - 86, sp.getY() + 103, 72, 22);
        const auto fp = L::fxPanel;
        at (fxSwitch, fp.getRight() - 62, fp.getY() + 10, 48, 24);
        at (fxEdit, fp.getRight() - 98, fp.getY() + 8, 28, 28);
        at (reverbMix, fp.getX() + 46, fp.getY() + 60, 48, 48);
        at (reverbText, fp.getX() + 20, fp.getY() + 108, 100, 18);
        at (delayMix, fp.getRight() - 94, fp.getY() + 60, 48, 48);
        at (delayText, fp.getRight() - 120, fp.getY() + 108, 100, 18);
        at (fxDetails, L::glitchPanel.getX(), L::glitchPanel.getY(), L::fxPanel.getRight() - L::glitchPanel.getX(), L::glitchPanel.getHeight());

        // footer
        at (footerInfo, 320, L::footer.getY(), 480, L::footer.getHeight());
        at (bypass, 934, L::footer.getY() + 1, 112, 28);
        at (power, 1056, L::footer.getY() - 2, 36, 36);
    }

    void tick (double dt)
    {
        auto& st = processor.getEngineStatus();
        if (inMeter.update (std::max (vex::dsp::EngineStatus::take (st.inPeak[0]), vex::dsp::EngineStatus::take (st.inPeak[1])), dt))
            inMeter.repaint();
        if (outMeter.update (std::max (vex::dsp::EngineStatus::take (st.outPeak[0]), vex::dsp::EngineStatus::take (st.outPeak[1])), dt))
            outMeter.repaint();
        waveform.refresh();
        steps.refresh();

        // capture state
        const bool freezeOn = param (ids::freeze).getValue() > 0.5f;
        const bool frozenNow = processor.getCapture().uiFrozen();
        const bool gateMode = juce::roundToInt (param (ids::mode).convertFrom0to1 (param (ids::mode).getValue())) == (int) vex::choices::Mode::gate;
        const bool pending = freezeOn && ! frozenNow;
        if (pending != freeze.pending) { freeze.pending = pending; freeze.repaint(); }
        if (frozenNow)
        {
            const auto cols = processor.getCapture().uiFrozenColumnEnd() - processor.getCapture().uiFrozenColumnStart();
            captureInfo.set (juce::String ((double) cols / vex::dsp::CaptureBuffer::kColumnsPerSecond, 1) + " s captured"
                             + (gateMode ? "  (Gate gates the live voice)" : ""), col::ice);
        }
        else
            captureInfo.set (pending ? "capturing 1 s ..." : "LIVE INPUT", pending ? col::ice : col::textDim);

        // timing
        const int s = st.syncState.load();
        const float bpm = st.bpm.load();
        const float sliceMs = st.sliceSeconds.load() * 1000.0f;
        const auto sliceText = formatMs (sliceMs);
        if (s == vex::dsp::EngineStatus::hostLocked)
            syncInfo.set ("HOST  " + juce::String (bpm, 1) + " BPM   slice " + sliceText, col::accentHi);
        else if (s == vex::dsp::EngineStatus::hostStopped)
            syncInfo.set ("INTERNAL CLOCK  " + juce::String (bpm, 1) + " BPM   slice " + sliceText, col::textDim);
        else
            syncInfo.set ("FREE TIME   slice " + sliceText, col::textDim);

        if (++slowTick % 6 == 0)          // ~5 Hz is plenty for the preset name
            presetBar.refresh();

        const bool bypassed = param (ids::bypass).getValue() > 0.5f;
        footerInfo.set (bypassed ? "Bypassed: dry signal (capture and timing keep running)" : "", col::warn);
    }

    void showFx (bool show)
    {
        fxDetails.setVisible (show);
        if (show)
            fxDetails.toFront (false);
    }

    std::function<void (float)> onScaleChosen;

private:
    void updateModeDependent()
    {
        const int mode = juce::roundToInt (param (ids::mode).convertFrom0to1 (param (ids::mode).getValue()));
        const bool glitchMode = mode == (int) vex::choices::Mode::glitch;
        for (juce::Component* c : std::initializer_list<juce::Component*> { &glitchType, &intensity, &intensityText })
            c->setAlpha (glitchMode ? 1.0f : 0.5f);
        if (background.glitchActive != glitchMode)
        {
            background.glitchActive = glitchMode;
            background.repaint();
        }
        freeze.setAlpha (mode == (int) vex::choices::Mode::gate ? 0.55f : 1.0f);

        const bool hostSync = param (ids::sync).getValue() > 0.5f;
        rate.setVisible (hostSync);
        rateMs.setVisible (! hostSync);
    }

    //---------------------------------------------------------------- presets --
    void showPresetMenu (juce::Component& target)
    {
        auto& pm = processor.getPresetManager();
        pm.rescan();
        const auto& list = pm.getPresets();
        const int current = pm.getCurrentIndex();

        juce::PopupMenu menu;
        menu.addSectionHeader ("Factory");
        juce::StringArray categories;
        for (const auto& p : list)
            if (p.factory)
                categories.addIfNotAlreadyThere (p.category);
        for (const auto& cat : categories)
        {
            juce::PopupMenu sub;
            bool containsCurrent = false;
            for (int i = 0; i < (int) list.size(); ++i)
                if (list[(size_t) i].factory && list[(size_t) i].category == cat)
                {
                    sub.addItem (list[(size_t) i].name, true, i == current, [this, i] { processor.getPresetManager().load (i); });
                    containsCurrent = containsCurrent || i == current;
                }
            menu.addSubMenu (cat, sub, true, nullptr, containsCurrent);
        }
        menu.addSectionHeader ("User");
        bool anyUser = false;
        for (int i = 0; i < (int) list.size(); ++i)
            if (! list[(size_t) i].factory)
            {
                anyUser = true;
                menu.addItem (list[(size_t) i].name, true, i == current, [this, i] { processor.getPresetManager().load (i); });
            }
        if (! anyUser)
            menu.addItem ("(no user presets yet)", false, false, nullptr);

        menu.addSeparator();
        menu.addItem ("Save As...", [this] { askSaveAs(); });
        const bool userCurrent = current >= 0 && ! list[(size_t) current].factory;
        menu.addItem ("Rename...", userCurrent, false, [this, current] { askRename (current); });
        menu.addItem ("Delete", userCurrent, false, [this, current] { askDelete (current); });
        menu.addItem ("Import Preset File...", [this] { importPreset(); });
        menu.addItem ("Show Preset Folder", [this]
        {
            const auto dir = processor.getPresetManager().getUserDirectory();
            dir.createDirectory();
            dir.revealToUser();
        });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target).withMinimumWidth (240));
    }

    void showMessage (const juce::String& title, const juce::String& message)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, title, message, "OK", this);
    }

    void askSaveAs()
    {
        auto& pm = processor.getPresetManager();
        auto* w = new juce::AlertWindow ("Save Preset", "Name of the new user preset:", juce::MessageBoxIconType::NoIcon, this);
        const auto suggestion = pm.isModified() || pm.getCurrentName() == "Init" ? juce::String ("My Preset") : pm.getCurrentName();
        w->addTextEditor ("name", suggestion);
        w->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        juce::Component::SafePointer<Content> safe (this);
        w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w] (int result)
        {
            if (result != 1 || safe == nullptr)
                return;
            const auto name = vex::presets::PresetManager::cleanName (w->getTextEditorContents ("name"));
            safe->saveNamed (name);
        }), true);
    }

    void saveNamed (const juce::String& name)
    {
        auto& pm = processor.getPresetManager();
        if (name.isEmpty())
        {
            showMessage ("Save Preset", "Please enter a name.");
            return;
        }
        if (pm.userPresetExists (name))
        {
            juce::Component::SafePointer<Content> safe (this);
            juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "Replace Preset",
                                                "A user preset called \"" + name + "\" already exists. Replace it?", "Replace", "Cancel", this,
                                                juce::ModalCallbackFunction::create ([safe, name] (int ok)
                                                {
                                                    if (ok == 1 && safe != nullptr)
                                                    {
                                                        const auto r = safe->processor.getPresetManager().saveAs (name, true);
                                                        if (r.failed()) safe->showMessage ("Save Preset", r.getErrorMessage());
                                                    }
                                                }));
            return;
        }
        const auto r = pm.saveAs (name, false);
        if (r.failed())
            showMessage ("Save Preset", r.getErrorMessage());
    }

    void askRename (int index)
    {
        auto& pm = processor.getPresetManager();
        const auto& list = pm.getPresets();
        if (index < 0 || index >= (int) list.size())
            return;
        auto* w = new juce::AlertWindow ("Rename Preset", "New name:", juce::MessageBoxIconType::NoIcon, this);
        w->addTextEditor ("name", list[(size_t) index].name);
        w->addButton ("Rename", 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        juce::Component::SafePointer<Content> safe (this);
        w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w, index] (int result)
        {
            if (result != 1 || safe == nullptr)
                return;
            const auto r = safe->processor.getPresetManager().rename (index, w->getTextEditorContents ("name"));
            if (r.failed())
                safe->showMessage ("Rename Preset", r.getErrorMessage());
        }), true);
    }

    void askDelete (int index)
    {
        auto& pm = processor.getPresetManager();
        const auto& list = pm.getPresets();
        if (index < 0 || index >= (int) list.size())
            return;
        juce::Component::SafePointer<Content> safe (this);
        juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "Delete Preset",
                                            "Delete the user preset \"" + list[(size_t) index].name + "\"? This cannot be undone.", "Delete", "Cancel", this,
                                            juce::ModalCallbackFunction::create ([safe, index] (int ok)
                                            {
                                                if (ok == 1 && safe != nullptr)
                                                {
                                                    const auto r = safe->processor.getPresetManager().remove (index);
                                                    if (r.failed()) safe->showMessage ("Delete Preset", r.getErrorMessage());
                                                }
                                            }));
    }

    void importPreset()
    {
        chooser = std::make_unique<juce::FileChooser> ("Import a VEX Stutter preset", juce::File(),
                                                       juce::String ("*") + vex::presets::PresetManager::kFileExtension);
        juce::Component::SafePointer<Content> safe (this);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [safe] (const juce::FileChooser& fc)
                              {
                                  if (safe == nullptr || fc.getResult() == juce::File())
                                      return;
                                  const auto r = safe->processor.getPresetManager().importFile (fc.getResult());
                                  if (r.failed())
                                      safe->showMessage ("Import Preset", r.getErrorMessage());
                              });
    }

    void showSettingsMenu()
    {
        juce::PopupMenu menu;
        juce::PopupMenu size;
        const float current = processor.getUiScale();
        for (float s : { 0.7f, 0.85f, 1.0f, 1.25f, 1.5f })
            size.addItem (juce::String (juce::roundToInt (s * 100.0f)) + " %", true, std::abs (current - s) < 0.01f,
                          [this, s] { if (onScaleChosen) onScaleChosen (s); });
        menu.addSubMenu ("Interface Size", size);
        menu.addItem ("Show Preset Folder", [this]
        {
            const auto dir = processor.getPresetManager().getUserDirectory();
            dir.createDirectory();
            dir.revealToUser();
        });
        menu.addSeparator();
        menu.addItem ("About VEX Stutter", [this]
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "VEX Stutter",
                                                    juce::String ("Version ") + VEX_VERSION_STRING + "\nVocal stutter / repeat / gate / glitch effect.\n"
                                                    "Interface font: Inter (SIL Open Font License 1.1).", "OK", this);
        });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&settingsButton));
    }

    VEXStutterAudioProcessor& processor;
    juce::AudioProcessorValueTreeState& apvts;

    Background background;
    PresetBar presetBar;
    IconButton saveButton, browseButton, settingsButton;
    WaveformView waveform;
    LevelMeter inMeter, outMeter;
    PillToggle freeze;
    StepStrip steps;
    ModeSelector modes;
    Knob amount;
    HSlider length;
    ValueText lengthText;
    Selector style;
    Knob mix;
    ValueText mixText;
    Selector glitchType;
    HSlider intensity;
    ValueText intensityText;
    Selector rate, rateMs;
    PillToggle sync;
    HSlider smoothness;
    ValueText smoothnessText;
    Selector shape;
    HSlider offset;
    ValueText offsetText;
    Switch fxSwitch;
    IconButton fxEdit;
    Knob reverbMix, delayMix;
    ValueText reverbText, delayText;
    FxDetails fxDetails;
    PillToggle bypass;
    PowerButton power;
    InfoText captureInfo, syncInfo, footerInfo;
    juce::ParameterAttachment modeAttachment, syncAttachment;
    std::unique_ptr<juce::FileChooser> chooser;
    int slowTick = 0;
};

//==============================================================================
VEXStutterAudioProcessorEditor::VEXStutterAudioProcessorEditor (VEXStutterAudioProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p)
{
    lookAndFeel = std::make_unique<VexLookAndFeel>();
    setLookAndFeel (lookAndFeel.get());
    content = std::make_unique<Content> (p);
    content->onScaleChosen = [this] (float s) { setUiScale (s); };
    addAndMakeVisible (*content);
    tooltips = std::make_unique<juce::TooltipWindow> (this, 700);

    setResizable (true, true);
    if (auto* c = getConstrainer())
    {
        c->setFixedAspectRatio ((double) kWidth / (double) kHeight);
        c->setSizeLimits (juce::roundToInt (kWidth * VEXStutterAudioProcessor::kMinUiScale), juce::roundToInt (kHeight * VEXStutterAudioProcessor::kMinUiScale),
                          juce::roundToInt (kWidth * VEXStutterAudioProcessor::kMaxUiScale), juce::roundToInt (kHeight * VEXStutterAudioProcessor::kMaxUiScale));
    }
    const float s = p.getUiScale();
    setSize (juce::roundToInt (kWidth * s), juce::roundToInt (kHeight * s));
    lastTick = juce::Time::getMillisecondCounterHiRes();
    startTimerHz (30);
}

VEXStutterAudioProcessorEditor::~VEXStutterAudioProcessorEditor()
{
    stopTimer();
    tooltips.reset();
    content.reset();
    setLookAndFeel (nullptr);
}

void VEXStutterAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (col::bg);
}

void VEXStutterAudioProcessorEditor::resized()
{
    const float scale = (float) getWidth() / (float) kWidth;
    content->setTransform (juce::AffineTransform::scale (scale));
    content->setBounds (0, 0, kWidth, kHeight);
    processorRef.setUiScale (scale);
}

void VEXStutterAudioProcessorEditor::setUiScale (float s)
{
    s = juce::jlimit (VEXStutterAudioProcessor::kMinUiScale, VEXStutterAudioProcessor::kMaxUiScale, s);
    setSize (juce::roundToInt (kWidth * s), juce::roundToInt (kHeight * s));
}

void VEXStutterAudioProcessorEditor::setFxDetailsVisible (bool show)
{
    content->showFx (show);
}

void VEXStutterAudioProcessorEditor::refreshNow()
{
    content->tick (1.0 / 30.0);
}

void VEXStutterAudioProcessorEditor::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    const double dt = juce::jlimit (0.0, 0.25, (now - lastTick) * 0.001);
    lastTick = now;
    content->tick (dt);
}
