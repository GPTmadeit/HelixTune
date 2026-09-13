<div align="center">

<img src="docs/banner.png" alt="HELIX Tune" width="100%">

<br>

**Real-time pitch correction with graphical note editing, automatic key detection and four-voice harmony.**

[![Release](https://img.shields.io/github/v/release/GPTmadeit/HelixTune?style=for-the-badge&color=00e5ff&labelColor=0a0f1a)](https://github.com/GPTmadeit/HelixTune/releases/latest)
[![Downloads](https://img.shields.io/github/downloads/GPTmadeit/HelixTune/total?style=for-the-badge&color=7cff4f&labelColor=0a0f1a)](https://github.com/GPTmadeit/HelixTune/releases)
[![Platform](https://img.shields.io/badge/platform-Windows%20x64-9d6bff?style=for-the-badge&labelColor=0a0f1a)](https://github.com/GPTmadeit/HelixTune/releases/latest)
[![Format](https://img.shields.io/badge/format-VST3%20%7C%20Standalone-ff2d95?style=for-the-badge&labelColor=0a0f1a)](#install)
[![License](https://img.shields.io/badge/license-MIT-ffb020?style=for-the-badge&labelColor=0a0f1a)](LICENSE)

### [⬇ Download the latest release](https://github.com/GPTmadeit/HelixTune/releases/latest)

Run the installer, open your DAW, and HELIX Tune is in your plugin list.
It checks GitHub for updates and can install them itself.

</div>

---

## What it is

A pitch corrector built the way the classic ones were — pitch-synchronous
time-domain shifting, a real scale engine, and a graphical editor where you draw
the notes you want — plus the things the classics never had: an octave-robust
tracker, true source-filter formant control, and a harmoniser that follows the
key.

It is fast enough to sit on a track and accurate enough to trust: **corrected
pitch lands within 0.1 cents of target**, and the whole chain costs about
**1.5% of one CPU core** — under 4% with four harmony voices running.

<table>
<tr>
<td width="50%"><b>Auto mode</b><br><sub>Scale-driven correction, live pitch scope, per-note scale editing</sub></td>
<td width="50%"><b>Harmony mode</b><br><sub>Auto-Key, four diatonic voices, live chord readout</sub></td>
</tr>
<tr>
<td><img src="docs/screenshot-auto.png" alt="Auto mode"></td>
<td><img src="docs/screenshot-harmony.png" alt="Harmony mode"></td>
</tr>
<tr>
<td colspan="2"><b>Graph mode</b> — draw and edit individual notes against the captured performance</td>
</tr>
<tr>
<td colspan="2"><img src="docs/screenshot-graph.png" alt="Graph mode"></td>
</tr>
</table>

---

## Install

**Windows 10/11, 64-bit.**

1. Download **`HELIX-Tune-x.y.z-Windows.exe`** from the
   [latest release](https://github.com/GPTmadeit/HelixTune/releases/latest).
2. Run it. The VST3 goes to `C:\Program Files\Common Files\VST3`, where every
   DAW looks by default. The standalone app is optional.
3. Rescan plugins in your DAW.

> **Staying up to date.** The version button in the plugin's header checks
> GitHub for new releases and will download and run the installer for you. It
> only accepts an installer from this repository's own release downloads, and
> only runs it once the file matches the size and SHA-256 digest GitHub
> publishes for it. Close your DAW first so the plugin file isn't locked.
> Automatic checks can be turned off from that same menu — the preference is
> stored per machine, not in your project.

<details>
<summary><b>Manual install (no admin rights)</b></summary>

Copy the `HELIX Tune.vst3` folder to:

```
%LOCALAPPDATA%\Programs\Common\VST3
```

Then add that folder to your DAW's plugin search paths.
</details>

<details>
<summary><b>Pops or crackles in a big session?</b></summary>

HELIX allocates the few megabytes it needs when it loads, so RAM is not what
runs out. A pop means the CPU did not finish an audio buffer before its
deadline. In FL Studio, raise the buffer in **Options → Audio settings** (512 or
more while mixing) and leave multithreaded processing on. In HELIX, pick a
specific **Input Type** rather than Generic when you know the singer's range:
Generic searches every range at once and costs about three times as much.
</details>

---

## Features

### Correction

| | |
|---|---|
| **Retune Speed** | 0–400 ms. Zero is the hard-snap sound. |
| **Humanize** | Relaxes retune *only on sustained notes*, so onsets stay tight. |
| **Flex-Tune** | Correction eases off away from a scale tone, so scoops and slides survive. |
| **Natural Vibrato** | Scales the singer's own vibrato, separated from the pitch contour. |
| **Targeting Ignores Vibrato** | Picks the target from the vibrato-free contour. |
| **Classic Mode** | The faster, harder grab of the early hardware. |
| **Stability** | How stubbornly the tracker holds pitch through ambiguity. |
| **Sibilance Guard** | How aggressively consonants are left uncorrected. |
| **Throat Length** | Moves the vocal tract independently of pitch. |
| **Transpose / Detune** | ±12 semitones, ±100 cents. |
| **Note Transition** | How long the glide from one note to the next takes, in note values locked to the project tempo: 1/16T, 1/16, 1/8T, 1/8, 1/4T, 1/4. The knob clicks between those steps. Separate from Retune Speed, which decides how hard pitch is held *within* a note. |
| **Correction Amount** | One knob, set apart beside the correction section, for how much correction is applied at all — 100% lands on the note, 50% goes halfway, 0% leaves the performance alone. Transpose and detune are unaffected. |

### Input types

**Soprano**, **Alto / Tenor**, **Low Male**, **Instrument** and **Bass
Instrument** narrow the pitch search to that source's range, which is the best
protection against octave errors. **Generic (All Ranges)** searches all of them
at once — 32 Hz to 2.2 kHz — so it tracks any voice with no setup. The cost is
the latency of the lowest range (see [Latency](#how-it-works)); if you know the
singer, the specific type is still the tighter choice.

### Scales and tuning

21 scales — major, the modes, pentatonics, blues, diminished, Hungarian minor,
Hirajoshi, double harmonic and more — plus **six historical temperaments**
implemented as real cent-offset tables: Just Intonation, Pythagorean,
quarter-comma Meantone, Werckmeister III, Kirnberger III and Vallotti. Those
aren't note subsets; the note set is the full chromatic and it's the *tuning of
each degree* that moves.

Every pitch class can be **Normal**, **Removed** (never a target) or
**Bypassed** (recognised, but passed through untouched).

### Auto-Key

Listens to the performance and names the key. The **Input Key** display sits on
the main page beside the scale controls — the detected key in large type, a
confidence bar, and the pitch-class histogram behind the answer, so when it
picks the relative minor you can see why. One click applies it, or leave it in
follow mode.

### Harmony

Four voices, each with its own formant scaling, pan, detune and timing offset.
Intervals are **scale degrees**, not fixed intervals — a third above the tonic
is four semitones and a third above the second degree is three. The panel shows
the live target note per voice.

A master **Harmony** switch turns the whole bus off: nothing is rendered, no CPU
is spent, and the voices keep their settings for when it comes back on. It
starts **off**, so a fresh instance only corrects. A lamp on the HARMONY tab
shows whether it is running from any page.

### Graph mode

A piano roll showing three layers of pitch at once: what was sung, what you've
asked for, and what is coming out.

Eight tools (`1`–`8`): select/move, draw note, draw curve, pitch line, split,
erase, zoom, pan. **Make Notes** turns the captured contour into flat notes at
the nearest scale pitch; **Make Curves** keeps each note's original shape so
only its centre is corrected. Per-note retune and vibrato overrides, undo/redo,
rubber-band select.

### Also

13 factory presets, user presets, and **MIDI pitch output** with pitch bend
carrying the cents actually sung — so a synth can double the vocal line and
follow the performance rather than a grid.

---

## How it works

<details open>
<summary><b>Pitch detection — YIN plus a Viterbi tracker</b></summary>

<br>

**YIN** (de Cheveigné & Kawahara, 2002) with an FFT-accelerated difference
function and parabolic interpolation, feeding a **Viterbi tracker** over a
lattice of candidate periods.

A single 5 ms frame genuinely cannot tell 220 Hz from 110 Hz — both explain the
waveform, and their costs sit within a fraction of a percent. What separates
them is history. The tracker runs an online forward pass where staying near the
previous pitch is cheap and leaping is expensive but never forbidden, so real
octave leaps still get through once the accumulated cost of persisting at the
wrong octave overtakes the one-off jump penalty.

Building that lattice correctly is subtler than it looks:

> A periodic signal repeats at **every integer multiple** of its period, so 2T,
> 3T, 4T … are all real minima. Worse, YIN's cumulative-mean normalisation
> divides by a running average that grows with τ, so subharmonics score
> **better** than the truth — for a 430 Hz tone, `cmnd` is 0.0014 at T and
> 0.0002 at 2T. A lattice built from "the lowest-cost minima" therefore evicts
> the correct period and keeps six wrong octaves. During development this
> locked the tracker onto 1/7 of the fundamental.
>
> The lattice is anchored on YIN's own threshold-crossing estimate, which is
> right by construction, with octave alternatives hung around it at a cost
> penalty.

</details>

<details>
<summary><b>Pitch shifting — TD-PSOLA</b></summary>

<br>

Two-period grains are lifted from the input at pitch-synchronous marks and
overlap-added at a different spacing. Because the grain *content* is never
stretched, the spectral envelope survives untouched and only the pulse rate
changes.

Analysis marks are refined by normalised cross-correlation against the previous
grain, with the correlation peak **interpolated** and applied as a measured lag.
Snapping marks to the integer sample grid instead discards the fractional part
of the period, which accumulates into a drifting delay — and a drifting delay
*is* a pitch error. That bug cost a constant −3.9 cents at every ratio before it
was found.

The overlap-add is normalised by the summed window, so output level stays flat
at any shift ratio.

</details>

<details>
<summary><b>Formants — LPC source-filter</b></summary>

<br>

The vocal tract response is estimated by Levinson-Durbin recursion (order 32,
ridge-regularised and lag-windowed), and a short linear-phase filter is built
from the ratio between the envelope you want and the envelope you have:

```
E(f) = 1 / |A(f)|          measured envelope
T(f) = E(f / ratio)        envelope with the formants moved
R(f) = T(f) / E(f)         correction applied to the output
```

Because `R` is a ratio, the LPC gain cancels and the filter is unity wherever
the two envelopes agree. PSOLA is left to do nothing but move pitch, so the
pitch and formant controls stop interfering.

</details>

<details>
<summary><b>Consonant protection</b></summary>

<br>

Sibilants and plosives have no meaningful pitch, but a tracker always reports
*something* for them, and correcting that something is what produces a lisping,
warbling "s". A cheap time-domain test — high-frequency ratio, zero-crossing
rate and aperiodicity together — fades correction out across consonants.
Transpose and detune stay applied throughout: leaving a sibilant uncorrected is
right, dropping it out of the transposed key is not.

</details>

<details>
<summary><b>Latency</b></summary>

<br>

PSOLA needs the input a grain will read *after* the synthesis mark it lands on,
so the delay scales with the longest period in the selected range.

| Input type | Latency @ 44.1 kHz |
|---|---|
| Soprano | ~18 ms |
| Alto / Tenor | ~34 ms |
| Low Male | ~47 ms |
| Instrument | ~55 ms |
| Bass Instrument | ~95 ms |
| Generic (All Ranges) | ~95 ms — inherits the lowest range |

Reported to the host for delay compensation. The harmony bus is delay-matched
to the lead's formant stage, and Mix crossfades against a dry signal delayed by
exactly the total — a true crossfade, not a comb filter.

</details>

---

## Verification

Two independent layers, because they catch different things.

**`HelixTuneDspTest`** — a console target over the same DSP sources that
measures real numbers off real signals. **83 checks, all passing:**

| Check | Result |
|---|---|
| f0 detection, 82–880 Hz | within **0.07 cents** |
| f0 detection on **Generic**, 41 Hz – 1.98 kHz | within **1.5 cents** (0.2 cents up to 1.3 kHz) |
| candidate lattice | true period present; subharmonic penalised 0.0014 → 0.35 |
| octave stability, vibrato take | **0 errors** in 765 voiced frames |
| PSOLA at ratios 0.75 – 2.0 | within **0.02 cents** |
| PSOLA delay drift at ratio 1.0 | **0 samples** over 100 k |
| 429.9 Hz (40 cents flat) → A440 | **0.02 cents** |
| Generic: G2 / A3 / C6 sung 40 cents flat | **0.02 / 0.00 / 0.14 cents** |
| transpose +12 | **0.03 cents** |
| Correction Amount 50% on a −40 cent note | leaves **−19.99 cents**, as asked; transpose unaffected at 0% |
| Note Transition 1/16 at 120 / 60 BPM | lands in **127.7 / 249.6 ms** against 125 / 250 (5.8 ms analysis hop); tracks its S-curve to 0.00 cents |
| harmony master switch off, four voices set up | output **bit-identical** to no harmony |
| harmony voice switched off, then on, then off | **bit-identical** before, and again 200 ms after — idle voices do no work |
| mono source on a stereo track, rendered once | **bit-identical** to rendering both channels, across a switch to true stereo |
| 88.2 / 96 / 192 kHz, detection on a decimated copy | 429.9 Hz → A440 within **0.02 cents** |
| diatonic intervals | exact — 3rd over tonic = 4 st, over 2nd = 3 st |
| harmony render, A3 +3rd in C | **C4, 0.0 cents**; hard pan L 0.234 / R 0.000 |
| Auto-Key | C major (0.90 confidence), A minor |
| consonant guard | vowel **0.000**, fricative **1.000** |
| LPC formants | centroid 370 → **449 Hz** up, **337 Hz** down |
| white noise in | no NaN/Inf, output bounded |
| silence in | silence out |

**`HelixTunePresetTest`** drives the real processor rather than the DSP
classes: every factory preset loads twice cleanly, a fresh instance starts with
harmony off, and a session saved by 1.0.x reopens with harmony exactly as it
sounded, and the updater refuses anything but this repository's own installer
with a matching SHA-256. **21 checks, all passing.**

**`HelixTuneBench`** — where the audio thread's time goes. Average CPU is the
wrong number for a plugin on its own: every buffer has a deadline, and one late
buffer is a pop however idle the rest were. So it reports the slowest buffers
as well. Stereo, 44.1 kHz, a voice-like input with vibrato and breath:

| | one core | slowest buffer, 64 samples | slowest buffer, 512 samples |
|---|---|---|---|
| correction, Alto/Tenor | **1.5%** | 17% of its deadline | 3% |
| + throat 1.2 and 4 harmony voices | **3.7%** | 25% | 6% |
| correction, Generic | **5.7%** | 46% | 9% |
| Generic + throat and 4 harmony voices | **7.9%** | 58% | 12% |

Before 1.1.2 the Generic rows used 14% and 18% of a core, and their slowest
64-sample buffers took 105% and 199% of the deadline — audible pops. Run it on
your own machine to see where you stand.

**`pluginval`** — passes [pluginval](https://github.com/Tracktion/pluginval) at
**strictness 10**, 25 test groups, zero failures: editor open/close while
processing, state save/restore, parameter fuzzing across every parameter,
bus-layout changes, and audio at 44.1/48/96 kHz across block sizes 64–1024.

Builds clean under MSVC `/W4` with JUCE's recommended warning flags — zero
warnings from project code.

> **Honest caveat:** every number above comes from synthetic tones, noise and
> generated melodies. The pitch maths is verifiably correct; whether the
> *character* suits your voice is your ears' call, not a test's.

---

## Building

Requires **CMake ≥ 3.22** and **MSVC** (Visual Studio 2022 Build Tools with the
C++ workload and a Windows SDK). JUCE is fetched automatically.

```bash
cmake -S . -B ../build/HelixTune -DCMAKE_BUILD_TYPE=Release
cmake --build ../build/HelixTune --config Release --parallel
```

Build outside the source tree — this project is kept in a synced folder, and
letting thousands of object files sync is slow and pointless.

| Target | What it is |
|---|---|
| `HelixTune_VST3` | the plugin |
| `HelixTune_Standalone` | standalone app with its own audio device picker |
| `HelixTuneDspTest` | the 83 offline checks, also runnable via `ctest` |
| `HelixTunePresetTest` | processor, preset and updater checks, also via `ctest` |
| `HelixTuneBench` | average and worst-case buffer timings on your machine |
| `HelixTuneBanner` | renders the artwork in `docs/` from the plugin's own palette |

To skip the JUCE download if you already have a checkout:

```bash
cmake -S . -B ../build/HelixTune -DFETCHCONTENT_SOURCE_DIR_JUCE="C:/path/to/JUCE"
```

Packaging the installer needs [Inno Setup 6](https://jrsoftware.org/isinfo.php):

```bash
ISCC.exe /DArtefacts="<build>/HelixTune_artefacts/Release" /DAppVersion=1.0.0 packaging/HelixTune.iss
```

### Code signing

Released binaries are **not currently signed**, so Windows SmartScreen warns on
first run. The pipeline is in place and tested — all that is missing is a
certificate, which has to be bought and tied to a verified identity.

`packaging/sign.ps1` signs the VST3, the standalone app **and** the installer,
in that order. Signing only the installer would leave the files it writes to
disk unsigned, and those are the ones a DAW actually loads. Everything is
SHA-256 and RFC3161-timestamped, so signatures keep validating after the
certificate expires.

```powershell
# Hardware token or any certificate already in the Windows store
./packaging/sign.ps1 -Thumbprint <thumbprint> `
                     -Artefacts "<build>/HelixTune_artefacts/Release" `
                     -Installer dist/HELIX-Tune-1.0.0-Windows.exe
```

The script also accepts `-PfxPath` (internal/test certificates only) and
`-AzureMetadata` for [Azure Trusted Signing](https://learn.microsoft.com/azure/trusted-signing/).
Add `-WhatIfOnly` to print the commands without running them.

To have Inno sign the installer and its uninstaller as part of the build:

```bash
ISCC.exe /DSignToolName=helixsign \
         /Shelixsign="signtool.exe sign /sha1 <thumb> /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 $f" \
         /DArtefacts="..." packaging/HelixTune.iss
```

**On obtaining a certificate.** Since June 2023 the CA/Browser Forum requires
private keys for publicly-trusted code-signing certificates to live on FIPS
140-2 Level 2 hardware — a USB token or a cloud HSM. A `.pfx` file you can copy
around is no longer issuable for public trust.

| Option | Roughly | Notes |
|---|---|---|
| **Azure Trusted Signing** | ~$10/month | Cheapest. Key in Microsoft's HSM, no token to lose. Individual identity validation requires ~3 years of verifiable history. |
| **OV certificate** (Sectigo, DigiCert, SSL.com) | ~$200–600/yr | Ships a USB token. SmartScreen reputation still has to be earned over time and downloads. |
| **EV certificate** | ~$400–800/yr | Immediate SmartScreen reputation. Usually requires a registered business. |

An OV certificate removes the "unknown publisher" wording but not necessarily
the SmartScreen prompt straight away — reputation accrues per publisher. EV
skips that wait.

---

## Layout

```
Source/
  PluginProcessor.*      host plumbing, parameters, state, MIDI in/out
  PluginEditor.*         header, presets, updater, mode switching
  DSP/
    PitchDetector.*      YIN + FFT difference function + candidate lattice
    AnalysisDecimator.h  half-band decimation for detection above 64 kHz
    PitchStabilizer.*    Viterbi tracking over the lattice
    PsolaShifter.*       TD-PSOLA, epoch refinement
    FormantProcessor.*   LPC envelope estimation and correction filter
    KeyDetector.*        Krumhansl-Schmuckler key finding
    TransientGuard.*     consonant / sibilance detection
    HarmonyEngine.*      four scale-aware harmony voices
    ScaleQuantizer.*     scales, temperaments, per-note states
    RetuneEngine.*       retune speed, humanize, flex-tune, vibrato split
    VibratoGenerator.*   LFO with note-triggered onset envelope
    CorrectionEngine.*   hop scheduling, ties the chain together
  Model/                 parameters, pitch track, graph model, presets, updater
  UI/                    look and feel, widgets, the three panels
Tests/DspTest.cpp        83 offline checks + throughput report
Tests/PresetTest.cpp     processor, preset and updater checks
Tests/Benchmark.cpp      average and worst-case buffer timings
Tools/BannerRenderer.cpp repository artwork
packaging/HelixTune.iss  Windows installer
```

---

## Licence

[MIT](LICENSE) for this source.

Built with [JUCE](https://juce.com), which is licensed separately — distributing
a binary requires complying with whichever JUCE licence applies to you. VST is a
trademark of Steinberg Media Technologies GmbH. Not affiliated with or endorsed
by Steinberg or Antares Audio Technologies.
