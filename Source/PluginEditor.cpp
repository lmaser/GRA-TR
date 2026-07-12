// PluginEditor.cpp

#include "PluginEditor.h"

#include "InfoContent.h"

#include <functional>


using namespace TR;


#if JUCE_WINDOWS

 #include <windows.h>

#endif


namespace UiStateKeys = TR::SimpleUiStateKeys;


// -- Timer & display constants --

static constexpr int   kCrtTimerHz   = 10;

static constexpr int   kIdleTimerHz  = 4;

static constexpr float kMultEpsilon  = 0.01f;

static juce::String formatGainFaderDb (float dB)
{
    return TR::formatGainFaderDbShared (dB, GRATRAudioProcessor::kGainFloorDb);
}


static juce::String formatGainFaderDbCompact (float dB)
{
    return TR::formatGainFaderDbCompactShared (dB, GRATRAudioProcessor::kGainFloorDb);
}


static juce::String formatTimeMsForDisplay (float ms, bool withLabel, bool compact)

{

    const juce::String suffix = withLabel ? " TIME" : "";

    if (ms >= 1000.0f)

        return juce::String (ms / 1000.0f, 2) + (compact ? "s" : " s") + suffix;

    if (ms >= 100.0f)

        return juce::String (ms, 1) + (compact ? "ms" : " ms") + suffix;

    if (ms >= 1.0f)

        return juce::String (ms, 2) + (compact ? "ms" : " ms") + suffix;

    return juce::String (ms, 3) + (compact ? "ms" : " ms") + suffix;

}


// -- Mod slider ? multiplier conversion (same as ECHO-TR) --

static constexpr double kModCenter  = 0.5;

static constexpr double kModScale   = 3.0;

static constexpr double kModMaxMult = 4.0;

static constexpr double kModMinMult = 0.25;


static double modSliderToMultiplier (double v)

{

    if (v < kModCenter)

        return 1.0 / (kModMaxMult - kModScale * (v / kModCenter));

    return 1.0 + kModScale * ((v - kModCenter) / kModCenter);

}

static juce::String formatModHarmText (double v, bool withSuffix)
{
    return TR::formatModHarmTextShared (v, withSuffix);
}


template <typename Processor>

static bool isModHarmEnabled (Processor& processor) noexcept

{

    if (auto* value = processor.apvts.getRawParameterValue (Processor::kParamModHarm))

        return value->load (std::memory_order_relaxed) > 0.5f;

    return false;

}


template <typename Processor>

static void setModHarmEnabled (Processor& processor, bool shouldBeEnabled)

{

    if (auto* param = processor.apvts.getParameter (Processor::kParamModHarm))

    {

        param->beginChangeGesture();

        param->setValueNotifyingHost (param->convertTo0to1 (shouldBeEnabled ? 1.0f : 0.0f));

        param->endChangeGesture();

    }

}


static juce::String formatModHarmTooltip (bool enabled)
{
    return TR::formatModHarmTooltipShared (enabled);
}

static double multiplierToModSlider (double mult)

{

    mult = juce::jlimit (kModMinMult, kModMaxMult, mult);

    if (mult < 1.0)

        return (kModMaxMult - 1.0 / mult) * kModCenter / kModScale;

    return kModCenter + (mult - 1.0) * kModCenter / kModScale;

}


// -- MIDI channel tooltip --

static juce::String formatMidiChannelTooltip (int ch, int delayMs)
{
    return TR::formatMidiChannelTooltipShared (ch, delayMs, true);
}


static juce::String formatAutoDelayTooltip (int delayMs)
{
    return TR::formatDelayMsTooltipShared (delayMs, true);
}


static juce::String formatTriggerDelayTooltip (int delayMs)
{
    return TR::formatDelayMsTooltipShared (delayMs, true);
}


// -- Parameter listener IDs (shared by ctor + dtor) --

static juce::String formatChaosTooltip (float amountPercent, float speedHz)
{
    return TR::formatChaosTooltipShared (amountPercent, speedHz, GRATRAudioProcessor::kChaosSpdMin, GRATRAudioProcessor::kChaosSpdMax);
}


static constexpr std::array<const char*, 8> kUiMirrorParamIds {

    GRATRAudioProcessor::kParamSync,

    GRATRAudioProcessor::kParamUiPalette,

    GRATRAudioProcessor::kParamUiCrt,

    GRATRAudioProcessor::kParamUiIoFx,

    GRATRAudioProcessor::kParamUiColor0,

    GRATRAudioProcessor::kParamUiColor1,

    GRATRAudioProcessor::kParamUiColor2,

    GRATRAudioProcessor::kParamUiColor3

};


//========================== Editor ==========================


GRATRAudioProcessorEditor::GRATRAudioProcessorEditor (GRATRAudioProcessor& p)

: AudioProcessorEditor (&p), audioProcessor (p)

{

    const std::array<BarSlider*, 13> barSliders { &timeSlider, &modSlider, &pitchSlider, &scanSlider, &smoothSlider, &jitterSlider, &modeSlider, &inputSlider, &outputSlider, &tiltSlider, &panSlider, &mixSlider, &limThresholdSlider };


    useCustomPalette = audioProcessor.getUiUseCustomPalette();

    crtEnabled = audioProcessor.getUiCrtEnabled();

    ioFxEnabled = audioProcessor.getUiIoFxEnabled();

    ioSectionExpanded_ = audioProcessor.getUiIoExpanded();


    for (int i = 0; i < 4; ++i)

        customPalette[(size_t) i] = audioProcessor.getUiCustomPaletteColour (i);


    TR::SimpleEditorLifecycle::initCommon(*this, audioProcessor, lnf, tooltipWindow,
        promptOverlay, resizeConstrainer, resizerCorner, kMinW, kMinH, kMaxW, kMaxH);
    applyActivePalette();
    suppressSizePersistence = true;
    lastPersistedEditorW = getWidth();
    lastPersistedEditorH = getHeight();
    suppressSizePersistence = false;


    {
        auto wireNumeric = [this](juce::Slider& s) { openNumericEntryPopupForSlider(s); };
        auto configure = [&](BarSlider& s, SliderValueFormat fmt, int dec = 0, bool numeric = true) {
            s.setFormat(fmt, dec);
            if (numeric) s.onPopup = wireNumeric;
        };
        configure(timeSlider,        SliderValueFormat::milliseconds, 3);
        configure(modSlider,         SliderValueFormat::plain,        2);
        configure(pitchSlider,       SliderValueFormat::semitones,    2);
        configure(scanSlider,        SliderValueFormat::plain,        0);
        configure(smoothSlider,      SliderValueFormat::plain,        2);
        configure(jitterSlider,      SliderValueFormat::percent,      2);
        configure(modeSlider,        SliderValueFormat::plain,        0, false);
        configure(inputSlider,       SliderValueFormat::gainDb,       1);
        configure(outputSlider,      SliderValueFormat::gainDb,       1);
        configure(tiltSlider,        SliderValueFormat::plain,        1);
        configure(panSlider,         SliderValueFormat::pan,          1);
        configure(mixSlider,         SliderValueFormat::percent,      2);
        configure(limThresholdSlider,SliderValueFormat::plain,        1);
    }

    for (auto* slider : barSliders)
    {
        setupBar (*slider);
        addAndMakeVisible (*slider);
        slider->addListener (this);
    }

    inputSlider.setSkewFactor (GRATRAudioProcessor::kGainSkew);
    outputSlider.setSkewFactor (GRATRAudioProcessor::kGainSkew);


    // IO sliders start hidden (collapsible section)

    TR::setSimpleComponentVisible (inputSlider, false);

    TR::setSimpleComponentVisible (outputSlider, false);

    TR::setSimpleComponentVisible (tiltSlider, false);

    TR::setSimpleComponentVisible (panSlider, false);

    TR::setSimpleComponentVisible (mixSlider, false);

    TR::setSimpleComponentVisible (limThresholdSlider, false);


    filterBar_.setOwner (this);

    filterBar_.setScheme (activeScheme);

    addAndMakeVisible (filterBar_);

    TR::setSimpleComponentVisible (filterBar_, false);

    filterBar_.updateFromProcessor();


    // Chaos filter button + tooltip overlay

    chaosFilterButton.setButtonText ("");

    addAndMakeVisible (chaosFilterButton);

    TR::setSimpleComponentVisible (chaosFilterButton, false);

    {

        const float savedAmtF = audioProcessor.apvts.getRawParameterValue (GRATRAudioProcessor::kParamChaosAmtFilter)->load();

        const float savedSpdF = audioProcessor.apvts.getRawParameterValue (GRATRAudioProcessor::kParamChaosSpdFilter)->load();

        chaosFilterDisplay.setText ("", juce::dontSendNotification);

        chaosFilterDisplay.setInterceptsMouseClicks (true, false);

        chaosFilterDisplay.addMouseListener (this, false);

        chaosFilterDisplay.setTooltip (formatChaosTooltip (savedAmtF, savedSpdF));

        TR::configureSimpleTransparentLabel (chaosFilterDisplay, activeScheme);

        addAndMakeVisible (chaosFilterDisplay);

        TR::setSimpleComponentVisible (chaosFilterDisplay, false);

    }


    // Chaos delay button + tooltip overlay

    chaosDelayButton.setButtonText ("");

    addAndMakeVisible (chaosDelayButton);

    TR::setSimpleComponentVisible (chaosDelayButton, false);

    {

        const float savedAmtD = audioProcessor.apvts.getRawParameterValue (GRATRAudioProcessor::kParamChaosAmt)->load();

        const float savedSpdD = audioProcessor.apvts.getRawParameterValue (GRATRAudioProcessor::kParamChaosSpd)->load();

        chaosDelayDisplay.setText ("", juce::dontSendNotification);

        chaosDelayDisplay.setInterceptsMouseClicks (true, false);

        chaosDelayDisplay.addMouseListener (this, false);

        chaosDelayDisplay.setTooltip (formatChaosTooltip (savedAmtD, savedSpdD));

        TR::configureSimpleTransparentLabel (chaosDelayDisplay, activeScheme);

        addAndMakeVisible (chaosDelayDisplay);

        TR::setSimpleComponentVisible (chaosDelayDisplay, false);

    }


    syncButton.setButtonText ("");

    autoButton.setButtonText ("");

    triggerButton.setButtonText ("");

    midiButton.setButtonText ("");

    reverseButton.setButtonText ("");

    backNForthButton.setButtonText ("");


    addAndMakeVisible (syncButton);

    addAndMakeVisible (autoButton);

    addAndMakeVisible (triggerButton);

    addAndMakeVisible (midiButton);

    addAndMakeVisible (reverseButton);

    addAndMakeVisible (backNForthButton);


    autoDisplay.setText ("", juce::dontSendNotification);

    autoDisplay.setInterceptsMouseClicks (true, false);

    autoDisplay.addMouseListener (this, false);

    autoDisplay.setTooltip (formatAutoDelayTooltip (audioProcessor.getAutoDelayMs()));

    TR::configureSimpleTransparentLabel (autoDisplay, activeScheme);

    addAndMakeVisible (autoDisplay);


    triggerDisplay.setText ("", juce::dontSendNotification);

    triggerDisplay.setInterceptsMouseClicks (true, false);

    triggerDisplay.addMouseListener (this, false);

    triggerDisplay.setTooltip (formatTriggerDelayTooltip (audioProcessor.getTriggerDelayMs()));

    TR::configureSimpleTransparentLabel (triggerDisplay, activeScheme);

    addAndMakeVisible (triggerDisplay);


    // MIDI channel tooltip overlay

    const int savedChannel = audioProcessor.getMidiChannel();

    const int savedMidiDelayMs = audioProcessor.getMidiDelayMs();

    midiChannelDisplay.setText ("", juce::dontSendNotification);

    midiChannelDisplay.setInterceptsMouseClicks (true, false);

    midiChannelDisplay.addMouseListener (this, false);

    midiChannelDisplay.setTooltip (formatMidiChannelTooltip (savedChannel, savedMidiDelayMs));

    TR::configureSimpleTransparentLabel (midiChannelDisplay, activeScheme);

    addAndMakeVisible (midiChannelDisplay);


    auto bindSlider = [&] (std::unique_ptr<SliderAttachment>& attachment,

                           const char* paramId,

                           BarSlider& slider,

                           double defaultValue)

    {

        attachment = std::make_unique<SliderAttachment> (audioProcessor.apvts, paramId, slider);

        slider.setDoubleClickReturnValue (true, defaultValue);

    };


    const bool syncEnabled = audioProcessor.apvts.getRawParameterValue (GRATRAudioProcessor::kParamSync)->load() > 0.5f;

    if (syncEnabled)

    {

        bindSlider (timeSyncAttachment, GRATRAudioProcessor::kParamTimeSync, timeSlider, (double) GRATRAudioProcessor::kTimeSyncDefault);

        timeSlider.setRange ((double) GRATRAudioProcessor::kTimeSyncMin,

                             (double) GRATRAudioProcessor::kTimeSyncMax,

                             1.0);

    }

    else

    {

        bindSlider (timeAttachment, GRATRAudioProcessor::kParamTimeMs, timeSlider, kDefaultTimeMs);

    }


    bindSlider (modAttachment, GRATRAudioProcessor::kParamMod, modSlider, (double) GRATRAudioProcessor::kModDefault);

    bindSlider (pitchAttachment, GRATRAudioProcessor::kParamPitch, pitchSlider, (double) GRATRAudioProcessor::kPitchDefault);

    bindSlider (scanAttachment, GRATRAudioProcessor::kParamScan, scanSlider, (double) GRATRAudioProcessor::kScanDefault);

    bindSlider (smoothAttachment, GRATRAudioProcessor::kParamSmooth, smoothSlider, kDefaultSmooth);

    bindSlider (jitterAttachment, GRATRAudioProcessor::kParamJitter, jitterSlider, kDefaultJitter);

    bindSlider (modeAttachment, GRATRAudioProcessor::kParamMode, modeSlider, 0.0);

    bindSlider (inputAttachment, GRATRAudioProcessor::kParamInput, inputSlider, kDefaultInput);

    bindSlider (outputAttachment, GRATRAudioProcessor::kParamOutput, outputSlider, kDefaultOutput);

    bindSlider (tiltAttachment, GRATRAudioProcessor::kParamTilt, tiltSlider, kDefaultTilt);

    bindSlider (panAttachment, GRATRAudioProcessor::kParamPan, panSlider, 0.5);

    bindSlider (mixAttachment, GRATRAudioProcessor::kParamMix, mixSlider, kDefaultMix);

    bindSlider (limThresholdAttachment, GRATRAudioProcessor::kParamLimThreshold, limThresholdSlider, kDefaultLimThreshold);


    // Mode In / Mode Out / Sum Bus combos

    {

        auto setupModeCombo = [this] (juce::ComboBox& combo)

        {

            addAndMakeVisible (combo);

            combo.addItem ("L+R",  1);

            combo.addItem ("M/S",  2);

            combo.addItem ("MID",  3);

            combo.addItem ("SIDE", 4);

            TR::centreSimpleCombo (combo);

            combo.setLookAndFeel (&lnf);

            TR::setSimpleComponentVisible (combo, false);

        };

        setupModeCombo (modeInCombo);

        setupModeCombo (modeOutCombo);


        addAndMakeVisible (sumBusCombo);

        sumBusCombo.addItem ("ST",              1);

        sumBusCombo.addItem (juce::String::fromUTF8 (u8"\u2192M"), 2);

        sumBusCombo.addItem (juce::String::fromUTF8 (u8"\u2192S"), 3);

        TR::centreSimpleCombo (sumBusCombo);

        sumBusCombo.setLookAndFeel (&lnf);

        TR::setSimpleComponentVisible (sumBusCombo, false);


        addAndMakeVisible (limModeCombo);

        limModeCombo.addItem ("NONE", 1);

        limModeCombo.addItem ("WET",  2);

        limModeCombo.addItem ("GLOBAL", 3);

        TR::centreSimpleCombo (limModeCombo);

        limModeCombo.setLookAndFeel (&lnf);

        TR::setSimpleComponentVisible (limModeCombo, false);


        // Invert Polarity / Invert Stereo combos

        {

            auto setupInvCombo = [this] (juce::ComboBox& combo)

            {

                addAndMakeVisible (combo);

                combo.addItem ("NONE",   1);

                combo.addItem ("WET",    2);

                combo.addItem ("GLOBAL", 3);

                TR::centreSimpleCombo (combo);

                combo.setLookAndFeel (&lnf);

                TR::setSimpleComponentVisible (combo, false);

            };

            setupInvCombo (invPolCombo);

            setupInvCombo (invStrCombo);

        }


        // Mix Mode combo (INSERT / SEND)

        {

            addAndMakeVisible (mixModeCombo);

            mixModeCombo.addItem ("INSERT", 1);

            mixModeCombo.addItem ("SEND",   2);

            TR::centreSimpleCombo (mixModeCombo);

            mixModeCombo.setLookAndFeel (&lnf);

            TR::setSimpleComponentVisible (mixModeCombo, false);

        }


        // Filter Position combo (POST / PRE)

        {

            addAndMakeVisible (filterPosCombo);

        filterPosCombo.addItem (juce::String::fromUTF8 (u8"F\u25bc T\u25bc"), 1);

        filterPosCombo.addItem (juce::String::fromUTF8 (u8"F\u25b2 T\u25b2"), 2);

        filterPosCombo.addItem (juce::String::fromUTF8 (u8"F\u25b2 T\u25bc"), 3);

        filterPosCombo.addItem (juce::String::fromUTF8 (u8"F\u25bc T\u25b2"), 4);

            TR::centreSimpleCombo (filterPosCombo);

            filterPosCombo.setLookAndFeel (&lnf);

            TR::setSimpleComponentVisible (filterPosCombo, false);

        }


        // Dual Mix Bar (SEND mode)

        addAndMakeVisible (dualMixBar_);

        dualMixBar_.setOwner (this);

        TR::setSimpleComponentVisible (dualMixBar_, false);


        modeInAttachment  = std::make_unique<ComboBoxAttachment> (audioProcessor.apvts, GRATRAudioProcessor::kParamModeIn,  modeInCombo);

        modeOutAttachment = std::make_unique<ComboBoxAttachment> (audioProcessor.apvts, GRATRAudioProcessor::kParamModeOut, modeOutCombo);

        sumBusAttachment  = std::make_unique<ComboBoxAttachment> (audioProcessor.apvts, GRATRAudioProcessor::kParamSumBus,  sumBusCombo);

        limModeAttachment = std::make_unique<ComboBoxAttachment> (audioProcessor.apvts, GRATRAudioProcessor::kParamLimMode, limModeCombo);

        invPolAttachment  = std::make_unique<ComboBoxAttachment> (audioProcessor.apvts, GRATRAudioProcessor::kParamInvPol,  invPolCombo);

        invStrAttachment  = std::make_unique<ComboBoxAttachment> (audioProcessor.apvts, GRATRAudioProcessor::kParamInvStr,  invStrCombo);

        mixModeAttachment = std::make_unique<ComboBoxAttachment> (audioProcessor.apvts, GRATRAudioProcessor::kParamMixMode, mixModeCombo);

        filterPosAttachment = std::make_unique<ComboBoxAttachment> (audioProcessor.apvts, GRATRAudioProcessor::kParamFilterPos, filterPosCombo);

    }


    // STYLE is a discrete/model control; the rest keep numeric prompts.




    auto bindButton = [&] (std::unique_ptr<ButtonAttachment>& attachment,

                           const char* paramId,

                           juce::Button& button)

    {

        attachment = std::make_unique<ButtonAttachment> (audioProcessor.apvts, paramId, button);

    };


    bindButton (syncAttachment, GRATRAudioProcessor::kParamSync, syncButton);

    bindButton (autoAttachment, GRATRAudioProcessor::kParamAuto, autoButton);

    bindButton (triggerAttachment, GRATRAudioProcessor::kParamTrigger, triggerButton);

    bindButton (midiAttachment, GRATRAudioProcessor::kParamMidi, midiButton);

    bindButton (reverseAttachment, GRATRAudioProcessor::kParamReverse, reverseButton);

    bindButton (backNForthAttachment, GRATRAudioProcessor::kParamBackNForth, backNForthButton);

    bindButton (chaosFilterAttachment, GRATRAudioProcessor::kParamChaos, chaosFilterButton);

    bindButton (chaosDelayAttachment, GRATRAudioProcessor::kParamChaosD, chaosDelayButton);


    for (auto* paramId : kUiMirrorParamIds)

        audioProcessor.apvts.addParameterListener (paramId, this);

    TR::SimpleEditorLifecycle::scheduleUiRestore (*this);

    applyCrtState (crtEnabled);


    refreshLegendTextCache();

    resized();

}


