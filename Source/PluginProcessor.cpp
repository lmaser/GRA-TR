#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
	// Hermite 4-point cubic interpolation --------------------------
	inline float hermite4pt (float ym1, float y0, float y1, float y2, float frac) noexcept
	{
		const float c0 = y0;
		const float c1 = 0.5f * (y1 - ym1);
		const float c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
		const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
		return ((c3 * frac + c2) * frac + c1) * frac + c0;
	}

	// Gain / mix EMA coefficient: one-pole ~5 ms time constant at 44.1 kHz
	constexpr float kGainSmoothCoeff = 0.9955f;
	constexpr float kGainSmoothStep  = 1.0f - kGainSmoothCoeff;

	// Minimum grain length in samples to avoid ultra-short grains that produce clicks
	constexpr float kMinGrainSamples = 4.0f;

	inline float loadAtomicOrDefault (std::atomic<float>* p, float def) noexcept
	{
		return p != nullptr ? p->load (std::memory_order_relaxed) : def;
	}

	inline int loadIntParamOrDefault (std::atomic<float>* p, int def) noexcept
	{
		return (int) std::lround (loadAtomicOrDefault (p, (float) def));
	}

	inline bool loadBoolParamOrDefault (std::atomic<float>* p, bool def) noexcept
	{
		return loadAtomicOrDefault (p, def ? 1.0f : 0.0f) > 0.5f;
	}

	inline void setParameterPlainValue (juce::AudioProcessorValueTreeState& apvts,
	                                    const char* paramId, float plainValue)
	{
		if (auto* param = apvts.getParameter (paramId))
		{
			const float norm = param->convertTo0to1 (plainValue);
			param->setValueNotifyingHost (norm);
		}
	}

	inline float fastDecibelsToGain (float dB) noexcept
	{
		return (dB <= -100.0f) ? 0.0f : std::exp2 (dB * 0.16609640474f);
	}

	// Precomputed Tukey taper for grain envelope -------------------
	constexpr int kTaperTableSize = 129;
	struct TaperTable
	{
		float data[kTaperTableSize];
		constexpr TaperTable() : data{}
		{
			for (int i = 0; i < kTaperTableSize; ++i)
			{
				const double t = 3.14159265358979323846 * static_cast<double>(i) / 128.0;
				const double t2 = t * t;
				const double t4 = t2 * t2;
				const double t6 = t4 * t2;
				const double t8 = t6 * t2;
				const double t10 = t8 * t2;
				const double cosVal = 1.0 - t2/2.0 + t4/24.0 - t6/720.0 + t8/40320.0 - t10/3628800.0;
				data[i] = static_cast<float>(0.5 * (1.0 - cosVal));
			}
		}
	};
	constexpr TaperTable kTaperTable {};

	inline float taperWeight (float pos, float taperLen) noexcept
	{
		if (pos <= 0.0f) return 0.0f;
		const float norm = pos * (128.0f / taperLen);
		const int idx = static_cast<int>(norm);
		if (idx >= 128) return 1.0f;
		const float frac = norm - static_cast<float>(idx);
		return kTaperTable.data[idx] + frac * (kTaperTable.data[idx + 1] - kTaperTable.data[idx]);
	}

	// Wet-signal biquad filter helpers -----------------------------
	using BQC = GRATRAudioProcessor::WetFilterBiquadCoeffs;

	constexpr float kBW4_Q1 = 0.54119610f;
	constexpr float kBW4_Q2 = 1.30656296f;
	constexpr float kBW2_Q  = 0.70710678f;

	inline BQC calcOnePoleLP (float fc, float sr)
	{
		const float w = std::tan (juce::MathConstants<float>::pi * juce::jlimit (1.0f, sr * 0.499f, fc) / sr);
		BQC c; c.b0 = w / (1.0f + w); c.b1 = c.b0; c.b2 = 0.0f;
		c.a1 = (w - 1.0f) / (1.0f + w); c.a2 = 0.0f; return c;
	}
	inline BQC calcOnePoleHP (float fc, float sr)
	{
		const float w = std::tan (juce::MathConstants<float>::pi * juce::jlimit (1.0f, sr * 0.499f, fc) / sr);
		BQC c; c.b0 = 1.0f / (1.0f + w); c.b1 = -c.b0; c.b2 = 0.0f;
		c.a1 = (w - 1.0f) / (1.0f + w); c.a2 = 0.0f; return c;
	}
	inline BQC calcBiquadLP (float fc, float sr, float Q)
	{
		const float w0 = 2.0f * juce::MathConstants<float>::pi * juce::jlimit (1.0f, sr * 0.499f, fc) / sr;
		const float cosw = std::cos (w0), sinw = std::sin (w0);
		const float alpha = sinw / (2.0f * Q), a0inv = 1.0f / (1.0f + alpha);
		BQC c; c.b0 = ((1.0f - cosw) * 0.5f) * a0inv; c.b1 = (1.0f - cosw) * a0inv;
		c.b2 = c.b0; c.a1 = (-2.0f * cosw) * a0inv; c.a2 = (1.0f - alpha) * a0inv; return c;
	}
	inline BQC calcBiquadHP (float fc, float sr, float Q)
	{
		const float w0 = 2.0f * juce::MathConstants<float>::pi * juce::jlimit (1.0f, sr * 0.499f, fc) / sr;
		const float cosw = std::cos (w0), sinw = std::sin (w0);
		const float alpha = sinw / (2.0f * Q), a0inv = 1.0f / (1.0f + alpha);
		BQC c; c.b0 = ((1.0f + cosw) * 0.5f) * a0inv; c.b1 = (-(1.0f + cosw)) * a0inv;
		c.b2 = c.b0; c.a1 = (-2.0f * cosw) * a0inv; c.a2 = (1.0f - alpha) * a0inv; return c;
	}

	inline float processBiquad (float in, const BQC& c,
	                            GRATRAudioProcessor::WetFilterBiquadState& s) noexcept
	{
		const float out = c.b0 * in + s.z1;
		s.z1 = c.b1 * in - c.a1 * out + s.z2;
		s.z2 = c.b2 * in - c.a2 * out;
		return out;
	}
}

//==============================================================================
GRATRAudioProcessor::GRATRAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
	: AudioProcessor (BusesProperties()
	                 #if ! JucePlugin_IsMidiEffect
	                  #if ! JucePlugin_IsSynth
	                   .withInput  ("Input", juce::AudioChannelSet::stereo(), true)
	                  #endif
	                   .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
	                 #endif
	                   )
#endif
	, apvts (*this, nullptr, "Parameters", createParameterLayout())
{
	timeMsParam   = apvts.getRawParameterValue (kParamTimeMs);
	timeSyncParam = apvts.getRawParameterValue (kParamTimeSync);
	modParam      = apvts.getRawParameterValue (kParamMod);
	pitchParam    = apvts.getRawParameterValue (kParamPitch);
	formantParam  = apvts.getRawParameterValue (kParamFormant);
	smoothParam   = apvts.getRawParameterValue (kParamSmooth);
	modeParam     = apvts.getRawParameterValue (kParamMode);
	inputParam    = apvts.getRawParameterValue (kParamInput);
	outputParam   = apvts.getRawParameterValue (kParamOutput);
	mixParam      = apvts.getRawParameterValue (kParamMix);
	modeInParam   = apvts.getRawParameterValue (kParamModeIn);
	modeOutParam  = apvts.getRawParameterValue (kParamModeOut);
	sumBusParam   = apvts.getRawParameterValue (kParamSumBus);
	limThresholdParam = apvts.getRawParameterValue (kParamLimThreshold);
	limModeParam      = apvts.getRawParameterValue (kParamLimMode);
	invPolParam       = apvts.getRawParameterValue (kParamInvPol);
	invStrParam       = apvts.getRawParameterValue (kParamInvStr);
	mixModeParam   = apvts.getRawParameterValue (kParamMixMode);
	dryLevelParam  = apvts.getRawParameterValue (kParamDryLevel);
	wetLevelParam  = apvts.getRawParameterValue (kParamWetLevel);
	filterPosParam = apvts.getRawParameterValue (kParamFilterPos);
	syncParam     = apvts.getRawParameterValue (kParamSync);
	midiParam     = apvts.getRawParameterValue (kParamMidi);
	autoParam     = apvts.getRawParameterValue (kParamAuto);
	triggerParam  = apvts.getRawParameterValue (kParamTrigger);
	reverseParam  = apvts.getRawParameterValue (kParamReverse);

	filterHpFreqParam  = apvts.getRawParameterValue (kParamFilterHpFreq);
	filterLpFreqParam  = apvts.getRawParameterValue (kParamFilterLpFreq);
	filterHpSlopeParam = apvts.getRawParameterValue (kParamFilterHpSlope);
	filterLpSlopeParam = apvts.getRawParameterValue (kParamFilterLpSlope);
	filterHpOnParam    = apvts.getRawParameterValue (kParamFilterHpOn);
	filterLpOnParam    = apvts.getRawParameterValue (kParamFilterLpOn);
	tiltParam          = apvts.getRawParameterValue (kParamTilt);
	panParam           = apvts.getRawParameterValue (kParamPan);
	chaosParam         = apvts.getRawParameterValue (kParamChaos);
	chaosDelayParam    = apvts.getRawParameterValue (kParamChaosD);
	chaosAmtParam      = apvts.getRawParameterValue (kParamChaosAmt);
	chaosSpdParam      = apvts.getRawParameterValue (kParamChaosSpd);
	chaosAmtFilterParam = apvts.getRawParameterValue (kParamChaosAmtFilter);
	chaosSpdFilterParam = apvts.getRawParameterValue (kParamChaosSpdFilter);

	uiWidthParam   = apvts.getRawParameterValue (kParamUiWidth);
	uiHeightParam  = apvts.getRawParameterValue (kParamUiHeight);
	uiPaletteParam = apvts.getRawParameterValue (kParamUiPalette);
	uiCrtParam     = apvts.getRawParameterValue (kParamUiCrt);
	uiColorParams[0] = apvts.getRawParameterValue (kParamUiColor0);
	uiColorParams[1] = apvts.getRawParameterValue (kParamUiColor1);

	const int w = loadIntParamOrDefault (uiWidthParam, 360);
	const int h = loadIntParamOrDefault (uiHeightParam, 480);
	uiEditorWidth.store (w, std::memory_order_relaxed);
	uiEditorHeight.store (h, std::memory_order_relaxed);

	perfTrace.enableDesktopAutoDump();
}

