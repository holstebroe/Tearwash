#ifndef TEARWASH_TWPARAMS_HPP
#define TEARWASH_TWPARAMS_HPP

// Tearwash plugin parameters (docs/02_ENGINE.md §5). The table is TwParams.def (spux_params X-macro
// columns); this header gives the ids, the lookups and the value text in the engine's units. Ids and
// keys never change; new parameters are appended.

#include <cstdint>

#include "spux/params/ParamInfo.hpp"
#include "spux/params/ParamTable.hpp"

namespace tearwash {

// The ids, in the order of TwParams.def (TwParams.cpp checks every key against its id).
enum TwParam : uint32_t {
    TW_PROGRAM, TW_DECAY, TW_LOWDECAY, TW_CROSSOVER, TW_DAMPING, TW_DISTANCE, TW_PREDELAY, TW_DIFFUSION,
    TW_MODULATION, TW_MODRATE, TW_REAR, TW_SIZE, TW_CLARITY, TW_HIGHCUT, TW_MIX, TW_INGAIN, TW_OUTGAIN,
    TW_MODDEPTH, TW_SHIMMER, TW_SHIMMERINT,
    TW_PARAM_COUNT
};

using TwParamInfo = spux::params::ParamInfo;

constexpr double kOutGainFloorDb = -60.0; // output gain at its minimum is −∞

const spux::params::ParamTable& twParams();
const TwParamInfo& twParamInfo(uint32_t id);
uint32_t twParamByKey(const char* key);   // TW_PARAM_COUNT if unknown
double twClamp(uint32_t id, double v);    // NaN → default, ±∞ and the rest clamped, stepped kinds rounded

int programCount();
const char* programName(int index);
int clampProgram(int index);

// Value text in the engine's units (Reverb.hpp, law::), and back.
void twValueText(uint32_t id, double v, char* buf, uint32_t cap);
bool twTextToValue(uint32_t id, const char* text, double* out);

} // namespace tearwash

#endif
