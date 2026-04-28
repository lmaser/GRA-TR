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

	constexpr float kGrainSizeChangeMinRatio = 0.0025f;
	constexpr float kGrainSizeChangeMinSamples = 4.0f;
	constexpr float kGrainSizeTransitionHoldGrains = 2.0f;
	constexpr float kGrainSizeTransitionMinSeconds = 0.020f;
	constexpr float kGrainSizeTransitionMaxSeconds = 0.500f;
	constexpr float kGrainSizeTransitionGlideTauSeconds = 0.025f;
	constexpr float kGrainSizeTransitionSmoothFloor = 0.12f;
	constexpr double kHostSyncPhaseBoundaryToleranceSamples = 2.0;
	constexpr double kSyncedAutoPeriodReanchorMinRatio = 0.01;

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
	scanParam     = apvts.getRawParameterValue (kParamScan);
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
	backNForthParam = apvts.getRawParameterValue (kParamBackNForth);

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
}

GRATRAudioProcessor::~GRATRAudioProcessor()
{
#if GRA_TR_BNF_DETERMINISM_DUMP
	flushBnfDeterminismDump();
#endif
}

#if GRA_TR_BNF_DETERMINISM_DUMP
const char* GRATRAudioProcessor::getBnfDumpEventName (BnfDumpEvent eventType) noexcept
{
	switch (eventType)
	{
		case BnfDumpEvent::prepare:        return "prepare";
		case BnfDumpEvent::release:        return "release";
		case BnfDumpEvent::transportReset: return "transport_reset";
		case BnfDumpEvent::schedulerReset: return "scheduler_reset";
		case BnfDumpEvent::launchRequest:  return "launch_request";
		case BnfDumpEvent::voiceLaunch:    return "voice_launch";
		case BnfDumpEvent::voiceRelaunch:  return "voice_relaunch";
		default:                           return "unknown";
	}
}

void GRATRAudioProcessor::logBnfDeterminismEvent (BnfDumpEvent eventType, int channel,
                                                  const GrainVoice* voice) noexcept
{
	const auto writeIndex = bnfDumpWriteCount_ % (std::uint64_t) kBnfDumpCapacity;
	auto& row = bnfDumpRows_[(size_t) writeIndex];
	row = {};

	row.eventIndex = bnfDumpWriteCount_++;
	row.blockIndex = bnfDumpBlockIndex_;
	row.streamSample = bnfDumpProcessedSamples_ + (std::uint64_t) juce::jmax (0, bnfDumpCurrentSampleInBlock_);
	row.launchSerial = bnfDumpLaunchSerial_;
	row.eventType = (int) eventType;
	row.reason = bnfDumpCurrentLaunchReason_;
	row.sampleInBlock = bnfDumpCurrentSampleInBlock_;
	row.numSamples = bnfDumpCurrentNumSamples_;
	row.channel = channel;
	row.mode = bnfDumpMode_;
	row.grainBufferWritePos = grainBufferWritePos;
	row.anchorOffsetSamples = bnfDumpCurrentAnchorOffsetSamples_;
	row.syncEnabled = bnfDumpSyncEnabled_ ? 1 : 0;
	row.autoEnabled = bnfDumpAutoEnabled_ ? 1 : 0;
	row.triggerEnabled = bnfDumpTriggerEnabled_ ? 1 : 0;
	row.reverseEnabled = bnfDumpReverseEnabled_ ? 1 : 0;
	row.backNForthEnabled = bnfDumpBackNForthEnabled_ ? 1 : 0;
	row.midiEnabled = bnfDumpMidiEnabled_ ? 1 : 0;
	row.hostPlaying = hostTransport_.isPlaying ? 1 : 0;
	row.hostPlayStarted = hostTransport_.playStarted ? 1 : 0;
	row.hostSampleDiscontinuity = hostTransport_.sampleDiscontinuity ? 1 : 0;
	row.hostPpqDiscontinuity = hostTransport_.ppqDiscontinuity ? 1 : 0;
	row.deterministicResetCandidate = hostTransport_.deterministicResetCandidate ? 1 : 0;
	row.hasHostSample = bnfDumpHasHostSample_ ? 1 : 0;
	row.hasHostPpq = bnfDumpHasHostPpq_ ? 1 : 0;
	row.hostSample = bnfDumpHasHostSample_
		? bnfDumpHostSampleAtBlockStart_ + (juce::int64) juce::jmax (0, bnfDumpCurrentSampleInBlock_)
		: 0;
	row.hostPpq = bnfDumpHasHostPpq_
		? bnfDumpPpqAtBlockStart_ + ((double) juce::jmax (0, bnfDumpCurrentSampleInBlock_) / currentSampleRate) * (bnfDumpHostBpm_ / 60.0)
		: 0.0;
	row.hostBpm = bnfDumpHostBpm_;
	row.autoPhaseCounter = autoPhaseCounter_;
	row.targetGrainLen = targetGrainLen_;
	row.smoothedGrainLen = smoothedGrainLen_;
	row.targetGrainMs = bnfDumpTargetGrainMs_;
	row.modValue = bnfDumpModValue_;
	row.pitchRatio = smoothedPitchRatio_;
	row.scanRatio = smoothedScanRatio_;
	row.smoothFraction = grainSmoothFraction_;
	row.captureLen = bnfDumpCaptureLen_;
	row.bnfEventLen = bnfDumpBackNForthEventLen_;
	row.bnfMaxCellLen = bnfDumpBackNForthMaxCellLen_;
	row.bnfCellLen = bnfDumpBackNForthCellLen_;
	row.launchGrainLen = bnfDumpLaunchGrainLen_;
	row.launchSourceLen = bnfDumpLaunchSourceLen_;
	row.launchLegLen = bnfDumpLaunchLegLen_;

	if (voice != nullptr)
	{
		row.anchorWritePos = voice->anchorWritePos;
		row.cellCount = voice->backNForthCellCount;
		row.voiceGrainLen = voice->grainLenSamples;
		row.voiceSourceLen = voice->sourceLenSamples;
		row.voiceLegLen = voice->backNForthLegLenSamples;
		row.voiceCellLen = voice->backNForthCellLenSamples;
		row.voiceSourceCellLen = voice->backNForthSourceCellLenSamples;
		row.voiceReadPos = voice->readPos;
		row.voiceFadeGain = voice->fadeGain;
	}
}

