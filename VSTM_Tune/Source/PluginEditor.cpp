#include "PluginEditor.h"
#include "Parameters.h"

#ifndef VSTM_TUNE_VERSION
 #define VSTM_TUNE_VERSION "1.2.0"
#endif

using namespace vui;
namespace ids = vstm::ids;
using vstm::i18n::tr;
using vstm::i18n::trf;
using vstm::KeyDetector;

namespace
{
// the hero knob's vertical axis on the design canvas: titles and captions are centred on it
constexpr float kKnobAxis = 769.0f;
constexpr float kCaptionOffset = 236.0f;           // Robotik / Insansi captions, symmetric about the axis

juce::Font titleFont()                             { return font (24.0f, Weight::medium, 0.43f); }

/** "41%": the confidence exactly as the engine holds it, rounded for display only. */
juce::String percent (float v)                     { return juce::String (juce::roundToInt (vstm::clamp01 (v) * 100.0f)) + "%"; }

/** tr() with "%1" and "%2" replaced. */
juce::String trf2 (const char* key, const juce::String& a, const juce::String& b) { return tr (key).replace ("%1", a).replace ("%2", b); }
juce::Font sectionFont (float size = 17.0f, float tracking = 0.13f) { return font (size, Weight::semibold, tracking); }

/** Tracked UPPERCASE label at x (left edge or centre), optically centred on y, shrunk to fit maxWidth. */
void label (juce::Graphics& g, const juce::String& text, float x, float y, float maxWidth, float size = 17.0f, float tracking = 0.13f,
            juce::Justification j = juce::Justification::left, juce::Colour c = col::label)
{
    drawText (g, text, { x, y }, fitted (sectionFont (size, tracking), text, maxWidth), c, j);
}

/** A caption line centred on its axis (both lines of a caption share the axis). */
void caption (juce::Graphics& g, const juce::String& text, float axis, float y, float size, float maxWidth, juce::Colour c)
{
    drawText (g, text, { axis, y }, fitted (font (size), text, maxWidth), c, juce::Justification::centred);
}

/** Outline of the big card: left card + right card joined by a band, with the knob notch on top
    and the PARAMETERS tab underneath (coordinates measured on the design canvas). */
juce::Path cardOutline()
{
    juce::Path p;
    const float slope = 0.4375f;                   // dx/dy of the notch sides
    const float top = 339.0f, notchBottom = 671.0f, cardBottom = 761.0f, tabY = 711.0f;
    const float left = 64.0f, right = 1474.0f, r = 28.0f;
    const float lTop = 397.0f, rTop = 1143.0f;      // where the notch sides meet the top edge
    const float lBot = lTop + slope * (notchBottom - top), rBot = rTop - slope * (notchBottom - top);

    auto len = [] (juce::Point<float> v) { return std::sqrt (v.x * v.x + v.y * v.y); };
    auto corner = [&] (juce::Point<float> prev, juce::Point<float> c, juce::Point<float> next, float radius, bool first)
    {
        const auto d1 = (c - prev) / len (c - prev), d2 = (next - c) / len (next - c);
        const auto a = c - d1 * radius, b = c + d2 * radius;
        if (first) p.startNewSubPath (a); else p.lineTo (a);
        p.quadraticTo (c, b);
    };

    const juce::Point<float> tl { left, top }, t1 { lTop, top }, n1 { lBot, notchBottom }, n2 { rBot, notchBottom },
                             t2 { rTop, top }, tr { right, top }, br { right, cardBottom }, bl { left, cardBottom };

    corner (bl, tl, t1, r, true);
    corner (tl, t1, n1, 26.0f, false);
    corner (t1, n1, n2, 44.0f, false);
    corner (n1, n2, t2, 44.0f, false);
    corner (n2, t2, tr, 26.0f, false);
    corner (t2, tr, br, r, false);
    corner (tr, br, { 1155.0f, cardBottom }, r, false);
    p.lineTo (1155.0f, cardBottom);
    p.cubicTo (1115.0f, cardBottom, 1090.0f, tabY, 1045.0f, tabY);
    p.lineTo (495.0f, tabY);
    p.cubicTo (450.0f, tabY, 425.0f, cardBottom, 385.0f, cardBottom);
    corner ({ 385.0f, cardBottom }, bl, tl, r, false);
    p.closeSubPath();
    return p;
}
} // namespace

