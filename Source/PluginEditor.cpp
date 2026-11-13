#include "PluginEditor.h"

namespace
{
    constexpr std::array<float, 6> kRatioPresets { 2.0f, 4.0f, 8.0f, 12.0f, 16.0f, 18.0f };

    // Yerlesim (tasarim pikseli, 960 x 736)
    namespace layout
    {
        const juce::Rectangle<int> panel       {   8,   8, 944, 720 };

        const juce::Rectangle<int> inMeter     {  40, 150,  28, 255 };
        const juce::Rectangle<int> outMeter    { 886, 150,  28, 255 };

        const juce::Rectangle<int> thrValue    { 130, 186, 140,  30 };
        const juce::Rectangle<int> thrDisplay  { 130, 224, 140, 144 };

        const juce::Rectangle<int> knob        { 254, 160, 236, 236 };
        const juce::Rectangle<int> amountValue { 272, 138, 200,  36 };

        const juce::Rectangle<int> scope       { 508, 128, 352, 178 };
        const juce::Rectangle<int> ratioBox    { 513, 344,  78,  36 };
        const juce::Rectangle<int> attackBox   { 601, 344,  78,  36 };
        const juce::Rectangle<int> releaseBox  { 689, 344,  78,  36 };
        const juce::Rectangle<int> kneeBox     { 777, 344,  78,  36 };

        // Analog Mode: kutularin yerinde dort manuel knob
        constexpr int analogKnobX = 508, analogKnobY = 312, analogKnobW = 88, analogKnobH = 122;

        const juce::Rectangle<int> bar         {  24, 446, 912, 124 };
        const juce::Rectangle<int> custom      { 444, 480,  66,  38 };
        const juce::Rectangle<int> chevUp      { 516, 480,  24,  19 };
        const juce::Rectangle<int> chevDown    { 516, 499,  24,  19 };
        constexpr int optionsXAdaptive = 572, optionsXAnalog = 630;
        constexpr int autoGainY = 474, lowCutY = 510, optionW = 84, optionH = 32;

        const juce::Rectangle<int> presetPrev  { 330,  58,  26,  30 };
        const juce::Rectangle<int> presetBox   { 358,  56, 152,  34 };
        const juce::Rectangle<int> presetNext  { 512,  58,  26,  30 };
        const juce::Rectangle<int> bypass      { 566,  56,  92,  34 };
        const juce::Rectangle<int> adaptive    { 690,  56, 112,  34 };
        const juce::Rectangle<int> analog      { 804,  56, 112,  34 };

        constexpr int ratioBtnX = 48, ratioBtnY = 480, ratioBtnW = 54, ratioBtnH = 38, ratioBtnStep = 62;
        constexpr int analogRatioX = 44, analogRatioW = 52, analogRatioStep = 56;
        constexpr int modeBtnX[3] = { 688, 770, 852 };
        constexpr int modeBtnY = 474, modeBtnW = 76, modeBtnH = 60;

        // CHARACTER seridi
        const juce::Rectangle<int> charBar     {  24, 582, 912, 130 };
        const juce::Rectangle<int> charOn      {  44, 612,  92,  34 };
        const juce::Rectangle<int> charMatch   {  44, 652,  92,  32 };
        const juce::Rectangle<int> charKnob    { 146, 596,  96, 112 };
        constexpr int typeX = 268, typeY = 612, typeW = 66, typeH = 34, typeStep = 70;
        const juce::Rectangle<int> typeInfo    { 268, 654, 490,  34 };
        const juce::Rectangle<int> slotA       { 784, 612,  40,  34 };
        const juce::Rectangle<int> slotB       { 828, 612,  40,  34 };
        const juce::Rectangle<int> designer    { 784, 652, 132,  34 };
    }

    juce::String fmtMs (float ms)
    {
        return (ms < 10.0f ? juce::String (ms, 1) : juce::String (juce::roundToInt (ms))) + " ms";
    }
}

