#pragma once
//==============================================================================
//  Ozel dizi (Custom Scale) duzenleyicisi: bir oktavlik klavye
//
//  Her tus bir notayi duzeltmeye acar / kapatir. Notalar tona gore tutulur (1, b2, 2 ...
//  parametreleri): ton degisince (ya da Auto yeni tonu bulunca) ayni dizi yeni tona tasinir,
//  klavyedeki isaretler de onunla kayar. Tonik halkayla gosterilir. "Diziden kopyala" secili
//  dizinin notalarini baslangic olarak alir.
//==============================================================================
#include "Controls.h"
#include "../Parameters.h"

namespace vui
{
class ScaleNoteEditor : public juce::Component, private juce::Timer
{
public:
    ScaleNoteEditor (juce::AudioProcessorValueTreeState& state, std::function<int()> detectedKeyFn)
        : apvts (state), detectedKey (std::move (detectedKeyFn))
    {
        copyButton.setButtonText (vstm::i18n::tr ("Copy from scale"));
        copyButton.onClick = [this] { copyFromScale(); };
        addAndMakeVisible (copyButton);
        setSize (620, 268);
        setTitle (vstm::i18n::tr ("CUSTOM SCALE"));
        startTimerHz (10);
    }

    /** The key the custom notes are counted from: the chosen key, else the detected one, else C. */
    int keyReference() const
    {
        const int k = juce::roundToInt (param (vstm::ids::key).convertFrom0to1 (param (vstm::ids::key).getValue())) - 1;
        if (k >= 0) return k;
        const int d = detectedKey ? detectedKey() : -1;
        return d >= 0 ? d : 0;
    }

    bool isAllowed (int pitchClass) const
    {
        const int degree = ((pitchClass - keyReference()) % 12 + 12) % 12;
        return param (vstm::ids::scaleNote (degree)).getValue() > 0.5f;
    }

    /** 1.5 note bypass: a sung note nearest to this one passes through uncorrected (right-click). */
    bool isBypassed (int pitchClass) const
    {
        const int degree = ((pitchClass - keyReference()) % 12 + 12) % 12;
        return param (vstm::ids::scaleBypass (degree)).getValue() > 0.5f;
    }

    void paint (juce::Graphics& g) override
    {
        using namespace juce;
        g.fillAll (Colour (0xff17161c));
        drawText (g, vstm::i18n::upper (vstm::i18n::tr ("CUSTOM SCALE")), { 24.0f, 26.0f }, font (15.0f, Weight::semibold, 0.08f),
                  col::label, Justification::left);
        drawText (g, hint(), { 24.0f, 52.0f }, fitted (font (13.5f), hint(), (float) getWidth() - 48.0f), col::dim, Justification::left);

        const int keyRef = keyReference();
        for (int pass = 0; pass < 2; ++pass)                 // white keys first, black keys on top
            for (int pc = 0; pc < 12; ++pc)
            {
                if (isBlack (pc) != (pass == 1))
                    continue;
                const auto r = keyBounds (pc);
                const bool bypassed = isBypassed (pc);
                const bool on = isAllowed (pc) && ! bypassed;
                const bool over = hover == pc;
                Colour fill = isBlack (pc) ? Colour (0xff25232c) : Colour (0xff3a3842);
                if (on) fill = isBlack (pc) ? col::accent.darker (0.25f) : col::accent;
                if (bypassed) fill = isBlack (pc) ? col::warn.darker (0.45f) : col::warn.darker (0.15f);
                if (over) fill = fill.brighter (0.12f);
                g.setColour (fill);
                g.fillRoundedRectangle (r, 6.0f);
                g.setColour (Colours::black.withAlpha (0.5f));
                g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
                if (pc == keyRef)
                {
                    g.setColour (Colours::white.withAlpha (0.9f));
                    g.drawRoundedRectangle (r.reduced (3.0f), 5.0f, 2.0f);
                }
                const auto tc = bypassed ? Colour (0xff17161c) : (on ? Colours::white : col::dim);
                const int degree = ((pc - keyRef) % 12 + 12) % 12;
                drawText (g, vstm::noteName (pc), { r.getCentreX(), r.getBottom() - 34.0f }, font (15.0f, Weight::semibold), tc);
                drawText (g, vstm::params::degreeName (degree), { r.getCentreX(), r.getBottom() - 15.0f }, font (12.5f), tc.withAlpha (0.8f));
            }

        // legend: what the two colours mean (left of the copy button)
        const float ly = (float) getHeight() - 30.0f;
        auto swatch = [&g, ly] (float x, juce::Colour c) { g.setColour (c); g.fillRoundedRectangle (x, ly - 6.0f, 12.0f, 12.0f, 3.0f); };
        const auto tuned = vstm::i18n::tr ("scale.tuned"), bypass = vstm::i18n::tr ("scale.bypass");
        const auto f = font (12.5f);
        swatch (24.0f, col::accent);
        drawText (g, tuned, { 42.0f, ly }, f, col::dim, Justification::left);
        const float bx = 42.0f + juce::GlyphArrangement::getStringWidth (f, tuned) + 18.0f;
        swatch (bx, col::warn.darker (0.15f));
        drawText (g, bypass, { bx + 18.0f, ly }, fitted (f, bypass, (float) getWidth() - 24.0f - 190.0f - 12.0f - (bx + 18.0f)), col::dim,
                  Justification::left);
    }

