#include "GraV2EditorFactory.h"
#include "GraBackendBindings.h"
#include "GraUiDefinition.h"
#include "../Modulation/GraModulationConfig.h"
#include "../../../TR-Shared/Modulation/UI/TRSimpleModulationWorkspace.h"
#include "../../../TR-Shared/SimpleUIV2/Runtime/SimpleEditorHost.h"

namespace TR::GraUIV2
{
juce::AudioProcessorEditor* createEditor(GRATRAudioProcessor& processor)
{
	std::vector<Modulation::UI::DestinationOption> destinations;
	int telemetryIndex = 0;
	for (const auto& descriptor : GraModulation::destinations())
		destinations.push_back({ descriptor.id, descriptor.group, descriptor.label,
		                         true, {}, telemetryIndex++ });
	auto backend = std::make_unique<GraBackendBindings>(processor);
	auto& modulationBackend = *backend;
	auto modulation = std::make_unique<Modulation::UI::SimpleModulationWorkspace>(
		Modulation::UI::workspaceCallbacks(modulationBackend), std::move(destinations));
	return new SimpleUIV2::SimpleEditorHost(
		processor, definition(), std::move(backend),
		std::move(modulation));
}
}