GRATRAudioProcessor::~GRATRAudioProcessor() {}

//==============================================================================
const juce::String GRATRAudioProcessor::getName() const   { return JucePlugin_Name; }

bool GRATRAudioProcessor::acceptsMidi() const
{
#if JucePlugin_WantsMidiInput
	return true;
#else
	return false;
#endif
}

bool GRATRAudioProcessor::producesMidi() const
{
#if JucePlugin_ProducesMidiOutput
	return true;
#else
	return false;
#endif
}

bool GRATRAudioProcessor::isMidiEffect() const
{
#if JucePlugin_IsMidiEffect
	return true;
#else
	return false;
#endif
}

double GRATRAudioProcessor::getTailLengthSeconds() const
{
	return 0.5;
}

int GRATRAudioProcessor::getNumPrograms()    { return 1; }
int GRATRAudioProcessor::getCurrentProgram() { return 0; }
void GRATRAudioProcessor::setCurrentProgram (int index) { juce::ignoreUnused (index); }
const juce::String GRATRAudioProcessor::getProgramName (int index) { juce::ignoreUnused (index); return {}; }
void GRATRAudioProcessor::changeProgramName (int index, const juce::String& newName) { juce::ignoreUnused (index, newName); }

//==============================================================================
void GRATRAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
	juce::ignoreUnused (samplesPerBlock);
	currentSampleRate = sampleRate;

	// Allocate grain buffer (power of 2, enough for max grain time)
	const int requestedSamples = (int) std::ceil (sampleRate * (kTimeMsMaxSync / 1000.0)) + 1024;
	int powerOf2 = 1;
	while (powerOf2 < requestedSamples)
		powerOf2 <<= 1;

	grainBufferLength = powerOf2;
	grainBuffer.setSize (2, grainBufferLength);
	grainBuffer.clear();
	grainBufferWritePos = 0;

	// Reset grain voices
	for (int ch = 0; ch < 2; ++ch)
	{
		voiceA_[ch] = {};
		voiceB_[ch] = {};
	}
	autoPhaseCounter_ = 0.0f;
	targetGrainLen_   = 0.0f;
	smoothedGrainLen_ = 0.0f;
	grainLenGlideStep_ = 1.0f;
	prevTriggerState_ = false;
	lastAutoEnabled_  = false;

	currentPitchRatio_ = 1.0f;
	currentFormantRatio_ = 1.0f;
	smoothedPitchRatio_ = 1.0f;
	smoothedFormantRatio_ = 1.0f;

	smoothedInputGain = fastDecibelsToGain (loadAtomicOrDefault (inputParam, kInputDefault));
	smoothedOutputGain = fastDecibelsToGain (loadAtomicOrDefault (outputParam, kOutputDefault));
	smoothedMix = juce::jlimit (0.0f, 1.0f, loadAtomicOrDefault (mixParam, kMixDefault));
	smoothedDryLevel = juce::jlimit (0.0f, 1.0f, loadAtomicOrDefault (dryLevelParam, kDryLevelDefault));
	smoothedWetLevel = juce::jlimit (0.0f, 1.0f, loadAtomicOrDefault (wetLevelParam, kWetLevelDefault));
	smoothedPan = juce::jlimit (kPanMin, kPanMax, loadAtomicOrDefault (panParam, kPanDefault));
	smoothedLimThreshold = fastDecibelsToGain (juce::jlimit (kLimThresholdMin, kLimThresholdMax,
		loadAtomicOrDefault (limThresholdParam, kLimThresholdDefault)));

	// Reset wet-signal filter state
	wetFilterState_[0].reset();
	wetFilterState_[1].reset();
	smoothedFilterHpFreq_ = loadAtomicOrDefault (filterHpFreqParam, kFilterHpFreqDefault);
	smoothedFilterLpFreq_ = loadAtomicOrDefault (filterLpFreqParam, kFilterLpFreqDefault);
	lastCalcHpFreq_ = -1.0f; lastCalcLpFreq_ = -1.0f;
	lastCalcHpSlope_ = -1;   lastCalcLpSlope_ = -1;
	filterCoeffCountdown_ = 0;
	updateFilterCoeffs (true, true);

	// Reset tilt state
	tiltDb_ = 0.0f;
	tiltB0_ = 1.0f; tiltB1_ = 0.0f; tiltA1_ = 0.0f;
	tiltTargetB0_ = 1.0f; tiltTargetB1_ = 0.0f; tiltTargetA1_ = 0.0f;
	tiltState_[0] = tiltState_[1] = 0.0f;
	lastTiltDb_ = 0.0f;
	tiltSmoothSc_ = 1.0f - std::exp (-1.0f / (static_cast<float> (currentSampleRate) * 0.03f));

	// Reset chaos state
	chaosFilterEnabled_ = false;
	chaosDelayEnabled_  = false;
	chaosStereo_ = false;
	chaosAmtD_ = 0.0f; chaosAmtNormD_ = 0.0f; chaosAmtF_ = 0.0f;
	chaosShPeriodD_ = 8820.0f; smoothedChaosShPeriodD_ = 8820.0f;
	chaosShPeriodF_ = 8820.0f; smoothedChaosShPeriodF_ = 8820.0f;
	chaosDelayMaxSamples_ = 0.0f; smoothedChaosDelayMaxSamples_ = 0.0f;
	chaosGainMaxDb_ = 0.0f; smoothedChaosGainMaxDb_ = 0.0f;
	chaosFilterMaxOct_ = 0.0f; smoothedChaosFilterMaxOct_ = 0.0f;
	for (int c = 0; c < 2; ++c)
	{
		chaosDPrev_[c] = chaosDCurr_[c] = chaosDNext_[c] = 0.0f;
		chaosDPhase_[c] = 0.0f; chaosDDriftPhase_[c] = 0.0f; chaosDDriftFreqHz_[c] = 0.0f; chaosDOut_[c] = 0.0f;
		chaosGPrev_[c] = chaosGCurr_[c] = chaosGNext_[c] = 0.0f;
		chaosGPhase_[c] = 0.0f; chaosGDriftPhase_[c] = 0.0f; chaosGDriftFreqHz_[c] = 0.0f; chaosGOut_[c] = 0.0f;
	}
	chaosFPrev_ = chaosFCurr_ = chaosFNext_ = 0.0f;
	chaosFPhase_ = 0.0f; chaosFDriftPhase_ = 0.0f; chaosFDriftFreqHz_ = 0.0f;
	chaosFOut_[0] = chaosFOut_[1] = 0.0f;
	std::memset (chaosDelayBuf_, 0, sizeof (chaosDelayBuf_));
	chaosDelayWritePos_ = 0;

	// Precompute sampleRate-dependent smooth coefficients
	cachedChaosParamSmoothCoeff_ = std::exp (-1.0f / ((float) currentSampleRate * 0.010f));

	// Limiter state reset
	limEnv1_[0] = limEnv1_[1] = kLimFloor;
	limEnv2_[0] = limEnv2_[1] = kLimFloor;
	{
		const float sr = static_cast<float> (currentSampleRate);
		limAtt1_ = std::exp (-1.0f / (sr * 0.002f));   // 2 ms attack
		limRel1_ = std::exp (-1.0f / (sr * 0.010f));   // 10 ms release
		limRel2_ = std::exp (-1.0f / (sr * 0.100f));   // 100 ms release
	}
}

void GRATRAudioProcessor::releaseResources()
{
	grainBuffer.setSize (0, 0);
	grainBufferLength   = 0;
	grainBufferWritePos = 0;
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool GRATRAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
	juce::ignoreUnused (layouts);
	return true;
  #else
	if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
	 && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
		return false;
   #if ! JucePlugin_IsSynth
	if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
		return false;
   #endif
	return true;
  #endif
}
#endif

//==============================================================================
// Wet-signal HP/LP filter coefficient update (identical to ECHO-TR)

void GRATRAudioProcessor::updateFilterCoeffs (bool forceHp, bool forceLp)
{
	const float sr = (float) currentSampleRate;
	const int hpSlope = juce::roundToInt (loadAtomicOrDefault (filterHpSlopeParam, (float) kFilterSlopeDefault));
	const int lpSlope = juce::roundToInt (loadAtomicOrDefault (filterLpSlopeParam, (float) kFilterSlopeDefault));

	if (forceHp || hpSlope != lastCalcHpSlope_ || std::abs (smoothedFilterHpFreq_ - lastCalcHpFreq_) > 0.01f)
	{
		lastCalcHpFreq_ = smoothedFilterHpFreq_;
		lastCalcHpSlope_ = hpSlope;
		if (hpSlope == 0)      { hpCoeffs_[0] = calcOnePoleHP (smoothedFilterHpFreq_, sr); hpCoeffs_[1] = {}; }
		else if (hpSlope == 1) { hpCoeffs_[0] = calcBiquadHP  (smoothedFilterHpFreq_, sr, kBW2_Q);  hpCoeffs_[1] = {}; }
		else                   { hpCoeffs_[0] = calcBiquadHP  (smoothedFilterHpFreq_, sr, kBW4_Q1); hpCoeffs_[1] = calcBiquadHP (smoothedFilterHpFreq_, sr, kBW4_Q2); }
	}

	if (forceLp || lpSlope != lastCalcLpSlope_ || std::abs (smoothedFilterLpFreq_ - lastCalcLpFreq_) > 0.01f)
	{
		lastCalcLpFreq_ = smoothedFilterLpFreq_;
		lastCalcLpSlope_ = lpSlope;
		if (lpSlope == 0)      { lpCoeffs_[0] = calcOnePoleLP (smoothedFilterLpFreq_, sr); lpCoeffs_[1] = {}; }
		else if (lpSlope == 1) { lpCoeffs_[0] = calcBiquadLP  (smoothedFilterLpFreq_, sr, kBW2_Q);  lpCoeffs_[1] = {}; }
		else                   { lpCoeffs_[0] = calcBiquadLP  (smoothedFilterLpFreq_, sr, kBW4_Q1); lpCoeffs_[1] = calcBiquadLP (smoothedFilterLpFreq_, sr, kBW4_Q2); }
	}
}

