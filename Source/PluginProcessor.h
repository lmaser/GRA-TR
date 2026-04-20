#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <vector>
#include "PerfTrace.h"

class GRATRAudioProcessor : public juce::AudioProcessor
{
public:
	GRATRAudioProcessor();
	~GRATRAudioProcessor() override;

	// Parameter IDs ------------------------------------------------
	static constexpr const char* kParamTimeMs     = "time_ms";
	static constexpr const char* kParamTimeSync   = "time_sync";
	static constexpr const char* kParamMod        = "mod";
	static constexpr const char* kParamPitch      = "pitch";
	static constexpr const char* kParamFormant    = "formant";
	static constexpr const char* kParamSmooth     = "smooth";
	static constexpr const char* kParamMode       = "mode";       // 0=MONO 1=STEREO 2=WIDE 3=DUAL
	static constexpr const char* kParamInput      = "input";
	static constexpr const char* kParamOutput     = "output";
	static constexpr const char* kParamMix        = "mix";
	static constexpr const char* kParamModeIn     = "mode_in";
	static constexpr const char* kParamModeOut    = "mode_out";
	static constexpr const char* kParamSumBus     = "sum_bus";
	static constexpr const char* kParamLimThreshold = "lim_threshold";
	static constexpr const char* kParamLimMode      = "lim_mode";
	static constexpr const char* kParamInvPol       = "inv_pol";
	static constexpr const char* kParamInvStr       = "inv_str";
	static constexpr const char* kParamSync       = "sync";
	static constexpr const char* kParamMidi       = "midi";
	static constexpr const char* kParamAuto       = "auto_grain";
	static constexpr const char* kParamTrigger    = "trigger";
	static constexpr const char* kParamReverse    = "reverse";

	// Filter parameter IDs
	static constexpr const char* kParamFilterHpFreq  = "filter_hp_freq";
	static constexpr const char* kParamFilterLpFreq  = "filter_lp_freq";
	static constexpr const char* kParamFilterHpSlope = "filter_hp_slope";
	static constexpr const char* kParamFilterLpSlope = "filter_lp_slope";
	static constexpr const char* kParamFilterHpOn    = "filter_hp_on";
	static constexpr const char* kParamFilterLpOn    = "filter_lp_on";

	// Mix Mode + Dry/Wet levels (SEND mode)
	static constexpr const char* kParamMixMode  = "mix_mode";
	static constexpr const char* kParamDryLevel = "dry_level";
	static constexpr const char* kParamWetLevel = "wet_level";

	// Filter position
	static constexpr const char* kParamFilterPos = "filter_pos";

	// Tilt / Pan
	static constexpr const char* kParamTilt = "tilt";
	static constexpr const char* kParamPan  = "pan";

	// Chaos parameter IDs
	static constexpr const char* kParamChaos          = "chaos";
	static constexpr const char* kParamChaosD         = "chaos_d";
	static constexpr const char* kParamChaosAmt       = "chaos_amt";
	static constexpr const char* kParamChaosSpd       = "chaos_spd";
	static constexpr const char* kParamChaosAmtFilter = "chaos_amt_filter";
	static constexpr const char* kParamChaosSpdFilter = "chaos_spd_filter";

	// UI state parameters (hidden from DAW automation)
	// Limiter constants
	static constexpr float kLimThresholdMin     = -36.0f;
	static constexpr float kLimThresholdMax     =   0.0f;
	static constexpr float kLimThresholdDefault =   0.0f;
	static constexpr int   kLimModeDefault      =   0;
	static constexpr int   kMixModeDefault   = 0;   // 0=INSERT, 1=SEND
	static constexpr float kDryLevelDefault  = 0.0f;
	static constexpr float kWetLevelDefault  = 1.0f;
	static constexpr int   kFilterPosDefault = 0;   // 0=POST, 1=PRE

	static constexpr const char* kParamUiWidth    = "ui_width";
	static constexpr const char* kParamUiHeight   = "ui_height";
	static constexpr const char* kParamUiPalette  = "ui_palette";
	static constexpr const char* kParamUiCrt      = "ui_fx_tail";
	static constexpr const char* kParamUiColor0   = "ui_color0";
	static constexpr const char* kParamUiColor1   = "ui_color1";

