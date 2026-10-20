#ifndef TEARWASH_TWSTATE_HPP
#define TEARWASH_TWSTATE_HPP

// Tearwash host state (docs/03_PRESETS.md, "State"): spux's chunk container (spux_state) with the
// magic `TWR1`, version 2, and two chunks:
//
//   PARM  spux's parameter chunk: count, then id (uint32) and plain value (float64) per parameter
//   PRST  the name of the preset the state came from (UTF-8, at most 1024 bytes)
//
// Unknown chunks and parameter ids are skipped, missing parameters keep their defaults, values are
// sanitized (twClamp). Version 1 is the text state of Tearwash before spux ("tearwash 1", then
// `preset=` and `key=value` lines); parseState still reads it and migrates it to version 2.

#include "TwPresets.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace tearwash {

constexpr uint32_t kStateVersion = 2;   // 1: the text state (before spux)

struct TwState {
    TwValues values{};          // every parameter, sanitized
    std::string preset;         // "" when the state names none
    uint32_t version = kStateVersion;   // the version it was read from
    int damagedChunks = 0, unknownChunks = 0;
};

std::vector<uint8_t> serializeState(const TwValues& values, const std::string& preset);
// False for anything that is neither a container with our magic nor a version 1 text state.
bool parseState(std::span<const uint8_t> bytes, TwState& out);

} // namespace tearwash

#endif