GRATRAudioProcessorEditor::~GRATRAudioProcessorEditor()

{

    setComponentEffect (nullptr);

    stopTimer();


    for (auto* paramId : kUiMirrorParamIds)

        audioProcessor.apvts.removeParameterListener (paramId, this);


    audioProcessor.setUiUseCustomPalette (useCustomPalette);

    audioProcessor.setUiCrtEnabled (crtEnabled);

    audioProcessor.setUiIoFxEnabled (ioFxEnabled);


    dismissEditorOwnedModalPrompts (lnf);

    setPromptOverlayActive (false);


    const std::array<BarSlider*, 13> barSliders { &timeSlider, &modSlider, &pitchSlider, &scanSlider, &smoothSlider, &jitterSlider, &modeSlider, &inputSlider, &outputSlider, &tiltSlider, &panSlider, &mixSlider, &limThresholdSlider };

    for (auto* slider : barSliders)

        slider->removeListener (this);


    if (tooltipWindow != nullptr)

        tooltipWindow->setLookAndFeel (nullptr);


    modeInCombo.setLookAndFeel (nullptr);

    modeOutCombo.setLookAndFeel (nullptr);

    sumBusCombo.setLookAndFeel (nullptr);

    limModeCombo.setLookAndFeel (nullptr);

    invPolCombo.setLookAndFeel (nullptr);

    invStrCombo.setLookAndFeel (nullptr);

    mixModeCombo.setLookAndFeel (nullptr);

    filterPosCombo.setLookAndFeel (nullptr);


    setLookAndFeel (nullptr);

}


void GRATRAudioProcessorEditor::applyActivePalette() {
    const auto& palette = useCustomPalette ? customPalette : defaultPalette;
    activeScheme = TR::applySimplePalette(palette, lnf,
        { &chaosFilterDisplay, &chaosDelayDisplay, &autoDisplay, &triggerDisplay, &midiChannelDisplay },
        { &timeSlider, &modSlider, &pitchSlider, &scanSlider, &smoothSlider, &jitterSlider, &modeSlider, &inputSlider, &outputSlider, &tiltSlider, &panSlider, &mixSlider, &limThresholdSlider },
        { &modeInCombo, &modeOutCombo, &sumBusCombo, &limModeCombo, &invPolCombo, &invStrCombo, &mixModeCombo, &filterPosCombo });
    filterBar_.setScheme(activeScheme);
    dualMixBar_.setScheme(activeScheme);
    updateIoFxMeterSliders();
}


void GRATRAudioProcessorEditor::applyCrtState (bool enabled) {
    crtEnabled = enabled;
    TR::SimpleUIController::applyCrt(crtEnabled, *this, *this, crtEffect, crtTime, kCrtTimerHz, kIdleTimerHz);
}


void GRATRAudioProcessorEditor::applyIoFxState (bool enabled)

{

    ioFxEnabled = enabled;

    updateIoFxMeterSliders();

}


void GRATRAudioProcessorEditor::updateIoFxMeterSliders() {
    TR::SimpleUIController::updateIoMeters(defaultPalette, customPalette, useCustomPalette,
        inputSlider, outputSlider, ioFxEnabled,
        lastInputSignalMs, lastOutputSignalMs,
        audioProcessor.getInputMeterPeak(), audioProcessor.getOutputMeterPeak());
}


void GRATRAudioProcessorEditor::applyLabelTextColour (juce::Label& label, juce::Colour colour)

{

    TR::applySimpleLabelTextColour (label, colour);

}


void GRATRAudioProcessorEditor::sliderValueChanged (juce::Slider* slider)

{

    auto isBarSlider = [&] (const juce::Slider* s)

    {

        return s == &timeSlider || s == &modSlider || s == &pitchSlider || s == &scanSlider || s == &smoothSlider

            || s == &jitterSlider || s == &modeSlider || s == &inputSlider || s == &outputSlider || s == &tiltSlider

            || s == &panSlider || s == &mixSlider || s == &limThresholdSlider;

    };


    refreshLegendTextCache();


    if (slider == nullptr)

    {

        repaint();

        return;

    }


    if (isBarSlider (slider))

    {

        repaint (getRowRepaintBounds (*slider));

        return;

    }


    repaint();

}


void GRATRAudioProcessorEditor::setPromptOverlayActive (bool shouldBeActive) {
    TR::SimpleUIController::setOverlayActive(*this, promptOverlay, promptOverlayActive, shouldBeActive, lnf);
}


void GRATRAudioProcessorEditor::moved() {
    TR::SimpleUIController::anchorPromptsOnMove (*this, promptOverlayActive, promptOverlay, lnf);
}


void GRATRAudioProcessorEditor::parentHierarchyChanged() {
    TR::SimpleUIController::darkenWindowBackground_Hwnd (*this);
}


void GRATRAudioProcessorEditor::parameterChanged (const juce::String& parameterID, float newValue)

{

    if (parameterID == GRATRAudioProcessor::kParamSync)

    {

        const bool syncEnabled = newValue > 0.5f;

        juce::Component::SafePointer<GRATRAudioProcessorEditor> safeThis (this);

        juce::MessageManager::callAsync ([safeThis, syncEnabled]()

        {

            if (safeThis == nullptr)

                return;

            safeThis->updateTimeSliderForSyncMode (syncEnabled);

            safeThis->refreshLegendTextCache();

            safeThis->repaint();

        });

        return;

    }

    

    const bool isSizeParam = parameterID == GRATRAudioProcessor::kParamUiWidth

                         || parameterID == GRATRAudioProcessor::kParamUiHeight;


    const bool isUiVisualParam = parameterID == GRATRAudioProcessor::kParamUiPalette

                             || parameterID == GRATRAudioProcessor::kParamUiCrt

                             || parameterID == GRATRAudioProcessor::kParamUiIoFx

                             || parameterID == GRATRAudioProcessor::kParamUiColor0

                             || parameterID == GRATRAudioProcessor::kParamUiColor1

                             || parameterID == GRATRAudioProcessor::kParamUiColor2

                             || parameterID == GRATRAudioProcessor::kParamUiColor3;


    if (! isSizeParam && ! isUiVisualParam)

        return;


    juce::Component::SafePointer<GRATRAudioProcessorEditor> safeThis (this);

    juce::MessageManager::callAsync ([safeThis, isSizeParam]()

    {

        if (safeThis == nullptr)

            return;


        if (isSizeParam)

            safeThis->applyPersistedUiStateFromProcessor (true, false);

        else

            safeThis->applyPersistedUiStateFromProcessor (false, true);

    });

}