//==============================================================================
MainView::MainView (SCCAudioProcessor& p)
    : proc (p), scope (p.getTelemetry())
{
    setBufferedToImage (true);          // sabit zemin bir kez cizilir, hareketli parcalar ustune biner

    amountParam  = proc.apvts.getRawParameterValue ("amount");
    ratioParam   = proc.apvts.getRawParameterValue ("ratio");
    modeParam    = proc.apvts.getRawParameterValue ("mode");
    bypassParam  = proc.apvts.getRawParameterValue ("bypass");
    analogParam  = proc.apvts.getRawParameterValue ("analog");
    aThrParam    = proc.apvts.getRawParameterValue ("a_thr");
    aRatioParam  = proc.apvts.getRawParameterValue ("a_ratio");
    aKneeParam   = proc.apvts.getRawParameterValue ("a_knee");
    charOnParam  = proc.apvts.getRawParameterValue ("char_on");
    charAmtParam = proc.apvts.getRawParameterValue ("char_amt");

    // ---- ana knob: ADAPTIVE'de AMOUNT, ANALOG'da THRESHOLD (ayni yerde iki slider)
    for (auto* s : { &amountSlider, &thresholdSlider })
    {
        s->setSliderStyle (juce::Slider::RotaryVerticalDrag);
        s->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        s->setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                                juce::MathConstants<float>::pi * 2.75f, true);
        s->setMouseDragSensitivity (220);
        s->setLookAndFeel (&knobLnf);
    }
    addAndMakeVisible (amountSlider);
    addChildComponent (thresholdSlider);
    amountAttach    = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "amount", amountSlider);
    thresholdAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "a_thr",  thresholdSlider);

    // ---- gostergeler
    addAndMakeVisible (scope);
    addAndMakeVisible (inMeter);
    addAndMakeVisible (outMeter);
    addAndMakeVisible (thresholdDisplay);
    addAndMakeVisible (amountValue);
    addAndMakeVisible (thresholdValue);
    addAndMakeVisible (ratioBox);
    addAndMakeVisible (attackBox);
    addAndMakeVisible (releaseBox);
    addAndMakeVisible (kneeBox);

    // ---- ADAPTIVE ratio tuslari: 2, 4, 8, 12, 16, 18
    for (size_t i = 0; i < ratioButtons.size(); ++i)
    {
        ratioButtons[i] = std::make_unique<sccui::NeonButton> (juce::String (juce::roundToInt (kRatioPresets[i])) + ":1", 14.0f);
        ratioButtons[i]->onClick = [this, i] { proc.setParameterReal ("ratio", kRatioPresets[i]); };
        addAndMakeVisible (*ratioButtons[i]);
    }

    // ---- serbest ratio girisi
    customLabel.setEditable (true, true, false);
    customLabel.setJustificationType (juce::Justification::centred);
    customLabel.setFont (customLabel.getFont().withHeight (20.0f));
    customLabel.setColour (juce::Label::textColourId, sccui::kNeon);
    customLabel.setColour (juce::Label::backgroundColourId, sccui::kBox);
    customLabel.setColour (juce::Label::outlineColourId, sccui::kEdge.brighter (0.25f));
    customLabel.setColour (juce::Label::textWhenEditingColourId, sccui::kNeon);
    customLabel.setColour (juce::Label::backgroundWhenEditingColourId, juce::Colour (0xff161a16));
    customLabel.setColour (juce::Label::outlineWhenEditingColourId, sccui::kNeon);
    customLabel.onTextChange = [this] { commitCustomRatio(); };
    addAndMakeVisible (customLabel);

    ratioUp.setRepeatSpeed (350, 70);
    ratioDown.setRepeatSpeed (350, 70);
    ratioUp.onClick   = [this] { proc.setParameterReal ("ratio", ratioParam->load() + 0.5f); };
    ratioDown.onClick = [this] { proc.setParameterReal ("ratio", ratioParam->load() - 0.5f); };
    addAndMakeVisible (ratioUp);
    addAndMakeVisible (ratioDown);

    // ---- auto gain / low cut (iki modda da ayni dugmeler)
    autoGainButton.setClickingTogglesState (true);
    addAndMakeVisible (autoGainButton);
    autoGainAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "autogain", autoGainButton);

    lowCutButton.setClickingTogglesState (true);
    addAndMakeVisible (lowCutButton);
    lowCutAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "lowcut", lowCutButton);

    // ---- ust cubuk: kompresor preset secici
    presetPrev.onClick = [this]
    {
        const int n = SCCAudioProcessor::getNumPresets();
        proc.applyPreset ((proc.getCurrentPreset() + n - 1) % n);
    };
    presetNext.onClick = [this]
    {
        const int n = SCCAudioProcessor::getNumPresets();
        proc.applyPreset ((proc.getCurrentPreset() + 1) % n);
    };
    presetBox.makeClickable();
    presetBox.onClick = [this] { showPresetMenu(); };
    addAndMakeVisible (presetPrev);
    addAndMakeVisible (presetNext);
    addAndMakeVisible (presetBox);

    // ---- bypass (acikken turuncu: plugin devre disi)
    bypassButton.setAccent (sccui::kAmber);
    bypassButton.setClickingTogglesState (true);
    addAndMakeVisible (bypassButton);
    bypassAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "bypass", bypassButton);

    // ---- calisma modu
    adaptiveButton.onClick = [this] { proc.setParameterReal ("analog", 0.0f); };
    analogButton.onClick   = [this] { proc.setParameterReal ("analog", 1.0f); };
    addAndMakeVisible (adaptiveButton);
    addAndMakeVisible (analogButton);

    // ---- adaptif tepki: Smooth / Balanced / Hard
    const char* modeNames[3] = { "SMOOTH", "BALANCED", "HARD" };
    for (size_t i = 0; i < modeButtons.size(); ++i)
    {
        modeButtons[i] = std::make_unique<sccui::ModeButton> (modeNames[i], (sccui::ModeButton::Kind) i);
        modeButtons[i]->onClick = [this, i] { proc.setParameterReal ("mode", (float) i); };
        addAndMakeVisible (*modeButtons[i]);
    }

    // ---- Analog Mode: manuel knob'lar
    attackKnob.formatValue  = [] (double v) { return (v < 1.0 ? juce::String (v, 2) : (v < 10.0 ? juce::String (v, 1) : juce::String (juce::roundToInt (v)))) + " ms"; };
    releaseKnob.formatValue = [] (double v) { return juce::String (juce::roundToInt (v)) + " ms"; };
    kneeKnob.formatValue    = [] (double v) { return juce::String (v, 1) + " dB"; };
    makeupKnob.formatValue  = [] (double v) { return "+" + juce::String (v, 1) + " dB"; };
    attackKnob.attach  (proc.apvts, "a_attack");
    releaseKnob.attach (proc.apvts, "a_release");
    kneeKnob.attach    (proc.apvts, "a_knee");
    makeupKnob.attach  (proc.apvts, "a_makeup");
    for (auto* k : { &attackKnob, &releaseKnob, &kneeKnob, &makeupKnob })
    {
        k->setTitleSize (12.0f);
        addChildComponent (*k);
    }

    // ---- Analog Mode: ratio tuslari 1.5 .. 20 ve LIMIT
    for (size_t i = 0; i < analogRatioButtons.size(); ++i)
    {
        const float r = scc::kAnalogRatios[i];
        const juce::String text = r >= scc::kManualLimitRatio ? juce::String ("LIMIT")
                                                              : juce::String (r, std::abs (r - std::round (r)) < 0.01f ? 0 : 1) + ":1";
        analogRatioButtons[i] = std::make_unique<sccui::NeonButton> (text, 12.5f);
        analogRatioButtons[i]->onClick = [this, i] { proc.setParameterReal ("a_ratio", (float) i); };
        addChildComponent (*analogRatioButtons[i]);
    }

    // ---- CHARACTER seridi
    charOnButton.setClickingTogglesState (true);
    addAndMakeVisible (charOnButton);
    charOnAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "char_on", charOnButton);

    charMatchButton.setClickingTogglesState (true);
    addAndMakeVisible (charMatchButton);
    charMatchAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "char_match", charMatchButton);

    charKnob.setValueSize (14.0f);
    charKnob.formatValue = [] (double v) { return juce::String (juce::roundToInt (v)) + "%"; };
    charKnob.attach (proc.apvts, "char_amt");
    addAndMakeVisible (charKnob);

    for (size_t i = 0; i < typeButtons.size(); ++i)
    {
        typeButtons[i] = std::make_unique<sccui::NeonButton> (sccui::characterTypeLabel ((int) i), 11.5f);
        typeButtons[i]->onClick = [this, i]
        {
            proc.setParameterReal (sccparams::slotTypeId (proc.getActiveSlot()), (float) i);
        };
        addAndMakeVisible (*typeButtons[i]);
    }
    addAndMakeVisible (typeInfo);

    slotAButton.onClick = [this] { proc.setActiveSlot (0); };
    slotBButton.onClick = [this] { proc.setActiveSlot (1); };
    designerButton.onClick = [this] { if (onOpenDesigner) onOpenDesigner(); };
    addAndMakeVisible (slotAButton);
    addAndMakeVisible (slotBButton);
    addAndMakeVisible (designerButton);

    setSize (kW, kH);                   // resized() cocuklar hazir olduktan sonra cagrilsin
    startTimerHz (30);
    timerCallback();
}

