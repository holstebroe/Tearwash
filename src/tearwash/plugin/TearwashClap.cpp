#include "TearwashClap.hpp"
#include "TwGui.hpp"
#include <clap/ext/preset-load.h>
#include <clap/factory/preset-discovery.h>
#include <clap/ext/state.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#if defined(__SSE__) || defined(_M_X64)
#include <xmmintrin.h>
#define TEARWASH_HAVE_SSE 1
#endif

namespace tearwash {

// A C++ exception must never cross into the host.
template <class R, class F>
static R guarded(R fallback, F&& f) noexcept {
    try { return f(); } catch (...) { return fallback; }
}
template <class F>
static void guardedVoid(F&& f) noexcept {
    try { f(); } catch (...) {}
}

#ifdef TEARWASH_HAVE_SSE
struct ScopedFlushDenormals {
    unsigned saved = _mm_getcsr();
    ScopedFlushDenormals() { _mm_setcsr(saved | 0x8040u); }
    ~ScopedFlushDenormals() { _mm_setcsr(saved); }
};
#else
struct ScopedFlushDenormals {};
#endif

static TearwashClap* self(const clap_plugin_t* p) { return static_cast<TearwashClap*>(p->plugin_data); }

static const clap_plugin_audio_ports_t g_audioPorts = {
    [](const clap_plugin_t*, bool) -> uint32_t { return 1; },
    [](const clap_plugin_t*, uint32_t index, bool isInput, clap_audio_port_info_t* info) -> bool {
        if (index != 0 || !info) return false;
        info->id = 0;
        std::snprintf(info->name, sizeof(info->name), "%s", isInput ? "Input" : "Output");
        info->channel_count = 2;
        info->flags = CLAP_AUDIO_PORT_IS_MAIN;
        info->port_type = CLAP_PORT_STEREO;
        info->in_place_pair = CLAP_INVALID_ID;
        return true;
    }
};

static const clap_plugin_params_t g_params = {
    [](const clap_plugin_t* p) -> uint32_t { return guarded<uint32_t>(0, [&] { return self(p)->paramsCount(); }); },
    [](const clap_plugin_t* p, uint32_t i, clap_param_info_t* info) -> bool {
        return guarded(false, [&] { return self(p)->paramsInfo(i, info); });
    },
    [](const clap_plugin_t* p, clap_id id, double* v) -> bool {
        return guarded(false, [&] { return self(p)->paramsValue(id, v); });
    },
    [](const clap_plugin_t* p, clap_id id, double v, char* buf, uint32_t cap) -> bool {
        return guarded(false, [&] { return self(p)->paramsValueToText(id, v, buf, cap); });
    },
    [](const clap_plugin_t* p, clap_id id, const char* text, double* v) -> bool {
        return guarded(false, [&] { return self(p)->paramsTextToValue(id, text, v); });
    },
    [](const clap_plugin_t* p, const clap_input_events_t* in, const clap_output_events_t* out) {
        guardedVoid([&] { ScopedFlushDenormals ftz; self(p)->paramsFlush(in, out); });
    }
};

static const clap_plugin_state_t g_state = {
    [](const clap_plugin_t* p, const clap_ostream_t* s) -> bool { return guarded(false, [&] { return self(p)->stateSave(s); }); },
    [](const clap_plugin_t* p, const clap_istream_t* s) -> bool { return guarded(false, [&] { return self(p)->stateLoad(s); }); },
};

TearwashClap::TearwashClap(const clap_host_t* host) : host_(host) {
    clapPlugin_.desc = nullptr;
    clapPlugin_.plugin_data = this;
    clapPlugin_.init = [](const clap_plugin_t* p) -> bool { return guarded(false, [&] { return self(p)->init(); }); };
    clapPlugin_.destroy = [](const clap_plugin_t* p) { guardedVoid([&] { self(p)->destroy(); }); };
    clapPlugin_.activate = [](const clap_plugin_t* p, double sr, uint32_t mn, uint32_t mx) -> bool {
        return guarded(false, [&] { return self(p)->activate(sr, mn, mx); });
    };
    clapPlugin_.deactivate = [](const clap_plugin_t* p) { guardedVoid([&] { self(p)->deactivate(); }); };
    clapPlugin_.start_processing = [](const clap_plugin_t* p) -> bool { return guarded(false, [&] { return self(p)->startProcessing(); }); };
    clapPlugin_.stop_processing = [](const clap_plugin_t* p) { guardedVoid([&] { self(p)->stopProcessing(); }); };
    clapPlugin_.reset = [](const clap_plugin_t* p) { guardedVoid([&] { self(p)->reset(); }); };
    clapPlugin_.process = [](const clap_plugin_t* p, const clap_process_t* pr) -> clap_process_status {
        return guarded<clap_process_status>(CLAP_PROCESS_CONTINUE, [&] { return self(p)->process(pr); });
    };
    clapPlugin_.get_extension = [](const clap_plugin_t* p, const char* id) -> const void* {
        return guarded<const void*>(nullptr, [&] { return self(p)->getExtension(id); });
    };
    clapPlugin_.on_main_thread = [](const clap_plugin_t* p) { guardedVoid([&] { self(p)->onMainThread(); }); };

    for (uint32_t i = 0; i < TW_PARAM_COUNT; ++i) values_[i].store(twParamInfo(i).def);
    for (uint32_t i = 0; i < TW_PARAM_COUNT; ++i) presetValues_[i] = values_[i].load();
    targetProgram_.store(program());   // the defaults are the first program's settings
}

TearwashClap::~TearwashClap() = default;

void TearwashClap::destroy() {
    destroyGuiWindow();
    delete this;
}

void TearwashClap::createGuiWindow() {
    if (!gui_) gui_ = std::make_unique<TwGui>(this);
}

void TearwashClap::destroyGuiWindow() { gui_.reset(); }

// --- Programs ------------------------------------------------------------------------------------

Controls TearwashClap::controls() const {
    Controls c;
    c.decay = paramValue(TW_DECAY); c.lowDecay = paramValue(TW_LOWDECAY); c.crossover = paramValue(TW_CROSSOVER);
    c.damping = paramValue(TW_DAMPING); c.distance = paramValue(TW_DISTANCE); c.predelay = paramValue(TW_PREDELAY);
    c.diffusion = paramValue(TW_DIFFUSION); c.modRate = paramValue(TW_MODRATE); c.size = paramValue(TW_SIZE);
    c.clarity = paramValue(TW_CLARITY); c.highCut = paramValue(TW_HIGHCUT);
    c.modulation = paramValue(TW_MODULATION) >= 0.5;
    c.modDepth = paramValue(TW_MODDEPTH);
    c.shimmer = paramValue(TW_SHIMMER);
    c.shimmerInterval = law::shimmerSemitones(paramValue(TW_SHIMMERINT));
    c.shimmerMode = c.shimmer > 0.0;
    return c;
}

// Main thread: the program's stored settings replace the engine parameters.
void TearwashClap::loadProgramSettings(int pg) {
    pg = clampProgram(pg);
    targetProgram_.store(pg);
    const TwValues v = twProgramValues(pg);
    for (uint32_t i = 0; i < TW_PARAM_COUNT; ++i)
        if (i != TW_MIX && i != TW_INGAIN && i != TW_OUTGAIN && i != TW_REAR && i != TW_SHIMMER && i != TW_SHIMMERINT)
            setValue(i, v[i]);
}

void TearwashClap::selectProgram(int pg) {
    pg = clampProgram(pg);
    double old[TW_PARAM_COUNT];
    for (uint32_t i = 0; i < TW_PARAM_COUNT; ++i) old[i] = paramValue(i);
    loadProgramSettings(pg);
    for (uint32_t i = 0; i < TW_PARAM_COUNT; ++i) {
        const double v = paramValue(i);
        if (v == old[i]) continue;
        queueOut({ CLAP_EVENT_PARAM_GESTURE_BEGIN, i, 0.0, CLAP_EVENT_IS_LIVE });
        queueOut({ CLAP_EVENT_PARAM_VALUE, i, v, CLAP_EVENT_IS_LIVE });
        queueOut({ CLAP_EVENT_PARAM_GESTURE_END, i, 0.0, CLAP_EVENT_IS_LIVE });
    }
    requestFlush();
    markDirty();
}

// Audio thread: the engine follows the program parameter at once; a change the parameters do not
// hold the settings for yet (a host edit) asks the main thread to load them.
void TearwashClap::checkProgramChange() {
    const int pg = program();
    if (pg != runningProgram_) {
        rev_.setProgram(pg);
        runningProgram_ = pg;
    }
    if (pg == targetProgram_.load() || pg == askedProgram_) return;
    askedProgram_ = pg;
    programRequest_.store(true);
    if (host_ && host_->request_callback) host_->request_callback(host_);
}

// Audio thread: control changes of the running program.
void TearwashClap::applyControls() {
    const Controls c = controls();
    if (c != revControls_) { revControls_ = c; rev_.setControls(c); }
}

// --- Lifecycle -----------------------------------------------------------------------------------

bool TearwashClap::activate(double sampleRate, uint32_t, uint32_t maxFrames) {
    fs_ = sampleRate;
    maxFrames_ = std::max<uint32_t>(maxFrames, 64);
    for (auto& s : scratch_) s.assign(maxFrames_, 0.f);
    gainCoef_ = 1.0 - std::exp(-1.0 / (0.010 * fs_));
    rev_.setSampleRate(fs_);
    runningProgram_ = program();
    rev_.setProgram(runningProgram_);
    revControls_ = controls();
    rev_.setControls(revControls_);
    rev_.reset();
    askedProgram_ = -1;
    active_ = true;
    inGain_ = std::pow(10.0, paramValue(TW_INGAIN) / 20.0);
    mix_ = paramValue(TW_MIX);
    outGain_ = paramValue(TW_OUTGAIN) <= kOutGainFloorDb ? 0.0 : std::pow(10.0, paramValue(TW_OUTGAIN) / 20.0);
    return true;
}

void TearwashClap::reset() {
    if (active_) rev_.reset();
}

void TearwashClap::markDirty() {
    stateDirty_.store(true);
    if (host_ && host_->request_callback) host_->request_callback(host_);
}

void TearwashClap::onMainThread() {
    if (programRequest_.exchange(false)) {
        loadProgramSettings(program());
        rescanValues_.store(true);
    }
    if (rescanValues_.exchange(false) && host_) {
        const auto* hp = static_cast<const clap_host_params_t*>(host_->get_extension(host_, CLAP_EXT_PARAMS));
        if (hp && hp->rescan) hp->rescan(host_, CLAP_PARAM_RESCAN_VALUES | CLAP_PARAM_RESCAN_TEXT);
    }
    if (stateDirty_.exchange(false) && host_) {
        const auto* hs = static_cast<const clap_host_state_t*>(host_->get_extension(host_, CLAP_EXT_STATE));
        if (hs && hs->mark_dirty) hs->mark_dirty(host_);
    }
}

// --- Processing ----------------------------------------------------------------------------------

void TearwashClap::handleEvent(const clap_event_header_t* hdr) {
    if (!hdr || hdr->size < sizeof(clap_event_header_t) || hdr->space_id != CLAP_CORE_EVENT_SPACE_ID) return;
    if (hdr->type != CLAP_EVENT_PARAM_VALUE || hdr->size < sizeof(clap_event_param_value_t)) return;
    const auto* ev = reinterpret_cast<const clap_event_param_value_t*>(hdr);
    if (ev->param_id < TW_PARAM_COUNT) setValue(ev->param_id, ev->value);
}

void TearwashClap::render(const float* inL, const float* inR, float* outL, float* outR, uint32_t n) {
    float* xl = scratch_[0].data();
    float* xr = scratch_[1].data();
    float* w[4] = { scratch_[2].data(), scratch_[3].data(), scratch_[4].data(), scratch_[5].data() };
    const double inTarget = std::pow(10.0, paramValue(TW_INGAIN) / 20.0);
    const double og = paramValue(TW_OUTGAIN);
    const double outTarget = og <= kOutGainFloorDb ? 0.0 : std::pow(10.0, og / 20.0);
    const double mixTarget = paramValue(TW_MIX);
    float peak = 0.f;
    double ig = inGain_;
    for (uint32_t i = 0; i < n; ++i) {
        ig += (inTarget - ig) * gainCoef_;
        const float l = inL ? inL[i] : 0.f, r = inR ? inR[i] : l;
        xl[i] = static_cast<float>(l * ig);
        xr[i] = static_cast<float>(r * ig);
        peak = std::max(peak, std::max(std::fabs(xl[i]), std::fabs(xr[i])));
    }
    inGain_ = ig;

    rev_.process(xl, xr, w, static_cast<int>(n));
    const bool rear = paramValue(TW_REAR) >= 0.5;
    float* wl = rear ? w[1] : w[0];
    float* wr = rear ? w[3] : w[2];

    double mx = mix_, go = outGain_;
    for (uint32_t i = 0; i < n; ++i) {
        mx += (mixTarget - mx) * gainCoef_;
        go += (outTarget - go) * gainCoef_;
        const float dl = inL ? inL[i] : 0.f, dr = inR ? inR[i] : dl;
        const double yl = (1.0 - mx) * dl + mx * wl[i];
        const double yr = (1.0 - mx) * dr + mx * wr[i];
        if (outL) outL[i] = static_cast<float>(yl * go);
        if (outR) outR[i] = static_cast<float>(yr * go);
    }
    mix_ = mx;
    outGain_ = go;

    // Headroom meter: five LEDs 6 dB apart up to full scale.
    peakHold_ = std::max(peak, peakHold_ * static_cast<float>(std::exp(-double(n) / (0.3 * fs_))));
    static const float kLed[5] = { 0.056f, 0.112f, 0.224f, 0.448f, 0.89f };
    int leds = 0;
    while (leds < 5 && peakHold_ >= kLed[leds]) ++leds;
    headroom_.store(leds, std::memory_order_relaxed);
    if (peak >= 1.0f) overload_.store(true, std::memory_order_relaxed);
    else if (peakHold_ < 0.5f) overload_.store(false, std::memory_order_relaxed);
}

clap_process_status TearwashClap::process(const clap_process_t* process) {
    if (!process) return CLAP_PROCESS_CONTINUE;
    ScopedFlushDenormals ftz;

    const uint32_t frames = process->frames_count;
    const uint32_t numEvents = process->in_events ? process->in_events->size(process->in_events) : 0;
    const bool haveIn = process->audio_inputs && process->audio_inputs_count > 0 && process->audio_inputs[0].data32;
    const bool haveOut = process->audio_outputs && process->audio_outputs_count > 0 && process->audio_outputs[0].data32;
    const float* inL = (haveIn && process->audio_inputs[0].channel_count > 0) ? process->audio_inputs[0].data32[0] : nullptr;
    const float* inR = (haveIn && process->audio_inputs[0].channel_count > 1) ? process->audio_inputs[0].data32[1] : inL;
    float* outL = (haveOut && process->audio_outputs[0].channel_count > 0) ? process->audio_outputs[0].data32[0] : nullptr;
    float* outR = (haveOut && process->audio_outputs[0].channel_count > 1) ? process->audio_outputs[0].data32[1] : nullptr;

    uint32_t ei = 0;
    for (uint32_t frame = 0; frame < frames;) {
        while (ei < numEvents) {
            const clap_event_header_t* hdr = process->in_events->get(process->in_events, ei);
            if (hdr && hdr->time > frame) break;
            handleEvent(hdr);
            ++ei;
        }
        uint32_t next = frames;
        if (ei < numEvents) {
            const clap_event_header_t* hdr = process->in_events->get(process->in_events, ei);
            if (hdr && hdr->time < next) next = hdr->time;
        }
        checkProgramChange();
        applyControls();
        uint32_t todo = next > frame ? next - frame : 0;
        if (todo == 0) { ++ei; continue; }
        while (todo > 0) {
            const uint32_t m = std::min(todo, maxFrames_);
            if (active_ && maxFrames_ > 0) {
                render(inL ? inL + frame : nullptr, inR ? inR + frame : nullptr, outL ? outL + frame : nullptr,
                       outR ? outR + frame : nullptr, m);
            } else {
                for (uint32_t i = 0; i < m; ++i) {
                    if (outL) outL[frame + i] = inL ? inL[frame + i] : 0.f;
                    if (outR) outR[frame + i] = inR ? inR[frame + i] : 0.f;
                }
            }
            frame += m;
            todo -= m;
        }
    }
    while (ei < numEvents) handleEvent(process->in_events->get(process->in_events, ei++));
    pushOut(process->out_events);
    return CLAP_PROCESS_CONTINUE;
}

// CLAP preset-load: factory presets by name (load key), user presets by file.
static bool presetLoadFromLocation(const clap_plugin_t* p, uint32_t kind, const char* location, const char* key) {
    auto* self = static_cast<TearwashClap*>(p->plugin_data);
    bool ok = false;
    if (kind == CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN && key) ok = self->loadPreset(self->presetLibrary().indexOf(key));
    else if (kind == CLAP_PRESET_DISCOVERY_LOCATION_FILE && location) ok = self->loadPresetFile(location);
    const clap_host_t* h = self->host();
    if (h) {
        const auto* hp = static_cast<const clap_host_preset_load_t*>(h->get_extension(h, CLAP_EXT_PRESET_LOAD));
        if (hp && ok && hp->loaded) hp->loaded(h, kind, location, key);
        if (hp && !ok && hp->on_error) hp->on_error(h, kind, location, key, 0, "preset not found");
    }
    return ok;
}
static const clap_plugin_preset_load_t g_presetLoad = { presetLoadFromLocation };

const void* TearwashClap::getExtension(const char* id) {
    if (!id) return nullptr;
    if (std::strcmp(id, CLAP_EXT_PRESET_LOAD) == 0 || std::strcmp(id, CLAP_EXT_PRESET_LOAD_COMPAT) == 0) return &g_presetLoad;
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &g_audioPorts;
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) return &g_params;
    if (std::strcmp(id, CLAP_EXT_STATE) == 0) return &g_state;
    if (std::strcmp(id, CLAP_EXT_GUI) == 0) return &g_tearwashGuiExtension;
    return nullptr;
}