void GRATRAudioProcessor::filterWetSample (float& wetL, float& wetR)
{
	float hpTarget = wetFilterTargetHpFreq_;
	float lpTarget = wetFilterTargetLpFreq_;

	// EMA frequency smoothing (base, no chaos)
	smoothedFilterHpFreq_ += (hpTarget - smoothedFilterHpFreq_) * kGainSmoothStep;
	smoothedFilterLpFreq_ += (lpTarget - smoothedFilterLpFreq_) * kGainSmoothStep;

	// Batched coefficient update (with per-channel chaos overlay)
	if (--filterCoeffCountdown_ <= 0)
	{
		filterCoeffCountdown_ = kFilterCoeffUpdateInterval;
		const bool chaosFilterActive = chaosFilterEnabled_ && chaosAmtF_ > 0.01f;
		if (chaosFilterActive)
		{
			const float sHp = smoothedFilterHpFreq_;
			const float sLp = smoothedFilterLpFreq_;

			// L channel coefficients
			const float octL = chaosFOut_[0] * smoothedChaosFilterMaxOct_;
			const float freqMultL = std::exp2 (octL);
			const float hpBaseL = wetFilterHpOn_ ? sHp : kFilterFreqMin;
			const float lpBaseL = wetFilterLpOn_ ? sLp : kFilterFreqMax;
			smoothedFilterHpFreq_ = juce::jlimit (kFilterFreqMin, kFilterFreqMax, hpBaseL * freqMultL);
			smoothedFilterLpFreq_ = juce::jlimit (kFilterFreqMin, kFilterFreqMax, lpBaseL * freqMultL);
			updateFilterCoeffs (true, true);

			if (chaosStereo_)
			{
				auto hpL0 = hpCoeffs_[0]; auto hpL1 = hpCoeffs_[1];
				auto lpL0 = lpCoeffs_[0]; auto lpL1 = lpCoeffs_[1];

				const float octR = chaosFOut_[1] * smoothedChaosFilterMaxOct_;
				const float freqMultR = std::exp2 (octR);
				smoothedFilterHpFreq_ = juce::jlimit (kFilterFreqMin, kFilterFreqMax, hpBaseL * freqMultR);
				smoothedFilterLpFreq_ = juce::jlimit (kFilterFreqMin, kFilterFreqMax, lpBaseL * freqMultR);
				updateFilterCoeffs (true, true);

				hpCoeffsR_[0] = hpCoeffs_[0]; hpCoeffsR_[1] = hpCoeffs_[1];
				lpCoeffsR_[0] = lpCoeffs_[0]; lpCoeffsR_[1] = lpCoeffs_[1];
				hpCoeffs_[0] = hpL0; hpCoeffs_[1] = hpL1;
				lpCoeffs_[0] = lpL0; lpCoeffs_[1] = lpL1;
			}
			else
			{
				hpCoeffsR_[0] = hpCoeffs_[0]; hpCoeffsR_[1] = hpCoeffs_[1];
				lpCoeffsR_[0] = lpCoeffs_[0]; lpCoeffsR_[1] = lpCoeffs_[1];
			}

			smoothedFilterHpFreq_ = sHp;
			smoothedFilterLpFreq_ = sLp;
		}
		else
		{
			updateFilterCoeffs (false, false);
			hpCoeffsR_[0] = hpCoeffs_[0]; hpCoeffsR_[1] = hpCoeffs_[1];
			lpCoeffsR_[0] = lpCoeffs_[0]; lpCoeffsR_[1] = lpCoeffs_[1];
		}
	}

	const bool chaosFilterActive = chaosFilterEnabled_ && chaosAmtF_ > 0.01f;
	if (wetFilterHpOn_ || chaosFilterActive)
	{
		for (int s = 0; s < wetFilterNumSectionsHp_; ++s)
		{
			wetL = processBiquad (wetL, hpCoeffs_[s], wetFilterState_[0].hp[s]);
			wetR = processBiquad (wetR, hpCoeffsR_[s], wetFilterState_[1].hp[s]);
		}
	}

	if (wetFilterLpOn_ || chaosFilterActive)
	{
		for (int s = 0; s < wetFilterNumSectionsLp_; ++s)
		{
			wetL = processBiquad (wetL, lpCoeffs_[s], wetFilterState_[0].lp[s]);
			wetR = processBiquad (wetR, lpCoeffsR_[s], wetFilterState_[1].lp[s]);
		}
	}

	// TILT filter - now handled by tiltWetSample() -----------------
}

void GRATRAudioProcessor::tiltWetSample (float& wetL, float& wetR)
{
	if (std::abs (tiltDb_) > 0.05f)
	{
		if (std::abs (tiltDb_ - lastTiltDb_) > 0.02f)
		{
			lastTiltDb_ = tiltDb_;
			const double pivot = 1000.0;
			const double octToNy = std::log2 ((currentSampleRate * 0.5) / pivot);
			const double gainNyDb = static_cast<double> (tiltDb_) * octToNy;
			const double gNy = std::pow (10.0, gainNyDb / 20.0);
			const double wc = 2.0 * currentSampleRate
			                * std::tan (juce::MathConstants<double>::pi * pivot / currentSampleRate);
			const double K = wc / (2.0 * currentSampleRate);
			const double g = std::sqrt (gNy);
			const double norm = 1.0 / (1.0 + K * g);
			tiltTargetB0_ = static_cast<float> ((g + K) * norm);
			tiltTargetB1_ = static_cast<float> ((K - g) * norm);
			tiltTargetA1_ = static_cast<float> ((K * g - 1.0) * norm);
		}

		const float sc = tiltSmoothSc_;
		tiltB0_ += (tiltTargetB0_ - tiltB0_) * sc;
		tiltB1_ += (tiltTargetB1_ - tiltB1_) * sc;
		tiltA1_ += (tiltTargetA1_ - tiltA1_) * sc;

		{ const float x = wetL; const float y = tiltB0_ * x + tiltState_[0]; tiltState_[0] = tiltB1_ * x - tiltA1_ * y; wetL = y; }
		{ const float x = wetR; const float y = tiltB0_ * x + tiltState_[1]; tiltState_[1] = tiltB1_ * x - tiltA1_ * y; wetR = y; }
	}
	else if (std::abs (lastTiltDb_) > 0.05f)
	{
		lastTiltDb_ = 0.0f;
		tiltB0_ = 1.0f; tiltB1_ = 0.0f; tiltA1_ = 0.0f;
		tiltTargetB0_ = 1.0f; tiltTargetB1_ = 0.0f; tiltTargetA1_ = 0.0f;
		tiltState_[0] = tiltState_[1] = 0.0f;
	}
}

//==============================================================================
// Formant control (grain-size-based spectral character shift)
//
// In granular synthesis, shorter grains contain fewer pitch periods, causing the
// spectral centroid to shift upward (brighter).  Longer grains capture more
// periods, producing a warmer sound.  The formant parameter scales the grain
// capture length by 1/formantRatio without changing the read rate or the
// auto-retrigger period, so pitch remains controlled solely by pitchRatio while
// the spectral character shifts.
//
// This is the approach used by hardware granular synths (e.g. Tasty Chips GR-1).
//

//==============================================================================
// Grain helpers

void GRATRAudioProcessor::launchNewGrain (int ch, float grainLenSamples, bool reverseGrain)
{
	const int wrapMask = grainBufferLength - 1;

	// Cross-fade: move current voice A to voice B (fade-out)
	voiceB_[ch] = voiceA_[ch];
	if (voiceB_[ch].active)
		voiceB_[ch].fadeGain = voiceA_[ch].fadeGain; // will fade out

	// Setup new voice A (fade-in)
	GrainVoice& v = voiceA_[ch];
	v.grainLenSamples = grainLenSamples;
	v.active = true;
	v.reverse = reverseGrain;
	v.pitchRatio = smoothedPitchRatio_;

	// Anchor: start reading from grainLen samples before current writePos
	v.anchorWritePos = (grainBufferWritePos - (int) grainLenSamples) & wrapMask;

	// Read position always starts at 0; reverse mapping is handled in readGrainInterpolated
	v.readPos = 0.0f;
	v.fadeGain = 0.0f;  // will fade in
}

float GRATRAudioProcessor::readGrainInterpolated (const GrainVoice& v, int ch) const
{
	if (!v.active || v.grainLenSamples < 1.0f)
		return 0.0f;

	const int wrapMask = grainBufferLength - 1;
	const auto* buf = grainBuffer.getReadPointer (ch);

	// Map readPos within grain to buffer position
	const float bufPos = (float) v.anchorWritePos + (v.reverse
		? (v.grainLenSamples - v.readPos)
		: v.readPos);

	const int idx0  = ((int) bufPos) & wrapMask;
	const int idxM1 = (idx0 + wrapMask) & wrapMask;
	const int idx1  = (idx0 + 1) & wrapMask;
	const int idx2  = (idx0 + 2) & wrapMask;
	const float frac = bufPos - std::floor (bufPos);

	return hermite4pt (buf[idxM1], buf[idx0], buf[idx1], buf[idx2], frac);
}

