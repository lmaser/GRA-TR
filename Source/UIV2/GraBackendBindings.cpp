#include "GraBackendBindings.h"
#include "GraUiDefinition.h"
#include "../Modulation/GraModulationConfig.h"
#include "../../../TR-Shared/Modulation/Integration/TRModulationPresetCodec.h"

#include <cmath>

namespace TR::GraUIV2
{
namespace
{
constexpr const char* midiPortKey = "midiPort";
constexpr const char* midiDelayKey = "midiDelayMs";
constexpr const char* autoDelayKey = "autoDelayMs";
constexpr const char* triggerDelayKey = "triggerDelayMs";
constexpr const char* selectedTaskKey = "uiV2SelectedTask";
constexpr const char* surfaceKey = "uiV2Surface";

float linearMultiplier(float value) noexcept
{
    value = juce::jlimit(0.0f, 1.0f, value);
    return value < 0.5f ? 1.0f / (4.0f - 6.0f * value)
                        : 1.0f + (value - 0.5f) * 6.0f;
}

float sliderFromLinearMultiplier(float multiplier) noexcept
{
    multiplier = juce::jlimit(0.25f, 4.0f, multiplier);
    return multiplier < 1.0f ? (4.0f - 1.0f / multiplier) / 6.0f
                             : 0.5f + (multiplier - 1.0f) / 6.0f;
}

int harmonicStep(float value) noexcept
{
    return juce::jlimit(-8, 8, juce::roundToInt(juce::jlimit(0.0f, 1.0f, value) * 16.0f - 8.0f));
}

float sliderFromHarmonicStep(int step) noexcept
{
	return static_cast<float>(juce::jlimit(-8, 8, step) + 8) / 16.0f;
}

}

GraBackendBindings::GraBackendBindings(GRATRAudioProcessor& processorToUse) noexcept
    : processor(processorToUse)
{
}

juce::AudioProcessorValueTreeState& GraBackendBindings::parameters() const noexcept
{
    return processor.apvts;
}

SimpleUIV2::ParameterSnapshot GraBackendBindings::parameterSnapshot() const
{
    SimpleUIV2::ParameterSnapshot values;
    updateParameterSnapshot(values);
    return values;
}

void GraBackendBindings::updateParameterSnapshot(SimpleUIV2::ParameterSnapshot& values) const
{
    if (values.empty()) values.reserve(definition().parameters.size());
    for (const auto& parameter : definition().parameters)
    {
        if (parameter.domain != SimpleUIV2::StateDomain::musicalParameter) continue;
        if (const auto* raw = processor.apvts.getRawParameterValue(juce::String(parameter.parameterId)))
            values[parameter.parameterId] = static_cast<double>(raw->load(std::memory_order_relaxed));
    }
}

std::optional<juce::String> GraBackendBindings::formatControlValue(
    std::string_view controlId, double value) const
{
    if (controlId == "macro-time")
    {
        const auto midiDisplay = processor.getCurrentTimeDisplay();
        if (midiDisplay.isNotEmpty()) return midiDisplay;
        const auto* sync = processor.apvts.getRawParameterValue(GRATRAudioProcessor::kParamSync);
        if (sync != nullptr && sync->load(std::memory_order_relaxed) > 0.5f)
            if (const auto* parameter = processor.apvts.getParameter(GRATRAudioProcessor::kParamTimeSync))
                return parameter->getText(parameter->convertTo0to1(static_cast<float>(value)), 12);
        return std::nullopt;
    }
    if (controlId != "mod-control") return std::nullopt;
    const auto* harmonic = processor.apvts.getRawParameterValue(GRATRAudioProcessor::kParamModHarm);
    if (harmonic != nullptr && harmonic->load(std::memory_order_relaxed) > 0.5f)
    {
        const int step = harmonicStep(static_cast<float>(value));
        return step > 0 ? "H+" + juce::String(step) : "H" + juce::String(step);
    }
    return "x" + juce::String(linearMultiplier(static_cast<float>(value)), 2);
}

std::optional<double> GraBackendBindings::parseControlValue(
    std::string_view controlId, const juce::String& text) const
{
    if (controlId == "macro-time")
    {
        const auto* sync = processor.apvts.getRawParameterValue(GRATRAudioProcessor::kParamSync);
        if (sync != nullptr && sync->load(std::memory_order_relaxed) > 0.5f)
            if (const auto* parameter = processor.apvts.getParameter(GRATRAudioProcessor::kParamTimeSync))
                return static_cast<double>(parameter->convertFrom0to1(parameter->getValueForText(text)));
        return std::nullopt;
    }
    if (controlId != "mod-control") return std::nullopt;
    const auto numeric = text.retainCharacters("0123456789-+.,").replaceCharacter(',', '.');
    const auto* harmonic = processor.apvts.getRawParameterValue(GRATRAudioProcessor::kParamModHarm);
    if (harmonic != nullptr && harmonic->load(std::memory_order_relaxed) > 0.5f)
        return static_cast<double>(sliderFromHarmonicStep(numeric.getIntValue()));
    return static_cast<double>(sliderFromLinearMultiplier(numeric.getFloatValue()));
}

std::string GraBackendBindings::resolveControlParameter(
    std::string_view controlId, std::string_view declaredParameter) const
{
    if (controlId == "macro-time")
    {
        const auto* sync = processor.apvts.getRawParameterValue(GRATRAudioProcessor::kParamSync);
        if (sync != nullptr && sync->load(std::memory_order_relaxed) > 0.5f)
            return GRATRAudioProcessor::kParamTimeSync;
    }
    return SimpleUIV2::SimpleJuceBackend::resolveControlParameter(controlId, declaredParameter);
}

void GraBackendBindings::prepareForUiRefresh()
{
    grainTelemetry = processor.getGrainTelemetry();
}

std::optional<float> GraBackendBindings::signatureSemanticValue(std::string_view role) const
{
    if (role == "grainLifetime") return grainTelemetry.lifetime;
    if (role == "sourceSpan") return grainTelemetry.sourceSpan;
    if (role == "phaseA") return grainTelemetry.phases[0];
    if (role == "phaseB") return grainTelemetry.phases[1];
    if (role == "phaseC") return grainTelemetry.phases[2];
    if (role == "voiceCount")
        return static_cast<float>(juce::jlimit(0, 3, grainTelemetry.activeVoiceCount)) / 3.0f;
    if (role == "taper") return grainTelemetry.taper;
    if (role == "direction") return grainTelemetry.direction;
    if (role == "pitch") return grainTelemetry.pitch;
    return std::nullopt;
}

float GraBackendBindings::inputMeterPeak() const noexcept { return processor.getInputMeterPeak(); }
float GraBackendBindings::outputMeterPeak() const noexcept { return processor.getOutputMeterPeak(); }

SimpleUIV2::MusicalState GraBackendBindings::readMusicalState() const
{
    SimpleUIV2::MusicalState state;
    state.values.emplace(midiPortKey, static_cast<double>(processor.getMidiChannel()));
    state.values.emplace(midiDelayKey, static_cast<double>(processor.getMidiDelayMs()));
	state.values.emplace(autoDelayKey, static_cast<double>(processor.getAutoDelayMs()));
	state.values.emplace(triggerDelayKey, static_cast<double>(processor.getTriggerDelayMs()));
	Modulation::Integration::writePresetState(state, processor.modulationState());
    return state;
}

SimpleUIV2::MusicalState GraBackendBindings::defaultMusicalState() const
{
    SimpleUIV2::MusicalState state;
    state.values.emplace(midiPortKey, 0.0);
    state.values.emplace(midiDelayKey, 0.0);
	state.values.emplace(autoDelayKey, 0.0);
	state.values.emplace(triggerDelayKey, 0.0);
	Modulation::Integration::writePresetState(state, Modulation::makeDefaultState());
    return state;
}

bool GraBackendBindings::validateMusicalState(const SimpleUIV2::MusicalState& state) const noexcept
{
	const auto marker = state.values.find(Modulation::Integration::presetStateId);
	const bool legacyMarker = marker != state.values.end() && marker->second == 0.0;
	if (state.values.size() != static_cast<std::size_t>(legacyMarker ? 5 : 4)
		|| state.textValues.size() > 1
		|| (!state.textValues.empty()
			&& state.textValues.find(Modulation::Integration::presetStateId)
				== state.textValues.end())) return false;
    const auto channel = state.values.find(midiPortKey);
    if (channel == state.values.end() || !std::isfinite(channel->second)
        || channel->second < 0.0 || channel->second > 16.0
        || std::floor(channel->second) != channel->second) return false;
    for (const auto* key : { midiDelayKey, autoDelayKey, triggerDelayKey })
    {
        const auto item = state.values.find(key);
        if (item == state.values.end() || !std::isfinite(item->second)
            || item->second < 0.0 || item->second > 100.0
            || std::floor(item->second) != item->second) return false;
    }
	Modulation::State modulation;
	return Modulation::Integration::readPresetState(state, modulation);
}

void GraBackendBindings::writeMusicalState(const SimpleUIV2::MusicalState& state)
{
	if (!validateMusicalState(state)) return;
    const auto integer = [&](const char* key, int maximum, const auto& setter)
    {
        if (const auto item = state.values.find(key); item != state.values.end())
            setter(juce::jlimit(0, maximum, static_cast<int>(std::lround(item->second))));
    };
    integer(midiPortKey, 16, [&](int value) { processor.setMidiChannel(value); });
    integer(midiDelayKey, 100, [&](int value) { processor.setMidiDelayMs(value); });
    integer(autoDelayKey, 100, [&](int value) { processor.setAutoDelayMs(value); });
	integer(triggerDelayKey, 100, [&](int value) { processor.setTriggerDelayMs(value); });
	Modulation::State modulation;
	if (Modulation::Integration::readPresetState(state, modulation))
		processor.setModulationState(modulation);
}

SimpleUIV2::UiInstanceState GraBackendBindings::readUiInstanceState() const
{
    SimpleUIV2::UiInstanceState state;
    state.selectedTask = static_cast<SimpleUIV2::TaskId>(juce::jlimit(
        0, 3, static_cast<int>(processor.apvts.state.getProperty(selectedTaskKey, 0))));
    state.surface = static_cast<SimpleUIV2::UiSurface>(juce::jlimit(
        0, 2, static_cast<int>(processor.apvts.state.getProperty(surfaceKey, 0))));
    return state;
}

void GraBackendBindings::writeUiInstanceState(const SimpleUIV2::UiInstanceState& state)
{
    processor.apvts.state.setProperty(selectedTaskKey, static_cast<int>(state.selectedTask), nullptr);
    processor.apvts.state.setProperty(surfaceKey, static_cast<int>(state.surface), nullptr);
}

void GraBackendBindings::setMacroName(int index, const juce::String& name)
{
    if (index < 0 || index >= Modulation::macroCount) return;
    auto mod = processor.modulationState();
    mod.macros[static_cast<std::size_t>(index)].name = name;
    processor.setModulationState(mod);
}

Modulation::State GraBackendBindings::modulationState() const { return processor.modulationState(); }
std::uint64_t GraBackendBindings::modulationStateGeneration() const noexcept { return processor.modulationStateGeneration(); }
std::array<float, Modulation::macroCount> GraBackendBindings::modulationMacroValues() const noexcept { return processor.modulationMacroValues(); }
void GraBackendBindings::setModulationMacroValue(int macro, float value) { processor.setModulationMacroValue(macro, value); }
bool GraBackendBindings::setModulationState(const Modulation::State& state) { return processor.setModulationState(state); }
Modulation::UI::SourceCapabilities GraBackendBindings::modulationSourceCapabilities() const noexcept { return { true }; }
std::vector<Modulation::UI::MotionRecipeOption> GraBackendBindings::modulationRecipeOptions() const
{
    return { { "native-jitter", "NATIVE JITTER" } };
}
bool GraBackendBindings::installModulationRecipe(const juce::String& id, int macro)
{
    if (id != "native-jitter") return false;
    auto* parameter = processor.apvts.getParameter(GRATRAudioProcessor::kParamJitter);
    if (parameter == nullptr) return false;
    const auto nativeAmount = parameter->getValue();
    const auto candidate = GraModulation::makeJitterParityRecipe(
        processor.modulationState(), macro);
    if (!processor.setModulationState(candidate)) return false;
    processor.setModulationMacroValue(macro - 1, nativeAmount);
    parameter->beginChangeGesture();
    parameter->setValueNotifyingHost(parameter->convertTo0to1(0.0f));
    parameter->endChangeGesture();
    return true;
}
Modulation::Runtime::TelemetrySnapshot GraBackendBindings::modulationTelemetry() const noexcept { return processor.modulationTelemetry(); }
}