// --- Params --------------------------------------------------------------------------------------

bool TearwashClap::paramsInfo(uint32_t index, clap_param_info_t* info) const {
    if (index >= TW_PARAM_COUNT || !info) return false;
    std::memset(info, 0, sizeof(*info));
    const TwParamInfo& p = twParamInfo(index);
    info->id = index;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE;
    if (p.kind == TwKind::Choice || p.kind == TwKind::Toggle) info->flags |= CLAP_PARAM_IS_STEPPED;
    if (p.kind == TwKind::Choice) info->flags |= CLAP_PARAM_IS_ENUM;
    std::snprintf(info->name, sizeof(info->name), "%s", p.name);
    std::snprintf(info->module, sizeof(info->module), "%s", p.module);
    info->min_value = p.min;
    info->max_value = p.max;
    info->default_value = p.def;
    return true;
}

bool TearwashClap::paramsValue(clap_id id, double* out) const {
    if (id >= TW_PARAM_COUNT || !out) return false;
    *out = values_[id].load(std::memory_order_relaxed);
    return true;
}

bool TearwashClap::paramsValueToText(clap_id id, double value, char* buf, uint32_t cap) const {
    if (id >= TW_PARAM_COUNT || !buf || cap == 0) return false;
    twValueText(id, value, buf, cap);
    return true;
}

