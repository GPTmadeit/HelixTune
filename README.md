# HELIX Tune

A real-time pitch corrector in the tradition of the classic hardware and
plugin correctors: automatic scale-based correction, a graphical mode where
individual notes are drawn and edited on a piano roll, automatic key detection,
and a four-voice diatonic harmoniser.

Built with JUCE 8, produced as a **VST3** and a **standalone** app.

---

## Signal path

```
input ──> channel sum ──> YIN detector ──> candidate lattice
   │                                             │
   │                                    Viterbi pitch tracker
   │                                    ├──> Auto-Key (chroma / K-S profiles)
   │                                    └──> consonant guard
   │                                             │
   │                        scale quantiser / graph note / MIDI note
   │                                             │
   │                                      retune engine
   │                          (speed, humanize, flex-tune, vibrato handling)
   │                                             │
   │                                      vibrato generator
   │                                             │
   ├──> PSOLA (per channel) ──> LPC formant filter ──┐
   │         pitch only            envelope only     │
   │                                                 │
   └──> 4x PSOLA harmony voices ─────────────────────┤
                                                     │
              latency-matched dry ──> mix ──> output ┘
```

---

## Pitch detection

**YIN** (de Cheveigné & Kawahara, 2002) with an FFT-accelerated difference
function and parabolic interpolation of the period minimum, feeding a
**Viterbi tracker** over a lattice of candidate periods.

A single 5 ms frame genuinely cannot tell 220 Hz from 110 Hz — both explain the
waveform, and their difference-function costs sit within a fraction of a
percent. What separates them is history. The tracker runs an online Viterbi
forward pass where staying near the previous pitch is cheap and leaping is
expensive but never forbidden, so real octave leaps still get through once the
accumulated cost of persisting at the wrong octave overtakes the one-off jump
penalty.

Building that lattice correctly is subtler than it looks, and getting it wrong
was the single worst bug in this project:

> A periodic signal repeats at **every integer multiple** of its period, so 2T,
> 3T, 4T … are all real minima. Worse, YIN's cumulative-mean normalisation
> divides by a running average that grows with τ, so subharmonics score
> **better** than the truth — for a 430 Hz tone, `cmnd` is 0.0014 at T and
> 0.0002 at 2T. A lattice built from "the lowest-cost minima" therefore evicted
> the correct period and kept six wrong octaves. The tracker locked onto 1/7 of
> the fundamental and handed PSOLA a period seven times too long.
>
> The fix is to anchor the lattice on YIN's own threshold-crossing estimate,
> which is right by construction, and hang octave alternatives around it at a
> deliberate cost penalty.

Every input-type configuration (window length, τ bounds, FFT size) is built
during `prepareToPlay`, so switching voice range mid-stream never allocates on
the audio thread.

## Pitch shifting

**TD-PSOLA**. Two-period grains are lifted from the input at pitch-synchronous
marks and overlap-added at a different spacing. Because the grain *content* is
never stretched, the spectral envelope survives untouched and only the pulse
rate changes.

- Analysis marks are refined by normalised cross-correlation against the
  previous grain, with the correlation peak **interpolated** and applied as a
  measured lag. Snapping marks to the integer grid instead discards the
  fractional part of the period, which accumulates into a drifting delay — and
  a drifting delay is a pitch error (see Verification).
- The overlap-add is normalised by the summed window, so output level stays
  flat at any shift ratio.

## Formants

Formants are handled by a genuine **LPC source-filter** stage rather than by
resampling PSOLA grains. The vocal tract response is estimated by
Levinson-Durbin recursion (order 32, ridge-regularised and lag-windowed), and a
short linear-phase correction filter is built from the ratio between the
envelope you want and the envelope you have:

```
E(f) = 1 / |A(f)|          measured envelope
T(f) = E(f / ratio)        envelope with formants moved
R(f) = T(f) / E(f)         correction applied to the output
```

Because `R` is a ratio, the LPC gain cancels and the filter is unity wherever
the two envelopes agree. PSOLA is then left to do nothing but move pitch, so
the pitch and formant controls stop interfering — the earlier grain-resampling
approach stretched the waveform inside the grain and read as a warble at strong
settings.

## Consonant protection

Sibilants and plosives have no meaningful pitch, but a tracker will always
report *something* for them. Correcting that something is what produces the
classic artefacts: a lisping, warbling "s", and a click where a "t" gets
dragged onto a scale note.

