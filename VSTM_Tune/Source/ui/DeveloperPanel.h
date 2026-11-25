#pragma once
//==============================================================================
//  Developer panel (1.4): shown only while an admin authorization is valid (Authorization.h).
//  - live engine diagnostics (read-only)
//  - expert engine switches (VSTMTuneProcessor::Expert): session only, never saved, all on by default;
//    the processor ignores them without a valid authorization
//  Nothing here touches files, the host or the operating system.
//==============================================================================
#include "../PluginProcessor.h"
#include "../Lang.h"
#include "Theme.h"

namespace vui
{
class DeveloperPanel : public juce::Component
{
public:
    explicit DeveloperPanel (VSTMTuneProcessor& p) : audio (p)
    {
        using vstm::i18n::tr;
        const char* names[] = { "dev.peakguard", "dev.backdate", "dev.fresh", "dev.swingkeep" };
        for (int i = 0; i < (int) VSTMTuneProcessor::Expert::count; ++i)
        {
            auto& b = toggles[(size_t) i];
            b.setButtonText (tr (names[i]));
            b.setColour (juce::ToggleButton::textColourId, col::text);
            b.setColour (juce::ToggleButton::tickColourId, col::accent);
            b.onClick = [this, i]
            {
                const auto e = (VSTMTuneProcessor::Expert) i;
                if (! audio.setExpert (e, toggles[(size_t) i].getToggleState()))
                    toggles[(size_t) i].setToggleState (audio.getExpert (e), juce::dontSendNotification);
            };
            addAndMakeVisible (b);
        }
        closeButton.setButtonText (tr ("Close"));
        closeButton.onClick = [this] { if (onClose) onClose(); };
        addAndMakeVisible (closeButton);
        setTitle (tr ("Developer panel"));
        update();
    }

    std::function<void()> onClose;

    /** Called by the editor's timer while the panel is visible. */
    void update()
    {
        auto& e = audio.engine;
        const bool voiced = e.currentVoiced.load();
        const int note = e.currentNote.load();
        auto midiText = [] (float m) { return juce::String (m, 2) + " (" + juce::String (440.0 * std::pow (2.0, (m - 69.0) / 12.0), 1) + " Hz)"; };
        juce::StringArray lines;
        lines.add ("voiced      " + juce::String (voiced ? "yes" : "no"));
        lines.add ("input       " + (voiced ? midiText (e.currentInMidi.load()) : juce::String ("-")));
        lines.add ("output      " + (voiced ? midiText (e.currentOutMidi.load()) : juce::String ("-")));
        lines.add ("target note " + (note >= 0 ? juce::String (note) : juce::String ("-")));
        lines.add ("clarity     " + juce::String (e.currentClarity.load(), 3));
        lines.add ("analysis    " + juce::String (e.currentDelayMs.load(), 1) + " ms behind the input");
        lines.add ("latency     " + juce::String (audio.getLatencySamples()) + " samples ("
                   + juce::String (1000.0 * audio.getLatencySamples() / std::max (1.0, audio.getSampleRate()), 1) + " ms)");
        const auto text = lines.joinIntoString ("\n");
        if (text != diagnostics) { diagnostics = text; repaint(); }

        const auto& a = audio.auth.current();
        juce::String status = "admin";
        if (a.label.isNotEmpty()) status << " - " << a.label;
        status << "   key " << juce::String::toHexString (a.keyId.data(), 8, 0) << "   serial " << juce::String ((int64_t) a.serial)
               << "   " << (a.expiresAt == 0 ? vstm::i18n::tr ("auth.noexpiry")
                                              : vstm::i18n::trf ("auth.until", juce::Time ((int64_t) a.expiresAt * 1000).formatted ("%Y-%m-%d")));
        if (status != authLine) { authLine = status; repaint(); }

        for (int i = 0; i < (int) VSTMTuneProcessor::Expert::count; ++i)
            toggles[(size_t) i].setToggleState (audio.getExpert ((VSTMTuneProcessor::Expert) i), juce::dontSendNotification);
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (col::card);
        g.fillRoundedRectangle (r, 14.0f);
        g.setColour (col::accent.withAlpha (0.6f));
        g.drawRoundedRectangle (r.reduced (0.75f), 14.0f, 1.5f);

        auto area = getLocalBounds().reduced (22, 18);
        g.setColour (col::accentText);
        g.setFont (font (17.0f, Weight::semibold, 0.08f));
        g.drawText (vstm::i18n::tr ("DEVELOPER PANEL"), area.removeFromTop (26), juce::Justification::centredLeft);
        g.setColour (col::dim);
        g.setFont (font (13.0f));
        g.drawText (authLine, area.removeFromTop (22), juce::Justification::centredLeft, true);
        area.removeFromTop (8);

        g.setColour (col::well);
        const auto box = area.removeFromTop (7 * 20 + 16);
        g.fillRoundedRectangle (box.toFloat(), 8.0f);
        g.setColour (col::value);
        g.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 15.0f, juce::Font::plain)));
        auto row = box.reduced (14, 8);
        for (const auto& line : juce::StringArray::fromLines (diagnostics))
            g.drawText (line, row.removeFromTop (20), juce::Justification::centredLeft, false);

        g.setColour (col::faint);
        g.setFont (font (12.5f));
        g.drawFittedText (vstm::i18n::tr ("dev.note"), getLocalBounds().reduced (22, 0).removeFromBottom (54).withTrimmedBottom (14),
                          juce::Justification::centredLeft, 2);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (22, 18);
        area.removeFromTop (26 + 22 + 8 + 7 * 20 + 16 + 12);
        for (auto& b : toggles)
            b.setBounds (area.removeFromTop (30));
        closeButton.setBounds (getWidth() - 22 - 96, 16, 96, 30);
    }

private:
    VSTMTuneProcessor& audio;
    std::array<juce::ToggleButton, (size_t) VSTMTuneProcessor::Expert::count> toggles;
    juce::TextButton closeButton;
    juce::String diagnostics, authLine;
};
} // namespace vui
