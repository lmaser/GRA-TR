#include "GraModulationConfig.h"

#include "../PluginProcessor.h"
#include "../../../TR-Shared/Modulation/Recipes/TROrganicMotionRecipe.h"
#include "../../../TR-Shared/Modulation/Recipes/TRMotionRecipeUtilities.h"

namespace TR::GraModulation
{
const std::vector<Modulation::Integration::ParameterDestination>& destinations()
{
    static const std::vector<Modulation::Integration::ParameterDestination> result {
        { "core:time", "CORE", "TIME", GRATRAudioProcessor::kParamTimeMs,
          GRATRAudioProcessor::kTimeMsMin, GRATRAudioProcessor::kTimeMsMax, true, 0.02f },
        { "core:mod", "CORE", "MOD", GRATRAudioProcessor::kParamMod,
          GRATRAudioProcessor::kModMin, GRATRAudioProcessor::kModMax, false, 0.02f },
        { "grain:pitch", "GRAIN", "PITCH", GRATRAudioProcessor::kParamPitch,
          GRATRAudioProcessor::kPitchMin, GRATRAudioProcessor::kPitchMax, false, 0.01f },
        { "grain:scan", "GRAIN", "SCAN", GRATRAudioProcessor::kParamScan,
          GRATRAudioProcessor::kScanMin, GRATRAudioProcessor::kScanMax, false, 0.02f },
        { "grain:smooth", "GRAIN", "SMOOTH", GRATRAudioProcessor::kParamSmooth,
          GRATRAudioProcessor::kSmoothMin, GRATRAudioProcessor::kSmoothMax, false, 0.02f },
        { "grain:jitter", "GRAIN", "JITTER", GRATRAudioProcessor::kParamJitter,
          GRATRAudioProcessor::kJitterMin, GRATRAudioProcessor::kJitterMax, false, 0.02f },
        { "core:mix", "CORE", "MIX", GRATRAudioProcessor::kParamMix,
          GRATRAudioProcessor::kMixMin, GRATRAudioProcessor::kMixMax, false, 0.01f },
        { "image:pan", "IMAGE", "PAN", GRATRAudioProcessor::kParamPan,
          GRATRAudioProcessor::kPanMin, GRATRAudioProcessor::kPanMax, false, 0.02f },
        { "motion:jitter-depth", "MOTION", "JITTER DEPTH", "", 0.0f, 1.0f, false, 0.0f,
          Modulation::Runtime::ModulationDomain::sampleControl,
          Modulation::Runtime::SignalRepresentation::control, true, 0.0f },
        { "motion:jitter-source-l", "MOTION", "SOURCE L", "", -1.0f, 1.0f, false, 0.0f,
          Modulation::Runtime::ModulationDomain::sampleControl,
          Modulation::Runtime::SignalRepresentation::control, true, 0.0f, 0 },
        { "motion:jitter-source-r", "MOTION", "SOURCE R", "", -1.0f, 1.0f, false, 0.0f,
          Modulation::Runtime::ModulationDomain::sampleControl,
          Modulation::Runtime::SignalRepresentation::control, true, 0.0f, 1 },
        { "motion:jitter-anchor-l", "MOTION", "ANCHOR L", "", -1.0f, 1.0f, false, 0.0f,
          Modulation::Runtime::ModulationDomain::sampleControl,
          Modulation::Runtime::SignalRepresentation::control, true, 0.0f, 0 },
        { "motion:jitter-anchor-r", "MOTION", "ANCHOR R", "", -1.0f, 1.0f, false, 0.0f,
          Modulation::Runtime::ModulationDomain::sampleControl,
          Modulation::Runtime::SignalRepresentation::control, true, 0.0f, 1 },
        { "motion:jitter-pitch-l", "MOTION", "PITCH L", "", -1.0f, 1.0f, false, 0.0f,
          Modulation::Runtime::ModulationDomain::sampleControl,
          Modulation::Runtime::SignalRepresentation::control, true, 0.0f, 0 },
        { "motion:jitter-pitch-r", "MOTION", "PITCH R", "", -1.0f, 1.0f, false, 0.0f,
          Modulation::Runtime::ModulationDomain::sampleControl,
          Modulation::Runtime::SignalRepresentation::control, true, 0.0f, 1 },
        { "motion:jitter-read-bend-l", "MOTION", "READ BEND L", "", -1.0f, 1.0f, false, 0.0f,
          Modulation::Runtime::ModulationDomain::sampleControl,
          Modulation::Runtime::SignalRepresentation::control, true, 0.0f, 0 },
        { "motion:jitter-read-bend-r", "MOTION", "READ BEND R", "", -1.0f, 1.0f, false, 0.0f,
          Modulation::Runtime::ModulationDomain::sampleControl,
          Modulation::Runtime::SignalRepresentation::control, true, 0.0f, 1 },
        { "motion:jitter-rapid-l", "MOTION", "RAPID L", "", -1.0f, 1.0f, false, 0.0f,
          Modulation::Runtime::ModulationDomain::sampleControl,
          Modulation::Runtime::SignalRepresentation::control, true, 0.0f, 0 },
        { "motion:jitter-rapid-r", "MOTION", "RAPID R", "", -1.0f, 1.0f, false, 0.0f,
          Modulation::Runtime::ModulationDomain::sampleControl,
          Modulation::Runtime::SignalRepresentation::control, true, 0.0f, 1 }
    };
    return result;
}

Modulation::State makeJitterParityRecipe(Modulation::State state, int macroOneBased)
{
    using namespace Modulation::Recipes;
    macroOneBased = juce::jlimit(1, Modulation::macroCount, macroOneBased);
    removeRoutesTo(state, { "motion:jitter-depth", "motion:jitter-source-l",
                            "motion:jitter-source-r", "motion:jitter-anchor-l",
                            "motion:jitter-anchor-r", "motion:jitter-pitch-l",
                            "motion:jitter-pitch-r", "motion:jitter-read-bend-l",
                            "motion:jitter-read-bend-r", "motion:jitter-rapid-l",
                            "motion:jitter-rapid-r" });
    state.macros[static_cast<std::size_t>(macroOneBased - 1)].name = "JITTER DEPTH";
    constexpr std::uint64_t baseSeed = 0x4752414a49543031ull;
    OrganicRandomSourceConfig source { baseSeed + 0x11ull, 0.070f, 0.113f };
    source.rateLaw = Modulation::OrganicRateLaw::adaptivePeriod;
    source.reference = Modulation::MotionRateReference::eventPeriod;
    source.fastRateMultiplier = 0.83f;
    source.maximumFastRateHz = 7000.0f;
    configureOrganicRandomSource(state, 2, macroOneBased, source);
    auto anchor = source;
    anchor.seed = baseSeed + 0x29ull;
    anchor.driftRateAHz = 0.091f;
    anchor.driftRateBHz = 0.157f;
    anchor.fastRateMultiplier = 1.17f;
    configureOrganicRandomSource(state, 3, macroOneBased, anchor);
    auto pitch = source;
    pitch.seed = baseSeed + 0x43ull;
    pitch.driftRateAHz = 0.121f;
    pitch.driftRateBHz = 0.193f;
    pitch.fastRateMultiplier = 1.41f;
    configureOrganicRandomSource(state, 4, macroOneBased, pitch);
    auto readBend = source;
    readBend.seed = baseSeed + 0x5full;
    readBend.driftRateAHz = 0.173f;
    readBend.driftRateBHz = 0.271f;
    readBend.fastRateMultiplier = 2.20f;
    configureOrganicRandomSource(state, 5, macroOneBased, readBend);
    auto rapid = source;
    rapid.seed = baseSeed + 0x7dull;
    rapid.driftRateAHz = 0.337f;
    rapid.driftRateBHz = 0.619f;
    rapid.slowRateMultiplier = 1.35f;
    rapid.fastRateMultiplier = 1.04f;
    rapid.blendLaw = Modulation::OrganicBlendLaw::finalControl;
    rapid.maximumBlend = 0.85f;
    configureOrganicRandomSource(state, 6, macroOneBased, rapid);
    appendMacroDepthRoute(state, macroOneBased, "motion:jitter-depth");
    appendOrganicRoute(state, 2, "motion:jitter-source-l");
    appendOrganicRoute(state, 2, "motion:jitter-source-r");
    appendOrganicRoute(state, 3, "motion:jitter-anchor-l");
    appendOrganicRoute(state, 3, "motion:jitter-anchor-r");
    appendOrganicRoute(state, 4, "motion:jitter-pitch-l");
    appendOrganicRoute(state, 4, "motion:jitter-pitch-r");
    appendOrganicRoute(state, 5, "motion:jitter-read-bend-l");
    appendOrganicRoute(state, 5, "motion:jitter-read-bend-r");
    appendOrganicRoute(state, 6, "motion:jitter-rapid-l");
    appendOrganicRoute(state, 6, "motion:jitter-rapid-r");
    return state;
}
}
