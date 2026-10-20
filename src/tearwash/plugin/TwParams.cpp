#include "TwParams.hpp"
#include "tearwash/engine/Reverb.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace tearwash {

namespace {

using namespace spux::params;

constexpr ParamInfo kInfos[] = {
#define SPUX_PARAM SPUX_PARAM_INFO
#include "TwParams.def"
#undef SPUX_PARAM
};
constexpr ParamTable kTable{ kInfos };

static_assert(kTable.valid());
static_assert(kTable.size() == TW_PARAM_COUNT);
// Ids are the enum and are dense from 0 (the plugin indexes by id); keys never change.
constexpr bool keyIs(uint32_t id, const char* key) { return kTable.indexOf(id) == id && kTable.indexOfKey(key) == id; }
static_assert(keyIs(TW_PROGRAM, "program") && keyIs(TW_DECAY, "decay") && keyIs(TW_LOWDECAY, "lowdecay") &&
              keyIs(TW_CROSSOVER, "crossover") && keyIs(TW_DAMPING, "damping") && keyIs(TW_DISTANCE, "distance") &&
              keyIs(TW_PREDELAY, "predelay") && keyIs(TW_DIFFUSION, "diffusion") && keyIs(TW_MODULATION, "modulation") &&
              keyIs(TW_MODRATE, "modrate") && keyIs(TW_REAR, "rear") && keyIs(TW_SIZE, "size") &&
              keyIs(TW_CLARITY, "clarity") && keyIs(TW_HIGHCUT, "highcut") && keyIs(TW_MIX, "mix") &&
              keyIs(TW_INGAIN, "ingain") && keyIs(TW_OUTGAIN, "outgain") && keyIs(TW_MODDEPTH, "moddepth") &&
              keyIs(TW_SHIMMER, "shimmer") && keyIs(TW_SHIMMERINT, "shimmerint"));
static_assert(kInfos[TW_PROGRAM].max == Reverb::kPrograms - 1);
static_assert(kInfos[TW_OUTGAIN].min == kOutGainFloorDb);

bool is(const char* a, const char* b) {
    for (; *a && *b; ++a, ++b)
        if (std::tolower(static_cast<unsigned char>(*a)) != std::tolower(static_cast<unsigned char>(*b))) return false;
    return *a == *b;
}

// The 0…1 controls that go through an engine law (all linear floats but the mix, which is a percentage).
bool isUnit(uint32_t id) { return kInfos[id].kind == ParamKind::Float && kInfos[id].law == ParamLaw::Linear && id != TW_MIX; }

} // namespace

const ParamTable& twParams() { return kTable; }

const TwParamInfo& twParamInfo(uint32_t id) { return kInfos[id < TW_PARAM_COUNT ? id : 0]; }

uint32_t twParamByKey(const char* key) {
    if (!key) return TW_PARAM_COUNT;
    const size_t i = kTable.indexOfKey(key);
    return i == kNoParam ? TW_PARAM_COUNT : static_cast<uint32_t>(i);
}

// spux's sanitizing, except that ±∞ goes to the end of the range (as Tearwash always did: "-inf dB" is the
// output gain's floor); spux sends every non-finite value to the default.
double twClamp(uint32_t id, double v) {
    const TwParamInfo& p = twParamInfo(id);
    if (std::isinf(v)) v = v < 0 ? p.min : p.max;
    return sanitizePlain(p, v);
}

int programCount() { return Reverb::kPrograms; }
const char* programName(int index) { return Reverb::programName(clampProgram(index)); }
int clampProgram(int index) { return index < 0 ? 0 : (index >= Reverb::kPrograms ? Reverb::kPrograms - 1 : index); }