bool TearwashClap::paramsTextToValue(clap_id id, const char* text, double* out) const {
    return twTextToValue(id, text, out);
}

void TearwashClap::paramsFlush(const clap_input_events_t* in, const clap_output_events_t* out) {
    if (in && in->size && in->get) {
        const uint32_t n = in->size(in);
        for (uint32_t i = 0; i < n; ++i) handleEvent(in->get(in, i));
    }
    // Not processing: changes reach the engine at the next process call.
    if (active_) checkProgramChange();
    pushOut(out);
}

void TearwashClap::requestFlush() {
    if (!host_) return;
    const auto* hp = static_cast<const clap_host_params_t*>(host_->get_extension(host_, CLAP_EXT_PARAMS));
    if (hp && hp->request_flush) hp->request_flush(host_);
    else if (host_->request_process) host_->request_process(host_);
}

void TearwashClap::queueOut(const OutEvent& ev) {
    std::lock_guard<std::mutex> lock(outMutex_);
    if (outCount_ < kOutCapacity) outQueue_[outCount_++] = ev;
}

void TearwashClap::pushOut(const clap_output_events_t* out) {
    if (!out || !out->try_push) return;
    size_t count = 0;
    {
        std::unique_lock<std::mutex> lock(outMutex_, std::try_to_lock);
        if (!lock.owns_lock()) return;
        count = outCount_;
        std::copy(outQueue_.begin(), outQueue_.begin() + static_cast<long>(count), drain_.begin());
        outCount_ = 0;
    }
    for (size_t i = 0; i < count; ++i) {
        const OutEvent& ev = drain_[i];
        if (ev.type == CLAP_EVENT_PARAM_VALUE) {
            clap_event_param_value_t e{};
            e.header.size = sizeof(e);
            e.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
            e.header.type = CLAP_EVENT_PARAM_VALUE;
            e.header.flags = ev.flags;
            e.param_id = ev.paramId;
            e.note_id = -1; e.port_index = -1; e.channel = -1; e.key = -1;
            e.value = ev.value;
            out->try_push(out, &e.header);
        } else {
            clap_event_param_gesture_t e{};
            e.header.size = sizeof(e);
            e.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
            e.header.type = ev.type;
            e.header.flags = ev.flags;
            e.param_id = ev.paramId;
            out->try_push(out, &e.header);
        }
    }
}

