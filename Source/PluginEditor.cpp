// PluginEditor.cpp
#include "PluginEditor.h"
#include "InfoContent.h"
#include <functional>

using namespace TR;

#if JUCE_WINDOWS
 #include <windows.h>
#endif

namespace UiStateKeys
{
    constexpr const char* editorWidth = "uiEditorWidth";
    constexpr const char* editorHeight = "uiEditorHeight";
    constexpr const char* useCustomPalette = "uiUseCustomPalette";
    constexpr const char* crtEnabled = "uiFxTailEnabled";
    constexpr std::array<const char*, 2> customPalette {
        "uiCustomPalette0",
        "uiCustomPalette1"
    };
}

// ── Timer & display constants ──
static constexpr int   kCrtTimerHz   = 10;
static constexpr int   kIdleTimerHz  = 4;
static constexpr float kSilenceDb    = -80.0f;
static constexpr float kMultEpsilon  = 0.01f;

// ── Mod slider ↔ multiplier conversion (same as ECHO-TR) ──
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

static double multiplierToModSlider (double mult)
{
    mult = juce::jlimit (kModMinMult, kModMaxMult, mult);
    if (mult < 1.0)
        return (kModMaxMult - 1.0 / mult) * kModCenter / kModScale;
    return kModCenter + (mult - 1.0) * kModCenter / kModScale;
}

// ── MIDI channel tooltip ──
static juce::String formatMidiChannelTooltip (int ch)
{
    return "CHANNEL " + juce::String (ch);
}

// ── Env-grain tooltip ──
static juce::String formatEnvGraTooltip (float tauPct, float amtPct)
{
    return juce::String (juce::roundToInt (tauPct)) + "% | "
         + juce::String (juce::roundToInt (amtPct)) + "%";
}

// ── Parameter listener IDs (shared by ctor + dtor) ──
static constexpr std::array<const char*, 5> kUiMirrorParamIds {
    GRATRAudioProcessor::kParamSync,
    GRATRAudioProcessor::kParamUiPalette,
    GRATRAudioProcessor::kParamUiCrt,
    GRATRAudioProcessor::kParamUiColor0,
    GRATRAudioProcessor::kParamUiColor1
};

//========================== LookAndFeel ==========================

void GRATRAudioProcessorEditor::MinimalLNF::drawLinearSlider (juce::Graphics& g,
                                                                  int x, int y, int width, int height,
                                                                  float sliderPos, float /*minSliderPos*/, float /*maxSliderPos*/,
                                                                  const juce::Slider::SliderStyle /*style*/, juce::Slider& /*slider*/)
{
    const juce::Rectangle<float> r ((float) x, (float) y, (float) width, (float) height);

    g.setColour (scheme.outline);
    g.drawRect (r, 4.0f);

    const float pad = 7.0f;
    auto inner = r.reduced (pad);

    g.setColour (scheme.bg);
        g.fillRect (inner);

    const float fillW = juce::jlimit (0.0f, inner.getWidth(), sliderPos - inner.getX());
    auto fill = inner.withWidth (fillW);

    g.setColour (scheme.fg);
    g.fillRect (fill);
}

void GRATRAudioProcessorEditor::MinimalLNF::drawTickBox (juce::Graphics& g, juce::Component& button,
                                                            float x, float y, float w, float h,
                                                            bool ticked, bool /*isEnabled*/,
                                                            bool /*highlighted*/, bool /*down*/)
{
    juce::ignoreUnused (x, y, w, h);

    const auto local = button.getLocalBounds().toFloat().reduced (1.0f);
    const float side = juce::jlimit (14.0f,
                                     juce::jmax (14.0f, local.getHeight() - 2.0f),
                                     std::round (local.getHeight() * 0.65f));

    auto r = juce::Rectangle<float> (local.getX() + 2.0f,
                                     local.getCentreY() - (side * 0.5f),
                                     side,
                                     side).getIntersection (local);

    if (ticked)
    {
        g.setColour (scheme.outline);
        g.fillRect (r);
    }
    else
    {
        g.setColour (scheme.outline);
        g.drawRect (r, 4.0f);

        const float innerInset = juce::jlimit (1.0f, side * 0.45f, side * UiMetrics::tickBoxInnerInsetRatio);
        auto inner = r.reduced (innerInset);
        g.setColour (scheme.bg);
        g.fillRect (inner);
    }
}

void GRATRAudioProcessorEditor::MinimalLNF::drawToggleButton (
	juce::Graphics& g, juce::ToggleButton& button,
	bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown)
{
	const auto local = button.getLocalBounds().toFloat().reduced (1.0f);
	const float side = juce::jlimit (14.0f,
	                                 juce::jmax (14.0f, local.getHeight() - 2.0f),
	                                 std::round (local.getHeight() * 0.65f));

	drawTickBox (g, button, 0, 0, 0, 0,
	             button.getToggleState(), button.isEnabled(),
	             shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);

	const float textX = local.getX() + 2.0f + side + 2.0f;
	auto textArea = button.getLocalBounds().toFloat();
	textArea.removeFromLeft (textX);

	g.setColour (button.findColour (juce::ToggleButton::textColourId));

	float fontSize = juce::jlimit (12.0f, 40.0f, (float) button.getHeight() - 6.0f);

	const auto text = button.getButtonText();
	const float availW = textArea.getWidth();
	if (availW > 0)
	{
		juce::Font testFont (juce::FontOptions (fontSize).withStyle ("Bold"));
		juce::GlyphArrangement ga;
		ga.addLineOfText (testFont, text, 0.0f, 0.0f);
		const float neededW = ga.getBoundingBox (0, -1, false).getWidth();
		if (neededW > availW)
			fontSize = juce::jmax (8.0f, fontSize * (availW / neededW));
	}

	g.setFont (juce::Font (juce::FontOptions (fontSize).withStyle ("Bold")));
	g.drawText (text, textArea, juce::Justification::centredLeft, false);
}

void GRATRAudioProcessorEditor::MinimalLNF::drawButtonBackground (juce::Graphics& g,
                                                                      juce::Button& button,
                                                                      const juce::Colour& backgroundColour,
                                                                      bool shouldDrawButtonAsHighlighted,
                                                                      bool shouldDrawButtonAsDown)
{
    auto r = button.getLocalBounds();

    auto fill = backgroundColour;
    if (shouldDrawButtonAsDown)
        fill = fill.brighter (0.12f);
    else if (shouldDrawButtonAsHighlighted)
        fill = fill.brighter (0.06f);

    g.setColour (fill);
    g.fillRect (r);

    g.setColour (scheme.outline);
    g.drawRect (r.reduced (1), 3);
}

void GRATRAudioProcessorEditor::MinimalLNF::drawAlertBox (juce::Graphics& g,
                                                              juce::AlertWindow& alert,
                                                              const juce::Rectangle<int>& textArea,
                                                              juce::TextLayout& textLayout)
{
    auto bounds = alert.getLocalBounds();

    g.setColour (scheme.bg);
    g.fillRect (bounds);

    g.setColour (scheme.outline);
    g.drawRect (bounds.reduced (1), 3);

    g.setColour (scheme.text);
    textLayout.draw (g, textArea.toFloat());
}

void GRATRAudioProcessorEditor::MinimalLNF::drawBubble (juce::Graphics& g,
                                                            juce::BubbleComponent&,
                                                            const juce::Point<float>&,
                                                            const juce::Rectangle<float>& body)
{
    drawOverlayPanel (g,
                      body.getSmallestIntegerContainer(),
                      findColour (juce::TooltipWindow::backgroundColourId),
                      findColour (juce::TooltipWindow::outlineColourId));
}

void GRATRAudioProcessorEditor::MinimalLNF::drawScrollbar (juce::Graphics& g,
                                                             juce::ScrollBar&,
                                                             int x, int y, int width, int height,
                                                             bool isScrollbarVertical,
                                                             int thumbStartPosition, int thumbSize,
                                                             bool isMouseOver, bool isMouseDown)
{
    juce::ignoreUnused (x, y, width, height);

    const auto thumbColour = scheme.text.withAlpha (isMouseDown ? 0.7f
                                                     : isMouseOver ? 0.5f
                                                                   : 0.3f);
    constexpr float barThickness = 7.0f;
    constexpr float cornerRadius = 3.5f;

    if (isScrollbarVertical)
    {
        const float bx = (float) (x + width) - barThickness - 1.0f;
        g.setColour (thumbColour);
        g.fillRoundedRectangle (bx, (float) thumbStartPosition,
                                barThickness, (float) thumbSize, cornerRadius);
    }
    else
    {
        const float by = (float) (y + height) - barThickness - 1.0f;
        g.setColour (thumbColour);
        g.fillRoundedRectangle ((float) thumbStartPosition, by,
                                (float) thumbSize, barThickness, cornerRadius);
    }
}

void GRATRAudioProcessorEditor::MinimalLNF::drawComboBox (
    juce::Graphics& g, int width, int height,
    bool /*isButtonDown*/, int /*buttonX*/, int /*buttonY*/,
    int /*buttonW*/, int /*buttonH*/, juce::ComboBox& /*box*/)
{
    const juce::Rectangle<int> r (0, 0, width, height);
    g.setColour (scheme.bg);
    g.fillRect (r);
    g.setColour (scheme.outline);
    g.drawRect (r, 3);
}

void GRATRAudioProcessorEditor::MinimalLNF::drawPopupMenuBackground (
    juce::Graphics& g, int width, int height)
{
    g.fillAll (scheme.bg);
    g.setColour (scheme.outline);
    g.drawRect (0, 0, width, height, 2);
}

juce::Font GRATRAudioProcessorEditor::MinimalLNF::getComboBoxFont (juce::ComboBox& box)
{
    const float h = juce::jlimit (10.0f, 18.0f, box.getHeight() * 0.55f);
    return juce::Font (juce::FontOptions (h).withStyle ("Bold"));
}

juce::Font GRATRAudioProcessorEditor::MinimalLNF::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    const float h = juce::jlimit (12.0f, 26.0f, buttonHeight * 0.48f);
    return juce::Font (juce::FontOptions (h).withStyle ("Bold"));
}

juce::Font GRATRAudioProcessorEditor::MinimalLNF::getAlertWindowMessageFont()
{
    auto f = juce::LookAndFeel_V4::getAlertWindowMessageFont();
    f.setBold (true);
    return f;
}

juce::Font GRATRAudioProcessorEditor::MinimalLNF::getLabelFont (juce::Label& label)
{
    auto f = label.getFont();
    if (f.getHeight() <= 0.0f)
    {
        const float h = juce::jlimit (12.0f, 40.0f, (float) juce::jmax (12, label.getHeight() - 6));
        f = juce::Font (juce::FontOptions (h).withStyle ("Bold"));
    }
    else
    {
        f.setBold (true);
    }

    return f;
}

juce::Font GRATRAudioProcessorEditor::MinimalLNF::getSliderPopupFont (juce::Slider&)
{
    return makeOverlayDisplayFont();
}

juce::Rectangle<int> GRATRAudioProcessorEditor::MinimalLNF::getTooltipBounds (const juce::String& tipText,
                                                                                   juce::Point<int> screenPos,
                                                                                   juce::Rectangle<int> parentArea)
{
    const auto f = makeOverlayDisplayFont();
    const int h = juce::jmax (UiMetrics::tooltipMinHeight,
                              (int) std::ceil (f.getHeight() * UiMetrics::tooltipHeightScale));

    const int anchorOffsetX = juce::jmax (8, (int) std::round ((double) h * UiMetrics::tooltipAnchorXRatio));
    const int anchorOffsetY = juce::jmax (10, (int) std::round ((double) h * UiMetrics::tooltipAnchorYRatio));
    const int parentMargin = juce::jmax (2, (int) std::round ((double) h * UiMetrics::tooltipParentMarginRatio));
    const int widthPad = juce::jmax (16, (int) std::round (f.getHeight() * UiMetrics::tooltipWidthPadFontRatio));

    const int w = juce::jmax (UiMetrics::tooltipMinWidth, stringWidth (f, tipText) + widthPad);
    auto r = juce::Rectangle<int> (screenPos.x + anchorOffsetX, screenPos.y + anchorOffsetY, w, h);
    return r.constrainedWithin (parentArea.reduced (parentMargin));
}

void GRATRAudioProcessorEditor::MinimalLNF::drawTooltip (juce::Graphics& g,
                                                              const juce::String& text,
                                                              int width,
                                                              int height)
{
    const auto f = makeOverlayDisplayFont();
    const int h = juce::jmax (UiMetrics::tooltipMinHeight,
                              (int) std::ceil (f.getHeight() * UiMetrics::tooltipHeightScale));
    const int textInsetX = juce::jmax (4, (int) std::round ((double) h * UiMetrics::tooltipTextInsetXRatio));
    const int textInsetY = juce::jmax (1, (int) std::round ((double) h * UiMetrics::tooltipTextInsetYRatio));

    drawOverlayPanel (g,
                      { 0, 0, width, height },
                      findColour (juce::TooltipWindow::backgroundColourId),
                      findColour (juce::TooltipWindow::outlineColourId));

    g.setColour (findColour (juce::TooltipWindow::textColourId));
    g.setFont (f);
    g.drawFittedText (text,
                      textInsetX,
                      textInsetY,
                      juce::jmax (1, width - (textInsetX * 2)),
                      juce::jmax (1, height - (textInsetY * 2)),
                      juce::Justification::centred,
                      1);
}

//========================== FilterBarComponent ==========================

juce::Rectangle<float> GRATRAudioProcessorEditor::FilterBarComponent::getInnerArea() const
{
    return getLocalBounds().toFloat().reduced (kPad);
}

float GRATRAudioProcessorEditor::FilterBarComponent::freqToNormX (float freq) const
{
    const float clamped = juce::jlimit (kMinFreq, kMaxFreq, freq);
    return std::log2 (clamped / kMinFreq) / std::log2 (kMaxFreq / kMinFreq);
}

float GRATRAudioProcessorEditor::FilterBarComponent::normXToFreq (float normX) const
{
    const float n = juce::jlimit (0.0f, 1.0f, normX);
    return kMinFreq * std::pow (2.0f, n * std::log2 (kMaxFreq / kMinFreq));
}

float GRATRAudioProcessorEditor::FilterBarComponent::getMarkerScreenX (float freq) const
{
    const auto inner = getInnerArea();
    return inner.getX() + freqToNormX (freq) * inner.getWidth();
}

GRATRAudioProcessorEditor::FilterBarComponent::DragTarget
GRATRAudioProcessorEditor::FilterBarComponent::hitTestMarker (juce::Point<float> p) const
{
    const float hpX = getMarkerScreenX (hpFreq_);
    const float lpX = getMarkerScreenX (lpFreq_);
    const float distHp = std::abs (p.x - hpX);
    const float distLp = std::abs (p.x - lpX);

    if (distHp <= kMarkerHitPx && distHp <= distLp)
        return HP;
    if (distLp <= kMarkerHitPx)
        return LP;
    if (distHp <= kMarkerHitPx)
        return HP;

    return None;
}

void GRATRAudioProcessorEditor::FilterBarComponent::setFreqFromMouseX (float mouseX, DragTarget target)
{
    if (owner == nullptr || target == None)
        return;

    const auto inner = getInnerArea();
    const float normX = (inner.getWidth() > 0.0f) ? (mouseX - inner.getX()) / inner.getWidth() : 0.0f;
    float freq = normXToFreq (normX);

    auto& proc = owner->audioProcessor;
    if (target == HP)
    {
        const float otherFreq = proc.apvts.getRawParameterValue (GRATRAudioProcessor::kParamFilterLpFreq)->load();
        freq = juce::jmin (freq, otherFreq);
    }
    else
    {
        const float otherFreq = proc.apvts.getRawParameterValue (GRATRAudioProcessor::kParamFilterHpFreq)->load();
        freq = juce::jmax (freq, otherFreq);
    }

    const char* paramId = (target == HP) ? GRATRAudioProcessor::kParamFilterHpFreq
                                         : GRATRAudioProcessor::kParamFilterLpFreq;
    if (auto* param = proc.apvts.getParameter (paramId))
        param->setValueNotifyingHost (param->convertTo0to1 (freq));
}

void GRATRAudioProcessorEditor::FilterBarComponent::updateFromProcessor()
{
    if (owner == nullptr) return;
    auto& proc = owner->audioProcessor;
    const float newHpFreq = proc.apvts.getRawParameterValue (GRATRAudioProcessor::kParamFilterHpFreq)->load();
    const float newLpFreq = proc.apvts.getRawParameterValue (GRATRAudioProcessor::kParamFilterLpFreq)->load();
    const bool  newHpOn   = proc.apvts.getRawParameterValue (GRATRAudioProcessor::kParamFilterHpOn)->load() > 0.5f;
    const bool  newLpOn   = proc.apvts.getRawParameterValue (GRATRAudioProcessor::kParamFilterLpOn)->load() > 0.5f;

    if (newHpFreq == hpFreq_ && newLpFreq == lpFreq_ && newHpOn == hpOn_ && newLpOn == lpOn_)
        return;

    hpFreq_ = newHpFreq;
    lpFreq_ = newLpFreq;
    hpOn_   = newHpOn;
    lpOn_   = newLpOn;
    repaint();
}

void GRATRAudioProcessorEditor::FilterBarComponent::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();

    g.setColour (scheme.outline);
    g.drawRect (r, 4.0f);

    const auto inner = getInnerArea();
    g.setColour (scheme.bg);
    g.fillRect (inner);

    if (hpOn_ || lpOn_)
    {
        const float hpX = hpOn_ ? getMarkerScreenX (hpFreq_) : inner.getX();
        const float lpX = lpOn_ ? getMarkerScreenX (lpFreq_) : inner.getRight();

        if (lpX > hpX)
        {
            const auto band = juce::Rectangle<float> (hpX, inner.getY(), lpX - hpX, inner.getHeight());
            g.setColour (scheme.fg.withAlpha (0.12f));
            g.fillRect (band.getIntersection (inner));
        }
    }

    {
        const float mx = getMarkerScreenX (hpFreq_);
        if (mx >= inner.getX() && mx <= inner.getRight())
        {
            const float alpha = hpOn_ ? 1.0f : 0.25f;
            g.setColour (scheme.fg.withAlpha (alpha));
            const float hw = 2.5f;
            const float overshoot = 3.0f;
            g.fillRoundedRectangle (mx - hw, inner.getY() - overshoot, hw * 2.0f,
                                    inner.getHeight() + overshoot * 2.0f, 2.0f);
        }
    }

    {
        const float mx = getMarkerScreenX (lpFreq_);
        if (mx >= inner.getX() && mx <= inner.getRight())
        {
            const float alpha = lpOn_ ? 1.0f : 0.25f;
            g.setColour (scheme.fg.withAlpha (alpha));
            const float hw = 2.5f;
            const float overshoot = 3.0f;
            g.fillRoundedRectangle (mx - hw, inner.getY() - overshoot, hw * 2.0f,
                                    inner.getHeight() + overshoot * 2.0f, 2.0f);
        }
    }
}

void GRATRAudioProcessorEditor::FilterBarComponent::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        if (owner != nullptr)
            owner->openFilterPrompt();
        return;
    }

    currentDrag_ = hitTestMarker (e.position);
    if (currentDrag_ != None)
    {
        setFreqFromMouseX (e.position.x, currentDrag_);
        updateFromProcessor();
    }
}

void GRATRAudioProcessorEditor::FilterBarComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (currentDrag_ != None)
    {
        setFreqFromMouseX (e.position.x, currentDrag_);
        updateFromProcessor();
    }
}

void GRATRAudioProcessorEditor::FilterBarComponent::mouseUp (const juce::MouseEvent&)
{
    currentDrag_ = None;
}

void GRATRAudioProcessorEditor::FilterBarComponent::mouseMove (const juce::MouseEvent& e)
{
    const auto target = hitTestMarker (e.position);
    if (target == HP)
    {
        const int hz = juce::roundToInt (hpFreq_);
        setTooltip ("HP: " + juce::String (hz) + " Hz");
    }
    else if (target == LP)
    {
        const int hz = juce::roundToInt (lpFreq_);
        setTooltip ("LP: " + juce::String (hz) + " Hz");
    }
    else
    {
        setTooltip ({});
    }
}

void GRATRAudioProcessorEditor::FilterBarComponent::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (owner == nullptr) return;
    auto& proc = owner->audioProcessor;

    const auto target = hitTestMarker (e.position);
    if (target == HP)
    {
        if (auto* param = proc.apvts.getParameter (GRATRAudioProcessor::kParamFilterHpOn))
        {
            const bool current = param->getValue() > 0.5f;
            param->setValueNotifyingHost (current ? 0.0f : 1.0f);
        }
    }
    else if (target == LP)
    {
        if (auto* param = proc.apvts.getParameter (GRATRAudioProcessor::kParamFilterLpOn))
        {
            const bool current = param->getValue() > 0.5f;
            param->setValueNotifyingHost (current ? 0.0f : 1.0f);
        }
    }
    else
    {
        owner->openFilterPrompt();
    }
}

//========================== Editor ==========================

