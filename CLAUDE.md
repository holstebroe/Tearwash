# Development guidelines (humans and AI agents)

Tearwash is an algorithmic reverb: a C++17 `.clap` plugin plus a reusable engine
(`tearwash_engine`, consumed by other plugins such as Tannhäuser as a git submodule). No
third-party DSP or GUI libraries.

- Spec and plan: `docs/` (start with `docs/README.md`, `docs/PLAN.md`). Code and spec must agree;
  change both in the same commit.
- The engine (`src/tearwash/engine`) must stay free of plugin, GUI and CLAP dependencies and keep
  the include root `src/` (`"tearwash/engine/Reverb.hpp"`); other projects depend on it.
- Parameters: `src/tearwash/plugin/TwParams.def` (spux_params X-macro) is the source of truth, mirrored in
  `docs/02_ENGINE.md` §5. Never renumber ids or rename keys; only append. The host state is the spux container
  `TWR1` (`TwState.*`); never break loading of older states (the text state from before the container included):
  `tearwash_state_test` loads the states in `src/tests/data/` saved by earlier versions.
- Presets: edit `tools/preset_library.py`, run `python3 tools/gen_presets.py` (never edit
  `src/tearwash/plugin/TwPresetData.cpp`), rebuild, `build/tearwash_preset_test --fit
  tools/preset_gains.json`, regenerate and rebuild (`docs/03_PRESETS.md`).
- Parameters, state and the CLAP glue come from spux (`spux_params`, `spux_state`, `spux_clap`), and so
  does the GUI drawing code (Graphics, Font, FileDialog, ClassicDraw, atlas fonts; `spux_ui_classic`,
  `extern/spux`). Clone with `--recurse-submodules`; the plugin build needs spux and its
  `third_party/clap` (the CLAP headers; Tearwash has no `clap` submodule of its own), the engine alone needs no spux.
- **Using spux.** Shared code lives in spux (`extern/spux`, pinned to a commit until tags start). Change
  shared code in spux (develop with `-DSPUX_SOURCE_DIR=...`), then bump the submodule here and compare the
  `tearwash_gui_test` snapshot (pass a `.ppm` path) before and after: Classic drawing must stay identical
  unless a look change is intended. This plugin's UI flavor: Classic. Character code stays here.
- Nothing allocates, locks (except `try_lock`) or throws on the audio thread.
- After changes run `build/tearwash_dsp_test`, `build/tearwash_preset_test`, `build/tearwash_gui_test`,
  `build/tearwash_state_test`.