A cheap time-domain test (high-frequency ratio, zero-crossing rate, and
aperiodicity together) fades the correction out across consonants. Transpose
and detune deliberately stay applied — leaving a sibilant uncorrected is right,
dropping it out of the transposed key is not.

## Auto-Key

Pitch-class histogram weighted by how long and how confidently each note was
held, correlated against the **Krumhansl-Schmuckler** probe-tone profiles.
Working from detected f0 rather than a spectral chroma is both cheaper and more
accurate for a monophonic source.

Confidence is reported as the margin over the runner-up, not the absolute
correlation: a vocal that fits C major well also fits A minor well, and what
matters is how cleanly the winner separates. The editor shows the histogram
alongside the answer, so when the detector picks the relative minor the
weighting is there to explain why.

## Harmony

Four voices, each with its own PSOLA shifter, formant scaling, pan, detune and
timing offset. Intervals are specified in **scale degrees**, so a third above
the tonic is four semitones and a third above the second degree is three —
fixed-interval harmony gets one of those wrong on every other note.

The panel shows the live target note per voice, because "+3rd" alone does not
tell you what you are about to hear.

## Correction behaviour

| Control | What it does |
|---|---|
| **Retune Speed** | 0–400 ms. 0 is the hard-snap sound. |
| **Humanize** | Relaxes retune *only on sustained notes*, keeping onsets tight. |
| **Flex-Tune** | Correction falls off away from a scale tone, so scoops survive. |
| **Natural Vibrato** | Scales the singer's own vibrato, split out by a 120 ms one-pole. |
| **Targeting Ignores Vibrato** | Picks the target from the vibrato-free contour. |
| **Classic Mode** | Faster, harder grab — the response of the early hardware. |
| **Stability** | Viterbi transition penalty: how stubbornly pitch is held. |
| **Sibilance Guard** | How aggressively consonants are left alone. |
| **Throat Length** | Scales the vocal tract independently of pitch (LPC). |
| **Tracking** | Detector permissiveness, for breathy vs. clean sources. |

## Scales

21 equal-tempered scales plus six **historical temperaments** implemented as
real cent-offset tables — Just Intonation, Pythagorean, quarter-comma Meantone,
Werckmeister III, Kirnberger III and Vallotti. Those aren't note subsets; the
note set is the full chromatic and it's the *tuning of each degree* that moves.

Every pitch class can be **Normal**, **Removed** (never a target) or
**Bypassed** (recognised, but passed through untouched).

## Graph mode

A piano roll showing three layers of pitch on one axis: what was sung, what has
been asked for, and what is coming out.

- **Tools** (keys `1`–`8`): select/move, draw note, draw curve, pitch line,
  split, erase, zoom, pan.
- **Make Notes** turns the captured contour into flat notes at the nearest
  scale pitch. **Make Curves** keeps each note's original shape.
- Per-note **Retune** and **Vibrato** overrides, undo/redo, rubber-band select.

The audio thread reads notes through a lock-free double-buffered snapshot — no
locks, no allocation, and a torn read is impossible because the buffer being
written is never the one being read.

## Presets, MIDI

13 factory presets (correction and harmony) plus user presets saved to
`%APPDATA%\Helix Audio\HELIX Tune\Presets`. Factory presets are sparse
parameter lists rather than full states, so adding a parameter later does not
require editing every preset.

**MIDI Pitch Out** emits the sung note as MIDI, with pitch bend carrying the
cents actually sung so a receiving instrument follows the performance rather
than a grid.

## Latency

PSOLA needs the input a grain will read *after* the synthesis mark it lands on,
so the delay scales with the longest period in the selected range. The LPC
filter adds its own 32-sample group delay.

| Input type | Latency @ 44.1 kHz |
|---|---|
| Soprano | ~18 ms |
| Alto / Tenor | ~34 ms |
| Low Male | ~47 ms |
| Instrument | ~55 ms |
| Bass Instrument | ~95 ms |

Reported to the host for delay compensation, and re-reported when the input
type changes. The harmony bus is delay-matched to the lead's formant stage; the
Mix control crossfades against a dry signal delayed by exactly the total, so
it's a true crossfade and not a comb filter.

---

## Building

Requires **CMake ≥ 3.22** and **MSVC** (Visual Studio 2022 Build Tools, C++
workload, plus a Windows SDK). JUCE is fetched automatically.

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