void TearwashClap::onBeginEditFromGui(clap_id id) {
    if (id >= TW_PARAM_COUNT) return;
    queueOut({ CLAP_EVENT_PARAM_GESTURE_BEGIN, id, 0.0, CLAP_EVENT_IS_LIVE });
    requestFlush();
}

void TearwashClap::onParamValueFromGui(clap_id id, double value) {
    if (id >= TW_PARAM_COUNT) return;
    if (id == TW_PROGRAM) {
        selectProgram(static_cast<int>(twClamp(id, value)));
        return;
    }
    setValue(id, value);
    queueOut({ CLAP_EVENT_PARAM_VALUE, id, values_[id].load(), CLAP_EVENT_IS_LIVE });
    requestFlush();
    markDirty();
}

void TearwashClap::onEndEditFromGui(clap_id id) {
    if (id >= TW_PARAM_COUNT) return;
    queueOut({ CLAP_EVENT_PARAM_GESTURE_END, id, 0.0, CLAP_EVENT_IS_LIVE });
    requestFlush();
}

// --- Presets -------------------------------------------------------------------------------------

void TearwashClap::rescanPresets() { presets_.scanUserPresets(); }

bool TearwashClap::loadPresetValues(const TwValues& values, const std::string& name) {
    double old[TW_PARAM_COUNT];
    for (uint32_t i = 0; i < TW_PARAM_COUNT; ++i) old[i] = paramValue(i);
    for (uint32_t i = 0; i < TW_PARAM_COUNT; ++i) setValue(i, values[i]);
    targetProgram_.store(program());   // the parameters hold the settings: nothing to load
    {
        std::lock_guard<std::mutex> lock(presetMutex_);
        presetName_ = name;
        for (uint32_t i = 0; i < TW_PARAM_COUNT; ++i) presetValues_[i] = paramValue(i);
    }
    for (uint32_t i = 0; i < TW_PARAM_COUNT; ++i) {
        const double v = paramValue(i);
        if (v == old[i]) continue;
        queueOut({ CLAP_EVENT_PARAM_GESTURE_BEGIN, i, 0.0, CLAP_EVENT_IS_LIVE });
        queueOut({ CLAP_EVENT_PARAM_VALUE, i, v, CLAP_EVENT_IS_LIVE });
        queueOut({ CLAP_EVENT_PARAM_GESTURE_END, i, 0.0, CLAP_EVENT_IS_LIVE });
    }
    requestFlush();
    markDirty();
    return true;
}

