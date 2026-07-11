#pragma once

#include <cstdint>
#include <atomic>
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "CrtEffect.h"
#include "../../TR-Shared/SimpleUI/TRSharedUI.h"

class GRATRAudioProcessorEditor  : public juce::AudioProcessorEditor,
                                     public juce::SettableTooltipClient,
                                     private juce::Slider::Listener,
                                     private juce::AudioProcessorValueTreeState::Listener,
                                     private juce::Timer
{
public:
    explicit GRATRAudioProcessorEditor (GRATRAudioProcessor&);
    ~GRATRAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    void moved() override;
    void parentHierarchyChanged() override;


private:
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent& e) override;

    void openNumericEntryPopupForSlider (juce::Slider& s);
    void openMidiChannelPrompt();
    void openAutoDelayPrompt();
    void openTriggerDelayPrompt();
    void openFilterPrompt();
    void openChaosConfigPrompt (const char* amtParamId, const char* spdParamId, const juce::String& title);
    void openChaosFilterPrompt();
    void openChaosDelayPrompt();
    void openMixSendPrompt();
    void openInfoPopup();
    void openGraphicsPopup();
    void setPromptOverlayActive (bool shouldBeActive);

    GRATRAudioProcessor& audioProcessor;

    using BarSlider = TR::SimpleBarSliderDecl;
    using MainGuiToggleButton = TR::MainGuiPromptToggleButton;

    BarSlider timeSlider;
    BarSlider modSlider;
    BarSlider pitchSlider;
    BarSlider scanSlider;
    BarSlider smoothSlider;
    BarSlider jitterSlider;
    BarSlider modeSlider;
    BarSlider inputSlider;
    BarSlider outputSlider;
    BarSlider tiltSlider;
    BarSlider panSlider;
    BarSlider mixSlider;
    BarSlider limThresholdSlider;

    juce::ComboBox modeInCombo;
    juce::ComboBox modeOutCombo;
    juce::ComboBox sumBusCombo;
    juce::ComboBox limModeCombo;
    juce::ComboBox invPolCombo;
    juce::ComboBox invStrCombo;
    juce::ComboBox mixModeCombo;
    juce::ComboBox filterPosCombo;

    MainGuiToggleButton syncButton;
    MainGuiToggleButton midiButton;
    MainGuiToggleButton autoButton;
    MainGuiToggleButton triggerButton;
    MainGuiToggleButton reverseButton;
    MainGuiToggleButton backNForthButton;
    MainGuiToggleButton chaosFilterButton;
    MainGuiToggleButton chaosDelayButton;

    juce::Label autoDisplay;
    juce::Label triggerDisplay;
    juce::Label midiChannelDisplay;
    juce::Label chaosFilterDisplay;
    juce::Label chaosDelayDisplay;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    std::unique_ptr<SliderAttachment> timeAttachment;
    std::unique_ptr<SliderAttachment> timeSyncAttachment;
    std::unique_ptr<SliderAttachment> modAttachment;
    std::unique_ptr<SliderAttachment> pitchAttachment;
    std::unique_ptr<SliderAttachment> scanAttachment;
    std::unique_ptr<SliderAttachment> smoothAttachment;
    std::unique_ptr<SliderAttachment> jitterAttachment;
    std::unique_ptr<SliderAttachment> modeAttachment;
    std::unique_ptr<SliderAttachment> inputAttachment;
    std::unique_ptr<SliderAttachment> outputAttachment;
    std::unique_ptr<SliderAttachment> tiltAttachment;
    std::unique_ptr<SliderAttachment> panAttachment;
    std::unique_ptr<SliderAttachment> mixAttachment;
    std::unique_ptr<SliderAttachment> limThresholdAttachment;

    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    std::unique_ptr<ComboBoxAttachment> modeInAttachment;
    std::unique_ptr<ComboBoxAttachment> modeOutAttachment;
    std::unique_ptr<ComboBoxAttachment> sumBusAttachment;
    std::unique_ptr<ComboBoxAttachment> limModeAttachment;
    std::unique_ptr<ComboBoxAttachment> invPolAttachment;
    std::unique_ptr<ComboBoxAttachment> invStrAttachment;
    std::unique_ptr<ComboBoxAttachment> mixModeAttachment;
    std::unique_ptr<ComboBoxAttachment> filterPosAttachment;

    std::unique_ptr<ButtonAttachment> syncAttachment;
    std::unique_ptr<ButtonAttachment> midiAttachment;
    std::unique_ptr<ButtonAttachment> autoAttachment;
    std::unique_ptr<ButtonAttachment> triggerAttachment;
    std::unique_ptr<ButtonAttachment> reverseAttachment;
    std::unique_ptr<ButtonAttachment> backNForthAttachment;
    std::unique_ptr<ButtonAttachment> chaosFilterAttachment;
    std::unique_ptr<ButtonAttachment> chaosDelayAttachment;

    juce::ComponentBoundsConstrainer resizeConstrainer;
    std::unique_ptr<juce::ResizableCornerComponent> resizerCorner;

    using GRAScheme = TR::TRScheme;

    GRAScheme activeScheme;

    using HorizontalLayoutMetrics = TR::SimpleHorizontalLayoutMetrics;

    using VerticalLayoutMetrics = TR::SimpleVerticalLayoutMetrics;

    static HorizontalLayoutMetrics buildHorizontalLayout (int editorW, int valueColW);
    static VerticalLayoutMetrics buildVerticalLayout (int editorH, int biasY, bool ioExpanded);
    void updateCachedLayout();
    using FilterBarComponent = TR::SimpleFilterBarComponent<GRATRAudioProcessorEditor, GRATRAudioProcessor, GRAScheme>;
    FilterBarComponent filterBar_;
    using DualMixBarComponent = TR::SimpleDualMixBarComponent<GRATRAudioProcessorEditor, GRATRAudioProcessor, GRAScheme>;
    DualMixBarComponent dualMixBar_;
    using MinimalLNF = TR::SimpleLookAndFeel;

    using PromptOverlay = TR::PromptOverlay;

    MinimalLNF lnf;
    std::unique_ptr<juce::TooltipWindow> tooltipWindow;
    PromptOverlay promptOverlay;

    void setupBar (juce::Slider& s);

    juce::String getTimeText() const;
    juce::String getTimeTextShort() const;
    
    juce::String getPitchText() const;
    juce::String getPitchTextShort() const;

    juce::String getScanText() const;
    juce::String getScanTextShort() const;

    juce::String getJitterText() const;
    juce::String getJitterTextShort() const;

    juce::String getSmoothText() const;
    juce::String getSmoothTextShort() const;

    juce::String getModeText() const;
    juce::String getModeTextShort() const;

    juce::String getModText() const;
    juce::String getModTextShort() const;

    juce::String getInputText() const;
    juce::String getInputTextShort() const;

    juce::String getOutputText() const;
    juce::String getOutputTextShort() const;

    juce::String getMixText() const;
    juce::String getMixTextShort() const;

    juce::String getTiltText() const;
    juce::String getTiltTextShort() const;

    juce::String getPanText() const;
    juce::String getPanTextShort() const;

    juce::String getLimThresholdText() const;
    juce::String getLimThresholdTextShort() const;

    int getTargetValueColumnWidth() const;

    void sliderValueChanged (juce::Slider* slider) override;
    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void timerCallback() override;

    void applyPersistedUiStateFromProcessor (bool applySize, bool applyPaletteAndFx);