MainView::~MainView()
{
    stopTimer();
    amountSlider.setLookAndFeel (nullptr);
    thresholdSlider.setLookAndFeel (nullptr);
}

void MainView::visibilityChanged()
{
    if (isVisible())
    {
        startTimerHz (30);
        timerCallback();
    }
    else
    {
        stopTimer();
    }
}

//==============================================================================
void MainView::showPresetMenu()
{
    juce::PopupMenu menu;
    const int n   = SCCAudioProcessor::getNumPresets();
    const int cur = proc.getCurrentPreset();
    for (int i = 0; i < n; ++i)
        menu.addItem (i + 1, juce::String (SCCAudioProcessor::getPresetName (i)), true, i == cur);

    juce::Component::SafePointer<MainView> safeThis (this);
    menu.showMenuAsync (juce::PopupMenu::Options(), [safeThis] (int result)
    {
        if (safeThis != nullptr && result > 0)
            safeThis->proc.applyPreset (result - 1);
    });
}

void MainView::commitCustomRatio()
{
    const juce::String txt = customLabel.getText().replaceCharacter (',', '.').retainCharacters ("0123456789.");
    float v = txt.getFloatValue();
    if (v < 1.0f) v = 1.0f;
    v = juce::jmin (v, 100.0f);

    proc.setParameterReal ("ratio", v);
    customLabel.setText (juce::String (v, 1), juce::dontSendNotification);
    shownRatio = v;
}

