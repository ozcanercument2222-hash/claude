#include "CharacterDesigner.h"
#include "SCCTransientProcessor.h"

namespace
{
    // Yerlesim (tasarim pikseli, 960 x 736)
    namespace dl
    {
        const juce::Rectangle<int> panel      {   8,   8, 944, 720 };
        const juce::Rectangle<int> back       { 796,  22, 134,  34 };

        const juce::Rectangle<int> presetPrev {  30,  82,  24,  30 };
        const juce::Rectangle<int> presetBox  {  56,  80, 204,  34 };
        const juce::Rectangle<int> presetNext { 262,  82,  24,  30 };
        const juce::Rectangle<int> save       { 296,  80,  60,  34 };
        const juce::Rectangle<int> saveAs     { 358,  80,  76,  34 };
        const juce::Rectangle<int> rename     { 436,  80,  76,  34 };
        const juce::Rectangle<int> del        { 514,  80,  72,  34 };
        const juce::Rectangle<int> slotA      { 608,  80,  40,  34 };
        const juce::Rectangle<int> slotB      { 650,  80,  40,  34 };
        const juce::Rectangle<int> copy       { 698,  80,  62,  34 };
        const juce::Rectangle<int> swap       { 762,  80,  62,  34 };
        const juce::Rectangle<int> match      { 834,  80,  96,  34 };

        const juce::Rectangle<int> on         {  30, 128,  72,  34 };
        constexpr int typeX = 112, typeY = 128, typeW = 68, typeH = 34, typeStep = 72;
        const juce::Rectangle<int> status     { 624, 128, 306,  34 };

        const juce::Rectangle<int> spectrum   {  30, 172, 560, 226 };
        const juce::Rectangle<int> curve      { 600, 172, 160, 160 };
        const juce::Rectangle<int> harmonics  { 768, 172, 162, 160 };
        constexpr int statX[4] = { 600, 684, 768, 852 };
        constexpr int statY = 358, statW = 78, statH = 34, statLabelY = 342;

        // kontrol gruplari
        const juce::Rectangle<int> gHarmonic  {  30, 408, 368, 150 };
        const juce::Rectangle<int> gSaturate  { 406, 408, 368, 150 };
        const juce::Rectangle<int> gAmount    { 782, 408, 148, 150 };
        const juce::Rectangle<int> gTransient {  30, 566, 276, 150 };
        const juce::Rectangle<int> gDensity   { 314, 566, 184, 150 };
        const juce::Rectangle<int> gTone      { 506, 566, 184, 150 };
        const juce::Rectangle<int> gOutput    { 698, 566, 232, 150 };
        constexpr int knobW = 92, knobTop = 24, knobH = 122;

        const juce::Rectangle<int> amountKnob { 792, 430, 128, 124 };
        const juce::Rectangle<int> outMeter   { 714, 596,  26, 108 };
        const juce::Rectangle<int> grBox      { 754, 610,  80,  34 };
        const juce::Rectangle<int> levelBox   { 842, 610,  78,  34 };
        const juce::Rectangle<int> hint       { 754, 652, 168,  56 };
    }

    // her kontrolun hangi grupta, kacinci sirada oldugu
    struct KnobSlot { const juce::Rectangle<int>* group; int column; };

    KnobSlot knobSlotFor (int index)
    {
        using namespace scc;
        if (index <= kCpHarmBalance) return { &dl::gHarmonic,  index - kCpHarmAmount };
        if (index <= kCpSatMix)      return { &dl::gSaturate,  index - kCpSaturation };
        if (index <= kCpRounding)    return { &dl::gTransient, index - kCpTransient };
        if (index <= kCpThickness)   return { &dl::gDensity,   index - kCpDensity };
        return { &dl::gTone, index - kCpLowChar };
    }

    void drawGroup (juce::Graphics& g, const juce::Rectangle<int>& r, const juce::String& title)
    {
        const auto b = r.toFloat();
        g.setColour (juce::Colour (0xff090b09));
        g.fillRoundedRectangle (b, 12.0f);
        g.setColour (sccui::kEdge);
        g.drawRoundedRectangle (b, 12.0f, 1.2f);
        sccui::drawLabel (g, title, { b.getX() + 14.0f, b.getY() + 7.0f, b.getWidth() - 28.0f, 14.0f },
                          11.0f, sccui::kTextDim, true, juce::Justification::centredLeft);
    }
}