The `HelixTune_Standalone` target builds a normal app with its own audio device
picker — the fastest way to test changes without loading a DAW.

---

## Verification

Two independent layers, because they catch different things.

### `HelixTuneDspTest` — does it actually work?

A console target over the same DSP sources. Run it directly, or via `ctest`.
Measures real numbers off real signals; **44 checks, all passing**:

| Check | Result |
|---|---|
| f0 detection, 82–880 Hz | within **0.4 cents** |
| candidate lattice | true period present, subharmonic penalised 0.0014 → 0.35 |
| octave stability, vibrato take | **0 errors** in 765 voiced frames |
| PSOLA at ratios 0.75 – 2.0 | within **0.08 cents** |
| PSOLA delay drift at ratio 1.0 | **0 samples** over 100 k |
| 429.9 Hz (40 cents flat) → A440 | **0.09 cents** |
| Transpose +12 | **0.36 cents** |
| diatonic harmony intervals | exact (3rd over tonic = 4 st, over 2nd = 3 st) |
| harmony rendering, A3 +3rd in C | **C4, 0.0 cents**; hard pan L=0.234 R=0.000 |
| Auto-Key | C major (0.90 confidence), A minor |
| consonant guard | vowel **0.000**, fricative **1.000** |
| LPC formants | centroid 370 → **449 Hz** up, **337 Hz** down |
| white noise input | no NaN/Inf, output bounded |
| silence in | silence out |

Throughput, stereo at 44.1 kHz with 512-sample blocks:

| | real-time factor | one core |
|---|---|---|
| correction only | 22.4x | **4.5%** |
| correction + 4 harmony voices | 14.3x | **7.0%** |

The tolerances are deliberately tight so a regression fails the build rather
than quietly detuning the plugin. That has already caught two real bugs:

> **Epoch drift.** The mark refinement returned `round(predicted) + delta`,
> discarding the fractional part of the period. At a 200.45-sample period that
> loses 0.45 samples per mark, accumulating into a drifting read offset — and a
> drifting delay *is* a pitch shift. The output sat a constant **−3.9 cents flat
> at every ratio, including 1.0**. Interpolating the correlation peak and
> applying it as a lag to the previous mark's own fractional position took it
> to under 0.1 cents.
>
> **Subharmonic lattice.** Described under Pitch detection above — the tracker
> locked onto 1/7 of the fundamental.

### `pluginval` — is it a well-behaved plugin?

Passes [pluginval](https://github.com/Tracktion/pluginval) at **strictness 10**,
25 test groups, zero failures: editor open/close while processing, state
save/restore, parameter fuzzing across all 56 parameters, bus-layout changes,
and audio at 44.1/48/96 kHz across block sizes 64–1024.

Builds clean under MSVC `/W4` with JUCE's recommended warning flags — zero
warnings from project code.

---

## Layout

```
Source/
  PluginProcessor.*        host plumbing, parameters, state, MIDI in/out
  PluginEditor.*           header, preset browser, Auto/Harmony/Graph switching
  DSP/
    PitchDetector.*        YIN + FFT difference function + candidate lattice
    PitchStabilizer.*      Viterbi tracking over the lattice
    PsolaShifter.*         TD-PSOLA, epoch refinement
    FormantProcessor.*     LPC envelope estimation and correction filter
    KeyDetector.*          Krumhansl-Schmuckler key finding
    TransientGuard.*       consonant / sibilance detection
    HarmonyEngine.*        four scale-aware harmony voices
    ScaleQuantizer.*       scales, temperaments, per-note states
    RetuneEngine.*         retune speed, humanize, flex-tune, vibrato split
    VibratoGenerator.*     LFO with note-triggered onset envelope
    CorrectionEngine.*     hop scheduling, ties the chain together
  Model/
    Parameters.*           APVTS layout
    PitchTrack.*           lock-free analysis handoff + note segmentation
    GraphModel.*           note objects, undo, audio-thread snapshot
    PresetManager.*        factory + user presets
  UI/
    FuturisticLookAndFeel.*
    Widgets/               NeonKnob, ScaleKeyboard, PitchScope, AutoKeyDisplay
    AutoModePanel.*
    HarmonyPanel.*
    Graph/                 PianoRollView, GraphEditorPanel
Tests/
  DspTest.cpp              44 offline checks + throughput report
```
