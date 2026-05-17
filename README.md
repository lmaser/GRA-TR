# GRA-TR v1.4

<br/><br/>

GRA-TR is a granular audio effect built for texture generation, spectral manipulation, and time-frozen soundscapes.
It captures audio into a circular buffer and replays it as overlapping grains with independent pitch, scan, jitter, smoothness, and time control, driven by MIDI notes, auto-trigger, or manual trigger.

## Concept

GRA-TR treats granular synthesis as a real-time performance tool. By splitting incoming audio into small grains and replaying them with adjustable pitch and scan ratio, it produces effects ranging from subtle thickening to extreme spectral transformation - all while keeping pitch and source-span motion independently controllable.

The trigger system offers three modes: AUTO continuously relaunches grains at the rate set by TIME, TRIGGER freezes the buffer and loops whatever was captured (creating infinite sustain from any source), and MIDI overrides grain length to match incoming note pitch - turning the granular engine into a resonator that plays melodies.

SCAN scales the captured source span independently from read rate, changing how much of the internal grain loop is traversed without changing pitch. SMOOTH controls the taper and crossfade depth of each grain in both forward and reverse playback. Reverse and Back N Forth modes can play grains backward or alternate between backward and forward loops. Combined with per-channel stereo processing, GRA-TR can produce everything from subtle chorusing to alien granular pads.

## Interface

GRA-TR uses a text-based UI with horizontal bar sliders. Core controls stay on the main panel, and the IO section toggles between the main view and the extended IO/routing view without opening separate pages or hidden menus.

- **Bar sliders**: Click and drag horizontally. Right-click for numeric entry on continuous controls. STYLE is slider-only, and TIME does not open a generic numeric prompt while SYNC is active.
- **Toggle buttons**: SYNC, MIDI, AUTO, TRG (trigger), RVS (reverse), BNF (Back N Forth). Click to enable/disable.
- **Sub-labels**: Click the text next to MIDI to open the MIDI channel prompt.
- **Collapsible INPUT/OUTPUT/MIX section**: Click the toggle bar (triangle) at the top of the slider area to swap between main parameters and the extended IO/routing controls: INPUT, OUTPUT, TILT, FILTER, PAN, MIX, LIM, MODE IN/OUT, SUM BUS, INV POL, INV STR, MIX MODE, and FILTER POS. The toggle bar stays fixed in place; only the arrow direction changes. State persists across sessions and preset changes.
- **Filter bar**: Visible in the INPUT/OUTPUT/MIX section. Click to open the HP/LP filter configuration prompt with frequency, slope, and enable/disable controls for each filter.
- **Gear icon** (top-right): Opens the info popup with version, credits, and a link to Graphics settings.
- **Graphics popup**: Toggle CRT post-processing effect and switch between default/custom colour palettes.
- **Resize**: Drag the bottom-right corner. Size persists across sessions.

The value column to the right of each slider shows the current state in context:
- TIME shows milliseconds with dynamic precision, seconds for long values, MIDI note name when MIDI is active, or sync division when SYNC is active.
- MOD shows the frequency multiplier.
- PITCH shows semitones with +/- sign and two decimal places.
- SCAN shows percentage with +/- sign and two decimal places.
- JIT shows jitter amount as a percentage.
- SMOOTH shows percentage.
- STYLE shows MONO/STEREO/WIDE/DUAL.
- INPUT/OUTPUT show dB values with 0.0 dB at unity.
- TILT shows dB values.
- MIX shows percentage.
- PAN shows L/C/R position.

## Parameters

### TIME (0.01-5000 ms)

Grain length in milliseconds. Controls the size of each grain captured from the circular buffer.
Overridden by MIDI or SYNC when active.
Smoothed per-sample for glitch-free sweeps. Default manual smoothing is short and responsive; in MIDI mode the glide time becomes velocity-dependent.
The visible readout follows the TR Series timing rule: sub-ms values use 3 decimals, short ms values use 2 decimals, mid ms values use 1 decimal, and long values switch to seconds.

When MIDI is active, TIME shows the note name instead of milliseconds. The grain length maps to `1000 / frequency` ms, so higher notes produce shorter grains.

### MOD (x0.25-x4.0)