float GRATRAudioProcessor::grainEnvelope (const GrainVoice& v) const
{
	if (!v.active || v.grainLenSamples < 2.0f)
		return 0.0f;

	// Guard: readPos past grain boundary -> envelope is zero
	if (v.readPos >= v.grainLenSamples || v.readPos < 0.0f)
		return 0.0f;

	// Tukey-windowed envelope with configurable taper fraction
	const float pos = v.reverse ? (v.grainLenSamples - v.readPos) : v.readPos;
	const float remaining = v.grainLenSamples - pos;

	// Taper length: fraction of grain used for fade-in/out (from SMOOTH)
	// Minimum must cover at least 2x the pitch ratio step so the fade-out
	// zone can't be entirely skipped in a single read advance.
	// Cap to 40% of grain so both tapers (in+out) never exceed 80% - the
	// envelope always reaches full amplitude even on very short grains.
	const float minTaper = juce::jmax (2.0f, v.pitchRatio * 2.0f);
	const float maxTaper = juce::jmax (2.0f, v.grainLenSamples * 0.4f);
	const float taperLen = juce::jmin (maxTaper,
	                                   juce::jmax (minTaper, grainSmoothFraction_ * v.grainLenSamples * 0.5f));

	if (pos < taperLen)
		return taperWeight (pos, taperLen);
	if (remaining < taperLen)
		return taperWeight (remaining, taperLen);
	return 1.0f;
}