//==============================================================================
//  ADAPTIVE <-> ANALOG: hangi kontrollerin gorunecegi
//==============================================================================
void MainView::applyMode (bool analog)
{
    shownAnalog = analog ? 1 : 0;

    // ana knob
    amountSlider.setVisible (! analog);
    thresholdSlider.setVisible (analog);

    // dalga formunun alti: otomatik deger kutulari  <->  manuel knob'lar
    for (auto* b : { &ratioBox, &attackBox, &releaseBox, &kneeBox })
        b->setVisible (! analog);
    for (auto* k : { &attackKnob, &releaseKnob, &kneeKnob, &makeupKnob })
        k->setVisible (analog);

    // alt cubuk
    for (auto& b : ratioButtons)       b->setVisible (! analog);
    for (auto& b : modeButtons)        b->setVisible (! analog);
    for (auto& b : analogRatioButtons) b->setVisible (analog);
    customLabel.setVisible (! analog);
    ratioUp.setVisible (! analog);
    ratioDown.setVisible (! analog);

    const int optionsX = analog ? layout::optionsXAnalog : layout::optionsXAdaptive;
    autoGainButton.setBounds (optionsX, layout::autoGainY, layout::optionW, layout::optionH);
    lowCutButton.setBounds   (optionsX, layout::lowCutY,   layout::optionW, layout::optionH);

    adaptiveButton.setToggleState (! analog, juce::dontSendNotification);
    analogButton.setToggleState (analog, juce::dontSendNotification);

    repaint();                          // sabit zemindeki basliklar moda gore degisir
}