public:
    void triggerUiRestore() { applyPersistedUiStateFromProcessor (true, true); }

private:
    void applyLabelTextColour (juce::Label& label, juce::Colour colour);
    
    void updateTimeSliderForSyncMode (bool syncEnabled);

    friend class TR::SimpleFilterBarComponent<GRATRAudioProcessorEditor, GRATRAudioProcessor, GRAScheme>;
    friend class TR::SimpleDualMixBarComponent<GRATRAudioProcessorEditor, GRATRAudioProcessor, GRAScheme>;
    friend struct TR::PromptHostBridge;

    juce::Rectangle<int> getValueAreaFor (const juce::Rectangle<int>& barBounds) const;
    juce::Slider* getSliderForValueAreaPoint (juce::Point<int> p);
    juce::Rectangle<int> getSyncLabelArea() const;
    juce::Rectangle<int> getAutoLabelArea() const;
    juce::Rectangle<int> getTriggerLabelArea() const;
    juce::Rectangle<int> getReverseLabelArea() const;
    juce::Rectangle<int> getBackNForthLabelArea() const;
    juce::Rectangle<int> getMidiLabelArea() const;
    juce::Rectangle<int> getChaosLabelArea() const;
    juce::Rectangle<int> getChaosDelayLabelArea() const;
    juce::Rectangle<int> getInfoIconArea() const;
    void updateInfoIconCache();
    bool refreshLegendTextCache();
    TR::SimpleMainPanelSpec buildMainPanelSpec();
    juce::Rectangle<int> getRowRepaintBounds (const juce::Slider& s) const;
    void applyActivePalette();
    void applyCrtState (bool enabled);
    void applyIoFxState (bool enabled);
    void updateIoFxMeterSliders();

    juce::Path cachedInfoGearPath;
    juce::Rectangle<float> cachedInfoGearHole;
    
    juce::String cachedTimeTextFull;
    juce::String cachedTimeTextShort;
    juce::String cachedPitchTextFull;
    juce::String cachedPitchTextShort;
    juce::String cachedScanTextFull;
    juce::String cachedScanTextShort;
    juce::String cachedJitterTextFull;
    juce::String cachedJitterTextShort;
    juce::String cachedSmoothTextFull;
    juce::String cachedSmoothTextShort;
    juce::String cachedModeTextFull;
    juce::String cachedModeTextShort;
    juce::String cachedModTextFull;
    juce::String cachedModTextShort;
    juce::String cachedInputTextFull;
    juce::String cachedInputTextShort;
    juce::String cachedOutputTextFull;
    juce::String cachedOutputTextShort;
    juce::String cachedMixTextFull;
    juce::String cachedMixTextShort;
    juce::String cachedTiltTextFull;
    juce::String cachedTiltTextShort;
    juce::String cachedLimThresholdTextFull;
    juce::String cachedLimThresholdTextShort;
    juce::String cachedLimThresholdIntOnly;

    juce::String cachedTimeIntOnly;
    juce::String cachedPitchIntOnly;
    juce::String cachedScanIntOnly;
    juce::String cachedJitterIntOnly;
    juce::String cachedSmoothIntOnly;
    juce::String cachedModeIntOnly;
    juce::String cachedModIntOnly;
    juce::String cachedInputIntOnly;
    juce::String cachedOutputIntOnly;
    juce::String cachedMixIntOnly;
    juce::String cachedTiltIntOnly;

    juce::String cachedFilterTextFull;
    juce::String cachedFilterTextShort;
    juce::String cachedPanTextFull;
    juce::String cachedPanTextShort;
    juce::String cachedPanIntOnly;
    
    juce::String cachedMidiDisplay;
    bool cachedTimeSliderHeld = false;
    
    mutable std::uint64_t cachedValueColumnWidthKey = 0;
    mutable int cachedValueColumnWidth = 90;

    HorizontalLayoutMetrics cachedHLayout_;
    VerticalLayoutMetrics cachedVLayout_;
    std::array<juce::Rectangle<int>, 12> cachedValueAreas_;
    juce::Rectangle<int> cachedFilterValueArea_;
    juce::Rectangle<int> cachedPanValueArea_;
    juce::Rectangle<int> cachedLimThresholdValueArea_;
    juce::Rectangle<int> cachedTiltValueArea_;
    juce::Rectangle<int> cachedToggleBarArea_;
    juce::Rectangle<int> cachedChaosArea_;
    bool ioSectionExpanded_ = false;

    static constexpr double kDefaultTimeMs = (double) GRATRAudioProcessor::kTimeMsDefault;
    static constexpr double kDefaultJitter = (double) GRATRAudioProcessor::kJitterDefault;
    static constexpr double kDefaultSmooth = (double) GRATRAudioProcessor::kSmoothDefault;
    static constexpr double kDefaultMix = (double) GRATRAudioProcessor::kMixDefault;
    static constexpr double kDefaultInput = (double) GRATRAudioProcessor::kInputDefault;
    static constexpr double kDefaultOutput = (double) GRATRAudioProcessor::kOutputDefault;
    static constexpr double kDefaultTilt = (double) GRATRAudioProcessor::kTiltDefault;
    static constexpr double kDefaultLimThreshold = 0.0;

    static constexpr int kMinW = 360;
    static constexpr int kMinH = 752;
    static constexpr int kMaxW = kMinW + (kMinW / 2);
    static constexpr int kMaxH = kMinH;

    static constexpr int kLayoutVerticalBiasPx = 10;

    bool promptOverlayActive = false;
    bool suppressSizePersistence = false;
    int lastPersistedEditorW = -1;
    int lastPersistedEditorH = -1;
    std::atomic<uint32_t> lastUserInteractionMs { 0 };
    static constexpr uint32_t kUserInteractionPersistWindowMs = 5000;
    bool crtEnabled = false;
    bool ioFxEnabled = true;
    bool useCustomPalette = false;
    double lastInputSignalMs = -10000.0;
    double lastOutputSignalMs = -10000.0;

    // CRT post-process effect (Retro-Windows-Terminal shader on CPU)
    CrtEffect crtEffect;
    float     crtTime = 0.0f;

    std::array<juce::Colour, 4> defaultPalette = TR::defaultSimplePalette();
    std::array<juce::Colour, 4> customPalette = TR::defaultSimpleCustomPalette();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GRATRAudioProcessorEditor)
};