//==============================================================================
void GRATRAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
	juce::ScopedNoDenormals noDenormals;
	PERF_BLOCK_BEGIN();

	const int numChannels = juce::jmin (buffer.getNumChannels(), 2);
	const int numSamples  = buffer.getNumSamples();

	// MIDI note tracking -------------------------------------------
	const bool midiEnabled = loadBoolParamOrDefault (midiParam, false);

	if (midiEnabled && ! midiMessages.isEmpty())
	{
		const int selectedMidiChannel = midiChannel.load (std::memory_order_relaxed);
		for (const auto metadata : midiMessages)
		{
			const auto msg = metadata.getMessage();
			if (selectedMidiChannel > 0 && msg.getChannel() != selectedMidiChannel)
				continue;

			if (msg.isNoteOn())
			{
				const int noteNumber = msg.getNoteNumber();
				lastMidiNote.store (noteNumber, std::memory_order_relaxed);
				lastMidiVelocity.store (msg.getVelocity(), std::memory_order_relaxed);
				const float frequency = 440.0f * std::exp2 ((noteNumber - 69) * (1.0f / 12.0f));
				currentMidiFrequency.store (frequency, std::memory_order_relaxed);
			}
			else if (msg.isNoteOff())
			{
				if (msg.getNoteNumber() == lastMidiNote.load (std::memory_order_relaxed))
				{
					lastMidiNote.store (-1, std::memory_order_relaxed);
					currentMidiFrequency.store (0.0f, std::memory_order_relaxed);
				}
			}
		}
	}
	else if (! midiEnabled)
	{
		if (lastMidiNote.load (std::memory_order_relaxed) >= 0)
		{
			lastMidiNote.store (-1, std::memory_order_relaxed);
			currentMidiFrequency.store (0.0f, std::memory_order_relaxed);
		}
	}

	for (int i = numChannels; i < buffer.getNumChannels(); ++i)
		buffer.clear (i, 0, numSamples);

	if (grainBufferLength == 0 || currentSampleRate <= 0.0)
	{
		PERF_BLOCK_END(perfTrace, numSamples, currentSampleRate);
		return;
	}

	// Read parameters ----------------------------------------------
	const bool syncEnabled    = loadBoolParamOrDefault (syncParam, false);
	const bool autoEnabled    = loadBoolParamOrDefault (autoParam, false);
	const bool triggerParamOn = loadBoolParamOrDefault (triggerParam, false);
	const bool reverseEnabled = loadBoolParamOrDefault (reverseParam, false);
	const int  midiNote       = lastMidiNote.load (std::memory_order_relaxed);
	const bool midiNoteActive = midiEnabled && (midiNote >= 0);
	const bool triggerEnabled = triggerParamOn || midiNoteActive;
	const int  mode           = loadIntParamOrDefault (modeParam, 1);

	const float timeMsValue  = loadAtomicOrDefault (timeMsParam, kTimeMsDefault);
	const float modValue     = loadAtomicOrDefault (modParam, kModDefault);
	const float pitchSemi    = loadAtomicOrDefault (pitchParam, kPitchDefault);
	const float formantSemi  = loadAtomicOrDefault (formantParam, kFormantDefault);
	const float inputGainDb  = loadAtomicOrDefault (inputParam, kInputDefault);
	const float outputGainDb = loadAtomicOrDefault (outputParam, kOutputDefault);
	const float mixValue     = loadAtomicOrDefault (mixParam, kMixDefault);
	const int   mixMode  = loadIntParamOrDefault (mixModeParam, kMixModeDefault);
	const float dryLevelTarget = (mixMode == 1) ? loadAtomicOrDefault (dryLevelParam, kDryLevelDefault) : kDryLevelDefault;
	const float wetLevelTarget = (mixMode == 1) ? loadAtomicOrDefault (wetLevelParam, kWetLevelDefault) : kWetLevelDefault;
	const float panTarget = juce::jlimit (kPanMin, kPanMax, loadAtomicOrDefault (panParam, kPanDefault));

	// Filter / Tilt position
	{
		const int fltPos = loadIntParamOrDefault (filterPosParam, kFilterPosDefault);
		// 0=F-post T-post  1=F-pre T-pre  2=F-pre T-post  3=F-post T-pre
		filterPre_ = (fltPos == 1 || fltPos == 2);
		tiltPre_   = (fltPos == 1 || fltPos == 3);
	}

	const int modeInVal  = loadIntParamOrDefault (modeInParam,  kModeInOutDefault);
	const int modeOutVal = loadIntParamOrDefault (modeOutParam, kModeInOutDefault);
	const int sumBusVal  = loadIntParamOrDefault (sumBusParam,  kSumBusDefault);
	const int invPol     = loadIntParamOrDefault (invPolParam,  kInvPolDefault);
	const int invStr     = loadIntParamOrDefault (invStrParam,  kInvStrDefault);

	const float inputGain  = fastDecibelsToGain (inputGainDb);
	const float outputGain = fastDecibelsToGain (outputGainDb);

	// Limiter ------------------------------------------------------
	const int limMode = loadIntParamOrDefault (limModeParam, kLimModeDefault);
	const float limThreshLinTarget = (limMode != 0)
		? fastDecibelsToGain (loadAtomicOrDefault (limThresholdParam, kLimThresholdDefault))
		: 1.0f;

	// Pitch ratio & formant ratio (targets - smoothed per-sample below)
	currentPitchRatio_   = std::exp2 (pitchSemi / 12.0f);
	currentFormantRatio_ = std::exp2 (formantSemi / 12.0f);

	// MOD frequency multiplier (hyperbolic below centre, linear above - same as ECHO-TR)
	// 0.0 -> x0.25, 0.5 -> x1.0, 1.0 -> x4.0
	float modFreqMultiplier;
	if (modValue < 0.5f)
		modFreqMultiplier = 1.0f / (4.0f - 6.0f * modValue);
	else
		modFreqMultiplier = 1.0f + ((modValue - 0.5f) * 6.0f);

	// Grain time calculation
	float targetGrainMs = timeMsValue;

	if (midiNoteActive)
	{
		const float frequency = currentMidiFrequency.load (std::memory_order_relaxed);
		if (frequency > 0.1f)
			targetGrainMs = 1000.0f / frequency;
	}
	else if (syncEnabled)
	{
		const int timeSyncValue = loadIntParamOrDefault (timeSyncParam, kTimeSyncDefault);
		double bpm = 120.0;
		auto posInfo = getPlayHead();
		if (posInfo != nullptr)
		{
			auto pos = posInfo->getPosition();
			if (pos.hasValue() && pos->getBpm().hasValue())
				bpm = *pos->getBpm();
		}
		targetGrainMs = tempoSyncToMs (timeSyncValue, bpm);
	}

	const float maxAllowedMs = syncEnabled ? kTimeMsMaxSync : kTimeMsMax;
	targetGrainMs = juce::jlimit (kTimeMsMin, maxAllowedMs, targetGrainMs);
	float grainLenSamples = (float) currentSampleRate * (targetGrainMs / 1000.0f);
	grainLenSamples = juce::jlimit (kMinGrainSamples, (float) (grainBufferLength - 2), grainLenSamples);

	// Apply MOD multiplier: higher multiplier -> shorter grain -> higher frequency
	const float effectiveGrainLen = juce::jlimit (kMinGrainSamples, (float) (grainBufferLength - 2),
	                                              grainLenSamples / modFreqMultiplier);

	// MIDI velocity-controlled glide (portamento) for grain length transitions
	if (midiNoteActive)
	{
		const float vel  = (float) lastMidiVelocity.load (std::memory_order_relaxed);
		const float tLin = juce::jlimit (0.0f, 1.0f, (vel - 1.0f) / 126.0f);

		constexpr float kTauMax = 0.200f;   // 200 ms - full portamento at pianissimo
		constexpr float kTauMin = 0.0002f;  // 0.2 ms - imperceptible at max velocity

		const float t   = std::pow (tLin, 0.12f);  // gentler curve for grain-quantised glide
		const float tau = kTauMax - t * (kTauMax - kTauMin);
		grainLenGlideStep_ = 1.0f - std::exp (-1.0f / ((float) currentSampleRate * tau));
	}
	else
	{
		grainLenGlideStep_ = kGainSmoothStep;  // default ~5 ms smoothing
	}

	// Snap smoothedGrainLen_ on first use (avoid gliding from zero)
	if (smoothedGrainLen_ < kMinGrainSamples)
		smoothedGrainLen_ = effectiveGrainLen;

	targetGrainLen_ = effectiveGrainLen;

	// SMOOTH: shared taper amount for forward and reverse grain playback.
	const float smoothPct = juce::jlimit (0.0f, 1.0f,
		loadAtomicOrDefault (smoothParam, kSmoothDefault) * 0.01f);
	grainSmoothFraction_ = 0.02f + smoothPct * 0.98f;

	// Load filter / tilt / chaos per-block -------------------------
	wetFilterHpOn_ = loadBoolParamOrDefault (filterHpOnParam, false);
	wetFilterLpOn_ = loadBoolParamOrDefault (filterLpOnParam, false);
	wetFilterTargetHpFreq_ = loadAtomicOrDefault (filterHpFreqParam, kFilterHpFreqDefault);
	wetFilterTargetLpFreq_ = loadAtomicOrDefault (filterLpFreqParam, kFilterLpFreqDefault);
	{
		const int hpSlope = juce::roundToInt (loadAtomicOrDefault (filterHpSlopeParam, (float) kFilterSlopeDefault));
		const int lpSlope = juce::roundToInt (loadAtomicOrDefault (filterLpSlopeParam, (float) kFilterSlopeDefault));
		wetFilterNumSectionsHp_ = (hpSlope == 0) ? 1 : (hpSlope == 1) ? 1 : 2;
		wetFilterNumSectionsLp_ = (lpSlope == 0) ? 1 : (lpSlope == 1) ? 1 : 2;
	}

	tiltDb_ = loadAtomicOrDefault (tiltParam, kTiltDefault);

	// Chaos --------------------------------------------------------
	chaosFilterEnabled_ = loadBoolParamOrDefault (chaosParam, false);
	chaosDelayEnabled_  = loadBoolParamOrDefault (chaosDelayParam, false);
	const bool anyChaos = chaosFilterEnabled_ || chaosDelayEnabled_;
	if (anyChaos)
	{
		if (chaosDelayEnabled_)
		{
			const float rawAmtD = loadAtomicOrDefault (chaosAmtParam, kChaosAmtDefault);
			const float rawSpdD = loadAtomicOrDefault (chaosSpdParam, kChaosSpdDefault);
			chaosAmtD_       = rawAmtD;
			chaosAmtNormD_   = rawAmtD * 0.01f;
			chaosShPeriodD_  = (float) currentSampleRate / rawSpdD;
			const float amtNormD = rawAmtD * 0.01f;
			chaosDelayMaxSamples_ = amtNormD * 0.005f * (float) currentSampleRate;
			chaosGainMaxDb_       = amtNormD * 1.0f;
		}
		else { chaosDelayMaxSamples_ = 0.0f; chaosGainMaxDb_ = 0.0f; }

		if (chaosFilterEnabled_)
		{
			const float rawAmtF = loadAtomicOrDefault (chaosAmtFilterParam, kChaosAmtDefault);
			const float rawSpdF = loadAtomicOrDefault (chaosSpdFilterParam, kChaosSpdDefault);
			chaosAmtF_       = rawAmtF;
			chaosShPeriodF_  = (float) currentSampleRate / rawSpdF;
			chaosFilterMaxOct_ = rawAmtF * 0.01f * 2.0f;
		}
		else { chaosFilterMaxOct_ = 0.0f; }

		chaosParamSmoothCoeff_ = cachedChaosParamSmoothCoeff_;
	}
	else { chaosAmtD_ = 0.0f; chaosAmtF_ = 0.0f; chaosDelayMaxSamples_ = 0.0f; chaosGainMaxDb_ = 0.0f; chaosFilterMaxOct_ = 0.0f; }

	chaosStereo_ = (mode >= 1);

	// Flush denormals (filter biquad states + tilt + chaos)
	{
		constexpr float kDnr = 1e-20f;
		for (int ch = 0; ch < 2; ++ch)
		{
			for (int s = 0; s < 2; ++s)
			{
				if (std::abs (wetFilterState_[ch].hp[s].z1) < kDnr) wetFilterState_[ch].hp[s].z1 = 0.0f;
				if (std::abs (wetFilterState_[ch].hp[s].z2) < kDnr) wetFilterState_[ch].hp[s].z2 = 0.0f;
				if (std::abs (wetFilterState_[ch].lp[s].z1) < kDnr) wetFilterState_[ch].lp[s].z1 = 0.0f;
				if (std::abs (wetFilterState_[ch].lp[s].z2) < kDnr) wetFilterState_[ch].lp[s].z2 = 0.0f;
			}
		}
		if (std::abs (tiltState_[0])   < kDnr) tiltState_[0]   = 0.0f;
		if (std::abs (tiltState_[1])   < kDnr) tiltState_[1]   = 0.0f;
	}

	// Snap gain/mix smoothers
	constexpr float kSnapEpsilon = 1e-5f;
	if (std::abs (smoothedInputGain  - inputGain)  < kSnapEpsilon) smoothedInputGain  = inputGain;
	if (std::abs (smoothedOutputGain - outputGain) < kSnapEpsilon) smoothedOutputGain = outputGain;
	if (std::abs (smoothedMix        - mixValue)   < kSnapEpsilon) smoothedMix        = mixValue;
	if (std::abs (smoothedDryLevel   - dryLevelTarget) < kSnapEpsilon) smoothedDryLevel = dryLevelTarget;
	if (std::abs (smoothedWetLevel   - wetLevelTarget) < kSnapEpsilon) smoothedWetLevel = wetLevelTarget;
	if (std::abs (smoothedPan        - panTarget) < kSnapEpsilon) smoothedPan = panTarget;
	if (std::abs (smoothedLimThreshold - limThreshLinTarget) < kSnapEpsilon) smoothedLimThreshold = limThreshLinTarget;
	if (std::abs (smoothedPitchRatio_   - currentPitchRatio_)   < kSnapEpsilon) smoothedPitchRatio_   = currentPitchRatio_;
	if (std::abs (smoothedFormantRatio_ - currentFormantRatio_) < kSnapEpsilon) smoothedFormantRatio_ = currentFormantRatio_;

	// Dry passthrough: when neither AUTO nor TRIGGER is active, fade mix to 0
	// so the dry signal passes through instead of silence.
	const float effectiveMixTarget = (!autoEnabled && !triggerEnabled) ? 0.0f : mixValue;

	// Detect TRIGGER edge ------------------------------------------
	const bool triggerEdge = triggerEnabled && !prevTriggerState_;
	prevTriggerState_ = triggerEnabled;

	// Detect AUTO enable edge (launch immediately on enable) ------
	const bool autoJustEnabled = autoEnabled && !lastAutoEnabled_;
	lastAutoEnabled_ = autoEnabled;

	// Per-sample processing ----------------------------------------
	const int wrapMask = grainBufferLength - 1;
	auto* bufL = grainBuffer.getWritePointer (0);
	auto* bufR = grainBuffer.getWritePointer (1);
	auto* channelL = numChannels > 0 ? buffer.getWritePointer (0) : nullptr;
	auto* channelR = numChannels > 1 ? buffer.getWritePointer (1) : nullptr;

	for (int i = 0; i < numSamples; ++i)
	{
		// S&H chaos advance
		if (chaosDelayEnabled_) advanceChaosD();
		if (chaosFilterEnabled_) advanceChaosF();

		// Smooth gains
		smoothedInputGain  += (inputGain  - smoothedInputGain)  * kGainSmoothStep;
		smoothedOutputGain += (outputGain - smoothedOutputGain) * kGainSmoothStep;
		smoothedMix        += (effectiveMixTarget - smoothedMix) * kGainSmoothStep;
		smoothedDryLevel   += (dryLevelTarget - smoothedDryLevel) * kGainSmoothStep;
		smoothedWetLevel   += (wetLevelTarget - smoothedWetLevel) * kGainSmoothStep;
		smoothedPan        += (panTarget - smoothedPan) * kGainSmoothStep;
		smoothedLimThreshold += (limThreshLinTarget - smoothedLimThreshold) * kGainSmoothStep;

		// Smooth pitch & formant ratios (same EMA as gain to avoid abrupt changes)
		smoothedPitchRatio_   += (currentPitchRatio_   - smoothedPitchRatio_)   * kGainSmoothStep;
		smoothedFormantRatio_ += (currentFormantRatio_ - smoothedFormantRatio_) * kGainSmoothStep;

		// Smooth grain length (velocity-controlled glide when MIDI active)
		smoothedGrainLen_ += (effectiveGrainLen - smoothedGrainLen_) * grainLenGlideStep_;

		// Capture length from smoothed grain length & formant ratio
		const float captureLen = juce::jlimit (kMinGrainSamples, (float) (grainBufferLength - 2),
		                                       smoothedGrainLen_ / smoothedFormantRatio_);

		// Read input
		float inL = (channelL != nullptr) ? channelL[i] * smoothedInputGain : 0.0f;
		float inR = (channelR != nullptr) ? channelR[i] * smoothedInputGain : inL;

		// Mode In: M/S encode input
		if (numChannels >= 2 && modeInVal != 0)
		{
			const float l = inL, r = inR;
			if (modeInVal == 1)      { const float mid  = (l + r) * kSqrt2Over2; inL = inR = mid; }
			else /* modeInVal==2 */   { const float side = (l - r) * kSqrt2Over2; inL = inR = side; }
		}

		// PRE filter/tilt: apply before grain capture
		if (filterPre_) filterWetSample (inL, inR);
		if (tiltPre_)   tiltWetSample   (inL, inR);

		// Write to grain buffer (frozen when TRIGGER held = grain freeze/loop mode)
		if (!triggerEnabled)
		{
			bufL[grainBufferWritePos] = inL;
			bufR[grainBufferWritePos] = inR;
			grainBufferWritePos = (grainBufferWritePos + 1) & wrapMask;
		}

		// Grain triggering -------------------------------------------
		bool shouldLaunch = false;

		if (autoEnabled)
		{
			// First sample after enabling AUTO: launch immediately
			if (autoJustEnabled && i == 0)
				shouldLaunch = true;

			autoPhaseCounter_ += 1.0f;
			if (autoPhaseCounter_ >= smoothedGrainLen_)
			{
				autoPhaseCounter_ -= smoothedGrainLen_;
				shouldLaunch = true;
			}
		}

		// TRIGGER edge: launch on first sample of the block when edge detected
		if (triggerEdge && i == 0)
			shouldLaunch = true;

		if (shouldLaunch)
		{
			if (mode == 0) // MONO: same grain for both channels
			{
				launchNewGrain (0, captureLen, reverseEnabled);
				launchNewGrain (1, captureLen, reverseEnabled);
				// Sync voice anchors for mono
				voiceA_[1].anchorWritePos = voiceA_[0].anchorWritePos;
			}
			else if (mode == 2) // WIDE: temporal decorrelation + M/S widening
			{
				launchNewGrain (0, captureLen, reverseEnabled);
				launchNewGrain (1, captureLen, reverseEnabled);
				// Offset R anchor by half grain for channel decorrelation
				voiceA_[1].anchorWritePos = (voiceA_[1].anchorWritePos - (int)(captureLen * 0.5f)) & wrapMask;
			}
			else if (mode == 3) // DUAL: R at x0.5 pitch (octave down) + temporal offset
			{
				launchNewGrain (0, captureLen, reverseEnabled);
				launchNewGrain (1, captureLen, reverseEnabled);
				voiceA_[1].pitchRatio = smoothedPitchRatio_ * 0.5f;
				voiceA_[1].anchorWritePos = (voiceA_[1].anchorWritePos - (int)(captureLen * 0.5f)) & wrapMask;
			}
			else // STEREO (default): independent per-channel
			{
				launchNewGrain (0, captureLen, reverseEnabled);
				launchNewGrain (1, captureLen, reverseEnabled);
			}
		}

		// Read grains and compute wet signal --------------------------
		float wetL = 0.0f, wetR = 0.0f;

		for (int ch = 0; ch < 2; ++ch)
		{
			float wet = 0.0f;

			// Voice A (primary, fading in)
			if (voiceA_[ch].active)
			{
				const float env = grainEnvelope (voiceA_[ch]);
				const float sample = readGrainInterpolated (voiceA_[ch], (mode == 0) ? 0 : ch);

				// Crossfade: fade-in
				voiceA_[ch].fadeGain = juce::jmin (1.0f, voiceA_[ch].fadeGain + (1.0f / juce::jmax (1.0f, smoothedGrainLen_ * grainSmoothFraction_ * 0.5f)));
				wet += sample * env * voiceA_[ch].fadeGain;

				// Advance read position (always forward; reverse mapping in readGrainInterpolated)
				voiceA_[ch].readPos += voiceA_[ch].pitchRatio;
				if (voiceA_[ch].readPos >= voiceA_[ch].grainLenSamples || voiceA_[ch].readPos < 0.0f)
				{
					if (autoEnabled || triggerEnabled)
					{
						// Immediate relaunch: prevents silence gaps when pitch > 1
						// or formant > 0 causes the grain to end before the auto
						// phase counter triggers the next one.
						launchNewGrain (ch, captureLen, reverseEnabled);
						// WIDE: R channel offset anchor for temporal decorrelation
						if (mode == 2 && ch == 1)
						{
							voiceA_[1].anchorWritePos = (voiceA_[1].anchorWritePos - (int)(captureLen * 0.5f)) & wrapMask;
						}
						// DUAL: R channel plays at x0.5 pitch (octave down) + offset anchor
						if (mode == 3 && ch == 1)
						{
							voiceA_[1].pitchRatio = smoothedPitchRatio_ * 0.5f;
							voiceA_[1].anchorWritePos = (voiceA_[1].anchorWritePos - (int)(captureLen * 0.5f)) & wrapMask;
						}
						autoPhaseCounter_ = 0.0f;
					}
					else
					{
						voiceA_[ch].active = false;
					}
				}
			}

			// Voice B (crossfade-out)
			if (voiceB_[ch].active)
			{
				const float env = grainEnvelope (voiceB_[ch]);
				const float sample = readGrainInterpolated (voiceB_[ch], (mode == 0) ? 0 : ch);

				// Crossfade: fade-out (use voice's own stored grain length for consistency)
				voiceB_[ch].fadeGain -= (1.0f / juce::jmax (1.0f, voiceB_[ch].grainLenSamples * grainSmoothFraction_ * 0.5f));
				if (voiceB_[ch].fadeGain <= 0.0f)
				{
					voiceB_[ch].active = false;
					voiceB_[ch].fadeGain = 0.0f;
				}
				else
				{
					wet += sample * env * voiceB_[ch].fadeGain;
					voiceB_[ch].readPos += voiceB_[ch].pitchRatio;
					if (voiceB_[ch].readPos >= voiceB_[ch].grainLenSamples || voiceB_[ch].readPos < 0.0f)
						voiceB_[ch].active = false;
				}
			}

			if (ch == 0) wetL = wet;
			else         wetR = wet;
		}

		// MONO mode: duplicate L to R
		if (mode == 0)
			wetR = wetL;

		// WIDE mode: M/S widening (boost sides)
		if (mode == 2)
		{
			const float mid  = (wetL + wetR) * 0.5f;
			const float side = (wetL - wetR) * 0.5f;
			wetL = mid + side * 1.5f;
			wetR = mid - side * 1.5f;
		}

		// Wet-signal processing chain (filter + tilt + chaos)
		if (!tiltPre_)   tiltWetSample   (wetL, wetR);
		if (!filterPre_) filterWetSample (wetL, wetR);
		if (chaosDelayEnabled_) applyChaosDelay (wetL, wetR);

		// Mode Out: M/S encode wet output
		if (numChannels >= 2 && modeOutVal != 0)
		{
			const float l = wetL, r = wetR;
			if (modeOutVal == 1)      { const float mid  = (l + r) * kSqrt2Over2; wetL = wetR = mid; }
			else /* modeOutVal==2 */   { const float side = (l - r) * kSqrt2Over2; wetL = wetR = side; }
		}

		// Mix dry/wet with Sum Bus routing
		const float dryL = (channelL != nullptr) ? channelL[i] : 0.0f;
		const float dryR = (channelR != nullptr) ? channelR[i] : dryL;

		float wL = wetL * smoothedOutputGain;
		float wR = wetR * smoothedOutputGain;
		if (limMode == 1)
			applyLimiterSample (wL, wR, smoothedLimThreshold);

		// Invert Polarity / Stereo (WET mode: after Limiter WET)
		if (invPol == 1) { wL = -wL; wR = -wR; }
		if (invStr == 1 && numChannels >= 2) std::swap (wL, wR);

		float dG, wG;
		if (mixMode == 0) { dG = 1.0f - smoothedMix; wG = smoothedMix; }
		else              { dG = smoothedDryLevel; wG = smoothedWetLevel; }
		wL *= wG;
		wR *= wG;
		const float dL = dryL * dG;
		const float dR = dryR * dG;
		float outL = 0.0f, outR = 0.0f;

		if (sumBusVal == 0) // ST: normal stereo
		{
			outL = dL + wL;
			outR = dR + wR;
		}
		else if (sumBusVal == 1) // to M: wet collapsed to mono mid
		{
			const float midBus = (wL + wR) * 0.5f;
			outL = dL + midBus;
			outR = dR + midBus;
		}
		else // to S: wet collapsed to side
		{
			const float sideBus = (wL - wR) * 0.5f;
			outL = dL + sideBus;
			outR = dR - sideBus;
		}

		if (numChannels >= 2
		 && (std::abs (panTarget - 0.5f) > 0.001f || std::abs (smoothedPan - 0.5f) > 0.001f))
		{
			const float angle = smoothedPan * 1.5707963f;
			outL *= std::cos (angle);
			outR *= std::sin (angle);
		}

		if (limMode == 2)
		{
			if (numChannels >= 2)
				applyLimiterSample (outL, outR, smoothedLimThreshold);
			else
			{
				float dummy = 0.0f;
				applyLimiterSample (outL, dummy, smoothedLimThreshold);
			}
		}

		if (channelL != nullptr) channelL[i] = outL;
		if (channelR != nullptr) channelR[i] = outR;
	}

	if (invPol == 2)
		for (int ch = 0; ch < numChannels; ++ch)
			juce::FloatVectorOperations::multiply (buffer.getWritePointer (ch), -1.0f, numSamples);
	if (invStr == 2 && numChannels >= 2)
	{
		float* sL = buffer.getWritePointer (0);
		float* sR = buffer.getWritePointer (1);
		for (int n = 0; n < numSamples; ++n)
			std::swap (sL[n], sR[n]);
	}

	// Safety hard-limiter
	{
		constexpr float kSafetyLimit = 251.19f;
		for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
		{
			auto* data = buffer.getWritePointer (ch);
			juce::FloatVectorOperations::clip (data, data, -kSafetyLimit, kSafetyLimit, numSamples);
		}
	}

	PERF_BLOCK_END(perfTrace, numSamples, currentSampleRate);
}