//==============================================================================
void Canvas::drawBackground (juce::Graphics& g)
{
    const auto full = juce::Rectangle<float> (0.0f, 0.0f, kDesignW, kDesignH);
    g.fillAll (col::outside);

    // window body
    juce::Path windowShape;
    windowShape.addRoundedRectangle (full.reduced (1.5f), 38.0f);
    g.setGradientFill (juce::ColourGradient (col::bgTop, 0.0f, 0.0f, col::bgBottom, 0.0f, kDesignH, false));
    g.fillPath (windowShape);
    {
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (windowShape);
        g.setColour (juce::Colours::white.withAlpha (0.012f));
        g.fillEllipse (300.0f, -500.0f, 936.0f, 900.0f);
        g.setTiledImageFill (makeNoise (128, 128, 0.018f), 0, 0, 1.0f);
        g.fillRect (full);
    }

    // header: wordmark + "T U N E"
    icons::drawLogo (g, { 51.0f, 35.0f, 131.0f, 26.0f }, 0.92f);
    const char* tune[] = { "T", "U", "N", "E" };
    const float tuneX[] = { 224.0f, 261.5f, 301.0f, 339.0f };
    for (int i = 0; i < 4; ++i)
        drawText (g, tune[i], { tuneX[i], 48.0f }, font (18.5f), juce::Colour (0xffc4c3ca));

    // halo behind the hero knob (under the title text)
    {
        const juce::Point<float> c { 769.0f, 490.0f };
        const float r = 205.0f;
        juce::ColourGradient hg (juce::Colour (0xff17161d), c.x, c.y - 40.0f, juce::Colour (0x000e0e12), c.x, c.y + r, true);
        hg.addColour (0.85, juce::Colour (0xff111015));
        g.setGradientFill (hg);
        g.fillEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f);
        g.setColour (juce::Colour (0xff1d1c24));
        g.drawEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f, 1.2f);
        const float r2 = 176.0f;
        g.setColour (juce::Colour (0xff16151b));
        g.drawEllipse (c.x - r2, c.y - r2, r2 * 2.0f, r2 * 2.0f, 1.0f);
    }

    // the big card with the knob notch
    const auto card = cardOutline();
    {
        juce::Graphics::ScopedSaveState ss (g);
        g.setGradientFill (juce::ColourGradient (col::cardTop, 0.0f, 339.0f, juce::Colour (0xff111015), 0.0f, 761.0f, false));
        g.fillPath (card);
        g.reduceClipRegion (card);
        // soft light from the top-left of each card
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.018f), 64.0f, 339.0f,
                                                 juce::Colours::transparentWhite, 420.0f, 760.0f, false));
        g.fillRect (64.0f, 339.0f, 420.0f, 422.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.018f), 1474.0f, 339.0f,
                                                 juce::Colours::transparentWhite, 1100.0f, 760.0f, false));
        g.fillRect (1050.0f, 339.0f, 424.0f, 422.0f);
    }
    {
        juce::Graphics::ScopedSaveState ss (g);
        // gap in the card edge for the PARAMETERS tab title, as wide as the title in this language
        const float w = textWidth (fitted (sectionFont (16.5f, 0.15f), tr ("PARAMETERS"), 400.0f), tr ("PARAMETERS"));
        g.excludeClipRegion (juce::Rectangle<float> (kKnobAxis + 0.5f - w * 0.5f - 16.0f, 698.0f, w + 32.0f, 26.0f).toNearestInt());
        g.setColour (col::cardEdge.withAlpha (0.9f));
        g.strokePath (card, juce::PathStrokeType (1.2f));
    }

    // card labels and separators
    label (g, tr ("KEY"), 99.0f, 380.0f, 288.0f);
    label (g, tr ("SCALE"), 99.0f, 504.0f, 288.0f);
    label (g, tr ("PITCH CORRECTION"), 99.0f, 633.0f, 288.0f);
    label (g, tr ("RETUNE SPEED"), 1173.0f, 380.0f, 268.0f);
    label (g, tr ("HUMANIZE"), 1174.0f, 566.0f, 268.0f);
    g.setGradientFill (juce::ColourGradient (col::hairline.brighter (0.15f), 96.0f, 0.0f, col::hairline.withAlpha (0.0f), 415.0f, 0.0f, false));
    g.fillRect (96.0f, 603.0f, 318.0f, 1.0f);
    g.setColour (col::hairline.brighter (0.1f));
    g.fillRect (1172.0f, 527.0f, 268.0f, 1.0f);

    // hero title on the knob's axis; the side captions symmetric about it, each pair on one axis.
    // Widths are limited by the halo around the knob (captions must stay outside it).
    drawText (g, tr ("ROBOTISM"), { kKnobAxis, 297.0f }, titleFont(), juce::Colour (0xffc9c6d2));
    const float leftAxis = kKnobAxis - kCaptionOffset, rightAxis = kKnobAxis + kCaptionOffset;
    caption (g, tr ("Robotic"),           leftAxis,  330.0f, 23.5f, 200.0f, juce::Colour (0xffc9c7d1));
    caption (g, tr ("(More artificial)"), leftAxis,  356.0f, 17.0f, 154.0f, juce::Colour (0xff8c8a95));
    caption (g, tr ("Human"),             rightAxis, 330.0f, 23.5f, 200.0f, juce::Colour (0xffc9c7d1));
    caption (g, tr ("(More natural)"),    rightAxis, 356.0f, 17.0f, 154.0f, juce::Colour (0xff8c8a95));

    label (g, tr ("PARAMETERS"), kKnobAxis + 0.5f, 711.0f, 400.0f, 16.5f, 0.15f, juce::Justification::centred, juce::Colour (0xffaeacb7));

    // footer bar
    {
        const auto bar = juce::Rectangle<float> (4.0f, 918.0f, kDesignW - 8.0f, 96.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff0e0e12), 0.0f, bar.getY(), juce::Colour (0xff0a0a0d), 0.0f, bar.getBottom(), false));
        g.fillRoundedRectangle (bar, 34.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff302f37), 0.0f, bar.getY(), juce::Colour (0xff17161c), 0.0f, bar.getBottom(), false));
        g.drawRoundedRectangle (bar.reduced (0.5f), 34.0f, 1.2f);

        icons::drawLogo (g, { 671.0f, 956.0f, 91.0f, 18.0f }, 0.5f);
        drawText (g, "TUNE", { 774.0f, 966.0f }, font (24.0f, Weight::regular, 0.10f), juce::Colour (0xff5d5c65), juce::Justification::left);
    }

    // window outline
    g.setColour (col::frame);
    g.drawRoundedRectangle (full.reduced (1.5f), 38.0f, 1.4f);
}

