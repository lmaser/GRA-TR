#pragma once

#include <JuceHeader.h>
#include "Modulation/GraModulationConfig.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <vector>
#include "../../TR-Shared/SimpleDSP/TRLimiterBank.h"
#include "../../TR-Shared/SimpleDSP/TRTemporalDSP.h"
#include "../../TR-Shared/Modulation/Runtime/TRDualSineSmoothRandom.h"

#ifndef GRA_TR_BNF_DETERMINISM_DUMP
#define GRA_TR_BNF_DETERMINISM_DUMP 0
#endif

class GRATRAudioProcessor : public juce::AudioProcessor
{
public:
	GRATRAudioProcessor();
	~GRATRAudioProcessor() override;

	// Parameter IDs ------------------------------------------------
	static constexpr const char* kParamTimeMs     = "time_ms";
	static constexpr const char* kParamTimeSync   = "time_sync";
	static constexpr const char* kParamMod        = "mod";
	static constexpr const char* kParamModHarm    = "mod_harm";
	static constexpr const char* kParamPitch      = "pitch";
	static constexpr const char* kParamScan       = "scan";
	static constexpr const char* kParamSmooth     = "smooth";
	static constexpr const char* kParamJitter     = "jitter";
	static constexpr const char* kParamMode       = "mode";       // 0=MONO 1=STEREO 2=WIDE 3=DUAL
	static constexpr const char* kParamInput      = "input";
	static constexpr const char* kParamOutput     = "output";
	static constexpr const char* kParamMix        = "mix";
	static constexpr const char* kParamModeIn     = "mode_in";
	static constexpr const char* kParamModeOut    = "mode_out";
	static constexpr const char* kParamSumBus     = "sum_bus";
	static constexpr const char* kParamLimThreshold = "lim_threshold";
	static constexpr const char* kParamLimMode      = "lim_mode";
	static constexpr const char* kParamLimQuality   = "lim_quality";
	static constexpr const char* kParamInvPol       = "inv_pol";
	static constexpr const char* kParamInvStr       = "inv_str";
	static constexpr const char* kParamSync       = "sync";
	static constexpr const char* kParamMidi       = "midi";
	static constexpr const char* kParamAuto       = "auto_grain";
	static constexpr const char* kParamTrigger    = "trigger";
	static constexpr const char* kParamReverse    = "reverse";
	static constexpr const char* kParamBackNForth = "bnf";

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
	static constexpr int   kLimQualityDefault   =   0;
	static constexpr int   kMixModeDefault   = 0;   // 0=INSERT, 1=SEND
	static constexpr float kDryLevelDefault  = 0.0f;
	static constexpr float kWetLevelDefault  = 1.0f;
	static constexpr int   kFilterPosDefault = 0;   // 0=POST, 1=PRE


	// Parameter ranges and defaults --------------------------------
	static constexpr float kTimeMsMin     = 0.01f;
	static constexpr float kTimeMsMax     = 5000.0f;
	static constexpr float kTimeMsMaxSync = 30000.0f;
	static constexpr float kTimeMsDefault = 100.0f;

	static constexpr int kTimeSyncMin     = 0;
	static constexpr int kTimeSyncMax     = 28;
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

	static constexpr float kScanMin     = -100.0f;
	static constexpr float kScanMax     =  100.0f;
	static constexpr float kScanDefault =    0.0f;

	static constexpr float kSmoothMin     = 0.0f;
	static constexpr float kSmoothMax     = 100.0f;
	static constexpr float kSmoothDefault = 25.0f;

	static constexpr float kJitterMin     = 0.0f;
	static constexpr float kJitterMax     = 1.0f;
	static constexpr float kJitterDefault = 0.0f;

	static constexpr float kGainFloorDb  = -144.0f;
	static constexpr float kGainMaxDb    =   24.0f;
	static constexpr float kGainDefaultDb =   0.0f;
	static constexpr float kGainSkew     = 4.4965561056f; // 0 dB at the fader midpoint

	static constexpr float kInputMin     = kGainFloorDb;
	static constexpr float kInputMax     = kGainMaxDb;
	static constexpr float kInputDefault = kGainDefaultDb;