void GRATRAudioProcessor::flushBnfDeterminismDump()
{
	if (bnfDumpWriteCount_ == 0)
		return;

	auto dumpFile = juce::File::getSpecialLocation (juce::File::userDesktopDirectory)
		.getChildFile ("GRA_TR_bnf_determinism_dump.csv");

	auto stream = dumpFile.createOutputStream();
	if (stream == nullptr)
		return;

	stream->writeText (
		"event_index,event_name,event_type,reason,block_index,sample_in_block,num_samples,stream_sample,"
		"host_sample,host_ppq,host_bpm,has_host_sample,has_host_ppq,host_playing,host_play_started,"
		"host_sample_discontinuity,host_ppq_discontinuity,deterministic_reset_candidate,launch_serial,"
		"channel,mode,sync,auto,trigger,rvs,bnf,midi,grain_buffer_write_pos,anchor_write_pos,"
		"anchor_offset_samples,auto_phase_counter,target_grain_len,smoothed_grain_len,target_grain_ms,"
		"mod_value,pitch_ratio,scan_ratio,smooth_fraction,capture_len,bnf_event_len,bnf_max_cell_len,"
		"bnf_cell_len,launch_grain_len,launch_source_len,launch_leg_len,cell_count,voice_grain_len,"
		"voice_source_len,voice_leg_len,voice_cell_len,voice_source_cell_len,voice_read_pos,voice_fade_gain\n",
		false, false, nullptr);

	const auto total = bnfDumpWriteCount_;
	const auto rowsToWrite = juce::jmin<std::uint64_t> (total, (std::uint64_t) kBnfDumpCapacity);
	const auto first = total - rowsToWrite;

	for (std::uint64_t n = 0; n < rowsToWrite; ++n)
	{
		const auto& row = bnfDumpRows_[(size_t) ((first + n) % (std::uint64_t) kBnfDumpCapacity)];
		const auto eventType = (BnfDumpEvent) row.eventType;

		juce::String line;
		line << (juce::int64) row.eventIndex << ","
		     << getBnfDumpEventName (eventType) << ","
		     << row.eventType << ","
		     << row.reason << ","
		     << (juce::int64) row.blockIndex << ","
		     << row.sampleInBlock << ","
		     << row.numSamples << ","
		     << (juce::int64) row.streamSample << ","
		     << row.hostSample << ","
		     << juce::String (row.hostPpq, 12) << ","
		     << juce::String (row.hostBpm, 6) << ","
		     << row.hasHostSample << ","
		     << row.hasHostPpq << ","
		     << row.hostPlaying << ","
		     << row.hostPlayStarted << ","
		     << row.hostSampleDiscontinuity << ","
		     << row.hostPpqDiscontinuity << ","
		     << row.deterministicResetCandidate << ","
		     << (juce::int64) row.launchSerial << ","
		     << row.channel << ","
		     << row.mode << ","
		     << row.syncEnabled << ","
		     << row.autoEnabled << ","
		     << row.triggerEnabled << ","
		     << row.reverseEnabled << ","
		     << row.backNForthEnabled << ","
		     << row.midiEnabled << ","
		     << row.grainBufferWritePos << ","
		     << row.anchorWritePos << ","
		     << row.anchorOffsetSamples << ","
		     << juce::String (row.autoPhaseCounter, 6) << ","
		     << juce::String (row.targetGrainLen, 6) << ","
		     << juce::String (row.smoothedGrainLen, 6) << ","
		     << juce::String (row.targetGrainMs, 6) << ","
		     << juce::String (row.modValue, 6) << ","
		     << juce::String (row.pitchRatio, 9) << ","
		     << juce::String (row.scanRatio, 9) << ","
		     << juce::String (row.smoothFraction, 9) << ","
		     << juce::String (row.captureLen, 6) << ","
		     << juce::String (row.bnfEventLen, 6) << ","
		     << juce::String (row.bnfMaxCellLen, 6) << ","
		     << juce::String (row.bnfCellLen, 6) << ","
		     << juce::String (row.launchGrainLen, 6) << ","
		     << juce::String (row.launchSourceLen, 6) << ","
		     << juce::String (row.launchLegLen, 6) << ","
		     << row.cellCount << ","
		     << juce::String (row.voiceGrainLen, 6) << ","
		     << juce::String (row.voiceSourceLen, 6) << ","
		     << juce::String (row.voiceLegLen, 6) << ","
		     << juce::String (row.voiceCellLen, 6) << ","
		     << juce::String (row.voiceSourceCellLen, 6) << ","
		     << juce::String (row.voiceReadPos, 6) << ","
		     << juce::String (row.voiceFadeGain, 9) << "\n";

		stream->writeText (line, false, false, nullptr);
	}

	bnfDumpWriteCount_ = 0;
}
#endif

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
	lastEffectiveGrainLenForTransition_ = 0.0f;
	lastSyncedAutoPeriodPpq_ = 0.0;
	grainSizeTransitionSamplesRemaining_ = 0;
	grainSizeTransitionActive_ = false;
	prevTriggerState_ = false;
	lastAutoEnabled_  = false;
	syncedAutoDryFillActive_ = false;
	syncedAutoDryFillGain_ = 0.0f;
	hostTransport_.reset();

#if GRA_TR_BNF_DETERMINISM_DUMP
	bnfDumpWriteCount_ = 0;
	bnfDumpBlockIndex_ = 0;
	bnfDumpProcessedSamples_ = 0;
	bnfDumpLaunchSerial_ = 0;
	bnfDumpCurrentSampleInBlock_ = 0;
	bnfDumpCurrentNumSamples_ = 0;
	bnfDumpCurrentLaunchReason_ = kBnfDumpReasonNone;
	bnfDumpCurrentAnchorOffsetSamples_ = 0;
	logBnfDeterminismEvent (BnfDumpEvent::prepare);
