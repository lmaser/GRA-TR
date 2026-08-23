#pragma once

#include "../PluginProcessor.h"

namespace TR::GraUIV2
{
juce::AudioProcessorEditor* createEditor(GRATRAudioProcessor& processor);
}