	static constexpr float kOutputMin     = kGainFloorDb;
	static constexpr float kOutputMax     = kGainMaxDb;
	static constexpr float kOutputDefault = kGainDefaultDb;

	static constexpr float kMixMin     = 0.0f;
	static constexpr float kMixMax     = 1.0f;
	static constexpr float kMixDefault = 1.0f;

	// Mode In / Mode Out / Sum Bus
	static constexpr int   kModeInOutDefault = 0;   // 0=L+R  1=M/S  2=MID  3=SIDE
	static constexpr int   kSumBusDefault    = 0;   // 0=ST   1=to M   2=to S
	static constexpr int   kInvPolDefault    = 0;   // 0=NONE  1=WET  2=GLOBAL
	static constexpr int   kInvStrDefault    = 0;   // 0=NONE  1=WET  2=GLOBAL

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

	void setMidiChannel (int channel);
	int  getMidiChannel() const noexcept;
	void setMidiDelayMs (int delayMs);
	int  getMidiDelayMs() const noexcept;
	void setAutoDelayMs (int delayMs);
	int  getAutoDelayMs() const noexcept;
	void setTriggerDelayMs (int delayMs);
	int  getTriggerDelayMs() const noexcept;

	float getInputMeterPeak() const noexcept { return inputMeterPeak_.load (std::memory_order_relaxed); }
	float getOutputMeterPeak() const noexcept { return outputMeterPeak_.load (std::memory_order_relaxed); }

	struct GrainTelemetry
	{
		float lifetime = 0.0f;
		float sourceSpan = 0.0f;
		std::array<float, 3> phases {};
		int activeVoiceCount = 0;
		float taper = 0.0f;
		float direction = 0.0f; // 0 forward, 0.5 reverse, 1 back-and-forth
		float pitch = 0.5f; // normalized effective smoothed pitch, -24..+24 semitones
	};

	GrainTelemetry getGrainTelemetry() const noexcept;

	juce::AudioProcessorValueTreeState apvts;
	static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
	TR::Modulation::State modulationState() const;
	bool setModulationState(const TR::Modulation::State&);
	std::uint64_t modulationStateGeneration() const noexcept;
	std::array<float, TR::Modulation::macroCount> modulationMacroValues() const noexcept;
	void setModulationMacroValue(int macro, float value);
	TR::Modulation::Runtime::TelemetrySnapshot modulationTelemetry() const noexcept;
	bool modulationDestinationValues(juce::StringRef id, float& base,
	                                 float& effective) const noexcept;
	friend struct GraJitterParityTestAccess;

	// Wet-signal HP/LP filter biquad structs
	struct WetFilterBiquadCoeffs { float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f; };
	struct WetFilterBiquadState  { float z1 = 0.0f, z2 = 0.0f; };

private:
	TR::Modulation::Integration::ParameterModulationBridge modulation;
	TR::Modulation::Runtime::MidiEventBuffer modulationMidiEvents;

	struct UiStateKeys
	{
		static constexpr const char* midiPort         = "midiPort";
		static constexpr const char* midiDelayMs      = "midiDelayMs";
		static constexpr const char* autoDelayMs      = "autoDelayMs";
		static constexpr const char* triggerDelayMs   = "triggerDelayMs";
	};