#endif

	currentPitchRatio_ = 1.0f;
	currentScanRatio_ = 1.0f;
	smoothedPitchRatio_ = 1.0f;
	smoothedScanRatio_ = 1.0f;

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
#if GRA_TR_BNF_DETERMINISM_DUMP
	logBnfDeterminismEvent (BnfDumpEvent::release);
	flushBnfDeterminismDump();
#endif

	grainBuffer.setSize (0, 0);
	grainBufferLength   = 0;
	grainBufferWritePos = 0;
	hostTransport_.reset();
}

void GRATRAudioProcessor::updateHostTransportMonitor (
	const juce::Optional<juce::AudioPlayHead::PositionInfo>& positionInfo,
	int numSamples) noexcept
{
	constexpr juce::int64 kSampleContinuityTolerance = 8;
	constexpr double kPpqBackwardTolerance = 1.0e-7;
	constexpr double kPpqContinuityTolerance = 0.01;

	auto& t = hostTransport_;
	t.playStarted = false;
	t.sampleDiscontinuity = false;
	t.ppqDiscontinuity = false;
	t.deterministicResetCandidate = false;

	if (! positionInfo.hasValue())
	{
		t.isPlaying = false;
		t.wasPlaying = false;
		t.hasLastTimeInSamples = false;
		t.hasLastPpqPosition = false;
		t.lastBlockSamples = numSamples;
		return;
	}

	const auto& pos = *positionInfo;
	const bool isPlaying = pos.getIsPlaying();
	t.isPlaying = isPlaying;
	t.playStarted = isPlaying && ! t.wasPlaying;

	if (const auto timeInSamples = pos.getTimeInSamples())
	{
		const auto currentSample = *timeInSamples;
		if (isPlaying && t.wasPlaying && t.hasLastTimeInSamples && t.lastBlockSamples > 0)
		{
			const auto expectedSample = t.lastTimeInSamples + (juce::int64) t.lastBlockSamples;
			const auto diff = currentSample >= expectedSample
				? currentSample - expectedSample
				: expectedSample - currentSample;

			if (currentSample < t.lastTimeInSamples || diff > kSampleContinuityTolerance)
				t.sampleDiscontinuity = true;
		}

		t.lastTimeInSamples = currentSample;
		t.hasLastTimeInSamples = true;
	}
	else
	{
		t.hasLastTimeInSamples = false;
	}

	if (const auto ppqPosition = pos.getPpqPosition())
	{
		const double currentPpq = *ppqPosition;
		if (isPlaying && t.wasPlaying && t.hasLastPpqPosition)
		{
			if (currentPpq + kPpqBackwardTolerance < t.lastPpqPosition)
			{
				t.ppqDiscontinuity = true;
			}
			else if (const auto bpm = pos.getBpm(); currentSampleRate > 0.0 && bpm.hasValue() && t.lastBlockSamples > 0)
			{
				const double expectedPpq = t.lastPpqPosition
					+ ((double) t.lastBlockSamples / currentSampleRate) * (*bpm / 60.0);

				if (std::abs (currentPpq - expectedPpq) > kPpqContinuityTolerance)
					t.ppqDiscontinuity = true;
			}
		}

		t.lastPpqPosition = currentPpq;
		t.hasLastPpqPosition = true;
	}
	else
	{
		t.hasLastPpqPosition = false;
	}

	t.deterministicResetCandidate = t.playStarted || t.sampleDiscontinuity || t.ppqDiscontinuity;
	t.wasPlaying = isPlaying;
	t.lastBlockSamples = numSamples;
}