//==============================================================================
void MainView::timerCallback()
{
    auto& tel = proc.getTelemetry();

    // metreler: aninda yukselir, 36 dB/sn duser
    constexpr float dt = 1.0f / 30.0f;
    for (int c = 0; c < 2; ++c)
    {
        const float inLin  = tel.inPeak[c].exchange (0.0f);
        const float outLin = tel.outPeak[c].exchange (0.0f);
        const float inDb   = inLin  > 1.0e-5f ? 20.0f * std::log10 (inLin)  : -90.0f;
        const float outDb  = outLin > 1.0e-5f ? 20.0f * std::log10 (outLin) : -90.0f;
        meterInDb[c]  = std::max (inDb,  meterInDb[c]  - 36.0f * dt);
        meterOutDb[c] = std::max (outDb, meterOutDb[c] - 36.0f * dt);
    }
    inMeter.setLevelsDb (meterInDb[0], meterInDb[1]);
    outMeter.setLevelsDb (meterOutDb[0], meterOutDb[1]);

    // calisma modu
    const bool analog = analogParam->load() > 0.5f;
    if ((analog ? 1 : 0) != shownAnalog)
        applyMode (analog);

    // threshold ve diz (anlik, ses calmasa da guncel)
    const float amount  = amountParam->load();
    const int   modeIdx = (int) std::lround (modeParam->load());
    const float thr     = analog ? aThrParam->load() : scc::Compressor::thresholdForAmount (amount);
    const float knee    = analog ? aKneeParam->load() : scc::kneeFor (scc::characterFor (modeIdx), amount);

    amountValue.setText (analog ? juce::String (thr, 1) + " dB" : juce::String (juce::roundToInt (amount)) + "%");
    thresholdValue.setText (juce::String (juce::roundToInt (thr)) + " dB");
    thresholdDisplay.setValues (thr, knee);

    if (analog)
    {
        const int index = (int) std::lround (aRatioParam->load());
        for (size_t i = 0; i < analogRatioButtons.size(); ++i)
            analogRatioButtons[i]->setToggleState ((int) i == index, juce::dontSendNotification);
    }
    else
    {
        // ratio
        const float ratio = ratioParam->load();
        ratioBox.setText (juce::String (ratio, 1) + ":1");
        for (size_t i = 0; i < ratioButtons.size(); ++i)
            ratioButtons[i]->setToggleState (std::abs (ratio - kRatioPresets[i]) < 0.05f, juce::dontSendNotification);
        if (std::abs (ratio - shownRatio) > 1.0e-4f && ! customLabel.isBeingEdited())
        {
            customLabel.setText (juce::String (ratio, 1), juce::dontSendNotification);
            shownRatio = ratio;
        }

        // adaptif tepki
        for (size_t i = 0; i < modeButtons.size(); ++i)
            modeButtons[i]->setToggleState ((int) i == modeIdx, juce::dontSendNotification);

        // canli adaptif attack / release ve diz
        attackBox.setText  (fmtMs (tel.attackMs.load()));
        releaseBox.setText (fmtMs (tel.releaseMs.load()));
        kneeBox.setText (juce::String (knee, 1) + " dB");
    }

    // kompresor preset adi (ayar elle degistirildiyse yanina *); Analog modda preset gecerli degil
    presetBox.setText (analog ? juce::String ("Manual")
                              : juce::String (SCCAudioProcessor::getPresetName (proc.getCurrentPreset()))
                                    + (proc.isPresetModified() ? " *" : ""));

    // ---- CHARACTER seridi
    const int  slot = proc.getActiveSlot();
    const auto spec = proc.getSlotSpec (slot);
    for (size_t i = 0; i < typeButtons.size(); ++i)
        typeButtons[i]->setToggleState ((int) i == spec.type, juce::dontSendNotification);
    slotAButton.setToggleState (slot == 0, juce::dontSendNotification);
    slotBButton.setToggleState (slot == 1, juce::dontSendNotification);

    juce::String info = sccui::characterTypeDescription (spec.type);
    if (spec.type == scc::kCharCustom)
    {
        const auto name = proc.getSlotPresetName (slot);
        info = "CUSTOM  -  " + (name.isEmpty() ? juce::String ("your own character, built in the DESIGNER")
                                               : name + (proc.isSlotModified (slot) ? " *" : ""));
    }
    const bool charOn = charOnParam->load() > 0.5f;
    typeInfo.setText (info, charOn ? sccui::kText : sccui::kTextDim);

    // ---- dalga formu
    scope.setBypassed (bypassParam->load() > 0.5f);
    scope.setTags (analog ? "ANALOG" : "ADAPTIVE",
                   charOn ? juce::String ("CHARACTER  ") + sccui::characterTypeLabel (spec.type) + "  "
                                + juce::String (juce::roundToInt (charAmtParam->load())) + "%  (" + (slot == 0 ? "A" : "B") + ")"
                          : juce::String());
    scope.repaint();
}