void GRATRAudioProcessorEditor::timerCallback()

{

    updateIoFxMeterSliders();


    if (suppressSizePersistence)

        return;


    const auto newMidiDisplay = audioProcessor.getCurrentTimeDisplay();

    const bool timeSliderHeld = timeSlider.isMouseButtonDown();

    if (newMidiDisplay != cachedMidiDisplay || timeSliderHeld != cachedTimeSliderHeld)

    {

        cachedMidiDisplay = newMidiDisplay;

        cachedTimeSliderHeld = timeSliderHeld;

        if (refreshLegendTextCache())

            updateCachedLayout();

        repaint (getRowRepaintBounds (timeSlider));

    }


    const int w = getWidth();

    const int h = getHeight();


    const uint32_t last = lastUserInteractionMs.load (std::memory_order_relaxed);

    const uint32_t now = juce::Time::getMillisecondCounter();

    const bool userRecent = (now - last) <= (uint32_t) kUserInteractionPersistWindowMs;


    if ((w != lastPersistedEditorW || h != lastPersistedEditorH) && userRecent)

    {

        audioProcessor.setUiEditorSize (w, h);

        lastPersistedEditorW = w;

        lastPersistedEditorH = h;

    }


    if (crtEnabled && w > 0 && h > 0)

    {

        crtTime += 0.1f;

        crtEffect.setTime (crtTime);


        const bool anySliderDragging = timeSlider.isMouseButtonDown()

                                    || pitchSlider.isMouseButtonDown()

                                    || modeSlider.isMouseButtonDown()

                                    || modSlider.isMouseButtonDown()

                                    || jitterSlider.isMouseButtonDown()

                                    || smoothSlider.isMouseButtonDown()

                                    || inputSlider.isMouseButtonDown()

                                    || outputSlider.isMouseButtonDown()

                                    || mixSlider.isMouseButtonDown();

        if (! anySliderDragging)

            repaint();

    }


    if (filterBar_.isVisible())

        filterBar_.updateFromProcessor();


    // Keep dual mix bar markers up to date + visibility swap

    if (ioSectionExpanded_)

    {

        const float prevDry = dualMixBar_.getDryLevel();

        const float prevWet = dualMixBar_.getWetLevel();

        dualMixBar_.updateFromProcessor();

        const bool isSendMode = mixModeCombo.getSelectedId() == 2;


        // Refresh legend when levels change in SEND mode

        if (isSendMode && (dualMixBar_.getDryLevel() != prevDry || dualMixBar_.getWetLevel() != prevWet))

        {

            if (refreshLegendTextCache())

                updateCachedLayout();

            repaint();

        }


        if (mixSlider.isVisible() == isSendMode)

        {

            TR::setSimpleComponentVisible (mixSlider, ! isSendMode);

            TR::setSimpleComponentVisible (dualMixBar_, isSendMode);

            if (refreshLegendTextCache())

                updateCachedLayout();

            repaint();

        }

    }

    else

    {

        if (dualMixBar_.isVisible())

            dualMixBar_.updateFromProcessor();


        const bool isSend = (mixModeCombo.getSelectedItemIndex() == 1);

        if (isSend && mixSlider.isVisible())

        {

            TR::setSimpleComponentVisible (mixSlider, false);

            TR::setSimpleComponentVisible (dualMixBar_, true);

        }

        else if (! isSend && dualMixBar_.isVisible())

        {

            TR::setSimpleComponentVisible (dualMixBar_, false);

            TR::setSimpleComponentVisible (mixSlider, true);

        }

    }

}


void GRATRAudioProcessorEditor::applyPersistedUiStateFromProcessor (bool applySize, bool applyPaletteAndFx)

{

    if (applySize)

    {

        const int targetW = juce::jlimit (kMinW, kMaxW, audioProcessor.getUiEditorWidth());

        const int targetH = juce::jlimit (kMinH, kMaxH, audioProcessor.getUiEditorHeight());


        if (getWidth() != targetW || getHeight() != targetH)

        {

            suppressSizePersistence = true;

            setSize (targetW, targetH);

            suppressSizePersistence = false;

        }

    }


    if (applyPaletteAndFx)

    {

        bool paletteChanged = false;

        for (int i = 0; i < 4; ++i)

        {

            const auto c = audioProcessor.getUiCustomPaletteColour (i);

            if (customPalette[(size_t) i].getARGB() != c.getARGB())

            {

                customPalette[(size_t) i] = c;

                paletteChanged = true;

            }

        }


        const bool targetUseCustomPalette = audioProcessor.getUiUseCustomPalette();

        const bool targetCrtEnabled = audioProcessor.getUiCrtEnabled();

        const bool targetIoFxEnabled = audioProcessor.getUiIoFxEnabled();


        const bool paletteSwitchChanged = (useCustomPalette != targetUseCustomPalette);

        const bool fxChanged = (crtEnabled != targetCrtEnabled);

        const bool ioFxChanged = (ioFxEnabled != targetIoFxEnabled);


        const bool targetIoExpanded = audioProcessor.getUiIoExpanded();

        const bool ioChanged = (ioSectionExpanded_ != targetIoExpanded);

        if (ioChanged)

        {

            ioSectionExpanded_ = targetIoExpanded;

            resized();

        }


        if (paletteSwitchChanged)

            useCustomPalette = targetUseCustomPalette;


        if (fxChanged)

            applyCrtState (targetCrtEnabled);

        if (ioFxChanged)

            applyIoFxState (targetIoFxEnabled);


        if (paletteChanged || paletteSwitchChanged)

            applyActivePalette();


        if (paletteChanged || paletteSwitchChanged || fxChanged || ioFxChanged || ioChanged)

            repaint();

    }

}


void GRATRAudioProcessorEditor::updateTimeSliderForSyncMode (bool syncEnabled)

{

    auto posInfo = audioProcessor.getPlayHead();

    double bpm = 120.0;

    if (posInfo != nullptr)

    {

        auto pos = posInfo->getPosition();

        if (pos.hasValue() && pos->getBpm().hasValue())

            bpm = *pos->getBpm();

    }

    

    if (syncEnabled)

    {

        const float currentMs = static_cast<float> (timeSlider.getValue());

        

        int bestSyncIndex = GRATRAudioProcessor::kTimeSyncDefault;

        float bestDiff = std::abs (currentMs - audioProcessor.tempoSyncToMs (bestSyncIndex, bpm));

        

        for (int i = GRATRAudioProcessor::kTimeSyncMin; i <= GRATRAudioProcessor::kTimeSyncMax; ++i)

        {

            const float syncMs = audioProcessor.tempoSyncToMs (i, bpm);

            const float diff = std::abs (currentMs - syncMs);

            if (diff < bestDiff)

            {

                bestDiff = diff;

                bestSyncIndex = i;

            }

        }

        

        timeAttachment.reset();

        timeSyncAttachment = std::make_unique<SliderAttachment> (audioProcessor.apvts, 

                                                                  GRATRAudioProcessor::kParamTimeSync, 

                                                                  timeSlider);

        timeSlider.setRange ((double) GRATRAudioProcessor::kTimeSyncMin,

                             (double) GRATRAudioProcessor::kTimeSyncMax,

                             1.0);

        timeSlider.setDoubleClickReturnValue (true, (double) GRATRAudioProcessor::kTimeSyncDefault);

        

        if (auto* param = audioProcessor.apvts.getParameter (GRATRAudioProcessor::kParamTimeSync))

            param->setValueNotifyingHost (param->convertTo0to1 ((float) bestSyncIndex));

    }

    else

    {

        const int currentSyncIndex = (int) timeSlider.getValue();

        const float targetMs = audioProcessor.tempoSyncToMs (currentSyncIndex, bpm);

        

        timeSyncAttachment.reset();

        timeAttachment = std::make_unique<SliderAttachment> (audioProcessor.apvts, 

                                                              GRATRAudioProcessor::kParamTimeMs, 

                                                              timeSlider);

        timeSlider.setRange (GRATRAudioProcessor::kTimeMsMin, 

                            GRATRAudioProcessor::kTimeMsMax, 

                            0.0);

        timeSlider.setDoubleClickReturnValue (true, kDefaultTimeMs);

        

        if (auto* param = audioProcessor.apvts.getParameter (GRATRAudioProcessor::kParamTimeMs))

            param->setValueNotifyingHost (param->convertTo0to1 (targetMs));

    }

}


bool GRATRAudioProcessorEditor::refreshLegendTextCache()

{

    const auto oldTimeFull      = cachedTimeTextFull;

    const auto oldTimeShort     = cachedTimeTextShort;

    const auto oldPitchFull     = cachedPitchTextFull;

    const auto oldPitchShort    = cachedPitchTextShort;

    const auto oldModeFull      = cachedModeTextFull;

    const auto oldModeShort     = cachedModeTextShort;

    const auto oldScanFull      = cachedScanTextFull;

    const auto oldScanShort     = cachedScanTextShort;

    const auto oldJitterFull    = cachedJitterTextFull;

    const auto oldJitterShort   = cachedJitterTextShort;

    const auto oldSmoothFull    = cachedSmoothTextFull;

    const auto oldSmoothShort   = cachedSmoothTextShort;

    const auto oldModFull       = cachedModTextFull;

    const auto oldModShort      = cachedModTextShort;

    const auto oldInputFull     = cachedInputTextFull;

    const auto oldInputShort    = cachedInputTextShort;

    const auto oldOutputFull    = cachedOutputTextFull;

    const auto oldOutputShort   = cachedOutputTextShort;

    const auto oldMixFull       = cachedMixTextFull;

    const auto oldMixShort      = cachedMixTextShort;

    const auto oldTiltFull      = cachedTiltTextFull;

    const auto oldTiltShort     = cachedTiltTextShort;

    const auto oldPanFull       = cachedPanTextFull;

    const auto oldPanShort      = cachedPanTextShort;

    const auto oldLimFull       = cachedLimThresholdTextFull;

    const auto oldLimShort      = cachedLimThresholdTextShort;


    cachedTimeTextFull = getTimeText();

    cachedTimeTextShort = getTimeTextShort();

    cachedPitchTextFull = getPitchText();

    cachedPitchTextShort = getPitchTextShort();

    cachedModeTextFull = getModeText();

    cachedModeTextShort = getModeTextShort();

    cachedScanTextFull = getScanText();

    cachedScanTextShort = getScanTextShort();

    cachedJitterTextFull = getJitterText();

    cachedJitterTextShort = getJitterTextShort();

    cachedSmoothTextFull = getSmoothText();

    cachedSmoothTextShort = getSmoothTextShort();

    cachedModTextFull = getModText();

    cachedModTextShort = getModTextShort();

    cachedInputTextFull = getInputText();

    cachedInputTextShort = getInputTextShort();

    cachedOutputTextFull = getOutputText();

    cachedOutputTextShort = getOutputTextShort();

    cachedMixTextFull = getMixText();

    cachedMixTextShort = getMixTextShort();

    cachedTiltTextFull = getTiltText();

    cachedTiltTextShort = getTiltTextShort();


    // Cached int-only representations

    {

        if (cachedMidiDisplay.isNotEmpty() && !cachedTimeSliderHeld)

            cachedTimeIntOnly = cachedMidiDisplay;

        else if (audioProcessor.apvts.getRawParameterValue (GRATRAudioProcessor::kParamSync)->load() > 0.5f)

            cachedTimeIntOnly = juce::String ((int) timeSlider.getValue());

        else

            cachedTimeIntOnly = formatTimeMsForDisplay ((float) timeSlider.getValue(), false, true);


        {

            const float mult = (float) modSliderToMultiplier (modSlider.getValue());

            if (isModHarmEnabled (audioProcessor))

                cachedModIntOnly = formatModHarmText (modSlider.getValue(), false);

            else if (std::abs (mult - 1.0f) < kMultEpsilon)

                cachedModIntOnly = "X1";

            else

                cachedModIntOnly = "X" + juce::String (mult, 2);

        }


    const float pitchSt = std::round ((float) pitchSlider.getValue() * 100.0f) / 100.0f;

    if (pitchSt > 0.0f)

        cachedPitchIntOnly = "+" + juce::String (pitchSt, 2) + "st";

    else

        cachedPitchIntOnly = juce::String (pitchSt, 2) + "st";


        const int scanPct = (int) std::lround (scanSlider.getValue());

        if (scanPct > 0)

            cachedScanIntOnly = "+" + juce::String (scanPct) + "%";

        else

            cachedScanIntOnly = juce::String (scanPct) + "%";

        cachedJitterIntOnly = juce::String ((int) std::lround (jitterSlider.getValue() * 100.0)) + "%";

        cachedSmoothIntOnly  = juce::String ((int) std::lround (smoothSlider.getValue())) + "%";

        cachedModeIntOnly    = juce::String ((int) modeSlider.getValue());

        cachedInputIntOnly   = formatGainFaderDbCompact ((float) inputSlider.getValue());

        cachedOutputIntOnly  = formatGainFaderDbCompact ((float) outputSlider.getValue());


        if (mixModeCombo.getSelectedId() == 2)

        {

            const bool isDry = (dualMixBar_.getLastTouched() != DualMixBarComponent::WET);

            const float level = isDry ? dualMixBar_.getDryLevel() : dualMixBar_.getWetLevel();

            const float dB = (level <= 0.0001f) ? -100.0f : 20.0f * std::log10 (level);

            const juce::String suffix = isDry ? " DRY" : " WET";

            if (dB <= -100.0f) cachedMixIntOnly = "-INF" + suffix;

            else if (std::abs (dB) < 0.05f) cachedMixIntOnly = "0.0dB" + suffix;

            else cachedMixIntOnly = juce::String (dB, 1) + "dB" + suffix;

        }

        else

        {

            cachedMixIntOnly = juce::String ((int) std::lround (mixSlider.getValue() * 100.0)) + "%";

        }


        const float tiltVal = (float) tiltSlider.getValue();

        if (std::abs (tiltVal) < 0.05f)

            cachedTiltIntOnly = "0.0dB";

        else

            cachedTiltIntOnly = juce::String (tiltVal, 1) + "dB";

    }


    cachedFilterTextFull  = "FILTER";

    cachedFilterTextShort = "FLTR";


    cachedPanTextFull  = getPanText();

    cachedPanTextShort = getPanTextShort();


    cachedLimThresholdTextFull  = getLimThresholdText();

    cachedLimThresholdTextShort = getLimThresholdTextShort();

    {

        const float limVal = (float) limThresholdSlider.getValue();

        if (std::abs (limVal) < 0.05f)

            cachedLimThresholdIntOnly = "0.0dB";

        else

            cachedLimThresholdIntOnly = juce::String (limVal, 1) + "dB";

    }


    {

        const float panVal = (float) panSlider.getValue();

        const int panPct = (int) std::lround (panVal * 100.0);

        if (panPct == 0)

            cachedPanIntOnly = "C";

        else if (panPct < 0)

            cachedPanIntOnly = juce::String (-panPct) + "L";

        else

            cachedPanIntOnly = juce::String (panPct) + "R";

    }


    const bool changed = oldTimeFull      != cachedTimeTextFull

                      || oldTimeShort     != cachedTimeTextShort

                      || oldPitchFull     != cachedPitchTextFull

                      || oldPitchShort    != cachedPitchTextShort

                      || oldModeFull      != cachedModeTextFull

                      || oldModeShort     != cachedModeTextShort

                      || oldScanFull      != cachedScanTextFull

                      || oldScanShort     != cachedScanTextShort

                      || oldJitterFull    != cachedJitterTextFull

                      || oldJitterShort   != cachedJitterTextShort

                      || oldSmoothFull    != cachedSmoothTextFull

                      || oldSmoothShort   != cachedSmoothTextShort

                      || oldModFull       != cachedModTextFull

                      || oldModShort      != cachedModTextShort

                      || oldInputFull     != cachedInputTextFull

                      || oldInputShort    != cachedInputTextShort

                      || oldOutputFull    != cachedOutputTextFull

                      || oldOutputShort   != cachedOutputTextShort

                      || oldMixFull       != cachedMixTextFull

                      || oldMixShort      != cachedMixTextShort

                      || oldTiltFull      != cachedTiltTextFull

                      || oldTiltShort     != cachedTiltTextShort

                      || oldPanFull       != cachedPanTextFull

                      || oldPanShort      != cachedPanTextShort

                      || oldLimFull       != cachedLimThresholdTextFull

                      || oldLimShort      != cachedLimThresholdTextShort;


    return changed;

}