void GRATRAudioProcessor::resetGranularSchedulersForDeterministicStart (bool reverseEnabled) noexcept
{
	juce::ignoreUnused (reverseEnabled);
	autoPhaseCounter_ = 0.0f;
	lastSyncedAutoPeriodPpq_ = 0.0;
	prevTriggerState_ = false;
	lastAutoEnabled_ = false;
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
// SCAN control (grain source-span scaling)
//
// SCAN changes how much source material each grain captures, without changing
// the external event period. At long TIME values this can feel like moving
// through the internal loop/fraseo rather than like a static tone control.
//

//==============================================================================
// Grain helpers

void GRATRAudioProcessor::launchNewGrain (int ch, float grainLenSamples, float sourceLenSamples, bool reverseGrain,
                                          bool backNForthGrain, int anchorOffsetSamples,
                                          float backNForthLegLenSamples)
{
	const int wrapMask = grainBufferLength - 1;

	// Cross-fade: move current voice A to voice B (fade-out)
	voiceB_[ch] = voiceA_[ch];
	if (voiceB_[ch].active)
	{
		voiceB_[ch].fadeGain = voiceA_[ch].fadeGain; // will fade out
		if (grainSizeTransitionActive_)
			voiceB_[ch].smoothFraction = juce::jmax (voiceB_[ch].smoothFraction, grainSmoothFraction_);
	}

	// Setup new voice A (fade-in)
	GrainVoice& v = voiceA_[ch];
	v.grainLenSamples = grainLenSamples;
	v.sourceLenSamples = juce::jlimit (kMinGrainSamples, (float) (grainBufferLength - 2), sourceLenSamples);
	v.backNForth = backNForthGrain;
	v.backNForthLegLenSamples = backNForthGrain
		? juce::jlimit (1.0f, grainLenSamples,
		                 backNForthLegLenSamples > 0.0f ? backNForthLegLenSamples : grainLenSamples * 0.5f)
		: grainLenSamples;
	v.backNForthCellLenSamples = juce::jmax (1.0f, v.backNForthLegLenSamples * 2.0f);
	v.backNForthInvCellLenSamples = 1.0f / v.backNForthCellLenSamples;
	v.backNForthCellCount = backNForthGrain
		? juce::jmax (1, (int) std::ceil (v.grainLenSamples * v.backNForthInvCellLenSamples))
		: 1;
	v.backNForthSourceCellLenSamples = v.sourceLenSamples / (float) v.backNForthCellCount;
	v.active = true;
	v.reverse = reverseGrain;
	v.pitchRatio = smoothedPitchRatio_;
	v.smoothFraction = grainSmoothFraction_;

	const float anchorLenSamples = backNForthGrain ? v.sourceLenSamples : grainLenSamples;

	// BNF anchors one leg of source audio; the second leg reads the same window backward.
	v.anchorWritePos = (grainBufferWritePos - (int) anchorLenSamples + anchorOffsetSamples) & wrapMask;

	// Read position always starts at 0; reverse mapping is handled in readGrainInterpolated
	v.readPos = 0.0f;
	v.fadeGain = 0.0f;  // will fade in

#if GRA_TR_BNF_DETERMINISM_DUMP
	bnfDumpCurrentAnchorOffsetSamples_ = anchorOffsetSamples;
	logBnfDeterminismEvent ((bnfDumpCurrentLaunchReason_ & kBnfDumpReasonVoiceEnd) != 0
		? BnfDumpEvent::voiceRelaunch
		: BnfDumpEvent::voiceLaunch,
		ch, &v);
#endif
}

float GRATRAudioProcessor::readGrainInterpolated (const GrainVoice& v, int ch) const
{
	if (!v.active || v.grainLenSamples < 1.0f || v.sourceLenSamples < 1.0f)
		return 0.0f;

	const int wrapMask = grainBufferLength - 1;
	const auto* buf = grainBuffer.getReadPointer (ch);

	// Map readPos within grain to buffer position. BNF performs the direction
	// turn inside the same voice so the midpoint does not relaunch or re-envelope.
	float readOffset = 0.0f;
	if (v.backNForth)
	{
		const float legLen = juce::jlimit (1.0f, v.grainLenSamples, v.backNForthLegLenSamples);
		const float cellLen = v.backNForthCellLenSamples;
		const int cellCount = v.backNForthCellCount;
		const int cellIndex = juce::jlimit (0, cellCount - 1, (int) std::floor (v.readPos / cellLen));
		float cellPos = v.readPos - (float) cellIndex * cellLen;
		if (cellPos < 0.0f)
			cellPos = 0.0f;

		const float sourceMax = juce::jmax (0.0f, v.sourceLenSamples - 1.0f);
		const float sourceCellLen = v.backNForthSourceCellLenSamples;
		const float sourceCellStart = juce::jmin (sourceMax, sourceCellLen * (float) cellIndex);
		const float sourceCellSpan = juce::jmax (0.0f, juce::jmin (sourceMax - sourceCellStart,
		                                                            sourceCellLen - 1.0f));
		const bool secondLeg = cellPos >= legLen;
		const bool reverseLeg = secondLeg ? ! v.reverse : v.reverse;
		const float legPos = juce::jlimit (0.0f, juce::jmax (0.0f, legLen - 1.0f),
		                                   secondLeg ? (cellPos - legLen) : cellPos);
		const float legSpan = juce::jmax (1.0f, legLen - 1.0f);
		const float readSpan = juce::jmin (sourceCellSpan, legSpan * v.pitchRatio);
		const float travel = juce::jlimit (0.0f, readSpan, legPos * v.pitchRatio);
		readOffset = sourceCellStart + (reverseLeg ? (readSpan - travel) : travel);
	}
	else
	{
		// Reverse starts at the last valid sample inside the captured grain.
		readOffset = v.reverse
			? juce::jmax (0.0f, (v.sourceLenSamples - 1.0f) - v.readPos)
			: v.readPos;
	}
	const float bufPos = (float) v.anchorWritePos + readOffset;

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
	const float pos = (v.backNForth || ! v.reverse) ? v.readPos : (v.grainLenSamples - v.readPos);
	const float remaining = v.grainLenSamples - pos;

	// Taper length: fraction of grain used for fade-in/out (from SMOOTH)
	// Minimum must cover at least 2x the pitch ratio step so the fade-out
	// zone can't be entirely skipped in a single read advance.
	// Cap to 40% of grain so both tapers (in+out) never exceed 80% - the
	// envelope always reaches full amplitude even on very short grains.
	const float minTaper = juce::jmax (2.0f, v.pitchRatio * 2.0f);
	const float maxTaper = juce::jmax (2.0f, v.grainLenSamples * 0.4f);
	const float taperLen = juce::jmin (maxTaper,
	                                   juce::jmax (minTaper, v.smoothFraction * v.grainLenSamples * 0.5f));

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
#if GRA_TR_BNF_DETERMINISM_DUMP
		bnfDumpProcessedSamples_ += (std::uint64_t) juce::jmax (0, numSamples);
		++bnfDumpBlockIndex_;
#endif
		return;
	}

	// Read parameters ----------------------------------------------
	const bool syncEnabled    = loadBoolParamOrDefault (syncParam, false);
	const bool autoEnabled    = loadBoolParamOrDefault (autoParam, false);
	const bool triggerParamOn = loadBoolParamOrDefault (triggerParam, false);
	const bool reverseEnabled = loadBoolParamOrDefault (reverseParam, false);
	const bool backNForthEnabled = loadBoolParamOrDefault (backNForthParam, false);
	const int  midiNote       = lastMidiNote.load (std::memory_order_relaxed);
	const bool midiNoteActive = midiEnabled && (midiNote >= 0);
	const bool triggerEnabled = triggerParamOn || midiNoteActive;
	const int  mode           = loadIntParamOrDefault (modeParam, 1);

	juce::Optional<juce::AudioPlayHead::PositionInfo> hostPosition;
	if (auto* currentPlayHead = getPlayHead())
		hostPosition = currentPlayHead->getPosition();
	updateHostTransportMonitor (hostPosition, numSamples);

	const float timeMsValue  = loadAtomicOrDefault (timeMsParam, kTimeMsDefault);
	const float modValue     = loadAtomicOrDefault (modParam, kModDefault);
	const float pitchSemi    = loadAtomicOrDefault (pitchParam, kPitchDefault);
	const float scanPercent  = loadAtomicOrDefault (scanParam, kScanDefault);
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

	// Pitch ratio & scan ratio (targets - smoothed per-sample below)
	currentPitchRatio_   = std::exp2 (pitchSemi / 12.0f);
	currentScanRatio_    = std::exp2 (scanPercent / 100.0f);

	// MOD frequency multiplier (hyperbolic below centre, linear above - same as ECHO-TR)
	// 0.0 -> x0.25, 0.5 -> x1.0, 1.0 -> x4.0
	float modFreqMultiplier;
	if (modValue < 0.5f)
		modFreqMultiplier = 1.0f / (4.0f - 6.0f * modValue);
	else
		modFreqMultiplier = 1.0f + ((modValue - 0.5f) * 6.0f);

	double hostBpm = 120.0;
	if (hostPosition.hasValue() && hostPosition->getBpm().hasValue())
		hostBpm = *hostPosition->getBpm();

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
		targetGrainMs = tempoSyncToMs (timeSyncValue, hostBpm);
	}

	const float maxAllowedMs = syncEnabled ? kTimeMsMaxSync : kTimeMsMax;
	targetGrainMs = juce::jlimit (kTimeMsMin, maxAllowedMs, targetGrainMs);
	float grainLenSamples = (float) currentSampleRate * (targetGrainMs / 1000.0f);
	grainLenSamples = juce::jlimit (kMinGrainSamples, (float) (grainBufferLength - 2), grainLenSamples);

	// Apply MOD multiplier: higher multiplier -> shorter grain -> higher frequency.
	// This is the perceptual event period; BNF splits each event into forward/reverse legs.
	const float effectiveGrainLen = juce::jlimit (kMinGrainSamples, (float) (grainBufferLength - 2),
	                                              grainLenSamples / modFreqMultiplier);
	const double syncedAutoPeriodPpq = (syncEnabled && hostBpm > 0.0 && modFreqMultiplier > 0.0f)
		? juce::jmax (1.0e-9, ((double) targetGrainMs * 0.001) * (hostBpm / 60.0) / (double) modFreqMultiplier)
		: 0.0;
	const float backNForthMaxCellLenSamples = juce::jlimit (kMinGrainSamples, (float) (grainBufferLength - 2),
		(float) currentSampleRate * (tempoSyncToMs (10, hostBpm) * 0.001f) / modFreqMultiplier);

	if (lastEffectiveGrainLenForTransition_ >= kMinGrainSamples)
	{
		const float grainLenScale = juce::jmax (effectiveGrainLen, lastEffectiveGrainLenForTransition_);
		const float grainLenDelta = std::abs (effectiveGrainLen - lastEffectiveGrainLenForTransition_);
		const float changeThreshold = juce::jmax (kGrainSizeChangeMinSamples,
		                                          grainLenScale * kGrainSizeChangeMinRatio);

		if (grainLenDelta >= changeThreshold)
		{
			const float minHoldSamples = (float) currentSampleRate * kGrainSizeTransitionMinSeconds;
			const float maxHoldSamples = (float) currentSampleRate * kGrainSizeTransitionMaxSeconds;
			const int transitionSamples = (int) std::lround (juce::jlimit (minHoldSamples,
			                                                               maxHoldSamples,
			                                                               grainLenScale * kGrainSizeTransitionHoldGrains));
			grainSizeTransitionSamplesRemaining_ = juce::jmax (grainSizeTransitionSamplesRemaining_, transitionSamples);
		}
	}
	lastEffectiveGrainLenForTransition_ = effectiveGrainLen;
	grainSizeTransitionActive_ = grainSizeTransitionSamplesRemaining_ > 0;

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
	if (! midiNoteActive && grainSizeTransitionActive_)
	{
		const float transitionStep = 1.0f - std::exp (-1.0f / ((float) currentSampleRate * kGrainSizeTransitionGlideTauSeconds));
		grainLenGlideStep_ = juce::jmin (grainLenGlideStep_, transitionStep);
	}

	// Snap smoothedGrainLen_ on first use (avoid gliding from zero)
	if (smoothedGrainLen_ < kMinGrainSamples)
		smoothedGrainLen_ = effectiveGrainLen;

	targetGrainLen_ = effectiveGrainLen;

	// SMOOTH: shared taper amount for forward and reverse grain playback.
	const float smoothPct = juce::jlimit (0.0f, 1.0f,
		loadAtomicOrDefault (smoothParam, kSmoothDefault) * 0.01f);
	const float baseGrainSmoothFraction = 0.02f + smoothPct * 0.98f;
	grainSmoothFraction_ = grainSizeTransitionActive_
		? juce::jmax (baseGrainSmoothFraction, kGrainSizeTransitionSmoothFloor)
		: baseGrainSmoothFraction;

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
	if (std::abs (smoothedScanRatio_ - currentScanRatio_) < kSnapEpsilon) smoothedScanRatio_ = currentScanRatio_;

	// Dry passthrough: when neither AUTO nor TRIGGER is active, fade mix to 0
	// so the dry signal passes through instead of silence.
	const float effectiveMixTarget = (!autoEnabled && !triggerEnabled) ? 0.0f : mixValue;

