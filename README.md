# GRA-TR v1.4

<br/><br/>

GRA-TR is a granular audio effect built for texture generation, spectral manipulation, and time-frozen soundscapes.  
It captures audio into a circular buffer and replays it as overlapping grains with independent pitch, formant, and time control, driven by MIDI notes, auto-trigger, or manual trigger.

## Concept

GRA-TR treats granular synthesis as a real-time performance tool. By splitting incoming audio into small grains and replaying them with adjustable pitch and formant ratios, it produces effects ranging from subtle thickening to extreme spectral transformation — all while keeping pitch and timbre independently controllable.

The trigger system offers three modes: AUTO continuously relaunches grains at the rate set by TIME, TRIGGER freezes the buffer and loops whatever was captured (creating infinite sustain from any source), and MIDI overrides grain length to match incoming note pitch — turning the granular engine into a resonator that plays melodies.

Formant control scales the capture window independently from read rate, shifting the spectral center without changing pitch. Combined with reverse mode and per-channel stereo processing, GRA-TR can produce everything from subtle chorusing to alien granular pads.

## Interface

GRA-TR uses a text-based UI with horizontal bar sliders. All controls are visible at once — no pages, tabs, or hidden menus.

- **Bar sliders**: Click and drag horizontally. Right-click for numeric entry (except STYLE, which is slider-only).
- **Toggle buttons**: SYNC, MIDI, AUTO, TRG (trigger), RVS (reverse), ENV GRA. Click to enable/disable.
- **Sub-labels**: Click the text next to MIDI or ENV GRA to open their configuration prompt.
- **Collapsible INPUT/OUTPUT/MIX section**: Click the toggle bar (triangle) at the top of the slider area to swap between main parameters and the INPUT, OUTPUT, MIX controls. The toggle bar stays fixed in place; only the arrow direction changes. State persists across sessions and preset changes.
- **Filter bar**: Visible in the INPUT/OUTPUT/MIX section. Click to open the HP/LP filter configuration prompt with frequency, slope, and enable/disable controls for each filter.
- **Gear icon** (top-right): Opens the info popup with version, credits, and a link to Graphics settings.
- **Graphics popup**: Toggle CRT post-processing effect and switch between default/custom colour palettes.
- **Resize**: Drag the bottom-right corner. Size persists across sessions.

The value column to the right of each slider shows the current state in context:
- TIME shows milliseconds (or MIDI note name when active, or sync division).
- MOD shows the frequency multiplier.
- PITCH shows semitones with +/− sign.
- FORMANT shows semitones with +/− sign.
- STYLE shows MONO/STEREO/WIDE/DUAL.
- INPUT/OUTPUT show dB values.
- TILT shows dB values.
- MIX shows percentage.
- PAN shows L/C/R position.

## Parameters

### TIME (0.01–5000 ms)

Grain length in milliseconds. Controls the size of each grain captured from the circular buffer.  
Overridden by MIDI or SYNC when active.  
Smoothed per-sample via exponential moving average (80 ms time constant) for glitch-free sweeps.

When MIDI is active, TIME shows the note name instead of milliseconds. The grain length maps to `1000 / frequency` ms, so higher notes produce shorter grains.

### MOD (×0.25–×4.0)

Frequency multiplier applied to the grain length.  
0% = ×0.25 (4× longer grains), 50% = ×1.0 (no change), 100% = ×4.0 (4× shorter grains).  
Useful for octave shifting, harmonic tuning, and detuned textures.

### PITCH (−24 to +24 semitones)

Grain read-rate control. Changes how fast each grain is read back, directly affecting perceived pitch.  
+12 st = reads at 2× speed (octave up). −12 st = reads at 0.5× speed (octave down).  
The capture window size stays the same — only the playback speed changes.

### FORMANT (−12 to +12 semitones)

Spectral character control. Scales the capture window size independently from read rate.  
+12 st = captures half the window (brighter, thinner timbre at the same pitch).  
−12 st = captures double the window (warmer, fuller timbre at the same pitch).  
Formant and pitch are independent: pitch changes speed, formant changes spectral content.

### STYLE

Routing topology for the granular engine:
- **MONO**: Single grain stream, summed to both channels.
- **STEREO**: Independent left/right grain streams.
- **WIDE**: Cross-channel grain offset for stereo widening.
- **DUAL**: Independent left/right with the right channel at half the grain length.

### INPUT (−100 to 0 dB)

Pre-processing gain. Controls how much signal enters the grain buffer.

### OUTPUT (−100 to +24 dB)

Post-processing gain. Applied to the wet signal only.

### MIX (0–100%)

Dry/wet balance. 0% = fully dry, 100% = fully wet.  
When neither AUTO nor TRG is active, MIX automatically goes to 0% (dry passthrough).

### HP/LP FILTER

High-pass and low-pass filters applied to the wet signal, accessible via the filter bar in the IO section.

- **HP FREQ (20–20 000 Hz)**: High-pass cutoff frequency.
- **LP FREQ (20–20 000 Hz)**: Low-pass cutoff frequency.
- **HP SLOPE (6 dB / 12 dB / 24 dB)**: High-pass filter slope.
- **LP SLOPE (6 dB / 12 dB / 24 dB)**: Low-pass filter slope.
- **HP / LP toggles**: Enable or disable each filter independently.

