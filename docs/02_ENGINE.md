# 02 — Engine [A]

`src/tearwash/engine/Reverb.{hpp,cpp}`, class `tearwash::Reverb`. Evidence tags: [S] sourced,
[D] our default, [A] added feature (the whole engine is [A]).

## 1. Principles

- Floating point at the host rate; delays in milliseconds scaled by SIZE, so the decay and the
  spacing do not depend on the sample rate (checked at 44.1 and 96 kHz). Coefficients come from
  times and frequencies, never per sample rate (CLAUDE.md rule 2).
- Static delays are rounded to whole samples (so the hold is lossless); only glides (SIZE,
  PREDELAY: ≈ 0.25 s) and modulation interpolate (linear; cubic Hermite in the modulated
  allpasses).
- Modulation is a smooth random glide: a new random target every period, two cascaded one-poles.
- A guard inside the loops limits to ±8 and flushes values below 1e-25 to zero (no denormals,
  no offset for the lossless hold to integrate).

## 2. Structure

Tank programs (halls, rooms, chamber, plates):

1. input high-cut one-pole (HIGH CUT), predelay (PREDELAY, glides);
2. four input allpasses per channel (gain from DIFFUSION; R's lengths ×1.031); the early taps
   are read after the second and the fourth;
3. a figure-of-eight tank of two halves, each: modulated allpass (gain from CLARITY), delay,
   two-band decay (one-pole crossover splits the band; LOW DECAY and DECAY set the two loop gains
   from the half's total delay so the decay time is what the control says), damping one-pole
   (DAMPING), two decay allpasses (DIFFUSION), delay into the other half;
4. outputs A (front L), B (rear L), C (front R), D (rear R): sums of tank taps at fractions of
   the delays with alternating signs, plus the early taps; DISTANCE moves the balance from the
   early taps to the tank. Wet ≈ −9 dB of dry [D].

ENSEMBLE: eight voices on two gliding delay lines (7–38 ms × SIZE, panned across), a little
diffusion from the first two input allpasses, echo feedback crossing L↔R through the damping
one-pole; DECAY sets the feedback (0.85·min(1, 1.1·v)).

## 3. Programs and delay sets

Delays in ms at SIZE ×1: four input diffusers, then per half {modulated allpass, delay 1, decay
diffuser 1, decay diffuser 2, delay 2}.

| Set | Input diffusers | Half 0 | Half 1 |
| --- | --- | --- | --- |
| Hall [D] | 5.229, 8.271, 12.771, 19.729 | 37.979, 85.396, 9.521, 15.146, 41.729 | 29.854, 70.271, 10.604, 15.854, 49.396 |
| Plate [S]/[D] | 4.77, 3.59, 12.7, 9.31 | 22.6, 149.6, 8.93, 6.29, 125.0 | 30.5, 141.7, 7.13, 5.41, 106.3 |

- **Hall set** [D], derived by rule: input diffusers 5.3·1.55^k ms (k = 0…3); tank halves of 190
  and 176 ms (about the mean free path of a large hall), split 20/45/5/8/22 % and
  17/40/6/9/28 % over the five lines; every length snapped to the nearest prime number of samples
  at 48 kHz, so no two lines share a period and the halves decorrelate.
- **Plate set**: input diffusers and the tank's allpass and delay lengths from Dattorro's plate
  (J. Dattorro, "Effect Design, Part 1: Reverberator and Other Filters", JAES 45(9), 1997) [S];
  the short decay diffusers are our own [D].
- Decay calibration: the hall set decays ≈ 8 % longer than its plain delay sum predicts, the
  plate set ≈ 1 %; the loop gains include these factors (measured, every program within ±5 % of
  its setting in the 1 kHz octave).

| # | Program | Set | Scale | Mod (ms) | Character |
| --- | --- | --- | --- | --- | --- |
| 0 | GRAND HALL | hall | 1.00 | 0.45 | the default |
| 1 | BRIGHT HALL | hall | 0.92 | 0.40 | open top |
| 2 | DARK HALL | hall | 1.10 | 0.50 | damped, longer low end |
| 3 | ROOM | hall | 0.38 | 0.25 | short, dense |
| 4 | CHAMBER | hall | 0.55 | 0.30 | high diffusion |
| 5 | PLATE | plate | 1.00 | 0.35 | |
| 6 | VOCAL PLATE | plate | 0.82 | 0.30 | darker, a little predelay |
| 7 | DENSE PLATE | plate | 1.20 | 0.55 | maximum diffusion, little early sound |
| 8 | ENSEMBLE | — | — | 3.00 | eight-voice chorus echo |
| 9 | VAST SPACE | hall | 2.40 | 1.20 | long decays, deep modulation |

Each program stores a full set of control values (`Reverb::programDefaults`), which selecting the
program loads.

## 4. Control laws

Control value v = 0…1 as the plugin stores it.

| Control | Law |
| --- | --- |
| DECAY, LOW DECAY | 0.2·300^v s (0.2–60 s); DECAY at v ≥ 0.997: hold (infinite decay, input muted over 50 ms, damping off) |
| CROSSOVER | 80·125^v Hz (80 Hz–10 kHz) |
| DAMPING | tank corner 1000·20^v Hz, open at v ≥ 0.995 |
| HIGH CUT | input corner 1000·20^v Hz, open at v ≥ 0.995 |
| PREDELAY | 1000·v² ms (0–1000 ms) |
| SIZE | ×0.5·4^v (×0.5–×2) on the program's delays |
| MOD RATE | 0.1·50^v Hz; depth = the program's excursion × (0.5 + v) |
| MODULATION | on / off |
| DIFFUSION | input allpasses g = 0.75·v, decay allpasses g = 0.7·v |
| CLARITY | tank allpass g = 0.70 − 0.55·v |
| DISTANCE | late taps ×(0.35 + 0.65·v), early taps ×(1 − v) |

## 5. Plugin parameters

Stable ids, append only (`src/tearwash/plugin/TwParams.cpp`). The panel and the host show the
units above; text entry accepts them ("3.5", "Hold", "Open", "x1.5").

| Id | Key | Name | Range |
| --- | --- | --- | --- |
| 0 | `program` | Program | 0–9 |
| 1 | `decay` | Decay | 0–1 (0.2–60 s, Hold) |
| 2 | `lowdecay` | Low Decay | 0–1 (0.2–60 s) |
| 3 | `crossover` | Crossover | 0–1 (80 Hz–10 kHz) |
| 4 | `damping` | Damping | 0–1 (1–20 kHz, Open) |
| 5 | `distance` | Distance | 0–1 |
| 6 | `predelay` | Predelay | 0–1 (0–1000 ms) |
| 7 | `diffusion` | Diffusion | 0–1 |
| 8 | `modulation` | Modulation | on / off |
| 9 | `modrate` | Mod Rate | 0–1 (0.1–5 Hz) |
| 10 | `rear` | Rear Outputs | on / off (outputs B/D instead of A/C) |
| 11 | `size` | Size | 0–1 (×0.5–×2) |
| 12 | `clarity` | Clarity | 0–1 |
| 13 | `highcut` | High Cut | 0–1 (1–20 kHz, Open) |
| 14 | `mix` | Mix | 0–100 % (default 35 %) |
| 15 | `ingain` | Input Gain | −12 … +12 dB |
| 16 | `outgain` | Output Gain | −60 dB (= −∞) … +12 dB |

Defaults: GRAND HALL with its stored settings, Mix 35 % [D]. Selecting a program (panel or host)
loads its stored settings into ids 1–9 and 11–13; Mix, gains and Rear Outputs stay.

## 6. Tests

- `tearwash_dsp_test`: the 1 kHz-octave decay follows DECAY (GRAND HALL, ROOM, PLATE; ±15 %);
  the same decay at 44.1 and 96 kHz (±5 %); the hold keeps the tail within 2 dB over 6.5 s and
  mutes new input; every program at extreme settings with loud noise stays finite and bounded.
- `tearwash_gui_test`: every program renders a finite, decaying response through the CLAP
  process call; no latency; stored settings on program change (panel and host); the panel's
  faders, wheel, toggles and program buttons; idle frames do not redraw; state round trip;
  parameter text.
- `tearwash_preset_test`: 03 §6.