void Canvas::paint (juce::Graphics& g)
{
    const float scale = g.getInternalContext().getPhysicalPixelScaleFactor();
    if (cache.isNull() || std::abs (scale - cacheScale) > 0.01f)
    {
        cacheScale = scale;
        cache = juce::Image (juce::Image::RGB, juce::roundToInt (kDesignW * scale), juce::roundToInt (kDesignH * scale), false);
        juce::Graphics cg (cache);
        cg.addTransform (juce::AffineTransform::scale (scale));
        drawBackground (cg);
    }
    g.drawImageTransformed (cache, juce::AffineTransform::scale (1.0f / cacheScale));
}

//==============================================================================
void KeyHint::paint (juce::Graphics& g)
{
    if (! shown)
        return;
    const auto text = vstm::i18n::upper (tr ("autokey.hint"));
    const auto f = fitted (font (12.5f, Weight::semibold, 0.12f), text, (float) getWidth());
    drawText (g, text, { (float) getWidth(), (float) getHeight() * 0.5f }, f, col::dim, juce::Justification::right);
}

//==============================================================================
void PresetBar::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    const float rad = r.getHeight() * 0.5f;
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff15141a), 0.0f, r.getY(), juce::Colour (0xff111015), 0.0f, r.getBottom(), false));
    g.fillRoundedRectangle (r, rad);
    g.setColour (hoverZone == 1 ? juce::Colour (0xff34333b) : juce::Colour (0xff25242b));
    g.drawRoundedRectangle (r, rad, 1.0f);

    const auto chevC = [this] (int zone) { return hoverZone == zone ? juce::Colours::white : juce::Colour (0xffd9d8de); };
    icons::drawCentred (g, icons::chevron (false, 8.5f), { 33.0f, r.getCentreY() }, chevC (0), 2.3f);
    icons::drawCentred (g, icons::chevron (true, 8.5f), { r.getRight() - 33.0f, r.getCentreY() }, chevC (2), 2.3f);

    const auto f = font (19.5f);
    const auto name = presets.getCurrentName();
    drawText (g, name, { r.getCentreX(), r.getCentreY() }, f, col::text);
    if (presets.isDirty())
    {
        const float x = r.getCentreX() + textWidth (f, name) * 0.5f + 10.0f;
        g.setColour (col::accent);
        g.fillEllipse (x - 3.0f, r.getCentreY() - 3.0f, 6.0f, 6.0f);
    }
}

void PresetBar::mouseDown (const juce::MouseEvent& e)
{
    switch (zoneAt (e.x))
    {
        case 0:  presets.loadPrevious(); break;
        case 2:  presets.loadNext(); break;
        default: showMenu(); return;
    }
    refresh();
    repaint();
}

void PresetBar::showMenu()
{
    presets.refreshUserPresets();
    juce::PopupMenu m;
    const int current = presets.getCurrentIndex();
    m.addSectionHeader (tr ("Factory"));
    juce::String category;
    for (int i = 0; i < presets.getNumFactoryPresets(); ++i)
    {
        const auto c = presets.getFactoryCategory (i);         // Robotic / Natural / Effects (Default has none)
        if (c != category && c.isNotEmpty())
            m.addSectionHeader (tr (c.toRawUTF8()));
        category = c;
        m.addItem (i + 1, presets.getPresetName (i), true, i == current);
    }
    if (presets.getNumPresets() > presets.getNumFactoryPresets())
    {
        m.addSectionHeader (tr ("User"));
        for (int i = presets.getNumFactoryPresets(); i < presets.getNumPresets(); ++i)
            m.addItem (i + 1, presets.getPresetName (i), true, i == current);
    }
    m.addSeparator();
    m.addItem (10001, tr ("Save preset..."));
    if (current >= presets.getNumFactoryPresets())
    {
        m.addItem (10004, trf ("Rename \"%1\"…", presets.getCurrentName()));
        m.addItem (10002, trf ("Delete \"%1\"", presets.getCurrentName()));
    }
    m.addItem (10003, tr ("Open preset folder"));
    m.addSeparator();
    // the song's key and scale stay when a preset is loaded (off: presets set them, as in 1.0)
    m.addItem (10005, tr ("Presets keep key and scale"), true, presets.getKeepTonal());

    juce::Component::SafePointer<PresetBar> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMinimumWidth (getWidth()).withStandardItemHeight (32),
                     [safe] (int result)
                     {
                         if (safe == nullptr || result == 0) return;
                         auto& pm = safe->presets;
                         if (result == 10001) { if (safe->onSaveRequest) safe->onSaveRequest(); }
                         else if (result == 10002) { pm.deleteUserPreset (pm.getCurrentIndex()); pm.loadPreset (0); }
                         else if (result == 10004) { if (safe->onRenameRequest) safe->onRenameRequest(); }
                         else if (result == 10005) { if (safe->onKeepKeyChanged) safe->onKeepKeyChanged (! pm.getKeepTonal()); }
                         else if (result == 10003)
                         {
                             auto folder = vstm::PresetManager::getUserPresetFolder();
                             folder.createDirectory();
                             folder.startAsProcess();
                         }
                         else pm.loadPreset (result - 1);
                         safe->refresh();
                         safe->repaint();
                     });
}