GRATRAudioProcessorEditor::GRATRAudioProcessorEditor (GRATRAudioProcessor& p)
: AudioProcessorEditor (&p), audioProcessor (p)
{
    const std::array<BarSlider*, 11> barSliders { &timeSlider, &modSlider, &pitchSlider, &formantSlider, &modeSlider, &inputSlider, &outputSlider, &tiltSlider, &panSlider, &mixSlider, &limThresholdSlider };

    useCustomPalette = audioProcessor.getUiUseCustomPalette();
    crtEnabled = audioProcessor.getUiCrtEnabled();
    ioSectionExpanded_ = audioProcessor.getUiIoExpanded();

    for (int i = 0; i < 2; ++i)
        customPalette[(size_t) i] = audioProcessor.getUiCustomPaletteColour (i);

    setOpaque (true);
    setBufferedToImage (true);

    applyActivePalette();
    setLookAndFeel (&lnf);
    tooltipWindow = std::make_unique<juce::TooltipWindow> (this, 250);
    tooltipWindow->setLookAndFeel (&lnf);
    tooltipWindow->setAlwaysOnTop (true);
    tooltipWindow->setInterceptsMouseClicks (false, false);

    setResizable (true, true);
    setResizeLimits (kMinW, kMinH, kMaxW, kMaxH);

    resizeConstrainer.setMinimumSize (kMinW, kMinH);
    resizeConstrainer.setMaximumSize (kMaxW, kMaxH);

    resizerCorner = std::make_unique<juce::ResizableCornerComponent> (this, &resizeConstrainer);
    addAndMakeVisible (*resizerCorner);
    resizerCorner->addMouseListener (this, true);

    addAndMakeVisible (promptOverlay);
    promptOverlay.setInterceptsMouseClicks (true, true);
    promptOverlay.setVisible (false);

    const int restoredW = juce::jlimit (kMinW, kMaxW, audioProcessor.getUiEditorWidth());
    const int restoredH = juce::jlimit (kMinH, kMaxH, audioProcessor.getUiEditorHeight());
    suppressSizePersistence = true;
    setSize (restoredW, restoredH);
    suppressSizePersistence = false;
    lastPersistedEditorW = restoredW;
    lastPersistedEditorH = restoredH;

    for (auto* slider : barSliders)
    {
        slider->setOwner (this);
        setupBar (*slider);
        addAndMakeVisible (*slider);
        slider->addListener (this);
    }

    timeSlider.setNumDecimalPlacesToDisplay (1);
    modSlider.setNumDecimalPlacesToDisplay (2);
    pitchSlider.setNumDecimalPlacesToDisplay (0);
    formantSlider.setNumDecimalPlacesToDisplay (0);
    modeSlider.setNumDecimalPlacesToDisplay (0);
    inputSlider.setNumDecimalPlacesToDisplay (1);
    outputSlider.setNumDecimalPlacesToDisplay (1);
    tiltSlider.setNumDecimalPlacesToDisplay (1);
    panSlider.setNumDecimalPlacesToDisplay (1);
    mixSlider.setNumDecimalPlacesToDisplay (1);
    limThresholdSlider.setNumDecimalPlacesToDisplay (1);

    // IO sliders start hidden (collapsible section)
    inputSlider.setVisible (false);
    outputSlider.setVisible (false);
    tiltSlider.setVisible (false);
    panSlider.setVisible (false);
    mixSlider.setVisible (false);
    limThresholdSlider.setVisible (false);

    filterBar_.setOwner (this);
    filterBar_.setScheme (activeScheme);
    addAndMakeVisible (filterBar_);
    filterBar_.setVisible (false);
    filterBar_.updateFromProcessor();

    // Chaos filter button + tooltip overlay
    chaosFilterButton.setButtonText ("");
    addAndMakeVisible (chaosFilterButton);
    chaosFilterButton.setVisible (false);
    {
        const float savedAmtF = audioProcessor.apvts.getRawParameterValue (GRATRAudioProcessor::kParamChaosAmtFilter)->load();
        const float savedSpdF = audioProcessor.apvts.getRawParameterValue (GRATRAudioProcessor::kParamChaosSpdFilter)->load();
        chaosFilterDisplay.setText ("", juce::dontSendNotification);
        chaosFilterDisplay.setInterceptsMouseClicks (true, false);
        chaosFilterDisplay.addMouseListener (this, false);
        chaosFilterDisplay.setTooltip (juce::String (juce::roundToInt (savedAmtF)) + "% | " + juce::String (juce::roundToInt (savedSpdF)) + " Hz");
        chaosFilterDisplay.setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        chaosFilterDisplay.setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
        chaosFilterDisplay.setOpaque (false);
        addAndMakeVisible (chaosFilterDisplay);
        chaosFilterDisplay.setVisible (false);
    }

    // Chaos delay button + tooltip overlay
    chaosDelayButton.setButtonText ("");
    addAndMakeVisible (chaosDelayButton);
    chaosDelayButton.setVisible (false);
    {
        const float savedAmtD = audioProcessor.apvts.getRawParameterValue (GRATRAudioProcessor::kParamChaosAmt)->load();
        const float savedSpdD = audioProcessor.apvts.getRawParameterValue (GRATRAudioProcessor::kParamChaosSpd)->load();
        chaosDelayDisplay.setText ("", juce::dontSendNotification);
        chaosDelayDisplay.setInterceptsMouseClicks (true, false);
        chaosDelayDisplay.addMouseListener (this, false);
        chaosDelayDisplay.setTooltip (juce::String (juce::roundToInt (savedAmtD)) + "% | " + juce::String (juce::roundToInt (savedSpdD)) + " Hz");
        chaosDelayDisplay.setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        chaosDelayDisplay.setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
        chaosDelayDisplay.setOpaque (false);
        addAndMakeVisible (chaosDelayDisplay);
        chaosDelayDisplay.setVisible (false);
    }

    syncButton.setButtonText ("");
    autoButton.setButtonText ("");
    triggerButton.setButtonText ("");
    midiButton.setButtonText ("");
    reverseButton.setButtonText ("");
    envGraButton.setButtonText ("");

    addAndMakeVisible (syncButton);
    addAndMakeVisible (autoButton);
    addAndMakeVisible (triggerButton);
    addAndMakeVisible (midiButton);
    addAndMakeVisible (reverseButton);
    addAndMakeVisible (envGraButton);

    // MIDI channel tooltip overlay
    const int savedChannel = audioProcessor.getMidiChannel();
    midiChannelDisplay.setText ("", juce::dontSendNotification);
    midiChannelDisplay.setInterceptsMouseClicks (true, false);
    midiChannelDisplay.addMouseListener (this, false);
    midiChannelDisplay.setTooltip (formatMidiChannelTooltip (savedChannel));
    midiChannelDisplay.setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    midiChannelDisplay.setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    midiChannelDisplay.setOpaque (false);
    addAndMakeVisible (midiChannelDisplay);

    // Env-grain tooltip overlay
    {
        const float savedTau = audioProcessor.apvts.getRawParameterValue (GRATRAudioProcessor::kParamEnvGraTau)->load();
        const float savedAmt = audioProcessor.apvts.getRawParameterValue (GRATRAudioProcessor::kParamEnvGraAmt)->load();
        envGraDisplay.setText ("", juce::dontSendNotification);
        envGraDisplay.setInterceptsMouseClicks (true, false);
        envGraDisplay.addMouseListener (this, false);
        envGraDisplay.setTooltip (formatEnvGraTooltip (savedTau, savedAmt));
        envGraDisplay.setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        envGraDisplay.setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
        envGraDisplay.setOpaque (false);
        addAndMakeVisible (envGraDisplay);
    }

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
        timeSlider.setRange (0.0, 29.0, 1.0);
    }
    else
    {
        bindSlider (timeAttachment, GRATRAudioProcessor::kParamTimeMs, timeSlider, kDefaultTimeMs);
    }

    bindSlider (modAttachment, GRATRAudioProcessor::kParamMod, modSlider, (double) GRATRAudioProcessor::kModDefault);
    bindSlider (pitchAttachment, GRATRAudioProcessor::kParamPitch, pitchSlider, (double) GRATRAudioProcessor::kPitchDefault);
    bindSlider (formantAttachment, GRATRAudioProcessor::kParamFormant, formantSlider, (double) GRATRAudioProcessor::kFormantDefault);
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
            combo.addItem ("MID",  2);
            combo.addItem ("SIDE", 3);
            combo.setJustificationType (juce::Justification::centred);
            combo.setLookAndFeel (&lnf);
            combo.setVisible (false);
        };
        setupModeCombo (modeInCombo);
        setupModeCombo (modeOutCombo);

        addAndMakeVisible (sumBusCombo);
        sumBusCombo.addItem ("ST",              1);
        sumBusCombo.addItem (juce::String::fromUTF8 (u8"\u2192M"), 2);
        sumBusCombo.addItem (juce::String::fromUTF8 (u8"\u2192S"), 3);
        sumBusCombo.setJustificationType (juce::Justification::centred);
        sumBusCombo.setLookAndFeel (&lnf);
        sumBusCombo.setVisible (false);

        addAndMakeVisible (limModeCombo);
        limModeCombo.addItem ("NONE", 1);
        limModeCombo.addItem ("WET",  2);
        limModeCombo.addItem ("GLOBAL", 3);
        limModeCombo.setJustificationType (juce::Justification::centred);
        limModeCombo.setLookAndFeel (&lnf);
        limModeCombo.setVisible (false);

        modeInAttachment  = std::make_unique<ComboBoxAttachment> (audioProcessor.apvts, GRATRAudioProcessor::kParamModeIn,  modeInCombo);
        modeOutAttachment = std::make_unique<ComboBoxAttachment> (audioProcessor.apvts, GRATRAudioProcessor::kParamModeOut, modeOutCombo);
        sumBusAttachment  = std::make_unique<ComboBoxAttachment> (audioProcessor.apvts, GRATRAudioProcessor::kParamSumBus,  sumBusCombo);
        limModeAttachment = std::make_unique<ComboBoxAttachment> (audioProcessor.apvts, GRATRAudioProcessor::kParamLimMode, limModeCombo);
    }

    // Disable numeric popup for STYLE (slider-only operation)
    modeSlider.setAllowNumericPopup (false);
    limThresholdSlider.setAllowNumericPopup (false);

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
    bindButton (envGraAttachment, GRATRAudioProcessor::kParamEnvGra, envGraButton);
    bindButton (chaosFilterAttachment, GRATRAudioProcessor::kParamChaos, chaosFilterButton);
    bindButton (chaosDelayAttachment, GRATRAudioProcessor::kParamChaosD, chaosDelayButton);

    for (auto* paramId : kUiMirrorParamIds)
        audioProcessor.apvts.addParameterListener (paramId, this);

    juce::Component::SafePointer<GRATRAudioProcessorEditor> safeThis (this);
    juce::MessageManager::callAsync ([safeThis]()
    {
        if (safeThis == nullptr)
            return;
        safeThis->applyPersistedUiStateFromProcessor (true, true);
    });

    juce::Timer::callAfterDelay (250, [safeThis]()
    {
        if (safeThis == nullptr)
            return;
        safeThis->applyPersistedUiStateFromProcessor (true, true);
    });

    juce::Timer::callAfterDelay (750, [safeThis]()
    {
        if (safeThis == nullptr)
            return;
        safeThis->applyPersistedUiStateFromProcessor (true, true);
    });

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

    dismissEditorOwnedModalPrompts (lnf);
    setPromptOverlayActive (false);

    const std::array<BarSlider*, 11> barSliders { &timeSlider, &modSlider, &pitchSlider, &formantSlider, &modeSlider, &inputSlider, &outputSlider, &tiltSlider, &panSlider, &mixSlider, &limThresholdSlider };
    for (auto* slider : barSliders)
        slider->removeListener (this);

    if (tooltipWindow != nullptr)
        tooltipWindow->setLookAndFeel (nullptr);

    modeInCombo.setLookAndFeel (nullptr);
    modeOutCombo.setLookAndFeel (nullptr);
    sumBusCombo.setLookAndFeel (nullptr);
    limModeCombo.setLookAndFeel (nullptr);

    setLookAndFeel (nullptr);
}

void GRATRAudioProcessorEditor::applyActivePalette()
{
    const auto& palette = useCustomPalette ? customPalette : defaultPalette;

    GRAScheme scheme;
    scheme.bg = palette[1];
    scheme.fg = palette[0];
    scheme.outline = palette[0];
    scheme.text = palette[0];

    activeScheme = scheme;
    lnf.setScheme (activeScheme);
    filterBar_.setScheme (activeScheme);
}

void GRATRAudioProcessorEditor::applyCrtState (bool enabled)
{
    crtEnabled = enabled;
    crtEffect.setEnabled (crtEnabled);
    setComponentEffect (crtEnabled ? &crtEffect : nullptr);
    stopTimer();
    startTimerHz (crtEnabled ? kCrtTimerHz : kIdleTimerHz);
}

void GRATRAudioProcessorEditor::applyLabelTextColour (juce::Label& label, juce::Colour colour)
{
    label.setColour (juce::Label::textColourId, colour);
}

