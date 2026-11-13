#include "PluginEditor.h"

namespace
{
    constexpr std::array<float, 6> kRatioPresets { 2.0f, 4.0f, 8.0f, 12.0f, 16.0f, 18.0f };

    // Yerlesim (tasarim pikseli, 960 x 600)
    namespace layout
    {
        const juce::Rectangle<int> panel       {   8,   8, 944, 584 };

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

        const juce::Rectangle<int> bar         {  24, 446, 912, 124 };
        const juce::Rectangle<int> custom      { 444, 480,  66,  38 };
        const juce::Rectangle<int> chevUp      { 516, 480,  24,  19 };
        const juce::Rectangle<int> chevDown    { 516, 499,  24,  19 };
        const juce::Rectangle<int> autoGain    { 572, 474,  84,  32 };
        const juce::Rectangle<int> lowCut      { 572, 510,  84,  32 };

        const juce::Rectangle<int> presetPrev  { 330,  58,  26,  30 };
        const juce::Rectangle<int> presetBox   { 358,  56, 152,  34 };
        const juce::Rectangle<int> presetNext  { 512,  58,  26,  30 };
        const juce::Rectangle<int> bypass      { 566,  56,  92,  34 };

        constexpr int ratioBtnX = 48, ratioBtnY = 480, ratioBtnW = 54, ratioBtnH = 38, ratioBtnStep = 62;
        constexpr int modeBtnX[3] = { 688, 770, 852 };
        constexpr int modeBtnY = 474, modeBtnW = 76, modeBtnH = 60;
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

    amountParam = proc.apvts.getRawParameterValue ("amount");
    ratioParam  = proc.apvts.getRawParameterValue ("ratio");
    modeParam   = proc.apvts.getRawParameterValue ("mode");
    bypassParam = proc.apvts.getRawParameterValue ("bypass");

    // ---- ana knob
    amountSlider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    amountSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    amountSlider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                                      juce::MathConstants<float>::pi * 2.75f, true);
    amountSlider.setMouseDragSensitivity (220);
    amountSlider.setLookAndFeel (&knobLnf);
    addAndMakeVisible (amountSlider);
    amountAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "amount", amountSlider);

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

    // ---- ratio tuslari: 2, 4, 8, 12, 16, 18
    for (size_t i = 0; i < ratioButtons.size(); ++i)
    {
        ratioButtons[i] = std::make_unique<sccui::NeonButton> (juce::String (juce::roundToInt (kRatioPresets[i])) + ":1", 14.0f);
        ratioButtons[i]->onClick = [this, i] { setParamReal ("ratio", kRatioPresets[i]); };
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
    ratioUp.onClick   = [this] { setParamReal ("ratio", ratioParam->load() + 0.5f); };
    ratioDown.onClick = [this] { setParamReal ("ratio", ratioParam->load() - 0.5f); };
    addAndMakeVisible (ratioUp);
    addAndMakeVisible (ratioDown);

    // ---- auto gain
    autoGainButton.setClickingTogglesState (true);
    addAndMakeVisible (autoGainButton);
    autoGainAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "autogain", autoGainButton);

    // ---- low cut (sadece dedektor)
    lowCutButton.setClickingTogglesState (true);
    addAndMakeVisible (lowCutButton);
    lowCutAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "lowcut", lowCutButton);

    // ---- ust cubuk: preset secici
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
    bypassButton.setAccent (juce::Colour (0xffffa62b));
    bypassButton.setClickingTogglesState (true);
    addAndMakeVisible (bypassButton);
    bypassAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "bypass", bypassButton);

    // ---- karakter: Smooth / Balanced / Hard
    const char* modeNames[3] = { "SMOOTH", "BALANCED", "HARD" };
    for (size_t i = 0; i < modeButtons.size(); ++i)
    {
        modeButtons[i] = std::make_unique<sccui::ModeButton> (modeNames[i], (sccui::ModeButton::Kind) i);
        modeButtons[i]->onClick = [this, i] { setParamReal ("mode", (float) i); };
        addAndMakeVisible (*modeButtons[i]);
    }

    setSize (kW, kH);                   // resized() cocuklar hazir olduktan sonra cagrilsin
    startTimerHz (30);
    timerCallback();
}

MainView::~MainView()
{
    stopTimer();
    amountSlider.setLookAndFeel (nullptr);
}