//==============================================================================
bool GRATRAudioProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor* GRATRAudioProcessor::createEditor()
{
	return new GRATRAudioProcessorEditor (*this);
}

//==============================================================================
void GRATRAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
	auto state = apvts.copyState();
	std::unique_ptr<juce::XmlElement> xml (state.createXml());
	copyXmlToBinary (*xml, destData);
}

void GRATRAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
	std::unique_ptr<juce::XmlElement> xmlState (getXmlFromBinary (data, sizeInBytes));
	if (xmlState.get() != nullptr)
	{
		if (xmlState->hasTagName (apvts.state.getType()))
		{
			apvts.replaceState (juce::ValueTree::fromXml (*xmlState));
			const auto restoredChannel = apvts.state.getProperty (UiStateKeys::midiPort);
			if (! restoredChannel.isVoid())
				midiChannel.store ((int) restoredChannel, std::memory_order_relaxed);
		}
	}
}

void GRATRAudioProcessor::getCurrentProgramStateInformation (juce::MemoryBlock& destData)
{
	getStateInformation (destData);
}

void GRATRAudioProcessor::setCurrentProgramStateInformation (const void* data, int sizeInBytes)
{
	setStateInformation (data, sizeInBytes);
}

//==============================================================================
// Tempo sync (identical to ECHO-TR)