	// Parameter ranges and defaults --------------------------------
	static constexpr float kTimeMsMin     = 0.01f;
	static constexpr float kTimeMsMax     = 5000.0f;
	static constexpr float kTimeMsMaxSync = 20000.0f;
	static constexpr float kTimeMsDefault = 100.0f;

	static constexpr int kTimeSyncMin     = 0;
	static constexpr int kTimeSyncMax     = 29;
	static constexpr int kTimeSyncDefault = 10;

	static constexpr int   kModeMin     = 0;
	static constexpr int   kModeMax     = 3;    // 0=MONO 1=STEREO 2=WIDE 3=DUAL
	static constexpr float kModeDefault = 1.0f;

	static constexpr float kModMin     = 0.0f;
	static constexpr float kModMax     = 1.0f;
	static constexpr float kModDefault = 0.5f;

	static constexpr float kPitchMin     = -24.0f;
	static constexpr float kPitchMax     =  24.0f;
	static constexpr float kPitchDefault =  0.0f;

static constexpr float kFormantMin     = -24.0f;
static constexpr float kFormantMax     =  24.0f;
	static constexpr float kFormantDefault =  0.0f;

	static constexpr float kSmoothMin     = 0.0f;
	static constexpr float kSmoothMax     = 100.0f;
	static constexpr float kSmoothDefault = 0.0f;

	static constexpr float kInputMin     = -100.0f;
	static constexpr float kInputMax     =  0.0f;
	static constexpr float kInputDefault =  0.0f;

	static constexpr float kOutputMin     = -100.0f;
	static constexpr float kOutputMax     =  24.0f;
	static constexpr float kOutputDefault =  0.0f;

	static constexpr float kMixMin     = 0.0f;
	static constexpr float kMixMax     = 1.0f;
	static constexpr float kMixDefault = 1.0f;

	// Mode In / Mode Out / Sum Bus
	static constexpr int   kModeInOutDefault = 0;   // 0=L+R  1=MID  2=SIDE
	static constexpr int   kSumBusDefault    = 0;   // 0=ST   1=to M   2=to S
	static constexpr int   kInvPolDefault    = 0;   // 0=NONE  1=WET  2=GLOBAL
	static constexpr int   kInvStrDefault    = 0;   // 0=NONE  1=WET  2=GLOBAL
	static constexpr float kSqrt2Over2       = 0.707106781f;

	// Filter
	static constexpr float kFilterFreqMin       = 20.0f;
	static constexpr float kFilterFreqMax       = 20000.0f;
	static constexpr float kFilterHpFreqDefault = 250.0f;
	static constexpr float kFilterLpFreqDefault = 2000.0f;
	static constexpr int   kFilterSlopeMin      = 0;
	static constexpr int   kFilterSlopeMax      = 2;
	static constexpr int   kFilterSlopeDefault  = 1;

	// Tilt
	static constexpr float kTiltMin     = -6.0f;
	static constexpr float kTiltMax     =  6.0f;
	static constexpr float kTiltDefault =  0.0f;

	// Pan
	static constexpr float kPanMin     = 0.0f;
	static constexpr float kPanMax     = 1.0f;
	static constexpr float kPanDefault = 0.5f;

	// Chaos
	static constexpr float kChaosAmtMin     = 0.0f;
	static constexpr float kChaosAmtMax     = 100.0f;
	static constexpr float kChaosAmtDefault = 50.0f;
	static constexpr float kChaosSpdMin     = 0.01f;
	static constexpr float kChaosSpdMax     = 100.0f;
	static constexpr float kChaosSpdDefault = 5.0f;

	// Helpers -------------------------------------------------------
	static juce::StringArray getTimeSyncChoices();
	static juce::String getTimeSyncName (int index);
	float tempoSyncToMs (int syncIndex, double bpm) const;

	static juce::String getMidiNoteName (int midiNote);
	float getCurrentGrainMs() const;
	juce::String getCurrentTimeDisplay() const;

	// AudioProcessor overrides -------------------------------------
	void prepareToPlay (double sampleRate, int samplesPerBlock) override;
	void releaseResources() override;

#if ! JucePlugin_PreferredChannelConfigurations
	bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
#endif

	void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

	juce::AudioProcessorEditor* createEditor() override;
	bool hasEditor() const override;

	const juce::String getName() const override;

	bool acceptsMidi() const override;
	bool producesMidi() const override;
	bool isMidiEffect() const override;
	double getTailLengthSeconds() const override;

