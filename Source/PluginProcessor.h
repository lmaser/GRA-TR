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

	// ── Parameter IDs ──────────────────────────────────────────────
	static constexpr const char* kParamTimeMs     = "time_ms";
	static constexpr const char* kParamTimeSync   = "time_sync";
	static constexpr const char* kParamMod        = "mod";
	static constexpr const char* kParamPitch      = "pitch";
	static constexpr const char* kParamFormant    = "formant";
	static constexpr const char* kParamMode       = "mode";       // 0=MONO 1=STEREO 2=WIDE 3=DUAL
	static constexpr const char* kParamInput      = "input";
	static constexpr const char* kParamOutput     = "output";
	static constexpr const char* kParamMix        = "mix";
	static constexpr const char* kParamSync       = "sync";
	static constexpr const char* kParamMidi       = "midi";
	static constexpr const char* kParamAuto       = "auto_grain";
	static constexpr const char* kParamTrigger    = "trigger";
	static constexpr const char* kParamReverse    = "reverse";
	static constexpr const char* kParamEnvGra     = "env_gra";
	static constexpr const char* kParamEnvGraTau  = "env_gra_tau";
	static constexpr const char* kParamEnvGraAmt  = "env_gra_amt";

	// Filter parameter IDs
	static constexpr const char* kParamFilterHpFreq  = "filter_hp_freq";
	static constexpr const char* kParamFilterLpFreq  = "filter_lp_freq";
	static constexpr const char* kParamFilterHpSlope = "filter_hp_slope";
	static constexpr const char* kParamFilterLpSlope = "filter_lp_slope";
	static constexpr const char* kParamFilterHpOn    = "filter_hp_on";
	static constexpr const char* kParamFilterLpOn    = "filter_lp_on";

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
	static constexpr const char* kParamUiWidth    = "ui_width";
	static constexpr const char* kParamUiHeight   = "ui_height";
	static constexpr const char* kParamUiPalette  = "ui_palette";
	static constexpr const char* kParamUiCrt      = "ui_fx_tail";
	static constexpr const char* kParamUiColor0   = "ui_color0";
	static constexpr const char* kParamUiColor1   = "ui_color1";

	// ── Parameter ranges and defaults ──────────────────────────────
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

	static constexpr float kFormantMin     = -12.0f;
	static constexpr float kFormantMax     =  12.0f;
	static constexpr float kFormantDefault =  0.0f;

	static constexpr float kInputMin     = -100.0f;
	static constexpr float kInputMax     =  0.0f;
	static constexpr float kInputDefault =  0.0f;

	static constexpr float kOutputMin     = -100.0f;
	static constexpr float kOutputMax     =  24.0f;
	static constexpr float kOutputDefault =  0.0f;

	static constexpr float kMixMin     = 0.0f;
	static constexpr float kMixMax     = 1.0f;
	static constexpr float kMixDefault = 1.0f;

	static constexpr float kEnvGraTauMin     = 0.0f;
	static constexpr float kEnvGraTauMax     = 100.0f;
	static constexpr float kEnvGraTauDefault = 50.0f;
	static constexpr float kEnvGraAmtMin     = 0.0f;
	static constexpr float kEnvGraAmtMax     = 100.0f;
	static constexpr float kEnvGraAmtDefault = 50.0f;

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

	// ── Helpers ────────────────────────────────────────────────────
	static juce::StringArray getTimeSyncChoices();
	static juce::String getTimeSyncName (int index);
	float tempoSyncToMs (int syncIndex, double bpm) const;

	static juce::String getMidiNoteName (int midiNote);
	float getCurrentGrainMs() const;
	juce::String getCurrentTimeDisplay() const;

	// ── AudioProcessor overrides ──────────────────────────────────
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

	// ── UI state management ───────────────────────────────────────
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

	// ── Grain circular buffer (continuously written) ──────────────
	juce::AudioBuffer<float> grainBuffer;
	int  grainBufferLength   = 0;
	int  grainBufferWritePos = 0;

	// ── Grain voice state (per-channel pair) ──────────────────────
	// Two voice layers allow crossfade between old and new grain.
	struct GrainVoice
	{
		int   anchorWritePos   = 0;       // write-pos snapshot when grain was captured
		float grainLenSamples  = 0.0f;    // locked grain length in samples
		float readPos          = 0.0f;    // fractional read position within grain
		float fadeGain         = 0.0f;    // crossfade envelope (0→1 fade-in, 1→0 fade-out)
		float pitchRatio       = 1.0f;    // locked pitch ratio at launch time
		bool  active           = false;
		bool  reverse          = false;   // play this grain backwards
	};

	GrainVoice voiceA_[2];     // primary voice per channel
	GrainVoice voiceB_[2];     // crossfade-out voice per channel

	// Auto-trigger phase accumulator (counts samples until next grain)
	float autoPhaseCounter_   = 0.0f;
	float targetGrainLen_     = 0.0f;     // current target grain length in samples
	bool  prevTriggerState_   = false;    // edge-detect for TRIGGER toggle
	bool  lastAutoEnabled_    = false;

	// ── Pitch state ───────────────────────────────────────────────
	float currentPitchRatio_  = 1.0f;     // 2^(semitones/12) playback rate
	float smoothedPitchRatio_ = 1.0f;     // EMA-smoothed pitch ratio

	// ── Formant shift (grain-size-based spectral envelope control) ──
	// Formant in semitones: controls grain capture length.
	// Shorter capture (+st) = brighter, longer capture (-st) = warmer.
	// Read rate stays at pitchRatio, so pitch is independent.
	float currentFormantRatio_ = 1.0f;  // 2^(formantSemitones/12)
	float smoothedFormantRatio_ = 1.0f; // EMA-smoothed formant ratio

	// ── Per-sample EMA-smoothed parameters ────────────────────────
	float smoothedInputGain  = 1.0f;
	float smoothedOutputGain = 1.0f;
	float smoothedMix        = 0.5f;
	// ── Wet-signal HP/LP filter state ─────────────────────────────
	struct WetFilterChannelState
	{
		WetFilterBiquadState hp[2];
		WetFilterBiquadState lp[2];
		void reset() { hp[0] = hp[1] = lp[0] = lp[1] = {}; }
	};
	WetFilterChannelState wetFilterState_[2];
	WetFilterBiquadCoeffs hpCoeffs_[2];
	WetFilterBiquadCoeffs lpCoeffs_[2];
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

	// ── Tilt filter state ─────────────────────────────────────────
	float tiltDb_ = 0.0f;
	float tiltB0_ = 1.0f, tiltB1_ = 0.0f, tiltA1_ = 0.0f;
	float tiltTargetB0_ = 1.0f, tiltTargetB1_ = 0.0f, tiltTargetA1_ = 0.0f;
	float tiltState_[2] = { 0.0f, 0.0f };
	float lastTiltDb_   = 0.0f;
	float tiltSmoothSc_ = 0.0f;

	// ── Chaos state (3 S&H engines, same as ECHO-TR) ─────────────
	bool  chaosFilterEnabled_ = false;
	bool  chaosDelayEnabled_  = false;

	float chaosAmtD_                    = 0.0f;
	float chaosShPeriodD_               = 8820.0f;
	float smoothedChaosShPeriodD_       = 8820.0f;
	float chaosDelayMaxSamples_         = 0.0f;
	float smoothedChaosDelayMaxSamples_ = 0.0f;
	float chaosGainMaxDb_               = 0.0f;
	float smoothedChaosGainMaxDb_       = 0.0f;

	float chaosDPhase_ = 0.0f, chaosDTarget_ = 0.0f, chaosDSmoothed_ = 0.0f;
	float chaosDSmoothCoeff_ = 0.999f;
	juce::Random chaosDRng_;

	float chaosGPhase_ = 0.0f, chaosGTarget_ = 0.0f, chaosGSmoothed_ = 0.0f;
	float chaosGSmoothCoeff_ = 0.999f;
	juce::Random chaosGRng_;

	float chaosAmtF_                 = 0.0f;
	float chaosShPeriodF_            = 8820.0f;
	float smoothedChaosShPeriodF_    = 8820.0f;
	float chaosFilterMaxOct_         = 0.0f;
	float smoothedChaosFilterMaxOct_ = 0.0f;

	float chaosFPhase_ = 0.0f, chaosFTarget_ = 0.0f, chaosFSmoothed_ = 0.0f;
	float chaosFSmoothCoeff_ = 0.999f;
	juce::Random chaosFRng_;

	float chaosParamSmoothCoeff_ = 0.999f;

	static constexpr int kChaosDelayBufLen = 1024;
	float chaosDelayBuf_[2][kChaosDelayBufLen] = {};
	int   chaosDelayWritePos_ = 0;

	// ── Pan state ─────────────────────────────────────────────────
	float lastPan_      = 0.5f;
	float lastPanLeft_  = 0.70710678f;
	float lastPanRight_ = 0.70710678f;

	// ── Env Gra crossfade parameters (live) ───────────────────────
	float envGraCrossfadeFraction_ = 0.5f;   // fraction of grain used for fade
	float envGraAmountScaled_      = 0.5f;   // 0-1 depth

	// ── MIDI state ────────────────────────────────────────────────
	std::atomic<int>   lastMidiNote       { -1 };
	std::atomic<int>   lastMidiVelocity   { 0 };
	std::atomic<float> currentMidiFrequency { 0.0f };
	std::atomic<int>   midiChannel        { 0 };

	// ── UI state atomics ──────────────────────────────────────────
	std::atomic<int> uiEditorWidth  { 360 };
	std::atomic<int> uiEditorHeight { 540 };
	std::atomic<int> uiUseCustomPalette { 0 };
	std::atomic<int> uiCrtEnabled  { 0 };
	std::atomic<juce::uint32> uiCustomPalette[2] {};

	// ── Raw parameter pointers (cached) ───────────────────────────
	std::atomic<float>* timeMsParam   = nullptr;
	std::atomic<float>* timeSyncParam = nullptr;
	std::atomic<float>* modParam      = nullptr;
	std::atomic<float>* pitchParam    = nullptr;
	std::atomic<float>* formantParam  = nullptr;
	std::atomic<float>* modeParam     = nullptr;
	std::atomic<float>* inputParam    = nullptr;
	std::atomic<float>* outputParam   = nullptr;
	std::atomic<float>* mixParam      = nullptr;
	std::atomic<float>* syncParam     = nullptr;
	std::atomic<float>* midiParam     = nullptr;
	std::atomic<float>* autoParam     = nullptr;
	std::atomic<float>* triggerParam  = nullptr;
	std::atomic<float>* reverseParam  = nullptr;
	std::atomic<float>* envGraParam   = nullptr;
	std::atomic<float>* envGraTauParam = nullptr;
	std::atomic<float>* envGraAmtParam = nullptr;

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

	// ── Inline chaos helpers (identical to ECHO-TR) ───────────────
	inline void advanceChaosD() noexcept
	{
		smoothedChaosDelayMaxSamples_ += (chaosDelayMaxSamples_ - smoothedChaosDelayMaxSamples_) * (1.0f - chaosParamSmoothCoeff_);
		smoothedChaosGainMaxDb_       += (chaosGainMaxDb_       - smoothedChaosGainMaxDb_)       * (1.0f - chaosParamSmoothCoeff_);
		smoothedChaosShPeriodD_       += (chaosShPeriodD_       - smoothedChaosShPeriodD_)       * (1.0f - chaosParamSmoothCoeff_);

		chaosDPhase_ += 1.0f;
		if (chaosDPhase_ >= smoothedChaosShPeriodD_)
		{
			chaosDPhase_ -= smoothedChaosShPeriodD_;
			chaosDTarget_ = chaosDRng_.nextFloat() * 2.0f - 1.0f;
		}
		chaosDSmoothed_ = chaosDSmoothCoeff_ * chaosDSmoothed_
		                + (1.0f - chaosDSmoothCoeff_) * chaosDTarget_;

		chaosGPhase_ += 1.0f;
		if (chaosGPhase_ >= smoothedChaosShPeriodD_)
		{
			chaosGPhase_ -= smoothedChaosShPeriodD_;
			chaosGTarget_ = chaosGRng_.nextFloat() * 2.0f - 1.0f;
		}
		chaosGSmoothed_ = chaosGSmoothCoeff_ * chaosGSmoothed_
		                + (1.0f - chaosGSmoothCoeff_) * chaosGTarget_;
	}

	inline void advanceChaosF() noexcept
	{
		smoothedChaosFilterMaxOct_ += (chaosFilterMaxOct_ - smoothedChaosFilterMaxOct_) * (1.0f - chaosParamSmoothCoeff_);
		smoothedChaosShPeriodF_    += (chaosShPeriodF_    - smoothedChaosShPeriodF_)    * (1.0f - chaosParamSmoothCoeff_);

		chaosFPhase_ += 1.0f;
		if (chaosFPhase_ >= smoothedChaosShPeriodF_)
		{
			chaosFPhase_ -= smoothedChaosShPeriodF_;
			chaosFTarget_ = chaosFRng_.nextFloat() * 2.0f - 1.0f;
		}
		chaosFSmoothed_ = chaosFSmoothCoeff_ * chaosFSmoothed_
		                + (1.0f - chaosFSmoothCoeff_) * chaosFTarget_;
	}

	inline void applyChaosDelay (float& wetL, float& wetR) noexcept
	{
		const int wp = chaosDelayWritePos_;
		chaosDelayBuf_[0][wp] = wetL;
		chaosDelayBuf_[1][wp] = wetR;

		const float centerDelay = smoothedChaosDelayMaxSamples_;
		const float delaySamp   = juce::jlimit (0.0f, (float)(kChaosDelayBufLen - 2),
		                                        centerDelay + chaosDSmoothed_ * smoothedChaosDelayMaxSamples_);

		const float readPos = (float) wp - delaySamp;
		const int iPos  = (int) std::floor (readPos);
		const float frac = readPos - (float) iPos;
		const int mask = kChaosDelayBufLen - 1;

		for (int ch = 0; ch < 2; ++ch)
		{
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

		const float gainDb  = chaosGSmoothed_ * smoothedChaosGainMaxDb_;
		const float gainLin = std::exp2 (gainDb * 0.16609640474f); // fast dB→linear
		wetL *= gainLin;
		wetR *= gainLin;
	}

	// ── Grain helpers ─────────────────────────────────────────────
	void launchNewGrain (int ch, float grainLenSamples, bool reverseGrain);
	float readGrainInterpolated (const GrainVoice& v, int ch) const;
	float grainEnvelope (const GrainVoice& v) const;

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GRATRAudioProcessor)
};