bool TearwashClap::loadPreset(int index) {
    if (index < 0 || index >= presets_.count()) return false;
    const TwPreset& p = presets_.presets()[static_cast<size_t>(index)];
    return loadPresetValues(p.values, p.name);
}

bool TearwashClap::loadPresetFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::stringstream ss;
    ss << in.rdbuf();
    TwValues v = twDefaultValues();
    std::string name;
    if (!parsePresetText(ss.str(), v, &name)) return false;
    if (name.empty()) {
        const size_t slash = path.find_last_of("/\\");
        name = path.substr(slash == std::string::npos ? 0 : slash + 1);
        const size_t dot = name.rfind('.');
        if (dot != std::string::npos) name.resize(dot);
    }
    return loadPresetValues(v, name);
}

bool TearwashClap::savePreset(const std::string& path, std::string& error) {
    TwValues v;
    for (uint32_t i = 0; i < TW_PARAM_COUNT; ++i) v[i] = paramValue(i);
    std::string name;
    if (!presets_.saveUserPreset(path, v, name, error)) return false;
    std::lock_guard<std::mutex> lock(presetMutex_);
    presetName_ = name;
    presetValues_ = v;
    return true;
}

void TearwashClap::stepPreset(int delta) {
    const int n = presets_.count();
    if (n <= 0) return;
    int i = presetIndex();
    i = i < 0 ? 0 : ((i + delta) % n + n) % n;
    loadPreset(i);
}