	int getNumPrograms() override;
	int getCurrentProgram() override;
	void setCurrentProgram (int index) override;
	const juce::String getProgramName (int index) override;
	void changeProgramName (int index, const juce::String& newName) override;

	void getStateInformation (juce::MemoryBlock& destData) override;
	void setStateInformation (const void* data, int sizeInBytes) override;
	void getCurrentProgramStateInformation (juce::MemoryBlock& destData) override;
	void setCurrentProgramStateInformation (const void* data, int sizeInBytes) override;

	// UI state management ------------------------------------------
	void setUiEditorSize (int width, int height);
	int  getUiEditorWidth() const noexcept;
	int  getUiEditorHeight() const noexcept;

	void setUiUseCustomPalette (bool shouldUseCustomPalette);
	bool getUiUseCustomPalette() const noexcept;

	void setUiCrtEnabled (bool enabled);
	bool getUiCrtEnabled() const noexcept;

	void setMidiChannel (int channel);
	int  getMidiChannel() const noexcept;

	void setUiIoExpanded (bool expanded);
	bool getUiIoExpanded() const noexcept;

	void setUiCustomPaletteColour (int index, juce::Colour colour);
	juce::Colour getUiCustomPaletteColour (int index) const noexcept;

	juce::AudioProcessorValueTreeState apvts;
	static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

	// Wet-signal HP/LP filter biquad structs
	struct WetFilterBiquadCoeffs { float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f; };
	struct WetFilterBiquadState  { float z1 = 0.0f, z2 = 0.0f; };

	PerfTrace perfTrace;

private:
	struct UiStateKeys
	{
		static constexpr const char* editorWidth      = "uiEditorWidth";
		static constexpr const char* editorHeight     = "uiEditorHeight";
		static constexpr const char* useCustomPalette = "uiUseCustomPalette";
		static constexpr const char* crtEnabled       = "uiFxTailEnabled";
		static constexpr const char* midiPort         = "midiPort";
		static constexpr const char* ioExpanded       = "uiIoExpanded";
		static constexpr std::array<const char*, 2> customPalette {
			"uiCustomPalette0", "uiCustomPalette1"
		};
	};

	double currentSampleRate = 44100.0;

	// Grain circular buffer (continuously written) -----------------
	juce::AudioBuffer<float> grainBuffer;
	int  grainBufferLength   = 0;
	int  grainBufferWritePos = 0;

	// Grain voice state (per-channel pair) -------------------------
	// Two voice layers allow crossfade between old and new grain.
	struct GrainVoice
	{
		int   anchorWritePos   = 0;       // write-pos snapshot when grain was captured
		float grainLenSamples  = 0.0f;    // locked grain length in samples
		float readPos          = 0.0f;    // fractional read position within grain
		float fadeGain         = 0.0f;    // crossfade envelope (0->1 fade-in, 1->0 fade-out)
		float pitchRatio       = 1.0f;    // locked pitch ratio at launch time
		bool  active           = false;
		bool  reverse          = false;   // play this grain backwards
	};

	GrainVoice voiceA_[2];     // primary voice per channel
	GrainVoice voiceB_[2];     // crossfade-out voice per channel

	// Auto-trigger phase accumulator (counts samples until next grain)
	float autoPhaseCounter_   = 0.0f;
	float targetGrainLen_     = 0.0f;     // current target grain length in samples
	float smoothedGrainLen_   = 0.0f;     // EMA-smoothed grain length for MIDI glide
	float grainLenGlideStep_  = 1.0f;     // EMA step (1-coeff) for grain length smoothing
	bool  prevTriggerState_   = false;    // edge-detect for TRIGGER toggle
	bool  lastAutoEnabled_    = false;

	// Pitch state --------------------------------------------------
	float currentPitchRatio_  = 1.0f;     // 2^(semitones/12) playback rate
	float smoothedPitchRatio_ = 1.0f;     // EMA-smoothed pitch ratio

	// Formant shift (grain-size-based spectral envelope control) ---
	// Formant in semitones: controls grain capture length.
	// Shorter capture (+st) = brighter, longer capture (-st) = warmer.
	// Read rate stays at pitchRatio, so pitch is independent.
	float currentFormantRatio_ = 1.0f;  // 2^(formantSemitones/12)
	float smoothedFormantRatio_ = 1.0f; // EMA-smoothed formant ratio