//==============================================================================
void MainView::resized()
{
    amountSlider.setBounds (layout::knob);
    thresholdSlider.setBounds (layout::knob);
    amountValue.setBounds (layout::amountValue);
    thresholdValue.setBounds (layout::thrValue);
    thresholdDisplay.setBounds (layout::thrDisplay);

    inMeter.setBounds (layout::inMeter);
    outMeter.setBounds (layout::outMeter);
    scope.setBounds (layout::scope);

    ratioBox.setBounds (layout::ratioBox);
    attackBox.setBounds (layout::attackBox);
    releaseBox.setBounds (layout::releaseBox);
    kneeBox.setBounds (layout::kneeBox);

    sccui::LabeledKnob* analogKnobs[4] = { &attackKnob, &releaseKnob, &kneeKnob, &makeupKnob };
    for (int i = 0; i < 4; ++i)
        analogKnobs[i]->setBounds (layout::analogKnobX + i * layout::analogKnobW, layout::analogKnobY,
                                   layout::analogKnobW, layout::analogKnobH);

    for (size_t i = 0; i < ratioButtons.size(); ++i)
        ratioButtons[i]->setBounds (layout::ratioBtnX + (int) i * layout::ratioBtnStep, layout::ratioBtnY,
                                    layout::ratioBtnW, layout::ratioBtnH);
    for (size_t i = 0; i < analogRatioButtons.size(); ++i)
        analogRatioButtons[i]->setBounds (layout::analogRatioX + (int) i * layout::analogRatioStep, layout::ratioBtnY,
                                          layout::analogRatioW, layout::ratioBtnH);

    customLabel.setBounds (layout::custom);
    ratioUp.setBounds (layout::chevUp);
    ratioDown.setBounds (layout::chevDown);

    const int optionsX = shownAnalog == 1 ? layout::optionsXAnalog : layout::optionsXAdaptive;
    autoGainButton.setBounds (optionsX, layout::autoGainY, layout::optionW, layout::optionH);
    lowCutButton.setBounds   (optionsX, layout::lowCutY,   layout::optionW, layout::optionH);

    presetPrev.setBounds (layout::presetPrev);
    presetBox.setBounds (layout::presetBox);
    presetNext.setBounds (layout::presetNext);
    bypassButton.setBounds (layout::bypass);
    adaptiveButton.setBounds (layout::adaptive);
    analogButton.setBounds (layout::analog);

    for (size_t i = 0; i < modeButtons.size(); ++i)
        modeButtons[i]->setBounds (layout::modeBtnX[i], layout::modeBtnY, layout::modeBtnW, layout::modeBtnH);

    charOnButton.setBounds (layout::charOn);
    charMatchButton.setBounds (layout::charMatch);
    charKnob.setBounds (layout::charKnob);
    for (size_t i = 0; i < typeButtons.size(); ++i)
        typeButtons[i]->setBounds (layout::typeX + (int) i * layout::typeStep, layout::typeY, layout::typeW, layout::typeH);
    typeInfo.setBounds (layout::typeInfo);
    slotAButton.setBounds (layout::slotA);
    slotBButton.setBounds (layout::slotB);
    designerButton.setBounds (layout::designer);
}