//==============================================================================
DesignerPanel::DesignerPanel (SCCAudioProcessor& p)
    : proc (p),
      spectrum (p.getAnalyzerPre(), p.getAnalyzerPost())
{
    setBufferedToImage (true);          // sabit zemin bir kez cizilir

    // ---- geri donus
    backButton.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible (backButton);

    // ---- preset cubugu
    presetPrev.onClick = [this] { stepPreset (-1); };
    presetNext.onClick = [this] { stepPreset (+1); };
    presetBox.makeClickable();
    presetBox.onClick = [this] { showPresetMenu(); };
    addAndMakeVisible (presetPrev);
    addAndMakeVisible (presetNext);
    addAndMakeVisible (presetBox);

    nameEditor.setFont (nameEditor.getFont().withHeight (15.0f));
    nameEditor.setJustification (juce::Justification::centred);
    nameEditor.setInputRestrictions (48);
    nameEditor.setSelectAllWhenFocused (true);
    nameEditor.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff161a16));
    nameEditor.setColour (juce::TextEditor::textColourId, sccui::kNeon);
    nameEditor.setColour (juce::TextEditor::outlineColourId, sccui::kNeon);
    nameEditor.setColour (juce::TextEditor::focusedOutlineColourId, sccui::kNeon);
    nameEditor.setColour (juce::TextEditor::highlightColourId, sccui::kNeon.withAlpha (0.25f));
    nameEditor.setColour (juce::CaretComponent::caretColourId, sccui::kNeon);
    nameEditor.onReturnKey = [this] { commitNameEdit(); };
    nameEditor.onEscapeKey = [this] { cancelNameEdit(); };
    nameEditor.onFocusLost = [this] { cancelNameEdit(); };
    addChildComponent (nameEditor);

    saveButton.onClick   = [this] { savePreset(); };
    saveAsButton.onClick = [this] { beginNameEdit (NameEdit::saveAs); };
    renameButton.onClick = [this] { beginNameEdit (NameEdit::rename); };
    deleteButton.onClick = [this] { deletePreset(); };
    addAndMakeVisible (saveButton);
    addAndMakeVisible (saveAsButton);
    addAndMakeVisible (renameButton);
    addAndMakeVisible (deleteButton);

    // ---- A / B
    slotAButton.onClick = [this] { proc.setActiveSlot (0); };
    slotBButton.onClick = [this] { proc.setActiveSlot (1); };
    copyButton.onClick  = [this]
    {
        const int from = proc.getActiveSlot();
        proc.copySlot (from, 1 - from);
        setStatus (from == 0 ? "A copied to B" : "B copied to A");
    };
    swapButton.onClick = [this]
    {
        proc.swapSlots();
        setStatus ("A and B swapped");
    };
    addAndMakeVisible (slotAButton);
    addAndMakeVisible (slotBButton);
    addAndMakeVisible (copyButton);
    addAndMakeVisible (swapButton);

    matchButton.setClickingTogglesState (true);
    addAndMakeVisible (matchButton);
    matchAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "char_match", matchButton);

    // ---- ON + tipler
    onButton.setClickingTogglesState (true);
    addAndMakeVisible (onButton);
    onAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "char_on", onButton);

    for (size_t i = 0; i < typeButtons.size(); ++i)
    {
        typeButtons[i] = std::make_unique<sccui::NeonButton> (sccui::characterTypeLabel ((int) i), 11.5f);
        typeButtons[i]->onClick = [this, i]
        {
            proc.setParameterReal (sccparams::slotTypeId (proc.getActiveSlot()), (float) i);
        };
        addAndMakeVisible (*typeButtons[i]);
    }
    addAndMakeVisible (statusText);

    // ---- gorsellestirme
    addAndMakeVisible (spectrum);
    addAndMakeVisible (curve);
    addAndMakeVisible (harmonics);
    addAndMakeVisible (thdBox);
    addAndMakeVisible (evenBox);
    addAndMakeVisible (oddBox);
    addAndMakeVisible (matchBox);

    // ---- Designer'in 15 kontrolu
    for (size_t i = 0; i < knobs.size(); ++i)
    {
        const auto& info = scc::customParamInfo ((int) i);
        const bool bipolar = info.minValue < 0.0f;
        knobs[i] = std::make_unique<sccui::LabeledKnob> (info.shortName, bipolar);

        if ((int) i == scc::kCpDrive)
            knobs[i]->formatValue = [] (double v) { return "+" + juce::String (0.24 * v, 1) + " dB"; };
        else if ((int) i == scc::kCpAttackChar)
            knobs[i]->formatValue = [] (double v) { return juce::String (scc::TransientProcessor::windowMsFor (0.01 * v), 1) + " ms"; };
        else if (bipolar)
            knobs[i]->formatValue = [] (double v) { const int r = juce::roundToInt (v); return (r > 0 ? "+" : "") + juce::String (r); };
        else
            knobs[i]->formatValue = [] (double v) { return juce::String (juce::roundToInt (v)) + "%"; };

        addAndMakeVisible (*knobs[i]);
    }

    amountKnob.setTitleSize (11.0f);
    amountKnob.setValueSize (15.0f);
    amountKnob.formatValue = [] (double v) { return juce::String (juce::roundToInt (v)) + "%"; };
    amountKnob.attach (proc.apvts, "char_amt");
    addAndMakeVisible (amountKnob);

    addAndMakeVisible (outMeter);
    addAndMakeVisible (grBox);
    addAndMakeVisible (levelBox);
    hintText.setText ("The compressor keeps running while you design. Every knob is heard instantly.");
    addAndMakeVisible (hintText);

    setSize (kW, kH);
    attachKnobsToSlot (proc.getActiveSlot());
}

