#include "PluginEditor.h"
#include "Parameters.h"

using namespace vui;
namespace ids = vstm::ids;

namespace
{
const juce::String insansi  = juce::String (juce::CharPointer_UTF8 ("\xc4\xb0nsans\xc4\xb1"));
const juce::String dahaDogal = juce::String (juce::CharPointer_UTF8 ("(Daha Do\xc4\x9f" "al)"));

/** Tracked UPPERCASE label, left aligned at x, optically centred on y. */
void label (juce::Graphics& g, const juce::String& text, float x, float y, float size = 17.0f, float tracking = 0.13f,
            juce::Justification j = juce::Justification::left, juce::Colour c = col::label)
{
    drawText (g, text, { x, y }, font (size, Weight::semibold, tracking), c, j);
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
        g.excludeClipRegion (juce::Rectangle<int> (660, 698, 232, 26));       // gap for "PARAMETERS"
        g.setColour (col::cardEdge.withAlpha (0.9f));
        g.strokePath (card, juce::PathStrokeType (1.2f));
    }

    // card labels and separators
    label (g, "KEY", 99.0f, 380.0f);
    label (g, "SCALE", 99.0f, 504.0f);
    label (g, "PITCH CORRECTION", 99.0f, 633.0f);
    label (g, "RETUNE SPEED", 1173.0f, 380.0f);
    label (g, "HUMANIZE", 1174.0f, 566.0f);
    g.setGradientFill (juce::ColourGradient (col::hairline.brighter (0.15f), 96.0f, 0.0f, col::hairline.withAlpha (0.0f), 415.0f, 0.0f, false));
    g.fillRect (96.0f, 603.0f, 318.0f, 1.0f);
    g.setColour (col::hairline.brighter (0.1f));
    g.fillRect (1172.0f, 527.0f, 268.0f, 1.0f);

    // hero title and side captions
    drawText (g, "ROBOTISM", { 786.5f, 297.0f }, font (24.0f, Weight::medium, 0.43f), juce::Colour (0xffc9c6d2));
    drawText (g, "Robotik", { 553.0f, 330.0f }, font (23.5f), juce::Colour (0xffc9c7d1));
    drawText (g, "(Daha Yapay)", { 536.0f, 356.0f }, font (17.0f), juce::Colour (0xff8c8a95));
    drawText (g, insansi, { 1005.0f, 330.0f }, font (23.5f), juce::Colour (0xffc9c7d1));
    drawText (g, dahaDogal, { 1008.0f, 356.0f }, font (17.0f), juce::Colour (0xff8c8a95));

    label (g, "PARAMETERS", 775.0f, 711.0f, 16.5f, 0.15f, juce::Justification::centred, juce::Colour (0xffaeacb7));

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
    m.addSectionHeader ("Factory");
    for (int i = 0; i < presets.getNumFactoryPresets(); ++i)
        m.addItem (i + 1, presets.getPresetName (i), true, i == current);
    if (presets.getNumPresets() > presets.getNumFactoryPresets())
    {
        m.addSectionHeader ("User");
        for (int i = presets.getNumFactoryPresets(); i < presets.getNumPresets(); ++i)
            m.addItem (i + 1, presets.getPresetName (i), true, i == current);
    }
    m.addSeparator();
    m.addItem (10001, "Save preset...");
    if (current >= presets.getNumFactoryPresets())
        m.addItem (10002, "Delete \"" + presets.getCurrentName() + "\"");
    m.addItem (10003, "Open preset folder");

