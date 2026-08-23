#pragma once

#include "../../../TR-Shared/Modulation/Integration/TRParameterModulationBridge.h"

#include <vector>

namespace TR::GraModulation
{
enum Destination : int
{
    time = 0,
    modulation,
    pitch,
    scan,
    smooth,
    jitter,
    mix,
    pan,
    jitterDepth,
    jitterSourceL,
    jitterSourceR,
    jitterAnchorL,
    jitterAnchorR,
    jitterPitchL,
    jitterPitchR,
    jitterReadBendL,
    jitterReadBendR,
    jitterRapidL,
    jitterRapidR,
    destinationCount
};

const std::vector<Modulation::Integration::ParameterDestination>& destinations();
Modulation::State makeJitterParityRecipe(Modulation::State, int macroOneBased = 1);
}