	double currentSampleRate = 0.0;

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
		float sourceLenSamples = 0.0f;    // locked source window length in samples
		float backNForthLegLenSamples = 0.0f;
		float backNForthCellLenSamples = 1.0f;
		float backNForthInvCellLenSamples = 1.0f;
		float backNForthSourceCellLenSamples = 0.0f;
		float readPos          = 0.0f;    // fractional read position within source window
		float playPos          = 0.0f;    // independent output/envelope phase
		float fadeGain         = 0.0f;    // crossfade envelope (0->1 fade-in, 1->0 fade-out)
		float pitchRatio       = 1.0f;    // locked pitch ratio at launch time
		float smoothFraction   = 0.02f;   // locked taper/fade amount at launch time
		float jitterReadBendDepthSamples = 0.0f;
		float jitterReadBendPhase = 0.0f;
		float jitterReadBendPhaseStep = 0.0f;
		int   backNForthCellCount = 1;
		bool  active           = false;
		bool  reverse          = false;   // play this grain backwards
		bool  backNForth       = false;   // ping-pong inside this grain cycle
		bool  fixedOlaWindow  = false;   // AUTO OLA uses a fixed window so SMOOTH cannot bend pitch
	};

	GrainVoice voiceA_[2];     // primary voice per channel
	GrainVoice voiceB_[2];     // crossfade-out voice per channel

	static constexpr int kAutoOlaVoiceCount = 3;
	GrainVoice autoOla_[2][kAutoOlaVoiceCount]; // continuous AUTO retune voices
	bool autoOlaReady_ = false;

	void resetGrainTelemetry() noexcept;
	void publishGrainTelemetry() noexcept;
	float normalizedGrainReadPhase (const GrainVoice& voice) const noexcept;
	float effectiveGrainTaperSamples (const GrainVoice& voice) const noexcept;

	std::atomic<float> telemetryLifetime_ { 0.0f };
	std::atomic<float> telemetrySourceSpan_ { 0.0f };
	std::array<std::atomic<float>, 3> telemetryPhases_ {};
	std::atomic<int> telemetryActiveVoiceCount_ { 0 };
	std::atomic<float> telemetryTaper_ { 0.0f };
	std::atomic<float> telemetryDirection_ { 0.0f };
	std::atomic<float> telemetryPitch_ { 0.5f };

	// Auto-trigger phase accumulator (counts samples until next grain)
	float autoPhaseCounter_   = 0.0f;
	float targetGrainLen_     = 0.0f;     // current target grain length in samples
	float smoothedGrainLen_   = 0.0f;     // EMA-smoothed grain length for MIDI glide
	float grainLenGlideStep_  = 1.0f;     // EMA step (1-coeff) for grain length smoothing
	float lastEffectiveGrainLenForTransition_ = 0.0f;
	double lastSyncedAutoPeriodPpq_ = 0.0;
	int   grainSizeTransitionSamplesRemaining_ = 0;
	bool  grainSizeTransitionActive_ = false;
	bool  prevTriggerState_   = false;    // edge-detect for TRIGGER toggle
	bool  lastTriggerParamOn_ = false;
	bool  delayedTriggerActive_ = false;
	bool  lastAutoEnabled_    = false;
	bool  syncedAutoDryFillActive_ = false;
	float syncedAutoDryFillGain_ = 0.0f;
	int   triggerDelayElapsedSamples_ = 0;

	struct HostTransportMonitor
	{
		juce::int64 lastTimeInSamples = 0;
		double lastPpqPosition = 0.0;
		int lastBlockSamples = 0;
		bool hasLastTimeInSamples = false;
		bool hasLastPpqPosition = false;
		bool wasPlaying = false;
		bool isPlaying = false;
		bool playStarted = false;
		bool sampleDiscontinuity = false;
		bool ppqDiscontinuity = false;
		bool deterministicResetCandidate = false;

		void reset() noexcept { *this = {}; }
	};

	HostTransportMonitor hostTransport_;