DesignerPanel::~DesignerPanel()
{
    stopTimer();
}

//==============================================================================
void DesignerPanel::visibilityChanged()
{
    if (isVisible())
    {
        proc.getCharacterPresets().refresh();        // baska bir plugin kopyasi kaydetmis olabilir
        shownType = -1;
        refreshProbe (true);
        startTimerHz (30);
        timerCallback();
    }
    else
    {
        stopTimer();
        cancelNameEdit();
    }
}

void DesignerPanel::attachKnobsToSlot (int slot)
{
    shownSlot = slot;
    for (size_t i = 0; i < knobs.size(); ++i)
        knobs[i]->attach (proc.apvts, sccparams::slotParamId (slot, (int) i));
}

void DesignerPanel::setStatus (const juce::String& text, bool isError)
{
    statusText.setText (text, isError ? sccui::kAmber : sccui::kNeon);
    statusTicks = 120;                               // ~4 sn sonra tip aciklamasina doner
}

bool DesignerPanel::slotHoldsUserPreset (int slot) const
{
    auto& presets = proc.getCharacterPresets();
    const int index = presets.indexOf (proc.getSlotPresetName (slot));
    return index >= presets.getNumFactoryPresets();
}

//==============================================================================
//  Preset islemleri (hepsi arayuz is parcaciginda)
//==============================================================================
void DesignerPanel::showPresetMenu()
{
    auto& presets = proc.getCharacterPresets();
    presets.refresh();

    const int slot    = proc.getActiveSlot();
    const int current = presets.indexOf (proc.getSlotPresetName (slot));
    const int numFactory = presets.getNumFactoryPresets();

    juce::PopupMenu menu;
    menu.addSectionHeader ("FACTORY CHARACTERS");
    for (int i = 0; i < numFactory; ++i)
        menu.addItem (i + 1, presets.getPreset (i).name, true, i == current);

    menu.addSeparator();
    menu.addSectionHeader ("USER CHARACTERS");
    if (presets.getNumPresets() == numFactory)
        menu.addItem (100000, "(none yet - use SAVE AS)", false, false);
    for (int i = numFactory; i < presets.getNumPresets(); ++i)
        menu.addItem (i + 1, presets.getPreset (i).name, true, i == current);

    juce::Component::SafePointer<DesignerPanel> safeThis (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetBox),
                        [safeThis, slot] (int result)
    {
        if (safeThis != nullptr && result > 0 && result < 100000)
            safeThis->proc.loadCharacterPreset (slot, result - 1);
    });
}