	// Per-sample EMA-smoothed parameters ---------------------------
	float smoothedInputGain   = 1.0f;
	float smoothedOutputGain  = 1.0f;
	float smoothedMix         = 0.5f;
	float smoothedDryLevel    = kDryLevelDefault;
	float smoothedWetLevel    = kWetLevelDefault;
	float smoothedPan         = kPanDefault;
	float smoothedLimThreshold = 1.0f;
	bool  filterPre_  = false;
	bool  tiltPre_    = false;
	// Wet-signal HP/LP filter state --------------------------------
	struct WetFilterChannelState
	{
		WetFilterBiquadState hp[2];
		WetFilterBiquadState lp[2];
		void reset() { hp[0] = hp[1] = lp[0] = lp[1] = {}; }
	};
	WetFilterChannelState wetFilterState_[2];
	WetFilterBiquadCoeffs hpCoeffs_[2];
	WetFilterBiquadCoeffs lpCoeffs_[2];
	WetFilterBiquadCoeffs hpCoeffsR_[2];      // per-section HP coeffs (R, stereo chaos)
	WetFilterBiquadCoeffs lpCoeffsR_[2];      // per-section LP coeffs (R, stereo chaos)
	float smoothedFilterHpFreq_ = kFilterHpFreqDefault;
	float smoothedFilterLpFreq_ = kFilterLpFreqDefault;
	float lastCalcHpFreq_ = -1.0f, lastCalcLpFreq_ = -1.0f;
	int   lastCalcHpSlope_ = -1,   lastCalcLpSlope_ = -1;
	int   filterCoeffCountdown_ = 0;
	static constexpr int kFilterCoeffUpdateInterval = 32;
	void updateFilterCoeffs (bool forceHp, bool forceLp);

	bool  wetFilterHpOn_ = false;
	bool  wetFilterLpOn_ = false;
	float wetFilterTargetHpFreq_ = kFilterHpFreqDefault;
	float wetFilterTargetLpFreq_ = kFilterLpFreqDefault;
	int   wetFilterNumSectionsHp_ = 0;
	int   wetFilterNumSectionsLp_ = 0;
	void  filterWetSample (float& wetL, float& wetR);
	void  tiltWetSample   (float& wetL, float& wetR);

	// Tilt filter state -------------------------------------------
	float tiltDb_ = 0.0f;
	float tiltB0_ = 1.0f, tiltB1_ = 0.0f, tiltA1_ = 0.0f;
	float tiltTargetB0_ = 1.0f, tiltTargetB1_ = 0.0f, tiltTargetA1_ = 0.0f;
	float tiltState_[2] = { 0.0f, 0.0f };
	float lastTiltDb_   = 0.0f;
	float tiltSmoothSc_ = 0.0f;

	// Chaos state (Hermite + Drift, per-channel D/G, quadrature F) -
	bool  chaosFilterEnabled_ = false;
	bool  chaosDelayEnabled_  = false;
	bool  chaosStereo_        = false;   // true when mode >= 1 (per-channel D/G)

	// CHS D parameters
	float chaosAmtD_                    = 0.0f;
	float chaosAmtNormD_                = 0.0f;   // cached amtD * 0.01
	float chaosShPeriodD_               = 8820.0f;
	float smoothedChaosShPeriodD_       = 8820.0f;
	float chaosDelayMaxSamples_         = 0.0f;
	float smoothedChaosDelayMaxSamples_ = 0.0f;
	float chaosGainMaxDb_               = 0.0f;
	float smoothedChaosGainMaxDb_       = 0.0f;

	// CHS D Hermite+Drift: delay (per-channel for stereo styles)
	float chaosDPrev_[2]         = {};
	float chaosDCurr_[2]         = {};
	float chaosDNext_[2]         = {};
	float chaosDPhase_[2]        = {};
	float chaosDDriftPhase_[2]   = {};
	float chaosDDriftFreqHz_[2]  = {};
	float chaosDOut_[2]          = {};
	juce::Random chaosDRng_[2];

	// CHS D Hermite+Drift: gain (per-channel, decorrelated)
	float chaosGPrev_[2]         = {};
	float chaosGCurr_[2]         = {};
	float chaosGNext_[2]         = {};
	float chaosGPhase_[2]        = {};
	float chaosGDriftPhase_[2]   = {};
	float chaosGDriftFreqHz_[2]  = {};
	float chaosGOut_[2]          = {};
	juce::Random chaosGRng_[2];