Frequency multiplier applied to the grain length.
0% = x0.25 (4x longer grains), 50% = x1.0 (no change), 100% = x4.0 (4x shorter grains).
Useful for octave shifting, harmonic tuning, and detuned textures.

### PITCH (-24.00 to +24.00 semitones)

Grain read-rate control. Changes how fast each grain is read back, directly affecting perceived pitch.
+12 st = reads at 2x speed (octave up). -12 st = reads at 0.5x speed (octave down).
The capture window size stays the same - only the playback speed changes.

### SCAN (-100.00% to +100.00%)

Source-span control. Scales the captured window independently from read rate.
+100% = captures half the source span.
-100% = captures double the source span.
SCAN and PITCH are independent controls: PITCH changes read speed, while SCAN changes the captured source span.

### JIT (0-100%)

Granular jitter engine. Adds deterministic, grain-locked micro-variation to source span, anchor position, pitch, and high-range read motion.

Low and medium values behave like subtle mechanical drift. High values add faster, more unstable motion while preserving the main TIME/SYNC/AUTO scheduler and BNF timing.

At 0%, JIT is neutral and does not alter grain launch values.

### SMOOTH (0-100%)

Controls the taper and crossfade depth of each grain.
Lower values keep entries and exits tighter and more immediate. Higher values lengthen the taper, soften transitions, and increase overlap between grains.

Default: 25%.

SMOOTH applies to both forward and reverse playback. It is locked per grain at launch time, so changing SMOOTH does not reshape grains that are already playing. It does not change pitch or scan ratio directly - it changes how softly each grain fades in and out.

### STYLE

Routing topology for the granular engine:
- **MONO**: Single grain stream, summed to both channels.
- **STEREO**: Independent left/right grain streams.
- **WIDE**: Cross-channel grain offset for stereo widening.
- **DUAL**: Independent left/right with the right channel at half the grain length.

### INPUT (-INF to +24 dB)

Pre-processing gain. Controls how much signal enters the grain buffer.
The fader floor is -144 dB, displayed as -INF; 0 dB is centered on the control.

### OUTPUT (-INF to +24 dB)

Post-processing gain. Applied to the wet signal only.

### MIX (0-100%)

Dry/wet balance. 0% = fully dry, 100% = fully wet.
When neither AUTO nor TRG is active, MIX automatically goes to 0% (dry passthrough).

### MIX MODE

Determines how the output blend is handled:
- **INSERT**: Uses the main MIX control as the dry/wet balance.
- **SEND**: Uses independent **Dry Level** and **Wet Level** controls instead of the single MIX balance.

The SEND levels are smoothed, so fast GUI moves do not step abruptly.

### HP/LP FILTER

High-pass and low-pass filters applied to the wet signal, accessible via the filter bar in the IO section.

- **HP FREQ (20-20 000 Hz)**: High-pass cutoff frequency.
- **LP FREQ (20-20 000 Hz)**: Low-pass cutoff frequency.
- **HP SLOPE (6 dB / 12 dB / 24 dB)**: High-pass filter slope.
- **LP SLOPE (6 dB / 12 dB / 24 dB)**: Low-pass filter slope.
- **HP / LP toggles**: Enable or disable each filter independently.

Slope modes:
- **6 dB/oct**: Single-pole filter.
- **12 dB/oct**: Second-order Butterworth.
- **24 dB/oct**: Two cascaded second-order Butterworth stages.

### TILT (-6 to +6 dB)

Spectral tilt applied to the wet signal. A first-order symmetric shelf filter pivoted at 1 kHz.
Positive values boost highs and cut lows; negative values cut highs and boost lows.
Useful for darkening or brightening the grain output without external EQ.

### FILTER POSITION

Controls whether the HP/LP filter block and the TILT block happen before or after grain capture:
- **F-post T-post**: Filter and tilt both after the granular engine.
- **F-pre T-pre**: Filter and tilt both before grain capture.
- **F-pre T-post**: Filter before grain capture, tilt after the granular engine.
- **F-post T-pre**: Filter after the granular engine, tilt before grain capture.

This changes whether you shape what gets written into the grain buffer or the wet signal that comes back out of it.

### PAN (L-C-R)

Stereo pan for the wet signal output.
The center position remains unity; the pan law only engages when you move away from center.

### MODE IN