#if GRA_TR_BNF_DETERMINISM_DUMP
	bnfDumpCurrentNumSamples_ = numSamples;
	bnfDumpCurrentSampleInBlock_ = 0;
	bnfDumpMode_ = mode;
	bnfDumpSyncEnabled_ = syncEnabled;
	bnfDumpAutoEnabled_ = autoEnabled;
	bnfDumpTriggerEnabled_ = triggerEnabled;
	bnfDumpReverseEnabled_ = reverseEnabled;
	bnfDumpBackNForthEnabled_ = backNForthEnabled;
	bnfDumpMidiEnabled_ = midiEnabled;
	bnfDumpHostBpm_ = hostBpm;
	bnfDumpTargetGrainMs_ = targetGrainMs;
	bnfDumpModValue_ = modValue;
	bnfDumpBackNForthMaxCellLen_ = backNForthMaxCellLenSamples;
	bnfDumpHasHostSample_ = hostPosition.hasValue() && hostPosition->getTimeInSamples().hasValue();
	bnfDumpHasHostPpq_ = hostPosition.hasValue() && hostPosition->getPpqPosition().hasValue();
	bnfDumpHostSampleAtBlockStart_ = bnfDumpHasHostSample_ ? *hostPosition->getTimeInSamples() : 0;
	bnfDumpPpqAtBlockStart_ = bnfDumpHasHostPpq_ ? *hostPosition->getPpqPosition() : 0.0;