void GRATRAudioProcessorEditor::sliderValueChanged (juce::Slider* slider)
{
    auto isBarSlider = [&] (const juce::Slider* s)
    {
        return s == &timeSlider || s == &pitchSlider || s == &modeSlider || s == &modSlider
            || s == &inputSlider || s == &outputSlider || s == &mixSlider;
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

void GRATRAudioProcessorEditor::setPromptOverlayActive (bool shouldBeActive)
{
    if (promptOverlayActive == shouldBeActive)
        return;

    promptOverlayActive = shouldBeActive;

    promptOverlay.setBounds (getLocalBounds());
    promptOverlay.setVisible (shouldBeActive);
    if (shouldBeActive)
        promptOverlay.toFront (false);

    const bool enableControls = ! shouldBeActive;
    const std::array<juce::Component*, 12> interactiveControls {
        &timeSlider, &pitchSlider, &modeSlider, &modSlider,
        &inputSlider, &outputSlider, &mixSlider,
        &syncButton, &autoButton, &triggerButton, &reverseButton, &midiButton
    };
    for (auto* control : interactiveControls)
        control->setEnabled (enableControls);

    if (resizerCorner != nullptr)
        resizerCorner->setEnabled (enableControls);

    repaint();

    if (promptOverlayActive)
        promptOverlay.toFront (false);

    anchorEditorOwnedPromptWindows (*this, lnf);
}

void GRATRAudioProcessorEditor::moved()
{
    if (promptOverlayActive)
        promptOverlay.toFront (false);

    anchorEditorOwnedPromptWindows (*this, lnf);
}

void GRATRAudioProcessorEditor::parentHierarchyChanged()
{
   #if JUCE_WINDOWS
    if (auto* peer = getPeer())
    {
        if (auto nativeHandle = peer->getNativeHandle())
        {
            static HBRUSH blackBrush = CreateSolidBrush (RGB (0, 0, 0));
            SetClassLongPtr (static_cast<HWND> (nativeHandle),
                             GCLP_HBRBACKGROUND,
                             reinterpret_cast<LONG_PTR> (blackBrush));
        }
    }
   #endif
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
                             || parameterID == GRATRAudioProcessor::kParamUiColor0
                             || parameterID == GRATRAudioProcessor::kParamUiColor1;

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
                                    || inputSlider.isMouseButtonDown()
                                    || outputSlider.isMouseButtonDown()
                                    || mixSlider.isMouseButtonDown();
        if (! anySliderDragging)
            repaint();
    }

    if (filterBar_.isVisible())
        filterBar_.updateFromProcessor();
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
        for (int i = 0; i < 2; ++i)
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

        const bool paletteSwitchChanged = (useCustomPalette != targetUseCustomPalette);
        const bool fxChanged = (crtEnabled != targetCrtEnabled);

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

        if (paletteChanged || paletteSwitchChanged)
            applyActivePalette();

        if (paletteChanged || paletteSwitchChanged || fxChanged || ioChanged)
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
        
        for (int i = 0; i < 30; ++i)
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
        timeSlider.setRange (0.0, 29.0, 1.0);
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
    const auto oldFormantFull   = cachedFormantTextFull;
    const auto oldFormantShort  = cachedFormantTextShort;
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
    cachedFormantTextFull = getFormantText();
    cachedFormantTextShort = getFormantTextShort();
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
            cachedTimeIntOnly = juce::String ((int) timeSlider.getValue());

        {
            const float mult = (float) modSliderToMultiplier (modSlider.getValue());
            if (std::abs (mult - 1.0f) < kMultEpsilon)
                cachedModIntOnly = "X1";
            else
                cachedModIntOnly = "X" + juce::String (mult, 2);
        }

        const int pitchSt = (int) std::lround (pitchSlider.getValue());
        if (pitchSt > 0)
            cachedPitchIntOnly = "+" + juce::String (pitchSt) + "st";
        else
            cachedPitchIntOnly = juce::String (pitchSt) + "st";

        const int formantSt = (int) std::lround (formantSlider.getValue());
        if (formantSt > 0)
            cachedFormantIntOnly = "+" + juce::String (formantSt) + "st";
        else
            cachedFormantIntOnly = juce::String (formantSt) + "st";
        cachedModeIntOnly    = juce::String ((int) modeSlider.getValue());
        cachedInputIntOnly   = juce::String ((int) inputSlider.getValue()) + "dB";
        cachedOutputIntOnly  = juce::String ((int) outputSlider.getValue()) + "dB";
        cachedMixIntOnly     = juce::String ((int) std::lround (mixSlider.getValue() * 100.0)) + "%";

        const float tiltVal = (float) tiltSlider.getValue();
        if (std::abs (tiltVal) < 0.05f)
            cachedTiltIntOnly = "0dB";
        else
            cachedTiltIntOnly = juce::String ((int) tiltVal) + "dB";
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
            cachedLimThresholdIntOnly = "0dB";
        else
            cachedLimThresholdIntOnly = juce::String ((int) limVal) + "dB";
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
                      || oldFormantFull   != cachedFormantTextFull
                      || oldFormantShort  != cachedFormantTextShort
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
    s.setColour (juce::Slider::trackColourId, juce::Colours::transparentBlack);
    s.setColour (juce::Slider::backgroundColourId, juce::Colours::transparentBlack);
    s.setColour (juce::Slider::thumbColourId, juce::Colours::transparentBlack);
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
    if (ms >= 1000.0f)
        return juce::String (ms / 1000.0f, 3) + " s TIME";
    return juce::String ((int) std::lround (ms)) + " ms TIME";
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
    if (ms >= 1000.0f)
        return juce::String (ms / 1000.0f, 3) + "s";
    return juce::String ((int) std::lround (ms)) + "ms";
}

juce::String GRATRAudioProcessorEditor::getPitchText() const
{
    const int st = (int) std::lround (pitchSlider.getValue());
    if (st > 0) return "+" + juce::String (st) + " st PITCH";
    if (st == 0) return "0 st PITCH";
    return juce::String (st) + " st PITCH";
}

juce::String GRATRAudioProcessorEditor::getPitchTextShort() const
{
    const int st = (int) std::lround (pitchSlider.getValue());
    if (st > 0) return "+" + juce::String (st) + "st";
    if (st == 0) return "0st";
    return juce::String (st) + "st";
}

juce::String GRATRAudioProcessorEditor::getFormantText() const
{
    const int st = (int) std::lround (formantSlider.getValue());
    if (st > 0) return "+" + juce::String (st) + "st FORMANT";
    return juce::String (st) + "st FORMANT";
}

juce::String GRATRAudioProcessorEditor::getFormantTextShort() const
{
    const int st = (int) std::lround (formantSlider.getValue());
    if (st > 0) return "+" + juce::String (st) + "st FMT";
    return juce::String (st) + "st FMT";
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
    const float mult = (float) modSliderToMultiplier (modSlider.getValue());
    if (std::abs (mult - 1.0f) < kMultEpsilon)
        return "X1 MOD";
    return "X" + juce::String (mult, 2) + " MOD";
}

juce::String GRATRAudioProcessorEditor::getModTextShort() const
{
    const float mult = (float) modSliderToMultiplier (modSlider.getValue());
    if (std::abs (mult - 1.0f) < kMultEpsilon)
        return "X1";
    return "X" + juce::String (mult, 2);
}

juce::String GRATRAudioProcessorEditor::getInputText() const
{
    const float db = (float) inputSlider.getValue();
    if (db <= kSilenceDb)
        return "-INF dB INPUT";
    if (std::abs (db) < 0.05f)
        return "0 dB INPUT";
    return juce::String (db, 1) + " dB INPUT";
}

juce::String GRATRAudioProcessorEditor::getInputTextShort() const
{
    const float db = (float) inputSlider.getValue();
    if (db <= kSilenceDb)
        return "-INF dB IN";
    if (std::abs (db) < 0.05f)
        return "0 dB IN";
    return juce::String (db, 1) + " dB IN";
}

juce::String GRATRAudioProcessorEditor::getOutputText() const
{
    const float db = (float) outputSlider.getValue();
    if (db <= kSilenceDb)
        return "-INF dB OUTPUT";
    if (std::abs (db) < 0.05f)
        return "0 dB OUTPUT";
    return juce::String (db, 1) + " dB OUTPUT";
}

juce::String GRATRAudioProcessorEditor::getOutputTextShort() const
{
    const float db = (float) outputSlider.getValue();
    if (db <= kSilenceDb)
        return "-INF dB OUT";
    if (std::abs (db) < 0.05f)
        return "0 dB OUT";
    return juce::String (db, 1) + " dB OUT";
}

juce::String GRATRAudioProcessorEditor::getMixText() const
{
    const int pct = (int) std::lround (mixSlider.getValue() * 100.0);
    return juce::String (pct) + "% MIX";
}

juce::String GRATRAudioProcessorEditor::getMixTextShort() const
{
    const int pct = (int) std::lround (mixSlider.getValue() * 100.0);
    return juce::String (pct) + "% MX";
}

juce::String GRATRAudioProcessorEditor::getTiltText() const
{
    const float db = (float) tiltSlider.getValue();
    if (std::abs (db) < 0.05f)
        return "0 dB TILT";
    return juce::String (db, 1) + " dB TILT";
}

juce::String GRATRAudioProcessorEditor::getTiltTextShort() const
{
    const float db = (float) tiltSlider.getValue();
    if (std::abs (db) < 0.05f)
        return "0 dB TLT";
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
        return "0 dB LIMIT";
    return juce::String (db, 1) + " dB LIMIT";
}

juce::String GRATRAudioProcessorEditor::getLimThresholdTextShort() const
{
    const float db = (float) limThresholdSlider.getValue();
    if (std::abs (db) < 0.05f)
        return "0 dB LIM";
    return juce::String (db, 1) + " dB LIM";
}

//========================== Legend width constants ==========================
namespace
{
    constexpr const char* kTimeLegendFull   = "5000 ms TIME";
    constexpr const char* kTimeLegendShort  = "5000ms";
    constexpr const char* kTimeLegendInt    = "5000";

    constexpr const char* kModLegendFull   = "100% MOD";
    constexpr const char* kModLegendShort  = "100%";
    constexpr const char* kModLegendInt    = "100%";

    constexpr const char* kPitchLegendFull  = "+24 st PITCH";
    constexpr const char* kPitchLegendShort = "+24st";
    constexpr const char* kPitchLegendInt   = "+24st";

    constexpr const char* kFormantLegendFull  = "100% FORMANT";
    constexpr const char* kFormantLegendShort = "100% FMT";
    constexpr const char* kFormantLegendInt   = "100%";

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
    constexpr const char* kMixLegendShort  = "100% MX";
    constexpr const char* kMixLegendInt    = "100%";

    constexpr const char* kLimLegendFull   = "-36.0 dB LIMIT";
    constexpr const char* kLimLegendShort  = "-36.0 dB LIM";
    constexpr const char* kLimLegendInt    = "-36dB";

    constexpr int kValueAreaHeightPx = 44;
    constexpr int kValueAreaRightMarginPx = 24;
    constexpr int kToggleLabelGapPx = 4;
    constexpr int kResizerCornerPx = 22;
    constexpr int kToggleBoxPx = 72;
    constexpr int kMinToggleBlocksGapPx = 10;
    constexpr int kMinSliderGapPx = 4;
    constexpr int kVersionGapPx = 8;
    constexpr int kToggleLegendCollisionPadPx = 6;
    constexpr int kTitleAreaExtraHeightPx = 4;
    constexpr int kTitleRightGapToInfoPx = 8;

    struct PopupSwatchButton final : public juce::TextButton
    {
        std::function<void()> onLeftClick;
        std::function<void()> onRightClick;

        void clicked() override
        {
            if (onLeftClick)
                onLeftClick();
            else
                juce::TextButton::clicked();
        }

        void mouseUp (const juce::MouseEvent& e) override
        {
            if (e.mods.isPopupMenu())
            {
                if (onRightClick)
                    onRightClick();
                return;
            }

            juce::TextButton::mouseUp (e);
        }
    };

    struct PopupClickableLabel final : public juce::Label
    {
        using juce::Label::Label;
        std::function<void()> onClick;

        void mouseUp (const juce::MouseEvent& e) override
        {
            juce::Label::mouseUp (e);
            if (! e.mods.isPopupMenu() && onClick)
                onClick();
        }
    };

    struct TextLayoutLabel final : public juce::Label
    {
        using juce::Label::Label;

        void paint (juce::Graphics& g) override
        {
            g.fillAll (findColour (backgroundColourId));

            if (isBeingEdited())
                return;

            const auto f     = getFont();
            const auto area  = getBorderSize().subtractedFrom (getLocalBounds()).toFloat();
            const auto alpha = isEnabled() ? 1.0f : 0.5f;

            juce::AttributedString as;
            as.append (getText(), f,
                       findColour (textColourId).withMultipliedAlpha (alpha));
            as.setJustification (getJustificationType());

            juce::TextLayout layout;
            layout.createLayout (as, area.getWidth());
            layout.draw (g, area);
        }
    };

}

static void layoutInfoPopupContent (juce::AlertWindow& aw)
{
    layoutAlertWindowButtons (aw);

    const int contentTop = kPromptBodyTopPad;
    const int contentBottom = getAlertButtonsTop (aw) - kPromptBodyBottomPad;
    const int contentH = juce::jmax (0, contentBottom - contentTop);
    const int bodyW = aw.getWidth() - (2 * kPromptInnerMargin);

    auto* viewport = dynamic_cast<juce::Viewport*> (aw.findChildWithID ("bodyViewport"));
    if (viewport == nullptr)
        return;

    viewport->setBounds (kPromptInnerMargin, contentTop, bodyW, contentH);

    auto* content = viewport->getViewedComponent();
    if (content == nullptr)
        return;

    constexpr int kItemGap = 10;
    int y = 0;
    const int innerW = bodyW - 10;

    for (int i = 0; i < content->getNumChildComponents(); ++i)
    {
        auto* child = content->getChildComponent (i);
        if (child == nullptr || ! child->isVisible())
            continue;

        int itemH = 30;
        if (auto* label = dynamic_cast<juce::Label*> (child))
        {
            auto font = label->getFont();
            const auto text = label->getText();
            const auto border = label->getBorderSize();

            if (! text.containsChar ('\n'))
            {
                itemH = (int) std::ceil (font.getHeight()) + border.getTopAndBottom();
            }
            else
            {
                juce::AttributedString as;
                as.append (text, font, label->findColour (juce::Label::textColourId));
                as.setJustification (label->getJustificationType());
                juce::TextLayout layout;
                const int textAreaW = innerW - border.getLeftAndRight();
                layout.createLayout (as, (float) juce::jmax (1, textAreaW));
                itemH = juce::jmax (20, (int) std::ceil (layout.getHeight()
                                                         + font.getDescent())
                                        + border.getTopAndBottom() + 4);
            }
        }
        else if (dynamic_cast<juce::HyperlinkButton*> (child) != nullptr)
        {
            itemH = 28;
        }

        child->setBounds (0, y, innerW, itemH);

        if (auto* label = dynamic_cast<juce::Label*> (child))
        {
            const auto& props = label->getProperties();
            if (props.contains ("poemPadFraction"))
            {
                const float padFrac = (float) props["poemPadFraction"];
                const int padPx = juce::jmax (4, (int) std::round (innerW * padFrac));
                label->setBorderSize (juce::BorderSize<int> (0, padPx, 0, padPx));

                auto font = label->getFont();
                const int textAreaW = innerW - 2 * padPx;
                for (float scale = 1.0f; scale >= 0.65f; scale -= 0.025f)
                {
                    font.setHorizontalScale (scale);
                    juce::GlyphArrangement glyphs;
                    glyphs.addLineOfText (font, label->getText(), 0.0f, 0.0f);
                    if (static_cast<int> (std::ceil (glyphs.getBoundingBox (0, -1, false).getWidth())) <= textAreaW)
                        break;
                }
                label->setFont (font);
            }
        }

        y += itemH + kItemGap;
    }

    if (y > kItemGap)
        y -= kItemGap;

    content->setSize (innerW, juce::jmax (contentH, y));
}

static void syncGraphicsPopupState (juce::AlertWindow& aw,
                                    const std::array<juce::Colour, 2>& defaultPalette,
                                    const std::array<juce::Colour, 2>& customPalette,
                                    bool useCustomPalette)
{
    if (auto* t = dynamic_cast<juce::ToggleButton*> (aw.findChildWithID ("paletteDefaultToggle")))
        t->setToggleState (! useCustomPalette, juce::dontSendNotification);
    if (auto* t = dynamic_cast<juce::ToggleButton*> (aw.findChildWithID ("paletteCustomToggle")))
        t->setToggleState (useCustomPalette, juce::dontSendNotification);

    for (int i = 0; i < 2; ++i)
    {
        if (auto* dflt = dynamic_cast<juce::TextButton*> (aw.findChildWithID ("defaultSwatch" + juce::String (i))))
            setPaletteSwatchColour (*dflt, defaultPalette[(size_t) i]);
        if (auto* custom = dynamic_cast<juce::TextButton*> (aw.findChildWithID ("customSwatch" + juce::String (i))))
        {
            setPaletteSwatchColour (*custom, customPalette[(size_t) i]);
            custom->setTooltip (colourToHexRgb (customPalette[(size_t) i]));
        }
    }

    auto applyLabelTextColourTo = [] (juce::Label* lbl, juce::Colour col)
    {
        if (lbl != nullptr)
            lbl->setColour (juce::Label::textColourId, col);
    };

    const juce::Colour activeText = useCustomPalette ? customPalette[0] : defaultPalette[0];
    applyLabelTextColourTo (dynamic_cast<juce::Label*> (aw.findChildWithID ("paletteDefaultLabel")), activeText);
    applyLabelTextColourTo (dynamic_cast<juce::Label*> (aw.findChildWithID ("paletteCustomLabel")), activeText);
    applyLabelTextColourTo (dynamic_cast<juce::Label*> (aw.findChildWithID ("paletteTitle")), activeText);
    applyLabelTextColourTo (dynamic_cast<juce::Label*> (aw.findChildWithID ("fxLabel")), activeText);
}

static void layoutGraphicsPopupContent (juce::AlertWindow& aw)
{
    layoutAlertWindowButtons (aw);

    auto snapEven = [] (int v) { return v & ~1; };

    const int contentLeft = kPromptInnerMargin;
    const int contentRight = aw.getWidth() - kPromptInnerMargin;
    const int contentW = juce::jmax (0, contentRight - contentLeft);

    auto* dfltToggle = dynamic_cast<juce::ToggleButton*> (aw.findChildWithID ("paletteDefaultToggle"));
    auto* dfltLabel  = dynamic_cast<juce::Label*> (aw.findChildWithID ("paletteDefaultLabel"));
    auto* customToggle = dynamic_cast<juce::ToggleButton*> (aw.findChildWithID ("paletteCustomToggle"));
    auto* customLabel  = dynamic_cast<juce::Label*> (aw.findChildWithID ("paletteCustomLabel"));
    auto* paletteTitle = dynamic_cast<juce::Label*> (aw.findChildWithID ("paletteTitle"));
    auto* fxToggle = dynamic_cast<juce::ToggleButton*> (aw.findChildWithID ("fxToggle"));
    auto* fxLabel  = dynamic_cast<juce::Label*> (aw.findChildWithID ("fxLabel"));
    auto* okBtn = aw.getNumButtons() > 0 ? aw.getButton (0) : nullptr;

    constexpr int toggleBox = GraphicsPromptLayout::toggleBox;
    constexpr int toggleGap = 4;
    constexpr int toggleVisualInsetLeft = 2;
    constexpr int swatchSize = GraphicsPromptLayout::swatchSize;
    constexpr int swatchGap = GraphicsPromptLayout::swatchGap;
    constexpr int columnGap = GraphicsPromptLayout::columnGap;
    constexpr int titleH = GraphicsPromptLayout::titleHeight;

    const int toggleVisualSide = juce::jlimit (14,
                                               juce::jmax (14, toggleBox - 2),
                                               (int) std::lround ((double) toggleBox * 0.65));

    const int swatchW = swatchSize;
    const int swatchH = (2 * swatchSize) + swatchGap;
    const int swatchGroupSize = (2 * swatchW) + swatchGap;
    const int swatchesH = swatchH;
    const int modeH = toggleBox;

    const int baseGap1 = GraphicsPromptLayout::titleToModeGap;
    const int baseGap2 = GraphicsPromptLayout::modeToSwatchesGap;

    const int titleY = snapEven (kPromptFooterBottomPad);
    const int footerY = getAlertButtonsTop (aw);

    const int bodyH = modeH + baseGap2 + swatchesH;
    const int bodyZoneTop = titleY + titleH + baseGap1;
    const int bodyZoneBottom = footerY - baseGap1;
    const int bodyZoneH = juce::jmax (0, bodyZoneBottom - bodyZoneTop);
    const int bodyY = snapEven (bodyZoneTop + juce::jmax (0, (bodyZoneH - bodyH) / 2));

    const int modeY = bodyY;
    const int blocksY = snapEven (modeY + modeH + baseGap2);

    const int dfltLabelW = (dfltLabel != nullptr) ? juce::jmax (38, stringWidth (dfltLabel->getFont(), "DFLT") + 2) : 40;
    const int customLabelW = (customLabel != nullptr) ? juce::jmax (38, stringWidth (customLabel->getFont(), "CSTM") + 2) : 40;
    const int fxLabelW = (fxLabel != nullptr)
                       ? juce::jmax (90, stringWidth (fxLabel->getFont(), fxLabel->getText().toUpperCase()) + 2)
                       : 96;

    const int toggleLabelStartOffset = toggleVisualInsetLeft + toggleVisualSide + toggleGap;
    const int dfltRowW = toggleLabelStartOffset + dfltLabelW;
    const int customRowW = toggleLabelStartOffset + customLabelW;
    const int fxRowW = toggleLabelStartOffset + fxLabelW;
    const int okBtnW = (okBtn != nullptr) ? okBtn->getWidth() : 96;

    const int leftColumnW = juce::jmax (swatchGroupSize, juce::jmax (dfltRowW, fxRowW));
    const int rightColumnW = juce::jmax (swatchGroupSize, juce::jmax (customRowW, okBtnW));
    const int columnsRowW = leftColumnW + columnGap + rightColumnW;
    const int columnsX = snapEven (contentLeft + juce::jmax (0, (contentW - columnsRowW) / 2));
    const int col0X = columnsX;
    const int col1X = columnsX + leftColumnW + columnGap;

    const int dfltX = col0X;
    const int customX = col1X;

    const int defaultSwatchStartX = col0X;
    const int customSwatchStartX = col1X;

    if (paletteTitle != nullptr)
    {
        const int paletteW = juce::jmax (100, juce::jmin (leftColumnW, contentRight - col0X));
        paletteTitle->setBounds (col0X,
                                 titleY,
                                 paletteW,
                                 titleH);
    }

    if (dfltToggle != nullptr)   dfltToggle->setBounds (dfltX, modeY, toggleBox, toggleBox);
    if (dfltLabel != nullptr)    dfltLabel->setBounds (dfltX + toggleLabelStartOffset, modeY, dfltLabelW, toggleBox);
    if (customToggle != nullptr) customToggle->setBounds (customX, modeY, toggleBox, toggleBox);
    if (customLabel != nullptr)  customLabel->setBounds (customX + toggleLabelStartOffset, modeY, customLabelW, toggleBox);

    auto placeSwatchGroup = [&] (const juce::String& prefix, int startX)
    {
        const int startY = blocksY;

        for (int i = 0; i < 2; ++i)
        {
            if (auto* b = dynamic_cast<juce::TextButton*> (aw.findChildWithID (prefix + juce::String (i))))
            {
                b->setBounds (startX + i * (swatchW + swatchGap),
                              startY,
                              swatchW,
                              swatchH);
            }
        }
    };

    placeSwatchGroup ("defaultSwatch", defaultSwatchStartX);
    placeSwatchGroup ("customSwatch", customSwatchStartX);

    if (okBtn != nullptr)
    {
        auto okR = okBtn->getBounds();
        okR.setX (col1X);
        okR.setY (footerY);
        okBtn->setBounds (okR);

        const int fxY = snapEven (footerY + juce::jmax (0, (okR.getHeight() - toggleBox) / 2));
        const int fxX = col0X;
        if (fxToggle != nullptr) fxToggle->setBounds (fxX, fxY, toggleBox, toggleBox);
        if (fxLabel != nullptr)  fxLabel->setBounds (fxX + toggleLabelStartOffset, fxY, fxLabelW, toggleBox);
    }

    auto updateVisualBounds = [] (juce::Component* c, int& minX, int& maxR)
    {
        if (c == nullptr)
            return;

        const auto r = c->getBounds();
        minX = juce::jmin (minX, r.getX());
        maxR = juce::jmax (maxR, r.getRight());
    };

    int visualMinX = aw.getWidth();
    int visualMaxR = 0;

    updateVisualBounds (paletteTitle, visualMinX, visualMaxR);
    updateVisualBounds (dfltToggle, visualMinX, visualMaxR);
    updateVisualBounds (dfltLabel, visualMinX, visualMaxR);
    updateVisualBounds (customToggle, visualMinX, visualMaxR);
    updateVisualBounds (customLabel, visualMinX, visualMaxR);
    updateVisualBounds (fxToggle, visualMinX, visualMaxR);
    updateVisualBounds (fxLabel, visualMinX, visualMaxR);
    updateVisualBounds (okBtn, visualMinX, visualMaxR);

    for (int i = 0; i < 2; ++i)
    {
        updateVisualBounds (aw.findChildWithID ("defaultSwatch" + juce::String (i)), visualMinX, visualMaxR);
        updateVisualBounds (aw.findChildWithID ("customSwatch" + juce::String (i)), visualMinX, visualMaxR);
    }

    if (visualMaxR > visualMinX)
    {
        const int leftMarginToPrompt = visualMinX;
        const int rightMarginToPrompt = aw.getWidth() - visualMaxR;

        int dx = (rightMarginToPrompt - leftMarginToPrompt) / 2;

        const int minDx = contentLeft - visualMinX;
        const int maxDx = contentRight - visualMaxR;
        dx = juce::jlimit (minDx, maxDx, dx);

        if (dx != 0)
        {
            auto shiftX = [dx] (juce::Component* c)
            {
                if (c == nullptr)
                    return;

                auto r = c->getBounds();
                r.setX (r.getX() + dx);
                c->setBounds (r);
            };

            shiftX (paletteTitle);
            shiftX (dfltToggle);
            shiftX (dfltLabel);
            shiftX (customToggle);
            shiftX (customLabel);
            shiftX (fxToggle);
            shiftX (fxLabel);
            shiftX (okBtn);

            for (int i = 0; i < 2; ++i)
            {
                shiftX (aw.findChildWithID ("defaultSwatch" + juce::String (i)));
                shiftX (aw.findChildWithID ("customSwatch" + juce::String (i)));
            }
        }
    }
}

//========================== openNumericEntryPopupForSlider ==========================

void GRATRAudioProcessorEditor::openNumericEntryPopupForSlider (juce::Slider& s)
{
    lnf.setScheme (activeScheme);
    const auto scheme = activeScheme;

    juce::String suffix;
    juce::String suffixShort;
    const bool isTimeSyncMode = (&s == &timeSlider && syncButton.getToggleState());
    if (&s == &timeSlider)
    {
        if (isTimeSyncMode) { suffix = ""; suffixShort = ""; }
        else                { suffix = " MS"; suffixShort = " MS"; }
    }
    else if (&s == &pitchSlider)     { suffix = " ST PITCH";   suffixShort = " ST"; }
    else if (&s == &formantSlider)   { suffix = " ST FORMANT"; suffixShort = " ST"; }
    else if (&s == &modSlider)       { suffix = " X MOD";      suffixShort = " X"; }
    else if (&s == &inputSlider)     { suffix = " DB INPUT";   suffixShort = " DB IN"; }
    else if (&s == &outputSlider)    { suffix = " DB OUTPUT";  suffixShort = " DB OUT"; }
    else if (&s == &mixSlider)       { suffix = " % MIX";      suffixShort = " % MIX"; }
    else if (&s == &panSlider)       { suffix = " % PAN";      suffixShort = " %"; }
    const juce::String suffixText = suffix.trimStart();
    const juce::String suffixTextShort = suffixShort.trimStart();
    const bool isPercentPrompt = (&s == &mixSlider || &s == &panSlider);

    auto* aw = new juce::AlertWindow ("", "", juce::AlertWindow::NoIcon);
    aw->setLookAndFeel (&lnf);

    juce::String currentDisplay;
    if (&s == &modSlider)
        currentDisplay = juce::String (modSliderToMultiplier (s.getValue()), 2);
    else if (&s == &panSlider)
        currentDisplay = juce::String (juce::jlimit (0.0, 100.0, s.getValue() * 100.0), 0);
    else if (&s == &pitchSlider)
    {
        const int st = (int) std::lround (s.getValue());
        currentDisplay = (st > 0) ? ("+" + juce::String (st)) : juce::String (st);
    }
    else if (&s == &formantSlider)
    {
        const int st = (int) std::lround (s.getValue());
        currentDisplay = (st > 0) ? ("+" + juce::String (st)) : juce::String (st);
    }
    else
        currentDisplay = s.getTextFromValue (s.getValue());

    aw->addTextEditor ("val", currentDisplay, juce::String());

    struct SyncDivisionInputFilter : juce::TextEditor::InputFilter
    {
        int maxLen;
        SyncDivisionInputFilter (int maxLength) : maxLen (maxLength) {}
        juce::String filterNewText (juce::TextEditor& editor, const juce::String& newText) override
        {
            juce::ignoreUnused (editor);
            juce::String result;
            for (auto c : newText)
            {
                if (juce::CharacterFunctions::isDigit (c) || c == '/' || c == 'T' || c == 't' || c == '.')
                    result += c;
                if (maxLen > 0 && result.length() >= maxLen)
                    break;
            }
            return result;
        }
    };

    juce::Rectangle<int> editorBaseBounds;
    std::function<void()> layoutValueAndSuffix;
    juce::Label* suffixLabel = nullptr;

    if (auto* te = aw->getTextEditor ("val"))
    {
        const auto& f = kBoldFont40();
        te->setFont (f);
        te->applyFontToAllText (f);

        auto r = te->getBounds();
        r.setHeight ((int) (f.getHeight() * kPromptEditorHeightScale) + kPromptEditorHeightPadPx);
        r.setY (juce::jmax (kPromptEditorMinTopPx, r.getY() - kPromptEditorRaiseYPx));
        editorBaseBounds = r;

        suffixLabel = new juce::Label ("suffix", suffixText);
        suffixLabel->setComponentID (kPromptSuffixLabelId);
        suffixLabel->setJustificationType (juce::Justification::centredLeft);
        applyLabelTextColour (*suffixLabel, scheme.text);
        suffixLabel->setBorderSize (juce::BorderSize<int> (0));
        suffixLabel->setFont (f);
        aw->addAndMakeVisible (suffixLabel);

        juce::String worstCaseText;
        if (&s == &timeSlider)
            worstCaseText = isTimeSyncMode ? "1/64T." : "10000.000";
        else if (&s == &pitchSlider)
            worstCaseText = "+24";
        else if (&s == &formantSlider)
            worstCaseText = "-12";
        else if (&s == &modSlider)
            worstCaseText = "4.00";
        else if (&s == &inputSlider)
            worstCaseText = "-100.0";
        else if (&s == &outputSlider)
            worstCaseText = "-100.0";
        else if (&s == &mixSlider)
            worstCaseText = "100.00";
        else if (&s == &panSlider)
            worstCaseText = "100";
        else
            worstCaseText = "999.99";

        const int maxInputTextW = juce::jmax (1, stringWidth (f, worstCaseText));

        layoutValueAndSuffix = [aw, te, suffixLabel, editorBaseBounds, isPercentPrompt, suffixText, suffixTextShort, maxInputTextW]()
        {
            const int contentPad = kPromptInlineContentPadPx;
            const int contentLeft = contentPad;
            const int contentRight = (aw != nullptr ? aw->getWidth() - contentPad : editorBaseBounds.getRight());
            const int availableW = contentRight - contentLeft;
            const int contentCenter = (contentLeft + contentRight) / 2;

            const int fullLabelW = stringWidth (suffixLabel->getFont(), suffixText) + 2;
            const bool stickPercentFull = suffixText.containsChar ('%');
            const int spaceWFull = stickPercentFull ? 0 : juce::jmax (2, stringWidth (suffixLabel->getFont(), " "));
            const int worstCaseFullW = maxInputTextW + spaceWFull + fullLabelW;

            const bool useShort = (worstCaseFullW > availableW) && suffixTextShort != suffixText;
            const juce::String& activeSuffix = useShort ? suffixTextShort : suffixText;
            suffixLabel->setText (activeSuffix, juce::dontSendNotification);

            const auto txt = te->getText();
            const int textW = juce::jmax (1, stringWidth (te->getFont(), txt));
            int labelW = stringWidth (suffixLabel->getFont(), activeSuffix) + 2;
            auto er = te->getBounds();

            const bool stickPercentToValue = activeSuffix.containsChar ('%');
            const int spaceW = stickPercentToValue ? 0 : juce::jmax (2, stringWidth (te->getFont(), " "));
            const int minGapPx = juce::jmax (1, spaceW);

            constexpr int kEditorTextPadPx = 12;
            constexpr int kMinEditorWidthPx = 24;
            const int editorW = juce::jlimit (kMinEditorWidthPx, editorBaseBounds.getWidth(), textW + (kEditorTextPadPx * 2));
            er.setWidth (editorW);

            const int combinedW = textW + minGapPx + labelW;
            int blockLeft = contentCenter - (combinedW / 2);
            const int minBlockLeft = contentLeft;
            const int maxBlockLeft = juce::jmax (minBlockLeft, contentRight - combinedW);
            blockLeft = juce::jlimit (minBlockLeft, maxBlockLeft, blockLeft);

            int teX = blockLeft - ((editorW - textW) / 2);
            const int minTeX = contentLeft;
            const int maxTeX = juce::jmax (minTeX, contentRight - editorW);
            teX = juce::jlimit (minTeX, maxTeX, teX);
            er.setX (teX);
            te->setBounds (er);

            const int textLeftActual = er.getX() + (er.getWidth() - textW) / 2;
            int labelX = textLeftActual + textW + minGapPx;
            const int minLabelX = contentLeft;
            const int maxLabelX = juce::jmax (minLabelX, contentRight - labelW);
            labelX = juce::jlimit (minLabelX, maxLabelX, labelX);

            const int labelY = er.getY();
            const int labelH = juce::jmax (1, er.getHeight());
            suffixLabel->setBounds (labelX, labelY, labelW, labelH);
        };

        te->setBounds (editorBaseBounds);
        int labelW0 = stringWidth (suffixLabel->getFont(), suffixText) + 2;
        suffixLabel->setBounds (r.getRight() + 2, r.getY() + 1, labelW0, juce::jmax (1, r.getHeight() - 2));

        if (layoutValueAndSuffix)
            layoutValueAndSuffix();

        double minVal = 0.0, maxVal = 1.0;
        int maxLen = 0, maxDecs = 4;

        if (&s == &timeSlider)
        {
            if (isTimeSyncMode) { minVal = 0.0; maxVal = 29.0; maxDecs = 0; maxLen = 6; }
            else                { minVal = 0.0; maxVal = 10000.0; maxDecs = 3; maxLen = 9; }
        }
        else if (&s == &pitchSlider)  { minVal = -24.0; maxVal = 24.0; maxDecs = 0; maxLen = 3; }
        else if (&s == &formantSlider) { minVal = -12.0; maxVal = 12.0; maxDecs = 0; maxLen = 3; }
        else if (&s == &modSlider)    { minVal = 0.25;  maxVal = 4.0; maxDecs = 2; maxLen = 4; }
        else if (&s == &inputSlider)  { minVal = -100.0; maxVal = 0.0; maxDecs = 1; maxLen = 6; }
        else if (&s == &outputSlider) { minVal = -100.0; maxVal = 24.0; maxDecs = 1; maxLen = 6; }
        else if (&s == &mixSlider)    { minVal = 0.0; maxVal = 100.0; maxDecs = 1; maxLen = 5; }
        else if (&s == &panSlider)    { minVal = 0.0; maxVal = 100.0; maxDecs = 0; maxLen = 3; }

        if (&s == &timeSlider && isTimeSyncMode)
            te->setInputFilter (new SyncDivisionInputFilter (maxLen), true);
        else
            te->setInputFilter (new NumericInputFilter (minVal, maxVal, maxLen, maxDecs), true);

        te->onTextChange = [te, layoutValueAndSuffix, maxDecs]() mutable
        {
            auto txt = te->getText();
            int dot = txt.indexOfChar('.');
            if (dot >= 0)
            {
                int decimals = txt.length() - dot - 1;
                if (decimals > maxDecs)
                    te->setText (txt.substring (0, dot + 1 + maxDecs), juce::dontSendNotification);
            }
            if (layoutValueAndSuffix) layoutValueAndSuffix();
        };
    }

    aw->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    applyPromptShellSize (*aw);
    layoutAlertWindowButtons (*aw);

    const juce::Font& kPromptFont = kBoldFont40();
    preparePromptTextEditor (*aw, "val", scheme.bg, scheme.text, scheme.fg, kPromptFont, false);

    if (suffixLabel != nullptr && ! editorBaseBounds.isEmpty())
    {
        if (auto* te = aw->getTextEditor ("val"))
            suffixLabel->setFont (te->getFont());
        if (layoutValueAndSuffix) layoutValueAndSuffix();
    }

    styleAlertButtons (*aw, lnf);

    juce::Component::SafePointer<GRATRAudioProcessorEditor> safeThis (this);
    juce::Slider* sliderPtr = &s;

    setPromptOverlayActive (true);
    aw->setLookAndFeel (&lnf);

    if (safeThis != nullptr)
    {
        fitAlertWindowToEditor (*aw, safeThis.getComponent(), [&] (juce::AlertWindow& a)
        {
            if (layoutValueAndSuffix) layoutValueAndSuffix();
            layoutAlertWindowButtons (a);
            preparePromptTextEditor (a, "val", scheme.bg, scheme.text, scheme.fg, kPromptFont, false);
        });
        embedAlertWindowInOverlay (safeThis.getComponent(), aw);
    }
    else
    {
        aw->centreAroundComponent (this, aw->getWidth(), aw->getHeight());
        bringPromptWindowToFront (*aw);
        aw->repaint();
    }

    {
        preparePromptTextEditor (*aw, "val", scheme.bg, scheme.text, scheme.fg, kPromptFont, false);
        if (auto* suffixLbl = dynamic_cast<juce::Label*> (aw->findChildWithID (kPromptSuffixLabelId)))
        {
            if (auto* te = aw->getTextEditor ("val"))
                suffixLbl->setFont (te->getFont());
        }
        if (layoutValueAndSuffix) layoutValueAndSuffix();

        juce::Component::SafePointer<juce::AlertWindow> safeAw (aw);
        juce::Component::SafePointer<GRATRAudioProcessorEditor> safeThisPtr (this);
        juce::MessageManager::callAsync ([safeAw, safeThisPtr]()
        {
            if (safeAw == nullptr) return;
            bringPromptWindowToFront (*safeAw);
            safeAw->repaint();
        });
    }

    aw->enterModalState (true,
        juce::ModalCallbackFunction::create ([safeThis, sliderPtr, aw] (int result) mutable
        {
            std::unique_ptr<juce::AlertWindow> killer (aw);
            if (safeThis != nullptr) safeThis->setPromptOverlayActive (false);
            if (safeThis == nullptr || sliderPtr == nullptr) return;
            if (result != 1) return;

            const auto txt = aw->getTextEditorContents ("val").trim();
            auto normalised = txt.replaceCharacter (',', '.');
            double v = 0.0;

            if (safeThis != nullptr && sliderPtr == &safeThis->timeSlider
                && safeThis->syncButton.getToggleState())
            {
                int foundIndex = -1;
                auto choices = safeThis->audioProcessor.getTimeSyncChoices();
                for (int i = 0; i < choices.size(); ++i)
                {
                    if (txt.equalsIgnoreCase (choices[i]) || txt.equalsIgnoreCase (choices[i].replace ("/", "")))
                    { foundIndex = i; break; }
                }
                if (foundIndex < 0)
                {
                    juce::String t = normalised.trimStart();
                    while (t.startsWithChar ('+')) t = t.substring (1).trimStart();
                    const juce::String numericToken = t.initialSectionContainingOnly ("0123456789");
                    foundIndex = numericToken.getIntValue();
                }
                v = (double) juce::jlimit (0, 29, foundIndex);
            }
            else
            {
                juce::String t = normalised.trimStart();
                while (t.startsWithChar ('+')) t = t.substring (1).trimStart();
                const juce::String numericToken = t.initialSectionContainingOnly ("0123456789.,-");
                v = numericToken.getDoubleValue();

                // Handle negative sign for pitch
                if (normalised.trimStart().startsWithChar ('-'))
                    v = -std::abs (v);

                // user typed percent for mix/pan; convert to slider's [0,1]
                if (safeThis != nullptr && (sliderPtr == &safeThis->mixSlider
                                         || sliderPtr == &safeThis->panSlider))
                    v *= 0.01;

                // user typed multiplier for mod; convert to slider's [0,1]
                if (safeThis != nullptr && sliderPtr == &safeThis->modSlider)
                    v = multiplierToModSlider (v);
            }

            const auto range = sliderPtr->getRange();
            double clamped = juce::jlimit (range.getStart(), range.getEnd(), v);

            if (safeThis != nullptr && sliderPtr == &safeThis->timeSlider
                && !safeThis->syncButton.getToggleState())
                clamped = roundToDecimals (clamped, 2);

            sliderPtr->setValue (clamped, juce::sendNotificationSync);
        }));
}

// ── Filter Prompt (HP/LP frequency + slope) ───────────────────────
void GRATRAudioProcessorEditor::openFilterPrompt()
{
    lnf.setScheme (activeScheme);
    const auto scheme = activeScheme;

    auto& proc = audioProcessor;
    const float hpFreq = proc.apvts.getRawParameterValue (GRATRAudioProcessor::kParamFilterHpFreq)->load();
    const float lpFreq = proc.apvts.getRawParameterValue (GRATRAudioProcessor::kParamFilterLpFreq)->load();
    const int hpSlope  = juce::roundToInt (proc.apvts.getRawParameterValue (GRATRAudioProcessor::kParamFilterHpSlope)->load());
    const int lpSlope  = juce::roundToInt (proc.apvts.getRawParameterValue (GRATRAudioProcessor::kParamFilterLpSlope)->load());
    const bool hpOn    = proc.apvts.getRawParameterValue (GRATRAudioProcessor::kParamFilterHpOn)->load() > 0.5f;
    const bool lpOn    = proc.apvts.getRawParameterValue (GRATRAudioProcessor::kParamFilterLpOn)->load() > 0.5f;

    auto* aw = new juce::AlertWindow ("", "", juce::AlertWindow::NoIcon);
    aw->setLookAndFeel (&lnf);

    struct PromptBar : public juce::Component
    {
        GRAScheme colours;
        float  value01    = 0.5f;
        float  default01  = 0.5f;
        std::function<void (float)> onValueChanged;
        PromptBar (const GRAScheme& s, float initial01, float def01) : colours (s), value01 (initial01), default01 (def01) {}
        void paint (juce::Graphics& g) override
        {
            const auto r = getLocalBounds().toFloat();
            g.setColour (colours.outline); g.drawRect (r, 4.0f);
            const float pad = 7.0f; auto inner = r.reduced (pad);
            g.setColour (colours.bg); g.fillRect (inner);
            const float fillW = juce::jlimit (0.0f, inner.getWidth(), inner.getWidth() * value01);
            g.setColour (colours.fg); g.fillRect (inner.withWidth (fillW));
        }
        void mouseDown (const juce::MouseEvent& e) override { updateFromMouse (e); }
        void mouseDrag (const juce::MouseEvent& e) override { updateFromMouse (e); }
        void mouseDoubleClick (const juce::MouseEvent&) override { setValue (default01); }
        void setValue (float v) { value01 = juce::jlimit (0.0f, 1.0f, v); repaint(); if (onValueChanged) onValueChanged (value01); }
    private:
        void updateFromMouse (const juce::MouseEvent& e)
        { const float pad = 7.0f; const float innerW = (float) getWidth() - pad * 2.0f; setValue (innerW > 0.0f ? ((float) e.x - pad) / innerW : 0.0f); }
    };

    auto freqToNorm = [] (float freq) -> float
    { constexpr float minF = 20.0f, maxF = 20000.0f; return std::log2 (juce::jlimit (minF, maxF, freq) / minF) / std::log2 (maxF / minF); };
    auto normToFreq = [] (float n) -> float
    { constexpr float minF = 20.0f, maxF = 20000.0f; return minF * std::pow (2.0f, juce::jlimit (0.0f, 1.0f, n) * std::log2 (maxF / minF)); };

    aw->addTextEditor ("hpFreq", juce::String (juce::roundToInt (hpFreq)), juce::String());
    auto* hpBar = new PromptBar (scheme, freqToNorm (hpFreq), freqToNorm (GRATRAudioProcessor::kFilterHpFreqDefault));
    aw->addAndMakeVisible (hpBar);
    aw->addTextEditor ("lpFreq", juce::String (juce::roundToInt (lpFreq)), juce::String());
    auto* lpBar = new PromptBar (scheme, freqToNorm (lpFreq), freqToNorm (GRATRAudioProcessor::kFilterLpFreqDefault));
    aw->addAndMakeVisible (lpBar);

    auto* hpToggle = new juce::ToggleButton ("");
    hpToggle->setToggleState (hpOn, juce::dontSendNotification);
    hpToggle->setLookAndFeel (&lnf);
    aw->addAndMakeVisible (hpToggle);

    auto* lpToggle = new juce::ToggleButton ("");
    lpToggle->setToggleState (lpOn, juce::dontSendNotification);
    lpToggle->setLookAndFeel (&lnf);
    aw->addAndMakeVisible (lpToggle);

    auto slopeToText = [] (int s) -> juce::String { if (s == 0) return "6dB"; if (s == 1) return "12dB"; return "24dB"; };

    auto* hpSlopeLabel = new juce::Label ("", slopeToText (hpSlope));
    hpSlopeLabel->setJustificationType (juce::Justification::centredRight);
    hpSlopeLabel->setColour (juce::Label::textColourId, scheme.text);
    aw->addAndMakeVisible (hpSlopeLabel);

    auto* lpSlopeLabel = new juce::Label ("", slopeToText (lpSlope));
    lpSlopeLabel->setJustificationType (juce::Justification::centredRight);
    lpSlopeLabel->setColour (juce::Label::textColourId, scheme.text);
    aw->addAndMakeVisible (lpSlopeLabel);

    auto hpSlopeVal  = std::make_shared<int> (hpSlope);
    auto lpSlopeVal  = std::make_shared<int> (lpSlope);
    auto syncing     = std::make_shared<bool> (false);
    auto layoutFn    = std::make_shared<std::function<void()>> ([] {});

    juce::Component::SafePointer<GRATRAudioProcessorEditor> safeThis (this);

    auto pushParams = [safeThis, hpToggle, lpToggle, hpSlopeVal, lpSlopeVal, normToFreq, aw] ()
    {
        if (safeThis == nullptr) return;
        auto& p = safeThis->audioProcessor;
        auto setP = [&p] (const char* id, float plain)
        { if (auto* param = p.apvts.getParameter (id)) param->setValueNotifyingHost (param->convertTo0to1 (plain)); };

        auto* hpTe = aw->getTextEditor ("hpFreq");
        auto* lpTe = aw->getTextEditor ("lpFreq");
        float hpF = hpTe ? juce::jlimit (20.0f, 20000.0f, (float) hpTe->getText().getIntValue()) : 20.0f;
        float lpF = lpTe ? juce::jlimit (20.0f, 20000.0f, (float) lpTe->getText().getIntValue()) : 20000.0f;
        if (hpF > lpF) { const float mid = (hpF + lpF) * 0.5f; hpF = mid; lpF = mid; }
        if (hpTe) setP (GRATRAudioProcessor::kParamFilterHpFreq, hpF);
        if (lpTe) setP (GRATRAudioProcessor::kParamFilterLpFreq, lpF);
        setP (GRATRAudioProcessor::kParamFilterHpSlope, (float) *hpSlopeVal);
        setP (GRATRAudioProcessor::kParamFilterLpSlope, (float) *lpSlopeVal);
        if (auto* hpOnParam = p.apvts.getParameter (GRATRAudioProcessor::kParamFilterHpOn))
            hpOnParam->setValueNotifyingHost (hpToggle->getToggleState() ? 1.0f : 0.0f);
        if (auto* lpOnParam = p.apvts.getParameter (GRATRAudioProcessor::kParamFilterLpOn))
            lpOnParam->setValueNotifyingHost (lpToggle->getToggleState() ? 1.0f : 0.0f);
        safeThis->filterBar_.updateFromProcessor();
    };

    hpSlopeLabel->setInterceptsMouseClicks (true, false);
    struct SlopeCycler : public juce::MouseListener
    {
        std::shared_ptr<int> val; juce::Label* label;
        std::function<juce::String (int)> toText; std::function<void()> push;
        std::shared_ptr<std::function<void()>> layout;
        void mouseDown (const juce::MouseEvent&) override
        { *val = (*val + 1) % 3; label->setText (toText (*val), juce::dontSendNotification); push(); if (layout && *layout) (*layout)(); }
    };
    auto* hpCycler = new SlopeCycler();
    hpCycler->val = hpSlopeVal; hpCycler->label = hpSlopeLabel; hpCycler->toText = slopeToText;
    hpCycler->push = pushParams; hpCycler->layout = layoutFn;
    hpSlopeLabel->addMouseListener (hpCycler, false);

    lpSlopeLabel->setInterceptsMouseClicks (true, false);
    auto* lpCycler = new SlopeCycler();
    lpCycler->val = lpSlopeVal; lpCycler->label = lpSlopeLabel; lpCycler->toText = slopeToText;
    lpCycler->push = pushParams; lpCycler->layout = layoutFn;
    lpSlopeLabel->addMouseListener (lpCycler, false);

    auto hpCyclerGuard = std::shared_ptr<SlopeCycler> (hpCycler);
    auto lpCyclerGuard = std::shared_ptr<SlopeCycler> (lpCycler);

    hpToggle->onClick = pushParams;
    lpToggle->onClick = pushParams;

    auto* hpTe = aw->getTextEditor ("hpFreq");
    auto* lpTe = aw->getTextEditor ("lpFreq");

    auto barToText = [aw, syncing, normToFreq, freqToNorm, pushParams, hpBar, lpBar] (const char* editorId, float v01, bool isHp)
    {
        if (*syncing) return;
        *syncing = true;
        if (isHp) v01 = juce::jmin (v01, lpBar->value01); else v01 = juce::jmax (v01, hpBar->value01);
        if (isHp) { hpBar->value01 = v01; hpBar->repaint(); } else { lpBar->value01 = v01; lpBar->repaint(); }
        if (auto* te = aw->getTextEditor (editorId))
        { te->setText (juce::String (juce::roundToInt (normToFreq (v01))), juce::sendNotification); te->selectAll(); }
        *syncing = false;
        pushParams();
    };

    hpBar->onValueChanged = [barToText] (float v) { barToText ("hpFreq", v, true); };
    lpBar->onValueChanged = [barToText] (float v) { barToText ("lpFreq", v, false); };

    auto textToBar = [syncing, freqToNorm, normToFreq, pushParams, aw, hpBar, lpBar] (juce::TextEditor* te, PromptBar* bar, bool isHp)
    {
        if (*syncing || te == nullptr || bar == nullptr) return;
        *syncing = true;
        float freq = juce::jlimit (20.0f, 20000.0f, (float) te->getText().getIntValue());
        auto* otherTe = aw->getTextEditor (isHp ? "lpFreq" : "hpFreq");
        const float otherFreq = otherTe ? juce::jlimit (20.0f, 20000.0f, (float) otherTe->getText().getIntValue()) : (isHp ? 20000.0f : 20.0f);
        if (isHp) freq = juce::jmin (freq, otherFreq); else freq = juce::jmax (freq, otherFreq);
        te->setText (juce::String (juce::roundToInt (freq)), juce::dontSendNotification);
        bar->value01 = freqToNorm (freq); bar->repaint();
        *syncing = false;
        pushParams();
    };

    if (hpTe != nullptr)
        hpTe->onTextChange = [syncing, textToBar, hpTe, hpBar, layoutFn] () { textToBar (hpTe, hpBar, true); if (*layoutFn) (*layoutFn)(); };
    if (lpTe != nullptr)
        lpTe->onTextChange = [syncing, textToBar, lpTe, lpBar, layoutFn] () { textToBar (lpTe, lpBar, false); if (*layoutFn) (*layoutFn)(); };

    aw->addButton ("OK",     1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    applyPromptShellSize (*aw);
    layoutAlertWindowButtons (*aw);

    const int margin     = kPromptInnerMargin;
    const int toggleSide = 26;
    const juce::Font  promptFont (juce::FontOptions (34.0f).withStyle ("Bold"));
    const juce::Font  slopeFont  (juce::FontOptions (24.0f).withStyle ("Bold"));

    auto* hpNameLabel = new juce::Label ("", "HP");
    hpNameLabel->setJustificationType (juce::Justification::centredLeft);
    hpNameLabel->setColour (juce::Label::textColourId, scheme.text);
    hpNameLabel->setBorderSize (juce::BorderSize<int> (0));
    hpNameLabel->setFont (promptFont);
    aw->addAndMakeVisible (hpNameLabel);

    auto* lpNameLabel = new juce::Label ("", "LP");
    lpNameLabel->setJustificationType (juce::Justification::centredLeft);
    lpNameLabel->setColour (juce::Label::textColourId, scheme.text);
    lpNameLabel->setBorderSize (juce::BorderSize<int> (0));
    lpNameLabel->setFont (promptFont);
    aw->addAndMakeVisible (lpNameLabel);

    auto* hpHzLabel = new juce::Label ("", "Hz");
    hpHzLabel->setJustificationType (juce::Justification::centredLeft);
    hpHzLabel->setColour (juce::Label::textColourId, scheme.text);
    hpHzLabel->setBorderSize (juce::BorderSize<int> (0));
    hpHzLabel->setFont (promptFont);
    aw->addAndMakeVisible (hpHzLabel);

    auto* lpHzLabel = new juce::Label ("", "Hz");
    lpHzLabel->setJustificationType (juce::Justification::centredLeft);
    lpHzLabel->setColour (juce::Label::textColourId, scheme.text);
    lpHzLabel->setBorderSize (juce::BorderSize<int> (0));
    lpHzLabel->setFont (promptFont);
    aw->addAndMakeVisible (lpHzLabel);

    preparePromptTextEditor (*aw, "hpFreq", scheme.bg, scheme.text, scheme.fg, promptFont, false);
    preparePromptTextEditor (*aw, "lpFreq", scheme.bg, scheme.text, scheme.fg, promptFont, false);

    struct ToggleForwarder : public juce::MouseListener
    { juce::ToggleButton* toggle = nullptr;
      void mouseDown (const juce::MouseEvent&) override { if (toggle != nullptr) toggle->setToggleState (! toggle->getToggleState(), juce::sendNotification); } };
    hpNameLabel->setInterceptsMouseClicks (true, false);
    auto* hpFwd = new ToggleForwarder(); hpFwd->toggle = hpToggle;
    hpNameLabel->addMouseListener (hpFwd, false);
    lpNameLabel->setInterceptsMouseClicks (true, false);
    auto* lpFwd = new ToggleForwarder(); lpFwd->toggle = lpToggle;
    lpNameLabel->addMouseListener (lpFwd, false);
    auto hpFwdGuard = std::shared_ptr<ToggleForwarder> (hpFwd);
    auto lpFwdGuard = std::shared_ptr<ToggleForwarder> (lpFwd);

    auto layoutRows = [aw, hpToggle, lpToggle,
                        hpNameLabel, lpNameLabel, hpHzLabel, lpHzLabel,
                        hpSlopeLabel, lpSlopeLabel, hpBar, lpBar,
                        promptFont, slopeFont, toggleSide, margin] ()
    {
        auto* hpTe = aw->getTextEditor ("hpFreq");
        auto* lpTe = aw->getTextEditor ("lpFreq");
        if (hpTe == nullptr || lpTe == nullptr) return;

        const int buttonsTop = getAlertButtonsTop (*aw);
        const int rowH       = hpTe->getHeight();
        const int barH       = juce::jmax (10, rowH / 2);
        const int barGap     = juce::jmax (2, rowH / 6);
        const int gap        = juce::jmax (4, rowH / 3);
        const int rowTotal   = rowH + barGap + barH;
        const int totalH     = rowTotal * 2 + gap;
        const int startY     = juce::jmax (kPromptEditorMinTopPx, (buttonsTop - totalH) / 2);

        const int barX = margin;
        const int barR = aw->getWidth() - margin;

        constexpr int toggleVisualInsetLeft = 2;
        constexpr int tglGap = 4;
        const int toggleVisualSide = juce::jlimit (14, juce::jmax (14, toggleSide - 2), (int) std::lround ((double) toggleSide * 0.65));
        const int labelOffset = toggleVisualInsetLeft + toggleVisualSide + tglGap;

        const int nameW  = stringWidth (slopeFont, "LP") + 2;
        const int slopeW = stringWidth (slopeFont, "24dB") + 4;
        const int hzGap  = 2;
        const int hzW    = stringWidth (promptFont, "Hz") + 2;

        auto placeRow = [&] (juce::ToggleButton* toggle, juce::Label* nameLabel,
                             juce::TextEditor* te, juce::Label* hzLabel,
                             juce::Label* slopeLabel, PromptBar* bar, int y)
        {
            nameLabel->setFont (slopeFont); hzLabel->setFont (promptFont); slopeLabel->setFont (slopeFont);
            toggle->setBounds (barX, y + (rowH - toggleSide) / 2, toggleSide, toggleSide);
            const int nameX = barX + labelOffset;
            nameLabel->setBounds (nameX, y, nameW, rowH);
            const int slopeX = barR - slopeW;
            slopeLabel->setBounds (slopeX, y, slopeW, rowH);
            const int midL = nameX + nameW;
            const int midR = slopeX;
            const int midW = midR - midL;
            const auto txt   = te->getText();
            const int textW  = juce::jmax (1, stringWidth (promptFont, txt));
            constexpr int kEditorPad = 6;
            const int editorW = textW + kEditorPad * 2;
            const int groupW  = editorW + hzGap + hzW;
            const int groupX = midL + juce::jmax (0, (midW - groupW) / 2);
            te->setBounds (groupX, y, editorW, rowH);
            hzLabel->setBounds (groupX + editorW + hzGap, y, hzW, rowH);
            const int barW = juce::jmax (60, barR - barX);
            bar->setBounds (barX, y + rowH + barGap, barW, barH);
        };

        placeRow (hpToggle, hpNameLabel, hpTe, hpHzLabel, hpSlopeLabel, hpBar, startY);
        placeRow (lpToggle, lpNameLabel, lpTe, lpHzLabel, lpSlopeLabel, lpBar, startY + rowTotal + gap);
    };

    layoutRows();
    *layoutFn = layoutRows;

    preparePromptTextEditor (*aw, "hpFreq", scheme.bg, scheme.text, scheme.fg, promptFont, false);
    preparePromptTextEditor (*aw, "lpFreq", scheme.bg, scheme.text, scheme.fg, promptFont, false);
    layoutRows();

    styleAlertButtons (*aw, lnf);

    const float origHpFreq  = hpFreq;
    const float origLpFreq  = lpFreq;
    const int   origHpSlope = hpSlope;
    const int   origLpSlope = lpSlope;
    const bool  origHpOn    = hpOn;
    const bool  origLpOn    = lpOn;

    fitAlertWindowToEditor (*aw, safeThis.getComponent(), [layoutRows] (juce::AlertWindow& a)
    { layoutAlertWindowButtons (a); layoutRows(); });

    embedAlertWindowInOverlay (safeThis.getComponent(), aw);

    aw->enterModalState (true,
        juce::ModalCallbackFunction::create (
            [safeThis, aw, origHpFreq, origLpFreq, origHpSlope, origLpSlope, origHpOn, origLpOn,
             hpCyclerGuard, lpCyclerGuard, hpFwdGuard, lpFwdGuard] (int result)
        {
            std::unique_ptr<juce::AlertWindow> killer (aw);
            if (safeThis == nullptr) return;
            if (result != 1)
            {
                auto& p = safeThis->audioProcessor;
                auto setP = [&p] (const char* id, float plain)
                { if (auto* param = p.apvts.getParameter (id)) param->setValueNotifyingHost (param->convertTo0to1 (plain)); };
                setP (GRATRAudioProcessor::kParamFilterHpFreq, origHpFreq);
                setP (GRATRAudioProcessor::kParamFilterLpFreq, origLpFreq);
                setP (GRATRAudioProcessor::kParamFilterHpSlope, (float) origHpSlope);
                setP (GRATRAudioProcessor::kParamFilterLpSlope, (float) origLpSlope);
                if (auto* hpOnParam = p.apvts.getParameter (GRATRAudioProcessor::kParamFilterHpOn))
                    hpOnParam->setValueNotifyingHost (origHpOn ? 1.0f : 0.0f);
                if (auto* lpOnParam = p.apvts.getParameter (GRATRAudioProcessor::kParamFilterLpOn))
                    lpOnParam->setValueNotifyingHost (origLpOn ? 1.0f : 0.0f);
                safeThis->filterBar_.updateFromProcessor();
            }
            safeThis->setPromptOverlayActive (false);
        }),
        false);
}

// ── MIDI Channel Prompt ───────────────────────────────────────────
void GRATRAudioProcessorEditor::openMidiChannelPrompt()
{
    lnf.setScheme (activeScheme);
    const auto scheme = activeScheme;

    const juce::String suffixText = "CHANNEL";
    const bool legendFirst = true;
    const int channel = audioProcessor.getMidiChannel();
    const juce::String currentValue = juce::String (channel);

    auto* aw = new juce::AlertWindow ("", "", juce::AlertWindow::NoIcon);
    aw->setLookAndFeel (&lnf);
    aw->addTextEditor ("val", currentValue, juce::String());

    juce::Label* suffixLabel = nullptr;

    struct MidiChannelInputFilter : juce::TextEditor::InputFilter
    {
        juce::String filterNewText (juce::TextEditor& editor, const juce::String& newText) override
        {
            juce::String result;
            for (auto c : newText)
            {
                if (juce::CharacterFunctions::isDigit (c)) result += c;
                if (result.length() >= 2) break;
            }
            juce::String proposed = editor.getText();
            int insertPos = editor.getCaretPosition();
            proposed = proposed.substring (0, insertPos) + result
                     + proposed.substring (insertPos + editor.getHighlightedText().length());
            if (proposed.length() > 2 || proposed.getIntValue() > 16) return juce::String();
            if (proposed.length() > 1 && proposed[0] == '0') return juce::String();
            return result;
        }
    };

    juce::Rectangle<int> editorBaseBounds;
    std::function<void()> layoutValueAndSuffix;

    if (auto* te = aw->getTextEditor ("val"))
    {
        const auto& f = kBoldFont40();
        te->setFont (f); te->applyFontToAllText (f);
        auto r = te->getBounds();
        r.setHeight ((int) (f.getHeight() * kPromptEditorHeightScale) + kPromptEditorHeightPadPx);
        r.setY (juce::jmax (kPromptEditorMinTopPx, r.getY() - kPromptEditorRaiseYPx));
        editorBaseBounds = r;

        suffixLabel = new juce::Label ("suffix", suffixText);
        suffixLabel->setComponentID (kPromptSuffixLabelId);
        suffixLabel->setJustificationType (juce::Justification::centredLeft);
        applyLabelTextColour (*suffixLabel, scheme.text);
        suffixLabel->setBorderSize (juce::BorderSize<int> (0));
        suffixLabel->setFont (f);
        aw->addAndMakeVisible (suffixLabel);

        layoutValueAndSuffix = [aw, te, suffixLabel, editorBaseBounds, legendFirst]()
        {
            int labelW = stringWidth (suffixLabel->getFont(), suffixLabel->getText()) + 2;
            auto er = te->getBounds();
            const auto txt = te->getText();
            const int textW = juce::jmax (1, stringWidth (te->getFont(), txt));
            const int spaceW = juce::jmax (2, stringWidth (te->getFont(), " "));
            const int minGapPx = juce::jmax (1, spaceW);
            constexpr int kEditorTextPadPx = 12;
            constexpr int kMinEditorWidthPx = 24;
            const int editorW = juce::jlimit (kMinEditorWidthPx, editorBaseBounds.getWidth(), textW + (kEditorTextPadPx * 2));
            er.setWidth (editorW);
            const int combinedW = labelW + minGapPx + textW;
            const int contentPad = kPromptInlineContentPadPx;
            const int contentLeft = contentPad;
            const int contentRight = (aw != nullptr ? aw->getWidth() - contentPad : editorBaseBounds.getRight());
            const int contentCenter = (contentLeft + contentRight) / 2;
            int blockLeft = contentCenter - (combinedW / 2);
            const int minBlockLeft = contentLeft;
            const int maxBlockLeft = juce::jmax (minBlockLeft, contentRight - combinedW);
            blockLeft = juce::jlimit (minBlockLeft, maxBlockLeft, blockLeft);
            if (legendFirst)
            {
                int labelX = juce::jlimit (contentLeft, juce::jmax (contentLeft, contentRight - combinedW), blockLeft);
                const int labelY = er.getY();
                const int labelH = juce::jmax (1, er.getHeight());
                suffixLabel->setBounds (labelX, labelY, labelW, labelH);
                int teX = labelX + labelW + minGapPx - ((editorW - textW) / 2);
                teX = juce::jlimit (contentLeft, juce::jmax (contentLeft, contentRight - editorW), teX);
                er.setX (teX); te->setBounds (er);
            }
            else
            {
                int teX = blockLeft - ((editorW - textW) / 2);
                teX = juce::jlimit (contentLeft, juce::jmax (contentLeft, contentRight - editorW), teX);
                er.setX (teX); te->setBounds (er);
                const int textLeftActual = er.getX() + (er.getWidth() - textW) / 2;
                int labelX = textLeftActual + textW + minGapPx;
                labelX = juce::jlimit (contentLeft, juce::jmax (contentLeft, contentRight - labelW), labelX);
                suffixLabel->setBounds (labelX, er.getY(), labelW, juce::jmax (1, er.getHeight()));
            }
        };

        te->setBounds (editorBaseBounds);
        int labelW0 = stringWidth (suffixLabel->getFont(), suffixText) + 2;
        suffixLabel->setBounds (r.getRight() + 2, r.getY() + 1, labelW0, juce::jmax (1, r.getHeight() - 2));
        if (layoutValueAndSuffix) layoutValueAndSuffix();
        te->setInputFilter (new MidiChannelInputFilter(), true);
        te->onTextChange = [te, layoutValueAndSuffix]() mutable { if (layoutValueAndSuffix) layoutValueAndSuffix(); };
    }

    aw->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    applyPromptShellSize (*aw);
    layoutAlertWindowButtons (*aw);

    const juce::Font& kMidiPromptFont = kBoldFont40();
    preparePromptTextEditor (*aw, "val", scheme.bg, scheme.text, scheme.fg, kMidiPromptFont, false);

    if (suffixLabel != nullptr && ! editorBaseBounds.isEmpty())
    {
        if (auto* te = aw->getTextEditor ("val")) suffixLabel->setFont (te->getFont());
        if (layoutValueAndSuffix) layoutValueAndSuffix();
    }

    styleAlertButtons (*aw, lnf);

    juce::Component::SafePointer<GRATRAudioProcessorEditor> safeThis (this);
    setPromptOverlayActive (true);

    if (safeThis != nullptr)
    {
        fitAlertWindowToEditor (*aw, safeThis.getComponent(), [layoutValueAndSuffix] (juce::AlertWindow& a)
        { layoutAlertWindowButtons (a); if (layoutValueAndSuffix) layoutValueAndSuffix(); });
        embedAlertWindowInOverlay (safeThis.getComponent(), aw);
    }
    else
    {
        aw->centreAroundComponent (this, aw->getWidth(), aw->getHeight());
        bringPromptWindowToFront (*aw);
    }

    {
        preparePromptTextEditor (*aw, "val", scheme.bg, scheme.text, scheme.fg, kMidiPromptFont, false);
        if (auto* suffixLbl = dynamic_cast<juce::Label*> (aw->findChildWithID (kPromptSuffixLabelId)))
        { if (auto* te = aw->getTextEditor ("val")) suffixLbl->setFont (te->getFont()); }
        if (layoutValueAndSuffix) layoutValueAndSuffix();
        juce::Component::SafePointer<juce::AlertWindow> safeAw (aw);
        juce::MessageManager::callAsync ([safeAw]() { if (safeAw != nullptr) { bringPromptWindowToFront (*safeAw); safeAw->repaint(); } });
    }

    aw->enterModalState (true,
        juce::ModalCallbackFunction::create ([safeThis, aw] (int result) mutable
        {
            std::unique_ptr<juce::AlertWindow> killer (aw);
            if (safeThis != nullptr) safeThis->setPromptOverlayActive (false);
            if (safeThis == nullptr || result != 1) return;
            const auto txt = aw->getTextEditorContents ("val").trim();
            const int ch = juce::jlimit (0, 16, txt.isEmpty() ? 0 : txt.getIntValue());
            safeThis->audioProcessor.setMidiChannel (ch);
            safeThis->midiChannelDisplay.setTooltip (formatMidiChannelTooltip (ch));
        }),
        false);
}

// ── ENV GRA Prompt (TAU + AMT bars) ───────────────────────────────
void GRATRAudioProcessorEditor::openEnvGraPrompt()
{
    lnf.setScheme (activeScheme);
    const auto scheme = activeScheme;

    const float currentTau = audioProcessor.apvts.getRawParameterValue (GRATRAudioProcessor::kParamEnvGraTau)->load();
    const float currentAmt = audioProcessor.apvts.getRawParameterValue (GRATRAudioProcessor::kParamEnvGraAmt)->load();

    auto* aw = new juce::AlertWindow ("", "", juce::AlertWindow::NoIcon);
    aw->setLookAndFeel (&lnf);

    aw->addTextEditor ("tau", juce::String (juce::roundToInt (currentTau)), juce::String());
    aw->addTextEditor ("amt", juce::String (juce::roundToInt (currentAmt)), juce::String());

    struct PromptBar : public juce::Component
    {
        GRAScheme colours;
        float value      = 0.5f;
        float defaultVal = 0.5f;
        std::function<void (float)> onValueChanged;
        PromptBar (const GRAScheme& s, float initial01, float default01) : colours (s), value (initial01), defaultVal (default01) {}
        void paint (juce::Graphics& g) override
        {
            const auto r = getLocalBounds().toFloat();
            g.setColour (colours.outline); g.drawRect (r, 4.0f);
            const float pad = 7.0f; auto inner = r.reduced (pad);
            g.setColour (colours.bg); g.fillRect (inner);
            const float fillW = juce::jlimit (0.0f, inner.getWidth(), inner.getWidth() * value);
            g.setColour (colours.fg); g.fillRect (inner.withWidth (fillW));
        }
        void mouseDown (const juce::MouseEvent& e) override { updateFromMouse (e); }
        void mouseDrag (const juce::MouseEvent& e) override { updateFromMouse (e); }
        void mouseDoubleClick (const juce::MouseEvent&) override { setValue (defaultVal); }
        void setValue (float v01) { value = juce::jlimit (0.0f, 1.0f, v01); repaint(); if (onValueChanged) onValueChanged (value); }
    private:
        void updateFromMouse (const juce::MouseEvent& e)
        { const float pad = 7.0f; const float innerW = (float) getWidth() - pad * 2.0f; setValue (innerW > 0.0f ? ((float) e.x - pad) / innerW : 0.0f); }
    };

    struct ResetLabel : public juce::Label
    { PromptBar* pairedBar = nullptr;
      void mouseDoubleClick (const juce::MouseEvent&) override { if (pairedBar != nullptr) pairedBar->setValue (pairedBar->defaultVal); } };

    const auto& f = kBoldFont40();
    ResetLabel* tauSuffix = nullptr;
    ResetLabel* amtSuffix = nullptr;
    juce::Label* tauPctLabel = nullptr;
    juce::Label* amtPctLabel = nullptr;

    auto setupField = [&] (const char* editorId, const juce::String& suffixText,
                           ResetLabel*& suffixOut, juce::Label*& pctOut)
    {
        if (auto* te = aw->getTextEditor (editorId))
        {
            te->setFont (f); te->applyFontToAllText (f);
            te->setInputFilter (new PctInputFilter(), true);
            auto r = te->getBounds();
            r.setHeight ((int) (f.getHeight() * kPromptEditorHeightScale) + kPromptEditorHeightPadPx);
            te->setBounds (r);

            suffixOut = new ResetLabel();
            suffixOut->setText (suffixText, juce::dontSendNotification);
            suffixOut->setJustificationType (juce::Justification::centredLeft);
            applyLabelTextColour (*suffixOut, scheme.text);
            suffixOut->setBorderSize (juce::BorderSize<int> (0));
            suffixOut->setFont (f);
            aw->addAndMakeVisible (suffixOut);

            pctOut = new juce::Label ("", "%");
            pctOut->setJustificationType (juce::Justification::centredLeft);
            applyLabelTextColour (*pctOut, scheme.text);
            pctOut->setBorderSize (juce::BorderSize<int> (0));
            pctOut->setFont (f);
            aw->addAndMakeVisible (pctOut);
        }
    };

    setupField ("tau", "TAU", tauSuffix, tauPctLabel);
    setupField ("amt", "AMT", amtSuffix, amtPctLabel);

    auto* tauBar = new PromptBar (scheme, currentTau * 0.01f, GRATRAudioProcessor::kEnvGraTauDefault * 0.01f);
    auto* amtBar = new PromptBar (scheme, currentAmt * 0.01f, GRATRAudioProcessor::kEnvGraAmtDefault * 0.01f);
    aw->addAndMakeVisible (tauBar);
    aw->addAndMakeVisible (amtBar);

    if (tauSuffix != nullptr) tauSuffix->pairedBar = tauBar;
    if (amtSuffix != nullptr) amtSuffix->pairedBar = amtBar;

    auto syncing = std::make_shared<bool> (false);

    auto* tauApvts = audioProcessor.apvts.getParameter (GRATRAudioProcessor::kParamEnvGraTau);
    auto* amtApvts = audioProcessor.apvts.getParameter (GRATRAudioProcessor::kParamEnvGraAmt);

    auto barToText = [aw, syncing] (const char* editorId, float v01)
    {
        if (*syncing) return;
        *syncing = true;
        if (auto* te = aw->getTextEditor (editorId))
        { te->setText (juce::String (juce::roundToInt (v01 * 100.0f)), juce::sendNotification); te->selectAll(); }
        *syncing = false;
    };

    tauBar->onValueChanged = [barToText, tauApvts] (float v)
    { barToText ("tau", v); if (tauApvts != nullptr) tauApvts->setValueNotifyingHost (tauApvts->convertTo0to1 (v * 100.0f)); };
    amtBar->onValueChanged = [barToText, amtApvts] (float v)
    { barToText ("amt", v); if (amtApvts != nullptr) amtApvts->setValueNotifyingHost (amtApvts->convertTo0to1 (v * 100.0f)); };

    auto layoutRows = [aw, tauSuffix, amtSuffix, tauPctLabel, amtPctLabel, tauBar, amtBar] ()
    {
        auto* tauTe = aw->getTextEditor ("tau");
        auto* amtTe = aw->getTextEditor ("amt");
        if (tauTe == nullptr || amtTe == nullptr) return;

        const int buttonsTop = getAlertButtonsTop (*aw);
        const int rowH = tauTe->getHeight();
        const int barH = juce::jmax (10, rowH / 2);
        const int barGap = juce::jmax (2, rowH / 6);
        const int rowTotal = rowH + barGap + barH;
        const int gap = juce::jmax (4, rowH / 3);
        const int totalH = rowTotal * 2 + gap;
        const int startY = juce::jmax (kPromptEditorMinTopPx, (buttonsTop - totalH) / 2);

        const int contentPad = kPromptInlineContentPadPx;
        const int contentW = aw->getWidth() - contentPad * 2;
        const auto& font = tauTe->getFont();
        const int spaceW = juce::jmax (2, stringWidth (font, " "));
        const int pctW = stringWidth (font, "%") + 2;

        auto placeRow = [&] (juce::TextEditor* te, juce::Label* suffix, juce::Label* pctLabel, PromptBar* bar, int y)
        {
            if (te == nullptr || suffix == nullptr || bar == nullptr) return;
            const int labelW = stringWidth (suffix->getFont(), suffix->getText()) + 2;
            const auto txt = te->getText();
            const int textW = juce::jmax (1, stringWidth (font, txt));
            constexpr int kEditorTextPadPx = 12;
            constexpr int kMinEditorWidthPx = 24;
            const int editorW = juce::jlimit (kMinEditorWidthPx, 80, textW + kEditorTextPadPx * 2);
            const int visualW = labelW + spaceW + textW + pctW;
            const int centerX = contentPad + contentW / 2;
            int blockLeft = juce::jlimit (contentPad, juce::jmax (contentPad, contentPad + contentW - visualW), centerX - visualW / 2);
            suffix->setBounds (blockLeft, y, labelW, rowH);
            int teX = blockLeft + labelW + spaceW - (editorW - textW) / 2;
            teX = juce::jlimit (contentPad, juce::jmax (contentPad, contentPad + contentW - editorW), teX);
            te->setBounds (teX, y, editorW, rowH);
            if (pctLabel != nullptr)
            { const int textRightX = blockLeft + labelW + spaceW + textW; pctLabel->setBounds (textRightX, y, pctW, rowH); }
            const int barX = kPromptInnerMargin;
            const int barW = juce::jmax (60, aw->getWidth() - kPromptInnerMargin * 2);
            bar->setBounds (barX, y + rowH + barGap, barW, barH);
        };

        placeRow (tauTe, tauSuffix, tauPctLabel, tauBar, startY);
        placeRow (amtTe, amtSuffix, amtPctLabel, amtBar, startY + rowTotal + gap);
    };

    auto textToBar = [syncing] (juce::TextEditor* te, PromptBar* bar, juce::RangedAudioParameter* apvtsParam)
    {
        if (*syncing || te == nullptr || bar == nullptr) return;
        *syncing = true;
        const float v = juce::jlimit (0.0f, 100.0f, (float) te->getText().getIntValue());
        bar->value = v * 0.01f; bar->repaint();
        if (apvtsParam != nullptr) apvtsParam->setValueNotifyingHost (apvtsParam->convertTo0to1 (v));
        *syncing = false;
    };

    if (auto* tauTe = aw->getTextEditor ("tau"))
        tauTe->onTextChange = [layoutRows, tauTe, tauBar, textToBar, tauApvts] () mutable
        { textToBar (tauTe, tauBar, tauApvts); layoutRows(); };
    if (auto* amtTe = aw->getTextEditor ("amt"))
        amtTe->onTextChange = [layoutRows, amtTe, amtBar, textToBar, amtApvts] () mutable
        { textToBar (amtTe, amtBar, amtApvts); layoutRows(); };

    aw->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    applyPromptShellSize (*aw);
    layoutAlertWindowButtons (*aw);
    layoutRows();

    const juce::Font& kEnvGraFont = kBoldFont40();
    preparePromptTextEditor (*aw, "tau", scheme.bg, scheme.text, scheme.fg, kEnvGraFont, false);
    preparePromptTextEditor (*aw, "amt", scheme.bg, scheme.text, scheme.fg, kEnvGraFont, false);
    layoutRows();

    styleAlertButtons (*aw, lnf);

    juce::Component::SafePointer<GRATRAudioProcessorEditor> safeThis (this);

    if (safeThis != nullptr)
    {
        fitAlertWindowToEditor (*aw, safeThis.getComponent(), [layoutRows] (juce::AlertWindow& a)
        { juce::ignoreUnused (a); layoutAlertWindowButtons (a); layoutRows(); });
        embedAlertWindowInOverlay (safeThis.getComponent(), aw);
    }
    else
    {
        aw->centreAroundComponent (this, aw->getWidth(), aw->getHeight());
        bringPromptWindowToFront (*aw);
    }

    {
        preparePromptTextEditor (*aw, "tau", scheme.bg, scheme.text, scheme.fg, kEnvGraFont, false);
        preparePromptTextEditor (*aw, "amt", scheme.bg, scheme.text, scheme.fg, kEnvGraFont, false);
        layoutRows();

        if (tauSuffix != nullptr)
        { if (auto* te = aw->getTextEditor ("tau")) { tauSuffix->setFont (te->getFont()); if (tauPctLabel != nullptr) tauPctLabel->setFont (te->getFont()); } }
        if (amtSuffix != nullptr)
        { if (auto* te = aw->getTextEditor ("amt")) { amtSuffix->setFont (te->getFont()); if (amtPctLabel != nullptr) amtPctLabel->setFont (te->getFont()); } }
        layoutRows();

        juce::Component::SafePointer<juce::AlertWindow> safeAw (aw);
        juce::MessageManager::callAsync ([safeAw]() { if (safeAw == nullptr) return; bringPromptWindowToFront (*safeAw); safeAw->repaint(); });
    }

    aw->enterModalState (true,
        juce::ModalCallbackFunction::create (
            [safeThis, aw, tauBar, amtBar,
             savedTau = currentTau, savedAmt = currentAmt] (int result) mutable
        {
            std::unique_ptr<juce::AlertWindow> killer (aw);
            if (safeThis != nullptr) safeThis->setPromptOverlayActive (false);
            if (safeThis == nullptr) return;

            if (result != 1)
            {
                if (auto* p = safeThis->audioProcessor.apvts.getParameter (GRATRAudioProcessor::kParamEnvGraTau))
                    p->setValueNotifyingHost (p->convertTo0to1 (savedTau));
                if (auto* p = safeThis->audioProcessor.apvts.getParameter (GRATRAudioProcessor::kParamEnvGraAmt))
                    p->setValueNotifyingHost (p->convertTo0to1 (savedAmt));
                return;
            }

            const float newTau = juce::jlimit (0.0f, 100.0f, tauBar->value * 100.0f);
            const float newAmt = juce::jlimit (0.0f, 100.0f, amtBar->value * 100.0f);
            safeThis->envGraDisplay.setTooltip (formatEnvGraTooltip (newTau, newAmt));
        }),
        false);
}

//==============================================================================
//  CHAOS prompt (AMOUNT + SPEED)
//==============================================================================
void GRATRAudioProcessorEditor::openChaosConfigPrompt (const char* amtParamId, const char* spdParamId, const juce::String& title)
{
    juce::ignoreUnused (title);
    using namespace TR;
    lnf.setScheme (activeScheme);
    const auto scheme = activeScheme;

    const float currentAmt = audioProcessor.apvts.getRawParameterValue (amtParamId)->load();
    const float currentSpd = audioProcessor.apvts.getRawParameterValue (spdParamId)->load();

    auto* aw = new juce::AlertWindow ("", "", juce::AlertWindow::NoIcon);
    aw->setLookAndFeel (&lnf);
    aw->addTextEditor ("amt", juce::String (juce::roundToInt (currentAmt)), juce::String());
    aw->addTextEditor ("spd", juce::String (currentSpd, 2), juce::String());

    struct PromptBar : public juce::Component
    {
        GRAScheme colours; float value = 0.5f; float defaultVal = 0.5f;
        std::function<void (float)> onValueChanged;
        PromptBar (const GRAScheme& s, float initial01, float default01) : colours (s), value (initial01), defaultVal (default01) {}
        void paint (juce::Graphics& g) override
        { const auto r = getLocalBounds().toFloat(); g.setColour (colours.outline); g.drawRect (r, 4.0f);
          const float pad = 7.0f; auto inner = r.reduced (pad); g.setColour (colours.bg); g.fillRect (inner);
          const float fillW = juce::jlimit (0.0f, inner.getWidth(), inner.getWidth() * value);
          g.setColour (colours.fg); g.fillRect (inner.withWidth (fillW)); }
        void mouseDown (const juce::MouseEvent& e) override { updateFromMouse (e); }
        void mouseDrag (const juce::MouseEvent& e) override { updateFromMouse (e); }
        void mouseDoubleClick (const juce::MouseEvent&) override { setValue (defaultVal); }
        void setValue (float v01) { value = juce::jlimit (0.0f, 1.0f, v01); repaint(); if (onValueChanged) onValueChanged (value); }
    private:
        void updateFromMouse (const juce::MouseEvent& e)
        { const float pad = 7.0f; const float innerW = (float) getWidth() - pad * 2.0f; setValue (innerW > 0.0f ? ((float) e.x - pad) / innerW : 0.0f); }
    };

    struct ResetLabel : public juce::Label
    { PromptBar* pairedBar = nullptr;
      void mouseDoubleClick (const juce::MouseEvent&) override { if (pairedBar != nullptr) pairedBar->setValue (pairedBar->defaultVal); } };

    const auto& f = kBoldFont40();
    ResetLabel* amtSuffix = nullptr; ResetLabel* spdSuffix = nullptr;
    juce::Label* amtUnitLabel = nullptr; juce::Label* spdUnitLabel = nullptr;

    auto setupField = [&] (const char* editorId, const juce::String& suffixText,
                           const juce::String& unitText, bool useDecimalFilter,
                           ResetLabel*& suffixOut, juce::Label*& unitOut)
    {
        if (auto* te = aw->getTextEditor (editorId))
        {
            te->setFont (f); te->applyFontToAllText (f);
            if (useDecimalFilter) te->setInputRestrictions (6, "0123456789.");
            else                  te->setInputFilter (new PctInputFilter(), true);
            auto r = te->getBounds();
            r.setHeight ((int) (f.getHeight() * kPromptEditorHeightScale) + kPromptEditorHeightPadPx);
            te->setBounds (r);

            suffixOut = new ResetLabel();
            suffixOut->setText (suffixText, juce::dontSendNotification);
            suffixOut->setJustificationType (juce::Justification::centredLeft);
            applyLabelTextColour (*suffixOut, scheme.text);
            suffixOut->setBorderSize (juce::BorderSize<int> (0));
            suffixOut->setFont (f);
            aw->addAndMakeVisible (suffixOut);

            unitOut = new juce::Label ("", unitText);
            unitOut->setJustificationType (juce::Justification::centredLeft);
            applyLabelTextColour (*unitOut, scheme.text);
            unitOut->setBorderSize (juce::BorderSize<int> (0));
            unitOut->setFont (f);
            aw->addAndMakeVisible (unitOut);
        }
    };

    setupField ("amt", "AMT", "%",  false, amtSuffix, amtUnitLabel);
    setupField ("spd", "SPD", "Hz", true,  spdSuffix, spdUnitLabel);

    const float spdLogMin   = std::log (GRATRAudioProcessor::kChaosSpdMin);
    const float spdLogMax   = std::log (GRATRAudioProcessor::kChaosSpdMax);
    const float spdLogRange = spdLogMax - spdLogMin;

    auto hzToBar = [spdLogMin, spdLogRange] (float hz) -> float
    { if (hz <= GRATRAudioProcessor::kChaosSpdMin) return 0.0f;
      if (hz >= GRATRAudioProcessor::kChaosSpdMax) return 1.0f;
      return (std::log (hz) - spdLogMin) / spdLogRange; };
    auto barToHz = [spdLogMin, spdLogRange] (float v01) -> float
    { return std::exp (spdLogMin + v01 * spdLogRange); };

    auto* amtBar = new PromptBar (scheme, currentAmt * 0.01f, GRATRAudioProcessor::kChaosAmtDefault * 0.01f);
    auto* spdBar = new PromptBar (scheme, hzToBar (currentSpd), hzToBar (GRATRAudioProcessor::kChaosSpdDefault));
    aw->addAndMakeVisible (amtBar);
    aw->addAndMakeVisible (spdBar);

    if (amtSuffix != nullptr) amtSuffix->pairedBar = amtBar;
    if (spdSuffix != nullptr) spdSuffix->pairedBar = spdBar;

    auto syncing = std::make_shared<bool> (false);
    auto* amtApvts = audioProcessor.apvts.getParameter (amtParamId);
    auto* spdApvts = audioProcessor.apvts.getParameter (spdParamId);

    auto barToTextAmt = [aw, syncing, amtApvts] (float v01)
    { if (*syncing) return; *syncing = true;
      if (auto* te = aw->getTextEditor ("amt")) { te->setText (juce::String (juce::roundToInt (v01 * 100.0f)), juce::sendNotification); te->selectAll(); }
      if (amtApvts != nullptr) amtApvts->setValueNotifyingHost (amtApvts->convertTo0to1 (v01 * 100.0f));
      *syncing = false; };

    auto barToTextSpd = [aw, syncing, spdApvts, barToHz] (float v01)
    { if (*syncing) return; *syncing = true;
      const float hz = juce::jlimit (GRATRAudioProcessor::kChaosSpdMin, GRATRAudioProcessor::kChaosSpdMax, barToHz (v01));
      if (auto* te = aw->getTextEditor ("spd")) { te->setText (juce::String (hz, 2), juce::sendNotification); te->selectAll(); }
      if (spdApvts != nullptr) spdApvts->setValueNotifyingHost (spdApvts->convertTo0to1 (hz));
      *syncing = false; };

    amtBar->onValueChanged = barToTextAmt;
    spdBar->onValueChanged = barToTextSpd;

    auto layoutRows = [aw, amtSuffix, spdSuffix, amtUnitLabel, spdUnitLabel, amtBar, spdBar] ()
    {
        auto* amtTe = aw->getTextEditor ("amt");
        auto* spdTe = aw->getTextEditor ("spd");
        if (amtTe == nullptr || spdTe == nullptr) return;
        const int buttonsTop = getAlertButtonsTop (*aw);
        const int rowH = amtTe->getHeight();
        const int barH = juce::jmax (10, rowH / 2);
        const int barGap = juce::jmax (2, rowH / 6);
        const int rowTotal = rowH + barGap + barH;
        const int gap = juce::jmax (4, rowH / 3);
        const int totalH = rowTotal * 2 + gap;
        const int startY = juce::jmax (kPromptEditorMinTopPx, (buttonsTop - totalH) / 2);
        const int contentPad = kPromptInlineContentPadPx;
        const int contentW = aw->getWidth() - contentPad * 2;
        const auto& font = amtTe->getFont();
        const int spaceW = juce::jmax (2, stringWidth (font, " "));

        auto placeRow = [&] (juce::TextEditor* te, juce::Label* suffix, juce::Label* unitLabel, PromptBar* bar, int y)
        {
            if (te == nullptr || suffix == nullptr || bar == nullptr) return;
            const int labelW = stringWidth (suffix->getFont(), suffix->getText()) + 2;
            const auto txt = te->getText();
            const int textW = juce::jmax (1, stringWidth (font, txt));
            const int unitW = (unitLabel != nullptr) ? stringWidth (font, unitLabel->getText()) + 2 : 0;
            constexpr int kEditorTextPadPx = 12; constexpr int kMinEditorWidthPx = 24;
            const int editorW = juce::jlimit (kMinEditorWidthPx, 80, textW + kEditorTextPadPx * 2);
            const int visualW = labelW + spaceW + textW + unitW;
            const int centerX = contentPad + contentW / 2;
            int blockLeft = juce::jlimit (contentPad, juce::jmax (contentPad, contentPad + contentW - visualW), centerX - visualW / 2);
            suffix->setBounds (blockLeft, y, labelW, rowH);
            int teX = blockLeft + labelW + spaceW - (editorW - textW) / 2;
            teX = juce::jlimit (contentPad, juce::jmax (contentPad, contentPad + contentW - editorW), teX);
            te->setBounds (teX, y, editorW, rowH);
            if (unitLabel != nullptr) { const int textRightX = blockLeft + labelW + spaceW + textW; unitLabel->setBounds (textRightX, y, unitW, rowH); }
            const int barX = kPromptInnerMargin;
            const int barW = juce::jmax (60, aw->getWidth() - kPromptInnerMargin * 2);
            bar->setBounds (barX, y + rowH + barGap, barW, barH);
        };
        placeRow (amtTe, amtSuffix, amtUnitLabel, amtBar, startY);
        placeRow (spdTe, spdSuffix, spdUnitLabel, spdBar, startY + rowTotal + gap);
    };

    auto textToBar = [syncing, hzToBar] (juce::TextEditor* te, PromptBar* bar, juce::RangedAudioParameter* param, bool isSpeed)
    {
        if (*syncing || te == nullptr || bar == nullptr) return;
        *syncing = true;
        const float raw = juce::jlimit (0.0f, 100.0f, te->getText().getFloatValue());
        if (isSpeed)
        { const float hz = juce::jlimit (GRATRAudioProcessor::kChaosSpdMin, GRATRAudioProcessor::kChaosSpdMax, raw);
          bar->value = hzToBar (hz);
          if (param != nullptr) param->setValueNotifyingHost (param->convertTo0to1 (hz)); }
        else
        { bar->value = raw * 0.01f;
          if (param != nullptr) param->setValueNotifyingHost (param->convertTo0to1 (raw)); }
        bar->repaint(); *syncing = false;
    };

    if (auto* amtTe = aw->getTextEditor ("amt"))
        amtTe->onTextChange = [layoutRows, amtTe, amtBar, textToBar, amtApvts] () mutable { textToBar (amtTe, amtBar, amtApvts, false); layoutRows(); };
    if (auto* spdTe = aw->getTextEditor ("spd"))
        spdTe->onTextChange = [layoutRows, spdTe, spdBar, textToBar, spdApvts] () mutable { textToBar (spdTe, spdBar, spdApvts, true); layoutRows(); };

    aw->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    aw->setEscapeKeyCancels (true);
    applyPromptShellSize (*aw);
    layoutAlertWindowButtons (*aw);
    layoutRows();

    const auto& kChaosFont = kBoldFont40();
    preparePromptTextEditor (*aw, "amt", scheme.bg, scheme.text, scheme.fg, kChaosFont, false);
    preparePromptTextEditor (*aw, "spd", scheme.bg, scheme.text, scheme.fg, kChaosFont, false);
    layoutRows();
    styleAlertButtons (*aw, lnf);

    juce::Component::SafePointer<GRATRAudioProcessorEditor> safeThis (this);

    if (safeThis != nullptr)
    {
        fitAlertWindowToEditor (*aw, safeThis.getComponent(), [layoutRows] (juce::AlertWindow& a)
        { juce::ignoreUnused (a); layoutAlertWindowButtons (a); layoutRows(); });
        embedAlertWindowInOverlay (safeThis.getComponent(), aw);
    }
    else
    {
        aw->centreAroundComponent (this, aw->getWidth(), aw->getHeight());
        bringPromptWindowToFront (*aw);
    }

    {
        preparePromptTextEditor (*aw, "amt", scheme.bg, scheme.text, scheme.fg, kChaosFont, false);
        preparePromptTextEditor (*aw, "spd", scheme.bg, scheme.text, scheme.fg, kChaosFont, false);
        layoutRows();
        if (amtSuffix != nullptr) { if (auto* te = aw->getTextEditor ("amt")) { amtSuffix->setFont (te->getFont()); if (amtUnitLabel != nullptr) amtUnitLabel->setFont (te->getFont()); } }
        if (spdSuffix != nullptr) { if (auto* te = aw->getTextEditor ("spd")) { spdSuffix->setFont (te->getFont()); if (spdUnitLabel != nullptr) spdUnitLabel->setFont (te->getFont()); } }
        layoutRows();
        juce::Component::SafePointer<juce::AlertWindow> safeAw (aw);
        juce::MessageManager::callAsync ([safeAw]() { if (safeAw == nullptr) return; bringPromptWindowToFront (*safeAw); safeAw->repaint(); });
    }

    aw->enterModalState (true,
        juce::ModalCallbackFunction::create (
            [safeThis, aw, amtBar, spdBar, savedAmt = currentAmt, savedSpd = currentSpd,
             spdLogMin, spdLogRange, amtParamId, spdParamId] (int result) mutable
        {
            std::unique_ptr<juce::AlertWindow> killer (aw);
            if (safeThis != nullptr) safeThis->setPromptOverlayActive (false);
            if (safeThis == nullptr) return;
            if (result != 1)
            {
                if (auto* p = safeThis->audioProcessor.apvts.getParameter (amtParamId))
                    p->setValueNotifyingHost (p->convertTo0to1 (savedAmt));
                if (auto* p = safeThis->audioProcessor.apvts.getParameter (spdParamId))
                    p->setValueNotifyingHost (p->convertTo0to1 (savedSpd));
                return;
            }
            const float newAmt = juce::jlimit (0.0f, 100.0f, amtBar->value * 100.0f);
            const float newSpd = juce::jlimit (GRATRAudioProcessor::kChaosSpdMin, GRATRAudioProcessor::kChaosSpdMax,
                                                std::exp (spdLogMin + juce::jlimit (0.0f, 1.0f, spdBar->value) * spdLogRange));
            auto tip = juce::String (juce::roundToInt (newAmt)) + "% | " + juce::String (juce::roundToInt (newSpd)) + " Hz";
            safeThis->chaosFilterDisplay.setTooltip (tip);
            safeThis->chaosDelayDisplay.setTooltip (tip);
        }), false);
}

void GRATRAudioProcessorEditor::openChaosFilterPrompt()
{
    openChaosConfigPrompt (GRATRAudioProcessor::kParamChaosAmtFilter,
                           GRATRAudioProcessor::kParamChaosSpdFilter, "CHS F");
}

void GRATRAudioProcessorEditor::openChaosDelayPrompt()
{
    openChaosConfigPrompt (GRATRAudioProcessor::kParamChaosAmt,
                           GRATRAudioProcessor::kParamChaosSpd, "CHS D");
}

//==============================================================================
//  Info Popup
//==============================================================================
void GRATRAudioProcessorEditor::openInfoPopup()
{
    lnf.setScheme (activeScheme);
    setPromptOverlayActive (true);

    auto* aw = new juce::AlertWindow ("", "", juce::AlertWindow::NoIcon);
    juce::Component::SafePointer<juce::AlertWindow> safeAw (aw);
    juce::Component::SafePointer<GRATRAudioProcessorEditor> safeThis (this);
    aw->setLookAndFeel (&lnf);
    aw->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("GRAPHICS", 2);

    applyPromptShellSize (*aw);

    auto* bodyContent = new juce::Component();
    bodyContent->setComponentID ("bodyContent");

    auto infoFont = lnf.getAlertWindowMessageFont();
    infoFont.setHeight (infoFont.getHeight() * 1.45f);
    auto headingFont = infoFont;
    headingFont.setBold (true);
    headingFont.setHeight (infoFont.getHeight() * 1.25f);
    auto linkFont = infoFont;
    linkFont.setHeight (infoFont.getHeight() * 1.08f);
    auto poemFont = infoFont;
    poemFont.setItalic (true);

    auto xmlDoc = juce::XmlDocument::parse (InfoContent::xml);
    auto* contentNode = xmlDoc != nullptr ? xmlDoc->getChildByName ("content") : nullptr;

    if (contentNode != nullptr)
    {
        int elemIdx = 0;
        for (auto* node : contentNode->getChildIterator())
        {
            const auto tag  = node->getTagName();
            const auto text = node->getAllSubText().trim();
            const auto id   = tag + juce::String (elemIdx++);

            if (tag == "heading")
            {
                auto* l = new juce::Label (id, text);
                l->setComponentID (id); l->setJustificationType (juce::Justification::centred);
                applyLabelTextColour (*l, activeScheme.text); l->setFont (headingFont);
                bodyContent->addAndMakeVisible (l);
            }
            else if (tag == "text" || tag == "separator")
            {
                auto* l = new juce::Label (id, text);
                l->setComponentID (id); l->setJustificationType (juce::Justification::centred);
                applyLabelTextColour (*l, activeScheme.text); l->setFont (infoFont);
                l->setBorderSize (juce::BorderSize<int> (0));
                bodyContent->addAndMakeVisible (l);
            }
            else if (tag == "link")
            {
                const auto url = node->getStringAttribute ("url");
                auto* lnk = new juce::HyperlinkButton (text, juce::URL (url));
                lnk->setComponentID (id); lnk->setJustificationType (juce::Justification::centred);
                lnk->setColour (juce::HyperlinkButton::textColourId, activeScheme.text);
                lnk->setFont (linkFont, false, juce::Justification::centred);
                lnk->setTooltip ("");
                bodyContent->addAndMakeVisible (lnk);
            }
            else if (tag == "poem")
            {
                auto* l = new juce::Label (id, text);
                l->setComponentID (id); l->setJustificationType (juce::Justification::centred);
                applyLabelTextColour (*l, activeScheme.text); l->setFont (poemFont);
                l->setBorderSize (juce::BorderSize<int> (0, 0, 0, 0));
                l->getProperties().set ("poemPadFraction", 0.12f);
                bodyContent->addAndMakeVisible (l);
            }
            else if (tag == "spacer")
            {
                auto* l = new juce::Label (id, "");
                l->setComponentID (id); l->setFont (infoFont);
                l->setBorderSize (juce::BorderSize<int> (0));
                bodyContent->addAndMakeVisible (l);
            }
        }
    }

    auto* viewport = new juce::Viewport();
    viewport->setComponentID ("bodyViewport");
    viewport->setViewedComponent (bodyContent, true);
    viewport->setScrollBarsShown (true, false);
    viewport->setScrollBarThickness (8);
    viewport->setLookAndFeel (&lnf);
    aw->addAndMakeVisible (viewport);

    layoutInfoPopupContent (*aw);

    if (safeThis != nullptr)
    {
        fitAlertWindowToEditor (*aw, safeThis.getComponent(), [] (juce::AlertWindow& a) { layoutInfoPopupContent (a); });
        embedAlertWindowInOverlay (safeThis.getComponent(), aw);
    }
    else
    {
        aw->centreAroundComponent (this, aw->getWidth(), aw->getHeight());
        bringPromptWindowToFront (*aw); aw->repaint();
    }

    juce::MessageManager::callAsync ([safeAw, safeThis]()
    { if (safeAw == nullptr || safeThis == nullptr) return; bringPromptWindowToFront (*safeAw); safeAw->repaint(); });

    aw->enterModalState (true,
        juce::ModalCallbackFunction::create ([safeThis = juce::Component::SafePointer<GRATRAudioProcessorEditor> (this), aw] (int result) mutable
        {
            std::unique_ptr<juce::AlertWindow> killer (aw);
            if (safeThis == nullptr) return;
            if (result == 2) { safeThis->openGraphicsPopup(); return; }
            safeThis->setPromptOverlayActive (false);
        }));
}

//==============================================================================
//  Graphics Popup
//==============================================================================
void GRATRAudioProcessorEditor::openGraphicsPopup()
{
    lnf.setScheme (activeScheme);
    useCustomPalette = audioProcessor.getUiUseCustomPalette();
    crtEnabled = audioProcessor.getUiCrtEnabled();
    crtEffect.setEnabled (crtEnabled);
    applyActivePalette();

    setPromptOverlayActive (true);

    auto* aw = new juce::AlertWindow ("", "", juce::AlertWindow::NoIcon);
    juce::Component::SafePointer<GRATRAudioProcessorEditor> safeThis (this);
    juce::Component::SafePointer<juce::AlertWindow> safeAw (aw);
    aw->setLookAndFeel (&lnf);
    aw->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));

    auto labelFont = lnf.getAlertWindowMessageFont();
    labelFont.setHeight (labelFont.getHeight() * 1.20f);

    auto addPopupLabel = [this, aw] (const juce::String& id, const juce::String& text, juce::Font font,
                                     juce::Justification justification = juce::Justification::centredLeft)
    {
        auto* label = new PopupClickableLabel (id, text);
        label->setComponentID (id); label->setJustificationType (justification);
        applyLabelTextColour (*label, activeScheme.text);
        label->setBorderSize (juce::BorderSize<int> (0));
        label->setFont (font); label->setMouseCursor (juce::MouseCursor::PointingHandCursor);
        aw->addAndMakeVisible (label);
        return label;
    };

    auto* defaultToggle = new juce::ToggleButton ("");
    defaultToggle->setComponentID ("paletteDefaultToggle");
    aw->addAndMakeVisible (defaultToggle);
    auto* defaultLabel = addPopupLabel ("paletteDefaultLabel", "DFLT", labelFont);

    auto* customToggle = new juce::ToggleButton ("");
    customToggle->setComponentID ("paletteCustomToggle");
    aw->addAndMakeVisible (customToggle);
    auto* customLabel = addPopupLabel ("paletteCustomLabel", "CSTM", labelFont);

    auto paletteTitleFont = labelFont;
    paletteTitleFont.setHeight (paletteTitleFont.getHeight() * 1.30f);
    addPopupLabel ("paletteTitle", "PALETTE", paletteTitleFont, juce::Justification::centredLeft);

    for (int i = 0; i < 2; ++i)
    {
        auto* dflt = new juce::TextButton();
        dflt->setComponentID ("defaultSwatch" + juce::String (i));
        dflt->setTooltip ("Default palette colour " + juce::String (i + 1));
        aw->addAndMakeVisible (dflt);

        auto* custom = new PopupSwatchButton();
        custom->setComponentID ("customSwatch" + juce::String (i));
        custom->setTooltip (colourToHexRgb (customPalette[(size_t) i]));
        aw->addAndMakeVisible (custom);
    }

    auto* fxToggle = new juce::ToggleButton ("");
    fxToggle->setComponentID ("fxToggle");
    fxToggle->setToggleState (crtEnabled, juce::dontSendNotification);
    fxToggle->onClick = [safeThis, fxToggle]()
    {
        if (safeThis == nullptr || fxToggle == nullptr) return;
        safeThis->applyCrtState (fxToggle->getToggleState());
        safeThis->audioProcessor.setUiCrtEnabled (safeThis->crtEnabled);
        safeThis->repaint();
    };
    aw->addAndMakeVisible (fxToggle);

    auto* fxLabel = addPopupLabel ("fxLabel", "GRAPHIC FX", labelFont);

    auto syncAndRepaintPopup = [safeThis, safeAw]()
    {
        if (safeThis == nullptr || safeAw == nullptr) return;
        syncGraphicsPopupState (*safeAw, safeThis->defaultPalette, safeThis->customPalette, safeThis->useCustomPalette);
        layoutGraphicsPopupContent (*safeAw);
        safeAw->repaint();
    };

    auto applyPaletteAndRepaint = [safeThis]()
    { if (safeThis == nullptr) return; safeThis->applyActivePalette(); safeThis->repaint(); };

    defaultToggle->onClick = [safeThis, defaultToggle, customToggle, applyPaletteAndRepaint, syncAndRepaintPopup]() mutable
    {
        if (safeThis == nullptr || defaultToggle == nullptr || customToggle == nullptr) return;
        safeThis->useCustomPalette = false;
        safeThis->audioProcessor.setUiUseCustomPalette (safeThis->useCustomPalette);
        defaultToggle->setToggleState (true, juce::dontSendNotification);
        customToggle->setToggleState (false, juce::dontSendNotification);
        applyPaletteAndRepaint(); syncAndRepaintPopup();
    };

    customToggle->onClick = [safeThis, defaultToggle, customToggle, applyPaletteAndRepaint, syncAndRepaintPopup]() mutable
    {
        if (safeThis == nullptr || defaultToggle == nullptr || customToggle == nullptr) return;
        safeThis->useCustomPalette = true;
        safeThis->audioProcessor.setUiUseCustomPalette (safeThis->useCustomPalette);
        defaultToggle->setToggleState (false, juce::dontSendNotification);
        customToggle->setToggleState (true, juce::dontSendNotification);
        applyPaletteAndRepaint(); syncAndRepaintPopup();
    };

    if (defaultLabel != nullptr && defaultToggle != nullptr)
        defaultLabel->onClick = [defaultToggle]() { defaultToggle->triggerClick(); };
    if (customLabel != nullptr && customToggle != nullptr)
        customLabel->onClick = [customToggle]() { customToggle->triggerClick(); };
    if (fxLabel != nullptr && fxToggle != nullptr)
        fxLabel->onClick = [fxToggle]() { fxToggle->triggerClick(); };

    for (int i = 0; i < 2; ++i)
    {
        if (auto* customSwatch = dynamic_cast<PopupSwatchButton*> (aw->findChildWithID ("customSwatch" + juce::String (i))))
        {
            customSwatch->onLeftClick = [safeThis, safeAw, i]()
            {
                if (safeThis == nullptr) return;
                auto& rng = juce::Random::getSystemRandom();
                const auto randomColour = juce::Colour::fromRGB ((juce::uint8) rng.nextInt (256),
                                                                 (juce::uint8) rng.nextInt (256),
                                                                 (juce::uint8) rng.nextInt (256));
                safeThis->customPalette[(size_t) i] = randomColour;
                safeThis->audioProcessor.setUiCustomPaletteColour (i, randomColour);
                if (safeThis->useCustomPalette) { safeThis->applyActivePalette(); safeThis->repaint(); }
                if (safeAw != nullptr)
                { syncGraphicsPopupState (*safeAw, safeThis->defaultPalette, safeThis->customPalette, safeThis->useCustomPalette);
                  layoutGraphicsPopupContent (*safeAw); safeAw->repaint(); }
            };

            customSwatch->onRightClick = [safeThis, safeAw, i]()
            {
                if (safeThis == nullptr) return;
                const auto scheme = safeThis->activeScheme;
                auto* colorAw = new juce::AlertWindow ("", "", juce::AlertWindow::NoIcon);
                colorAw->setLookAndFeel (&safeThis->lnf);
                colorAw->addTextEditor ("hex", colourToHexRgb (safeThis->customPalette[(size_t) i]), juce::String());
                if (auto* te = colorAw->getTextEditor ("hex")) te->setInputFilter (new HexInputFilter(), true);
                colorAw->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
                colorAw->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));
                styleAlertButtons (*colorAw, safeThis->lnf);
                applyPromptShellSize (*colorAw);
                layoutAlertWindowButtons (*colorAw);
                const juce::Font& kHexPromptFont = kBoldFont40();
                preparePromptTextEditor (*colorAw, "hex", scheme.bg, scheme.text, scheme.fg, kHexPromptFont, true, 6);

                if (safeThis != nullptr)
                {
                    fitAlertWindowToEditor (*colorAw, safeThis.getComponent(), [&] (juce::AlertWindow& a)
                    { layoutAlertWindowButtons (a); preparePromptTextEditor (a, "hex", scheme.bg, scheme.text, scheme.fg, kHexPromptFont, true, 6); });
                    embedAlertWindowInOverlay (safeThis.getComponent(), colorAw, true);
                }
                else
                {
                    colorAw->centreAroundComponent (safeThis.getComponent(), colorAw->getWidth(), colorAw->getHeight());
                    bringPromptWindowToFront (*colorAw); colorAw->repaint();
                }

                preparePromptTextEditor (*colorAw, "hex", scheme.bg, scheme.text, scheme.fg, kHexPromptFont, true, 6);
                juce::Component::SafePointer<juce::AlertWindow> safeColorAw (colorAw);
                juce::MessageManager::callAsync ([safeColorAw]() { if (safeColorAw != nullptr) { bringPromptWindowToFront (*safeColorAw); safeColorAw->repaint(); } });

                colorAw->enterModalState (true,
                    juce::ModalCallbackFunction::create ([safeThis, safeAw, colorAw, i] (int result) mutable
                    {
                        std::unique_ptr<juce::AlertWindow> killer (colorAw);
                        if (safeThis == nullptr) return;
                        if (result != 1) return;
                        juce::Colour parsed;
                        if (! tryParseHexColour (killer->getTextEditorContents ("hex"), parsed)) return;
                        safeThis->customPalette[(size_t) i] = parsed;
                        safeThis->audioProcessor.setUiCustomPaletteColour (i, parsed);
                        if (safeThis->useCustomPalette) { safeThis->applyActivePalette(); safeThis->repaint(); }
                        if (safeAw != nullptr)
                        { syncGraphicsPopupState (*safeAw, safeThis->defaultPalette, safeThis->customPalette, safeThis->useCustomPalette);
                          layoutGraphicsPopupContent (*safeAw); safeAw->repaint(); }
                    }));
            };
        }
    }

    applyPromptShellSize (*aw);
    syncGraphicsPopupState (*aw, defaultPalette, customPalette, useCustomPalette);
    layoutGraphicsPopupContent (*aw);

    if (safeThis != nullptr)
    {
        fitAlertWindowToEditor (*aw, safeThis.getComponent(), [&] (juce::AlertWindow& a)
        { syncGraphicsPopupState (a, defaultPalette, customPalette, useCustomPalette); layoutGraphicsPopupContent (a); });
    }
    if (safeThis != nullptr)
    {
        embedAlertWindowInOverlay (safeThis.getComponent(), aw);
        juce::MessageManager::callAsync ([safeAw, safeThis]()
        { if (safeAw == nullptr || safeThis == nullptr) return; safeAw->toFront (false); safeAw->repaint(); });
    }
    else
    {
        aw->centreAroundComponent (this, aw->getWidth(), aw->getHeight());
        bringPromptWindowToFront (*aw); aw->repaint();
    }

    aw->enterModalState (true,
        juce::ModalCallbackFunction::create ([safeThis, aw] (int) mutable
        { std::unique_ptr<juce::AlertWindow> killer (aw); if (safeThis != nullptr) safeThis->setPromptOverlayActive (false); }));
}