juce::Rectangle<int> GRATRAudioProcessorEditor::getRowRepaintBounds (const juce::Slider& s) const

{

    auto bounds = s.getBounds().getUnion (getValueAreaFor (s.getBounds()));

    return bounds.expanded (8, 8).getIntersection (getLocalBounds());

}


void GRATRAudioProcessorEditor::setupBar (juce::Slider& s)

{

    s.setSliderStyle (juce::Slider::LinearBar);

    s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);

    s.setPopupDisplayEnabled (false, false, this);

    s.setTooltip (juce::String());

    s.setPopupMenuEnabled (false);

    TR::applySimpleTransparentSliderColours (s, activeScheme);

}


//========================== Text helpers ==========================


juce::String GRATRAudioProcessorEditor::getTimeText() const

{

    if (cachedMidiDisplay.isNotEmpty() && ! timeSlider.isMouseButtonDown())

        return cachedMidiDisplay;


    const bool isSyncOn = syncButton.getToggleState();

    if (isSyncOn)

    {

        const int idx = (int) timeSlider.getValue();

        return audioProcessor.getTimeSyncName (idx);

    }

    

    const float ms = (float) timeSlider.getValue();

    return formatTimeMsForDisplay (ms, true, false);

}


juce::String GRATRAudioProcessorEditor::getTimeTextShort() const

{

    if (cachedMidiDisplay.isNotEmpty() && ! timeSlider.isMouseButtonDown())

        return cachedMidiDisplay;


    const bool isSyncOn = syncButton.getToggleState();

    if (isSyncOn)

    {

        const int idx = (int) timeSlider.getValue();

        return audioProcessor.getTimeSyncName (idx);

    }

    

    const float ms = (float) timeSlider.getValue();

    return formatTimeMsForDisplay (ms, false, true) + " TIME";

}


juce::String GRATRAudioProcessorEditor::getPitchText() const

{

    const float st = std::round ((float) pitchSlider.getValue() * 100.0f) / 100.0f;

    if (st > 0.0f) return "+" + juce::String (st, 2) + " st PITCH";

    return juce::String (st, 2) + " st PITCH";

}


juce::String GRATRAudioProcessorEditor::getPitchTextShort() const

{

    const float st = std::round ((float) pitchSlider.getValue() * 100.0f) / 100.0f;

    if (st > 0.0f) return "+" + juce::String (st, 2) + "st";

    return juce::String (st, 2) + "st";

}


juce::String GRATRAudioProcessorEditor::getScanText() const

{

    const int pct = (int) std::lround (scanSlider.getValue());

    if (pct > 0) return "+" + juce::String (pct) + "% SCAN";

    return juce::String (pct) + "% SCAN";

}


juce::String GRATRAudioProcessorEditor::getScanTextShort() const

{

    const int pct = (int) std::lround (scanSlider.getValue());

    if (pct > 0) return "+" + juce::String (pct) + "% SCAN";

    return juce::String (pct) + "% SCAN";

}


juce::String GRATRAudioProcessorEditor::getJitterText() const

{

    return juce::String ((int) std::lround (jitterSlider.getValue() * 100.0)) + "% JITTER";

}


juce::String GRATRAudioProcessorEditor::getJitterTextShort() const

{

    return juce::String ((int) std::lround (jitterSlider.getValue() * 100.0)) + "% JIT";

}


juce::String GRATRAudioProcessorEditor::getSmoothText() const

{

    return juce::String ((int) std::lround (smoothSlider.getValue())) + "% SMOOTH";

}


juce::String GRATRAudioProcessorEditor::getSmoothTextShort() const

{

    return juce::String ((int) std::lround (smoothSlider.getValue())) + "% SMTH";

}


juce::String GRATRAudioProcessorEditor::getModeText() const

{

    const int mode = (int) modeSlider.getValue();

    switch (mode)

    {

        case 0: return "MONO STYLE";

        case 1: return "STEREO STYLE";

        case 2: return "WIDE STYLE";

        case 3: return "DUAL STYLE";

        default: return "STEREO STYLE";

    }

}


juce::String GRATRAudioProcessorEditor::getModeTextShort() const

{

    const int mode = (int) modeSlider.getValue();

    switch (mode)

    {

        case 0: return "MONO";

        case 1: return "STEREO";

        case 2: return "WIDE";

        case 3: return "DUAL";

        default: return "STEREO";

    }

}


juce::String GRATRAudioProcessorEditor::getModText() const

{

    if (isModHarmEnabled (audioProcessor))

        return formatModHarmText (modSlider.getValue(), true);


    const float mult = (float) modSliderToMultiplier (modSlider.getValue());

    if (std::abs (mult - 1.0f) < kMultEpsilon)

        return "X1 MOD";

    return "X" + juce::String (mult, 2) + " MOD";

}


juce::String GRATRAudioProcessorEditor::getModTextShort() const

{

    if (isModHarmEnabled (audioProcessor))

        return formatModHarmText (modSlider.getValue(), false);


    const float mult = (float) modSliderToMultiplier (modSlider.getValue());

    if (std::abs (mult - 1.0f) < kMultEpsilon)

        return "X1 MOD";

    return "X" + juce::String (mult, 2) + " MOD";

}


juce::String GRATRAudioProcessorEditor::getInputText() const

{

    const float db = (float) inputSlider.getValue();

    return formatGainFaderDb (db) + " INPUT";

}


juce::String GRATRAudioProcessorEditor::getInputTextShort() const

{

    const float db = (float) inputSlider.getValue();

    return formatGainFaderDb (db) + " IN";

}


juce::String GRATRAudioProcessorEditor::getOutputText() const

{

    const float db = (float) outputSlider.getValue();

    return formatGainFaderDb (db) + " OUTPUT";

}


juce::String GRATRAudioProcessorEditor::getOutputTextShort() const

{

    const float db = (float) outputSlider.getValue();

    return formatGainFaderDb (db) + " OUT";

}


juce::String GRATRAudioProcessorEditor::getMixText() const

{

    if (mixModeCombo.getSelectedId() == 2)

    {

        const bool isDry = (dualMixBar_.getLastTouched() != DualMixBarComponent::WET);

        const float level = isDry ? dualMixBar_.getDryLevel() : dualMixBar_.getWetLevel();

        const float dB = (level <= 0.0001f) ? -100.0f : 20.0f * std::log10 (level);

        const juce::String suffix = isDry ? " DRY" : " WET";

        if (dB <= -100.0f) return "-INF dB" + suffix;

        if (std::abs (dB) < 0.05f) return "0.0 dB" + suffix;

        return juce::String (dB, 1) + " dB" + suffix;

    }

    const int pct = (int) std::lround (mixSlider.getValue() * 100.0);

    return juce::String (pct) + "% MIX";

}


juce::String GRATRAudioProcessorEditor::getMixTextShort() const

{

    if (mixModeCombo.getSelectedId() == 2)

    {

        const bool isDry = (dualMixBar_.getLastTouched() != DualMixBarComponent::WET);

        const float level = isDry ? dualMixBar_.getDryLevel() : dualMixBar_.getWetLevel();

        const float dB = (level <= 0.0001f) ? -100.0f : 20.0f * std::log10 (level);

        const juce::String suffix = isDry ? " DRY" : " WET";

        if (dB <= -100.0f) return "-INF" + suffix;

        if (std::abs (dB) < 0.05f) return "0.0dB" + suffix;

        return juce::String (dB, 1) + "dB" + suffix;

    }

    const int pct = (int) std::lround (mixSlider.getValue() * 100.0);

    return juce::String (pct) + "% MIX";

}


juce::String GRATRAudioProcessorEditor::getTiltText() const

{

    const float db = (float) tiltSlider.getValue();

    if (std::abs (db) < 0.05f)

        return "0.0 dB TILT";

    return juce::String (db, 1) + " dB TILT";

}


juce::String GRATRAudioProcessorEditor::getTiltTextShort() const

{

    const float db = (float) tiltSlider.getValue();

    if (std::abs (db) < 0.05f)

        return "0.0 dB TLT";

    return juce::String (db, 1) + " dB TLT";

}


juce::String GRATRAudioProcessorEditor::getPanText() const

{

    const float v = (float) panSlider.getValue();

    const int pct = juce::roundToInt ((v - 0.5f) * 200.0f);

    if (pct == 0) return "C PAN";

    if (pct < 0)  return "L" + juce::String (-pct) + " PAN";

    return "R" + juce::String (pct) + " PAN";

}


juce::String GRATRAudioProcessorEditor::getPanTextShort() const

{

    const float v = (float) panSlider.getValue();

    const int pct = juce::roundToInt ((v - 0.5f) * 200.0f);

    if (pct == 0) return "C";

    if (pct < 0)  return "L" + juce::String (-pct);

    return "R" + juce::String (pct);

}


juce::String GRATRAudioProcessorEditor::getLimThresholdText() const

{

    const float db = (float) limThresholdSlider.getValue();

    if (std::abs (db) < 0.05f)

        return "0.0 dB LIM";

    return juce::String (db, 1) + " dB LIM";

}


juce::String GRATRAudioProcessorEditor::getLimThresholdTextShort() const

{

    const float db = (float) limThresholdSlider.getValue();

    if (std::abs (db) < 0.05f)

        return "0.0 dB LIM";

    return juce::String (db, 1) + " dB LIM";

}


//========================== Legend width constants ==========================

namespace

{

    constexpr const char* kTimeLegendFull   = "999.9 ms TIME";

    constexpr const char* kTimeLegendShort  = "5.00s TIME";

    constexpr const char* kTimeLegendInt    = "5.00s";


    constexpr const char* kModLegendFull   = "X4.00 MOD";