#if GRA_TR_BNF_DETERMINISM_DUMP
	enum class BnfDumpEvent : int
	{
		prepare = 1,
		release,
		transportReset,
		schedulerReset,
		launchRequest,
		voiceLaunch,
		voiceRelaunch
	};

	enum BnfDumpLaunchReason
	{
		kBnfDumpReasonNone = 0,
		kBnfDumpReasonAutoStart = 1 << 0,
		kBnfDumpReasonAutoPeriod = 1 << 1,
		kBnfDumpReasonTriggerEdge = 1 << 2,
		kBnfDumpReasonVoiceEnd = 1 << 3
	};

	struct BnfDumpRow
	{
		std::uint64_t eventIndex = 0;
		std::uint64_t blockIndex = 0;
		std::uint64_t streamSample = 0;
		std::uint64_t launchSerial = 0;
		int eventType = 0;
		int reason = 0;
		int sampleInBlock = 0;
		int numSamples = 0;
		int channel = -1;
		int mode = 0;
		int grainBufferWritePos = 0;
		int anchorWritePos = 0;
		int anchorOffsetSamples = 0;
		int cellCount = 0;
		int syncEnabled = 0;
		int autoEnabled = 0;
		int triggerEnabled = 0;
		int reverseEnabled = 0;
		int backNForthEnabled = 0;
		int midiEnabled = 0;
		int hostPlaying = 0;
		int hostPlayStarted = 0;
		int hostSampleDiscontinuity = 0;
		int hostPpqDiscontinuity = 0;
		int deterministicResetCandidate = 0;
		int hasHostSample = 0;
		int hasHostPpq = 0;
		juce::int64 hostSample = 0;
		double hostPpq = 0.0;
		double hostBpm = 0.0;
		float autoPhaseCounter = 0.0f;
		float targetGrainLen = 0.0f;
		float smoothedGrainLen = 0.0f;
		float targetGrainMs = 0.0f;
		float modValue = 0.0f;
		float pitchRatio = 0.0f;
		float scanRatio = 0.0f;
		float smoothFraction = 0.0f;
		float captureLen = 0.0f;
		float bnfEventLen = 0.0f;
		float bnfMaxCellLen = 0.0f;
		float bnfCellLen = 0.0f;
		float launchGrainLen = 0.0f;
		float launchSourceLen = 0.0f;
		float launchLegLen = 0.0f;
		float voiceGrainLen = 0.0f;
		float voiceSourceLen = 0.0f;
		float voiceLegLen = 0.0f;
		float voiceCellLen = 0.0f;
		float voiceSourceCellLen = 0.0f;
		float voiceReadPos = 0.0f;
		float voiceFadeGain = 0.0f;
	};

	static constexpr int kBnfDumpCapacity = 65536;
	std::array<BnfDumpRow, kBnfDumpCapacity> bnfDumpRows_ {};
	std::uint64_t bnfDumpWriteCount_ = 0;
	std::uint64_t bnfDumpBlockIndex_ = 0;
	std::uint64_t bnfDumpProcessedSamples_ = 0;
	std::uint64_t bnfDumpLaunchSerial_ = 0;
	int bnfDumpCurrentSampleInBlock_ = 0;
	int bnfDumpCurrentNumSamples_ = 0;
	int bnfDumpCurrentLaunchReason_ = kBnfDumpReasonNone;
	int bnfDumpCurrentAnchorOffsetSamples_ = 0;
	bool bnfDumpSyncEnabled_ = false;
	bool bnfDumpAutoEnabled_ = false;
	bool bnfDumpTriggerEnabled_ = false;
	bool bnfDumpReverseEnabled_ = false;
	bool bnfDumpBackNForthEnabled_ = false;
	bool bnfDumpMidiEnabled_ = false;
	bool bnfDumpHasHostSample_ = false;
	bool bnfDumpHasHostPpq_ = false;
	juce::int64 bnfDumpHostSampleAtBlockStart_ = 0;
	double bnfDumpPpqAtBlockStart_ = 0.0;
	double bnfDumpHostBpm_ = 0.0;
	float bnfDumpTargetGrainMs_ = 0.0f;
	float bnfDumpModValue_ = 0.0f;
	float bnfDumpCaptureLen_ = 0.0f;
	float bnfDumpBackNForthEventLen_ = 0.0f;
	float bnfDumpBackNForthMaxCellLen_ = 0.0f;
	float bnfDumpBackNForthCellLen_ = 0.0f;
	float bnfDumpLaunchGrainLen_ = 0.0f;
	float bnfDumpLaunchSourceLen_ = 0.0f;
	float bnfDumpLaunchLegLen_ = 0.0f;
	int bnfDumpMode_ = 0;

	void logBnfDeterminismEvent (BnfDumpEvent eventType, int channel = -1,
	                             const GrainVoice* voice = nullptr) noexcept;
	void flushBnfDeterminismDump();
	static const char* getBnfDumpEventName (BnfDumpEvent eventType) noexcept;