//==============================================================================
//  Layout helpers
//==============================================================================
GRATRAudioProcessorEditor::HorizontalLayoutMetrics
GRATRAudioProcessorEditor::buildHorizontalLayout (int editorW, int valueColW)
{
    HorizontalLayoutMetrics m;
    m.barW = (int) std::round (editorW * 0.455);
    m.valuePad = (int) std::round (editorW * 0.02);
    m.valueW = valueColW;
    m.contentW = m.barW + m.valuePad + m.valueW;
    m.leftX = juce::jmax (6, (editorW - m.contentW) / 2);
    return m;
}

GRATRAudioProcessorEditor::VerticalLayoutMetrics
GRATRAudioProcessorEditor::buildVerticalLayout (int editorH, int biasY, bool ioExpanded)
{
    VerticalLayoutMetrics m;
    m.rhythm = juce::jlimit (6, 16, (int) std::round (editorH * 0.018));
    const int nominalBarH = juce::jlimit (14, 120, m.rhythm * 6);
    const int nominalGapY = juce::jmax (4, m.rhythm * 4);

    m.titleH = juce::jlimit (24, 56, m.rhythm * 4);
    m.titleAreaH = m.titleH + 4;
    const int computedTitleTopPad = 6 + biasY;
    m.titleTopPad = (computedTitleTopPad > 8) ? computedTitleTopPad : 8;
    const int titleGap = m.titleTopPad;
    m.topMargin = m.titleTopPad + m.titleAreaH + titleGap;
    m.betweenSlidersAndButtons = juce::jmax (8, m.rhythm * 2);
    m.bottomMargin = m.titleTopPad;

    m.box = juce::jlimit (40, kToggleBoxPx, (int) std::round (editorH * 0.085));
    m.btnRowGap = juce::jlimit (4, 14, (int) std::round (editorH * 0.008));
    m.btnRow3Y = editorH - m.bottomMargin - m.box;
    m.btnRow2Y = m.btnRow3Y - m.btnRowGap - m.box;
    m.btnRow1Y = m.btnRow2Y - m.btnRowGap - m.box;

    // When IO is expanded, buttons are hidden and chaos sits at the bottom
    m.chaosRowY = ioExpanded ? (editorH - m.bottomMargin - m.box) : 0;

    const int sliderBottomRef = ioExpanded ? m.chaosRowY : m.btnRow1Y;
    m.availableForSliders = juce::jmax (40, sliderBottomRef - m.betweenSlidersAndButtons - m.topMargin);

    // Bars below toggle: 8 IO bars when expanded (IN/OUT/TILT/FILTER/PAN/MIX/LIM/MODE_ROW),
    // 5 main bars when collapsed (TIME/MOD/PITCH/FORMANT/STYLE).
    const int numSliders = ioExpanded ? 8 : 5;
    const int numGaps    = ioExpanded ? 8 : 5;

    m.toggleBarH = 20;
    const int spaceForScale = juce::jmax (40, m.availableForSliders - m.toggleBarH);

    const int nominalStack = numSliders * nominalBarH + numGaps * nominalGapY;
    const double stackScale = nominalStack > 0 ? juce::jmin (1.0, (double) spaceForScale / (double) nominalStack)
                                               : 1.0;

    m.barH = juce::jmax (14, (int) std::round (nominalBarH * stackScale));
    m.gapY = juce::jmax (4,  (int) std::round (nominalGapY * stackScale));

    auto stackHeight = [&]() { return numSliders * m.barH + numGaps * m.gapY; };

    while (stackHeight() > spaceForScale && m.gapY > 4)
        --m.gapY;

    while (stackHeight() > spaceForScale && m.barH > 14)
        --m.barH;

    m.topY = m.topMargin;
    m.toggleBarY = m.topY;

    return m;
}

