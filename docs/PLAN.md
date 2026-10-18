# Tearwash — plan

Spec: [01_ARCHITECTURE.md](01_ARCHITECTURE.md), [02_ENGINE.md](02_ENGINE.md),
[03_PRESETS.md](03_PRESETS.md). Status: ✅ done, 🟡 in progress, ⬜ open.

| ID | Work package | Status |
| --- | --- | --- |
| TW1 | Engine: figure-of-eight tank, ten programs, laws in units, hold, ensemble echo | ✅ |
| TW2 | Plugin `tearwash.clap`: parameters, state, panel, headroom meter | ✅ |
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