Slope modes:
- **6 dB/oct**: Single-pole filter.
- **12 dB/oct**: Second-order Butterworth.
- **24 dB/oct**: Two cascaded second-order Butterworth stages.

### TILT (−6 to +6 dB)

Spectral tilt applied to the wet signal. A first-order symmetric shelf filter pivoted at 1 kHz.  
Positive values boost highs and cut lows; negative values cut highs and boost lows.  
Useful for darkening or brightening the grain output without external EQ.

### PAN (L–C–R)

Stereo pan for the wet signal output.

### SYNC

Locks grain length to DAW tempo. Provides 30 musical subdivisions:  
1/64 through 8/1, each with triplet, normal, and dotted variants.  
Disabled when MIDI is active (MIDI takes priority).

### MIDI

Enables MIDI note control of grain length. Incoming notes set grain length to `1000 / frequency` ms.  
Example: A4 (440 Hz) → 2.27 ms.

**Velocity → Glide**: Note velocity controls the portamento speed between pitch changes.

**MIDI Channel**: Click the channel display to select channel 1–16, or OMNI (all channels).

### AUTO

Enables automatic grain triggering. When active, grains are continuously relaunched at the rate determined by TIME (or MIDI/SYNC).

### TRG (Trigger)

Manual trigger mode. When enabled, the grain buffer is frozen — no new audio is written — and grains loop the captured content indefinitely. This creates a freeze/sustain effect from any audio source.

### RVS (Reverse)

Reverse grain playback. When enabled, each grain is read backward, producing reversed texture output. The buffer capture direction remains forward — only the grain readout is reversed.

### ENV GRA (Envelope Grain)

Envelope-driven grain retriggering. When enabled, input amplitude changes trigger new grains.

**TAU (0–100%)**: Recovery speed — how quickly the envelope resets after a trigger.  
**AMT (0–100%)**: Sensitivity — how much amplitude change is needed to trigger a new grain.

### CHAOS

Micro-variation engine that adds organic randomness to the effect. Two independent chaos targets:

- **CHAOS F (Filter)**: Modulates the HP/LP filter cutoff frequencies when filters are enabled. Creates evolving tonal movement in the grain output.
- **CHAOS D (Delay)**: Modulates the grain timing. Produces drifting, organic variation.

Each chaos target has its own toggle and shares two global controls:

- **AMOUNT (0–100%)**: Modulation depth. Default: 50%.
- **SPEED (0.01–100 Hz)**: Sample-and-hold rate. Default: 5 Hz.

Uses exponential smoothing between random targets for glitch-free transitions.

### LIM THRESHOLD (−36 to 0 dB)

Peak limiter threshold. Sets the ceiling above which the limiter engages.
At 0 dB (default) the limiter acts as a transparent safety net. Lower values compress the signal harder.

### LIM MODE

Limiter insertion point:
- **NONE**: Limiter disabled.
- **WET**: Limiter applied to the wet signal only (after processing, before dry/wet mix).
- **GLOBAL**: Limiter applied to the final output (after output gain and dry/wet mix).

The limiter is a dual-stage transparent peak limiter:
- **Stage 1 (Leveler)**: 2 ms attack, 10 ms release — catches sustained overs.
- **Stage 2 (Brickwall)**: Instant attack, 100 ms release — catches transient peaks.

Stereo-linked gain reduction ensures consistent imaging.

## Technical Details

### DSP Architecture
- **Buffer**: Power-of-2 circular buffer with bitwise AND wrapping. Frozen (stops writing) during TRG mode.
- **Interpolation**: 4-point Hermite cubic on all grain reads.
- **Grain voices**: Dual voice per channel (A = primary fade-in, B = crossfade-out) for click-free transitions.
- **Envelope**: Precomputed 129-point Tukey (raised-cosine) lookup table with linear interpolation. No per-sample trigonometry.
- **Pitch**: Read rate = `2^(semitones/12)`. Grains advance by pitch ratio each sample.
- **Formant**: Capture length = `effectiveGrainLen / 2^(formantSemitones/12)`. Scales capture window independently from pitch.
- **Reverse**: Read position always advances forward; reverse mapping (`grainLen - readPos`) applied in the read function.
- **Smoothing**: One-pole EMA per sample for gain, mix, and grain length.
- **Wet filter**: Biquad HP/LP on the wet signal. Transposed Direct Form II. Coefficients updated once per block.
- **Tilt EQ**: First-order symmetric shelf at 1 kHz. Coefficients cached with tolerance-based update.
- **Chaos**: Sample-and-hold random modulation with exponential smoothing. Per-block coefficient precomputation.
- **Minimum grain**: 4 samples. Minimum taper: 2 samples.

### MIDI Implementation
- Standard A440 tuning: `frequency = 440 × 2^((note − 69) / 12)`.
- Monophonic last-note priority. Note-off falls back to manual TIME knob value.
- Channel filtering: OMNI (0) or specific channel (1–16).
- Priority: MIDI > SYNC > Manual TIME.

## Changelog

### v1.4
- Added dual-stage transparent peak limiter with LIM THRESHOLD (−36 to 0 dB) and LIM MODE (NONE/WET/GLOBAL). Stereo-linked gain reduction with 2 ms/10 ms leveler + instant/100 ms brickwall stages.