void GRATRAudioProcessorEditor::updateCachedLayout()
{
    cachedHLayout_ = buildHorizontalLayout (getWidth(), getTargetValueColumnWidth());
    cachedVLayout_ = buildVerticalLayout (getHeight(), kLayoutVerticalBiasPx, ioSectionExpanded_);

    const juce::Slider* sliders[10] = { &timeSlider, &modSlider, &pitchSlider, &formantSlider, &modeSlider,
                                        &inputSlider, &outputSlider, &tiltSlider, &mixSlider, &panSlider };

    for (int i = 0; i < 10; ++i)
    {
        if (! sliders[i]->isVisible())
        {
            cachedValueAreas_[(size_t) i] = {};
            continue;
        }

        const auto& bb = sliders[i]->getBounds();
        const int valueX = bb.getRight() + cachedHLayout_.valuePad;
        const int maxW = juce::jmax (0, getWidth() - valueX - kValueAreaRightMarginPx);
        const int vw   = juce::jmin (cachedHLayout_.valueW, maxW);
        const int y    = bb.getCentreY() - (kValueAreaHeightPx / 2);
        cachedValueAreas_[(size_t) i] = { valueX, y, juce::jmax (0, vw), kValueAreaHeightPx };
    }

    if (filterBar_.isVisible())
    {
        const auto& bb = filterBar_.getBounds();
        const int valueX = bb.getRight() + cachedHLayout_.valuePad;
        const int maxW = juce::jmax (0, getWidth() - valueX - kValueAreaRightMarginPx);
        const int vw   = juce::jmin (cachedHLayout_.valueW, maxW);
        const int y    = bb.getCentreY() - (kValueAreaHeightPx / 2);
        cachedFilterValueArea_ = { valueX, y, juce::jmax (0, vw), kValueAreaHeightPx };
    }
    else
    {
        cachedFilterValueArea_ = {};
    }

    if (tiltSlider.isVisible())
    {
        const auto& bb = tiltSlider.getBounds();
        const int valueX = bb.getRight() + cachedHLayout_.valuePad;
        const int maxW = juce::jmax (0, getWidth() - valueX - kValueAreaRightMarginPx);
        const int vw   = juce::jmin (cachedHLayout_.valueW, maxW);
        const int y    = bb.getCentreY() - (kValueAreaHeightPx / 2);
        cachedTiltValueArea_ = { valueX, y, juce::jmax (0, vw), kValueAreaHeightPx };
    }
    else
    {
        cachedTiltValueArea_ = {};
    }

    if (panSlider.isVisible())
    {
        const auto& bb = panSlider.getBounds();
        const int valueX = bb.getRight() + cachedHLayout_.valuePad;
        const int maxW = juce::jmax (0, getWidth() - valueX - kValueAreaRightMarginPx);
        const int vw   = juce::jmin (cachedHLayout_.valueW, maxW);
        const int y    = bb.getCentreY() - (kValueAreaHeightPx / 2);
        cachedPanValueArea_ = { valueX, y, juce::jmax (0, vw), kValueAreaHeightPx };
    }
    else
    {
        cachedPanValueArea_ = {};
    }

    if (limThresholdSlider.isVisible())
    {
        const auto& bb = limThresholdSlider.getBounds();
        const int valueX = bb.getRight() + cachedHLayout_.valuePad;
        const int maxW = juce::jmax (0, getWidth() - valueX - kValueAreaRightMarginPx);
        const int vw   = juce::jmin (cachedHLayout_.valueW, maxW);
        const int y    = bb.getCentreY() - (kValueAreaHeightPx / 2);
        cachedLimThresholdValueArea_ = { valueX, y, juce::jmax (0, vw), kValueAreaHeightPx };
    }
    else
    {
        cachedLimThresholdValueArea_ = {};
    }

    if (chaosFilterButton.isVisible())
        cachedChaosArea_ = chaosFilterButton.getBounds().getUnion (chaosDelayButton.getBounds());
    else
        cachedChaosArea_ = {};

    cachedToggleBarArea_ = { cachedHLayout_.leftX, cachedVLayout_.toggleBarY,
                             cachedHLayout_.contentW, cachedVLayout_.toggleBarH };
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

    const int formantMaxW = juce::jmax (stringWidth (font, kFormantLegendFull),
                                        juce::jmax (stringWidth (font, kFormantLegendShort),
                                                    stringWidth (font, kFormantLegendInt)));

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
                                 juce::jmax (juce::jmax (inputMaxW, outputMaxW), juce::jmax (mixMaxW, juce::jmax (formantMaxW, limMaxW))));

    const int desired = maxW + 16;
    const int minW = 90;
    const int maxAllowed = juce::jmax (minW, (int) std::round (getWidth() * 0.40));
    cachedValueColumnWidth = juce::jlimit (minW, maxAllowed, desired);
    cachedValueColumnWidthKey = key;
    return cachedValueColumnWidth;
}