void DesignerPanel::stepPreset (int delta)
{
    auto& presets = proc.getCharacterPresets();
    const int n = presets.getNumPresets();
    if (n <= 0)
        return;

    const int slot    = proc.getActiveSlot();
    const int current = presets.indexOf (proc.getSlotPresetName (slot));
    const int next    = current < 0 ? (delta > 0 ? 0 : n - 1) : (current + delta + n) % n;
    proc.loadCharacterPreset (slot, next);
}

void DesignerPanel::savePreset()
{
    const int slot = proc.getActiveSlot();
    if (! slotHoldsUserPreset (slot))
    {
        beginNameEdit (NameEdit::saveAs);            // fabrika / adsiz: yeni bir ad iste
        return;
    }

    const auto name   = proc.getSlotPresetName (slot);
    const auto result = proc.getCharacterPresets().saveUserPreset (name, proc.getSlotSpec (slot), true);
    setStatus (result.wasOk() ? "Saved \"" + name + "\"" : result.getErrorMessage(), result.failed());
}

void DesignerPanel::beginNameEdit (NameEdit mode)
{
    const int slot = proc.getActiveSlot();
    if (mode == NameEdit::rename && ! slotHoldsUserPreset (slot))
    {
        setStatus ("Only user characters can be renamed", true);
        return;
    }

    auto name = proc.getSlotPresetName (slot);
    if (mode == NameEdit::saveAs && (name.isEmpty() || proc.getCharacterPresets().isFactoryName (name)))
        name = name.isEmpty() ? "My Character" : name + " (mine)";

    nameEdit = mode;
    nameEditor.setText (name, false);
    nameEditor.setVisible (true);
    nameEditor.toFront (true);
    nameEditor.grabKeyboardFocus();
    setStatus (mode == NameEdit::rename ? "Type the new name, then press Enter" : "Type a name, then press Enter (Esc cancels)");
}

void DesignerPanel::commitNameEdit()
{
    if (nameEdit == NameEdit::none)
        return;

    const auto mode = nameEdit;
    const auto name = CharacterPresetManager::sanitiseName (nameEditor.getText());
    const int  slot = proc.getActiveSlot();
    auto& presets   = proc.getCharacterPresets();

    juce::Result result = juce::Result::ok();
    if (mode == NameEdit::saveAs)
    {
        result = presets.saveUserPreset (name, proc.getSlotSpec (slot), false);
        if (result.wasOk())
        {
            proc.setSlotPresetName (slot, name);
            setStatus ("Saved \"" + name + "\"");
        }
    }
    else
    {
        const auto oldName = proc.getSlotPresetName (slot);
        result = presets.renameUserPreset (oldName, name);
        if (result.wasOk())
        {
            // diger slot da ayni preseti tasiyorsa adi onda da guncelle
            for (int s = 0; s < 2; ++s)
                if (proc.getSlotPresetName (s).equalsIgnoreCase (oldName))
                    proc.setSlotPresetName (s, name);
            setStatus ("Renamed to \"" + name + "\"");
        }
    }

    if (result.failed())
    {
        setStatus (result.getErrorMessage(), true);
        nameEditor.grabKeyboardFocus();              // duzeltebilsin diye acik birak
        return;
    }

    nameEdit = NameEdit::none;
    nameEditor.setVisible (false);
}

void DesignerPanel::cancelNameEdit()
{
    if (nameEdit == NameEdit::none)
        return;
    nameEdit = NameEdit::none;
    nameEditor.setVisible (false);
}

void DesignerPanel::deletePreset()
{
    const int slot = proc.getActiveSlot();
    if (! slotHoldsUserPreset (slot))
    {
        setStatus ("Only user characters can be deleted", true);
        return;
    }

    if (deleteArmed <= 0)
    {
        deleteArmed = 90;                            // 3 sn icinde ikinci tik: sil
        deleteButton.setLabel ("SURE?");
        deleteButton.setAccent (sccui::kAmber);
        deleteButton.setToggleState (true, juce::dontSendNotification);
        return;
    }

    const auto name   = proc.getSlotPresetName (slot);
    const auto result = proc.getCharacterPresets().deleteUserPreset (name);
    if (result.wasOk())
    {
        // ses ayni kalir (degerler parametrelerde); yalnizca dosya ve ad gider
        for (int s = 0; s < 2; ++s)
            if (proc.getSlotPresetName (s).equalsIgnoreCase (name))
                proc.setSlotPresetName (s, {});
    }
    setStatus (result.wasOk() ? "Deleted \"" + name + "\"" : result.getErrorMessage(), result.failed());
    deleteArmed = 1;                                 // bir sonraki adimda dugme eski haline doner
}

