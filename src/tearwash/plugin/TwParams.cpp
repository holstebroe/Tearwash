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

TwParamInfo g_info[TW_PARAM_COUNT] = {
    { "program",    "Program",       "Program", 0, Reverb::kPrograms - 1, 0, TwKind::Choice },
    { "decay",      "High Decay",    "Decay",   0, 1, 0, TwKind::Unit },
    { "lowdecay",   "Low Decay",     "Decay",   0, 1, 0, TwKind::Unit },
    { "crossover",  "Split Freq",    "Decay",   0, 1, 0, TwKind::Unit },
    { "damping",    "Damp Cut",      "Decay",   0, 1, 0, TwKind::Unit },
    { "distance",   "Distance",      "Space",   0, 1, 0, TwKind::Unit },
    { "predelay",   "Predelay",      "Space",   0, 1, 0, TwKind::Unit },
    { "diffusion",  "Diffusion",     "Space",   0, 1, 0, TwKind::Unit },
    { "modulation", "Modulation",    "Motion",  0, 1, 1, TwKind::Toggle },
    { "modrate",    "Mod Rate",      "Motion",  0, 1, 0, TwKind::Unit },
    { "rear",       "Rear Outputs",  "Output",  0, 1, 0, TwKind::Toggle },
    { "size",       "Size",          "Space",   0, 1, 0, TwKind::Unit },
    { "clarity",    "Clarity",       "Space",   0, 1, 0, TwKind::Unit },
    { "highcut",    "In Cut",        "Decay",   0, 1, 0, TwKind::Unit },
    { "mix",        "Mix",           "Output",  0, 1, 0.35, TwKind::Percent },
    { "ingain",     "Input Gain",    "Output",  -12, 12, 0, TwKind::Decibel },
    { "outgain",    "Output Gain",   "Output",  kOutGainFloorDb, 12, 0, TwKind::Decibel },
    { "moddepth",   "Mod Depth",     "Motion",  0, 1, 0.5, TwKind::Unit },
    { "shimmer",    "Shimmer",       "Shimmer", 0, 1, 0, TwKind::Unit },
    { "shimmerint", "Shimmer Interval", "Shimmer", 0, 1, 2.0 / 3.0, TwKind::Unit },   // +12 st
};

// Defaults: the first program's stored settings.
bool initDefaults() {
    const Controls c = Reverb::programDefaults(0);
    const struct { uint32_t id; double v; } map[] = {
        { TW_DECAY, c.decay }, { TW_LOWDECAY, c.lowDecay }, { TW_CROSSOVER, c.crossover }, { TW_DAMPING, c.damping },
        { TW_DISTANCE, c.distance }, { TW_PREDELAY, c.predelay }, { TW_DIFFUSION, c.diffusion }, { TW_MODRATE, c.modRate },
        { TW_SIZE, c.size }, { TW_CLARITY, c.clarity }, { TW_HIGHCUT, c.highCut },
    };
    for (const auto& m : map) g_info[m.id].def = m.v;
    g_info[TW_MODULATION].def = c.modulation ? 1.0 : 0.0;
    g_info[TW_MODDEPTH].def = c.modDepth;
    return true;
}

bool is(const char* a, const char* b) {
    for (; *a && *b; ++a, ++b)
        if (std::tolower(static_cast<unsigned char>(*a)) != std::tolower(static_cast<unsigned char>(*b))) return false;
    return *a == *b;
}

} // namespace

const TwParamInfo& twParamInfo(uint32_t id) {
    static const bool init = initDefaults();
    (void)init;
    return g_info[id < TW_PARAM_COUNT ? id : 0];
}

uint32_t twParamByKey(const char* key) {
    if (!key) return TW_PARAM_COUNT;
    for (uint32_t i = 0; i < TW_PARAM_COUNT; ++i)
        if (std::strcmp(twParamInfo(i).key, key) == 0) return i;
    return TW_PARAM_COUNT;
}

double twClamp(uint32_t id, double v) {
    const TwParamInfo& p = twParamInfo(id);
    if (!(v == v)) v = p.def;
    if (v < p.min) v = p.min;
    if (v > p.max) v = p.max;
    if (p.kind == TwKind::Choice || p.kind == TwKind::Toggle) v = std::floor(v + 0.5);
    return v;
}

int programCount() { return Reverb::kPrograms; }
const char* programName(int index) { return Reverb::programName(clampProgram(index)); }
int clampProgram(int index) { return index < 0 ? 0 : (index >= Reverb::kPrograms ? Reverb::kPrograms - 1 : index); }

void twValueText(uint32_t id, double v, char* buf, uint32_t cap) {
    if (!buf || cap == 0) return;
    const TwParamInfo& p = twParamInfo(id);
    v = twClamp(id, v);
    if (id == TW_PROGRAM) { std::snprintf(buf, cap, "%s", programName(static_cast<int>(v))); return; }
    if (p.kind == TwKind::Toggle) { std::snprintf(buf, cap, "%s", v >= 0.5 ? "On" : "Off"); return; }
    if (p.kind == TwKind::Percent) { std::snprintf(buf, cap, "%.0f %%", v * 100.0); return; }
    if (p.kind == TwKind::Decibel) {
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
    if (p.kind == TwKind::Toggle) {
        if (is(text, "on")) { *out = 1; return true; }
        if (is(text, "off")) { *out = 0; return true; }
    }
    char* end = nullptr;
    if (p.kind == TwKind::Unit) {   // the inverse of the laws
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
    if (p.kind == TwKind::Percent) *out = twClamp(id, d / 100.0);
    else *out = twClamp(id, d);
    return true;
}

} // namespace tearwash
