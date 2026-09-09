# HELIX Tune

A real-time pitch corrector in the tradition of the classic hardware and
plugin correctors: automatic scale-based correction, and a graphical mode where
individual notes are drawn and edited on a piano roll.

Built with JUCE 8, produced as a **VST3** and a **standalone** app.

---

## What's in it

### Signal path

```
input ──> channel sum ──> YIN pitch detector ──┐
   │                                            │
   │                              scale quantiser / graph note / MIDI note
   │                                            │
   │                                     retune engine
   │                          (speed, humanize, flex-tune, vibrato handling)
   │                                            │
   │                                     vibrato generator
   │                                            │
   └──> PSOLA shifter (per channel) <── pitch ratio + formant ratio
                     │
                     └──> mix against latency-matched dry ──> output
```

### Pitch detection

**YIN** (de Cheveigné & Kawahara, 2002) with an FFT-accelerated difference
function and parabolic interpolation of the period minimum.

The cumulative-mean normalisation step is what makes this usable on real
vocals — plain autocorrelation reports the octave below far too often. Parabolic
interpolation matters just as much: without it the period is quantised to whole
samples, which is a ~13-cent error at A4 and ~50 cents at A5.

Every input-type configuration (window length, τ bounds, FFT size) is built
during `prepareToPlay`, so switching voice range mid-stream never allocates on
the audio thread.

### Pitch shifting

**TD-PSOLA**. Two-period grains are lifted from the input at pitch-synchronous
marks and overlap-added at a different spacing. Because the grain *content* is
never stretched, the spectral envelope survives untouched and only the pulse
rate changes — which is why it sounds like a person singing a different note
rather than a sped-up tape.

- Analysis marks are refined by normalised cross-correlation against the
  previous grain. Marks that drift produce the metallic buzz that gives cheap
  PSOLA implementations away.
- The overlap-add is normalised by the summed window, so output level stays
  flat at any shift ratio.
- Formant motion is reintroduced *deliberately*, by resampling the grain
  content (Catmull–Rom). That single ratio drives both "formant correction off"
  (formants track pitch) and throat-length modelling.

### Correction behaviour

| Control | What it does |
|---|---|
| **Retune Speed** | 0–400 ms. 0 is the hard-snap sound. |
| **Humanize** | Relaxes retune *only on sustained notes*, keeping onsets tight. Uniform slow retune just sounds out of tune; the asymmetry is the point. |
| **Flex-Tune** | Correction strength falls off as the input moves away from a scale tone, so scoops and slides survive while flat sustains still get pulled in. |
| **Natural Vibrato** | Scales the singer's own vibrato, separated from the pitch contour by a 120 ms one-pole. |
| **Targeting Ignores Vibrato** | Picks the target from the vibrato-free contour, so a wide vibrato doesn't flip the target between adjacent scale tones. |
| **Classic Mode** | Faster, harder grab for the same nominal setting — the response of the early hardware. |
| **Transpose / Detune** | ±12 semitones, ±100 cents. |
| **Throat Length** | Scales the vocal tract independently of pitch. |
| **Tracking** | Detector permissiveness, for breathy vs. clean sources. |

### Scales

21 equal-tempered scales (major, the modes, pentatonics, blues, diminished,
Hungarian minor, Hirajoshi, double harmonic, …) plus six **historical
temperaments** implemented as real cent-offset tables — Just Intonation,
Pythagorean, quarter-comma Meantone, Werckmeister III, Kirnberger III and
Vallotti. Those aren't note subsets; the note set is the full chromatic and it's
the *tuning of each degree* that moves.

Every pitch class can be set to **Normal**, **Removed** (never a target) or
**Bypassed** (recognised, but passed through untouched).

### Graph mode

A piano roll showing three layers of pitch on one axis: what was sung, what has
been asked for, and what is coming out.

- **Tools** (keys `1`–`8`): select/move, draw note, draw curve, pitch line,
  split, erase, zoom, pan.
- **Make Notes** turns the captured contour into flat notes at the nearest
  scale pitch. **Make Curves** does the same but keeps each note's original
  shape, so only the centre pitch is corrected.
- Per-note **Retune** and **Vibrato** overrides.
- Undo/redo, rubber-band select, semitone snapping, snap-selection-to-scale.
- The contour is captured continuously, including while Auto mode is on screen.

The audio thread reads notes through a lock-free double-buffered snapshot —
no locks, no allocation, and a torn read is impossible because the buffer being
written is never the one being read.

### Latency

PSOLA needs the input a grain will read *after* the synthesis mark it lands on,
so the delay scales with the longest period in the selected range:

| Input type | Latency @ 44.1 kHz |
|---|---|
| Soprano | ~17 ms |
| Alto / Tenor | ~33 ms |
| Low Male | ~46 ms |
| Instrument | ~55 ms |
| Bass Instrument | ~94 ms |

Reported to the host for delay compensation, and re-reported when the input
type changes. The Mix control crossfades against a dry signal delayed by exactly
this amount, so it's a true crossfade and not a comb filter.

---

## Building

Requires **CMake ≥ 3.22** and **MSVC** (Visual Studio 2022 Build Tools, C++
workload). JUCE is fetched automatically.

Build **outside** the source tree — this project lives in OneDrive, and letting
thousands of object files sync is slow and pointless:

```bash
cmake -S "C:/Users/carlb/OneDrive/Documents/Custom AutoTune" -B "C:/Users/carlb/build/HelixTune" -DCMAKE_BUILD_TYPE=Release
```

```bash
cmake --build "C:/Users/carlb/build/HelixTune" --config Release --parallel
```

Installing is a separate step, because the system VST3 folder needs
administrator rights and a build that fails at the end every time is worse than
an explicit copy:

```bash
powershell -Command "Start-Process powershell -Verb RunAs -ArgumentList '-Command','Copy-Item -LiteralPath \"C:/Users/carlb/build/HelixTune/HelixTune_artefacts/Release/VST3/HELIX Tune.vst3\" -Destination \"C:/Program Files/Common Files/VST3\" -Recurse -Force'"
```

To skip the JUCE download if you already have a checkout:

```bash
cmake -S . -B ../build/HelixTune -DFETCHCONTENT_SOURCE_DIR_JUCE="C:/path/to/JUCE"
```

### Standalone

The `HelixTune_Standalone` target builds a normal app with its own audio device
picker — the fastest way to test changes without loading a DAW.

---

## Verification

Two independent layers, because they catch different things.

### `HelixTuneDspTest` — does it actually tune?

A console target over the same DSP sources. Run it directly, or via `ctest`.
Measures real numbers off real signals; 25 checks, all passing:

| Check | Result |
|---|---|
| f0 detection, 82–880 Hz | within **0.4 cents** |
| PSOLA at ratios 0.75 – 2.0 | within **0.08 cents** |
| PSOLA delay drift at ratio 1.0 | **0 samples** over 100 k |
| 429.9 Hz (40 cents flat) → A440 | **0.09 cents** |
| Transpose +12 | **0.36 cents** |
| Scale targeting, Remove / Bypass, Just Intonation | exact |
| White noise input | no NaN/Inf, output bounded |
| Silence in | silence out |

The tolerances are deliberately tight so a regression fails the build rather
than quietly detuning the plugin. That is not hypothetical — it already caught
one:

> The epoch refinement returned `round(predicted) + delta`, which discards the
> fractional part of the period. At a 200.45-sample period that loses 0.45
> samples per mark, which accumulates into a drifting read offset. A drifting
> delay *is* a pitch shift, so the output sat a constant **−3.9 cents flat at
> every ratio, including 1.0**. The fix interpolates the correlation peak and
> applies it as a measured lag to the previous mark's own fractional position.
> Accuracy went from −3.9 cents to under 0.1.

### `pluginval` — is it a well-behaved plugin?

Passes [pluginval](https://github.com/Tracktion/pluginval) at **strictness 10**,
25 test groups, zero failures: editor open/close while processing, state
save/restore, parameter fuzzing, bus-layout changes, and audio at 44.1/48/96 kHz
across block sizes 64–1024.

Builds clean under MSVC `/W4` with JUCE's recommended warning flags — zero
warnings from project code.

---

## Layout

```
Source/
  PluginProcessor.*        host plumbing, parameters, state, MIDI targets
  PluginEditor.*           header bar, Auto/Graph switching
  DSP/
    PitchDetector.*        YIN + FFT difference function
    PsolaShifter.*         TD-PSOLA, epoch refinement, formant resampling
    ScaleQuantizer.*       scales, temperaments, per-note states
    RetuneEngine.*         retune speed, humanize, flex-tune, vibrato split
    VibratoGenerator.*     LFO with note-triggered onset envelope
    CorrectionEngine.*     hop scheduling, ties the chain together
  Model/
    Parameters.*           APVTS layout
    PitchTrack.*           lock-free analysis handoff + note segmentation
    GraphModel.*           note objects, undo, audio-thread snapshot
  UI/
    FuturisticLookAndFeel.*
    Widgets/               NeonKnob, ScaleKeyboard, PitchScope
    AutoModePanel.*
    Graph/                 PianoRollView, GraphEditorPanel
```