std::string TearwashClap::presetName() const {
    std::lock_guard<std::mutex> lock(presetMutex_);
    return presetName_;
}

bool TearwashClap::presetModified() const {
    std::lock_guard<std::mutex> lock(presetMutex_);
    for (uint32_t i = 0; i < TW_PARAM_COUNT; ++i)
        if (std::fabs(paramValue(i) - presetValues_[i]) > 1e-9) return true;
    return false;
}

int TearwashClap::presetIndex() const { return presets_.indexOf(presetName()); }

// --- State ---------------------------------------------------------------------------------------

std::string TearwashClap::stateText() const {
    std::string out = "tearwash 1\n";
    out += "preset=" + presetName() + "\n";
    char buf[96];
    for (uint32_t i = 0; i < TW_PARAM_COUNT; ++i) {
        std::snprintf(buf, sizeof(buf), "%s=%.17g\n", twParamInfo(i).key, paramValue(i));
        out += buf;
    }
    return out;
}

bool TearwashClap::loadStateText(const std::string& text) {
    std::istringstream in(text);
    std::string line;
    if (!std::getline(in, line) || line.compare(0, 9, "tearwash ") != 0) return false;
    double v[TW_PARAM_COUNT];
    for (uint32_t i = 0; i < TW_PARAM_COUNT; ++i) v[i] = twParamInfo(i).def;
    std::string preset;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        if (line.compare(0, eq, "preset") == 0) { preset = line.substr(eq + 1); continue; }
        const uint32_t id = twParamByKey(line.substr(0, eq).c_str());
        if (id < TW_PARAM_COUNT) v[id] = std::strtod(line.c_str() + eq + 1, nullptr);
    }
    for (uint32_t i = 0; i < TW_PARAM_COUNT; ++i) setValue(i, v[i]);
    targetProgram_.store(program());
    // The preset the state came from: its stored values (so the modified marker is right), or the
    // state itself when the preset is not in the library.
    std::lock_guard<std::mutex> lock(presetMutex_);
    presetName_ = preset.empty() ? std::string("IN Init") : preset;
    const int idx = presets_.indexOf(presetName_);
    for (uint32_t i = 0; i < TW_PARAM_COUNT; ++i)
        presetValues_[i] = idx >= 0 ? presets_.presets()[static_cast<size_t>(idx)].values[i] : paramValue(i);
    return true;
}