//========================== Hit areas ==========================

juce::Rectangle<int> GRATRAudioProcessorEditor::getValueAreaFor (const juce::Rectangle<int>& barBounds) const
{
    const int valueX = barBounds.getRight() + cachedHLayout_.valuePad;
    const int maxW = juce::jmax (0, getWidth() - valueX - kValueAreaRightMarginPx);
    const int valueW = juce::jmin (cachedHLayout_.valueW, maxW);

    const int y = barBounds.getCentreY() - (kValueAreaHeightPx / 2);
    return { valueX, y, juce::jmax (0, valueW), kValueAreaHeightPx };
}

juce::Slider* GRATRAudioProcessorEditor::getSliderForValueAreaPoint (juce::Point<int> p)
{
    juce::Slider* sliders[7] = { &timeSlider, &modSlider, &pitchSlider, &modeSlider,
                                  &inputSlider, &outputSlider, &mixSlider };

    for (int i = 0; i < 7; ++i)
        if (cachedValueAreas_[(size_t) i].contains (p))
            return sliders[i];

    return nullptr;
}

namespace
{
    int getToggleVisualBoxSidePx (const juce::Component& button)
    {
        const int h = button.getHeight();
        return juce::jlimit (14, juce::jmax (14, h - 2), (int) std::lround ((double) h * 0.65));
    }