    void resized() override
    {
        copyButton.setBounds (getWidth() - 24 - 190, getHeight() - 46, 190, 32);
    }

    void mouseMove (const juce::MouseEvent& e) override { setHover (keyAt (e.position)); }
    void mouseExit (const juce::MouseEvent&) override    { setHover (-1); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        const int pc = keyAt (e.position);
        if (pc < 0)
            return;
        const int degree = ((pc - keyReference()) % 12 + 12) % 12;
        // right-click (Ctrl-click on a Mac): note bypass; left click: the note is a target or not
        auto& p = param (e.mods.isPopupMenu() ? vstm::ids::scaleBypass (degree) : vstm::ids::scaleNote (degree));
        p.beginChangeGesture();
        p.setValueNotifyingHost (p.getValue() > 0.5f ? 0.0f : 1.0f);
        p.endChangeGesture();
        repaint();
    }

private:
    juce::RangedAudioParameter& param (const juce::String& id) const { return *apvts.getParameter (id); }

    juce::String hint() const
    {
        const int k = juce::roundToInt (param (vstm::ids::key).convertFrom0to1 (param (vstm::ids::key).getValue())) - 1;
        const int d = detectedKey ? detectedKey() : -1;
        const juce::String keyText = k >= 0 ? juce::String (vstm::noteName (k))
                                            : vstm::i18n::tr ("Auto") + " " + (d >= 0 ? juce::String (vstm::noteName (d))
                                                                                       : juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xa6")));
        return vstm::i18n::trf ("Click the notes the voice may be tuned to. They move with the key (%1).", keyText);
    }

    static bool isBlack (int pc) { return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10; }

    juce::Rectangle<float> keyBounds (int pc) const
    {
        static const int whiteIndex[12] = { 0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6 };
        const float x0 = 24.0f, y0 = 76.0f, ww = 70.0f, wh = 128.0f, bw = 44.0f, bh = 78.0f;
        if (! isBlack (pc))
            return { x0 + (float) whiteIndex[pc] * (ww + 4.0f), y0, ww, wh };
        const float edge = x0 + (float) (whiteIndex[pc] + 1) * (ww + 4.0f) - 2.0f;     // gap between two white keys
        return { edge - bw * 0.5f, y0 - 8.0f, bw, bh };
    }

    int keyAt (juce::Point<float> p) const
    {
        for (int pc = 0; pc < 12; ++pc)                      // black keys lie on top
            if (isBlack (pc) && keyBounds (pc).contains (p)) return pc;
        for (int pc = 0; pc < 12; ++pc)
            if (! isBlack (pc) && keyBounds (pc).contains (p)) return pc;
        return -1;
    }

    void setHover (int pc) { if (pc != hover) { hover = pc; repaint(); } }

    void copyFromScale()
    {
        const int scale = juce::roundToInt (param (vstm::ids::scale).convertFrom0to1 (param (vstm::ids::scale).getValue()));
        const uint16_t mask = vstm::scaleMask (scale);
        for (int i = 0; i < 12; ++i)
        {
            auto& p = param (vstm::ids::scaleNote (i));
            p.beginChangeGesture();
            p.setValueNotifyingHost (((mask >> i) & 1u) ? 1.0f : 0.0f);
            p.endChangeGesture();
        }
        repaint();
    }

    void timerCallback() override
    {
        int sig = keyReference() * (1 << 24);
        for (int i = 0; i < 12; ++i)
        {
            if (param (vstm::ids::scaleNote (i)).getValue() > 0.5f) sig |= 1 << i;
            if (param (vstm::ids::scaleBypass (i)).getValue() > 0.5f) sig |= 1 << (12 + i);
        }
        if (sig != shown)
        {
            shown = sig;
            repaint();
        }
    }

    juce::AudioProcessorValueTreeState& apvts;
    std::function<int()> detectedKey;
    juce::TextButton copyButton;
    int hover = -1, shown = -1;
};
} // namespace vui