//==============================================================================
VSTMTuneEditor::VSTMTuneEditor (VSTMTuneProcessor& p)
    : AudioProcessorEditor (p),
      audio (p),
      presetBar (p.presets),
      saveButton (icons::download(), 2.2f),
      sizeButton (icons::windowIcon(), 2.2f),
      settingsButton (icons::gear(), 2.1f),
      display (p.engine),
      keySelector (param (ids::key), tr ("tip.key")),
      scaleSelector (param (ids::scale), tr ("Scale: which notes are allowed. Chromatic = all 12 notes. Custom scale: pick your own notes.")),
      // all three value boxes centre their value (no text that sits left in one box and centred in another)
      correction (param (ids::correction), ValueSlider::Align::centre, tr ("Pitch Correction: how much of the distance to the target note is corrected.")),
      retune (param (ids::retune), ValueSlider::Align::centre, tr ("Retune Speed: how fast the pitch is pulled to the note. 0 ms = instant (robotic).")),
      humanize (param (ids::humanize), ValueSlider::Align::centre, tr ("Humanize: relaxes the correction on long, sustained notes so they keep their natural movement.")),
      robotAttachment (param (ids::robotism), robot),
      vibratoKnob (param (ids::vibrato), tr ("knob.vibrato"), tr ("Vibrato Scale: depth of the natural vibrato and pitch movement (100% = as sung).")),
      formantKnob (param (ids::formant), tr ("FORMANT"), tr ("Formant: shifts the vocal character (throat size) in semitones without changing the pitch.")),
      transposeKnob (param (ids::transpose), tr ("PITCH RANGE"), tr ("Pitch Range: transposes the corrected voice by up to 12 semitones up or down.")),
      mixKnob (param (ids::mix), tr ("MIX"), tr ("Mix: blend between the original and the tuned voice.")),
      autotuneToggle (param (ids::bypass), true, true, tr ("AUTOTUNE ON"), tr ("AUTOTUNE OFF")),
      realtimeToggle (param (ids::realtime), false, false, tr ("REALTIME"), tr ("REALTIME")),
      adaptiveToggle (param (ids::adaptive), false, true, tr ("ADAPTIVE"), tr ("ADAPTIVE")),
      waveButton (icons::pulse(), 1.7f, true)
{
    setLookAndFeel (&lnf);
    tooltips.setLookAndFeel (&lnf);

    addAndMakeVisible (canvas);
    for (juce::Component* c : std::initializer_list<juce::Component*> {
             &presetBar, &saveButton, &sizeButton, &settingsButton, &display, &keySelector, &keyHint, &scaleSelector,
             &correction, &retune, &humanize, &robot, &infoButton, &vibratoKnob, &formantKnob, &transposeKnob,
             &mixKnob, &autotuneToggle, &waveButton, &realtimeToggle, &adaptiveToggle })
        canvas.addAndMakeVisible (c);

    // layout on the 1536 x 1024 design canvas (measured from the reference design)
    presetBar.setBounds (535, 26, 457, 45);
    saveButton.setBounds (1308, 26, 44, 44);
    sizeButton.setBounds (1376, 26, 44, 44);
    settingsButton.setBounds (1443, 26, 44, 44);
    display.setBounds (30, 88, 1476, 176);
    keySelector.setBounds (96, 401, 292, 60);
    {
        // right of the KEY label, up to the box's right edge (the label's width depends on the language)
        const float labelEnd = 99.0f + textWidth (fitted (sectionFont(), tr ("KEY"), 288.0f), tr ("KEY")) + 14.0f;
        keyHint.setBounds (juce::roundToInt (labelEnd), 368, 388 - juce::roundToInt (labelEnd), 24);
    }
    scaleSelector.setBounds (96, 524, 292, 60);
    correction.setBounds (96, 648, 298, 90);
    correction.setLayout ({ 1, 2, 293, 58 }, { 5, 51, 287, 34 });
    retune.setBounds (1170, 404, 276, 100);
    retune.setLayout ({ 1, 2, 272, 59 }, { 7, 62, 260, 34 });
    humanize.setBounds (1170, 584, 276, 100);
    humanize.setLayout ({ 2, 2, 271, 59 }, { 7, 59, 260, 34 });
    robot.setBounds (juce::roundToInt (kKnobAxis) - 215, 275, 430, 430);
    // the "i" pill follows the title, which is centred on the knob axis
    infoButton.setBounds (juce::roundToInt (kKnobAxis + textWidth (titleFont(), tr ("ROBOTISM")) * 0.5f + 18.0f), 279, 40, 36);
    vibratoKnob.setBounds (450, 731, 160, 172);
    formantKnob.setBounds (611, 731, 160, 172);
    transposeKnob.setBounds (770, 731, 160, 172);
    mixKnob.setBounds (929, 731, 160, 172);
    // footer: toggles as wide as their text in this language, the input-pitch button left of REALTIME
    autotuneToggle.setBounds (30, 940, juce::jlimit (260, 600, juce::roundToInt (autotuneToggle.preferredWidth())), 50);
    const int rtWidth = juce::jlimit (160, 420, juce::roundToInt (realtimeToggle.preferredWidth()));
    realtimeToggle.setBounds (1505 - rtWidth, 940, rtWidth, 50);
    const float rtText = realtimeToggle.preferredWidth() - 70.0f;        // the REALTIME text itself
    waveButton.setBounds (juce::roundToInt (1505.0f - 54.0f - rtText - 18.0f - 44.0f), 943, 44, 44);
    // ADAPTIVE belongs to how the correction behaves: under HUMANIZE in the right card
    adaptiveToggle.setBounds (1170, 704, 276, 46);

    // separators in the scale menu: | Chromatic | Major .. Melodic | Pentatonics, Blues | modes | Hicaz | Custom
    scaleSelector.separatorsAfter = { vstm::kChromatic, vstm::kMelodicMinor, vstm::kBlues, vstm::kMixolydian };
    // the custom scale is a separate switch (the Scale choice keeps its 13 values: automation and
    // sessions of 1.0 read the same), shown in the scale box and its menu
    auto customOn = [this] { return param (ids::customScale).getValue() > 0.5f; };
    scaleSelector.textOverride = [this, customOn] { return customOn() ? tr ("Custom") : juce::String(); };
    scaleSelector.addExtraItems = [customOn] (juce::PopupMenu& m)
    {
        m.addSeparator();
        m.addItem (vui::DropSelector::kExtraItem + 1, tr ("Custom scale…"), true, customOn());
        if (customOn())
            m.addItem (vui::DropSelector::kExtraItem + 2, tr ("Edit custom scale…"));
    };
    scaleSelector.onExtraItem = [this] (int) { setCustomScale (true); openScaleEditor(); };
    scaleSelector.onChoice = [this] (int) { setCustomScale (false); };
    adaptiveToggle.setTooltip (tr ("Adaptive (experimental): follows the performance within fixed limits. A sung vibrato and deliberate slides are kept (not at the robotic end), fast runs are tuned quicker, uncertain pitch is pulled less. Your settings are not changed."));
    keySelector.separatorsAfter = { 0 };
    // Auto: "· A" + padlock (locked), "· A? 41%" (a guess, not used yet), "· …" (listening); the
    // Lock / Re-analyze / Follow items are added to the menu while Auto is selected
    keySelector.suffix = [this] { return keyStatusText(); };
    keySelector.suffixLocked = [this] { return keyStatusLocked(); };
    keySelector.addExtraItems = [this] (juce::PopupMenu& m) { addAutoKeyItems (m); };
    keySelector.onExtraItem = [this] (int id) { autoKeyAction (id); };

    robot.setTooltip (tr ("ROBOTISM: turn left for the hard, robotic effect; turn right for natural, human transitions."));
    robot.gestureParam = &param (ids::robotism);
    robot.onContextMenu = [this] { showHostMenu (param (ids::robotism)); };
    for (auto* k : { &vibratoKnob, &formantKnob, &transposeKnob, &mixKnob })
        k->knob.onContextMenu = [this, k] { showHostMenu (k->getParameter()); };
    correction.slider.onContextMenu = [this] { showHostMenu (param (ids::correction)); };
    retune.slider.onContextMenu = [this] { showHostMenu (param (ids::retune)); };
    humanize.slider.onContextMenu = [this] { showHostMenu (param (ids::humanize)); };

    infoButton.setTooltip (tr ("What does ROBOTISM do?"));
    infoButton.onClick = [this] { showRobotismInfo(); };
    saveButton.setTooltip (tr ("Save preset"));
    saveButton.onClick = [this] { askPresetName(); };
    presetBar.onSaveRequest = [this] { askPresetName(); };
    presetBar.onRenameRequest = [this] { askRenamePreset(); };
    presetBar.onKeepKeyChanged = [this] (bool keep) { audio.setPresetsKeepKey (keep); presetBar.refresh(); presetBar.repaint(); };
    sizeButton.setTooltip (tr ("Window size"));
    sizeButton.onClick = [this] { showSizeMenu(); };
    settingsButton.setTooltip (tr ("Settings"));
    settingsButton.onClick = [this] { showSettingsMenu(); };
    waveButton.setTooltip (tr ("Show the raw input pitch in the display"));
    waveButton.onClick = [this] { display.showInput = ! display.showInput; waveButton.lit = display.showInput; waveButton.repaint(); display.repaint(); };
    autotuneToggle.setTooltip (tr ("Turn the tuning on / off (follows the host's bypass)."));
    realtimeToggle.setTooltip (tr ("REALTIME on: low latency (20 ms) for tracking and live use.\nOff: high quality (40 ms), better for deep voices; the host compensates the delay."));

    // window size: fixed aspect, 768..1536 wide
    const float s = juce::jlimit (0.5f, 1.0f, audio.uiScale.load());
    setResizable (true, true);
    setResizeLimits (768, 512, 1536, 1024);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio (1.5);
    setSize (juce::roundToInt (kDesignW * s), juce::roundToInt (kDesignH * s));

    presetBar.refresh();
    startTimerHz (15);
}

