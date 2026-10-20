# 01 — Architecture

## 1. Products

- **`tearwash.clap`**: stereo audio effect, own panel, factory and user presets
  (plugin id `com.holstebroe.tearwash`, state: spux container `TWR1` version 1; the text state
  `tearwash 1` from before it still loads, 03 §1).
- **Tannhäuser's reverb** (later, PLAN TW7): the same engine behind Tannhäuser's four reverb
  knobs (§4).

Everything here is an added feature [A]: Tearwash models no particular hardware unit.

## 2. Code layout

| Path | Content |
| --- | --- |
| `src/tearwash/engine/Reverb.{hpp,cpp}` | the engine (`tearwash::Reverb`, controls, laws, programs); no plugin or GUI dependencies |
| `src/tearwash/analysis/Metrics.hpp` | offline filters and Schroeder decay fits for the tests |
| `src/tearwash/plugin/TwParams.{def,hpp,cpp}` | parameters: `TwParams.def` (spux_params X-macro: ids, keys, ranges, defaults; the single source of truth), lookups and value text |
| `src/tearwash/plugin/TwState.*` | host state: spux_state container `TWR1` (PARM, PRST), reader of the text state from before it |
| `src/tearwash/plugin/TwPresets.*`, `TwPresetData.cpp` | presets (03); `TwPresetData.cpp` is generated |
| `src/tearwash/plugin/TearwashClap.*` | the plugin on spux_clap (`spux::clap::Plugin<TearwashClap>`: params, state, audio ports and GUI extensions from its methods), preset-load, preset discovery, entry |
| `src/tearwash/plugin/TwGui.*` | the panel (Tannhäuser's procedural drawing; the window is spux's `spux::platform::NativeWindow`) |
| `src/tests/tearwash_{dsp,gui,preset,state}_test_main.cpp`, `src/tests/data/` | tests (02 §6, 03 §6); fixtures saved by Tearwash before spux |
| `tools/` | preset library, generator, loudness trims |

## 3. Threads and real time

- `Reverb::setSampleRate()` allocates every delay line for the longest program at SIZE ×2 and a
  1 s predelay; it runs in `activate()`. After that nothing allocates: program and control
  changes are applied on the audio thread (`setProgram` clears the lines, `setControls` only
  recomputes coefficients).
- A host change of the program parameter switches the engine at once and asks the main thread
  (`request_callback`) to load the program's stored settings into the parameters, then tells the
  host (`rescan`). Panel program buttons and preset loads set all parameters on the main thread
  and send gestures.
- No latency: the dry path is not delayed. Mix, input and output gain are smoothed (10 ms).

## 4. Tannhäuser integration (TW7, deferred)

- The Tannhäuser panel keeps its four reverb knobs (Mix, Decay, Tone, Pre-delay) and parameter
  ids 91–94; an appended `rev.model` (0 = legacy plate, 1 = Tearwash) selects the engine, with no
  panel control. States and presets without `rev.model` keep the plate.
- In Tearwash mode: Decay → DECAY (LOW DECAY tied), Tone → DAMPING, Pre-delay → PREDELAY,
  Mix → wet level; program GRAND HALL or VAST SPACE [D]. Decays above 10 s must be reachable.
