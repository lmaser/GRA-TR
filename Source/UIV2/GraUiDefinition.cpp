#include "GraUiDefinition.h"

#include <utility>

namespace TR::GraUIV2
{
namespace V2 = SimpleUIV2;

namespace
{
std::string tooltipFor(const std::string& parameter, const std::string& label)
{
    const std::pair<const char*, const char*> descriptions[] {
        { "time_ms", "Manual grain length before sync or MIDI control" },
        { "pitch", "Grain playback pitch in semitones" },
        { "scan", "Captured source-span direction and scale" },
        { "mix", "Dry and processed signal balance" },
        { "mod", "Grain-length frequency multiplier" },
        { "mod_harm", "Quantize modulation to harmonic ratios" },
        { "smooth", "Grain taper and overlap depth" },
        { "jitter", "Grain-locked source, anchor, pitch and read-bend variation" },
        { "mode", "Stereo granular topology" },
        { "auto_grain", "Continuously launch overlapping grains" },
        { "trigger", "Capture and sustain a triggered grain" },
        { "reverse", "Play grains backward" },
        { "bnf", "Alternate backward and forward grain legs" },
        { "sync", "Use a tempo-synchronized grain length" },
        { "midi", "Control grain length from MIDI notes" },
        { "input", "Level written into the grain buffer" },
        { "output", "Final wet-path output level" },
        { "pan", "Wet-signal stereo position" },
        { "mix_mode", "Choose insert or send signal flow" },
        { "lim_mode", "Limiter position in the signal path" },
        { "lim_quality", "Limiter processing quality" },
        { "filter_pos", "Filter and tilt position around grain capture" }
    };
    for (const auto& [id, text] : descriptions)
        if (parameter == id) return text;
    if (label == "FILTER OPTIONS") return "Open grain-path filter and tilt controls";
    if (label == "ROUTING") return "Open input, output and polarity routing";
    return label;
}

void addParameter(V2::SimplePluginDefinition& d, std::string id, V2::ParameterAccess access,
                  std::string target = {}, V2::StateDomain domain = V2::StateDomain::musicalParameter,
                  std::string backendJustification = {})
{
    const auto stableId = id;
    d.parameters.push_back({ std::move(id), domain, access, std::move(target),
                             std::move(backendJustification) });
    if (domain == V2::StateDomain::musicalParameter)
        d.preset.parameterWhitelist.push_back(stableId);
    else if (domain == V2::StateDomain::musicalState)
        d.preset.musicalStateWhitelist.push_back(stableId);
}

V2::SimpleControlSpec control(std::string id, std::string parameter, std::string label,
                              V2::ControlRole role = V2::ControlRole::knob)
{
    V2::SimpleControlSpec result;
    result.controlId = std::move(id);
    result.parameterId = std::move(parameter);
    result.label = std::move(label);
    result.role = role;
    result.tooltip = tooltipFor(result.parameterId, result.label);
    return result;
}

V2::SimpleGroupSpec hiddenGroup(std::string id, std::vector<V2::SimpleControlSpec> controls,
                                unsigned depth = 0)
{
    return { std::move(id), {}, std::move(controls), {}, depth, V2::GroupLabelVisibility::hidden };
}

V2::SimpleGroupSpec group(std::string id, std::string label, std::vector<V2::SimpleControlSpec> controls)
{
    return { std::move(id), std::move(label), std::move(controls), {}, 0,
             V2::GroupLabelVisibility::automatic };
}

V2::SimpleControlSpec formatted(V2::SimpleControlSpec result, int decimals, double scale,
                                std::string suffix, double offset = 0.0)
{
    result.valueFormat = { true, decimals, scale, std::move(suffix), offset };
    return result;
}

V2::SimpleControlSpec frequency(V2::SimpleControlSpec result)
{
    result.valueFormat = { true, 1, 1.0, "", 0.0, V2::ValueStyle::frequency };
    return result;
}

V2::SimpleControlSpec delayState(std::string id, std::string parameter)
{
    auto result = formatted(control(std::move(id), std::move(parameter), "DELAY"), 0, 1.0, " ms");
    result.domain = V2::StateDomain::musicalState;
    result.manualRange = { true, 0.0, 100.0, 1.0, 0.0 };
    return result;
}

void addCommonIoParameters(V2::SimplePluginDefinition& d)
{
    for (const auto* id : { "input", "output", "pan", "mix_mode", "dry_level", "wet_level",
                            "lim_mode", "lim_quality", "lim_threshold" })
        addParameter(d, id, V2::ParameterAccess::direct);
    for (const auto* id : { "filter_hp_on", "filter_hp_freq", "filter_hp_slope", "filter_lp_on",
                            "filter_lp_freq", "filter_lp_slope", "tilt", "filter_pos" })
        addParameter(d, id, V2::ParameterAccess::prompt, "filter-options");
    for (const auto* id : { "mode_in", "mode_out", "sum_bus", "inv_pol", "inv_str" })
        addParameter(d, id, V2::ParameterAccess::prompt, "routing-options");
}

V2::SimplePluginDefinition buildDefinition()
{
    V2::SimplePluginDefinition d;
	d.product = { "com.tr.audio.gra", "GRA-TR", "1.4.0", "https://github.com/lmaser/GRA-TR/issues" };
	d.capabilities = { false, true, true, true };
	for (int macro = 1; macro <= 8; ++macro)
	{
		const auto id = "mod_macro_" + std::to_string(macro);
		addParameter(d, id, V2::ParameterAccess::backendOnly, {},
		             V2::StateDomain::musicalParameter,
		             "Automatable Macro value exposed by the shared MACROS workspace.");
		d.preset.missingParameterDefaults.push_back({ id, 0.0 });
	}
	addParameter(d, "modulation_v1", V2::ParameterAccess::backendOnly, {},
	             V2::StateDomain::musicalState,
	             "Macro names, routes, source settings and transfer curves.");
	d.preset.missingMusicalStateDefaults.push_back({ "modulation_v1", 0.0 });

	for (const auto* id : { "time_ms", "pitch", "scan", "mix" })
        addParameter(d, id, V2::ParameterAccess::direct);
    auto time = formatted(control("macro-time", "time_ms", "TIME", V2::ControlRole::macro), 2, 1.0, " ms");
    time.parameterAlternatives = { "time_sync" };
    auto mixMacro = formatted(control("macro-mix", "mix", "MIX", V2::ControlRole::macro),
                              1, 100.0, "%");
    mixMacro.parameterAlternatives = { "wet_level" };
    d.macros = {
        std::move(time),
        formatted(control("macro-pitch", "pitch", "PITCH", V2::ControlRole::macro), 2, 1.0, " st"),
        formatted(control("macro-scan", "scan", "SCAN", V2::ControlRole::macro), 1, 1.0, "%"),
        std::move(mixMacro)
    };

    for (const auto* id : { "mod", "smooth", "mode" })
        addParameter(d, id, V2::ParameterAccess::direct);
    addParameter(d, "jitter", V2::ParameterAccess::backendOnly, {},
                 V2::StateDomain::musicalParameter,
                 "Legacy Jitter parameter retained for presets and host automation; new editing uses a MACROS motion recipe.");
    addParameter(d, "mod_harm", V2::ParameterAccess::prompt, "mod-options");
    auto modulation = control("mod-control", "mod", "MOD");
    modulation.promptId = "mod-options";
    auto style = control("style-control", "mode", "STYLE", V2::ControlRole::choice);
    style.choiceLabels = { "MONO", "STEREO", "WIDE", "DUAL" };
    style.choicePresentation = V2::ChoicePresentation::rail;
    auto smooth = formatted(control("smooth-control", "smooth", "SMOOTH"), 1, 1.0, "%");
    for (const auto* id : { "auto_grain", "trigger", "reverse", "bnf", "chaos", "chaos_d" })
        addParameter(d, id, V2::ParameterAccess::direct);
    addParameter(d, "autoDelayMs", V2::ParameterAccess::prompt, "auto-options", V2::StateDomain::musicalState);
    addParameter(d, "triggerDelayMs", V2::ParameterAccess::prompt, "trigger-options", V2::StateDomain::musicalState);
    for (const auto* id : { "chaos_amt_filter", "chaos_spd_filter" })
        addParameter(d, id, V2::ParameterAccess::inspector, "chaos-filter-inspector");
    for (const auto* id : { "chaos_amt", "chaos_spd" })
        addParameter(d, id, V2::ParameterAccess::inspector, "chaos-delay-inspector");

    auto autoGrain = control("auto-control", "auto_grain", "AUTO", V2::ControlRole::toggle);
    autoGrain.promptId = "auto-options";
    auto trigger = control("trigger-control", "trigger", "TRIGGER", V2::ControlRole::toggle);
    trigger.promptId = "trigger-options";
    auto chaosFilter = control("chaos-filter-control", "chaos", "CHAOS FILTER", V2::ControlRole::toggle);
    chaosFilter.inspectorId = "chaos-filter-inspector";
    auto chaosDelay = control("chaos-delay-control", "chaos_d", "CHAOS DELAY", V2::ControlRole::toggle);
    chaosDelay.inspectorId = "chaos-delay-inspector";
    auto reverse = control("reverse-control", "reverse", "REVERSE", V2::ControlRole::toggle);
    auto backAndForth = control("bnf-control", "bnf", "BACK / FORTH", V2::ControlRole::toggle);

    addParameter(d, "sync", V2::ParameterAccess::direct);
    addParameter(d, "time_sync", V2::ParameterAccess::direct);
    addParameter(d, "midi", V2::ParameterAccess::direct);
    addParameter(d, "midiPort", V2::ParameterAccess::prompt, "midi-options", V2::StateDomain::musicalState);
    addParameter(d, "midiDelayMs", V2::ParameterAccess::prompt, "midi-options", V2::StateDomain::musicalState);
    auto midi = control("midi-control", "midi", "MIDI", V2::ControlRole::toggle);
    midi.promptId = "midi-options";
    auto sync = control("sync-control", "sync", "SYNC", V2::ControlRole::toggle);
    V2::SimplePageSpec main { V2::TaskId::core, "MAIN", {
        hiddenGroup("main-controls", { modulation, smooth, style,
                                        reverse, backAndForth, chaosFilter, chaosDelay, sync, midi })
    } };
    main.signatureActions = { autoGrain, trigger };

    addCommonIoParameters(d);
    auto filterAction = control("filter-options-action", {}, "FILTER OPTIONS", V2::ControlRole::action);
    filterAction.domain = V2::StateDomain::uiInstance;
    filterAction.promptId = "filter-options";
    auto routingAction = V2::makeCanonicalRoutingAction();
    auto input = formatted(control("input-control", "input", "INPUT", V2::ControlRole::fader), 1, 1.0, " dB");
    input.meterSource = V2::MeterSource::input;
    auto output = formatted(control("output-control", "output", "OUTPUT", V2::ControlRole::fader), 1, 1.0, " dB");
    output.meterSource = V2::MeterSource::output;
    V2::SimplePageSpec io { V2::TaskId::io, "I/O", V2::makeCommonIoGroups(input, output) };
    io.fixedActions = { std::move(filterAction), std::move(routingAction) };
    d.pages = { std::move(main), std::move(io) };

    auto filterControls = V2::makeCanonicalFilterStageControls("prompt-filter", {
        "filter_hp_on", "filter_hp_freq", "filter_hp_slope", "filter_lp_on",
        "filter_lp_freq", "filter_lp_slope", "tilt" });
    auto filterPosition = control("prompt-filter-position", "filter_pos", "F / T POSITION", V2::ControlRole::choice);
    filterPosition.choiceLabels = { "POST/POST", "PRE/PRE", "PRE/POST", "POST/PRE" };
    filterControls.push_back(filterPosition);

    d.prompts = {
        { "mod-options", "Modulation", { "mod_harm" }, {
            control("mod-harmonic-control", "mod_harm", "HARMONIC", V2::ControlRole::toggle) } },
        { "auto-options", "Auto capture", { "autoDelayMs" }, {
            delayState("auto-delay-control", "autoDelayMs") } },
        { "trigger-options", "Trigger capture", { "triggerDelayMs" }, {
            delayState("trigger-delay-control", "triggerDelayMs") } },
        V2::makeCanonicalMidiSessionPrompt(),
        { "filter-options", "Filter / Grain Path", {
            "filter_hp_on", "filter_hp_freq", "filter_hp_slope", "filter_lp_on", "filter_lp_freq",
            "filter_lp_slope", "tilt", "filter_pos" }, std::move(filterControls) },
        V2::makeCanonicalRoutingPrompt()
    };

    d.inspectors = {
        { "chaos-filter-inspector", "Chaos filter", { hiddenGroup("chaos-filter-detail", {
            formatted(control("chaos-filter-amount", "chaos_amt_filter", "AMOUNT"), 1, 1.0, "%"),
            frequency(control("chaos-filter-speed", "chaos_spd_filter", "SPEED")) }, 1) } },
        { "chaos-delay-inspector", "Chaos delay", { hiddenGroup("chaos-delay-detail", {
            formatted(control("chaos-delay-amount", "chaos_amt", "AMOUNT"), 1, 1.0, "%"),
            frequency(control("chaos-delay-speed", "chaos_spd", "SPEED")) }, 1) } }
    };

    d.signatureModel = V2::SignatureModel::grainRibbon;
    d.signature = {
        { "grainLifetime", "time_ms" },
        { "sourceSpan", "scan" },
        { "phaseA", "time_ms" },
        { "phaseB", "time_ms" },
        { "phaseC", "time_ms" },
        { "voiceCount", "auto_grain" },
        { "taper", "smooth" },
        { "direction", "reverse" },
        { "pitch", "pitch" }
    };
    return d;
}
}

const V2::SimplePluginDefinition& definition()
{
    static const auto value = buildDefinition();
    return value;
}

const std::vector<std::string>& retiredUiParameterIds()
{
    static const std::vector<std::string> ids {
        "ui_width", "ui_height", "ui_palette", "ui_fx_tail", "ui_io_fx",
        "ui_color0", "ui_color1", "ui_color2", "ui_color3"
    };
    return ids;
}
}