VSTMTuneEditor::~VSTMTuneEditor()
{
    closeRobotismInfo();
    closeScaleEditor();
    tooltips.setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void VSTMTuneEditor::resized()
{
    const float s = (float) getWidth() / kDesignW;
    canvas.setBounds (0, 0, (int) kDesignW, (int) kDesignH);
    canvas.setTransform (juce::AffineTransform::scale (s));
    audio.uiScale = s;
}

void VSTMTuneEditor::timerCallback()
{
    presetBar.refresh();

    const bool active = param (ids::bypass).getValue() < 0.5f;
    if (active != lastActive)
    {
        lastActive = active;
        display.setActive (active);
        robot.setAlpha (active ? 1.0f : 0.45f);
        for (juce::Component* c : std::initializer_list<juce::Component*> { &vibratoKnob, &formantKnob, &transposeKnob, &mixKnob, &correction, &retune, &humanize })
            c->setAlpha (active ? 1.0f : 0.55f);
    }

    const bool custom = param (ids::customScale).getValue() > 0.5f;
    if (custom != lastCustom)
    {
        lastCustom = custom;
        scaleSelector.repaint();
    }

    // what the key box shows (locked key, guess and its percentage, state) and the vocal-estimate note
    const bool autoKey = isAutoKey();
    keyHint.setShown (autoKey);
    const int shown = autoKey ? (int) std::hash<std::string>{} ((keyStatusText() + (keyStatusLocked() ? "L" : "")).toStdString()) : 0;
    if (shown != lastKeyShown)
    {
        lastKeyShown = shown;
        keySelector.repaint();
    }
}

bool VSTMTuneEditor::isAutoKey()
{
    return juce::roundToInt (param (ids::key).convertFrom0to1 (param (ids::key).getValue())) == 0;
}

juce::String VSTMTuneEditor::keyStatusText() const
{
    auto& keyParam = *audio.apvts.getParameter (ids::key);
    if (juce::roundToInt (keyParam.convertFrom0to1 (keyParam.getValue())) != 0)
        return {};                                               // a key chosen by the user: nothing to add
    const auto& e = audio.engine;
    const auto dot = juce::String (juce::CharPointer_UTF8 ("\xc2\xb7 "));
    switch (e.keyState.load())
    {
        case KeyDetector::kNotNeeded: return {};                 // chromatic: the key does not matter
        case KeyDetector::kLocked:    return e.detectedKey.load() >= 0 ? dot + vstm::noteName (e.detectedKey.load()) : juce::String();
        case KeyDetector::kCandidate: return e.keyCandidate.load() >= 0 ? dot + vstm::noteName (e.keyCandidate.load()) + "? " + percent (e.keyConfidence.load())
                                                                        : dot + juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xa6"));
        default:                      return dot + juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xa6"));
    }
}

bool VSTMTuneEditor::keyStatusLocked() const
{
    auto& keyParam = *audio.apvts.getParameter (ids::key);
    return juce::roundToInt (keyParam.convertFrom0to1 (keyParam.getValue())) == 0
        && audio.engine.keyState.load() == KeyDetector::kLocked && audio.engine.detectedKey.load() >= 0;
}

juce::String VSTMTuneEditor::autoKeyMenuStatus() const
{
    const auto& e = audio.engine;
    const int state = e.keyState.load();
    if (state == KeyDetector::kNotNeeded)
        return tr ("autokey.notneeded");
    if (state == KeyDetector::kLocked && e.detectedKey.load() >= 0)
        return trf2 ("autokey.locked", vstm::noteName (e.detectedKey.load()), percent (e.keyConfidence.load()));
    if (state == KeyDetector::kCandidate && e.keyCandidate.load() >= 0)
        return trf2 ("autokey.guess", vstm::noteName (e.keyCandidate.load()), percent (e.keyConfidence.load()));
    return tr ("autokey.listening");
}

void VSTMTuneEditor::addAutoKeyItems (juce::PopupMenu& m)
{
    if (! isAutoKey())
        return;                                                  // a fixed key: nothing to lock or analyse
    const auto& e = audio.engine;
    const int state = e.keyState.load();
    m.addSeparator();
    m.addSectionHeader (tr ("autokey.header"));
    m.addItem (vui::DropSelector::kExtraItem + 10, autoKeyMenuStatus(), false);
    if (state == KeyDetector::kCandidate && e.keyCandidate.load() >= 0)
        m.addItem (kLockNow, trf ("autokey.lock", vstm::noteName (e.keyCandidate.load())));
    if (state == KeyDetector::kLocked && e.keySuggestion.load() >= 0)
        m.addItem (kUseSuggestion, trf2 ("autokey.use", vstm::noteName (e.keySuggestion.load()), percent (e.keySuggestionConfidence.load())));
    m.addItem (kReanalyze, tr ("autokey.reanalyze"), state == KeyDetector::kLocked || state == KeyDetector::kCandidate);
    m.addItem (kFollow, tr ("autokey.follow"), true, audio.getFollowKeyChanges());
}

void VSTMTuneEditor::autoKeyAction (int itemId)
{
    const auto& e = audio.engine;
    // the key shown in the menu when it was opened is the one locked (the engine may have moved on)
    if (itemId == kLockNow && e.keyCandidate.load() >= 0)
        audio.lockAutoKey (e.keyCandidate.load(), e.keyConfidence.load());
    else if (itemId == kUseSuggestion && e.keySuggestion.load() >= 0)
        audio.lockAutoKey (e.keySuggestion.load(), e.keySuggestionConfidence.load());
    else if (itemId == kReanalyze)
        audio.reanalyzeAutoKey();
    else if (itemId == kFollow)
        audio.setFollowKeyChanges (! audio.getFollowKeyChanges());
    keySelector.repaint();
}

void VSTMTuneEditor::setCustomScale (bool on)
{
    auto& p = param (ids::customScale);
    if ((p.getValue() > 0.5f) == on)
        return;
    if (on)
    {
        // first use (notes still at their defaults): start from the scale that is selected
        bool untouched = true;
        for (int i = 0; i < 12; ++i)
        {
            auto& n = param (ids::scaleNote (i));
            untouched = untouched && std::abs (n.getValue() - n.getDefaultValue()) < 0.5f;
        }
        if (untouched)
        {
            const uint16_t mask = vstm::scaleMask (juce::roundToInt (param (ids::scale).convertFrom0to1 (param (ids::scale).getValue())));
            for (int i = 0; i < 12; ++i)
            {
                auto& n = param (ids::scaleNote (i));
                n.beginChangeGesture();
                n.setValueNotifyingHost (((mask >> i) & 1u) ? 1.0f : 0.0f);
                n.endChangeGesture();
            }
        }
    }
    p.beginChangeGesture();
    p.setValueNotifyingHost (on ? 1.0f : 0.0f);
    p.endChangeGesture();
    scaleSelector.repaint();
}

void VSTMTuneEditor::openScaleEditor()
{
    closeScaleEditor();
    auto& engine = audio.engine;
    scaleContent = std::make_unique<vui::ScaleNoteEditor> (audio.apvts, [&engine] { return engine.detectedKey.load(); });
    scaleContent->setLookAndFeel (&lnf);
    scaleCallout = std::make_unique<juce::CallOutBox> (*scaleContent, scaleSelector.getBoundsInParent(), &canvas);

    // A click outside ends the box's modal state (CallOutBox::inputAttemptWhenModal); then it is deleted
    // here, if this editor still exists and the box is still the open one (not replaced in between).
    juce::Component::SafePointer<VSTMTuneEditor> self (this);
    juce::Component::SafePointer<juce::CallOutBox> box (scaleCallout.get());
    scaleCallout->enterModalState (true, juce::ModalCallbackFunction::create ([self, box] (int)
    {
        if (auto* ed = self.getComponent())
            if (box != nullptr && box.getComponent() == ed->scaleCallout.get())
                ed->closeScaleEditor();
    }), false);
}

void VSTMTuneEditor::closeScaleEditor()
{
    scaleCallout.reset();                   // the box first: the content is its child
    if (scaleContent != nullptr)
        scaleContent->setLookAndFeel (nullptr);
    scaleContent.reset();
}

void VSTMTuneEditor::showHostMenu (juce::RangedAudioParameter& p)
{
    if (auto* ctx = getHostContext())
        if (auto menu = ctx->getContextMenuForParameter (&p))
        {
            menu->showNativeMenu (getMouseXYRelative());
            return;
        }

    juce::PopupMenu m;
    m.addItem (1, trf ("Reset to default (%1)", p.getText (p.getDefaultValue(), 32)));
    juce::Component::SafePointer<VSTMTuneEditor> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options(), [&p, safe] (int r)
    {
        if (safe == nullptr || r != 1) return;
        p.beginChangeGesture();
        p.setValueNotifyingHost (p.getDefaultValue());
        p.endChangeGesture();
    });
}

