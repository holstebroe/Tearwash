# Tearwash — plan

Spec: [01_ARCHITECTURE.md](01_ARCHITECTURE.md), [02_ENGINE.md](02_ENGINE.md),
[03_PRESETS.md](03_PRESETS.md). Status: ✅ done, 🟡 in progress, ⬜ open.

| ID | Work package | Status |
| --- | --- | --- |
| TW1 | Engine: figure-of-eight tank, ten programs, laws in units, hold, ensemble echo | ✅ |
| TW2 | Plugin `tearwash.clap`: parameters, state, panel | ✅ |
| TW3 | Presets: `.twpreset`, 75 factory presets (28 space, 6 with shimmer), loudness trims, CLAP preset-load and discovery | ✅ |
| TW4 | Tests: dsp, gui, preset | ✅ |
| TW5 | CPU profile and optimisation (SIMD for the tank taps) | ⬜ |
| TW6 | macOS GUI glue | ⬜ |
| TW7 | Tannhäuser integration (01 §4) | ⬜ deferred while Tannhäuser's UI is reworked on another branch |
| TW8 | Shimmer in the engine and on the panel (02 §7, AMOUNT and INTERVAL), requested by Plumeflow | ✅ |

## Decision log

| Date | Decision |
| --- | --- |
| 2026-10-16 | Tearwash is a single-engine algorithmic reverb of our own; the plugin is named Tearwash (id `com.holstebroe.tearwash`). It is unreleased, so the parameter list starts fresh. |
| 2026-10-16 | Hall delay set derived by a stated rule (02 §3); plate set from Dattorro's published plate with credit. |
| 2026-10-16 | Neutral vocabulary for programs and controls (DECAY, LOW DECAY, DAMPING, DISTANCE, CLARITY, HIGH CUT, MOD RATE; GRAND HALL … VAST SPACE). |
| 2026-10-16 | Factory decays stop at 58 s: the top of DECAY is the hold, which mutes the input, so it belongs to live use. |
| 2026-10-16 | Tannhäuser keeps its legacy plate as default for existing sounds; Tearwash becomes `rev.model` 1 in TW7. |
| 2026-10-20 | Host state becomes spux's chunk container, magic `TWR1`, version 2 (the text state was version 1 and keeps loading). Presets (`.twpreset`) stay text. |

## Progress log

- 2026-10-16 — Tearwash as its own single-engine reverb: engine, plugin, panel, 69 presets
  (loudness-matched), tests.
- 2026-10-17 — Shimmer in the engine (TW8): a pitch-shifted feedback path with `shimmer`, `shimmerInterval`
  and `shimmerMode` in `Controls` (02 §7), requested by Plumeflow's spec 09; bit-identical when off,
  bounded at decay 60 s. `tearwash_dsp_test` gains the off/pitch/stability cases. The plugin panel does not expose it yet.
- 2026-10-18 — Panel pass: decay and space in signal order with the names IN CUT, SPLIT FREQ, LOW DECAY,
  HIGH DECAY, DAMP CUT and SIZE, PREDELAY, DIFFUSION, CLARITY, DISTANCE; the info box is replaced by a
  description line at the bottom and each control shows its value; REAR OUT and INPUT are off the panel
  (ids kept); MODULATION is replaced by MOD RATE and MOD DEPTH knobs (MOD DEPTH 0 = none); SHIMMER AMOUNT
  and INTERVAL knobs (ids 18, 19). Presets that set MOD RATE keep their sound through MOD DEPTH.
- 2026-10-18 — Six shimmer presets (three plates, three space), 75 factory presets in all; loudness
  trims refitted. The two existing "Shimmer" names (PL Bright Shimmer Plate, SP Diamond Dust Shimmer)
  still have shimmer off.
- 2026-10-18 — Panel layout: tall and narrow (640 × 628), programs in two rows, larger labels and light-amber
  values, level knobs beside motion and shimmer, headroom meter removed (it only showed the input peak).
  A program change from the panel or the host makes the preset that program's PG preset. PL Bright Shimmer
  Plate and SP Diamond Dust Shimmer now set shimmer.
- 2026-10-18 — Panel look: dark rounded buttons (the selected program has a light outline), dark fader caps
  with a light centre line, plain dark knobs with a thin pointer, a diagonal blue-grey gradient, a light
  preset name instead of the red LED; the preset menu follows.
- 2026-10-18 — Panel redraw cost: a redraw after a change fell from about 10.7 ms to 3.7 ms (idle frames cost
  nothing). The idle buttons, the knob shadows and the labels are drawn once into the cached background; only
  the selected or hovered button, the fader caps, the knob bodies and the values are redrawn, and the 2x2
  downsample works on two channels at a time.
- 2026-10-20 — Plugin on spux_params, spux_state and spux_clap (spux migration step 5, Tearwash as pilot):
  the parameter table is `TwParams.def` (same ids, keys, names, modules, ranges, defaults and flags), values
  live in a spux `ParamStore`, GUI and preset edits reach the host through a spux `EditQueue` (was a mutex-guarded
  array), the CLAP tables come from `spux::clap::Plugin`, and the state is the spux container `TWR1` v2 (the
  text state `tearwash 1` still loads). New `tearwash_state_test` with states, a preset and the parameter info and
  value text saved by the previous version; audio output and the panel snapshot are bit-identical.