    constexpr const char* kModLegendShort  = "X4.00";

    constexpr const char* kModLegendInt    = "X4.00";


constexpr const char* kPitchLegendFull  = "+24.00 st PITCH";

constexpr const char* kPitchLegendShort = "+24.00st";

constexpr const char* kPitchLegendInt   = "+24.00st";


constexpr const char* kScanLegendFull  = "-100% SCAN";

constexpr const char* kScanLegendShort = "-100% SCN";

constexpr const char* kScanLegendInt   = "-100%";


    constexpr const char* kJitterLegendFull  = "100% JITTER";

    constexpr const char* kJitterLegendShort = "100% JIT";

    constexpr const char* kJitterLegendInt   = "100%";


    constexpr const char* kSmoothLegendFull  = "100% SMOOTH";

    constexpr const char* kSmoothLegendShort = "100% SMTH";

    constexpr const char* kSmoothLegendInt   = "100%";


    constexpr const char* kModeLegendFull  = "STEREO STYLE";

    constexpr const char* kModeLegendShort = "STEREO";

    constexpr const char* kModeLegendInt   = "1";


    constexpr const char* kInputLegendFull  = "-INF dB INPUT";

    constexpr const char* kInputLegendShort = "-INF dB IN";

    constexpr const char* kInputLegendInt   = "-INFdB";


    constexpr const char* kOutputLegendFull  = "-INF dB OUTPUT";

    constexpr const char* kOutputLegendShort = "-INF dB OUT";

    constexpr const char* kOutputLegendInt   = "-INFdB";


    constexpr const char* kMixLegendFull   = "100% MIX";

    constexpr const char* kMixLegendShort  = "100% MIX";

    constexpr const char* kMixLegendInt    = "100%";


    constexpr const char* kLimLegendFull   = "-36.0 dB LIM";

    constexpr const char* kLimLegendShort  = "-36.0 dB LIM";

    constexpr const char* kLimLegendInt    = "-36.0dB";

    constexpr int kResizerCornerPx = 22;

    constexpr int kToggleBoxPx = 72;

    constexpr int kMinToggleBlocksGapPx = 10;

    constexpr int kMinSliderGapPx = 4;




    using PopupSwatchButton = TR::PopupSwatchButton;


    using PopupClickableLabel = TR::PopupClickableLabel;
    using TextLayoutLabel = TR::TextLayoutLabel;


}


//========================== openNumericEntryPopupForSlider ==========================


void GRATRAudioProcessorEditor::openNumericEntryPopupForSlider (juce::Slider& s)
{
    lnf.setScheme (activeScheme);

    const bool isTimeSyncMode = (&s == &timeSlider && syncButton.getToggleState());
    const bool isModHarmPrompt = (&s == &modSlider && isModHarmEnabled (audioProcessor));
    if (isTimeSyncMode)
        return;

    TR::NumericEntryPromptSpec spec;
    if (&s == &timeSlider)          { spec.suffix = " ms";        spec.suffixShort = " ms"; }
    else if (&s == &pitchSlider)    { spec.suffix = " st PITCH";  spec.suffixShort = " st PCH"; }
    else if (&s == &scanSlider)     { spec.suffix = " % SCAN";    spec.suffixShort = " % SCN"; }
    else if (&s == &jitterSlider)   { spec.suffix = " % JITTER";  spec.suffixShort = " % JIT"; }
    else if (&s == &smoothSlider)   { spec.suffix = " % SMOOTH";  spec.suffixShort = " % SMTH"; }
    else if (&s == &modSlider)      { if (! isModHarmPrompt) spec.prefix = "X"; spec.suffix = " MOD"; spec.suffixShort = " MOD"; }
    else if (&s == &inputSlider)    { spec.suffix = " dB INPUT";  spec.suffixShort = " dB IN"; }
    else if (&s == &outputSlider)   { spec.suffix = " dB OUTPUT"; spec.suffixShort = " dB OUT"; }
    else if (&s == &tiltSlider)     { spec.suffix = " dB TILT";   spec.suffixShort = " dB TILT"; }
    else if (&s == &mixSlider)      { spec.suffix = " % MIX";     spec.suffixShort = " % MIX"; }
    else if (&s == &panSlider)      { spec.suffix = " % PAN";     spec.suffixShort = " % PAN"; }
    else if (&s == &limThresholdSlider) { spec.suffix = " dB LIM"; spec.suffixShort = " dB LIM"; }

    if (&s == &modSlider) spec.currentDisplay = isModHarmPrompt ? formatModHarmText (s.getValue(), false) : juce::String (modSliderToMultiplier (s.getValue()), 2);
    else if (&s == &panSlider) spec.currentDisplay = juce::String (juce::jlimit (0.0, 100.0, s.getValue() * 100.0), 0);
    else if (&s == &pitchSlider) { const float st = std::round ((float) s.getValue() * 100.0f) / 100.0f; spec.currentDisplay = (st > 0.0f) ? ("+" + juce::String (st, 2)) : juce::String (st, 2); }
    else if (&s == &scanSlider) { const float pct = std::round ((float) s.getValue() * 100.0f) / 100.0f; spec.currentDisplay = (pct > 0.0f) ? ("+" + juce::String (pct, 2)) : juce::String (pct, 2); }
    else if (&s == &jitterSlider) spec.currentDisplay = juce::String (s.getValue() * 100.0, 2);
    else if (&s == &smoothSlider) spec.currentDisplay = juce::String (s.getValue(), 2);
    else if (&s == &mixSlider) spec.currentDisplay = juce::String (s.getValue() * 100.0, 2);
    else if (&s == &timeSlider) spec.currentDisplay = juce::String (s.getValue(), 3);
    else spec.currentDisplay = s.getTextFromValue (s.getValue());

    if (&s == &timeSlider) { spec.minValue = GRATRAudioProcessor::kTimeMsMin; spec.maxValue = GRATRAudioProcessor::kTimeMsMax; spec.maxDecimals = 3; spec.maxLength = 9; spec.worstCaseText = juce::String (GRATRAudioProcessor::kTimeMsMax, 3); }
    else if (&s == &pitchSlider) { spec.minValue = -24.0; spec.maxValue = 24.0; spec.maxDecimals = 2; spec.maxLength = 6; spec.worstCaseText = "+24.00"; }
    else if (&s == &scanSlider) { spec.minValue = -100.0; spec.maxValue = 100.0; spec.maxDecimals = 2; spec.maxLength = 7; spec.worstCaseText = "-100.00"; }
    else if (&s == &jitterSlider || &s == &smoothSlider || &s == &mixSlider) { spec.minValue = 0.0; spec.maxValue = 100.0; spec.maxDecimals = 2; spec.maxLength = 6; spec.worstCaseText = "100.00"; }
    else if (&s == &modSlider)
    {
        if (isModHarmPrompt) { spec.minValue = -8.0; spec.maxValue = 8.0; spec.maxDecimals = 0; spec.maxLength = 4; spec.worstCaseText = "H+8"; spec.inputKind = TR::NumericEntryPromptInputKind::HarmonicStep; }
        else { spec.minValue = 0.25; spec.maxValue = 4.0; spec.maxDecimals = 2; spec.maxLength = 4; spec.worstCaseText = "4.00"; }
    }
    else if (&s == &inputSlider || &s == &outputSlider) { spec.minValue = GRATRAudioProcessor::kGainFloorDb; spec.maxValue = GRATRAudioProcessor::kGainMaxDb; spec.maxDecimals = 1; spec.maxLength = 6; spec.worstCaseText = "-144.0"; }
    else if (&s == &tiltSlider) { spec.minValue = GRATRAudioProcessor::kTiltMin; spec.maxValue = GRATRAudioProcessor::kTiltMax; spec.maxDecimals = 1; spec.maxLength = 4; spec.worstCaseText = "-6.0"; }
    else if (&s == &panSlider) { spec.minValue = 0.0; spec.maxValue = 100.0; spec.maxDecimals = 0; spec.maxLength = 3; spec.worstCaseText = "100"; }
    else if (&s == &limThresholdSlider) { spec.minValue = GRATRAudioProcessor::kLimThresholdMin; spec.maxValue = GRATRAudioProcessor::kLimThresholdMax; spec.maxDecimals = 1; spec.maxLength = 5; spec.worstCaseText = "-36.0"; }

    juce::Component::SafePointer<GRATRAudioProcessorEditor> safeThis (this);
    juce::Slider* sliderPtr = &s;
    spec.onAccept = [safeThis, sliderPtr] (const juce::String& txt)
    {
        if (safeThis == nullptr || sliderPtr == nullptr)
            return;
        auto normalised = txt.replaceCharacter (',', '.');
        juce::String t = normalised.trimStart();
        while (t.startsWithChar ('+')) t = t.substring (1).trimStart();
        double v = t.initialSectionContainingOnly ("0123456789.,-").getDoubleValue();
        if (normalised.trimStart().startsWithChar ('-')) v = -std::abs (v);

        if (sliderPtr == &safeThis->mixSlider || sliderPtr == &safeThis->panSlider || sliderPtr == &safeThis->jitterSlider)
            v *= 0.01;

        if (sliderPtr == &safeThis->modSlider)
        {
            if (isModHarmEnabled (safeThis->audioProcessor))
            {
                juce::String h = normalised.trim().toUpperCase();
                if (h.startsWithChar ('H')) h = h.substring (1).trimStart();
                while (h.startsWithChar ('+')) h = h.substring (1).trimStart();
                const int step = juce::jlimit (-8, 8, h.getIntValue());
                v = ((double) step + 8.0) / 16.0;
            }
            else v = multiplierToModSlider (v);
        }

        const auto range = sliderPtr->getRange();
        double clamped = juce::jlimit (range.getStart(), range.getEnd(), v);
        if (sliderPtr == &safeThis->timeSlider && ! safeThis->syncButton.getToggleState())
            clamped = roundToDecimals (clamped, 3);
        sliderPtr->setValue (clamped, juce::sendNotificationSync);
    };

    TR::openNumericEntryPopupShared (this, lnf, activeScheme, spec);
}


// -- Filter Prompt (HP/LP frequency + slope) -----------------------

void GRATRAudioProcessorEditor::openFilterPrompt()
{
    lnf.setScheme (activeScheme);
    auto& vts = audioProcessor.apvts;

    FilterPromptSpec spec;
    spec.hpParam = GRATRAudioProcessor::kParamFilterHpFreq;
    spec.lpParam = GRATRAudioProcessor::kParamFilterLpFreq;
    spec.hpOnParam = GRATRAudioProcessor::kParamFilterHpOn;
    spec.lpOnParam = GRATRAudioProcessor::kParamFilterLpOn;
    spec.hpSlopeParam = GRATRAudioProcessor::kParamFilterHpSlope;
    spec.lpSlopeParam = GRATRAudioProcessor::kParamFilterLpSlope;
    spec.freqMin = 20.0f;
    spec.freqMax = 20000.0f;
    spec.hpDefault = GRATRAudioProcessor::kFilterHpFreqDefault;
    spec.lpDefault = GRATRAudioProcessor::kFilterLpFreqDefault;
    spec.slopeMin = GRATRAudioProcessor::kFilterSlopeMin;
    spec.slopeMax = GRATRAudioProcessor::kFilterSlopeMax;
    spec.refreshFilterDisplay = [this] { filterBar_.updateFromProcessor(); };

    openFilterPromptShared (this, lnf, activeScheme, vts, spec);
}


void GRATRAudioProcessorEditor::openAutoDelayPrompt()
{
    lnf.setScheme (activeScheme);
    const int delayMs = audioProcessor.getAutoDelayMs();

    auto applyLiveDelay = [this] (int newDelayMs)
    {
        const int clamped = juce::jlimit (0, 100, newDelayMs);
        audioProcessor.setAutoDelayMs (clamped);
        autoDisplay.setTooltip (formatAutoDelayTooltip (clamped));
    };

    TR::openSimpleDelayMsPromptAction<GRATRAudioProcessorEditor> (this,
                                              lnf,
                                              activeScheme,
                                              delayMs,
                                              0,
                                              applyLiveDelay,
                                              [this, delayMs]
                                              {
                                                  audioProcessor.setAutoDelayMs (delayMs);
                                                  autoDisplay.setTooltip (formatAutoDelayTooltip (delayMs));
                                              },
                                              [applyLiveDelay] (int value) { applyLiveDelay (value); });
}


void GRATRAudioProcessorEditor::openTriggerDelayPrompt()
{
    lnf.setScheme (activeScheme);
    const int delayMs = audioProcessor.getTriggerDelayMs();

    auto applyLiveDelay = [this] (int newDelayMs)
    {
        const int clamped = juce::jlimit (0, 100, newDelayMs);
        audioProcessor.setTriggerDelayMs (clamped);
        triggerDisplay.setTooltip (formatTriggerDelayTooltip (clamped));
    };

    TR::openSimpleDelayMsPromptAction<GRATRAudioProcessorEditor> (this,
                                              lnf,
                                              activeScheme,
                                              delayMs,
                                              0,
                                              applyLiveDelay,
                                              [this, delayMs]
                                              {
                                                  audioProcessor.setTriggerDelayMs (delayMs);
                                                  triggerDisplay.setTooltip (formatTriggerDelayTooltip (delayMs));
                                              },
                                              [applyLiveDelay] (int value) { applyLiveDelay (value); });
}