void VSTMTuneEditor::showSizeMenu()
{
    juce::PopupMenu m;
    const int widths[] = { 768, 960, 1152, 1344, 1536 };
    const char* names[] = { "Small", "Compact", "Default", "Large", "Full" };
    for (int i = 0; i < 5; ++i)
        m.addItem (i + 1, tr (names[i]) + " (" + juce::String (widths[i]) + " x " + juce::String (widths[i] * 2 / 3) + ")",
                   true, std::abs (getWidth() - widths[i]) < 4);
    juce::Component::SafePointer<VSTMTuneEditor> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&sizeButton), [safe, widths] (int r)
    {
        if (safe != nullptr && r > 0)
            safe->setSize (widths[r - 1], widths[r - 1] * 2 / 3);
    });
}

void VSTMTuneEditor::showSettingsMenu()
{
    auto& range = param (ids::range);
    const int current = juce::roundToInt (range.convertFrom0to1 (range.getValue()));
    juce::PopupMenu voice;
    const auto names = vstm::params::rangeChoices();
    for (int i = 0; i < names.size(); ++i)
        voice.addItem (100 + i, names[i], true, i == current);

    juce::PopupMenu m;
    m.addSubMenu (tr ("Vocal range"), voice);
    m.addItem (2, tr ("Show input pitch"), true, display.showInput);
    m.addSeparator();
    m.addItem (3, tr ("Reset all parameters"));
    m.addItem (4, tr ("About VSTM Tune"));

    juce::Component::SafePointer<VSTMTuneEditor> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&settingsButton), [safe] (int r)
    {
        if (safe == nullptr || r == 0) return;
        auto& ed = *safe;
        if (r >= 100)
        {
            auto& rp = ed.param (ids::range);
            rp.beginChangeGesture();
            rp.setValueNotifyingHost (rp.convertTo0to1 ((float) (r - 100)));
            rp.endChangeGesture();
        }
        else if (r == 2) ed.waveButton.onClick();
        else if (r == 3) { ed.audio.presets.resetAll(); ed.presetBar.refresh(); }
        else if (r == 4)
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::NoIcon, juce::String ("VSTM Tune ") + VSTM_TUNE_VERSION,
                trf ("about.text", juce::String (ed.audio.getLatencySamples())), tr ("OK"), &ed);
    });
}

