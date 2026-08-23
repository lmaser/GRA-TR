#include "../Source/PluginProcessor.h"
#include "../Source/UIV2/GraBackendBindings.h"
#include "../Source/UIV2/GraUiDefinition.h"
#include "../../TR-Shared/Testing/TRPluginCpuBenchmark.h"
#include "../../TR-Shared/Modulation/Tests/TRModulationJourneyAssertions.h"
#include "../../TR-Shared/Modulation/Tests/TRDualSineSmoothRandomAssertions.h"
#include "../../TR-Shared/Modulation/Tests/TRJitterMotionEvidence.h"
#include "../../TR-Shared/Modulation/Tests/TRNativeSidechainBaseline.h"
#include "../../TR-Shared/Modulation/Tests/TRMotionRecipeUiAssertions.h"
#include "../../TR-Shared/SimpleUIV2/Preset/TRPresetManager.h"

#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>

struct GraJitterParityTestAccess
{
    static std::array<float, 11> snapshot(const GRATRAudioProcessor& processor) noexcept
    {
        return { processor.jitterSmoothed_,
                 processor.jitterSourceOut_[0], processor.jitterSourceOut_[1],
                 processor.jitterAnchorOut_[0], processor.jitterAnchorOut_[1],
                 processor.jitterPitchOut_[0], processor.jitterPitchOut_[1],
                 processor.jitterReadBendOut_[0], processor.jitterReadBendOut_[1],
                 processor.jitterRapidOut_[0], processor.jitterRapidOut_[1] };
    }
};