	// CHS F parameters
	float chaosAmtF_                 = 0.0f;
	float chaosShPeriodF_            = 8820.0f;
	float smoothedChaosShPeriodF_    = 8820.0f;
	float chaosFilterMaxOct_         = 0.0f;
	float smoothedChaosFilterMaxOct_ = 0.0f;

	// CHS F Hermite+Drift: filter (mono S&H + quadrature drift)
	float chaosFPrev_            = 0.0f;
	float chaosFCurr_            = 0.0f;
	float chaosFNext_            = 0.0f;
	float chaosFPhase_           = 0.0f;
	float chaosFDriftPhase_      = 0.0f;   // single phase; R = +90 deg offset
	float chaosFDriftFreqHz_     = 0.0f;
	float chaosFOut_[2]          = {};     // [0]=L, [1]=R (quadrature when stereo)
	juce::Random chaosFRng_;

	float chaosParamSmoothCoeff_ = 0.999f;

	// Precomputed sampleRate-dependent smooth coefficients (set in prepareToPlay)
	float cachedChaosParamSmoothCoeff_   = 0.999f;

	static constexpr int kChaosDelayBufLen = 1024;
	float chaosDelayBuf_[2][kChaosDelayBufLen] = {};
	int   chaosDelayWritePos_ = 0;

	static constexpr float kChaosDriftAmp = 0.3f;
	static constexpr float kTwoPi = 6.283185307f;

	// Pan state ----------------------------------------------------

	// Grain smoothing parameters (live) ----------------------------
	float grainSmoothFraction_ = 0.02f;   // fraction of grain used for fade

	// MIDI state ---------------------------------------------------
	std::atomic<int>   lastMidiNote       { -1 };
	std::atomic<int>   lastMidiVelocity   { 0 };
	std::atomic<float> currentMidiFrequency { 0.0f };
	std::atomic<int>   midiChannel        { 0 };

	// UI state atomics ---------------------------------------------
	std::atomic<int> uiEditorWidth  { 360 };
	std::atomic<int> uiEditorHeight { 540 };
	std::atomic<int> uiUseCustomPalette { 0 };
	std::atomic<int> uiCrtEnabled  { 0 };
	std::atomic<juce::uint32> uiCustomPalette[2] {};

	// Raw parameter pointers (cached) ------------------------------
	std::atomic<float>* timeMsParam   = nullptr;
	std::atomic<float>* timeSyncParam = nullptr;
	std::atomic<float>* modParam      = nullptr;
	std::atomic<float>* pitchParam    = nullptr;
	std::atomic<float>* formantParam  = nullptr;
	std::atomic<float>* smoothParam   = nullptr;
	std::atomic<float>* modeParam     = nullptr;
	std::atomic<float>* inputParam    = nullptr;
	std::atomic<float>* outputParam   = nullptr;
	std::atomic<float>* mixParam      = nullptr;
	std::atomic<float>* modeInParam   = nullptr;
	std::atomic<float>* modeOutParam  = nullptr;
	std::atomic<float>* sumBusParam   = nullptr;
	std::atomic<float>* limThresholdParam = nullptr;
	std::atomic<float>* limModeParam     = nullptr;
	std::atomic<float>* invPolParam      = nullptr;
	std::atomic<float>* invStrParam      = nullptr;
	std::atomic<float>* mixModeParam   = nullptr;
	std::atomic<float>* dryLevelParam  = nullptr;
	std::atomic<float>* wetLevelParam  = nullptr;
	std::atomic<float>* filterPosParam = nullptr;
	std::atomic<float>* syncParam     = nullptr;
	std::atomic<float>* midiParam     = nullptr;
	std::atomic<float>* autoParam     = nullptr;
	std::atomic<float>* triggerParam  = nullptr;
	std::atomic<float>* reverseParam  = nullptr;

	std::atomic<float>* filterHpFreqParam  = nullptr;
	std::atomic<float>* filterLpFreqParam  = nullptr;
	std::atomic<float>* filterHpSlopeParam = nullptr;
	std::atomic<float>* filterLpSlopeParam = nullptr;
	std::atomic<float>* filterHpOnParam    = nullptr;
	std::atomic<float>* filterLpOnParam    = nullptr;
	std::atomic<float>* tiltParam    = nullptr;
	std::atomic<float>* panParam     = nullptr;
	std::atomic<float>* chaosParam   = nullptr;
	std::atomic<float>* chaosDelayParam   = nullptr;
	std::atomic<float>* chaosAmtParam     = nullptr;
	std::atomic<float>* chaosSpdParam     = nullptr;
	std::atomic<float>* chaosAmtFilterParam = nullptr;
	std::atomic<float>* chaosSpdFilterParam = nullptr;

