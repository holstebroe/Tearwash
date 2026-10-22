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
  does the editor (`spux_ui_core` with the Classic style `spux_ui_classic`, `spux_platform`; `extern/spux`). Clone with
  `--recurse-submodules`; the build needs spux and its `third_party/clap` (the CLAP headers; Tearwash has no `clap`
  submodule of its own). The engine uses spux's header-only `spux_dsp` (its delay line) and nothing else.
- **Using spux.** Shared code lives in spux (`extern/spux`, pinned to a commit until tags start). Change
  shared code in spux (develop with `-DSPUX_SOURCE_DIR=...`), release a tag (until then a commit), then bump the
  submodule here and run `tearwash_null_test` against its three short reference renders (`src/tests/reference/`; sound tolerances: spectrum
  1 dB, 10 ms level 0.5 dB, the peak is a report only; for a wider check render with the previous pin on the fly and
  store nothing). Accept differences by regenerating the references (`SPUX_UPDATE_REFERENCES=1 build/tearwash_null_test`)
  in the same commit as the pointer bump; compare the panel with its golden frames (`tearwash_gui_test`, `tearwash_preset_test`;
  `SPUX_UPDATE_GOLDEN=1` rewrites them, only when a look change is intended). This plugin's UI flavor: Classic (set
  `SPUX_UI_FLAVOR` to it before adding spux: `TEARWASH_UI=CLASSIC|NONE` does; a simple Retro plugin builds its UI for
  size, a large one for speed). Character code stays here.
- Nothing allocates, locks (except `try_lock`) or throws on the audio thread.
- After changes run `build/tearwash_dsp_test`, `build/tearwash_preset_test`, `build/tearwash_gui_test`,
  `build/tearwash_state_test`, `build/tearwash_null_test` (or `ctest --test-dir build`; with `-DTEARWASH_UI=NONE` there
  is no editor and no panel test).