//==============================================================================
//  Harmonik imza: ses yolundakiyle ayni motor sinifi, test sinusuyle
//==============================================================================
void DesignerPanel::refreshProbe (bool force)
{
    const int    slot      = proc.getActiveSlot();
    const auto   spec      = proc.getSlotSpec (slot);
    const double intensity = 0.01 * (double) proc.apvts.getRawParameterValue ("char_amt")->load();
    const bool   on        = proc.apvts.getRawParameterValue ("char_on")->load() > 0.5f && intensity > 0.0;

    // seviye: ses varsa gercek seviye (1 dB adimla), yoksa -12 dBFS referans
    const bool   live    = levelSlow > 0.003;
    const double levelDb = live ? std::round (scc::linToDb (levelSlow)) : -12.0;

    if (! force)
    {
        if (++probeTick < 4)
            return;
        probeTick = 0;
        if (spec == probeSpec && std::abs (intensity - probeIntensity) < 1.0e-9
            && std::abs (levelDb - probeLevelDb) < 1.0e-9 && on == probeOn)
            return;
    }

    probeSpec = spec;
    probeIntensity = intensity;
    probeLevelDb = levelDb;
    probeOn = on;

    scc::HarmonicSignature sig;
    for (int h = 1; h < scc::HarmonicSignature::kNumHarmonics; ++h)
        sig.levelDb[h] = -200.0;
    if (on)
        sig = probe.measure (spec, intensity, proc.getEngineSampleRate(), 220.0, scc::dbToLin (levelDb));

    harmonics.setSignature (sig, levelDb, live);
    thdBox.setText  (juce::String (sig.thdPercent,  sig.thdPercent  < 10.0 ? 2 : 1) + " %");
    evenBox.setText (juce::String (sig.evenPercent, sig.evenPercent < 10.0 ? 2 : 1) + " %");
    oddBox.setText  (juce::String (sig.oddPercent,  sig.oddPercent  < 10.0 ? 2 : 1) + " %");
}