    juce::Component::SafePointer<PresetBar> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMinimumWidth (getWidth()).withStandardItemHeight (32),
                     [safe] (int result)
                     {
                         if (safe == nullptr || result == 0) return;
                         auto& pm = safe->presets;
                         if (result == 10001) { if (safe->onSaveRequest) safe->onSaveRequest(); }
                         else if (result == 10002) { pm.deleteUserPreset (pm.getCurrentIndex()); pm.loadPreset (0); }
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
      keySelector (param (ids::key), "Key: the tonic notes are snapped to. Auto listens to the singing and finds it."),
      scaleSelector (param (ids::scale), "Scale: which notes are allowed. Chromatic = all 12 notes."),
      correction (param (ids::correction), ValueSlider::Align::centre, "Pitch Correction: how much of the distance to the target note is corrected."),
      retune (param (ids::retune), ValueSlider::Align::left, "Retune Speed: how fast the pitch is pulled to the note. 0 ms = instant (robotic)."),
      humanize (param (ids::humanize), ValueSlider::Align::left, "Humanize: relaxes the correction on long, sustained notes so they keep their natural movement."),
      robotAttachment (param (ids::robotism), robot),
      vibratoKnob (param (ids::vibrato), "SCALE", "Vibrato Scale: depth of the natural vibrato and pitch movement (100% = as sung)."),
      formantKnob (param (ids::formant), "FORMANT", "Formant: shifts the vocal character (throat size) in semitones without changing the pitch."),
      transposeKnob (param (ids::transpose), "PITCH RANGE", "Pitch Range: transposes the corrected voice by up to 12 semitones up or down."),
      mixKnob (param (ids::mix), "MIX", "Mix: blend between the original and the tuned voice."),
      autotuneToggle (param (ids::bypass), true, true, "AUTOTUNE ON", "AUTOTUNE OFF"),
      realtimeToggle (param (ids::realtime), false, false, "REALTIME", "REALTIME"),
      waveButton (icons::pulse(), 1.7f, true)
{
    setLookAndFeel (&lnf);
    tooltips.setLookAndFeel (&lnf);

    addAndMakeVisible (canvas);
    for (juce::Component* c : std::initializer_list<juce::Component*> {
             &presetBar, &saveButton, &sizeButton, &settingsButton, &display, &keySelector, &scaleSelector,
             &correction, &retune, &humanize, &robot, &infoButton, &vibratoKnob, &formantKnob, &transposeKnob,
             &mixKnob, &autotuneToggle, &waveButton, &realtimeToggle })
        canvas.addAndMakeVisible (c);

    // layout on the 1536 x 1024 design canvas (measured from the reference design)
    presetBar.setBounds (535, 26, 457, 45);
    saveButton.setBounds (1308, 26, 44, 44);
    sizeButton.setBounds (1376, 26, 44, 44);
    settingsButton.setBounds (1443, 26, 44, 44);
    display.setBounds (30, 88, 1476, 176);
    keySelector.setBounds (96, 401, 292, 60);
    scaleSelector.setBounds (96, 524, 292, 60);
    correction.setBounds (96, 648, 298, 90);
    correction.setLayout ({ 1, 2, 293, 58 }, { 5, 51, 287, 34 });
    retune.setBounds (1170, 404, 276, 100);
    retune.setLayout ({ 1, 2, 272, 59 }, { 7, 62, 260, 34 });
    humanize.setBounds (1170, 584, 276, 100);
    humanize.setLayout ({ 2, 2, 271, 59 }, { 7, 59, 260, 34 });
    robot.setBounds (554, 275, 430, 430);
    infoButton.setBounds (913, 279, 40, 36);
    vibratoKnob.setBounds (450, 731, 160, 172);
    formantKnob.setBounds (611, 731, 160, 172);
    transposeKnob.setBounds (770, 731, 160, 172);
    mixKnob.setBounds (929, 731, 160, 172);
    autotuneToggle.setBounds (30, 940, 260, 50);
    waveButton.setBounds (1294, 943, 44, 44);
    realtimeToggle.setBounds (1345, 940, 160, 50);

    // separators in the scale menu: | Chromatic | Major .. Melodic | Pentatonics, Blues | modes | Hicaz
    scaleSelector.separatorsAfter = { vstm::kChromatic, vstm::kMelodicMinor, vstm::kBlues, vstm::kMixolydian };
    keySelector.separatorsAfter = { 0 };
    keySelector.suffix = [this]
    {
        if (juce::roundToInt (param (ids::key).convertFrom0to1 (param (ids::key).getValue())) != 0)
            return juce::String();
        const int k = audio.engine.detectedKey.load();
        return k >= 0 ? juce::String (juce::CharPointer_UTF8 ("\xc2\xb7 ")) + vstm::noteName (k) : juce::String();
    };

    robot.setTooltip ("ROBOTISM: turn left for the hard, robotic effect; turn right for natural, human transitions.");
    robot.onContextMenu = [this] { showHostMenu (param (ids::robotism)); };
    for (auto* k : { &vibratoKnob, &formantKnob, &transposeKnob, &mixKnob })
        k->knob.onContextMenu = [this, k] { showHostMenu (k->getParameter()); };
    correction.slider.onContextMenu = [this] { showHostMenu (param (ids::correction)); };
    retune.slider.onContextMenu = [this] { showHostMenu (param (ids::retune)); };
    humanize.slider.onContextMenu = [this] { showHostMenu (param (ids::humanize)); };

    infoButton.setTooltip ("What does ROBOTISM do?");
    infoButton.onClick = [this] { showRobotismInfo(); };
    saveButton.setTooltip ("Save preset");
    saveButton.onClick = [this] { askPresetName(); };
    presetBar.onSaveRequest = [this] { askPresetName(); };
    sizeButton.setTooltip ("Window size");
    sizeButton.onClick = [this] { showSizeMenu(); };
    settingsButton.setTooltip ("Settings");
    settingsButton.onClick = [this] { showSettingsMenu(); };
    waveButton.setTooltip ("Show the raw input pitch in the display");
    waveButton.onClick = [this] { display.showInput = ! display.showInput; waveButton.lit = display.showInput; waveButton.repaint(); display.repaint(); };
    autotuneToggle.setTooltip ("Turn the tuning on / off (follows the host's bypass).");
    realtimeToggle.setTooltip ("REALTIME on: low latency (20 ms) for tracking and live use.\nOff: high quality (40 ms), better for deep voices; the host compensates the delay.");

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

    const int k = audio.engine.detectedKey.load();
    if (k != lastDetectedKey)
    {
        lastDetectedKey = k;
        keySelector.repaint();
    }
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
    m.addItem (1, "Reset to default (" + p.getText (p.getDefaultValue(), 32) + ")");
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
    const char* names[] = { "Small (768 x 512)", "Compact (960 x 640)", "Default (1152 x 768)", "Large (1344 x 896)", "Full (1536 x 1024)" };
    for (int i = 0; i < 5; ++i)
        m.addItem (i + 1, names[i], true, std::abs (getWidth() - widths[i]) < 4);
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
    m.addSubMenu ("Vocal range", voice);
    m.addItem (2, "Show input pitch", true, display.showInput);
    m.addSeparator();
    m.addItem (3, "Reset all parameters");
    m.addItem (4, "About VSTM Tune");

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
        else if (r == 3) ed.audio.presets.loadPreset (0);
        else if (r == 4)
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::NoIcon, "VSTM Tune 1.0.0",
                "Real-time vocal pitch correction.\n\n"
                "Pitch tracking: YIN with sub-sample refinement\n"
                "Shifting: pitch-synchronous overlap-add (formant-preserving)\n"
                "Latency: " + juce::String (ed.audio.getLatencySamples()) + " samples\n\n"
                "Font: Inter (SIL Open Font License 1.1)", "OK", &ed);
    });
}