juce::StringArray GRATRAudioProcessor::getTimeSyncChoices()
{
	return {
		"1/64T", "1/64", "1/64.",
		"1/32T", "1/32", "1/32.",
		"1/16T", "1/16", "1/16.",
		"1/8T",  "1/8",  "1/8.",
		"1/4T",  "1/4",  "1/4.",
		"1/2T",  "1/2",  "1/2.",
		"1/1T",  "1/1",  "1/1.",
		"2/1T",  "2/1",  "2/1.",
		"4/1T",  "4/1",  "4/1.",
		"8/1T",  "8/1",  "8/1."
	};
}

juce::String GRATRAudioProcessor::getTimeSyncName (int index)
{
	auto choices = getTimeSyncChoices();
	if (index >= 0 && index < choices.size())
		return choices[index];
	return "1/8";
}

float GRATRAudioProcessor::tempoSyncToMs (int syncIndex, double bpm) const
{
	if (bpm <= 0.0) bpm = 120.0;
	syncIndex = juce::jlimit (0, 29, syncIndex);

	const float divisions[] = { 64.0f, 32.0f, 16.0f, 8.0f, 4.0f, 2.0f, 1.0f, 0.5f, 0.25f, 0.125f };
	const int baseIndex = syncIndex / 3;
	const int modifier  = syncIndex % 3;

	const float quarterNoteMs = (float) (60000.0 / bpm);
	float durationMs = quarterNoteMs * (4.0f / divisions[baseIndex]);

	if (modifier == 0) durationMs *= (2.0f / 3.0f);
	else if (modifier == 2) durationMs *= 1.5f;

	return durationMs;
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout GRATRAudioProcessor::createParameterLayout()
{
	std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

	params.push_back (std::make_unique<juce::AudioParameterFloat> (
		kParamTimeMs, "Time",
		juce::NormalisableRange<float> (kTimeMsMin, kTimeMsMax, 0.0f, 0.25f), kTimeMsDefault));

	params.push_back (std::make_unique<juce::AudioParameterChoice> (
		kParamTimeSync, "Time Sync", getTimeSyncChoices(), kTimeSyncDefault));

	params.push_back (std::make_unique<juce::AudioParameterFloat> (
		kParamMod, "Mod",
		juce::NormalisableRange<float> (kModMin, kModMax, 0.0f, 1.0f), kModDefault));

	params.push_back (std::make_unique<juce::AudioParameterFloat> (
		kParamPitch, "Pitch",
		juce::NormalisableRange<float> (kPitchMin, kPitchMax, 0.01f, 1.0f), kPitchDefault));

	params.push_back (std::make_unique<juce::AudioParameterFloat> (
		kParamFormant, "Formant",
		juce::NormalisableRange<float> (kFormantMin, kFormantMax, 0.01f, 1.0f), kFormantDefault));

	params.push_back (std::make_unique<juce::AudioParameterFloat> (
		kParamSmooth, "Smooth",
		juce::NormalisableRange<float> (kSmoothMin, kSmoothMax, 0.01f, 1.0f), kSmoothDefault));

	params.push_back (std::make_unique<juce::AudioParameterFloat> (
		kParamMode, "Style",
		juce::NormalisableRange<float> ((float) kModeMin, (float) kModeMax, 1.0f, 1.0f), kModeDefault));

	params.push_back (std::make_unique<juce::AudioParameterFloat> (
		kParamInput, "Input",
		juce::NormalisableRange<float> (kInputMin, kInputMax, 0.0f, 2.5f), kInputDefault));

	params.push_back (std::make_unique<juce::AudioParameterFloat> (
		kParamOutput, "Output",
		juce::NormalisableRange<float> (kOutputMin, kOutputMax, 0.0f, 3.23f), kOutputDefault));

	params.push_back (std::make_unique<juce::AudioParameterFloat> (
		kParamMix, "Mix",
		juce::NormalisableRange<float> (kMixMin, kMixMax, 0.0f, 1.0f), kMixDefault));

	params.push_back (std::make_unique<juce::AudioParameterChoice> (
		kParamModeIn, "Mode In", juce::StringArray { "L+R", "MID", "SIDE" }, kModeInOutDefault));
	params.push_back (std::make_unique<juce::AudioParameterChoice> (
		kParamModeOut, "Mode Out", juce::StringArray { "L+R", "MID", "SIDE" }, kModeInOutDefault));
	params.push_back (std::make_unique<juce::AudioParameterChoice> (
		kParamSumBus, "Sum Bus", juce::StringArray { "ST", u8"\u2192M", u8"\u2192S" }, kSumBusDefault));

	// Invert Polarity / Invert Stereo
	params.push_back (std::make_unique<juce::AudioParameterChoice> (
		kParamInvPol, "Invert Polarity",
		juce::StringArray { "NONE", "WET", "GLOBAL" }, kInvPolDefault));
	params.push_back (std::make_unique<juce::AudioParameterChoice> (
		kParamInvStr, "Invert Stereo",
		juce::StringArray { "NONE", "WET", "GLOBAL" }, kInvStrDefault));

	// Mix Mode + Dry/Wet levels (SEND mode) + Filter position
	params.push_back (std::make_unique<juce::AudioParameterChoice> (
		kParamMixMode, "Mix Mode",
		juce::StringArray { "INSERT", "SEND" }, kMixModeDefault));
	params.push_back (std::make_unique<juce::AudioParameterFloat> (
		kParamDryLevel, "Dry Level",
		juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), kDryLevelDefault));
	params.push_back (std::make_unique<juce::AudioParameterFloat> (
		kParamWetLevel, "Wet Level",
		juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), kWetLevelDefault));
	// Filter / Tilt position (PRE / POST)
	params.push_back (std::make_unique<juce::AudioParameterChoice> (
		kParamFilterPos, "Filter Position",
		juce::StringArray { juce::String::fromUTF8 (u8"F\u25bc T\u25bc"),
		                    juce::String::fromUTF8 (u8"F\u25b2 T\u25b2"),
		                    juce::String::fromUTF8 (u8"F\u25b2 T\u25bc"),
		                    juce::String::fromUTF8 (u8"F\u25bc T\u25b2") },
		kFilterPosDefault));

	params.push_back (std::make_unique<juce::AudioParameterBool> (kParamSync, "Sync", false));
	params.push_back (std::make_unique<juce::AudioParameterBool> (kParamMidi, "MIDI", false));
	params.push_back (std::make_unique<juce::AudioParameterBool> (kParamAuto, "Auto", false));
	params.push_back (std::make_unique<juce::AudioParameterBool> (kParamTrigger, "Trigger", false));
	params.push_back (std::make_unique<juce::AudioParameterBool> (kParamReverse, "Reverse", false));

	// HP/LP wet-signal filter
	params.push_back (std::make_unique<juce::AudioParameterFloat> (
		kParamFilterHpFreq, "Filter HP Freq",
		juce::NormalisableRange<float> (kFilterFreqMin, kFilterFreqMax, 0.01f, 0.35f), kFilterHpFreqDefault));
	params.push_back (std::make_unique<juce::AudioParameterFloat> (
		kParamFilterLpFreq, "Filter LP Freq",
		juce::NormalisableRange<float> (kFilterFreqMin, kFilterFreqMax, 0.01f, 0.35f), kFilterLpFreqDefault));
	params.push_back (std::make_unique<juce::AudioParameterFloat> (
		kParamFilterHpSlope, "Filter HP Slope",
		juce::NormalisableRange<float> ((float) kFilterSlopeMin, (float) kFilterSlopeMax, 1.0f), (float) kFilterSlopeDefault));
	params.push_back (std::make_unique<juce::AudioParameterFloat> (
		kParamFilterLpSlope, "Filter LP Slope",
		juce::NormalisableRange<float> ((float) kFilterSlopeMin, (float) kFilterSlopeMax, 1.0f), (float) kFilterSlopeDefault));
	params.push_back (std::make_unique<juce::AudioParameterBool> (kParamFilterHpOn, "Filter HP On", false));
	params.push_back (std::make_unique<juce::AudioParameterBool> (kParamFilterLpOn, "Filter LP On", false));

	// Tilt EQ
	params.push_back (std::make_unique<juce::AudioParameterFloat> (
		kParamTilt, "Tilt",
		juce::NormalisableRange<float> (kTiltMin, kTiltMax, 0.01f), kTiltDefault));

	params.push_back (std::make_unique<juce::AudioParameterFloat> (
		kParamPan, "Pan",
		juce::NormalisableRange<float> (kPanMin, kPanMax, 0.01f), kPanDefault));

	// Chaos
	params.push_back (std::make_unique<juce::AudioParameterBool> (kParamChaos, "Chaos Filter", false));
	params.push_back (std::make_unique<juce::AudioParameterBool> (kParamChaosD, "Chaos Delay", false));
	params.push_back (std::make_unique<juce::AudioParameterFloat> (
		kParamChaosAmt, "Chaos Amount",
		juce::NormalisableRange<float> (kChaosAmtMin, kChaosAmtMax, 0.1f), kChaosAmtDefault));
	params.push_back (std::make_unique<juce::AudioParameterFloat> (
		kParamChaosSpd, "Chaos Speed",
		juce::NormalisableRange<float> (kChaosSpdMin, kChaosSpdMax, 0.01f, 0.3f), kChaosSpdDefault));
	params.push_back (std::make_unique<juce::AudioParameterFloat> (
		kParamChaosAmtFilter, "Chaos Filter Amount",
		juce::NormalisableRange<float> (kChaosAmtMin, kChaosAmtMax, 0.1f), kChaosAmtDefault));
	params.push_back (std::make_unique<juce::AudioParameterFloat> (
		kParamChaosSpdFilter, "Chaos Filter Speed",
		juce::NormalisableRange<float> (kChaosSpdMin, kChaosSpdMax, 0.01f, 0.3f), kChaosSpdDefault));

	// Limiter
	params.push_back (std::make_unique<juce::AudioParameterFloat> (
		kParamLimThreshold, "Lim Threshold",
		juce::NormalisableRange<float> (kLimThresholdMin, kLimThresholdMax, 0.1f), kLimThresholdDefault));
	params.push_back (std::make_unique<juce::AudioParameterChoice> (
		kParamLimMode, "Lim Mode", juce::StringArray { "NONE", "WET", "GLOBAL" }, kLimModeDefault));

	// UI state (hidden from automation)
	params.push_back (std::make_unique<juce::AudioParameterInt> (kParamUiWidth, "UI Width", 360, 1600, 360));
	params.push_back (std::make_unique<juce::AudioParameterInt> (kParamUiHeight, "UI Height", 240, 1200, 480));
	params.push_back (std::make_unique<juce::AudioParameterBool> (kParamUiPalette, "UI Palette", false));
	params.push_back (std::make_unique<juce::AudioParameterBool> (kParamUiCrt, "UI CRT", false));
	params.push_back (std::make_unique<juce::AudioParameterInt> (kParamUiColor0, "UI Color 0", 0, 0xFFFFFF, 0x00FF00));
	params.push_back (std::make_unique<juce::AudioParameterInt> (kParamUiColor1, "UI Color 1", 0, 0xFFFFFF, 0x000000));

	return { params.begin(), params.end() };
}