	std::atomic<float>* uiWidthParam   = nullptr;
	std::atomic<float>* uiHeightParam  = nullptr;
	std::atomic<float>* uiPaletteParam = nullptr;
	std::atomic<float>* uiCrtParam     = nullptr;
	std::atomic<float>* uiColorParams[2] = { nullptr, nullptr };

	// Inline chaos helpers (Hermite+Drift, consistent with FREQ-TR) -
	// Generic Hermite + Drift chaos engine (per-sample advance)
	inline void advanceChaosEngine (
		float& prev, float& curr, float& next, float& phase,
		float& driftPhase, float& driftFreqHz, float& output,
		juce::Random& rng, float period, float amtNorm, float sr) noexcept
	{
		phase += 1.0f;
		if (phase >= period)
		{
			phase -= period;
			prev = curr;
			curr = next;
			next = rng.nextFloat() * 2.0f - 1.0f;
			const float driftBase = sr / juce::jmax (1.0f, period) * 0.37f;
			driftFreqHz = driftBase * (0.88f + rng.nextFloat() * 0.24f);
		}
		const float t  = phase / period;
		const float t2 = t * t;
		const float t3 = t2 * t;
		const float h00 =  2.0f * t3 - 3.0f * t2 + 1.0f;
		const float h10 =         t3 - 2.0f * t2 + t;
		const float h01 = -2.0f * t3 + 3.0f * t2;
		const float h11 =         t3 -        t2;
		const float tangCurr = (next - prev) * 0.5f;
		const float tangNext = -curr * 0.5f;
		const float shValue  = h00 * curr + h10 * tangCurr + h01 * next + h11 * tangNext;

		driftPhase += driftFreqHz / sr;
		if (driftPhase > 1e6f) driftPhase -= 1e6f;
		const float driftValue = std::sin (driftPhase * kTwoPi) * kChaosDriftAmp;

		const float shWeight = juce::jlimit (0.0f, 1.0f, amtNorm * 1.5f - 0.15f);
		output = driftValue + shValue * shWeight;
	}

	inline void advanceChaosD() noexcept
	{
		smoothedChaosDelayMaxSamples_ += (chaosDelayMaxSamples_ - smoothedChaosDelayMaxSamples_) * (1.0f - chaosParamSmoothCoeff_);
		smoothedChaosGainMaxDb_       += (chaosGainMaxDb_       - smoothedChaosGainMaxDb_)       * (1.0f - chaosParamSmoothCoeff_);
		smoothedChaosShPeriodD_       += (chaosShPeriodD_       - smoothedChaosShPeriodD_)       * (1.0f - chaosParamSmoothCoeff_);

		const float period = smoothedChaosShPeriodD_;
		const float sr = (float) currentSampleRate;
		const int nCh = chaosStereo_ ? 2 : 1;

		for (int c = 0; c < nCh; ++c)
		{
			advanceChaosEngine (chaosDPrev_[c], chaosDCurr_[c], chaosDNext_[c], chaosDPhase_[c],
				chaosDDriftPhase_[c], chaosDDriftFreqHz_[c], chaosDOut_[c],
				chaosDRng_[c], period, chaosAmtNormD_, sr);

			advanceChaosEngine (chaosGPrev_[c], chaosGCurr_[c], chaosGNext_[c], chaosGPhase_[c],
				chaosGDriftPhase_[c], chaosGDriftFreqHz_[c], chaosGOut_[c],
				chaosGRng_[c], period, chaosAmtNormD_, sr);
		}

		if (! chaosStereo_)
		{
			chaosDOut_[1] = chaosDOut_[0];
			chaosGOut_[1] = chaosGOut_[0];
		}
	}

