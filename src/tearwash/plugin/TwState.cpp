#include "TwState.hpp"

#include "spux/state/Container.hpp"

#include <cstdlib>
#include <cstring>
#include <sstream>

namespace tearwash {

namespace {

namespace ss = spux::state;

constexpr ss::FourCC kMagic{ "TWR1" };
constexpr ss::FourCC kPresetChunk{ "PRST" };
constexpr size_t kMaxPresetName = 1024;

// The text state from before the container: "tearwash 1", then lines `preset=NAME` and `key=value` (%.17g).
bool parseText(std::span<const uint8_t> bytes, TwState& out) {
    std::istringstream in(std::string(bytes.begin(), bytes.end()));
    std::string line;
    if (!std::getline(in, line) || line.compare(0, 9, "tearwash ") != 0) return false;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        if (line.compare(0, eq, "preset") == 0) { out.preset = line.substr(eq + 1); continue; }
        const uint32_t id = twParamByKey(line.substr(0, eq).c_str());
        if (id < TW_PARAM_COUNT) out.values[id] = std::strtod(line.c_str() + eq + 1, nullptr);
    }
    out.version = 0;
    return true;
}

// One step per released version (spux migration hook): { from, apply } turns version `from` into
// `from + 1`. None yet: version 1 is the first. The text state reads into the same values and preset
// name as the container, so it needs no step either.

} // namespace

std::vector<uint8_t> serializeState(const TwValues& values, const std::string& preset) {
    ss::ContainerWriter w(kMagic, kStateVersion);
    w.chunk(ss::kParamChunk, ss::paramPayload(TW_PARAM_COUNT, [&](size_t i) {
        return std::pair<uint32_t, double>{ twParamInfo(static_cast<uint32_t>(i)).id, values[i] };
    }));
    ss::ByteWriter name;
    name.bytes(std::string_view(preset).substr(0, kMaxPresetName));
    w.chunk(kPresetChunk, name);
    return w.take();
}

bool parseState(std::span<const uint8_t> bytes, TwState& out) {
    TwState s;
    for (uint32_t i = 0; i < TW_PARAM_COUNT; ++i) s.values[i] = twParamInfo(i).def;
    if (ss::hasMagic(bytes, kMagic)) {
        const ss::ReadReport r = ss::readContainer(bytes, kMagic, [&](ss::FourCC id, std::span<const uint8_t> p, uint32_t) {
            if (id == ss::kParamChunk) {
                const bool ok = ss::readParamPayload(p, [&](uint32_t pid, double v) {
                    if (pid < TW_PARAM_COUNT) s.values[pid] = v;
                });
                return ok ? ss::ChunkResult::Read : ss::ChunkResult::Damaged;
            }
            if (id == kPresetChunk) {
                if (p.size() > kMaxPresetName) return ss::ChunkResult::Damaged;
                s.preset.assign(p.begin(), p.end());
                return ss::ChunkResult::Read;
            }
            return ss::ChunkResult::Unknown;
        });
        if (!r.ok()) return false;
        s.version = r.version;
        s.damagedChunks = r.damagedChunks;
        s.unknownChunks = r.unknownChunks;
    } else if (!parseText(bytes, s)) {
        return false;
    }
    for (uint32_t i = 0; i < TW_PARAM_COUNT; ++i) s.values[i] = twClamp(i, s.values[i]);
    out = std::move(s);
    return true;
}

} // namespace tearwash
