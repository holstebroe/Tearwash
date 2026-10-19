# Tearwash
Vintage digital reverb plugin: an algorithmic stereo reverb (halls, rooms, plates, ensemble echo,
vast "space" programs with decays up to a minute), as a CLAP plugin and as a reusable engine.

## Build and test

```
git clone --recurse-submodules https://github.com/holstebroe/Tearwash
cmake -S . -B build && cmake --build build -j
build/tearwash_dsp_test
build/tearwash_preset_test
build/tearwash_gui_test build/tearwash_snapshot.ppm
```

## Using Tearwash in another project

Add it as a submodule and link the engine (no plugin, GUI or CLAP dependencies):

```
git submodule add https://github.com/holstebroe/Tearwash extern/tearwash
```
```cmake
set(TEARWASH_BUILD_PLUGIN OFF)          # engine only; ON also builds tearwash.clap and its tests
add_subdirectory(extern/tearwash)
target_link_libraries(my_plugin PRIVATE tearwash_engine)   # #include "tearwash/engine/Reverb.hpp"
```

With `TEARWASH_BUILD_PLUGIN` ON, Tearwash needs spux (`extern/spux`, with its nested `third_party/clap`).
Set `TEARWASH_CLAP_INCLUDE_DIR` to your own CLAP headers to use those instead of spux's.

Specification and plan: [docs/](docs/README.md).
