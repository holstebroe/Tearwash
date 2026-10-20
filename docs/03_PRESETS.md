# 03 — Presets [A]

Tearwash's preset system, in the manner of Tannhäuser's (`docs/spec/05_PRESETS.md`).

## 1. Model and format

- A preset = name + a value for every plugin parameter (02 §5), the program included.
- Text format `.twpreset` (UTF-8, `key=value` per line, `#` comments):
  ```
  # Tearwash preset v1
  name=SP Teardrops In Neon Rain
  program=GRAND HALL
  decay=0.8073888172
  lowdecay=0.8533871115
  ...
  ```
- The program is stored by name, so presets survive list changes; loading starts from that
  program's stored settings (what selecting the program loads), then applies the listed values
  (clamped). Unknown keys are ignored.
- Loading a preset sets every parameter and sends each change to the host as a gesture. The
  plugin state records the preset's name, so the modified marker survives a session.
- **State** (the host's project data, `TwState.hpp`): spux's chunk container with the magic `TWR1`,
  version 2: chunk `PARM` (id and plain value per parameter, float64) and `PRST` (the preset name).
  Unknown chunks and ids are skipped, missing parameters keep their defaults, values are clamped.
  Version 1, the text state before spux (`tearwash 1`, a `preset=` line, then `key=value` lines),
  still loads and is saved again as version 2.

## 2. Library and categories

Names start with a category code; the menu lists the categories in this order:

| Code | Category | Content |
| --- | --- | --- |
| IN | Init | the default (GRAND HALL) |
| PG | Programs | every program with its stored settings (10), generated from the program list |
| HL | Halls | |
| RM | Rooms & Chambers | |
| PL | Plates | three of them with shimmer (AMOUNT and INTERVAL set) |
| SP | Space | long tails (8–58 s), long predelays (up to 1 s), deep modulation, three with shimmer; the largest group (28) |
| EC | Echo & Delay | the ensemble as echo, predelay "space delays" |
| FX | Ensemble & Effects | |
| User | the user folder | |

The factory presets (75) are defined in `tools/preset_library.py` and generated into
`src/tearwash/plugin/TwPresetData.cpp` by `python3 tools/gen_presets.py` (which checks
keys, program names and ranges; never edit the generated file). Values are given in units
(seconds, Hz, ms, ×SIZE, %) through the library's helpers. Names may evoke famous records, films
and artists, but are invented look-alikes, never a real title, name or trademark. The library's
decays stop at 58 s: the top of DECAY is the hold, which mutes the input, so it belongs to live
use.

**Mod depth.** A preset that sets MOD RATE also gets MOD DEPTH = (0.5 + rate) / 2 unless it sets
MOD DEPTH itself. That is the depth the earlier single MODULATION control gave at that rate, so
the sound of those presets did not change when MOD DEPTH was added (`gen_presets.py` applies it).

**Loudness.** Every factory preset carries an output-gain trim so that its output (dry + wet at
its Mix) matches the input level for gently pinked noise within ±1.5 dB (the PG presets are left
as the programs are). Workflow after adding or editing presets:
`python3 tools/gen_presets.py` → build → `build/tearwash_preset_test --fit
tools/preset_gains.json` → `python3 tools/gen_presets.py` → build. Trims span
−0.9 … +4.4 dB.

## 3. User presets

- Folder: `$HOME/Documents/Tearwash/Presets` (Windows: `%USERPROFILE%\Documents\...`;
  `TEARWASH_PRESET_DIR` overrides it), created on the first save; every `*.twpreset` there is
  listed under User (rescanned when the menu opens).
- SAVE opens the system file dialog in that folder (Linux: zenity or kdialog, the panel keeps
  running; Windows: the common dialog); without a dialog program the preset is saved as the next
  free "User NNN".

## 4. Panel

- Preset bar in the header: `<` and `>` step through the whole list (also the mouse wheel over
  the name), the name shows `*` when any parameter differs from the loaded preset, SAVE saves.
- Clicking the name opens the menu over the panel: categories on the left, the category's
  presets in two columns (the wheel scrolls a column at a time), the current preset
  highlighted; a click loads a preset and closes the menu, a click outside closes it.
- A program change, from the panel's buttons or from the host, makes the current preset that
  program's PG preset (the program as stored); the modified marker then starts clear.
- The panel is tall and narrow: the preset bar, the programs in two rows of five, the decay
  faders, the space faders, then the motion, shimmer and level knobs. There is no meter.
- Every fader and knob shows its current value above it. The line at the bottom says what the
  control under the pointer does; when nothing is under the pointer it shows a hint instead.

## 5. Host integration

- CLAP preset-load (`clap.preset-load/2`): factory presets by load key (the preset name, location
  kind PLUGIN), user presets by file path.
- CLAP preset discovery (`clap.preset-discovery-factory/2`): one provider declares the
  `.twpreset` file type, the factory presets as a plugin location (each with the plugin id, the
  factory flag and its category as a feature) and the user folder as a file location.

## 6. Tests (`tearwash_preset_test`)

Format round trip; every built-in preset names a program; one PG preset per program; every
preset finite and below full scale through the plugin; every factory preset within ±1.5 dB of
the input level; step, modified marker, save, rescan and file load; the state keeps the preset;
the panel's bar and menu; preset-load by key and by file; discovery lists every factory preset
and reads a user file.