	inline void advanceChaosF() noexcept
	{
		smoothedChaosFilterMaxOct_  += (chaosFilterMaxOct_  - smoothedChaosFilterMaxOct_)  * (1.0f - chaosParamSmoothCoeff_);
		smoothedChaosShPeriodF_     += (chaosShPeriodF_     - smoothedChaosShPeriodF_)     * (1.0f - chaosParamSmoothCoeff_);

		const float amtNormF = chaosAmtF_ * 0.01f;
		const float period   = smoothedChaosShPeriodF_;
		const float sr       = (float) currentSampleRate;

		chaosFPhase_ += 1.0f;
		if (chaosFPhase_ >= period)
		{
			chaosFPhase_ -= period;
			chaosFPrev_ = chaosFCurr_;
			chaosFCurr_ = chaosFNext_;
			chaosFNext_ = chaosFRng_.nextFloat() * 2.0f - 1.0f;
			const float driftBase = sr / juce::jmax (1.0f, period) * 0.37f;
			chaosFDriftFreqHz_ = driftBase * (0.88f + chaosFRng_.nextFloat() * 0.24f);
		}

		const float t  = chaosFPhase_ / period;
		const float t2 = t * t;
		const float t3 = t2 * t;
		const float h00 =  2.0f * t3 - 3.0f * t2 + 1.0f;
		const float h10 =         t3 - 2.0f * t2 + t;
		const float h01 = -2.0f * t3 + 3.0f * t2;
		const float h11 =         t3 -        t2;
		const float tangCurr = (chaosFNext_ - chaosFPrev_) * 0.5f;
		const float tangNext = -chaosFCurr_ * 0.5f;
		const float shValue  = h00 * chaosFCurr_ + h10 * tangCurr + h01 * chaosFNext_ + h11 * tangNext;

		chaosFDriftPhase_ += chaosFDriftFreqHz_ / sr;
		if (chaosFDriftPhase_ > 1e6f) chaosFDriftPhase_ -= 1e6f;
		const float driftL = std::sin (chaosFDriftPhase_ * kTwoPi) * kChaosDriftAmp;

		const float shWeight = juce::jlimit (0.0f, 1.0f, amtNormF * 1.5f - 0.15f);
		chaosFOut_[0] = driftL + shValue * shWeight;

		if (chaosStereo_)
		{
			const float driftR = std::sin (chaosFDriftPhase_ * kTwoPi + kTwoPi * 0.25f) * kChaosDriftAmp;
			chaosFOut_[1] = driftR + shValue * shWeight;
		}
		else
		{
			chaosFOut_[1] = chaosFOut_[0];
		}
	}

	inline void applyChaosDelay (float& wetL, float& wetR) noexcept
	{
		const int wp = chaosDelayWritePos_;
		chaosDelayBuf_[0][wp] = wetL;
		chaosDelayBuf_[1][wp] = wetR;

		const float centerDelay = smoothedChaosDelayMaxSamples_;
		const int mask = kChaosDelayBufLen - 1;

		for (int ch = 0; ch < 2; ++ch)
		{
			const float delaySamp = juce::jlimit (0.0f, (float)(kChaosDelayBufLen - 2),
				centerDelay + chaosDOut_[ch] * smoothedChaosDelayMaxSamples_);
			const float readPos = (float) wp - delaySamp;
			const int iPos = (int) std::floor (readPos);
			const float frac = readPos - (float) iPos;

			const float p0 = chaosDelayBuf_[ch][(iPos - 1) & mask];
			const float p1 = chaosDelayBuf_[ch][(iPos    ) & mask];
			const float p2 = chaosDelayBuf_[ch][(iPos + 1) & mask];
			const float p3 = chaosDelayBuf_[ch][(iPos + 2) & mask];
			const float c0 = p1;
			const float c1 = p2 - (1.0f / 3.0f) * p0 - 0.5f * p1 - (1.0f / 6.0f) * p3;
			const float c2 = 0.5f * (p0 + p2) - p1;
			const float c3 = (1.0f / 6.0f) * (p3 - p0) + 0.5f * (p1 - p2);
			float& wet = (ch == 0) ? wetL : wetR;
			wet = ((c3 * frac + c2) * frac + c1) * frac + c0;
		}

		chaosDelayWritePos_ = (wp + 1) & mask;

		// Per-channel gain modulation
		for (int ch = 0; ch < 2; ++ch)
		{
			const float gainDb  = chaosGOut_[ch] * smoothedChaosGainMaxDb_;
			const float ex = gainDb * 0.16609640474f;
			const float exln2 = ex * 0.6931472f;
			const float gainLin = 1.0f + exln2 * (1.0f + exln2 * 0.5f);
			float& wet = (ch == 0) ? wetL : wetR;
			wet *= gainLin;
		}
	}