//==============================================================================
void DesignerPanel::timerCallback()
{
    auto& tel = proc.getTelemetry();

    // ---- etkin slot: knob'lar o slotun parametrelerine bagli
    const int slot = proc.getActiveSlot();
    if (slot != shownSlot)
    {
        cancelNameEdit();
        attachKnobsToSlot (slot);
    }
    slotAButton.setToggleState (slot == 0, juce::dontSendNotification);
    slotBButton.setToggleState (slot == 1, juce::dontSendNotification);
    copyButton.setLabel (slot == 0 ? "A > B" : "B > A");

    // ---- tip: Designer kontrolleri yalnizca CUSTOM'da sesi etkiler
    const auto spec     = proc.getSlotSpec (slot);
    const bool isCustom = spec.type == scc::kCharCustom;
    for (size_t i = 0; i < typeButtons.size(); ++i)
        typeButtons[i]->setToggleState ((int) i == spec.type, juce::dontSendNotification);
    if (spec.type != shownType)
    {
        shownType = spec.type;
        for (auto& k : knobs)
            k->setActive (isCustom);
        statusTicks = 0;
    }

    // ---- preset adi
    const auto name = proc.getSlotPresetName (slot);
    juce::String shown;
    if (isCustom)
        shown = (name.isEmpty() ? juce::String ("Custom (unsaved)") : name) + (proc.isSlotModified (slot) ? " *" : "");
    else
        shown = juce::String (sccui::characterTypeLabel (spec.type)) + "  (factory type)";
    presetBox.setText (shown);

    const bool user = slotHoldsUserPreset (slot);
    renameButton.setEnabled (user);
    deleteButton.setEnabled (user);

    if (deleteArmed > 0 && --deleteArmed == 0)
    {
        deleteButton.setLabel ("DELETE");
        deleteButton.setAccent (sccui::kNeon);
        deleteButton.setToggleState (false, juce::dontSendNotification);
    }

    // ---- durum satiri: mesaj yoksa tipin aciklamasi
    if (statusTicks > 0)
        --statusTicks;
    if (statusTicks == 0)
        statusText.setText (isCustom ? juce::String ("CUSTOM  -  all 15 controls below are live")
                                     : juce::String (sccui::characterTypeDescription (spec.type))
                                           + ".  Select CUSTOM to design your own.");

    // ---- seviye olcumleri
    constexpr float dt = 1.0f / 30.0f;
    for (int c = 0; c < 2; ++c)
    {
        tel.inPeak[c].exchange (0.0f);
        const float outLin = tel.outPeak[c].exchange (0.0f);
        const float outDb  = outLin > 1.0e-5f ? 20.0f * std::log10 (outLin) : -90.0f;
        meterOutDb[c] = std::max (outDb, meterOutDb[c] - 36.0f * dt);
    }
    outMeter.setLevelsDb (meterOutDb[0], meterOutDb[1]);
    grBox.setText ("-" + juce::String (tel.grDb.load(), 1) + " dB");

    const double peak = (double) tel.charInPeak.exchange (0.0f);
    levelFast = std::max (peak, levelFast * 0.80);
    levelSlow = std::max (peak, levelSlow * 0.965);
    levelBox.setText (levelFast > 1.0e-4 ? juce::String (scc::linToDb (levelFast), 1) + " dB" : juce::String ("-inf"));

    const float matchDb = tel.charMatchDb.load();
    matchBox.setText ((matchDb >= 0.0f ? "+" : "") + juce::String (matchDb, 1) + " dB");

    // ---- gorsellestirme
    spectrum.setSampleRate (proc.getEngineSampleRate());
    spectrum.update();

    const double intensity = 0.01 * (double) proc.apvts.getRawParameterValue ("char_amt")->load();
    const bool   on = proc.apvts.getRawParameterValue ("char_on")->load() > 0.5f && intensity > 0.0;
    scc::Voicing voicing;
    scc::character::computeVoicing (spec, intensity, voicing);
    curve.setVoicing (voicing, levelFast, on);

    refreshProbe (false);
}

//==============================================================================
void DesignerPanel::resized()
{
    backButton.setBounds (dl::back);

    presetPrev.setBounds (dl::presetPrev);
    presetBox.setBounds (dl::presetBox);
    nameEditor.setBounds (dl::presetBox.reduced (1));
    presetNext.setBounds (dl::presetNext);
    saveButton.setBounds (dl::save);
    saveAsButton.setBounds (dl::saveAs);
    renameButton.setBounds (dl::rename);
    deleteButton.setBounds (dl::del);
    slotAButton.setBounds (dl::slotA);
    slotBButton.setBounds (dl::slotB);
    copyButton.setBounds (dl::copy);
    swapButton.setBounds (dl::swap);
    matchButton.setBounds (dl::match);

    onButton.setBounds (dl::on);
    for (size_t i = 0; i < typeButtons.size(); ++i)
        typeButtons[i]->setBounds (dl::typeX + (int) i * dl::typeStep, dl::typeY, dl::typeW, dl::typeH);
    statusText.setBounds (dl::status);

    spectrum.setBounds (dl::spectrum);
    curve.setBounds (dl::curve);
    harmonics.setBounds (dl::harmonics);
    thdBox.setBounds   (dl::statX[0], dl::statY, dl::statW, dl::statH);
    evenBox.setBounds  (dl::statX[1], dl::statY, dl::statW, dl::statH);
    oddBox.setBounds   (dl::statX[2], dl::statY, dl::statW, dl::statH);
    matchBox.setBounds (dl::statX[3], dl::statY, dl::statW, dl::statH);

    for (size_t i = 0; i < knobs.size(); ++i)
    {
        const auto slot = knobSlotFor ((int) i);
        knobs[i]->setBounds (slot.group->getX() + slot.column * dl::knobW, slot.group->getY() + dl::knobTop, dl::knobW, dl::knobH);
    }

    amountKnob.setBounds (dl::amountKnob);
    outMeter.setBounds (dl::outMeter);
    grBox.setBounds (dl::grBox);
    levelBox.setBounds (dl::levelBox);
    hintText.setBounds (dl::hint);
}

