#ifndef TEARWASH_TWSTATE_HPP
#define TEARWASH_TWSTATE_HPP

// Tearwash host state (docs/03_PRESETS.md, "State"): spux's chunk container (spux_state) with the
// magic `TWR1`, version 1, and two chunks:
//
//   PARM  spux's parameter chunk: count, then id (uint32) and plain value (float64) per parameter
//   PRST  the name of the preset the state came from (UTF-8, at most 1024 bytes)
//
// Unknown chunks and parameter ids are skipped, missing parameters keep their defaults, values are
// sanitized (twClamp). The text state of Tearwash before spux ("tearwash 1", then `preset=` and
// `key=value` lines) has no magic; parseState still reads it (TwState::version 0). Tearwash was
// unreleased when the container came, so the container starts at version 1.

#include "TwPresets.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace tearwash {

constexpr uint32_t kStateVersion = 1;   // 0 stands for the text state from before the container

struct TwState {
    TwValues values{};          // every parameter, sanitized
    std::string preset;         // "" when the state names none
    uint32_t version = kStateVersion;   // the version it was read from (0: the text state)
    int damagedChunks = 0, unknownChunks = 0;
};

std::vector<uint8_t> serializeState(const TwValues& values, const std::string& preset);
// False for anything that is neither a container with our magic nor the text state.
bool parseState(std::span<const uint8_t> bytes, TwState& out);

} // namespace tearwash

#endif