Pre-granular input encoding:
- **L+R**: Standard stereo input.
- **MID**: Sum L/R to mono mid before grain capture.
- **SIDE**: Convert L/R difference to mono side before grain capture.

### MODE OUT

Post-granular wet encoding:
- **L+R**: Standard stereo wet output.
- **MID**: Collapse wet output to mono mid.
- **SIDE**: Collapse wet output to mono side.

### SUM BUS

Controls how the wet path is injected into the final output:
- **ST**: Standard stereo wet summing.
- **to M**: Wet signal collapsed to mono mid and summed equally to left and right.
- **to S**: Wet signal collapsed to side and summed as opposite polarity between channels.

### SYNC

Locks grain length to DAW tempo. Provides 30 musical subdivisions:
1/64 through 8/1, each with triplet, normal, and dotted variants.
Disabled when MIDI is active (MIDI takes priority).

### MIDI

Enables MIDI note control of grain length. Incoming notes set grain length to `1000 / frequency` ms.
Example: A4 (440 Hz) -> 2.27 ms.

**Velocity -> Glide**: Note velocity controls the portamento speed between pitch changes.

**MIDI Channel**: Click the channel display to select channel 1-16, or OMNI (all channels).

### AUTO

Enables automatic grain triggering. When active, grains are continuously relaunched at the rate determined by TIME, MIDI, or SYNC.

When SYNC is active and the host provides musical position, AUTO aligns its scheduler to DAW transport/PPQ so loop replay stays deterministic.

### TRG (Trigger)

Manual trigger mode. When enabled, the grain buffer is frozen - no new audio is written - and grains loop the captured content indefinitely. This creates a freeze/sustain effect from any audio source.

### RVS (Reverse)

Reverse grain playback. When enabled, each grain is read backward, producing reversed texture output. The buffer capture direction remains forward - only the grain readout is reversed.

### BNF (Back N Forth)

Back N Forth playback. When enabled, each grain cycle plays as a ping-pong motion: one half in the starting direction and the second half in the opposite direction.

RVS controls the starting direction: with RVS off, BNF starts forward then reverses; with RVS on, BNF starts reverse then returns forward.

For manual TIME and SYNC modes, BNF treats the current TIME/SYNC + MOD period as the event length, then divides long events into internal ping-pong cells up to 1/8-note long. Each cell is split into two equal legs: starting direction first, opposite direction second. If a setting produces eight grain events, BNF still produces eight events; each one contains its own forward+reverse or reverse+forward motion.

SCAN still scales the captured source window, and that window is divided across the internal BNF cells so BNF remains rhythmically consistent while retaining scan control.
BNF also keeps its internal cell boundaries crossfaded and pitch-aware, so high positive or negative pitch settings stay continuous without changing the musical event timing.

### CHAOS

Micro-variation engine that adds organic randomness to the effect. Two independent chaos targets:

- **CHAOS F (Filter)**: Modulates the HP/LP filter cutoff frequencies when filters are enabled. Creates evolving tonal movement in the grain output.
- **CHAOS D (Delay)**: Modulates the grain timing. Produces drifting, organic variation.

Each chaos target has its own toggle and shares two global controls:

- **AMOUNT (0-100%)**: Modulation depth. Default: 50%.
- **SPEED (0.01-100 Hz)**: Random target rate - how often a new random value is generated. Default: 5 Hz.

Uses Hermite cubic interpolation (Catmull-Rom) between random targets with a per-channel quadrature drift LFO for organic, stereo-decorrelated movement.

### LIM (-36 to 0 dB)

Peak limiter threshold. Sets the ceiling above which the limiter engages.
At 0 dB (default) the limiter acts as a transparent safety net. Lower values compress the signal harder.

### LIM MODE

Limiter insertion point:
- **NONE**: Limiter disabled.
- **WET**: Limiter applied to the wet signal only (after processing, before dry/wet mix).
- **GLOBAL**: Limiter applied to the final output (after output gain and dry/wet mix).

The limiter is a dual-stage transparent peak limiter:
- **Stage 1 (Leveler)**: 2 ms attack, 10 ms release - catches sustained overs.
- **Stage 2 (Brickwall)**: Instant attack, 100 ms release - catches transient peaks.

Stereo-linked gain reduction ensures consistent imaging.

### INV POL / INV STR