void VSTMTuneEditor::showRobotismInfo()
{
    closeRobotismInfo();
    auto text = std::make_unique<juce::Label>();
    const auto info = tr ("robotism.info");
    text->setText (info, juce::dontSendNotification);
    text->setFont (font (15.0f));
    text->setColour (juce::Label::textColourId, col::text);
    text->setJustificationType (juce::Justification::topLeft);
    text->setBorderSize ({ 10, 12, 10, 12 });
    {
        // as tall as the text needs in this language
        juce::AttributedString as;
        as.append (info, font (15.0f), col::text);
        as.setWordWrap (juce::AttributedString::byWord);
        juce::TextLayout layout;
        layout.createLayout (as, 380.0f - 24.0f);
        text->setSize (380, juce::jlimit (150, 520, juce::roundToInt (layout.getHeight() * 1.08f) + 24));
    }
    // owned here like the custom scale call-out (not launchAsynchronously): it closes with the editor
    // and can never be left behind to swallow a click in another VSTM Tune window
    infoContent = std::move (text);
    infoCallout = std::make_unique<juce::CallOutBox> (*infoContent, canvas.getLocalArea (&infoButton, infoButton.getLocalBounds()), &canvas);
    juce::Component::SafePointer<VSTMTuneEditor> self (this);
    juce::Component::SafePointer<juce::CallOutBox> box (infoCallout.get());
    infoCallout->enterModalState (true, juce::ModalCallbackFunction::create ([self, box] (int)
    {
        if (auto* ed = self.getComponent())
            if (box != nullptr && box.getComponent() == ed->infoCallout.get())
                ed->closeRobotismInfo();
    }), false);
}

