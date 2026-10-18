#ifndef TEARWASH_TWPARAMS_HPP
#define TEARWASH_TWPARAMS_HPP

// Tearwash plugin parameters (docs/02_ENGINE.md §5): the single source of truth for ids,
// keys, ranges and value text. Ids and keys never change; new parameters are appended.

#include <cstdint>

namespace tearwash {

enum TwParam : uint32_t {
    TW_PROGRAM, TW_DECAY, TW_LOWDECAY, TW_CROSSOVER, TW_DAMPING, TW_DISTANCE, TW_PREDELAY, TW_DIFFUSION,
    TW_MODULATION, TW_MODRATE, TW_REAR, TW_SIZE, TW_CLARITY, TW_HIGHCUT, TW_MIX, TW_INGAIN, TW_OUTGAIN,
    TW_MODDEPTH, TW_SHIMMER, TW_SHIMMERINT,
    TW_PARAM_COUNT
};

enum class TwKind { Unit, Choice, Toggle, Percent, Decibel };

struct TwParamInfo {
    const char* key;
    const char* name;
    const char* module;
    double min, max, def;
    TwKind kind;
};

constexpr double kOutGainFloorDb = -60.0; // output gain at its minimum is −∞

const TwParamInfo& twParamInfo(uint32_t id);
uint32_t twParamByKey(const char* key);   // TW_PARAM_COUNT if unknown
double twClamp(uint32_t id, double v);

int programCount();
const char* programName(int index);
int clampProgram(int index);

// Value text in the engine's units (Reverb.hpp, law::), and back.
void twValueText(uint32_t id, double v, char* buf, uint32_t cap);
bool twTextToValue(uint32_t id, const char* text, double* out);

} // namespace tearwash

#endif