#endif

	bool hostSyncedAutoLaunchAtBlockStart = false;
	const bool canHostAlignSyncedAuto = syncEnabled && autoEnabled && ! triggerEnabled && ! midiNoteActive
		&& syncedAutoPeriodPpq > 0.0
		&& hostPosition.hasValue()
		&& hostPosition->getPpqPosition().hasValue();
	const bool syncedAutoHadPeriod = lastSyncedAutoPeriodPpq_ > 0.0;
	const bool syncedAutoPeriodChanged = canHostAlignSyncedAuto
		&& syncedAutoHadPeriod
		&& std::abs (syncedAutoPeriodPpq - lastSyncedAutoPeriodPpq_)
			> juce::jmax (1.0e-9, lastSyncedAutoPeriodPpq_ * kSyncedAutoPeriodReanchorMinRatio);
	const auto alignSyncedAutoToHost = [&]() -> bool
	{
		if (! canHostAlignSyncedAuto)
			return false;

		double phasePpq = std::fmod (*hostPosition->getPpqPosition(), syncedAutoPeriodPpq);
		if (phasePpq < 0.0)
			phasePpq += syncedAutoPeriodPpq;

		const double ppqPerSample = hostBpm / (60.0 * currentSampleRate);
		const double boundaryTolerancePpq = juce::jmax (1.0e-9,
		                                                ppqPerSample * kHostSyncPhaseBoundaryToleranceSamples);

		if (phasePpq <= boundaryTolerancePpq)
		{
			autoPhaseCounter_ = 0.0f;
			return true;
		}

		const float phaseSamples = (float) ((phasePpq / syncedAutoPeriodPpq)
			* (double) juce::jmax (kMinGrainSamples, smoothedGrainLen_));
		autoPhaseCounter_ = juce::jlimit (0.0f,
		                                  juce::jmax (0.0f, smoothedGrainLen_ - 1.0f),
		                                  phaseSamples);
		return false;
	};
	const auto clearActiveGranularVoices = [&]() noexcept
	{
		for (auto& voice : voiceA_)
			voice = {};
		for (auto& voice : voiceB_)
			voice = {};
	};
	if (hostTransport_.deterministicResetCandidate)
	{
#if GRA_TR_BNF_DETERMINISM_DUMP
		bnfDumpCurrentLaunchReason_ = kBnfDumpReasonNone;
		logBnfDeterminismEvent (BnfDumpEvent::transportReset);
#endif

		resetGranularSchedulersForDeterministicStart (reverseEnabled);
		bool clearedVoicesForSyncedAuto = false;
		if (autoEnabled && backNForthEnabled && ! triggerEnabled)
		{
			clearActiveGranularVoices();
			clearedVoicesForSyncedAuto = true;
		}

		if (canHostAlignSyncedAuto)
		{
			const bool launchAtBlockStart = alignSyncedAutoToHost();
			if (launchAtBlockStart)
				hostSyncedAutoLaunchAtBlockStart = true;
			if (clearedVoicesForSyncedAuto)
				syncedAutoDryFillActive_ = ! launchAtBlockStart;

			lastAutoEnabled_ = autoEnabled;
		}

#if GRA_TR_BNF_DETERMINISM_DUMP
		logBnfDeterminismEvent (BnfDumpEvent::schedulerReset);
#endif
	}

	// Detect TRIGGER edge ------------------------------------------
	const bool triggerEdge = triggerEnabled && !prevTriggerState_;
	prevTriggerState_ = triggerEnabled;

	// Detect AUTO enable edge (launch immediately on enable) ------
	const bool autoJustEnabled = autoEnabled && !lastAutoEnabled_;
	lastAutoEnabled_ = autoEnabled;
	bool suppressImmediateAutoStart = false;
	if (autoJustEnabled && canHostAlignSyncedAuto)
	{
		suppressImmediateAutoStart = true;
		smoothedGrainLen_ = effectiveGrainLen;
		const bool launchAtBlockStart = alignSyncedAutoToHost();
		if (reverseEnabled || backNForthEnabled)
		{
			clearActiveGranularVoices();
			syncedAutoDryFillActive_ = ! launchAtBlockStart;
		}

		if (launchAtBlockStart)
			hostSyncedAutoLaunchAtBlockStart = true;

#if GRA_TR_BNF_DETERMINISM_DUMP
		bnfDumpCurrentLaunchReason_ = kBnfDumpReasonAutoStart;
		logBnfDeterminismEvent (BnfDumpEvent::schedulerReset);
		bnfDumpCurrentLaunchReason_ = kBnfDumpReasonNone;
#endif
	}
	else if (syncedAutoPeriodChanged && ! hostTransport_.deterministicResetCandidate)
	{
		// Changing synced TIME/MOD while AUTO is already running must behave like
		// re-arming AUTO on the host grid, otherwise the old free-running phase
		// leaks into the new division and repeated DAW loops can produce a
		// different first grain.
		smoothedGrainLen_ = effectiveGrainLen;
		const bool launchAtBlockStart = alignSyncedAutoToHost();
		if (reverseEnabled || backNForthEnabled)
		{
			clearActiveGranularVoices();
			syncedAutoDryFillActive_ = ! launchAtBlockStart;
		}

		if (launchAtBlockStart)
			hostSyncedAutoLaunchAtBlockStart = true;

#if GRA_TR_BNF_DETERMINISM_DUMP
		bnfDumpCurrentLaunchReason_ = kBnfDumpReasonAutoPeriod;
		logBnfDeterminismEvent (BnfDumpEvent::schedulerReset);
		bnfDumpCurrentLaunchReason_ = kBnfDumpReasonNone;
#endif
	}
	if (! canHostAlignSyncedAuto || (! reverseEnabled && ! backNForthEnabled))
		syncedAutoDryFillActive_ = false;
	lastSyncedAutoPeriodPpq_ = canHostAlignSyncedAuto ? syncedAutoPeriodPpq : 0.0;

	// Per-sample processing ----------------------------------------
	const int wrapMask = grainBufferLength - 1;
	auto* bufL = grainBuffer.getWritePointer (0);
	auto* bufR = grainBuffer.getWritePointer (1);
	auto* channelL = numChannels > 0 ? buffer.getWritePointer (0) : nullptr;
	auto* channelR = numChannels > 1 ? buffer.getWritePointer (1) : nullptr;

	for (int i = 0; i < numSamples; ++i)
	{
#if GRA_TR_BNF_DETERMINISM_DUMP
		bnfDumpCurrentSampleInBlock_ = i;
#endif

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
		syncedAutoDryFillGain_ += ((syncedAutoDryFillActive_ ? 1.0f : 0.0f) - syncedAutoDryFillGain_) * kGainSmoothStep;
		if (! syncedAutoDryFillActive_ && syncedAutoDryFillGain_ < kSnapEpsilon)
			syncedAutoDryFillGain_ = 0.0f;

		// Smooth pitch & scan ratios (same EMA as gain to avoid abrupt changes)
		smoothedPitchRatio_   += (currentPitchRatio_   - smoothedPitchRatio_)   * kGainSmoothStep;
		smoothedScanRatio_    += (currentScanRatio_    - smoothedScanRatio_)    * kGainSmoothStep;

		// Smooth grain length (velocity-controlled glide when MIDI active)
		smoothedGrainLen_ += (effectiveGrainLen - smoothedGrainLen_) * grainLenGlideStep_;
		if (grainSizeTransitionSamplesRemaining_ > 0)
			--grainSizeTransitionSamplesRemaining_;

		// Capture length from smoothed grain length & scan ratio
		const float captureLen = juce::jlimit (kMinGrainSamples, (float) (grainBufferLength - 2),
		                                       smoothedGrainLen_ / smoothedScanRatio_);
		const float backNForthEventLen = smoothedGrainLen_;
		const int backNForthCellCount = juce::jmax (1, (int) std::ceil (backNForthEventLen / backNForthMaxCellLenSamples));
		const float backNForthCellLen = backNForthEventLen / (float) backNForthCellCount;
		const float launchBackNForthLegLen = backNForthEnabled ? backNForthCellLen * 0.5f : 0.0f;
		const float launchGrainLen = backNForthEnabled ? backNForthEventLen : captureLen;
		const float launchSourceLen = captureLen;

#if GRA_TR_BNF_DETERMINISM_DUMP
		bnfDumpCaptureLen_ = captureLen;
		bnfDumpBackNForthEventLen_ = backNForthEventLen;
		bnfDumpBackNForthCellLen_ = backNForthCellLen;
		bnfDumpLaunchGrainLen_ = launchGrainLen;
		bnfDumpLaunchSourceLen_ = launchSourceLen;
		bnfDumpLaunchLegLen_ = launchBackNForthLegLen;
#endif

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
#if GRA_TR_BNF_DETERMINISM_DUMP
		int launchReason = kBnfDumpReasonNone;
#endif

		if (autoEnabled)
		{
			// Unsynced AUTO starts immediately; synced AUTO follows host phase.
			if (((autoJustEnabled && ! suppressImmediateAutoStart) || hostSyncedAutoLaunchAtBlockStart) && i == 0)
			{
				shouldLaunch = true;
#if GRA_TR_BNF_DETERMINISM_DUMP
				launchReason |= kBnfDumpReasonAutoStart;
#endif
			}

			autoPhaseCounter_ += 1.0f;
			if (autoPhaseCounter_ >= smoothedGrainLen_)
			{
				autoPhaseCounter_ -= smoothedGrainLen_;
				shouldLaunch = true;
#if GRA_TR_BNF_DETERMINISM_DUMP
				launchReason |= kBnfDumpReasonAutoPeriod;
#endif
			}
		}

		// TRIGGER edge: launch on first sample of the block when edge detected
		if (triggerEdge && i == 0)
		{
			shouldLaunch = true;
#if GRA_TR_BNF_DETERMINISM_DUMP
			launchReason |= kBnfDumpReasonTriggerEdge;
#endif
		}

		if (shouldLaunch)
		{
			syncedAutoDryFillActive_ = false;

			const bool launchReverse = reverseEnabled;
			const int decorrelationOffsetSamples = - (int) (launchSourceLen * 0.5f);

#if GRA_TR_BNF_DETERMINISM_DUMP
			bnfDumpCurrentLaunchReason_ = launchReason;
			++bnfDumpLaunchSerial_;
			bnfDumpCurrentAnchorOffsetSamples_ = 0;
			logBnfDeterminismEvent (BnfDumpEvent::launchRequest);
#endif

			if (mode == 0) // MONO: same grain for both channels
			{
				launchNewGrain (0, launchGrainLen, launchSourceLen, launchReverse, backNForthEnabled,
				                0, launchBackNForthLegLen);
				launchNewGrain (1, launchGrainLen, launchSourceLen, launchReverse, backNForthEnabled,
				                0, launchBackNForthLegLen);
				// Sync voice anchors for mono
				voiceA_[1].anchorWritePos = voiceA_[0].anchorWritePos;
			}
			else if (mode == 2) // WIDE: temporal decorrelation + M/S widening
			{
				launchNewGrain (0, launchGrainLen, launchSourceLen, launchReverse, backNForthEnabled,
				                0, launchBackNForthLegLen);
				launchNewGrain (1, launchGrainLen, launchSourceLen, launchReverse, backNForthEnabled,
				                decorrelationOffsetSamples, launchBackNForthLegLen);
			}
			else if (mode == 3) // DUAL: R at x0.5 pitch (octave down) + temporal offset
			{
				launchNewGrain (0, launchGrainLen, launchSourceLen, launchReverse, backNForthEnabled,
				                0, launchBackNForthLegLen);
				launchNewGrain (1, launchGrainLen, backNForthEnabled ? launchSourceLen * 0.5f : launchSourceLen,
				                launchReverse, backNForthEnabled, decorrelationOffsetSamples, launchBackNForthLegLen);
				voiceA_[1].pitchRatio = smoothedPitchRatio_ * 0.5f;
			}
			else // STEREO (default): independent per-channel
			{
				launchNewGrain (0, launchGrainLen, launchSourceLen, launchReverse, backNForthEnabled,
				                0, launchBackNForthLegLen);
				launchNewGrain (1, launchGrainLen, launchSourceLen, launchReverse, backNForthEnabled,
				                0, launchBackNForthLegLen);
			}

#if GRA_TR_BNF_DETERMINISM_DUMP
			bnfDumpCurrentLaunchReason_ = kBnfDumpReasonNone;
			bnfDumpCurrentAnchorOffsetSamples_ = 0;
#endif
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

				// Crossfade: fade-in using the grain's own locked length, so
				// TIME changes after launch do not alter the entry slope mid-grain.
				voiceA_[ch].fadeGain = juce::jmin (1.0f, voiceA_[ch].fadeGain + (1.0f / juce::jmax (1.0f, voiceA_[ch].grainLenSamples * voiceA_[ch].smoothFraction * 0.5f)));
				wet += sample * env * voiceA_[ch].fadeGain;

				// Advance read position (always forward; reverse mapping in readGrainInterpolated)
				voiceA_[ch].readPos += voiceA_[ch].backNForth ? 1.0f : voiceA_[ch].pitchRatio;
				if (voiceA_[ch].readPos >= voiceA_[ch].grainLenSamples || voiceA_[ch].readPos < 0.0f)
				{
					if (autoEnabled || triggerEnabled)
					{
						// Immediate relaunch: prevents silence gaps when pitch > 1
						// or SCAN > 0 causes the grain to end before the auto
						// phase counter triggers the next one.
						const bool launchReverse = reverseEnabled;
						const int relaunchAnchorOffsetSamples = ((mode == 2 || mode == 3) && ch == 1)
							? - (int) (launchSourceLen * 0.5f)
							: 0;
						const float relaunchSourceLen = (mode == 3 && ch == 1 && backNForthEnabled)
							? launchSourceLen * 0.5f
							: launchSourceLen;
#if GRA_TR_BNF_DETERMINISM_DUMP
						bnfDumpCurrentLaunchReason_ = kBnfDumpReasonVoiceEnd;
						++bnfDumpLaunchSerial_;
						bnfDumpCurrentAnchorOffsetSamples_ = relaunchAnchorOffsetSamples;
#endif
						launchNewGrain (ch, launchGrainLen, relaunchSourceLen,
						                launchReverse, backNForthEnabled, relaunchAnchorOffsetSamples,
						                launchBackNForthLegLen);
						// DUAL: R channel plays at x0.5 pitch (octave down) + offset anchor
						if (mode == 3 && ch == 1)
						{
							voiceA_[1].pitchRatio = smoothedPitchRatio_ * 0.5f;
						}
						autoPhaseCounter_ = 0.0f;
#if GRA_TR_BNF_DETERMINISM_DUMP
						bnfDumpCurrentLaunchReason_ = kBnfDumpReasonNone;
						bnfDumpCurrentAnchorOffsetSamples_ = 0;
#endif
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
				voiceB_[ch].fadeGain -= (1.0f / juce::jmax (1.0f, voiceB_[ch].grainLenSamples * voiceB_[ch].smoothFraction * 0.5f));
				if (voiceB_[ch].fadeGain <= 0.0f)
				{
					voiceB_[ch].active = false;
					voiceB_[ch].fadeGain = 0.0f;
				}
				else
				{
					wet += sample * env * voiceB_[ch].fadeGain;
					voiceB_[ch].readPos += voiceB_[ch].backNForth ? 1.0f : voiceB_[ch].pitchRatio;
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

		// Mode Out: MID stays dual-mono, SIDE becomes true stereo (+S / -S)
		if (numChannels >= 2 && modeOutVal != 0)
		{
			const float mono = (wetL + wetR) * 0.5f;
			if (modeOutVal == 1)
			{
				wetL = mono;
				wetR = mono;
			}
			else /* modeOutVal == 2 */
			{
				wetL = mono;
				wetR = -mono;
			}
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
		if (mixMode == 0)
		{
			dG = 1.0f - smoothedMix;
			wG = smoothedMix;
			dG = juce::jlimit (0.0f, 1.0f, dG + (1.0f - dG) * syncedAutoDryFillGain_);
		}
		else
		{
			dG = smoothedDryLevel;
			wG = smoothedWetLevel;
		}
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

#if GRA_TR_BNF_DETERMINISM_DUMP
	bnfDumpProcessedSamples_ += (std::uint64_t) juce::jmax (0, numSamples);
	++bnfDumpBlockIndex_;
#endif
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
		kParamScan, "Scan",
		juce::NormalisableRange<float> (kScanMin, kScanMax, 0.001f, 1.0f), kScanDefault));

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
	params.push_back (std::make_unique<juce::AudioParameterBool> (kParamBackNForth, "Back N Forth", false));

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