void VSTMTuneEditor::closeRobotismInfo()
{
    infoCallout.reset();                    // the box first: the label is its child
    infoContent.reset();
}

void VSTMTuneEditor::askRenamePreset()
{
    auto& pm = audio.presets;
    const int index = pm.getCurrentIndex();
    if (index < pm.getNumFactoryPresets())
        return;
    auto* w = new juce::AlertWindow (tr ("Rename preset"), tr ("New name:"), juce::MessageBoxIconType::NoIcon, this);
    w->setLookAndFeel (&lnf);
    w->addTextEditor ("name", pm.getCurrentName());
    w->addButton (tr ("Rename"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::Component::SafePointer<VSTMTuneEditor> safe (this);
    w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w, index] (int result)
    {
        if (safe == nullptr || result != 1)
            return;
        if (! safe->audio.presets.renameUserPreset (index, w->getTextEditorContents ("name")))
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::NoIcon, tr ("Rename preset"),
                                                    tr ("A preset with this name already exists."), tr ("OK"), safe.getComponent());
        safe->presetBar.refresh();
        safe->presetBar.repaint();
    }), true);
}

void VSTMTuneEditor::askPresetName()
{
    auto* w = new juce::AlertWindow (tr ("Save preset"), tr ("Name of the new preset:"), juce::MessageBoxIconType::NoIcon, this);
    w->setLookAndFeel (&lnf);
    w->addTextEditor ("name", audio.presets.isFactoryPreset (audio.presets.getCurrentIndex()) ? tr ("My Preset") : audio.presets.getCurrentName());
    w->addButton (tr ("Save"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::Component::SafePointer<VSTMTuneEditor> safe (this);
    w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w] (int result)
    {
        if (safe != nullptr && result == 1)
        {
            safe->audio.presets.saveUserPreset (w->getTextEditorContents ("name"));
            safe->presetBar.refresh();
            safe->presetBar.repaint();
        }
    }), true);
}