void GRATRAudioProcessorEditor::openMidiChannelPrompt()
{
    TR::openMidiChannelDelayPromptShared<GRATRAudioProcessorEditor> (this,
                                                   lnf,
                                                   activeScheme,
                                                   [this]() { return audioProcessor.getMidiChannel(); },
                                                   [this] (int ch) { audioProcessor.setMidiChannel (ch); },
                                                   [this]() { return audioProcessor.getMidiDelayMs(); },
                                                   [this] (int delayMs) { audioProcessor.setMidiDelayMs (delayMs); },
                                                   [this] (int ch, int delayMs)
                                                   {
                                                       midiChannelDisplay.setTooltip (formatMidiChannelTooltip (ch, delayMs));
                                                   });
}


// -- ENV GRA Prompt (TAU + AMT bars) -------------------------------

//==============================================================================

//  MIX SEND prompt (DRY + WET levels)

//==============================================================================

void GRATRAudioProcessorEditor::openMixSendPrompt()
{
    TR::openMixSendPromptShared<GRATRAudioProcessorEditor> (this,
                                          lnf,
                                          activeScheme,
                                          audioProcessor.apvts,
                                          GRATRAudioProcessor::kParamDryLevel,
                                          GRATRAudioProcessor::kParamWetLevel,
                                          GRATRAudioProcessor::kDryLevelDefault,
                                          GRATRAudioProcessor::kWetLevelDefault,
                                          [this]() { dualMixBar_.updateFromProcessor(); });
}


//==============================================================================

//  CHAOS prompt (AMOUNT + SPEED)

//==============================================================================

void GRATRAudioProcessorEditor::openChaosConfigPrompt (const char* amtParamId,
                                                 const char* spdParamId,
                                                 const juce::String& title)
{
    auto& vts = audioProcessor.apvts;
    const bool isFilterChaos = title == "CHSF";
    const TR::SimpleChaosPromptBinding binding {
        amtParamId,
        spdParamId,
        isFilterChaos ? vts.getRawParameterValue (GRATRAudioProcessor::kParamChaosAmtFilter)->load()
                      : vts.getRawParameterValue (GRATRAudioProcessor::kParamChaosAmt)->load(),
        isFilterChaos ? vts.getRawParameterValue (GRATRAudioProcessor::kParamChaosSpdFilter)->load()
                      : vts.getRawParameterValue (GRATRAudioProcessor::kParamChaosSpd)->load()
    };

    TR::openSimpleChaosPromptAction<GRATRAudioProcessorEditor> (this,
                                            lnf,
                                            activeScheme,
                                            vts,
                                            binding,
                                            [this, isFilterChaos, amtParamId, spdParamId]
                                            {
                                                const auto amt = audioProcessor.apvts.getRawParameterValue (amtParamId)->load();
                                                const auto spd = audioProcessor.apvts.getRawParameterValue (spdParamId)->load();
                                                const auto tip = formatChaosTooltip (amt, spd);
                                                if (isFilterChaos)
                                                    chaosFilterDisplay.setTooltip (tip);
                                                else
                                                    chaosDelayDisplay.setTooltip (tip);
                                                repaint();
                                            });
}

void GRATRAudioProcessorEditor::openChaosFilterPrompt()
{
    TR::openSimpleChaosSelectorPromptAction (
        [this] (const char* amountParamId, const char* speedParamId, const juce::String& title)
        {
            openChaosConfigPrompt (amountParamId, speedParamId, title);
        },
        GRATRAudioProcessor::kParamChaosAmtFilter,
        GRATRAudioProcessor::kParamChaosSpdFilter,
        true);
}


void GRATRAudioProcessorEditor::openChaosDelayPrompt()
{
    TR::openSimpleChaosSelectorPromptAction (
        [this] (const char* amountParamId, const char* speedParamId, const juce::String& title)
        {
            openChaosConfigPrompt (amountParamId, speedParamId, title);
        },
        GRATRAudioProcessor::kParamChaosAmt,
        GRATRAudioProcessor::kParamChaosSpd,
        false);
}


//==============================================================================

//  Info Popup

//==============================================================================

void GRATRAudioProcessorEditor::openInfoPopup()
{
    lnf.setScheme (activeScheme);
    TR::openInfoPopupFromXmlShared<GRATRAudioProcessorEditor> (this,
                                           lnf,
                                           activeScheme,
                                           InfoContent::xml,
                                           [this]() { openGraphicsPopup(); });
}


//==============================================================================

//  Graphics Popup

//==============================================================================

void GRATRAudioProcessorEditor::openGraphicsPopup()
{
    lnf.setScheme (activeScheme);
    useCustomPalette = audioProcessor.getUiUseCustomPalette();
    crtEnabled = false;
    ioFxEnabled = audioProcessor.getUiIoFxEnabled();
    crtEffect.setEnabled (false);
    applyActivePalette();

    TR::openGraphicsPopupShared<GRATRAudioProcessorEditor> (this,
                                        lnf,
                                        activeScheme,
                                        defaultPalette,
                                        customPalette,
                                        useCustomPalette,
                                        ioFxEnabled,
                                        [this] (bool enabled)
                                        {
                                            useCustomPalette = enabled;
                                            audioProcessor.setUiUseCustomPalette (enabled);
                                        },
                                        [this] (int index, juce::Colour colour)
                                        {
                                            customPalette[(size_t) index] = colour;
                                            audioProcessor.setUiCustomPaletteColour (index, colour);
                                        },
                                        [this] (bool enabled)
                                        {
                                            applyIoFxState (enabled);
                                            audioProcessor.setUiIoFxEnabled (ioFxEnabled);
                                        },
                                        [this]()
                                        {
                                            applyActivePalette();
                                            updateIoFxMeterSliders();
                                            repaint();
                                        });
}


//==============================================================================

//  Layout helpers

//==============================================================================

GRATRAudioProcessorEditor::HorizontalLayoutMetrics

GRATRAudioProcessorEditor::buildHorizontalLayout (int editorW, int valueColW)

{
    return TR::buildSimpleHorizontalLayout (editorW, valueColW);
}

GRATRAudioProcessorEditor::VerticalLayoutMetrics
GRATRAudioProcessorEditor::buildVerticalLayout (int editorH, int biasY, bool ioExpanded)
{
    TR::SimpleVerticalLayoutConfig config;
    config.mainRows = 7;
    config.collapsedButtonRows = 3;
    config.collapsedSliderBottomRow = 0;
    config.expandedHasSidechainRow = false;

    return TR::buildSimpleVerticalLayout (editorH, biasY, ioExpanded, config);
}


void GRATRAudioProcessorEditor::updateCachedLayout()

{

    cachedHLayout_ = buildHorizontalLayout (getWidth(), getTargetValueColumnWidth());

    cachedVLayout_ = buildVerticalLayout (getHeight(), kLayoutVerticalBiasPx, ioSectionExpanded_);


    const juce::Slider* sliders[12] = { &timeSlider, &modSlider, &pitchSlider, &scanSlider, &smoothSlider, &jitterSlider,

                                        &modeSlider, &inputSlider, &outputSlider, &tiltSlider, &mixSlider, &panSlider };


    for (int i = 0; i < 12; ++i)

    {

        if (! sliders[i]->isVisible())

        {

            // MIX row: use dualMixBar_ bounds when SEND mode is active

            if (i == 10 && dualMixBar_.isVisible())

            {

                const auto& bb = dualMixBar_.getBounds();
                cachedValueAreas_[10] = TR::makeSimpleValueArea (bb, cachedHLayout_, getWidth());

                continue;

            }

            cachedValueAreas_[(size_t) i] = {};

            continue;

        }


        const auto& bb = sliders[i]->getBounds();
                cachedValueAreas_[(size_t) i] = TR::makeSimpleValueArea (bb, cachedHLayout_, getWidth());

    }


    if (filterBar_.isVisible())

    {

        const auto& bb = filterBar_.getBounds();
                cachedFilterValueArea_ = TR::makeSimpleValueArea (bb, cachedHLayout_, getWidth());

    }

    else

    {

        cachedFilterValueArea_ = {};

    }


    if (tiltSlider.isVisible())

    {

        const auto& bb = tiltSlider.getBounds();
                cachedTiltValueArea_ = TR::makeSimpleValueArea (bb, cachedHLayout_, getWidth());

    }

    else

    {

        cachedTiltValueArea_ = {};

    }


    if (panSlider.isVisible())

    {

        const auto& bb = panSlider.getBounds();
                cachedPanValueArea_ = TR::makeSimpleValueArea (bb, cachedHLayout_, getWidth());

    }

    else

    {

        cachedPanValueArea_ = {};

    }


    if (limThresholdSlider.isVisible())

    {

        const auto& bb = limThresholdSlider.getBounds();
                cachedLimThresholdValueArea_ = TR::makeSimpleValueArea (bb, cachedHLayout_, getWidth());

    }

    else

    {

        cachedLimThresholdValueArea_ = {};

    }


    if (chaosFilterButton.isVisible())

        cachedChaosArea_ = chaosFilterButton.getBounds().getUnion (chaosDelayButton.getBounds());

    else

        cachedChaosArea_ = {};


    cachedToggleBarArea_ = TR::makeSimpleToggleBarArea (cachedHLayout_, cachedVLayout_);

}


int GRATRAudioProcessorEditor::getTargetValueColumnWidth() const

{

    std::uint64_t key = 1469598103934665603ull;

    auto mix = [&] (std::uint64_t v)

    {

        key ^= v;

        key *= 1099511628211ull;

    };


    mix ((std::uint64_t) getWidth());


    if (key == cachedValueColumnWidthKey)

        return cachedValueColumnWidth;


    const auto& font = kBoldFont40();


    const int timeMaxW = juce::jmax (stringWidth (font, kTimeLegendFull),

                                     juce::jmax (stringWidth (font, kTimeLegendShort),

                                                 stringWidth (font, kTimeLegendInt)));


    const int pitchMaxW = juce::jmax (stringWidth (font, kPitchLegendFull),

                                      juce::jmax (stringWidth (font, kPitchLegendShort),

                                                  stringWidth (font, kPitchLegendInt)));


    const int modeMaxW = juce::jmax (stringWidth (font, kModeLegendFull),

                                     juce::jmax (stringWidth (font, kModeLegendShort),

                                                 stringWidth (font, kModeLegendInt)));


    const int scanMaxW = juce::jmax (stringWidth (font, kScanLegendFull),

                                     juce::jmax (stringWidth (font, kScanLegendShort),

                                                 stringWidth (font, kScanLegendInt)));


    const int jitterMaxW = juce::jmax (stringWidth (font, kJitterLegendFull),

                                       juce::jmax (stringWidth (font, kJitterLegendShort),

                                                   stringWidth (font, kJitterLegendInt)));


    const int smoothMaxW = juce::jmax (stringWidth (font, kSmoothLegendFull),

                                       juce::jmax (stringWidth (font, kSmoothLegendShort),

                                                   stringWidth (font, kSmoothLegendInt)));


    const int modMaxW = juce::jmax (stringWidth (font, kModLegendFull),

                                    juce::jmax (stringWidth (font, kModLegendShort),

                                                stringWidth (font, kModLegendInt)));


    const int inputMaxW = juce::jmax (stringWidth (font, kInputLegendFull),

                                      juce::jmax (stringWidth (font, kInputLegendShort),

                                                  stringWidth (font, kInputLegendInt)));


    const int outputMaxW = juce::jmax (stringWidth (font, kOutputLegendFull),

                                       juce::jmax (stringWidth (font, kOutputLegendShort),

                                                   stringWidth (font, kOutputLegendInt)));


    const int mixMaxW = juce::jmax (stringWidth (font, kMixLegendFull),

                                    juce::jmax (stringWidth (font, kMixLegendShort),

                                                stringWidth (font, kMixLegendInt)));


    const int limMaxW = juce::jmax (stringWidth (font, kLimLegendFull),

                                    juce::jmax (stringWidth (font, kLimLegendShort),

                                                stringWidth (font, kLimLegendInt)));


    const int maxW = juce::jmax (juce::jmax (juce::jmax (timeMaxW, pitchMaxW), juce::jmax (modeMaxW, modMaxW)),

                                 juce::jmax (juce::jmax (inputMaxW, outputMaxW), juce::jmax (mixMaxW, juce::jmax (scanMaxW, juce::jmax (jitterMaxW, limMaxW)))));

    const int maxWithSmooth = juce::jmax (maxW, smoothMaxW);


    const int desired = maxWithSmooth + 16;

    const int minW = 90;

    const int maxAllowed = juce::jmax (minW, (int) std::round (getWidth() * 0.40));

    cachedValueColumnWidth = juce::jlimit (minW, maxAllowed, desired);

    cachedValueColumnWidthKey = key;

    return cachedValueColumnWidth;

}


//========================== Hit areas ==========================


juce::Rectangle<int> GRATRAudioProcessorEditor::getValueAreaFor (const juce::Rectangle<int>& barBounds) const
{
    return TR::makeSimpleValueArea (barBounds, cachedHLayout_, getWidth());
}