//==============================================================================
void MainView::setParamReal (const char* paramId, float value)
{
    if (auto* p = proc.apvts.getParameter (paramId))
    {
        const auto& range = p->getNormalisableRange();
        const float v = juce::jlimit (range.start, range.end, value);
        p->beginChangeGesture();
        p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, p->convertTo0to1 (v)));
        p->endChangeGesture();
    }
}

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

    setParamReal ("ratio", v);
    customLabel.setText (juce::String (v, 1), juce::dontSendNotification);
    shownRatio = v;
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

    // knob -> yuzde ve threshold (anlik, ses calmasa da guncel)
    const float amount = amountParam->load();
    const float thr    = scc::Compressor::thresholdForAmount (amount);
    amountValue.setText (juce::String (juce::roundToInt (amount)) + "%");
    thresholdValue.setText (juce::String (juce::roundToInt (thr)) + " dB");

    // diz (knee): karaktere ve knob'a gore
    const int   modeIdx = (int) std::lround (modeParam->load());
    const float knee    = scc::kneeFor (scc::characterFor (modeIdx), amount);
    thresholdDisplay.setValues (thr, knee);
    kneeBox.setText (juce::String (knee, 1) + " dB");

    // ratio
    const float ratio = ratioParam->load();
    ratioBox.setText (juce::String (ratio, 1) + ":1");
    for (size_t i = 0; i < ratioButtons.size(); ++i)
        ratioButtons[i]->setToggleState (std::abs (ratio - kRatioPresets[i]) < 0.05f, juce::dontSendNotification);
    if (ratio != shownRatio && ! customLabel.isBeingEdited())
    {
        customLabel.setText (juce::String (ratio, 1), juce::dontSendNotification);
        shownRatio = ratio;
    }

    // karakter
    for (size_t i = 0; i < modeButtons.size(); ++i)
        modeButtons[i]->setToggleState ((int) i == modeIdx, juce::dontSendNotification);

    // preset adi (ayar elle degistirildiyse yanina *)
    presetBox.setText (juce::String (SCCAudioProcessor::getPresetName (proc.getCurrentPreset()))
                       + (proc.isPresetModified() ? " *" : ""));
    scope.setBypassed (bypassParam->load() > 0.5f);

    // canli adaptif attack / release
    attackBox.setText  (fmtMs (tel.attackMs.load()));
    releaseBox.setText (fmtMs (tel.releaseMs.load()));

    scope.repaint();
}

//==============================================================================
void MainView::resized()
{
    amountSlider.setBounds (layout::knob);
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

    for (size_t i = 0; i < ratioButtons.size(); ++i)
        ratioButtons[i]->setBounds (layout::ratioBtnX + (int) i * layout::ratioBtnStep, layout::ratioBtnY,
                                    layout::ratioBtnW, layout::ratioBtnH);

    customLabel.setBounds (layout::custom);
    ratioUp.setBounds (layout::chevUp);
    ratioDown.setBounds (layout::chevDown);
    autoGainButton.setBounds (layout::autoGain);
    lowCutButton.setBounds (layout::lowCut);

    presetPrev.setBounds (layout::presetPrev);
    presetBox.setBounds (layout::presetBox);
    presetNext.setBounds (layout::presetNext);
    bypassButton.setBounds (layout::bypass);

    for (size_t i = 0; i < modeButtons.size(); ++i)
        modeButtons[i]->setBounds (layout::modeBtnX[i], layout::modeBtnY, layout::modeBtnW, layout::modeBtnH);
}

