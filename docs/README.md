# Tearwash

Tearwash is an algorithmic stereo reverb [A]: halls, rooms, plates, an ensemble echo and vast
"space" programs with decays up to a minute. It ships as its own CLAP plugin
(`build/tearwash.clap`) and will become Tannhäuser's second reverb model (PLAN TW7).

| Document | Content |
| --- | --- |
| [01_ARCHITECTURE.md](01_ARCHITECTURE.md) | products, code layout, threads, Tannhäuser integration |
| [02_ENGINE.md](02_ENGINE.md) | the reverb: structure, programs, delay sets, control laws, parameters, tests |
| [03_PRESETS.md](03_PRESETS.md) | preset format, library, loudness matching, panel, host integration |
| [PLAN.md](PLAN.md) | work packages, status, decision log |

Build and test:

```
cmake -S . -B build && cmake --build build -j
build/tearwash_dsp_test
build/tearwash_gui_test build/tearwash_snapshot.ppm
build/tearwash_preset_test
```