juce::Slider* GRATRAudioProcessorEditor::getSliderForValueAreaPoint (juce::Point<int> p)
{
    if (auto* slider = TR::findSimpleSliderForValueAreaPoint (p, cachedValueAreas_, {
            { 0, &timeSlider },
            { 1, &modSlider },
            { 2, &pitchSlider },
            { 3, &scanSlider },
            { 4, &smoothSlider },
            { 5, &jitterSlider },
            { 6, &modeSlider },
            { 7, &inputSlider },
            { 8, &outputSlider },
            { 10, &mixSlider } }))
        return slider;

    if (cachedTiltValueArea_.contains (p))
        return &tiltSlider;

    if (cachedPanValueArea_.contains (p))
        return &panSlider;

    if (cachedLimThresholdValueArea_.contains (p))
        return &limThresholdSlider;

    return nullptr;
}


namespace

{

}


juce::Rectangle<int> GRATRAudioProcessorEditor::getSyncLabelArea() const

{

    return TR::makeSimpleToggleLabelArea (syncButton, midiButton.getX() - TR::kSimpleToggleLegendCollisionPadPx, "SYNC", "SYN");

}


juce::Rectangle<int> GRATRAudioProcessorEditor::getAutoLabelArea() const

{

    return TR::makeSimpleToggleLabelArea (autoButton, triggerButton.getX() - TR::kSimpleToggleLegendCollisionPadPx, "AUTO", "AUT");

}


juce::Rectangle<int> GRATRAudioProcessorEditor::getTriggerLabelArea() const

{

    return TR::makeSimpleToggleLabelArea (triggerButton, getWidth() - TR::kSimpleToggleLegendCollisionPadPx, "TRIGGER", "TRG");

}


juce::Rectangle<int> GRATRAudioProcessorEditor::getReverseLabelArea() const

{

    return TR::makeSimpleToggleLabelArea (reverseButton, backNForthButton.getX() - TR::kSimpleToggleLegendCollisionPadPx, "REVERSE", "RVS");

}


juce::Rectangle<int> GRATRAudioProcessorEditor::getBackNForthLabelArea() const

{

    return TR::makeSimpleToggleLabelArea (backNForthButton, getWidth() - TR::kSimpleToggleLegendCollisionPadPx, "BACK N FORTH", "BNF");

}


juce::Rectangle<int> GRATRAudioProcessorEditor::getMidiLabelArea() const

{

    return TR::makeSimpleToggleLabelArea (midiButton, getWidth() - TR::kSimpleToggleLegendCollisionPadPx, "MIDI", "MIDI");

}


juce::Rectangle<int> GRATRAudioProcessorEditor::getChaosLabelArea() const

{

    if (chaosFilterButton.getWidth() <= 0 || chaosFilterButton.getHeight() <= 0)

        return {};


    return TR::makeSimpleToggleLabelArea (chaosFilterButton,

                                chaosDelayButton.getX() - TR::kSimpleToggleLegendCollisionPadPx,

                                "CHSF", "CHSF");

}


juce::Rectangle<int> GRATRAudioProcessorEditor::getChaosDelayLabelArea() const

{

    if (chaosDelayButton.getWidth() <= 0 || chaosDelayButton.getHeight() <= 0)

        return {};


    return TR::makeSimpleToggleLabelArea (chaosDelayButton,

                                getWidth() - TR::kSimpleToggleLegendCollisionPadPx,

                                "CHSD", "CHSD");

}


juce::Rectangle<int> GRATRAudioProcessorEditor::getInfoIconArea() const

{

    int contentRight = 0;

    for (size_t i = 0; i < cachedValueAreas_.size(); ++i)

    {

        if (! cachedValueAreas_[i].isEmpty())

        {

            contentRight = cachedValueAreas_[i].getRight();

            break;

        }

    }

    if (contentRight <= 0)

        contentRight = getWidth() - 8;


    const int titleH = cachedVLayout_.titleH;

    const int titleY = cachedVLayout_.titleTopPad;

    const int titleAreaH = cachedVLayout_.titleAreaH;

    const int size = juce::jlimit (20, 36, titleH);


    const int x = contentRight - size;

    const int y = titleY + juce::jmax (0, (titleAreaH - size) / 2);

    return { x, y, size, size };

}


//========================== Mouse handlers ==========================


void GRATRAudioProcessorEditor::mouseDown (const juce::MouseEvent& e)
{
    lastUserInteractionMs.store (juce::Time::getMillisecondCounter(), std::memory_order_relaxed);
    const auto p = e.getEventRelativeTo (this).getPosition();

    if (TR::SimpleMouseRouter::routeMouseDown (*this, e, p,
            cachedToggleBarArea_, ioSectionExpanded_,
            modSlider, getValueAreaFor (modSlider.getBounds()),
            [this] { return isModHarmEnabled (audioProcessor); },
            [this] (bool v) { setModHarmEnabled (audioProcessor, v); },
            [this] (bool v) { return formatModHarmTooltip (v); },
            [this] {
                if (refreshLegendTextCache()) updateCachedLayout();
            },
            filterBar_, cachedFilterValueArea_,
            [this] { openFilterPrompt(); },
            [this] (juce::Point<int> pt) { return getSliderForValueAreaPoint (pt); },
            [this] (juce::Slider& s) { openNumericEntryPopupForSlider (s); },
            getInfoIconArea(), crtEnabled,
            [this] { openInfoPopup(); },
            {
                TR::SimpleMouseRouter::ToggleBinding { &syncButton,       getSyncLabelArea(),       nullptr,             {},                 true },
                TR::SimpleMouseRouter::ToggleBinding { &autoButton,       getAutoLabelArea(),       &autoDisplay,        [this] { openAutoDelayPrompt(); } },
                TR::SimpleMouseRouter::ToggleBinding { &triggerButton,    getTriggerLabelArea(),    &triggerDisplay,     [this] { openTriggerDelayPrompt(); } },
                TR::SimpleMouseRouter::ToggleBinding { &reverseButton,    getReverseLabelArea(),    nullptr,             {},                 true },
                TR::SimpleMouseRouter::ToggleBinding { &backNForthButton, getBackNForthLabelArea(), nullptr,             {},                 true },
                TR::SimpleMouseRouter::ToggleBinding { &midiButton,       getMidiLabelArea(),       &midiChannelDisplay, [this] { openMidiChannelPrompt(); } },
                TR::SimpleMouseRouter::ToggleBinding { &chaosFilterButton,getChaosLabelArea(),      &chaosFilterDisplay, [this] { openChaosFilterPrompt(); } },
                TR::SimpleMouseRouter::ToggleBinding { &chaosDelayButton, getChaosDelayLabelArea(), &chaosDelayDisplay,  [this] { openChaosDelayPrompt(); } },
            }))
        return;
}




void GRATRAudioProcessorEditor::mouseDrag (const juce::MouseEvent& e)

{

    juce::ignoreUnused (e);

    lastUserInteractionMs.store (juce::Time::getMillisecondCounter(), std::memory_order_relaxed);

}


void GRATRAudioProcessorEditor::mouseMove (const juce::MouseEvent& e)
{
    const auto p = e.getEventRelativeTo (this).getPosition();
    TR::routeSimpleHoverTooltip (*this, tooltipWindow.get(), p,
    {
        { modSlider.isVisible() ? getValueAreaFor (modSlider.getBounds()) : juce::Rectangle<int>(),
          formatModHarmTooltip (isModHarmEnabled (audioProcessor)) }
    });
}

void GRATRAudioProcessorEditor::mouseExit (const juce::MouseEvent& e)
{
    juce::ignoreUnused (e);
    TR::clearSimpleHoverTooltip (*this, tooltipWindow.get());
}



void GRATRAudioProcessorEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    TR::SimpleMouseRouter::routeMouseDoubleClick (*this, e.getPosition(),
        [this] (juce::Point<int> pt) { return getSliderForValueAreaPoint (pt); },
        {
            { &timeSlider,        kDefaultTimeMs },
            { &pitchSlider,       (double) GRATRAudioProcessor::kPitchDefault },
            { &modeSlider,        0.0 },
            { &scanSlider,        (double) GRATRAudioProcessor::kScanDefault },
            { &jitterSlider,      kDefaultJitter },
            { &smoothSlider,      kDefaultSmooth },
            { &modSlider,         (double) GRATRAudioProcessor::kModDefault },
            { &inputSlider,       kDefaultInput },
            { &outputSlider,      kDefaultOutput },
            { &tiltSlider,        kDefaultTilt },
            { &panSlider,         0.5 },
            { &mixSlider,         kDefaultMix },
            { &limThresholdSlider,kDefaultLimThreshold },
        });
}


//==============================================================================

TR::SimpleMainPanelSpec GRATRAudioProcessorEditor::buildMainPanelSpec()
{
    TR::SimpleMainPanelSpec spec;
    spec.title   = "GRA-TR";
    spec.version = juce::String ("v") + InfoContent::version;
    spec.ioExpanded   = ioSectionExpanded_;
    spec.toggleBarArea = cachedToggleBarArea_;

    {
        const juce::String* full[12] = { &cachedTimeTextFull, &cachedPitchTextFull, &cachedScanTextFull,
                                          &cachedJitterTextFull, &cachedSmoothTextFull, &cachedModeTextFull,
                                          &cachedModTextFull, &cachedInputTextFull, &cachedOutputTextFull,
                                          &cachedMixTextFull, &cachedTiltTextFull, &cachedLimThresholdTextFull };
        const juce::String* shrt[12] = { &cachedTimeTextShort, &cachedPitchTextShort, &cachedScanTextShort,
                                          &cachedJitterTextShort, &cachedSmoothTextShort, &cachedModeTextShort,
                                          &cachedModTextShort, &cachedInputTextShort, &cachedOutputTextShort,
                                          &cachedMixTextShort, &cachedTiltTextShort, &cachedLimThresholdTextShort };
        const juce::String* intOnly[12] = { &cachedTimeIntOnly, &cachedPitchIntOnly, &cachedScanIntOnly,
                                             &cachedJitterIntOnly, &cachedSmoothIntOnly, &cachedModeIntOnly,
                                             &cachedModIntOnly, &cachedInputIntOnly, &cachedOutputIntOnly,
                                             &cachedMixIntOnly, &cachedTiltIntOnly, &cachedLimThresholdIntOnly };
        for (int i = 0; i < 12; ++i)
            TR::addSimpleMainPanelRow (spec, false, full[i], shrt[i], intOnly[i],
                                       cachedValueAreas_[(size_t) i]);
    }

    // Expanded-only rows
    auto addExp = [&](const juce::Slider& s, const juce::Rectangle<int>& area,
                       const juce::String* full, const juce::String* shrt, const juce::String* intOnly = nullptr)
    {
        TR::addSimpleMainPanelRow (spec, true, full, shrt, intOnly, area, s.isVisible());
    };
    addExp (tiltSlider, cachedTiltValueArea_, &cachedTiltTextFull, &cachedTiltTextShort, &cachedTiltIntOnly);
    TR::addSimpleMainPanelRow (spec, true, &cachedFilterTextFull, &cachedFilterTextShort, nullptr,
                               cachedFilterValueArea_, filterBar_.isVisible());
    addExp (panSlider, cachedPanValueArea_, &cachedPanTextFull, &cachedPanTextShort);
    addExp (limThresholdSlider, cachedLimThresholdValueArea_, &cachedLimThresholdTextFull, &cachedLimThresholdTextShort, &cachedLimThresholdIntOnly);

    spec.combosVisible = modeInCombo.isVisible();
    spec.comboLabels = {
        { &modeInCombo, "MODE IN", "IN" }, { &modeOutCombo, "MODE OUT", "OUT" },
        { &sumBusCombo, "SUM BUS", "SUM" }, { &limModeCombo, "LIMIT", "LIM" },
        { &mixModeCombo, "MIX", "MIX" }, { &filterPosCombo, "F / T", "F/T" },
        { &invPolCombo, "INV POL", "POL" }, { &invStrCombo, "INV STR", "STR" }
    };

    // Chaos toggles (always when expanded)
    const int W = getWidth();
    TR::addSimpleMainPanelToggle (spec, false, chaosFilterButton, getChaosLabelArea(), "CHSF", "CHSF",
                                  TR::makeSimpleMainPanelRightBoundBefore (chaosDelayButton, W));
    TR::addSimpleMainPanelToggle (spec, false, chaosDelayButton,
                                  TR::makeSimpleToggleLabelArea (chaosDelayButton, TR::makeSimpleMainPanelRightBound (W), "CHSD", "CHSD"),
                                  "CHSD", "CHSD", TR::makeSimpleMainPanelRightBound (W));

    // Collapsed toggles
    if (! ioSectionExpanded_)
    {
        TR::addSimpleMainPanelToggle (spec, true, reverseButton, getReverseLabelArea(), "GRN", "GRN",
                                      TR::makeSimpleMainPanelRightBoundBefore (backNForthButton, W));
        TR::addSimpleMainPanelToggle (spec, true, backNForthButton, getBackNForthLabelArea(), "B/F", "B/F",
                                      TR::makeSimpleMainPanelRightBound (W));
        TR::addSimpleMainPanelToggle (spec, true, syncButton, getSyncLabelArea(), "SYNC", "SYN",
                                      TR::makeSimpleMainPanelRightBoundBefore (autoButton, W));
        TR::addSimpleMainPanelToggle (spec, true, autoButton, getAutoLabelArea(), "AUTO", "AUTO",
                                      TR::makeSimpleMainPanelRightBoundBefore (triggerButton, W));
        TR::addSimpleMainPanelToggle (spec, true, triggerButton, getTriggerLabelArea(), "TRIG", "TRIG",
                                      TR::makeSimpleMainPanelRightBoundBefore (midiButton, W));
        TR::addSimpleMainPanelToggle (spec, true, midiButton, getMidiLabelArea(), "MIDI", "MIDI",
                                      TR::makeSimpleMainPanelRightBound (W));
    }

    if (cachedInfoGearPath.isEmpty())
        updateInfoIconCache();
    TR::setSimpleMainPanelInfoGear (spec, cachedInfoGearPath, cachedInfoGearHole);

    return spec;
}