//==============================================================================
// UI state management (identical to ECHO-TR pattern)

void GRATRAudioProcessor::setUiEditorSize (int width, int height)
{
	const int w = juce::jlimit (360, 1600, width);
	const int h = juce::jlimit (240, 1200, height);
	uiEditorWidth.store (w, std::memory_order_relaxed);
	uiEditorHeight.store (h, std::memory_order_relaxed);
	apvts.state.setProperty (UiStateKeys::editorWidth, w, nullptr);
	apvts.state.setProperty (UiStateKeys::editorHeight, h, nullptr);
	setParameterPlainValue (apvts, kParamUiWidth, (float) w);
	setParameterPlainValue (apvts, kParamUiHeight, (float) h);
	updateHostDisplay();
}

int GRATRAudioProcessor::getUiEditorWidth() const noexcept
{
	const auto fromState = apvts.state.getProperty (UiStateKeys::editorWidth);
	if (! fromState.isVoid()) return (int) fromState;
	if (uiWidthParam != nullptr) return (int) std::lround (uiWidthParam->load (std::memory_order_relaxed));
	return uiEditorWidth.load (std::memory_order_relaxed);
}

int GRATRAudioProcessor::getUiEditorHeight() const noexcept
{
	const auto fromState = apvts.state.getProperty (UiStateKeys::editorHeight);
	if (! fromState.isVoid()) return (int) fromState;
	if (uiHeightParam != nullptr) return (int) std::lround (uiHeightParam->load (std::memory_order_relaxed));
	return uiEditorHeight.load (std::memory_order_relaxed);
}

void GRATRAudioProcessor::setUiUseCustomPalette (bool shouldUseCustomPalette)
{
	uiUseCustomPalette.store (shouldUseCustomPalette ? 1 : 0, std::memory_order_relaxed);
	apvts.state.setProperty (UiStateKeys::useCustomPalette, shouldUseCustomPalette, nullptr);
	setParameterPlainValue (apvts, kParamUiPalette, shouldUseCustomPalette ? 1.0f : 0.0f);
	updateHostDisplay();
}

bool GRATRAudioProcessor::getUiUseCustomPalette() const noexcept
{
	const auto fromState = apvts.state.getProperty (UiStateKeys::useCustomPalette);
	if (! fromState.isVoid()) return (bool) fromState;
	if (uiPaletteParam != nullptr) return uiPaletteParam->load (std::memory_order_relaxed) > 0.5f;
	return uiUseCustomPalette.load (std::memory_order_relaxed) != 0;
}

void GRATRAudioProcessor::setUiCrtEnabled (bool enabled)
{
	uiCrtEnabled.store (enabled ? 1 : 0, std::memory_order_relaxed);
	apvts.state.setProperty (UiStateKeys::crtEnabled, enabled, nullptr);
	setParameterPlainValue (apvts, kParamUiCrt, enabled ? 1.0f : 0.0f);
	updateHostDisplay();
}

bool GRATRAudioProcessor::getUiCrtEnabled() const noexcept
{
	const auto fromState = apvts.state.getProperty (UiStateKeys::crtEnabled);
	if (! fromState.isVoid()) return (bool) fromState;
	if (uiCrtParam != nullptr) return uiCrtParam->load (std::memory_order_relaxed) > 0.5f;
	return uiCrtEnabled.load (std::memory_order_relaxed) != 0;
}

void GRATRAudioProcessor::setUiIoExpanded (bool expanded)
{
	apvts.state.setProperty (UiStateKeys::ioExpanded, expanded, nullptr);
}

bool GRATRAudioProcessor::getUiIoExpanded() const noexcept
{
	const auto fromState = apvts.state.getProperty (UiStateKeys::ioExpanded);
	if (! fromState.isVoid()) return (bool) fromState;
	return false;
}

void GRATRAudioProcessor::setMidiChannel (int channel)
{
	const int ch = juce::jlimit (0, 16, channel);
	midiChannel.store (ch, std::memory_order_relaxed);
	apvts.state.setProperty (UiStateKeys::midiPort, ch, nullptr);
}

int GRATRAudioProcessor::getMidiChannel() const noexcept
{
	const auto fromState = apvts.state.getProperty (UiStateKeys::midiPort);
	if (! fromState.isVoid()) return juce::jlimit (0, 16, (int) fromState);
	return midiChannel.load (std::memory_order_relaxed);
}

void GRATRAudioProcessor::setUiCustomPaletteColour (int index, juce::Colour colour)
{
	if (index >= 0 && index < 2)
	{
		uiCustomPalette[(size_t) index].store (colour.getARGB(), std::memory_order_relaxed);
		const juce::String key = UiStateKeys::customPalette[(size_t) index];
		apvts.state.setProperty (key, (int) colour.getARGB(), nullptr);
		if (uiColorParams[(size_t) index] != nullptr)
			setParameterPlainValue (apvts, (index == 0 ? kParamUiColor0 : kParamUiColor1),
			                        (float) (int) colour.getARGB());
		updateHostDisplay();
	}
}

juce::Colour GRATRAudioProcessor::getUiCustomPaletteColour (int index) const noexcept
{
	if (index < 0 || index >= 2)
		return juce::Colours::white;

	const juce::String key = UiStateKeys::customPalette[(size_t) index];
	const auto fromState = apvts.state.getProperty (key);
	if (! fromState.isVoid())
		return juce::Colour ((juce::uint32) (int) fromState);

	if (uiColorParams[(size_t) index] != nullptr)
	{
		const int rgb = juce::jlimit (0, 0xFFFFFF,
		                              (int) std::lround (uiColorParams[(size_t) index]->load (std::memory_order_relaxed)));
		const juce::uint8 r = (juce::uint8) ((rgb >> 16) & 0xFF);
		const juce::uint8 g = (juce::uint8) ((rgb >> 8) & 0xFF);
		const juce::uint8 b = (juce::uint8) (rgb & 0xFF);
		return juce::Colour::fromRGB (r, g, b);
	}

	return juce::Colour (uiCustomPalette[(size_t) index].load (std::memory_order_relaxed));
}

//==============================================================================
// MIDI helpers

juce::String GRATRAudioProcessor::getMidiNoteName (int midiNote)
{
	if (midiNote < 0 || midiNote > 127) return "";
	const char* noteNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
	const int octave = (midiNote / 12) - 1;
	const int noteIndex = midiNote % 12;
	return juce::String (noteNames[noteIndex]) + juce::String (octave);
}

float GRATRAudioProcessor::getCurrentGrainMs() const
{
	const bool midiEn = loadBoolParamOrDefault (midiParam, false);
	const int note = lastMidiNote.load (std::memory_order_relaxed);
	const bool midiActive = midiEn && (note >= 0);

	if (midiActive)
	{
		const float freq = currentMidiFrequency.load (std::memory_order_relaxed);
		if (freq > 0.1f) return 1000.0f / freq;
	}

	const bool syncEn = loadBoolParamOrDefault (syncParam, false);
	if (syncEn)
	{
		const int idx = loadIntParamOrDefault (timeSyncParam, kTimeSyncDefault);
		double bpm = 120.0;
		auto posInfo = getPlayHead();
		if (posInfo != nullptr)
		{
			auto pos = posInfo->getPosition();
			if (pos.hasValue() && pos->getBpm().hasValue())
				bpm = *pos->getBpm();
		}
		return tempoSyncToMs (idx, bpm);
	}

	return loadAtomicOrDefault (timeMsParam, kTimeMsDefault);
}

juce::String GRATRAudioProcessor::getCurrentTimeDisplay() const
{
	const bool midiEn = loadBoolParamOrDefault (midiParam, false);
	const int note = lastMidiNote.load (std::memory_order_relaxed);
	if (midiEn && note >= 0)
		return getMidiNoteName (note);

	const bool syncEn = loadBoolParamOrDefault (syncParam, false);
	if (syncEn)
	{
		const int idx = loadIntParamOrDefault (timeSyncParam, kTimeSyncDefault);
		return getTimeSyncName (idx);
	}

	return juce::String (loadAtomicOrDefault (timeMsParam, kTimeMsDefault), 1) + " ms";
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
	return new GRATRAudioProcessor();
}