bool TearwashClap::stateSave(const clap_ostream_t* stream) {
    if (!stream || !stream->write) return false;
    const std::string s = stateText();
    size_t done = 0;
    while (done < s.size()) {
        const int64_t n = stream->write(stream, s.data() + done, s.size() - done);
        if (n <= 0) return false;
        done += static_cast<size_t>(n);
    }
    return true;
}

bool TearwashClap::stateLoad(const clap_istream_t* stream) {
    if (!stream || !stream->read) return false;
    std::string data;
    char chunk[4096];
    while (data.size() < (1u << 16)) {
        const int64_t n = stream->read(stream, chunk, sizeof(chunk));
        if (n < 0) return false;
        if (n == 0) break;
        data.append(chunk, static_cast<size_t>(std::min<int64_t>(n, sizeof(chunk))));
    }
    return loadStateText(data);
}

// --- Entry ---------------------------------------------------------------------------------------

static const char* g_features[] = { CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_REVERB,
                                    CLAP_PLUGIN_FEATURE_STEREO, nullptr };

static const clap_plugin_descriptor_t g_descriptor = {
    CLAP_VERSION,
    "com.holstebroe.tearwash",
    "Tearwash",
    "holstebroe",
    "https://github.com/holstebroe/Tearwash",
    "",
    "",
    "0.1.0",
    "Algorithmic stereo reverb: halls, rooms, plates and vast spaces",
    g_features,
};

static uint32_t factoryCount(const clap_plugin_factory_t*) { return 1; }
static const clap_plugin_descriptor_t* factoryDescriptor(const clap_plugin_factory_t*, uint32_t i) {
    return i == 0 ? &g_descriptor : nullptr;
}
static const clap_plugin_t* factoryCreate(const clap_plugin_factory_t*, const clap_host_t* host, const char* id) {
    if (!host || !id || !clap_version_is_compatible(host->clap_version)) return nullptr;
    if (std::strcmp(id, g_descriptor.id) != 0) return nullptr;
    try {
        auto* p = new TearwashClap(host);
        return p->getClapPlugin();
    } catch (...) {
        return nullptr;
    }
}

static const clap_plugin_factory_t g_factory = { factoryCount, factoryDescriptor, factoryCreate };

// --- Preset discovery: the built-in library as plugin presets, the user folder as files.

static const clap_preset_discovery_provider_descriptor_t g_providerDesc = {
    CLAP_VERSION, "com.holstebroe.tearwash.presets", "Tearwash presets", "holstebroe"
};

struct PresetProvider {
    clap_preset_discovery_provider_t provider{};
    const clap_preset_discovery_indexer_t* indexer = nullptr;
    std::string userDir;
};

