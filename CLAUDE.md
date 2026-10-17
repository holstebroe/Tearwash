# Development guidelines (humans and AI agents)

Tearwash is an algorithmic reverb: a C++17 `.clap` plugin plus a reusable engine
(`tearwash_engine`, consumed by other plugins such as Tannhäuser as a git submodule). No
third-party DSP or GUI libraries.

- Spec and plan: `docs/` (start with `docs/README.md`, `docs/PLAN.md`). Code and spec must agree;
  change both in the same commit.
- The engine (`src/tearwash/engine`) must stay free of plugin, GUI and CLAP dependencies and keep
  the include root `src/` (`"tearwash/engine/Reverb.hpp"`); other projects depend on it.
- Parameters: `src/tearwash/plugin/TwParams.cpp` is the source of truth, mirrored in
  `docs/02_ENGINE.md` §5. Never renumber ids or rename keys; only append.
- Presets: edit `tools/preset_library.py`, run `python3 tools/gen_presets.py` (never edit
  `src/tearwash/plugin/TwPresetData.cpp`), rebuild, `build/tearwash_preset_test --fit
  tools/preset_gains.json`, regenerate and rebuild (`docs/03_PRESETS.md`).
- `src/tearwash/gui/` is a vendored copy of Tannhäuser's procedural GUI toolkit (namespace
  `tannhauser`); it is not kept in sync automatically.
- Nothing allocates, locks (except `try_lock`) or throws on the audio thread.
- After changes run `build/tearwash_dsp_test`, `build/tearwash_preset_test`, `build/tearwash_gui_test`.