These controls can be applied either to the wet path or to the final global output:
- **INV POL**: Inverts polarity.
- **INV STR**: Swaps left/right stereo channels.

Modes:
- **NONE**
- **WET**
- **GLOBAL**

## Technical Details

### DSP Architecture
- **Buffer**: Power-of-2 circular buffer with bitwise AND wrapping. Frozen (stops writing) during TRG mode.
- **Interpolation**: 4-point Hermite cubic on all grain reads.
- **Grain voices**: Dual voice per channel (A = primary fade-in, B = crossfade-out) for click-free transitions.
- **Envelope**: Precomputed 129-point Tukey (raised-cosine) lookup table with linear interpolation. No per-sample trigonometry. SMOOTH controls how much of each grain is used as taper and is locked per grain at launch time.
- **Pitch**: Read rate = `2^(semitones/12)`. Grains advance by pitch ratio each sample.
- **Scan**: Capture length = `effectiveGrainLen / 2^(scanPercent/100)`. Scales the captured source span independently from pitch.
- **Reverse**: Read position always advances forward; reverse mapping (`grainLen - 1 - readPos`) is applied in the read function so reverse playback starts on the last valid sample inside the captured grain.
- **Back N Forth**: Direction turns inside each grain event. RVS seeds the initial direction. Long BNF events are subdivided into internal ping-pong cells with a 1/8-note maximum cell length, while SCAN still scales the captured source window used by those cells. Internal BNF cell boundaries are crossfaded, and the captured source span remains pitch-aware to avoid discontinuities at extreme pitch settings.
- **Smoothing**: One-pole EMA per sample for gain, mix, SEND dry/wet, pan, limiter threshold, pitch ratio, scan ratio, and the default manual grain-length path. MIDI grain-length changes use a velocity-dependent glide. Large TIME/MOD/SYNC size transitions apply a temporary minimum grain taper and slower grain-length glide to avoid discontinuities without changing steady-state playback.
- **Wet filter**: Biquad HP/LP on the wet signal. Transposed Direct Form II. Coefficients updated once per block.
- **Tilt EQ**: First-order symmetric shelf at 1 kHz. Coefficients cached with tolerance-based update.
- **Chaos**: Hermite cubic interpolation between random targets with per-channel quadrature drift LFO. Per-block coefficient precomputation.
- **Minimum grain**: 4 samples. Minimum taper: 2 samples.
- **SYNC scheduler**: AUTO can align to host PPQ/BPM on transport start and loop discontinuities for deterministic DAW replay.

### MIDI Implementation
- Standard A440 tuning: `frequency = 440 * 2^((note - 69) / 12)`.
- Monophonic last-note priority. Note-off falls back to the manual TIME knob value.
- Channel filtering: OMNI (0) or specific channel (1-16).
- Priority: MIDI > SYNC > Manual TIME.

## Changelog

### v1.4
- Added dual-stage transparent peak limiter with LIM (-36 to 0 dB) and LIM MODE (NONE/WET/GLOBAL). Stereo-linked gain reduction with 2 ms/10 ms leveler + instant/100 ms brickwall stages.
- Added SEND dry/wet controls and smoothing for SEND dry/wet, pan, and limiter threshold to keep fast GUI moves artifact-free.
- Replaced the old ENV GRA workflow with the main-panel SMOOTH control for grain taper/crossfade shaping in both forward and reverse playback.
- PITCH and SCAN now support two-decimal precision in the GUI and numeric prompt; SCAN is processed internally with 0.001% parameter resolution.
- Added BNF (Back N Forth) mode for deterministic forward/reverse alternation.
- Improved BNF continuity across wide pitch ranges with pitch-aware source spans and internal cell-boundary smoothing.
- SMOOTH now defaults to 25% for safer click-free startup behavior.
- Improved deterministic DAW loop/replay behavior for AUTO + SYNC using host transport/PPQ alignment.
- Grain taper is now locked per launched grain, so SMOOTH automation does not reshape active grains mid-playback.
- Removed release-facing performance dump/debug instrumentation.
- Normalised visible value formatting and prompt suffixes for TIME, dB controls, JIT, SMTH, MIX, LIM, and filter frequency input.
- Normalised the JIT engine around a time-aware deterministic model while preserving grain-locked launch variation.