namespace
{
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
juce::Component* findById(juce::Component& parent, const juce::String& id)
{
    if (parent.getComponentID() == id) return &parent;
    for (auto* child : parent.getChildren()) if (auto* found = findById(*child, id)) return found;
    return nullptr;
}
void process(GRATRAudioProcessor& processor, bool noteOn, float sidechainLevel = 0.0f)
{
    constexpr int blockSize = 512;
    juce::AudioBuffer<float> audio(processor.getTotalNumInputChannels(), blockSize);
    for (int sample = 0; sample < blockSize; ++sample)
    {
        const auto value = 0.05f * std::sin(0.01f * static_cast<float>(sample));
        audio.setSample(0, sample, value); audio.setSample(1, sample, value);
        if (audio.getNumChannels() >= 4)
        {
            audio.setSample(2, sample, sidechainLevel);
            audio.setSample(3, sample, sidechainLevel);
        }
    }
    juce::MidiBuffer midi;
    if (noteOn) midi.addEvent(juce::MidiMessage::noteOn(1, 127, static_cast<juce::uint8>(127)), 16);
    processor.processBlock(audio, midi);
    for (int channel = 0; channel < audio.getNumChannels(); ++channel)
        for (int sample = 0; sample < blockSize; ++sample)
            require(std::isfinite(audio.getSample(channel, sample)), "GRA produced non-finite audio");
}

std::vector<float> renderJitterControls(int path, double sampleRate, int blockSize,
                                        int mode, bool automate)
{
    auto processor = std::make_unique<GRATRAudioProcessor>();
    using TR::Modulation::Tests::setNativeBaselineParameter;
    require(setNativeBaselineParameter(processor->apvts, GRATRAudioProcessor::kParamMode,
                                        static_cast<float>(mode))
                && setNativeBaselineParameter(processor->apvts, GRATRAudioProcessor::kParamAuto, 1.0f)
                && setNativeBaselineParameter(processor->apvts, GRATRAudioProcessor::kParamTimeMs, 120.0f)
                && setNativeBaselineParameter(processor->apvts, GRATRAudioProcessor::kParamJitter, 0.0f)
                && setNativeBaselineParameter(processor->apvts, GRATRAudioProcessor::kParamMix, 1.0f),
            "GRA parity parameters rejected");
    if (path == 2)
    {
        const auto recipe = TR::GraModulation::makeJitterParityRecipe(
            TR::Modulation::makeDefaultState());
        require(setNativeBaselineParameter(processor->apvts, "mod_macro_1", 0.0f)
                    && processor->setModulationState(recipe),
                "GRA parity recipe rejected");
    }
    processor->prepareToPlay(sampleRate, blockSize);
    const auto totalSamples = static_cast<int>(sampleRate * (automate ? 8.0 : 4.0));
    std::vector<float> result;
    result.reserve(static_cast<std::size_t>((totalSamples / blockSize + 1) * 11));
    for (int offset = 0; offset < totalSamples; offset += blockSize)
    {
        const auto count = juce::jmin(blockSize, totalSamples - offset);
        const auto segment = static_cast<std::size_t>((offset
            / juce::jmax(1, static_cast<int>(sampleRate * 0.5))) & 3);
        constexpr std::array<float, 4> amounts { 0.2f, 0.6f, 1.0f, 0.35f };
        constexpr std::array<float, 4> times { 8.0f, 120.0f, 1800.0f, 40.0f };
        const auto amount = automate ? amounts[segment] : 0.6f;
        require(setNativeBaselineParameter(processor->apvts,
                    path == 1 ? GRATRAudioProcessor::kParamJitter : "mod_macro_1",
                    amount)
                    && (!automate || setNativeBaselineParameter(
                        processor->apvts, GRATRAudioProcessor::kParamTimeMs, times[segment])),
                "GRA parity automation rejected");
        juce::AudioBuffer<float> block(2, count);
        for (int sample = 0; sample < count; ++sample)
        {
            const auto phase = static_cast<float>((offset + sample) * 0.017);
            block.setSample(0, sample, 0.1f * std::sin(phase));
            block.setSample(1, sample, 0.08f * std::sin(phase * 1.013f));
        }
        juce::MidiBuffer midi;
        processor->processBlock(block, midi);
        const auto snapshot = GraJitterParityTestAccess::snapshot(*processor);
        result.insert(result.end(), snapshot.begin(), snapshot.end());
    }
    return result;
}

bool writeJitterParityMatrix(const juce::File& output, bool automate)
{
    std::ofstream csv(output.getFullPathName().toStdString(), std::ios::trunc);
    csv << "sample_rate_hz,block_size,mode,rms_ratio,correlation,rms_error,max_window_rms_ratio_error,passed\n";
    bool passed = true;
    for (const auto sampleRate : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (const auto blockSize : { 64, 257, 2048 })
            for (int mode = 0; mode < 4; ++mode)
            {
                const auto native = renderJitterControls(1, sampleRate, blockSize, mode, automate);
                const auto matrix = renderJitterControls(2, sampleRate, blockSize, mode, automate);
                const auto skip = static_cast<std::size_t>(juce::jmax(11,
                    static_cast<int>(sampleRate / blockSize) * 11));
                const auto ratio = TR::Modulation::Tests::rmsRatio(native, matrix, skip);
                const auto correlation = TR::Modulation::Tests::correlation(native, matrix, skip);
                const auto error = TR::Modulation::Tests::rmsDifference(native, matrix, skip);
                const auto window = static_cast<std::size_t>(juce::jmax(22,
                    static_cast<int>(sampleRate * 0.25 / blockSize) * 11));
                const auto windowError = TR::Modulation::Tests::maximumWindowedRmsRatioError(
                    native, matrix, skip, window);
                const auto rowPassed = ratio >= 0.97 && ratio <= 1.03
                    && (automate ? windowError <= 0.15
                                 : correlation >= 0.995 && error <= 0.01);
                passed = passed && rowPassed;
                csv << sampleRate << ',' << blockSize << ',' << mode << ',' << ratio << ','
                    << correlation << ',' << error << ',' << windowError << ',' << rowPassed << '\n';
            }
    return csv.good() && passed;
}

bool writeJitterPresetEvidence(const juce::File& output)
{
    require(output.createDirectory(), "GRA Jitter evidence directory unavailable");
    auto processor = std::make_unique<GRATRAudioProcessor>();
    const auto state = TR::GraModulation::makeJitterParityRecipe(
        TR::Modulation::makeDefaultState());
    using TR::Modulation::Tests::setNativeBaselineParameter;
    require(setNativeBaselineParameter(processor->apvts, GRATRAudioProcessor::kParamJitter, 0.0f)
                && setNativeBaselineParameter(processor->apvts, "mod_macro_1", 1.0f)
                && processor->setModulationState(state),
            "GRA Jitter preset state rejected");
    const auto staging = output.getChildFile("preset-staging");
    TR::GraUIV2::GraBackendBindings backend(*processor);
    TR::SimpleUIV2::TRPresetManager manager(TR::GraUIV2::definition(), backend, staging);
    constexpr const char* name = "GRA Jitter MATRIX 100";
    require(manager.saveAs(name, true).wasOk(), "GRA Jitter preset save failed");
    const auto saved = manager.libraryFolder().getChildFile(juce::String(name) + ".trpreset");
    const auto evidence = output.getChildFile(saved.getFileName());
    require(saved.existsAsFile() && saved.copyFileTo(evidence), "GRA Jitter preset copy failed");
    auto restored = std::make_unique<GRATRAudioProcessor>();
    TR::GraUIV2::GraBackendBindings restoredBackend(*restored);
    TR::SimpleUIV2::TRPresetManager restoredManager(
        TR::GraUIV2::definition(), restoredBackend, staging);
    require(restoredManager.load(name).wasOk() && restored->modulationState() == state
                && std::abs(restored->apvts.getRawParameterValue(
                    GRATRAudioProcessor::kParamJitter)->load()) <= 1.0e-7f
                && std::abs(restored->apvts.getRawParameterValue("mod_macro_1")->load()
                            - 1.0f) <= 1.0e-7f,
            "GRA Jitter preset round-trip failed");
    std::ofstream proof(output.getChildFile("preset-verification.csv")
                            .getFullPathName().toStdString(), std::ios::trunc);
    proof << "preset,native_jitter,macro_1,route_count,round_trip\n"
          << name << ",0,1," << state.routes.size() << ",1\n";
    return proof.good();
}

bool writeJitterParityTrace(const juce::File& output)
{
    constexpr int width = 11;
    const auto native = renderJitterControls(1, 48000.0, 257, 1, false);
    const auto matrix = renderJitterControls(2, 48000.0, 257, 1, false);
    std::ofstream csv(output.getFullPathName().toStdString(), std::ios::trunc);
    csv << "frame,signal,native,matrix,error\n";
    constexpr std::array<const char*, width> names { "depth", "source_l", "source_r",
        "anchor_l", "anchor_r", "pitch_l", "pitch_r", "read_bend_l", "read_bend_r",
        "rapid_l", "rapid_r" };
    const auto frameCount = juce::jmin(native.size(), matrix.size()) / width;
    for (std::size_t frame = 0; frame < frameCount; frame += 8)
        for (int signal = 0; signal < width; ++signal)
        {
            const auto index = frame * width + static_cast<std::size_t>(signal);
            csv << frame << ',' << names[static_cast<std::size_t>(signal)] << ','
                << native[index] << ',' << matrix[index] << ','
                << (matrix[index] - native[index]) << '\n';
        }
    return csv.good();
}
}

int main(int argc, char** argv)
{
    try
    {
        juce::ScopedJuceInitialiser_GUI juceInitialiser;
        std::cerr << "GRA dual-sine assertion start\n";
        TR::Modulation::Tests::assertDualSineSmoothRandomExtraction();
        std::cerr << "GRA dual-sine assertion passed\n";
        if (argc == 3 && juce::String(argv[1]) == "--qualify-jitter-host-matrix")
            return writeJitterParityMatrix(juce::File(argv[2]), false) ? 0 : 3;
        if (argc == 3 && juce::String(argv[1]) == "--qualify-jitter-automation")
            return writeJitterParityMatrix(juce::File(argv[2]), true) ? 0 : 4;
        if (argc == 3 && juce::String(argv[1]) == "--trace-jitter-parity")
            return writeJitterParityTrace(juce::File(argv[2])) ? 0 : 5;
        if (argc == 3 && juce::String(argv[1]) == "--export-jitter-motion-evidence")
            return writeJitterPresetEvidence(juce::File(argv[2])) ? 0 : 2;
        {
            auto auditProcessor = std::make_unique<GRATRAudioProcessor>();
            TR::GraUIV2::GraBackendBindings auditBackend(*auditProcessor);
            require(TR::Modulation::Tests::auditMotionRecipeBackend(
                        auditBackend, auditProcessor->apvts,
                        GRATRAudioProcessor::kParamJitter, "native-jitter", 3, 11, 1).passed(),
                    "GRA Jitter recipe UI/backend contract failed");
        }
        auto processor = std::make_unique<GRATRAudioProcessor>();
        require(processor->acceptsMidi(), "GRA does not advertise MIDI input");
        auto layout = processor->getBusesLayout();
        layout.inputBuses.set(1, juce::AudioChannelSet::stereo());
        require(processor->isBusesLayoutSupported(layout) && processor->setBusesLayout(layout),
                "GRA rejected its optional stereo sidechain layout");
        auto monoLayout = layout;
        monoLayout.inputBuses.set(1, juce::AudioChannelSet::mono());
        require(processor->isBusesLayoutSupported(monoLayout),
                "GRA rejected its optional mono sidechain layout");
        processor->prepareToPlay(48000.0, 512);
        auto state = TR::Modulation::makeDefaultState();
        state.midiSources[static_cast<std::size_t>(TR::Modulation::MidiSourceType::note)].smoothingSeconds = 0.0f;
        require(TR::Modulation::appendRoute(state, { 0, 0, true,
            TR::Modulation::SourceId::midi(TR::Modulation::MidiSourceType::note),
            TR::Modulation::Polarity::unipolar, 1.0f, "macro:1", TR::Modulation::SourceId::none(),
            TR::Modulation::Polarity::unipolar, TR::Modulation::makeLinearCurve(), TR::Modulation::makeLinearCurve() }),
            "GRA MIDI to Macro route rejected");
        require(TR::Modulation::appendRoute(state, { 0, 0, true, TR::Modulation::SourceId::macro(1),
            TR::Modulation::Polarity::unipolar, 1.0f, "grain:pitch", TR::Modulation::SourceId::none(),
            TR::Modulation::Polarity::unipolar, TR::Modulation::makeLinearCurve(), TR::Modulation::makeLinearCurve() }),
            "GRA Macro to Pitch route rejected");
        require(TR::Modulation::appendRoute(state, { 0, 0, true,
            TR::Modulation::SourceId::sidechainEnvelope(), TR::Modulation::Polarity::unipolar,
            1.0f, "image:pan", TR::Modulation::SourceId::none(),
            TR::Modulation::Polarity::unipolar, TR::Modulation::makeLinearCurve(),
            TR::Modulation::makeLinearCurve() }), "GRA Sidechain to Pan route rejected");
        require(processor->setModulationState(state), "GRA modulation state rejected");

        TR::GraUIV2::GraBackendBindings backend(*processor);
        const auto presetState = backend.readMusicalState();
        require(backend.validateMusicalState(presetState)
                    && presetState.textValues.count(TR::Modulation::Integration::presetStateId) == 1,
                "GRA internal preset omitted modulation XML");
        require(backend.parameterSnapshot().count("mod_macro_1") == 1,
                "GRA internal preset omitted Macro parameters");

        std::unique_ptr<juce::AudioProcessorEditor> editor(processor->createEditor());
        editor->addToDesktop(juce::ComponentPeer::windowIsTemporary);
        editor->setVisible(true);
        juce::Timer::callPendingTimersSynchronously();
        auto* macrosButton = dynamic_cast<juce::Button*>(findById(*editor, "macros-panel-button"));
        auto* matrixButton = dynamic_cast<juce::Button*>(findById(*editor, "matrix-workspace-button"));
        auto* workspace = findById(*editor, "auxiliary-workspace");
        require(macrosButton != nullptr && matrixButton != nullptr
                    && workspace != nullptr && !workspace->isVisible(),
                "GRA MACROS/MATRIX controls are missing");
        const auto productSize = juce::Point<int> { editor->getWidth(), editor->getHeight() };
        TR::Modulation::Tests::clickButton(*macrosButton);
        auto* compactPanel = findById(*editor, "macro-panel");
        require(compactPanel != nullptr && compactPanel->isShowing()
                    && !workspace->isVisible()
                    && editor->getWidth() == productSize.x + 200
                    && editor->getHeight() == productSize.y,
                "First MACROS click did not open the compact Macro panel");
        TR::Modulation::Tests::clickButton(*matrixButton);
        require(workspace->isVisible() && matrixButton->getToggleState(), "GRA MATRIX workspace did not open");
        require(editor->getWidth() == 1040 && editor->getHeight() == 680,
                "GRA MATRIX workspace did not request its canonical size");
        const auto journey = TR::Modulation::Tests::auditMacroJourney(workspace);
        require(journey.workspaceFound && journey.visible && journey.hasAllMacroCards
                    && journey.hasFocusTargets && journey.containerHasNoFocusRing
                    && journey.nameEditingContract,
                "GRA MATRIX journey has complete cards and control-local focus");
        TR::Modulation::Tests::clickButton(*matrixButton);
        require(compactPanel->isShowing()
                    && editor->getWidth() == productSize.x + 200
                    && editor->getHeight() == productSize.y,
                "GRA MATRIX did not restore the originating MACROS panel");

        process(*processor, true);
        for (int block = 0; block < 32; ++block) process(*processor, false);
        float base = 0.0f, effective = 0.0f;
        require(processor->modulationDestinationValues("grain:pitch", base, effective),
                "GRA destination telemetry unavailable");
        const auto telemetry = processor->modulationTelemetry();
        require(telemetry.destinationCount > 0 && telemetry.sources[1].available
                    && telemetry.sources[1].signalState
                        == TR::Modulation::Runtime::SourceSignalState::silent,
                "GRA connected silent sidechain telemetry is inconsistent");
        require(effective > base + 6.0f, "GRA MIDI Macro route did not reach DSP destination");
        for (int block = 0; block < 32; ++block) process(*processor, false, 0.5f);
        const auto activeSidechain = processor->modulationTelemetry();
        require(activeSidechain.sources[1].signalState
                    == TR::Modulation::Runtime::SourceSignalState::active
                    && activeSidechain.sources[1].value > 0.45f,
                "GRA external sidechain did not drive the shared detector");
        auto jitterRecipe = TR::GraModulation::makeJitterParityRecipe(
            TR::Modulation::makeDefaultState());
        require(jitterRecipe.routes.size() == 11,
                "GRA Jitter parity recipe topology is incomplete");
        require(TR::Modulation::Tests::setNativeBaselineParameter(
                    processor->apvts, GRATRAudioProcessor::kParamJitter, 0.0f)
                    && TR::Modulation::Tests::setNativeBaselineParameter(
                        processor->apvts, "mod_macro_1", 1.0f)
                    && processor->setModulationState(jitterRecipe),
                "GRA Jitter parity recipe was rejected");
        for (int block = 0; block < 32; ++block) process(*processor, false);
        require(processor->modulationDestinationValues(
                    "motion:jitter-depth", base, effective)
                    && base == 0.0f && effective > 0.95f,
                "GRA Jitter parity depth did not reach the internal adapter");
        require(processor->modulationDestinationValues(
                    "motion:jitter-pitch-l", base, effective)
                    && std::isfinite(effective) && std::abs(effective) > 1.0e-5f,
                "GRA Organic Random source did not reach the pitch adapter");
        const auto encodedJitterRecipe = TR::Modulation::encodeState(jitterRecipe);
        require(encodedJitterRecipe.has_value(),
                "GRA schema-11 Jitter recipe could not be encoded");
        const auto decodedJitterRecipe = TR::Modulation::decodeState(*encodedJitterRecipe);
        require(decodedJitterRecipe.ok && decodedJitterRecipe.state == jitterRecipe,
                "GRA schema-11 Jitter recipe did not round-trip exactly");
        require(processor->setModulationState(state),
                "GRA could not restore its main smoke state after Jitter proof");
        require(TR::Testing::writePluginCpuComparison (std::cout, "GRA", *processor),
                "GRA CPU comparison could not restore modulation state");

        juce::MemoryBlock preset; processor->getStateInformation(preset); editor.reset();
        auto restored = std::make_unique<GRATRAudioProcessor>();
        restored->setStateInformation(preset.getData(), static_cast<int>(preset.getSize()));
        require(restored->modulationState().routes.size() == 3, "GRA routes did not survive preset round-trip");
        std::cout << "GRA modulation smoke probe passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "GRA modulation smoke probe failed: " << error.what() << '\n'; return 1;
    }
}