//==============================================================================
//  Sabit zemin (bir kez cizilir, resme alinir; mod degisince yeniden cizilir)
//==============================================================================
void MainView::paint (juce::Graphics& g)
{
    using namespace sccui;
    const bool analog = shownAnalog == 1;

    // arka plan
    g.setGradientFill (juce::ColourGradient (kBgTop, 0.0f, 0.0f, kBgBottom, 0.0f, (float) kH, false));
    g.fillAll();

    // ana panel
    const auto panel = layout::panel.toFloat();
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff181a18), 0.0f, panel.getY(),
                                             juce::Colour (0xff0b0c0b), 0.0f, panel.getBottom(), false));
    g.fillRoundedRectangle (panel, 22.0f);
    g.setColour (kEdge);
    g.drawRoundedRectangle (panel, 22.0f, 1.5f);
    g.setColour (juce::Colours::white.withAlpha (0.05f));
    g.drawLine (panel.getX() + 26.0f, panel.getY() + 1.5f, panel.getRight() - 26.0f, panel.getY() + 1.5f, 1.0f);

    // logo: SCC
    {
        const juce::Rectangle<float> logo (34.0f, 22.0f, 240.0f, 100.0f);
        g.setFont (g.getCurrentFont().withHeight (92.0f).withStyle (juce::Font::bold | juce::Font::italic));
        g.setColour (juce::Colours::black.withAlpha (0.7f));
        g.drawText ("SCC", logo.translated (3.0f, 4.0f), juce::Justification::centredLeft, false);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff4f6f4), 0.0f, logo.getY() + 14.0f,
                                                 juce::Colour (0xff5d625d), 0.0f, logo.getBottom() - 10.0f, false));
        g.drawText ("SCC", logo, juce::Justification::centredLeft, false);
    }
    drawLabel (g, "SINGLE  KNOB  COMPRESSOR", { 42.0f, 122.0f, 200.0f, 16.0f }, 11.5f, kTextDim, true, juce::Justification::centredLeft);
    drawLabel (g, "V6", { 222.0f, 122.0f, 30.0f, 16.0f }, 11.5f, kNeon, true, juce::Justification::centredLeft);

    // ust cubuk basliklari
    drawLabel (g, "PRESET", { 332.0f, 38.0f, 100.0f, 14.0f }, 11.0f, kTextDim, true, juce::Justification::centredLeft);
    drawLabel (g, "MODE",   { 692.0f, 38.0f, 100.0f, 14.0f }, 11.0f, kTextDim, true, juce::Justification::centredLeft);
    drawLabel (g, analog ? "MANUAL CONTROL" : "SELF-ADJUSTING", { 780.0f, 38.0f, 136.0f, 14.0f }, 10.0f,
               kTextDim, false, juce::Justification::centredRight);

    // metre cetvelleri
    auto drawScale = [&g] (const juce::Rectangle<int>& m)
    {
        const float x = (float) m.getRight() + 6.0f;
        for (int db = 0; db >= -60; db -= 6)
        {
            const float y = (float) m.getY() + (float) m.getHeight() * (float) (-db) / 60.0f;
            drawLabel (g, juce::String (db), { x, y - 7.0f, 30.0f, 14.0f }, 11.0f, kTextDim, false, juce::Justification::centredLeft);
        }
    };
    drawScale (layout::inMeter);
    drawScale (layout::outMeter);
    drawLabel (g, "IN",  { 34.0f, 412.0f, 40.0f, 14.0f },  12.0f, kTextDim, true);
    drawLabel (g, "OUT", { 880.0f, 412.0f, 40.0f, 14.0f }, 12.0f, kTextDim, true);

    // threshold
    drawLabel (g, "THRESHOLD", { 130.0f, 166.0f, 140.0f, 16.0f }, 13.0f, kText);
    drawLabel (g, analog ? "(MANUAL)" : "(AUTO RANGE)", { 130.0f, 374.0f, 140.0f, 14.0f }, 10.5f, kTextDim);

    // ana knob'un basligi
    drawLabel (g, analog ? "THRESHOLD" : "AMOUNT", { 272.0f, 120.0f, 200.0f, 18.0f }, 15.0f, kText);

    // dalga formunun alti (Analog modda knob'lar kendi basligini tasir)
    if (! analog)
    {
        drawLabel (g, "RATIO",   { 513.0f, 324.0f, 78.0f, 16.0f }, 12.5f, kText);
        drawLabel (g, "ATTACK",  { 601.0f, 324.0f, 78.0f, 16.0f }, 12.5f, kText);
        drawLabel (g, "RELEASE", { 689.0f, 324.0f, 78.0f, 16.0f }, 12.5f, kText);
        drawLabel (g, "KNEE",    { 777.0f, 324.0f, 78.0f, 16.0f }, 12.5f, kText);
        drawLabel (g, "(AUTO ADAPTIVE)", { 513.0f, 390.0f, 342.0f, 14.0f }, 10.5f, kTextDim);
    }

    // alt cubuk
    const auto bar = layout::bar.toFloat();
    g.setColour (juce::Colour (0xff090b09));
    g.fillRoundedRectangle (bar, 16.0f);
    g.setColour (kEdge);
    g.drawRoundedRectangle (bar, 16.0f, 1.3f);

    drawLabel (g, "RATIO", { 48.0f, 456.0f, 120.0f, 14.0f }, 11.0f, kTextDim, true, juce::Justification::centredLeft);
    if (analog)
    {
        for (const int dx : { 616, 728 })
            g.fillRect ((float) dx, bar.getY() + 16.0f, 1.0f, bar.getHeight() - 32.0f);

        drawLabel (g, "OPTIONS",     { 630.0f, 456.0f, 100.0f, 14.0f }, 11.0f, kTextDim, true, juce::Justification::centredLeft);
        drawLabel (g, "ANALOG MODE", { 744.0f, 456.0f, 170.0f, 14.0f }, 11.0f, kTextDim, true, juce::Justification::centredLeft);
        drawLabel (g, "Fully manual compressor:",       { 744.0f, 478.0f, 176.0f, 14.0f }, 10.5f, kText,    false, juce::Justification::centredLeft);
        drawLabel (g, "you set every time constant.",   { 744.0f, 493.0f, 176.0f, 14.0f }, 10.5f, kText,    false, juce::Justification::centredLeft);
        drawLabel (g, "A classic workflow, not an",     { 744.0f, 516.0f, 176.0f, 14.0f }, 10.0f, kTextDim, false, juce::Justification::centredLeft);
        drawLabel (g, "emulation of any hardware.",     { 744.0f, 530.0f, 176.0f, 14.0f }, 10.0f, kTextDim, false, juce::Justification::centredLeft);
    }
    else
    {
        for (const int dx : { 428, 556, 672 })
            g.fillRect ((float) dx, bar.getY() + 16.0f, 1.0f, bar.getHeight() - 32.0f);

        drawLabel (g, "CUSTOM",   { 444.0f, 456.0f, 100.0f, 14.0f }, 11.0f, kTextDim, true, juce::Justification::centredLeft);
        drawLabel (g, "OPTIONS",  { 572.0f, 456.0f, 100.0f, 14.0f }, 11.0f, kTextDim, true, juce::Justification::centredLeft);
        drawLabel (g, "RESPONSE", { 688.0f, 456.0f, 200.0f, 14.0f }, 11.0f, kTextDim, true, juce::Justification::centredLeft);
    }

    // CHARACTER seridi
    const auto cbar = layout::charBar.toFloat();
    g.setColour (juce::Colour (0xff090b09));
    g.fillRoundedRectangle (cbar, 16.0f);
    g.setColour (kEdge);
    g.drawRoundedRectangle (cbar, 16.0f, 1.3f);
    for (const int dx : { 254, 770 })
        g.fillRect ((float) dx, cbar.getY() + 16.0f, 1.0f, cbar.getHeight() - 32.0f);

    drawLabel (g, "CHARACTER", {  44.0f, 592.0f, 100.0f, 14.0f }, 11.0f, kTextDim, true, juce::Justification::centredLeft);
    drawLabel (g, "TYPE",      { 268.0f, 592.0f, 100.0f, 14.0f }, 11.0f, kTextDim, true, juce::Justification::centredLeft);
    drawLabel (g, "independent of the compressor - works in both modes",
               { 400.0f, 592.0f, 356.0f, 14.0f }, 10.0f, kTextDim, false, juce::Justification::centredRight);
    drawLabel (g, "A / B",     { 784.0f, 592.0f,  60.0f, 14.0f }, 11.0f, kTextDim, true, juce::Justification::centredLeft);
}