void twValueText(uint32_t id, double v, char* buf, uint32_t cap) {
    if (!buf || cap == 0) return;
    const TwParamInfo& p = twParamInfo(id);
    v = twClamp(id, v);
    if (id == TW_PROGRAM) { std::snprintf(buf, cap, "%s", programName(static_cast<int>(v))); return; }
    if (p.kind == ParamKind::Bool) { std::snprintf(buf, cap, "%s", v >= 0.5 ? "On" : "Off"); return; }
    if (id == TW_MIX) { std::snprintf(buf, cap, "%.0f %%", v * 100.0); return; }
    if (p.law == ParamLaw::Db) {
        if (id == TW_OUTGAIN && v <= kOutGainFloorDb) std::snprintf(buf, cap, "-inf dB");
        else std::snprintf(buf, cap, "%+.1f dB", v);
        return;
    }
    switch (id) {
        case TW_LOWDECAY: std::snprintf(buf, cap, "%.2f s", law::decaySeconds(v)); return;
        case TW_DECAY:
            if (v >= law::kHold) std::snprintf(buf, cap, "Hold");
            else std::snprintf(buf, cap, "%.2f s", law::decaySeconds(v));
            return;
        case TW_CROSSOVER: std::snprintf(buf, cap, "%.0f Hz", law::crossoverHz(v)); return;
        case TW_DAMPING: case TW_HIGHCUT:
            if (v >= 0.995) std::snprintf(buf, cap, "Open");
            else std::snprintf(buf, cap, "%.0f Hz", id == TW_DAMPING ? law::dampingHz(v) : law::highCutHz(v));
            return;
        case TW_PREDELAY: std::snprintf(buf, cap, "%.0f ms", law::predelayMs(v)); return;
        case TW_SIZE: std::snprintf(buf, cap, "x%.2f", law::sizeScale(v)); return;
        case TW_MODRATE: std::snprintf(buf, cap, "%.2f Hz", law::modRateHz(v)); return;
        case TW_SHIMMERINT: std::snprintf(buf, cap, "%+d st", law::shimmerSemitones(v)); return;
        default: std::snprintf(buf, cap, "%.0f %%", v * 100.0); return;
    }
}

bool twTextToValue(uint32_t id, const char* text, double* out) {
    if (id >= TW_PARAM_COUNT || !text || !out) return false;
    const TwParamInfo& p = twParamInfo(id);
    if (id == TW_PROGRAM) {
        for (int i = 0; i < programCount(); ++i)
            if (is(text, programName(i))) { *out = i; return true; }
    }
    if (p.kind == ParamKind::Bool) {
        if (is(text, "on")) { *out = 1; return true; }
        if (is(text, "off")) { *out = 0; return true; }
    }
    char* end = nullptr;
    if (isUnit(id)) {   // the inverse of the laws
        if (id == TW_DECAY && is(text, "hold")) { *out = 1.0; return true; }
        if ((id == TW_DAMPING || id == TW_HIGHCUT) && is(text, "open")) { *out = 1.0; return true; }
        const char* t = text[0] == 'x' ? text + 1 : text;
        const double d = std::strtod(t, &end);
        if (end == t || !(d >= 0)) return false;
        double v;
        switch (id) {
            case TW_LOWDECAY: case TW_DECAY: v = std::log(std::max(d, 0.2) / 0.2) / std::log(300.0); break;
            case TW_CROSSOVER: v = std::log(std::max(d, 80.0) / 80.0) / std::log(125.0); break;
            case TW_DAMPING: case TW_HIGHCUT: v = std::log(std::max(d, 1000.0) / 1000.0) / std::log(20.0); break;
            case TW_PREDELAY: v = std::sqrt(d / 1000.0); break;
            case TW_SIZE: v = std::log(std::max(d, 0.5) / 0.5) / std::log(4.0); break;
            case TW_MODRATE: v = std::log(std::max(d, 0.1) / 0.1) / std::log(50.0); break;
            case TW_SHIMMERINT: v = law::shimmerStored(static_cast<int>(std::lround(d))); break;
            default: v = d / 100.0; break;
        }
        if (id == TW_DECAY && v >= law::kHold) v = law::kHold - 0.001;   // a time, not the hold
        *out = twClamp(id, v);
        return true;
    }
    const double d = std::strtod(text, &end);
    if (end == text) return false;
    if (id == TW_MIX) *out = twClamp(id, d / 100.0);
    else *out = twClamp(id, d);
    return true;
}

} // namespace tearwash