    int getToggleVisualBoxLeftPx (const juce::Component& button)
    {
        return button.getX() + 2;
    }

    juce::Rectangle<int> makeToggleLabelArea (const juce::Component& button,
                                              int collisionRight,
                                              const juce::String& fullLabel,
                                              const juce::String& shortLabel)
    {
        const auto b = button.getBounds();
        const int visualRight = getToggleVisualBoxLeftPx (button) + getToggleVisualBoxSidePx (button);
        const int x = visualRight + kToggleLabelGapPx;

        const auto& labelFont = kBoldFont40();
        const int fullW  = stringWidth (labelFont, fullLabel) + 2;
        const int shortW = stringWidth (labelFont, shortLabel) + 2;
        const int maxW   = juce::jmax (0, collisionRight - x);

        const int w = (fullW <= maxW) ? fullW : juce::jmin (shortW, maxW);
        return { x, b.getY(), w, b.getHeight() };
    }

    juce::String chooseToggleLabel (const juce::Component& button,
                                   int collisionRight,
                                   const juce::String& fullLabel,
                                   const juce::String& shortLabel)
    {
        const int visualRight = getToggleVisualBoxLeftPx (button) + getToggleVisualBoxSidePx (button);
        const int x = visualRight + kToggleLabelGapPx;
        const auto& labelFont = kBoldFont40();
        const int fullW = stringWidth (labelFont, fullLabel) + 2;
        return (fullW <= juce::jmax (0, collisionRight - x)) ? fullLabel : shortLabel;
    }
}

juce::Rectangle<int> GRATRAudioProcessorEditor::getSyncLabelArea() const
{
    return makeToggleLabelArea (syncButton, midiButton.getX() - kToggleLegendCollisionPadPx, "SYNC", "SYN");
}

juce::Rectangle<int> GRATRAudioProcessorEditor::getAutoLabelArea() const
{
    return makeToggleLabelArea (autoButton, triggerButton.getX() - kToggleLegendCollisionPadPx, "AUTO", "AUT");
}

juce::Rectangle<int> GRATRAudioProcessorEditor::getTriggerLabelArea() const
{
    return makeToggleLabelArea (triggerButton, getWidth() - kToggleLegendCollisionPadPx, "TRIGGER", "TRG");
}

juce::Rectangle<int> GRATRAudioProcessorEditor::getReverseLabelArea() const
{
    return makeToggleLabelArea (reverseButton, envGraButton.getX() - kToggleLegendCollisionPadPx, "REVERSE", "RVS");
}

juce::Rectangle<int> GRATRAudioProcessorEditor::getEnvGraLabelArea() const
{
    return makeToggleLabelArea (envGraButton, getWidth() - kToggleLegendCollisionPadPx, "ENV GRA", "ENV");
}

juce::Rectangle<int> GRATRAudioProcessorEditor::getMidiLabelArea() const
{
    return makeToggleLabelArea (midiButton, getWidth() - kToggleLegendCollisionPadPx, "MIDI", "MD");
}

juce::Rectangle<int> GRATRAudioProcessorEditor::getChaosLabelArea() const
{
    if (! chaosFilterButton.isVisible())
        return {};

    return makeToggleLabelArea (chaosFilterButton,
                                chaosDelayButton.getX() - kToggleLegendCollisionPadPx,
                                "CHSF", "CHSF");
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

    // Toggle IO section expand/collapse
    if (cachedToggleBarArea_.contains (p))
    {
        ioSectionExpanded_ = ! ioSectionExpanded_;
        audioProcessor.setUiIoExpanded (ioSectionExpanded_);
        resized();
        repaint();
        return;
    }

    if (e.mods.isPopupMenu())
    {
        if (auto* slider = getSliderForValueAreaPoint (p))
        {
            openNumericEntryPopupForSlider (*slider);
            return;
        }
    }

    {
        auto infoArea = getInfoIconArea();
        if (crtEnabled)
            infoArea = infoArea.expanded (4, 0);
        if (infoArea.contains (p))
        {
            openInfoPopup();
            return;
        }
    }

    if (getSyncLabelArea().contains (p))
    {
        syncButton.setToggleState (! syncButton.getToggleState(), juce::sendNotificationSync);
        return;
    }

    if (getAutoLabelArea().contains (p))
    {
        autoButton.setToggleState (! autoButton.getToggleState(), juce::sendNotificationSync);
        return;
    }

    if (getTriggerLabelArea().contains (p))
    {
        triggerButton.setToggleState (! triggerButton.getToggleState(), juce::sendNotificationSync);
        return;
    }

    if (getReverseLabelArea().contains (p))
    {
        reverseButton.setToggleState (! reverseButton.getToggleState(), juce::sendNotificationSync);
        return;
    }

    if (getEnvGraLabelArea().contains (p) || envGraDisplay.getBounds().contains (p))
    {
        if (e.mods.isPopupMenu())
            openEnvGraPrompt();
        else
            envGraButton.setToggleState (! envGraButton.getToggleState(), juce::sendNotificationSync);
        return;
    }

    if (getMidiLabelArea().contains (p) || midiChannelDisplay.getBounds().contains (p))
    {
        if (e.mods.isPopupMenu())
            openMidiChannelPrompt();
        else
            midiButton.setToggleState (! midiButton.getToggleState(), juce::sendNotificationSync);
        return;
    }

    if (getChaosLabelArea().contains (p) || chaosFilterDisplay.getBounds().contains (p))
    {
        if (e.mods.isPopupMenu())
            openChaosFilterPrompt();
        else
            chaosFilterButton.setToggleState (! chaosFilterButton.getToggleState(), juce::sendNotificationSync);
        return;
    }

    if (chaosDelayButton.isVisible())
    {
        const auto bb = chaosDelayButton.getBounds();
        const int toggleVisualSide = juce::jlimit (14, juce::jmax (14, bb.getHeight() - 2),
                                                   (int) std::lround (bb.getHeight() * 0.65));
        const int toggleHitW = toggleVisualSide + 6;
        const int lx = bb.getX() + toggleHitW + 4;
        const juce::Rectangle<int> dLabelArea { lx, bb.getY(), bb.getRight() - lx, bb.getHeight() };
        if (dLabelArea.contains (p) || chaosDelayDisplay.getBounds().contains (p))
        {
            if (e.mods.isPopupMenu())
                openChaosDelayPrompt();
            else
                chaosDelayButton.setToggleState (! chaosDelayButton.getToggleState(), juce::sendNotificationSync);
            return;
        }
    }
}