static void describe(const clap_preset_discovery_metadata_receiver_t* r, uint32_t flags, const std::string& category) {
    static const clap_universal_plugin_id_t kId = { "clap", "com.holstebroe.tearwash" };
    if (r->add_plugin_id) r->add_plugin_id(r, &kId);
    if (r->set_flags) r->set_flags(r, flags);
    if (r->add_feature) r->add_feature(r, CLAP_PLUGIN_FEATURE_REVERB);
    if (r->add_feature && !category.empty()) r->add_feature(r, category.c_str());
}

static bool providerInit(const clap_preset_discovery_provider_t* p) {
    auto* self = static_cast<PresetProvider*>(p->provider_data);
    const clap_preset_discovery_indexer_t* ix = self->indexer;
    const clap_preset_discovery_filetype_t ft = { "Tearwash preset", "", "twpreset" };
    ix->declare_filetype(ix, &ft);
    const clap_preset_discovery_location_t factory = { CLAP_PRESET_DISCOVERY_IS_FACTORY_CONTENT, "Tearwash factory",
                                                       CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, nullptr };
    ix->declare_location(ix, &factory);
    self->userDir = TwPresetLibrary::userPresetDirectory();
    if (!self->userDir.empty()) {
        const clap_preset_discovery_location_t user = { CLAP_PRESET_DISCOVERY_IS_USER_CONTENT, "Tearwash user",
                                                        CLAP_PRESET_DISCOVERY_LOCATION_FILE, self->userDir.c_str() };
        ix->declare_location(ix, &user);
    }
    return true;
}

static void providerDestroy(const clap_preset_discovery_provider_t* p) { delete static_cast<PresetProvider*>(p->provider_data); }

static bool providerMetadata(const clap_preset_discovery_provider_t*, uint32_t kind, const char* location,
                             const clap_preset_discovery_metadata_receiver_t* r) {
    if (kind == CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN) {
        static const TwPresetLibrary lib;
        for (const TwPreset& p : lib.presets()) {
            if (p.user) continue;
            if (!r->begin_preset(r, p.name.c_str(), p.name.c_str())) break;
            describe(r, CLAP_PRESET_DISCOVERY_IS_FACTORY_CONTENT, TwPresetLibrary::categoryName(p.category));
        }
        return true;
    }
    if (!location) return false;
    std::ifstream in(location, std::ios::binary);
    if (!in) return false;
    std::stringstream ss;
    ss << in.rdbuf();
    TwValues v = twDefaultValues();
    std::string name;
    if (!parsePresetText(ss.str(), v, &name)) return false;
    if (name.empty()) name = location;
    if (r->begin_preset(r, name.c_str(), nullptr)) describe(r, CLAP_PRESET_DISCOVERY_IS_USER_CONTENT, "User");
    return true;
}

static const void* providerExtension(const clap_preset_discovery_provider_t*, const char*) { return nullptr; }

static uint32_t discoveryCount(const clap_preset_discovery_factory_t*) { return 1; }
static const clap_preset_discovery_provider_descriptor_t* discoveryDescriptor(const clap_preset_discovery_factory_t*, uint32_t i) {
    return i == 0 ? &g_providerDesc : nullptr;
}
static const clap_preset_discovery_provider_t* discoveryCreate(const clap_preset_discovery_factory_t*,
                                                               const clap_preset_discovery_indexer_t* indexer, const char* id) {
    if (!indexer || !id || std::strcmp(id, g_providerDesc.id) != 0) return nullptr;
    auto* self = new PresetProvider;
    self->indexer = indexer;
    self->provider.desc = &g_providerDesc;
    self->provider.provider_data = self;
    self->provider.init = providerInit;
    self->provider.destroy = providerDestroy;
    self->provider.get_metadata = providerMetadata;
    self->provider.get_extension = providerExtension;
    return &self->provider;
}
static const clap_preset_discovery_factory_t g_discovery = { discoveryCount, discoveryDescriptor, discoveryCreate };

static bool entryInit(const char*) { return true; }
static void entryDeinit() {}
static const void* entryGetFactory(const char* id) {
    if (!id) return nullptr;
    if (std::strcmp(id, CLAP_PLUGIN_FACTORY_ID) == 0) return &g_factory;
    if (std::strcmp(id, CLAP_PRESET_DISCOVERY_FACTORY_ID) == 0 || std::strcmp(id, CLAP_PRESET_DISCOVERY_FACTORY_ID_COMPAT) == 0)
        return &g_discovery;
    return nullptr;
}

} // namespace tearwash

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry = {
    CLAP_VERSION, tearwash::entryInit, tearwash::entryDeinit, tearwash::entryGetFactory
};