//==============================================================================
//  Sabit zemin
//==============================================================================
void DesignerPanel::paint (juce::Graphics& g)
{
    using namespace sccui;

    g.setGradientFill (juce::ColourGradient (kBgTop, 0.0f, 0.0f, kBgBottom, 0.0f, (float) kH, false));
    g.fillAll();

    const auto panel = dl::panel.toFloat();
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff181a18), 0.0f, panel.getY(),
                                             juce::Colour (0xff0b0c0b), 0.0f, panel.getBottom(), false));
    g.fillRoundedRectangle (panel, 22.0f);
    g.setColour (kEdge);
    g.drawRoundedRectangle (panel, 22.0f, 1.5f);
    g.setColour (juce::Colours::white.withAlpha (0.05f));
    g.drawLine (panel.getX() + 26.0f, panel.getY() + 1.5f, panel.getRight() - 26.0f, panel.getY() + 1.5f, 1.0f);

    // logo + baslik
    {
        const juce::Rectangle<float> logo (30.0f, 14.0f, 112.0f, 50.0f);
        g.setFont (g.getCurrentFont().withHeight (46.0f).withStyle (juce::Font::bold | juce::Font::italic));
        g.setColour (juce::Colours::black.withAlpha (0.7f));
        g.drawText ("SCC", logo.translated (2.0f, 3.0f), juce::Justification::centredLeft, false);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff4f6f4), 0.0f, logo.getY() + 8.0f,
                                                 juce::Colour (0xff5d625d), 0.0f, logo.getBottom() - 6.0f, false));
        g.drawText ("SCC", logo, juce::Justification::centredLeft, false);
    }
    drawLabel (g, "CHARACTER  DESIGNER", { 150.0f, 20.0f, 400.0f, 22.0f }, 18.0f, kText, true, juce::Justification::centredLeft);
    drawLabel (g, "COMPRESS   >   DESIGN CHARACTER   >   LISTEN LIVE   >   A/B   >   SAVE   >   USE",
               { 150.0f, 44.0f, 560.0f, 14.0f }, 10.0f, kTextDim, false, juce::Justification::centredLeft);

    // satir basliklari
    drawLabel (g, "CHARACTER PRESET", {  58.0f, 66.0f, 200.0f, 12.0f }, 9.5f, kTextDim, true, juce::Justification::centredLeft);
    drawLabel (g, "A / B",            { 608.0f, 66.0f,  80.0f, 12.0f }, 9.5f, kTextDim, true, juce::Justification::centredLeft);
    drawLabel (g, "LEVEL",            { 834.0f, 66.0f,  96.0f, 12.0f }, 9.5f, kTextDim, true, juce::Justification::centredLeft);
    drawLabel (g, "CHARACTER",        {  30.0f, 116.0f, 80.0f, 11.0f }, 9.5f, kTextDim, true, juce::Justification::centredLeft);
    drawLabel (g, "TYPE",             { 112.0f, 116.0f, 80.0f, 11.0f }, 9.5f, kTextDim, true, juce::Justification::centredLeft);

    // olcum kutularinin basliklari
    const char* statNames[4] = { "THD", "EVEN", "ODD", "MATCH" };
    for (int i = 0; i < 4; ++i)
        drawLabel (g, statNames[i], { (float) dl::statX[i], (float) dl::statLabelY, (float) dl::statW, 12.0f }, 9.5f, kTextDim, true);

    // kontrol gruplari
    drawGroup (g, dl::gHarmonic,  "HARMONICS   (level independent)");
    drawGroup (g, dl::gSaturate,  "SATURATION   (level dependent)");
    drawGroup (g, dl::gAmount,    "CHARACTER");
    drawGroup (g, dl::gTransient, "TRANSIENT");
    drawGroup (g, dl::gDensity,   "DENSITY");
    drawGroup (g, dl::gTone,      "CHARACTER TONE");
    drawGroup (g, dl::gOutput,    "OUTPUT");

    drawLabel (g, "GAIN RED.", { (float) dl::grBox.getX(),    596.0f, (float) dl::grBox.getWidth(),    12.0f }, 9.5f, kTextDim, true);
    drawLabel (g, "INTO CHAR", { (float) dl::levelBox.getX(), 596.0f, (float) dl::levelBox.getWidth(), 12.0f }, 9.5f, kTextDim, true);
}