void GRATRAudioProcessorEditor::mouseDrag (const juce::MouseEvent& e)
{
    juce::ignoreUnused (e);
    lastUserInteractionMs.store (juce::Time::getMillisecondCounter(), std::memory_order_relaxed);
}

void GRATRAudioProcessorEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto p = e.getPosition();

    if (auto* slider = getSliderForValueAreaPoint (p))
    {
        if (slider == &timeSlider)          slider->setValue (kDefaultTimeMs, juce::sendNotificationSync);
        else if (slider == &pitchSlider)    slider->setValue (0.0, juce::sendNotificationSync);
        else if (slider == &modeSlider)     slider->setValue (0.0, juce::sendNotificationSync);
        else if (slider == &modSlider)      slider->setValue (0.5, juce::sendNotificationSync);
        else if (slider == &inputSlider)    slider->setValue (kDefaultInput, juce::sendNotificationSync);
        else if (slider == &outputSlider)   slider->setValue (kDefaultOutput, juce::sendNotificationSync);
        else if (slider == &mixSlider)      slider->setValue (kDefaultMix, juce::sendNotificationSync);
        return;
    }
}

//==============================================================================

void GRATRAudioProcessorEditor::paint (juce::Graphics& g)
{
    const int W = getWidth();
    const auto& horizontalLayout = cachedHLayout_;
    const auto& verticalLayout   = cachedVLayout_;

    const auto scheme = activeScheme;

    g.fillAll (scheme.bg);
    g.setColour (scheme.text);

    constexpr float baseFontPx = 40.0f;
    constexpr float minFontPx  = 18.0f;
    constexpr float fullShrinkFloor = baseFontPx * 0.75f;
    g.setFont (kBoldFont40());

    auto tryDrawLegend = [&] (const juce::Rectangle<int>& area,
                              const juce::String& text,
                              float shrinkFloor) -> bool
    {
        auto t = text.trim();
        if (t.isEmpty() || area.getWidth() <= 2 || area.getHeight() <= 2)
            return false;

        const int split = t.lastIndexOfChar (' ');
        if (split <= 0 || split >= t.length() - 1)
        {
            g.setFont (kBoldFont40());
            return drawIfFitsWithOptionalShrink (g, area, t, baseFontPx, shrinkFloor);
        }

        const auto value  = t.substring (0, split).trimEnd();
        const auto suffix = t.substring (split + 1).trimStart();

        g.setFont (kBoldFont40());
        if (drawValueWithRightAlignedSuffix (g, area, value, suffix, false,
                                              baseFontPx, shrinkFloor))
        {
            g.setColour (scheme.text);
            return true;
        }
        return false;
    };

    auto drawLegendForMode = [&] (const juce::Rectangle<int>& area,
                                  const juce::String& fullLegend,
                                  const juce::String& shortLegend,
                                  const juce::String& intOnlyLegend)
    {
        if (tryDrawLegend (area, fullLegend, fullShrinkFloor))
            return;

        if (tryDrawLegend (area, shortLegend, minFontPx))
            return;

        g.setFont (kBoldFont40());
        drawValueNoEllipsis (g, area, intOnlyLegend, juce::String(), intOnlyLegend, baseFontPx, minFontPx);
        g.setColour (scheme.text);
    };

    {
        const int titleH = verticalLayout.titleH;

        const int barW = horizontalLayout.barW;
        const int contentW = horizontalLayout.contentW;
        const int leftX = horizontalLayout.leftX;

        const int titleX = juce::jlimit (0, juce::jmax (0, W - 1), leftX);
        const int titleW = juce::jmax (0, juce::jmin (contentW, W - titleX));
        const int titleY = verticalLayout.titleTopPad;

        auto titleFont = g.getCurrentFont();
        titleFont.setHeight ((float) titleH);
        g.setFont (titleFont);

        const auto titleArea = juce::Rectangle<int> (titleX, titleY, titleW, titleH + kTitleAreaExtraHeightPx);
        const juce::String titleText ("GRA-TR");

        g.drawText (titleText, titleArea.getX(), titleArea.getY(), titleArea.getWidth(), titleArea.getHeight(), juce::Justification::left, false);

        const auto infoIconArea = getInfoIconArea();
        const int titleRightLimit = infoIconArea.getX() - kTitleRightGapToInfoPx;
        const int titleMaxW = juce::jmax (0, titleRightLimit - titleArea.getX());
        const int titleBaseW = stringWidth (titleFont, titleText);
        const int originalTitleLimitW = juce::jmax (0, juce::jmin (titleW, barW));
        const bool originalWouldClipTitle = titleBaseW > originalTitleLimitW;

        if (titleMaxW > 0 && (originalWouldClipTitle || titleBaseW > titleMaxW))
        {
            auto fittedTitleFont = titleFont;
            fittedTitleFont.setHorizontalScale (1.0f);
            const float titleMinScale = juce::jlimit (0.4f, 1.0f, 12.0f / (float) titleH);
            for (float s = 1.0f; s >= titleMinScale; s -= 0.025f)
            {
                fittedTitleFont.setHorizontalScale (s);
                if (stringWidth (fittedTitleFont, titleText) <= titleMaxW)
                    break;
            }

            g.setColour (scheme.text);
            g.setFont (fittedTitleFont);
            g.drawText (titleText, titleArea.getX(), titleArea.getY(), titleMaxW, titleArea.getHeight(), juce::Justification::left, false);
        }

        g.setColour (scheme.text);

        auto versionFont = juce::Font (juce::FontOptions (juce::jmax (10.0f, (float) titleH * UiMetrics::versionFontRatio)).withStyle ("Bold"));
        g.setFont (versionFont);

        const int versionH = juce::jlimit (10, infoIconArea.getHeight(), (int) std::round ((double) infoIconArea.getHeight() * UiMetrics::versionHeightRatio));
        const int versionY = infoIconArea.getBottom() - versionH;

        const int desiredVersionW = juce::jlimit (28, 64, (int) std::round ((double) infoIconArea.getWidth() * UiMetrics::versionDesiredWidthRatio));
        const int versionRight = infoIconArea.getX() - kVersionGapPx;
        const int versionLeftLimit = titleArea.getX();
        const int versionX = juce::jmax (versionLeftLimit, versionRight - desiredVersionW);
        const int versionW = juce::jmax (0, versionRight - versionX);

        if (versionW > 0)
            g.drawText (juce::String ("v") + InfoContent::version,
                        versionX, versionY, versionW, versionH,
                        juce::Justification::bottomRight, false);

        g.setFont (kBoldFont40());
    }

    // ── Toggle bar (triangle + rounded horizontal bar) ──
    {
        if (! cachedToggleBarArea_.isEmpty())
        {
            const float barRadius = (float) cachedToggleBarArea_.getHeight() * 0.3f;
            g.setColour (scheme.fg.withAlpha (0.25f));
            g.fillRoundedRectangle (cachedToggleBarArea_.toFloat(), barRadius);

            const float triH = (float) cachedToggleBarArea_.getHeight() * 0.8f;
            const float triW = triH * 1.125f;
            const float cx = (float) cachedToggleBarArea_.getCentreX();
            const float cy = (float) cachedToggleBarArea_.getCentreY();

            juce::Path tri;
            if (ioSectionExpanded_)
            {
                tri.addTriangle (cx - triW * 0.5f, cy + triH * 0.35f,
                                 cx + triW * 0.5f, cy + triH * 0.35f,
                                 cx,               cy - triH * 0.35f);
            }
            else
            {
                tri.addTriangle (cx - triW * 0.5f, cy - triH * 0.35f,
                                 cx + triW * 0.5f, cy - triH * 0.35f,
                                 cx,               cy + triH * 0.35f);
            }
            g.setColour (scheme.text);
            g.fillPath (tri);
        }
    }

    g.setColour (scheme.text);

    {
        const juce::String* fullTexts[10]  = { &cachedTimeTextFull, &cachedModTextFull, &cachedPitchTextFull,
                                               &cachedFormantTextFull, &cachedModeTextFull, &cachedInputTextFull,
                                               &cachedOutputTextFull, &cachedTiltTextFull, &cachedMixTextFull, &cachedPanTextFull };
        const juce::String* shortTexts[10] = { &cachedTimeTextShort, &cachedModTextShort, &cachedPitchTextShort,
                                               &cachedFormantTextShort, &cachedModeTextShort, &cachedInputTextShort,
                                               &cachedOutputTextShort, &cachedTiltTextShort, &cachedMixTextShort, &cachedPanTextShort };
        const juce::String* intTexts[10] = {
            &cachedTimeIntOnly,
            &cachedModIntOnly,
            &cachedPitchIntOnly,
            &cachedFormantIntOnly,
            &cachedModeIntOnly,
            &cachedInputIntOnly,
            &cachedOutputIntOnly,
            &cachedTiltIntOnly,
            &cachedMixIntOnly,
            &cachedPanIntOnly
        };

        for (int i = 0; i < 10; ++i)
            drawLegendForMode (cachedValueAreas_[(size_t) i], *fullTexts[i], *shortTexts[i], *intTexts[i]);

        if (tiltSlider.isVisible() && cachedTiltValueArea_.getWidth() > 0)
            drawLegendForMode (cachedTiltValueArea_, cachedTiltTextFull, cachedTiltTextShort, cachedTiltIntOnly);

        if (filterBar_.isVisible() && cachedFilterValueArea_.getWidth() > 0)
            drawLegendForMode (cachedFilterValueArea_, cachedFilterTextFull, cachedFilterTextShort, cachedFilterTextShort);

        if (panSlider.isVisible() && cachedPanValueArea_.getWidth() > 0)
            drawLegendForMode (cachedPanValueArea_, cachedPanTextFull, cachedPanTextShort, cachedPanTextShort);

        if (limThresholdSlider.isVisible() && cachedLimThresholdValueArea_.getWidth() > 0)
            drawLegendForMode (cachedLimThresholdValueArea_, cachedLimThresholdTextFull, cachedLimThresholdTextShort, cachedLimThresholdIntOnly);

        // Mode In / Mode Out / Sum Bus / Limiter Mode labels above combos
        if (modeInCombo.isVisible())
        {
            const auto font = juce::Font (juce::FontOptions (11.0f).withStyle ("Bold"));
            g.setFont (font);
            auto drawComboLabel = [&] (const juce::ComboBox& combo, const juce::String& full, const juce::String& shortTxt)
            {
                const auto area = combo.getBounds().withHeight (14).translated (0, -15);
                const float comboW = (float) combo.getWidth();
                juce::GlyphArrangement ga;
                ga.addLineOfText (font, full, 0.0f, 0.0f);
                const bool useShort = ga.getBoundingBox (0, -1, false).getWidth() > comboW;
                g.drawText (useShort ? shortTxt : full, area, juce::Justification::centred);
            };
            drawComboLabel (modeInCombo,  "MODE IN",  "IN");
            drawComboLabel (modeOutCombo, "MODE OUT", "OUT");
            drawComboLabel (sumBusCombo,  "SUM BUS",  "SUM");
            drawComboLabel (limModeCombo, "LIMIT",    "LIM");
        }

        g.setFont (kBoldFont40());
        if (chaosFilterButton.isVisible())
        {
            const auto chaosArea = getChaosLabelArea();
            if (chaosArea.getWidth() > 0)
                g.drawText ("CHSF", chaosArea, juce::Justification::left, true);
        }
        if (chaosDelayButton.isVisible())
        {
            const auto dArea = makeToggleLabelArea (chaosDelayButton,
                                                     getWidth() - kToggleLegendCollisionPadPx,
                                                     "CHSD", "CHSD");
            if (dArea.getWidth() > 0)
                g.drawText ("CHSD", dArea, juce::Justification::left, true);
        }
    }

    {
        const auto& labelFont = kBoldFont40();
        g.setFont (labelFont);

        if (reverseButton.isVisible())
        {
        // Row 1: RVS + ENV GRA
        const int rvsCR    = envGraButton.getX() - kToggleLegendCollisionPadPx;
        const int envGraCR = getWidth() - kToggleLegendCollisionPadPx;
        // Row 2: AUTO + TRIGGER
        const int autoCR   = triggerButton.getX() - kToggleLegendCollisionPadPx;
        const int trgCR    = getWidth() - kToggleLegendCollisionPadPx;
        // Row 3: SYNC + MIDI
        const int syncCR   = midiButton.getX() - kToggleLegendCollisionPadPx;
        const int midiCR   = getWidth() - kToggleLegendCollisionPadPx;

        const juce::String rvsLabel    = chooseToggleLabel (reverseButton, rvsCR,    "REVERSE",  "RVS");
        const juce::String envGraLabel = chooseToggleLabel (envGraButton,  envGraCR, "ENV GRA",  "ENV");
        const juce::String autoLabel   = chooseToggleLabel (autoButton,    autoCR,   "AUTO",     "AUT");
        const juce::String trgLabel    = chooseToggleLabel (triggerButton,  trgCR,   "TRIGGER",  "TRG");
        const juce::String syncLabel   = chooseToggleLabel (syncButton,    syncCR,   "SYNC",     "SYN");
        const juce::String midiLabel   = chooseToggleLabel (midiButton,    midiCR,   "MIDI",     "MD");

        auto drawToggleLegend = [&] (const juce::Rectangle<int>& labelArea,
                                     const juce::String& labelText,
                                     int noCollisionRight)
        {
            const int safeW = juce::jmax (0, noCollisionRight - labelArea.getX());
            auto snapEven = [] (int v) { return v & ~1; };
            const int ax = snapEven (labelArea.getX());
            const int ay = snapEven (labelArea.getY());
            const int aw = snapEven (safeW);
            const int ah = labelArea.getHeight();
            const auto drawArea = juce::Rectangle<int> (ax, ay, aw, ah);

            g.drawText (labelText, drawArea.getX(), drawArea.getY(), drawArea.getWidth(), drawArea.getHeight(), juce::Justification::left, true);
        };

        // Row 1: RVS + ENV GRA
        drawToggleLegend (getReverseLabelArea(), rvsLabel,    rvsCR);
        drawToggleLegend (getEnvGraLabelArea(),  envGraLabel, envGraCR);

        // Row 2: AUTO + TRIGGER
        drawToggleLegend (getAutoLabelArea(),    autoLabel,   autoCR);
        drawToggleLegend (getTriggerLabelArea(), trgLabel,    trgCR);

        // Row 3: SYNC + MIDI
        drawToggleLegend (getSyncLabelArea(),    syncLabel,   syncCR);
        drawToggleLegend (getMidiLabelArea(),    midiLabel,   midiCR);
        }
    }

    g.setColour (scheme.text);

    {
        if (cachedInfoGearPath.isEmpty())
            updateInfoIconCache();

        g.setColour (scheme.text);
        g.fillPath (cachedInfoGearPath);
        g.strokePath (cachedInfoGearPath, juce::PathStrokeType (1.0f));

        g.setColour (scheme.bg);
        g.fillEllipse (cachedInfoGearHole);
    }
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

    const int mainTop = verticalLayout.toggleBarY + verticalLayout.toggleBarH + verticalLayout.gapY;
    const int step = verticalLayout.barH + verticalLayout.gapY;

    if (ioSectionExpanded_)
    {
        // Expanded: [toggle bar] → INPUT, OUTPUT, TILT, FILTER, PAN, MIX, LIM, MODE combos, CHAOS; main params hidden
        inputSlider.setBounds  (horizontalLayout.leftX, mainTop + 0 * step, horizontalLayout.barW, verticalLayout.barH);
        outputSlider.setBounds (horizontalLayout.leftX, mainTop + 1 * step, horizontalLayout.barW, verticalLayout.barH);
        tiltSlider.setBounds   (horizontalLayout.leftX, mainTop + 2 * step, horizontalLayout.barW, verticalLayout.barH);
        filterBar_.setBounds   (horizontalLayout.leftX, mainTop + 3 * step, horizontalLayout.barW, verticalLayout.barH);
        panSlider.setBounds    (horizontalLayout.leftX, mainTop + 4 * step, horizontalLayout.barW, verticalLayout.barH);
        mixSlider.setBounds    (horizontalLayout.leftX, mainTop + 5 * step, horizontalLayout.barW, verticalLayout.barH);
        limThresholdSlider.setBounds (horizontalLayout.leftX, mainTop + 6 * step, horizontalLayout.barW, verticalLayout.barH);

        const int modeRowPad = 10;

        // Mode In / Mode Out / Sum Bus / Limiter Mode — 4 combos on row 7
        {
            const int modeY = mainTop + 7 * step + modeRowPad;
            const int comboGap = 4;
            const int totalW = horizontalLayout.barW + horizontalLayout.valuePad + horizontalLayout.valueW;
            const int comboW = (totalW - comboGap * 3) / 4;
            const int comboH = juce::jmax (24, verticalLayout.barH);
            modeInCombo.setBounds  (horizontalLayout.leftX,                           modeY, comboW, comboH);
            modeOutCombo.setBounds (horizontalLayout.leftX + (comboW + comboGap),      modeY, comboW, comboH);
            sumBusCombo.setBounds  (horizontalLayout.leftX + (comboW + comboGap) * 2,  modeY, comboW, comboH);
            limModeCombo.setBounds (horizontalLayout.leftX + (comboW + comboGap) * 3,  modeY, comboW, comboH);
        }

        const int chaosY = verticalLayout.chaosRowY;
        const int chaosH = verticalLayout.box;
        const int chaosRightX = horizontalLayout.leftX + horizontalLayout.barW + horizontalLayout.valuePad;
        const int chaosLeftW  = chaosRightX - horizontalLayout.leftX;
        const int chaosRightW = horizontalLayout.leftX + horizontalLayout.contentW - chaosRightX;
        chaosFilterButton.setBounds  (horizontalLayout.leftX, chaosY, chaosLeftW,  chaosH);
        chaosFilterDisplay.setBounds (horizontalLayout.leftX, chaosY, chaosLeftW,  chaosH);
        chaosDelayButton.setBounds   (chaosRightX,            chaosY, chaosRightW, chaosH);
        chaosDelayDisplay.setBounds  (chaosRightX,            chaosY, chaosRightW, chaosH);

        inputSlider.setVisible (true);
        outputSlider.setVisible (true);
        tiltSlider.setVisible (true);
        filterBar_.setVisible (true);
        panSlider.setVisible (true);
        mixSlider.setVisible (true);
        limThresholdSlider.setVisible (true);
        modeInCombo.setVisible (true);
        modeOutCombo.setVisible (true);
        sumBusCombo.setVisible (true);
        limModeCombo.setVisible (true);
        chaosFilterButton.setVisible (true);
        chaosFilterDisplay.setVisible (true);
        chaosDelayButton.setVisible (true);
        chaosDelayDisplay.setVisible (true);

        reverseButton.setVisible (false);
        envGraButton.setVisible (false);
        envGraDisplay.setVisible (false);
        autoButton.setVisible (false);
        triggerButton.setVisible (false);
        syncButton.setVisible (false);
        midiButton.setVisible (false);
        midiChannelDisplay.setVisible (false);

        timeSlider.setBounds (0, 0, 0, 0);
        modSlider.setBounds (0, 0, 0, 0);
        pitchSlider.setBounds (0, 0, 0, 0);
        formantSlider.setBounds (0, 0, 0, 0);
        modeSlider.setBounds (0, 0, 0, 0);

        timeSlider.setVisible (false);
        modSlider.setVisible (false);
        pitchSlider.setVisible (false);
        formantSlider.setVisible (false);
        modeSlider.setVisible (false);
    }
    else
    {
        // Collapsed: [toggle bar] → main params; IO hidden
        timeSlider.setBounds     (horizontalLayout.leftX, mainTop + 0 * step, horizontalLayout.barW, verticalLayout.barH);
        modSlider.setBounds      (horizontalLayout.leftX, mainTop + 1 * step, horizontalLayout.barW, verticalLayout.barH);
        pitchSlider.setBounds    (horizontalLayout.leftX, mainTop + 2 * step, horizontalLayout.barW, verticalLayout.barH);
        formantSlider.setBounds  (horizontalLayout.leftX, mainTop + 3 * step, horizontalLayout.barW, verticalLayout.barH);
        modeSlider.setBounds     (horizontalLayout.leftX, mainTop + 4 * step, horizontalLayout.barW, verticalLayout.barH);

        timeSlider.setVisible (true);
        modSlider.setVisible (true);
        pitchSlider.setVisible (true);
        formantSlider.setVisible (true);
        modeSlider.setVisible (true);

        inputSlider.setBounds (0, 0, 0, 0);
        outputSlider.setBounds (0, 0, 0, 0);
        tiltSlider.setBounds (0, 0, 0, 0);
        mixSlider.setBounds (0, 0, 0, 0);
        panSlider.setBounds (0, 0, 0, 0);
        filterBar_.setBounds (0, 0, 0, 0);
        limThresholdSlider.setBounds (0, 0, 0, 0);

        inputSlider.setVisible (false);
        outputSlider.setVisible (false);
        tiltSlider.setVisible (false);
        mixSlider.setVisible (false);
        panSlider.setVisible (false);
        filterBar_.setVisible (false);
        limThresholdSlider.setVisible (false);
        chaosFilterButton.setVisible (false);
        chaosFilterDisplay.setVisible (false);
        chaosDelayButton.setVisible (false);
        chaosDelayDisplay.setVisible (false);
        modeInCombo.setVisible (false);
        modeOutCombo.setVisible (false);
        sumBusCombo.setVisible (false);
        limModeCombo.setVisible (false);

        reverseButton.setVisible (true);
        envGraButton.setVisible (true);
        envGraDisplay.setVisible (true);
        autoButton.setVisible (true);
        triggerButton.setVisible (true);
        syncButton.setVisible (true);
        midiButton.setVisible (true);
        midiChannelDisplay.setVisible (true);
    }

    // Button area: 3x2 grid
    // Row 1: RVS (left) + ENV GRA (right)
    // Row 2: AUTO (left) + TRIGGER (right)
    // Row 3: SYNC (left) + MIDI (right)
    const int buttonAreaX = horizontalLayout.leftX;

    const int toggleVisualSide = juce::jlimit (14,
                                               juce::jmax (14, verticalLayout.box - 2),
                                               (int) std::lround ((double) verticalLayout.box * 0.65));
    const int toggleHitW = toggleVisualSide + 6;

    const int leftBlockX = buttonAreaX;
    const int rightBlockX = horizontalLayout.leftX + horizontalLayout.barW + horizontalLayout.valuePad;

    // Need 3 rows: use pre-computed from buildVerticalLayout
    const int btnRow1Y = verticalLayout.btnRow1Y;
    const int btnRow2Y = verticalLayout.btnRow2Y;
    const int btnRow3Y = verticalLayout.btnRow3Y;

    reverseButton.setBounds (leftBlockX,  btnRow1Y, toggleHitW, verticalLayout.box);
    envGraButton.setBounds  (rightBlockX, btnRow1Y, toggleHitW, verticalLayout.box);
    autoButton.setBounds    (leftBlockX,  btnRow2Y, toggleHitW, verticalLayout.box);
    triggerButton.setBounds (rightBlockX, btnRow2Y, toggleHitW, verticalLayout.box);
    syncButton.setBounds    (leftBlockX,  btnRow3Y, toggleHitW, verticalLayout.box);
    midiButton.setBounds    (rightBlockX, btnRow3Y, toggleHitW, verticalLayout.box);

    // Position invisible tooltip overlays on label areas
    {
        const auto midiLabelRect = getMidiLabelArea();
        midiChannelDisplay.setBounds (midiLabelRect);
    }
    {
        const auto envGraLabelRect = getEnvGraLabelArea();
        envGraDisplay.setBounds (envGraLabelRect);
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