//==============================================================================
SCCAudioProcessorEditor::SCCAudioProcessorEditor (SCCAudioProcessor& p)
    : juce::AudioProcessorEditor (&p), proc (p), view (p), designer (p)
{
    addAndMakeVisible (view);
    addChildComponent (designer);

    view.onOpenDesigner = [this] { showDesigner (true); };
    designer.onClose    = [this] { showDesigner (false); };

    setResizable (true, true);
    setResizeLimits (600, 460, 1920, 1472);
    getConstrainer()->setFixedAspectRatio ((double) MainView::kW / (double) MainView::kH);

    // en son kullanilan pencere boyutunu ve sayfayi hatirla
    const int savedW = juce::jlimit (600, 1920, (int) proc.apvts.state.getProperty ("uiWidth", MainView::kW));
    setSize (savedW, juce::roundToInt ((float) savedW * (float) MainView::kH / (float) MainView::kW));

    if ((bool) proc.apvts.state.getProperty ("designerOpen", false))
        showDesigner (true);
}

void SCCAudioProcessorEditor::showDesigner (bool shouldShowDesigner)
{
    designer.setVisible (shouldShowDesigner);
    view.setVisible (! shouldShowDesigner);
    proc.apvts.state.setProperty ("designerOpen", shouldShowDesigner, nullptr);
}

void SCCAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff050605));
}

void SCCAudioProcessorEditor::resized()
{
    const float scale = (float) getWidth() / (float) MainView::kW;
    const auto transform = juce::AffineTransform::scale (scale);

    view.setTransform (transform);
    view.setBounds (0, 0, MainView::kW, MainView::kH);
    designer.setTransform (transform);
    designer.setBounds (0, 0, DesignerPanel::kW, DesignerPanel::kH);

    proc.apvts.state.setProperty ("uiWidth", getWidth(), nullptr);
}
