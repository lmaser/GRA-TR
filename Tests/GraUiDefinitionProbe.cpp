#include "../Source/UIV2/GraUiDefinition.h"

#include <iostream>
#include <set>
#include <stdexcept>

namespace V2 = TR::SimpleUIV2;

namespace
{
void require(bool value, const std::string& message)
{
    if (!value) throw std::runtime_error(message);
}

const V2::SimplePageSpec& page(const V2::SimplePluginDefinition& definition, V2::TaskId task)
{
    for (const auto& candidate : definition.pages)
        if (candidate.taskId == task) return candidate;
    throw std::runtime_error("Missing task page");
}

const V2::SimpleGroupSpec& group(const V2::SimplePageSpec& source, const std::string& id)
{
    for (const auto& candidate : source.groups)
        if (candidate.groupId == id) return candidate;
    throw std::runtime_error("Missing group: " + id);
}

void requireIds(const std::vector<V2::SimpleControlSpec>& controls,
                std::initializer_list<const char*> expected, const std::string& message)
{
    require(controls.size() == expected.size(), message);
    std::size_t index = 0;
    for (const auto* id : expected) require(controls[index++].controlId == id, message);
}
}

int main()
{
    try
    {
        const auto& definition = TR::GraUIV2::definition();
        const auto issues = V2::validateDefinition(definition);
        if (V2::hasValidationErrors(issues))
        {
            for (const auto& issue : issues)
                std::cerr << issue.code << " at " << issue.path << " - " << issue.message << '\n';
            throw std::runtime_error("GRA definition validation failed");
        }

        std::set<std::string> apvts, state, preset, presetState, retired;
        for (const auto& item : definition.parameters)
        {
            if (item.domain == V2::StateDomain::musicalParameter) apvts.insert(item.parameterId);
            if (item.domain == V2::StateDomain::musicalState) state.insert(item.parameterId);
        }
        preset.insert(definition.preset.parameterWhitelist.begin(), definition.preset.parameterWhitelist.end());
        presetState.insert(definition.preset.musicalStateWhitelist.begin(), definition.preset.musicalStateWhitelist.end());
        retired.insert(TR::GraUIV2::retiredUiParameterIds().begin(), TR::GraUIV2::retiredUiParameterIds().end());

		require(apvts.size() == 52, "Expected 52 musical APVTS parameters");
		require(state == std::set<std::string> { "autoDelayMs", "midiDelayMs", "midiPort", "modulation_v1", "triggerDelayMs" },
				"Expected session timing and modulation graph state values");
        require(preset == apvts && presetState == state, "Preset whitelist differs from musical state");
        require(retired.size() == 9, "Expected nine retired UI IDs");
        for (const auto& id : retired)
            require(apvts.count(id) == 0 && preset.count(id) == 0, "Retired UI state leaked into presets");

        requireIds(definition.macros,
                   { "macro-time", "macro-pitch", "macro-scan", "macro-mix" },
                   "Macro order must remain TIME, PITCH, SCAN, MIX");
        require(definition.macros.front().parameterAlternatives == std::vector<std::string> { "time_sync" },
                "TIME must substitute its sync subdivision parameter");
        require(definition.pages.size() == 2
                    && definition.pages[0].label == "MAIN"
                    && definition.pages[1].label == "I/O",
                "GRA must expose exactly MAIN and I/O");
        const auto& main = page(definition, V2::TaskId::core);
        requireIds(group(main, "main-controls").controls,
                   { "mod-control", "smooth-control", "style-control",
                     "reverse-control", "bnf-control",
                     "chaos-filter-control", "chaos-delay-control", "sync-control",
                     "midi-control" },
                   "MAIN order changed");
        requireIds(main.signatureActions, { "auto-control", "trigger-control" },
                   "MAIN signature activation order changed");
        require(main.fixedActions.empty(), "GRA MAIN must not retain a parameter footer");
        const auto& io = page(definition, V2::TaskId::io);
        requireIds(io.fixedActions, { "filter-options-action", "routing-options-action" },
                   "I/O fixed route order changed");
        requireIds(group(io, "io-levels").controls, { "input-control", "output-control" },
                   "INPUT and OUTPUT must remain consecutive");
        requireIds(group(io, "io-image").controls, { "pan-control" }, "I/O image group changed");
        requireIds(group(io, "io-mix").controls, { "mix-mode-control", "dry-level-control" },
                   "I/O mix group changed");
        requireIds(group(io, "io-limiter").controls,
                   { "lim-mode-control", "lim-quality-control", "lim-threshold-control" },
                   "I/O limiter group changed");
        require(definition.signatureModel == V2::SignatureModel::grainRibbon,
                "GRA must use the grain ribbon signature model");
        const std::vector<std::string> expectedSignatureRoles {
            "grainLifetime", "sourceSpan", "phaseA", "phaseB", "phaseC",
            "voiceCount", "taper", "direction", "pitch"
        };
        require(definition.signature.size() == expectedSignatureRoles.size(),
                "GRA grain ribbon must expose all nine semantic roles");
        for (std::size_t index = 0; index < expectedSignatureRoles.size(); ++index)
            require(definition.signature[index].semanticRole == expectedSignatureRoles[index],
                    "GRA grain ribbon semantic role order changed");

		std::cout << "GRA UI V2 definition passed: 52 APVTS + 5 musical state, 9 retired UI IDs excluded.\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "GRA UI V2 definition failed: " << exception.what() << '\n';
        return 1;
    }
}