#endif

	// Pitch state --------------------------------------------------
	float currentPitchRatio_  = 1.0f;     // 2^(semitones/12) playback rate
	float smoothedPitchRatio_ = 1.0f;     // EMA-smoothed pitch ratio

	// SCAN controls the captured source span inside each grain.
	// +100% halves the capture span, -100% doubles it; pitch remains separate.
	float currentScanRatio_ = 1.0f;  // 2^(scanPercent/100)
	float smoothedScanRatio_ = 1.0f; // EMA-smoothed scan ratio

	// JIT locks a subtle deterministic modulation per grain launch.
	using JitterEngine = TR::Modulation::Runtime::DualSineSmoothRandomState;

	struct JitterMetrics
	{
		float amount = 0.0f;
		float delayMs = 1.0f;
		float shortness = 0.0f;
		float longness = 0.0f;
		float driftRateHz = 0.1f;
		float flutterRateHz = 4.0f;
	};

	struct JitterLaunchValues
	{
		float sourceLenSamples = 0.0f;
		int anchorOffsetSamples = 0;
		float pitchScale = 1.0f;
		float readBendDepthSamples = 0.0f;
		float readBendPhase = 0.0f;
		float readBendPhaseStep = 0.0f;
	};

	JitterEngine jitterSource_[2];
	JitterEngine jitterAnchor_[2];
	JitterEngine jitterPitch_[2];
	JitterEngine jitterReadBend_[2];
	JitterEngine jitterRapid_[2];
	float jitterSourceOut_[2] = {};
	float jitterAnchorOut_[2] = {};
	float jitterPitchOut_[2] = {};
	float jitterReadBendOut_[2] = {};
	float jitterRapidOut_[2] = {};
	float jitterSmoothed_ = kJitterDefault;
	float jitterSmoothStep_ = 0.001f;
	float gainSmoothStep_ = 1.0f;

	static constexpr float kJitterMinDelaySamples = 2.0f;
	static constexpr float kJitterMinDelayMs = 0.05f;
	static constexpr float kJitterShortRefMs = 8.0f;
	static constexpr float kJitterMidRefMs = 500.0f;
	static constexpr float kJitterLongRefMs = 4000.0f;
	static constexpr float kJitterLongnessRefMs = 250.0f;
	static constexpr float kJitterHighStart = 0.55f;
	static constexpr float kJitterHighRange = 0.45f;
	static constexpr float kJitterDriftRateMinHz = 0.03f;
	static constexpr float kJitterDriftRateMaxHz = 2.0f;
	static constexpr float kJitterDriftRateBaseHz = 0.08f;
	static constexpr float kJitterDriftRateTopHz = 1.20f;
	static constexpr float kJitterDriftLongnessDamping = 0.65f;
	static constexpr float kJitterDriftShortnessBoost = 0.10f;
	static constexpr float kJitterFlutterRateMinHz = 2.0f;
	static constexpr float kJitterFlutterRateMaxHz = 7000.0f;
	static constexpr float kJitterFlutterRateBaseHz = 4.0f;
	static constexpr float kJitterFlutterRateTopHz = 130.0f;
	static constexpr float kJitterFlutterRefMs = 250.0f;
	static constexpr float kJitterFlutterDelayPower = 0.90f;
	static constexpr float kJitterFlutterHighBoost = 0.08f;
	static constexpr float kJitterDriftReferenceHz = 0.10f;
	static constexpr float kJitterSlowRateMinHz = 0.01f;
	static constexpr float kJitterSlowRateMaxHz = 8.0f;
	static constexpr float kJitterFastRateMaxHz = 7000.0f;

	void resetJitterEngines() noexcept;
	JitterMetrics makeJitterMetrics (float referenceSamples, float amount) const noexcept;
	float advanceJitterEngine (JitterEngine& engine, float slowRateHz, float fastRateHz,
	                           float fastBlend, float maxSlowRateHz = kJitterSlowRateMaxHz,
	                           float maxFastRateHz = kJitterFastRateMaxHz, float maxBlend = 0.35f) noexcept;
	void advanceJitterEngines (float amount, float referenceSamples) noexcept;
	JitterLaunchValues makeJitterLaunchValues (int ch, int mode, float sourceLenSamples) const noexcept;

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

	// Chaos state (smooth S&H + Drift, per-channel D/G, quadrature F) -
	bool  chaosFilterEnabled_ = false;
	bool  chaosDelayEnabled_  = false;
	bool  chaosStereo_        = false;   // true when mode >= 1 (per-channel D/G)

	// CHS D parameters
	float chaosAmtD_                    = 0.0f;
	float chaosAmtNormD_                = 0.0f;   // cached amtD * 0.01
	float chaosShPeriodD_               = 1.0f;
	float smoothedChaosShPeriodD_       = 1.0f;
	float chaosDelayMaxSamples_         = 0.0f;
	float smoothedChaosDelayMaxSamples_ = 0.0f;
	float chaosGainMaxDb_               = 0.0f;
	float smoothedChaosGainMaxDb_       = 0.0f;
	float chaosDelaySmoothedSamples_[2] = {};
	bool  chaosDelaySmoothReady_[2]     = {};
	float chaosDriveAmtSmoothed_        = 0.0f;
	float chaosDriveSpdSmoothed_        = 0.0f;
	bool  chaosDriveParamSmoothReady_   = false;

	// CHS D smooth S&H + Drift: delay (per-channel for stereo styles)
	float chaosDPrev_[2]         = {};
	float chaosDCurr_[2]         = {};
	float chaosDNext_[2]         = {};
	float chaosDPhase_[2]        = {};
	float chaosDDriftPhase_[2]   = {};
	float chaosDDriftFreqHz_[2]  = {};
	float chaosDOut_[2]          = {};
	juce::Random chaosDRng_[2];

	// CHS D smooth S&H + Drift: gain (per-channel, decorrelated)
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
	float chaosShPeriodF_            = 1.0f;
	float smoothedChaosShPeriodF_    = 1.0f;
	float chaosFilterMaxOct_         = 0.0f;
	float smoothedChaosFilterMaxOct_ = 0.0f;
	float chaosFilterAmtSmoothed_    = 0.0f;
	float chaosFilterSpdSmoothed_    = 0.0f;
	bool  chaosFilterParamSmoothReady_ = false;

	// CHS F smooth S&H + Drift: filter (mono S&H + quadrature drift)
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
	float chaosDelaySmoothStep_          = 0.001f;

	static constexpr int kChaosDelayBufLen = 1024;
	float chaosDelayBuf_[2][kChaosDelayBufLen] = {};
	int   chaosDelayWritePos_ = 0;

	static constexpr float kChaosDriftAmp = 0.3f;
	static constexpr float kTwoPi = 6.283185307f;

	// Pan state ----------------------------------------------------

	// Grain smoothing parameters (live) ----------------------------
	float grainSmoothFraction_ = 0.02f;   // fraction of grain used for fade

	// MIDI state ---------------------------------------------------
	enum class PendingMidiEventType
	{
		noteOn,
		noteOff,
		allNotesOff
	};

	struct PendingMidiEvent
	{
		PendingMidiEventType type = PendingMidiEventType::allNotesOff;
		int note = -1;
		int velocity = 0;
		int samplesRemaining = 0;
	};

	void clearMidiTrackingState() noexcept;
	void clearPendingMidiEvents() noexcept;
	void enqueuePendingMidiEvent (const PendingMidiEvent& event) noexcept;
	void applyPendingMidiEvent (const PendingMidiEvent& event) noexcept;

	static constexpr int kPendingMidiEventCapacity = 256;
	std::atomic<int>   lastMidiNote       { -1 };
	std::atomic<int>   lastMidiVelocity   { 0 };
	std::atomic<float> currentMidiFrequency { 0.0f };
	std::atomic<int>   midiChannel        { 0 };
	std::atomic<int>   midiDelayMs        { 0 };
	std::atomic<int>   autoDelayMs        { 0 };
	std::atomic<int>   triggerDelayMs     { 0 };
	std::array<PendingMidiEvent, kPendingMidiEventCapacity> pendingMidiEvents_ {};
	int pendingMidiEventCount_ = 0;

	// UI state atomics ---------------------------------------------
	std::atomic<float> inputMeterPeak_ { 0.0f };
	std::atomic<float> outputMeterPeak_ { 0.0f };

	// Raw parameter pointers (cached) ------------------------------
	std::atomic<float>* timeMsParam   = nullptr;
	std::atomic<float>* timeSyncParam = nullptr;
	std::atomic<float>* modParam      = nullptr;
	std::atomic<float>* modHarmParam = nullptr;
	std::atomic<float>* pitchParam    = nullptr;
	std::atomic<float>* scanParam     = nullptr;
	std::atomic<float>* smoothParam   = nullptr;
	std::atomic<float>* jitterParam   = nullptr;
	std::atomic<float>* modeParam     = nullptr;
	std::atomic<float>* inputParam    = nullptr;
	std::atomic<float>* outputParam   = nullptr;
	std::atomic<float>* mixParam      = nullptr;
	std::atomic<float>* modeInParam   = nullptr;
	std::atomic<float>* modeOutParam  = nullptr;
	std::atomic<float>* sumBusParam   = nullptr;
	std::atomic<float>* limThresholdParam = nullptr;
	std::atomic<float>* limModeParam     = nullptr;
	std::atomic<float>* limQualityParam  = nullptr;
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
	std::atomic<float>* backNForthParam = nullptr;

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


	// Inline chaos helpers (smooth S&H + Drift) -
	// Generic smooth S&H + Drift chaos engine (per-sample advance)
	inline void advanceChaosEngine (
		float& prev, float& curr, float& next, float& phase,
		float& driftPhase, float& driftFreqHz, float& output,
		juce::Random& rng, float period, float amtNorm, float sr) noexcept
	{
		const float safePeriod = juce::jmax (1.0f, period);
		phase += 1.0f / safePeriod;
		if (phase >= 1.0f)
		{
			phase -= std::floor (phase);
			prev = curr;
			curr = next;
			next = rng.nextFloat() * 2.0f - 1.0f;
			const float driftBase = sr / safePeriod * 0.37f;
			driftFreqHz = driftBase * (0.88f + rng.nextFloat() * 0.24f);
		}
		const float t = juce::jlimit (0.0f, 1.0f, phase);
		const float t2 = t * t;
		const float t3 = t2 * t;
		const float u = t3 * (t * (t * 6.0f - 15.0f) + 10.0f);
		const float shValue = curr + (next - curr) * u;

		driftPhase += driftFreqHz / sr;
		if (driftPhase > 1e6f) driftPhase -= 1e6f;
		const float driftValue = std::sin (driftPhase * kTwoPi) * kChaosDriftAmp;

		const float shWeight = juce::jlimit (0.0f, 1.0f, amtNorm * 1.5f - 0.15f);
		output = driftValue + shValue * shWeight;
	}

	inline void advanceChaosD() noexcept
	{
		const float sr = (float) currentSampleRate;
		const float smoothStep = 1.0f - chaosParamSmoothCoeff_;
		const float targetAmt = juce::jlimit (kChaosAmtMin, kChaosAmtMax, chaosAmtD_);
		const float targetSpd = juce::jlimit (kChaosSpdMin, kChaosSpdMax, sr / juce::jmax (1.0f, chaosShPeriodD_));

		if (! chaosDriveParamSmoothReady_)
		{
			chaosDriveParamSmoothReady_ = true;
			if (chaosDriveSpdSmoothed_ <= 0.0f)
				chaosDriveSpdSmoothed_ = targetSpd;
		}

		chaosDriveAmtSmoothed_ += (targetAmt - chaosDriveAmtSmoothed_) * smoothStep;
		const float spdLog = std::log (juce::jmax (kChaosSpdMin, chaosDriveSpdSmoothed_));
		const float targetSpdLog = std::log (targetSpd);
		chaosDriveSpdSmoothed_ = std::exp (spdLog + (targetSpdLog - spdLog) * smoothStep);

		chaosAmtNormD_ = chaosDriveAmtSmoothed_ * 0.01f;
		smoothedChaosDelayMaxSamples_ = chaosAmtNormD_ * 0.005f * sr;
		smoothedChaosGainMaxDb_ = chaosAmtNormD_ * 1.0f;
		smoothedChaosShPeriodD_ = sr / juce::jmax (kChaosSpdMin, chaosDriveSpdSmoothed_);

		const float period = smoothedChaosShPeriodD_;
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

		// Delay modulation stays mono-linked to avoid mono-sum phaser/comb artifacts.
		// Gain modulation may stay stereo for width when the mode supports it.
		chaosDOut_[1] = chaosDOut_[0];
		if (! chaosStereo_)
			chaosGOut_[1] = chaosGOut_[0];
	}

	inline void advanceChaosF() noexcept
	{
		const float sr       = (float) currentSampleRate;
		const float smoothStep = 1.0f - chaosParamSmoothCoeff_;
		const float targetAmt = juce::jlimit (kChaosAmtMin, kChaosAmtMax, chaosAmtF_);
		const float targetSpd = juce::jlimit (kChaosSpdMin, kChaosSpdMax, sr / juce::jmax (1.0f, chaosShPeriodF_));

		if (! chaosFilterParamSmoothReady_)
		{
			chaosFilterParamSmoothReady_ = true;
			if (chaosFilterSpdSmoothed_ <= 0.0f)
				chaosFilterSpdSmoothed_ = targetSpd;
		}

		chaosFilterAmtSmoothed_ += (targetAmt - chaosFilterAmtSmoothed_) * smoothStep;
		const float spdLog = std::log (juce::jmax (kChaosSpdMin, chaosFilterSpdSmoothed_));
		const float targetSpdLog = std::log (targetSpd);
		chaosFilterSpdSmoothed_ = std::exp (spdLog + (targetSpdLog - spdLog) * smoothStep);

		const float amtNormF = chaosFilterAmtSmoothed_ * 0.01f;
		smoothedChaosFilterMaxOct_ = amtNormF * 2.0f;
		smoothedChaosShPeriodF_ = sr / juce::jmax (kChaosSpdMin, chaosFilterSpdSmoothed_);
		const float period = smoothedChaosShPeriodF_;

		const float safePeriod = juce::jmax (1.0f, period);
		chaosFPhase_ += 1.0f / safePeriod;
		if (chaosFPhase_ >= 1.0f)
		{
			chaosFPhase_ -= std::floor (chaosFPhase_);
			chaosFPrev_ = chaosFCurr_;
			chaosFCurr_ = chaosFNext_;
			chaosFNext_ = chaosFRng_.nextFloat() * 2.0f - 1.0f;
			const float driftBase = sr / safePeriod * 0.37f;
			chaosFDriftFreqHz_ = driftBase * (0.88f + chaosFRng_.nextFloat() * 0.24f);
		}

		const float t = juce::jlimit (0.0f, 1.0f, chaosFPhase_);
		const float t2 = t * t;
		const float t3 = t2 * t;
		const float u = t3 * (t * (t * 6.0f - 15.0f) + 10.0f);
		const float shValue = chaosFCurr_ + (chaosFNext_ - chaosFCurr_) * u;

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
			const float targetDelaySamp = juce::jlimit (0.0f, (float)(kChaosDelayBufLen - 2),
				centerDelay + chaosDOut_[ch] * smoothedChaosDelayMaxSamples_);
			float& delaySamp = chaosDelaySmoothedSamples_[ch];
			if (! chaosDelaySmoothReady_[ch])
			{
				delaySamp = targetDelaySamp;
				chaosDelaySmoothReady_[ch] = true;
			}
			else
			{
				delaySamp += (targetDelaySamp - delaySamp) * chaosDelaySmoothStep_;
			}

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

	TR::DSP::LimiterBank limiterBank_;

	inline void applyLimiterSample (float& sampleL, float& sampleR, float thresholdGain) noexcept
	{
		limiterBank_.fastProcessor().processStereo (sampleL, sampleR, thresholdGain);
	}

	// Grain helpers -----------------------------------------------
	void launchNewGrain (int ch, float grainLenSamples, float sourceLenSamples, bool reverseGrain,
	                     bool backNForthGrain = false, int anchorOffsetSamples = 0,
	                     float backNForthLegLenSamples = 0.0f, float pitchRatioScale = 1.0f,
	                     float readBendDepthSamples = 0.0f, float readBendPhase = 0.0f,
	                     float readBendPhaseStep = 0.0f, bool phaseAlignAuto = false);
	float readGrainInterpolated (const GrainVoice& v, int ch) const;
	float grainEnvelope (const GrainVoice& v) const;
	void resetGranularSchedulersForDeterministicStart (bool reverseEnabled) noexcept;
	void updateHostTransportMonitor (const juce::Optional<juce::AudioPlayHead::PositionInfo>& positionInfo,
	                                 int numSamples) noexcept;

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GRATRAudioProcessor)
};