	// Limiter state -----------------------------------------------
	static constexpr float kLimFloor = 1.0e-12f;
	float limEnv1_[2] = { kLimFloor, kLimFloor };
	float limEnv2_[2] = { kLimFloor, kLimFloor };
	float limAtt1_  = 0.0f;
	float limRel1_  = 0.0f;
	float limRel2_  = 0.0f;

	inline void applyLimiter (float* leftData, float* rightData, int numSamples,
	                         float thresholdGain) noexcept
	{
		for (int i = 0; i < numSamples; ++i)
		{
			const float peakL = std::abs (leftData[i]);
			const float peakR = std::abs (rightData[i]);

	// Stage 1 - leveler (2 ms attack, 10 ms release)
			for (int ch = 0; ch < 2; ++ch)
			{
				const float p = (ch == 0) ? peakL : peakR;
				if (p > limEnv1_[ch])
					limEnv1_[ch] = limAtt1_ * limEnv1_[ch] + (1.0f - limAtt1_) * p;
				else
					limEnv1_[ch] = limRel1_ * limEnv1_[ch] + (1.0f - limRel1_) * p;
				if (limEnv1_[ch] < kLimFloor) limEnv1_[ch] = kLimFloor;
			}

	// Stage 2 - brickwall (instant attack, 100 ms release)
			for (int ch = 0; ch < 2; ++ch)
			{
				const float p = (ch == 0) ? peakL : peakR;
				if (p > limEnv2_[ch])
					limEnv2_[ch] = p;
				else
					limEnv2_[ch] = limRel2_ * limEnv2_[ch] + (1.0f - limRel2_) * p;
				if (limEnv2_[ch] < kLimFloor) limEnv2_[ch] = kLimFloor;
			}

			// Stereo-linked gain reduction
			float gr = 1.0f;
			const float maxEnv1 = juce::jmax (limEnv1_[0], limEnv1_[1]);
			const float maxEnv2 = juce::jmax (limEnv2_[0], limEnv2_[1]);
			if (maxEnv1 > thresholdGain)
				gr = juce::jmin (gr, thresholdGain / maxEnv1);
			if (maxEnv2 > thresholdGain)
				gr = juce::jmin (gr, thresholdGain / maxEnv2);

			leftData[i]  *= gr;
			rightData[i] *= gr;
		}
	}

	inline void applyLimiterSample (float& sampleL, float& sampleR, float thresholdGain) noexcept
	{
		const float peakL = std::abs (sampleL);
		const float peakR = std::abs (sampleR);

		for (int ch = 0; ch < 2; ++ch)
		{
			const float p = (ch == 0) ? peakL : peakR;
			if (p > limEnv1_[ch])
				limEnv1_[ch] = limAtt1_ * limEnv1_[ch] + (1.0f - limAtt1_) * p;
			else
				limEnv1_[ch] = limRel1_ * limEnv1_[ch] + (1.0f - limRel1_) * p;
			if (limEnv1_[ch] < kLimFloor) limEnv1_[ch] = kLimFloor;
		}

		for (int ch = 0; ch < 2; ++ch)
		{
			const float p = (ch == 0) ? peakL : peakR;
			if (p > limEnv2_[ch])
				limEnv2_[ch] = p;
			else
				limEnv2_[ch] = limRel2_ * limEnv2_[ch] + (1.0f - limRel2_) * p;
			if (limEnv2_[ch] < kLimFloor) limEnv2_[ch] = kLimFloor;
		}

		float gr = 1.0f;
		const float maxEnv1 = juce::jmax (limEnv1_[0], limEnv1_[1]);
		const float maxEnv2 = juce::jmax (limEnv2_[0], limEnv2_[1]);
		if (maxEnv1 > thresholdGain)
			gr = juce::jmin (gr, thresholdGain / maxEnv1);
		if (maxEnv2 > thresholdGain)
			gr = juce::jmin (gr, thresholdGain / maxEnv2);

		sampleL *= gr;
		sampleR *= gr;
	}

	// Grain helpers -----------------------------------------------
	void launchNewGrain (int ch, float grainLenSamples, bool reverseGrain);
	float readGrainInterpolated (const GrainVoice& v, int ch) const;
	float grainEnvelope (const GrainVoice& v) const;

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GRATRAudioProcessor)
};