//==============================================================================
//  Sabit zemin (bir kez cizilir, resme alinir)
//==============================================================================
void MainView::paint (juce::Graphics& g)
{
    using namespace sccui;

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
    drawLabel (g, "SINGLE  KNOB  COMPRESSOR", { 42.0f, 122.0f, 300.0f, 16.0f }, 11.5f, kTextDim, true, juce::Justification::centredLeft);

    // ust cubuk basliklari
    drawLabel (g, "PRESET", { 332.0f, 38.0f, 100.0f, 14.0f }, 11.0f, kTextDim, true, juce::Justification::centredLeft);

    // sag ust baslik
    drawLabel (g, "SMOOTH  /  HARD  /  CONTROL", { 520.0f, 44.0f, 352.0f, 18.0f }, 12.0f, kTextDim, false, juce::Justification::centredRight);
    g.setColour (kTextDim.withAlpha (0.45f));
    g.fillRect (672.0f, 68.0f, 200.0f, 1.0f);
    {
        const float hs[9] = { 5.0f, 13.0f, 26.0f, 38.0f, 22.0f, 34.0f, 15.0f, 9.0f, 4.0f };
        g.setColour (kTextDim.withAlpha (0.8f));
        for (int i = 0; i < 9; ++i)
        {
            const float x = 890.0f + (float) i * 4.0f;
            g.drawLine (x, 62.0f - hs[i] * 0.5f, x, 62.0f + hs[i] * 0.5f, 1.4f);
        }
    }

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
    drawLabel (g, "THRESHOLD",    { 130.0f, 166.0f, 140.0f, 16.0f }, 13.0f, kText);
    drawLabel (g, "(AUTO RANGE)", { 130.0f, 374.0f, 140.0f, 14.0f }, 10.5f, kTextDim);

    // knob basligi
    drawLabel (g, "AMOUNT", { 272.0f, 120.0f, 200.0f, 18.0f }, 15.0f, kText);

    // ratio / attack / release
    drawLabel (g, "RATIO",   { 513.0f, 324.0f, 78.0f, 16.0f }, 12.5f, kText);
    drawLabel (g, "ATTACK",  { 601.0f, 324.0f, 78.0f, 16.0f }, 12.5f, kText);
    drawLabel (g, "RELEASE", { 689.0f, 324.0f, 78.0f, 16.0f }, 12.5f, kText);
    drawLabel (g, "KNEE",    { 777.0f, 324.0f, 78.0f, 16.0f }, 12.5f, kText);
    drawLabel (g, "(AUTO ADAPTIVE)", { 513.0f, 390.0f, 342.0f, 14.0f }, 10.5f, kTextDim);

    // alt cubuk
    const auto bar = layout::bar.toFloat();
    g.setColour (juce::Colour (0xff090b09));
    g.fillRoundedRectangle (bar, 16.0f);
    g.setColour (kEdge);
    g.drawRoundedRectangle (bar, 16.0f, 1.3f);
    constexpr int dividerX[3] = { 428, 556, 672 };
    for (const int dx : dividerX)
        g.fillRect ((float) dx, bar.getY() + 16.0f, 1.0f, bar.getHeight() - 32.0f);

    drawLabel (g, "RATIO",     { 48.0f,  456.0f, 120.0f, 14.0f }, 11.0f, kTextDim, true, juce::Justification::centredLeft);
    drawLabel (g, "CUSTOM",    { 444.0f, 456.0f, 100.0f, 14.0f }, 11.0f, kTextDim, true, juce::Justification::centredLeft);
    drawLabel (g, "OPTIONS",   { 572.0f, 456.0f, 100.0f, 14.0f }, 11.0f, kTextDim, true, juce::Justification::centredLeft);
    drawLabel (g, "CHARACTER", { 688.0f, 456.0f, 200.0f, 14.0f }, 11.0f, kTextDim, true, juce::Justification::centredLeft);
}

//==============================================================================
SCCAudioProcessorEditor::SCCAudioProcessorEditor (SCCAudioProcessor& p)
    : juce::AudioProcessorEditor (&p), proc (p), view (p)
{
    addAndMakeVisible (view);

    setResizable (true, true);
    setResizeLimits (480, 300, 1920, 1200);
    getConstrainer()->setFixedAspectRatio ((double) MainView::kW / (double) MainView::kH);

    // en son kullanilan pencere boyutunu hatirla
    const int savedW = juce::jlimit (480, 1920, (int) proc.apvts.state.getProperty ("uiWidth", MainView::kW));
    setSize (savedW, juce::roundToInt ((float) savedW * (float) MainView::kH / (float) MainView::kW));
}

void SCCAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff050605));
}

void SCCAudioProcessorEditor::resized()
{
    const float scale = (float) getWidth() / (float) MainView::kW;
    view.setTransform (juce::AffineTransform::scale (scale));
    view.setBounds (0, 0, MainView::kW, MainView::kH);

    proc.apvts.state.setProperty ("uiWidth", getWidth(), nullptr);
}