void GRATRAudioProcessorEditor::paint (juce::Graphics& g)
{
    TR::SimpleMainPanelRenderer::paint (g, buildMainPanelSpec(), activeScheme, kBoldFont40(), getWidth());
}


void GRATRAudioProcessorEditor::paintOverChildren (juce::Graphics& g)

{

    juce::ignoreUnused (g);

}


void GRATRAudioProcessorEditor::updateInfoIconCache()

{

    const auto iconArea = getInfoIconArea();

    const auto iconF = iconArea.toFloat();

    const auto center = iconF.getCentre();

    const float toothTipR = (float) iconArea.getWidth() * 0.47f;

    const float toothRootR = toothTipR * 0.78f;

    const float holeR = toothTipR * 0.40f;

    constexpr int teeth = 8;


    cachedInfoGearPath.clear();

    for (int i = 0; i < teeth * 2; ++i)

    {

        const float a = -juce::MathConstants<float>::halfPi

                      + (juce::MathConstants<float>::pi * (float) i / (float) teeth);

        const float r = (i % 2 == 0) ? toothTipR : toothRootR;

        const float x = center.x + std::cos (a) * r;

        const float y = center.y + std::sin (a) * r;


        if (i == 0)

            cachedInfoGearPath.startNewSubPath (x, y);

        else

            cachedInfoGearPath.lineTo (x, y);

    }

    cachedInfoGearPath.closeSubPath();

    cachedInfoGearHole = { center.x - holeR, center.y - holeR, holeR * 2.0f, holeR * 2.0f };

}


void GRATRAudioProcessorEditor::resized()

{

    refreshLegendTextCache();


    if (! suppressSizePersistence)

    {

        if (juce::ModifierKeys::getCurrentModifiers().isAnyMouseButtonDown()

            || juce::Desktop::getInstance().getMainMouseSource().isDragging())

        {

            lastUserInteractionMs.store (juce::Time::getMillisecondCounter(), std::memory_order_relaxed);

        }

    }


    const int W = getWidth();

    const int H = getHeight();


    if (! suppressSizePersistence)

    {

        const uint32_t last = lastUserInteractionMs.load (std::memory_order_relaxed);

        const uint32_t now = juce::Time::getMillisecondCounter();

        const bool userRecent = (now - last) <= (uint32_t) kUserInteractionPersistWindowMs;

        if ((W != lastPersistedEditorW || H != lastPersistedEditorH) && userRecent)

        {

            audioProcessor.setUiEditorSize (W, H);

            lastPersistedEditorW = W;

            lastPersistedEditorH = H;

        }

    }


    const auto horizontalLayout = buildHorizontalLayout (W, getTargetValueColumnWidth());

    const auto verticalLayout = buildVerticalLayout (H, kLayoutVerticalBiasPx, ioSectionExpanded_);


    if (ioSectionExpanded_)

    {

        // Expanded: [toggle bar] ? INPUT, OUTPUT, TILT, FILTER, PAN, MIX, LIM, MODE combos, CHAOS; main params hidden

        TR::placeSimpleRowComponent (inputSlider, horizontalLayout, verticalLayout, 0);

        TR::placeSimpleRowComponent (outputSlider, horizontalLayout, verticalLayout, 1);

        TR::placeSimpleRowComponent (tiltSlider, horizontalLayout, verticalLayout, 2);

        TR::placeSimpleRowComponent (filterBar_, horizontalLayout, verticalLayout, 3);

        TR::placeSimpleRowComponent (panSlider, horizontalLayout, verticalLayout, 4);

        TR::placeSimpleRowComponent (mixSlider, horizontalLayout, verticalLayout, 5);

        TR::placeSimpleRowComponent (dualMixBar_, horizontalLayout, verticalLayout, 5);

        TR::placeSimpleRowComponent (limThresholdSlider, horizontalLayout, verticalLayout, 6);


        {
            const int blockTopLimit = limThresholdSlider.getBottom() + verticalLayout.gapY;
            const int blockBottomLimit = verticalLayout.chaosRowY - verticalLayout.gapY;
            TR::placeSimpleIoComboGrid (horizontalLayout, verticalLayout, blockTopLimit, blockBottomLimit,
                                        modeInCombo, modeOutCombo, sumBusCombo, limModeCombo,
                                        mixModeCombo, filterPosCombo, invPolCombo, invStrCombo);
        }
        const int chaosY = verticalLayout.chaosRowY;
        TR::placeSimpleWideTogglePair (chaosFilterButton, chaosDelayButton, horizontalLayout, verticalLayout, chaosY);
        TR::placeSimpleDisplayLabel (chaosFilterDisplay, getChaosLabelArea());

        TR::placeSimpleDisplayLabel (chaosDelayDisplay, getChaosDelayLabelArea());


        TR::setSimpleComponentVisible (inputSlider, true);

        TR::setSimpleComponentVisible (outputSlider, true);

        TR::setSimpleComponentVisible (tiltSlider, true);

        TR::setSimpleComponentVisible (filterBar_, true);

        TR::setSimpleComponentVisible (panSlider, true);

        TR::setSimpleComponentVisible (mixSlider, true);

        TR::setSimpleComponentVisible (limThresholdSlider, true);

        TR::setSimpleComponentVisible (modeInCombo, true);

        TR::setSimpleComponentVisible (modeOutCombo, true);

        TR::setSimpleComponentVisible (sumBusCombo, true);

        TR::setSimpleComponentVisible (limModeCombo, true);

        TR::setSimpleComponentVisible (invPolCombo, true);

        TR::setSimpleComponentVisible (invStrCombo, true);

        TR::setSimpleComponentVisible (mixModeCombo, true);

        TR::setSimpleComponentVisible (filterPosCombo, true);

        {

            const bool isSendMode = mixModeCombo.getSelectedId() == 2;

            TR::setSimpleComponentVisible (mixSlider, ! isSendMode);

            TR::setSimpleComponentVisible (dualMixBar_, isSendMode);

        }

        TR::setSimpleComponentVisible (chaosFilterButton, true);

        TR::setSimpleComponentVisible (chaosFilterDisplay, true);

        TR::setSimpleComponentVisible (chaosDelayButton, true);

        TR::setSimpleComponentVisible (chaosDelayDisplay, true);


        TR::setSimpleComponentVisible (reverseButton, false);

        TR::setSimpleComponentVisible (backNForthButton, false);

        TR::setSimpleComponentVisible (autoButton, false);

        TR::setSimpleComponentVisible (triggerButton, false);

        TR::setSimpleComponentVisible (autoDisplay, false);

        TR::setSimpleComponentVisible (triggerDisplay, false);

        TR::setSimpleComponentVisible (syncButton, false);

        TR::setSimpleComponentVisible (midiButton, false);

        TR::setSimpleComponentVisible (midiChannelDisplay, false);


        TR::hideSimpleComponent (timeSlider);

        TR::hideSimpleComponent (modSlider);

        TR::hideSimpleComponent (pitchSlider);

        TR::hideSimpleComponent (scanSlider);

        TR::hideSimpleComponent (smoothSlider);

        TR::hideSimpleComponent (jitterSlider);

        TR::hideSimpleComponent (modeSlider);


        TR::setSimpleComponentVisible (timeSlider, false);

        TR::setSimpleComponentVisible (modSlider, false);

        TR::setSimpleComponentVisible (pitchSlider, false);

        TR::setSimpleComponentVisible (scanSlider, false);

        TR::setSimpleComponentVisible (smoothSlider, false);

        TR::setSimpleComponentVisible (jitterSlider, false);

        TR::setSimpleComponentVisible (modeSlider, false);

    }

    else

    {

        // Collapsed: [toggle bar] ? main params; IO hidden

        TR::placeSimpleRowComponent (timeSlider, horizontalLayout, verticalLayout, 0);

        TR::placeSimpleRowComponent (modSlider, horizontalLayout, verticalLayout, 1);

        TR::placeSimpleRowComponent (pitchSlider, horizontalLayout, verticalLayout, 2);

        TR::placeSimpleRowComponent (scanSlider, horizontalLayout, verticalLayout, 3);

        TR::placeSimpleRowComponent (smoothSlider, horizontalLayout, verticalLayout, 4);

        TR::placeSimpleRowComponent (jitterSlider, horizontalLayout, verticalLayout, 5);

        TR::placeSimpleRowComponent (modeSlider, horizontalLayout, verticalLayout, 6);


        TR::setSimpleComponentVisible (timeSlider, true);

        TR::setSimpleComponentVisible (modSlider, true);

        TR::setSimpleComponentVisible (pitchSlider, true);

        TR::setSimpleComponentVisible (scanSlider, true);

        TR::setSimpleComponentVisible (smoothSlider, true);

        TR::setSimpleComponentVisible (jitterSlider, true);

        TR::setSimpleComponentVisible (modeSlider, true);


        TR::hideSimpleComponent (inputSlider);

        TR::hideSimpleComponent (outputSlider);

        TR::hideSimpleComponent (tiltSlider);

        TR::hideSimpleComponent (mixSlider);

        TR::hideSimpleComponent (dualMixBar_);

        TR::hideSimpleComponent (panSlider);

        TR::hideSimpleComponent (filterBar_);

        TR::hideSimpleComponent (limThresholdSlider);


        TR::setSimpleComponentVisible (inputSlider, false);

        TR::setSimpleComponentVisible (outputSlider, false);

        TR::setSimpleComponentVisible (tiltSlider, false);

        TR::setSimpleComponentVisible (mixSlider, false);

        TR::setSimpleComponentVisible (dualMixBar_, false);

        TR::setSimpleComponentVisible (panSlider, false);

        TR::setSimpleComponentVisible (filterBar_, false);

        TR::setSimpleComponentVisible (limThresholdSlider, false);

        TR::setSimpleComponentVisible (chaosFilterButton, false);

        TR::setSimpleComponentVisible (chaosFilterDisplay, false);

        TR::setSimpleComponentVisible (chaosDelayButton, false);

        TR::setSimpleComponentVisible (chaosDelayDisplay, false);

        TR::setSimpleComponentVisible (modeInCombo, false);

        TR::setSimpleComponentVisible (modeOutCombo, false);

        TR::setSimpleComponentVisible (sumBusCombo, false);

        TR::setSimpleComponentVisible (limModeCombo, false);

        TR::setSimpleComponentVisible (invPolCombo, false);

        TR::setSimpleComponentVisible (invStrCombo, false);

        TR::setSimpleComponentVisible (mixModeCombo, false);

        TR::setSimpleComponentVisible (filterPosCombo, false);


        TR::setSimpleComponentVisible (reverseButton, true);

        TR::setSimpleComponentVisible (backNForthButton, true);

        TR::setSimpleComponentVisible (autoButton, true);

        TR::setSimpleComponentVisible (triggerButton, true);

        TR::setSimpleComponentVisible (autoDisplay, true);

        TR::setSimpleComponentVisible (triggerDisplay, true);

        TR::setSimpleComponentVisible (syncButton, true);

        TR::setSimpleComponentVisible (midiButton, true);

        TR::setSimpleComponentVisible (midiChannelDisplay, true);

    }


    // Button area: 3x2 grid

    // Row 1: RVS (left) + BNF (right)

    // Row 2: AUTO (left) + TRIGGER (right)

    // Row 3: SYNC (left) + MIDI (right)
    const int btnRow1Y = verticalLayout.btnRow1Y;
    const int btnRow2Y = verticalLayout.btnRow2Y;
    const int btnRow3Y = verticalLayout.btnRow3Y;
    TR::placeSimpleToggleAt (reverseButton, horizontalLayout, verticalLayout, false, btnRow1Y);
    TR::placeSimpleToggleAt (backNForthButton, horizontalLayout, verticalLayout, true, btnRow1Y);
    TR::placeSimpleToggleAt (autoButton, horizontalLayout, verticalLayout, false, btnRow2Y);
    TR::placeSimpleToggleAt (triggerButton, horizontalLayout, verticalLayout, true, btnRow2Y);
    TR::placeSimpleToggleAt (syncButton, horizontalLayout, verticalLayout, false, btnRow3Y);
    TR::placeSimpleToggleAt (midiButton, horizontalLayout, verticalLayout, true, btnRow3Y);

// Position invisible tooltip overlays on label areas

    {

        autoDisplay.setBounds (getAutoLabelArea());

        TR::placeSimpleDisplayLabel (triggerDisplay, getTriggerLabelArea());

        midiChannelDisplay.setBounds (getMidiLabelArea());

    }


    if (resizerCorner != nullptr)

        resizerCorner->setBounds (W - kResizerCornerPx, H - kResizerCornerPx, kResizerCornerPx, kResizerCornerPx);


    promptOverlay.setBounds (getLocalBounds());

    if (promptOverlayActive)

        promptOverlay.toFront (false);


    updateCachedLayout();


    updateInfoIconCache();

    crtEffect.setResolution (static_cast<float> (W), static_cast<float> (H));

}