void VSTMTuneEditor::showRobotismInfo()
{
    auto text = std::make_unique<juce::Label>();
    text->setText (juce::String (juce::CharPointer_UTF8 (
        "ROBOTISM - tek d\xc3\xbc\xc4\x9fmeyle karakter\n\n"
        "\xe2\x97\x80  Robotik (sola): notalar an\xc4\xb1nda ve basamak gibi oturur, vibrato d\xc3\xbcmd\xc3\xbcz olur. "
        "Klasik robotik autotune efekti.\n\n"
        "\xe2\x96\xb6  \xc4\xb0nsans\xc4\xb1 (sa\xc4\x9f" "a): d\xc3\xbczeltme yumu\xc5\x9f" "ar, notalar aras\xc4\xb1 ge\xc3\xa7i\xc5\x9f "
        "S-e\xc4\x9frisiyle kayar, vibrato ve do\xc4\x9f" "al kay\xc4\xb1\xc5\x9flar korunur. Duyulmayan, do\xc4\x9f" "al d\xc3\xbczeltme.\n\n"
        "Ortada: RETUNE SPEED ve HUMANIZE ayarlar\xc4\xb1 oldu\xc4\x9fu gibi uygulan\xc4\xb1r.")), juce::dontSendNotification);
    text->setFont (font (15.0f));
    text->setColour (juce::Label::textColourId, col::text);
    text->setJustificationType (juce::Justification::topLeft);
    text->setBorderSize ({ 10, 12, 10, 12 });
    text->setSize (380, 250);
    juce::CallOutBox::launchAsynchronously (std::move (text), canvas.getLocalArea (&infoButton, infoButton.getLocalBounds()), &canvas);
}

void VSTMTuneEditor::askPresetName()
{
    auto* w = new juce::AlertWindow ("Save preset", "Name of the new preset:", juce::MessageBoxIconType::NoIcon, this);
    w->setLookAndFeel (&lnf);
    w->addTextEditor ("name", audio.presets.isFactoryPreset (audio.presets.getCurrentIndex()) ? "My Preset" : audio.presets.getCurrentName());
    w->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
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
