#include "TearwashClap.hpp"
#include "spux/core/DenormalGuard.hpp"
#ifdef TEARWASH_WITH_EDITOR
#include "TwEditor.hpp"
#endif
#include "TwState.hpp"
#include "spux/clap/ClapHelpers.hpp"
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

namespace tearwash {

TearwashClap::TearwashClap(const clap_host_t* host) : Plugin(&descriptor(), host) {
    for (uint32_t i = 0; i < TW_PARAM_COUNT; ++i) presetValues_[i] = paramValue(i);   // the store starts at the defaults
    targetProgram_.store(program());   // the defaults are the first program's settings
}

TearwashClap::~TearwashClap() = default;

void TearwashClap::destroy() {
#ifdef TEARWASH_WITH_EDITOR
    destroyGuiWindow();
#endif
    delete this;
}

#ifdef TEARWASH_WITH_EDITOR
void TearwashClap::createGuiWindow() {
    if (!gui_) gui_ = std::make_unique<TwEditor>(this);
}

void TearwashClap::destroyGuiWindow() { gui_.reset(); }
#endif

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
    // The preset follows the program (from the panel or the host): it becomes the program as stored.
    std::lock_guard<std::mutex> lock(presetMutex_);
    presetName_ = std::string("PG ") + programName(pg);
    for (uint32_t i = 0; i < TW_PARAM_COUNT; ++i) presetValues_[i] = paramValue(i);
}

void TearwashClap::selectProgram(int pg) {
    pg = clampProgram(pg);
    double old[TW_PARAM_COUNT];
    for (uint32_t i = 0; i < TW_PARAM_COUNT; ++i) old[i] = paramValue(i);
    loadProgramSettings(pg);
    for (uint32_t i = 0; i < TW_PARAM_COUNT; ++i) {
        if (paramValue(i) != old[i]) queueChange(i);
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
    requestCallback();
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
    inGain_.prepare(fs_);
    mix_.prepare(fs_);
    outGain_.prepare(fs_);
    rev_.setSampleRate(fs_);
    runningProgram_ = program();
    rev_.setProgram(runningProgram_);
    revControls_ = controls();
    rev_.setControls(revControls_);
    rev_.reset();
    askedProgram_ = -1;
    active_ = true;
    inGain_.reset(std::pow(10.0, paramValue(TW_INGAIN) / 20.0));
    mix_.reset(paramValue(TW_MIX));
    outGain_.reset(paramValue(TW_OUTGAIN) <= kOutGainFloorDb ? 0.0 : std::pow(10.0, paramValue(TW_OUTGAIN) / 20.0));
    return true;
}

void TearwashClap::reset() {
    if (active_) rev_.reset();
}

void TearwashClap::markDirty() {
    stateDirty_.store(true);
    requestCallback();
}

void TearwashClap::onMainThread() {
    if (programRequest_.exchange(false)) {
        loadProgramSettings(program());
        rescanValues_.store(true);
    }
    if (rescanValues_.exchange(false)) rescanParams(CLAP_PARAM_RESCAN_VALUES | CLAP_PARAM_RESCAN_TEXT);
    if (stateDirty_.exchange(false)) markStateDirty();
}

// --- Processing ----------------------------------------------------------------------------------

void TearwashClap::handleEvent(const clap_event_header_t* hdr) {
    const auto* ev = spux::clap::asParamValue(hdr);
    if (ev && ev->param_id < TW_PARAM_COUNT) setValue(ev->param_id, ev->value);
}

void TearwashClap::render(const float* inL, const float* inR, float* outL, float* outR, uint32_t n) {
    float* xl = scratch_[0].data();
    float* xr = scratch_[1].data();
    float* w[4] = { scratch_[2].data(), scratch_[3].data(), scratch_[4].data(), scratch_[5].data() };
    const double inTarget = std::pow(10.0, paramValue(TW_INGAIN) / 20.0);
    const double og = paramValue(TW_OUTGAIN);
    const double outTarget = og <= kOutGainFloorDb ? 0.0 : std::pow(10.0, og / 20.0);
    const double mixTarget = paramValue(TW_MIX);
    inGain_.setTarget(inTarget);
    for (uint32_t i = 0; i < n; ++i) {
        const double ig = inGain_.next();
        const float l = inL ? inL[i] : 0.f, r = inR ? inR[i] : l;
        xl[i] = static_cast<float>(l * ig);
        xr[i] = static_cast<float>(r * ig);
    }

    rev_.process(xl, xr, w, static_cast<int>(n));
    const bool rear = paramValue(TW_REAR) >= 0.5;
    float* wl = rear ? w[1] : w[0];
    float* wr = rear ? w[3] : w[2];

    mix_.setTarget(mixTarget);
    outGain_.setTarget(outTarget);
    for (uint32_t i = 0; i < n; ++i) {
        const double mx = mix_.next(), go = outGain_.next();
        const float dl = inL ? inL[i] : 0.f, dr = inR ? inR[i] : dl;
        const double yl = (1.0 - mx) * dl + mx * wl[i];
        const double yr = (1.0 - mx) * dr + mx * wr[i];
        if (outL) outL[i] = static_cast<float>(yl * go);
        if (outR) outR[i] = static_cast<float>(yr * go);
    }
}

clap_process_status TearwashClap::process(const clap_process_t* process) {
    spux::DenormalGuard ftz;

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
    spux::clap::drainEdits(edits_, twParams(), process->out_events);
    return CLAP_PROCESS_CONTINUE;
}

// CLAP preset-load: factory presets by name (load key), user presets by file.
static bool presetLoadFromLocation(const clap_plugin_t* p, uint32_t kind, const char* location, const char* key) {
    auto* self = static_cast<TearwashClap*>(p->plugin_data);
    bool ok = false;
    if (kind == CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN && key) ok = self->loadPreset(self->presetLibrary().indexOf(key));
    else if (kind == CLAP_PRESET_DISCOVERY_LOCATION_FILE && location) ok = self->loadPresetFile(location);
    const clap_host_t* h = self->host();
    if (h && h->get_extension) {
        const auto* hp = static_cast<const clap_host_preset_load_t*>(h->get_extension(h, CLAP_EXT_PRESET_LOAD));
        if (hp && ok && hp->loaded) hp->loaded(h, kind, location, key);
        if (hp && !ok && hp->on_error) hp->on_error(h, kind, location, key, 0, "preset not found");
    }
    return ok;
}
static const clap_plugin_preset_load_t g_presetLoad = { presetLoadFromLocation };

// Preset-load is Tearwash's own; params, state, audio ports and the GUI come from spux_clap, which
// offers them because this class implements their methods.
const void* TearwashClap::getExtension(const char* id) {
    if (std::strcmp(id, CLAP_EXT_PRESET_LOAD) == 0 || std::strcmp(id, CLAP_EXT_PRESET_LOAD_COMPAT) == 0) return &g_presetLoad;
    return defaultExtension(id);
}

bool TearwashClap::audioPortsGet(uint32_t index, bool isInput, clap_audio_port_info_t* info) const {
    if (index != 0) return false;
    info->id = 0;
    std::snprintf(info->name, sizeof(info->name), "%s", isInput ? "Input" : "Output");
    info->channel_count = 2;
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->port_type = CLAP_PORT_STEREO;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}

// --- Params --------------------------------------------------------------------------------------

bool TearwashClap::paramsInfo(uint32_t index, clap_param_info_t* info) const {
    if (index >= TW_PARAM_COUNT || !info) return false;
    spux::clap::fillParamInfo(twParams()[index], *info);
    return true;
}

bool TearwashClap::paramsValue(clap_id id, double* out) const {
    if (id >= TW_PARAM_COUNT || !out) return false;
    *out = params_.host(id);
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
    spux::DenormalGuard ftz;
    if (in && in->size && in->get) {
        const uint32_t n = in->size(in);
        for (uint32_t i = 0; i < n; ++i) handleEvent(in->get(in, i));
    }
    // Not processing: changes reach the engine at the next process call.
    if (active_) checkProgramChange();
    spux::clap::drainEdits(edits_, twParams(), out);
}

// Main or GUI thread. A change from a program or preset load: the whole gesture or nothing, so the host
// never sees an unbalanced one; it leaves the queue's gesture reserve to the GUI's drags.
void TearwashClap::queueChange(clap_id id) {
    std::lock_guard<std::mutex> lock(queueMutex_);
    if (edits_.freeSlots() < 3 + spux::params::EditQueue::kGestureReserve) return;
    edits_.tryPush({ spux::params::ParamEdit::Kind::Begin, id, 0.0 });
    edits_.tryPush({ spux::params::ParamEdit::Kind::Value, id, params_.host(id) });
    edits_.tryPush({ spux::params::ParamEdit::Kind::End, id, 0.0 });
}

// GUI drags: a begin keeps a slot for its end, values leave the gesture reserve free (spux EditQueue);
// a begin that did not fit drops its end too. A dropped value is harmless: the store has it.
void TearwashClap::onBeginEditFromGui(clap_id id) {
    if (id >= TW_PARAM_COUNT) return;
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        gestureOpen_[id] = edits_.pushBegin(id);
    }
    requestFlush();
}

void TearwashClap::onParamValueFromGui(clap_id id, double value) {
    if (id >= TW_PARAM_COUNT) return;
    if (id == TW_PROGRAM) {
        selectProgram(static_cast<int>(twClamp(id, value)));
        return;
    }
    setValue(id, value);
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        edits_.pushValue(id, params_.host(id));
    }
    requestFlush();
    markDirty();
}

void TearwashClap::onEndEditFromGui(clap_id id) {
    if (id >= TW_PARAM_COUNT) return;
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        if (!gestureOpen_[id]) return;   // its begin was dropped
        gestureOpen_[id] = false;
        edits_.pushEnd(id);              // fits: values leave the gesture reserve free
    }
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
        if (paramValue(i) != old[i]) queueChange(i);
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

std::vector<uint8_t> TearwashClap::stateBytes() const {
    TwValues v;
    for (uint32_t i = 0; i < TW_PARAM_COUNT; ++i) v[i] = paramValue(i);
    return serializeState(v, presetName());
}

bool TearwashClap::loadState(const std::vector<uint8_t>& bytes) {
    TwState st;
    if (!parseState(bytes, st)) return false;
    for (uint32_t i = 0; i < TW_PARAM_COUNT; ++i) setValue(i, st.values[i]);
    targetProgram_.store(program());
    // The preset the state came from: its stored values (so the modified marker is right), or the
    // state itself when the preset is not in the library.
    std::lock_guard<std::mutex> lock(presetMutex_);
    presetName_ = st.preset.empty() ? std::string("IN Init") : st.preset;
    const int idx = presets_.indexOf(presetName_);
    for (uint32_t i = 0; i < TW_PARAM_COUNT; ++i)
        presetValues_[i] = idx >= 0 ? presets_.presets()[static_cast<size_t>(idx)].values[i] : paramValue(i);
    return true;
}

bool TearwashClap::stateSave(const clap_ostream_t* stream) { return spux::clap::writeAll(stream, stateBytes()); }

bool TearwashClap::stateLoad(const clap_istream_t* stream) {
    std::vector<uint8_t> data;
    return spux::clap::readAll(stream, data, 1u << 20) && loadState(data);
}

// --- GUI -----------------------------------------------------------------------------------------

// The editor writes a host value (every parameter's host value is its plain value) and queues the edit for the host.
void TearwashClap::setHostValueFromGui(clap_id id, double host) {
    if (id >= TW_PARAM_COUNT) return;
    params_.setHost(id, host);
    markDirty();
}

bool TearwashClap::pushEditFromGui(const spux::params::ParamEdit& e, size_t reserve) {
    std::lock_guard<std::mutex> lock(queueMutex_);
    return edits_.tryPush(e, reserve);
}

#ifdef TEARWASH_WITH_EDITOR
bool TearwashClap::guiCreate(const char*, bool) {
    createGuiWindow();
    return true;
}

bool TearwashClap::guiSetParent(const clap_window_t* window) {
    if (!window->api) return false;
    if (!gui_) createGuiWindow();
    return gui_->setParent(window);
}

bool TearwashClap::guiGetSize(uint32_t* width, uint32_t* height) const {
    *width = TwEditor::kWidth;
    *height = TwEditor::kHeight;
    return true;
}

bool TearwashClap::guiSetSize(uint32_t width, uint32_t height) { return gui_ ? gui_->setSize(width, height) : true; }
bool TearwashClap::guiShow() { return gui_ ? gui_->show() : false; }
bool TearwashClap::guiHide() { return gui_ ? gui_->hide() : false; }
#endif

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

const clap_plugin_descriptor_t& TearwashClap::descriptor() { return g_descriptor; }



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
    if (std::strcmp(id, CLAP_PLUGIN_FACTORY_ID) == 0) return &spux::clap::SingleFactory<TearwashClap>::factory;
    if (std::strcmp(id, CLAP_PRESET_DISCOVERY_FACTORY_ID) == 0 || std::strcmp(id, CLAP_PRESET_DISCOVERY_FACTORY_ID_COMPAT) == 0)
        return &g_discovery;
    return nullptr;
}

} // namespace tearwash

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry = {
    CLAP_VERSION, tearwash::entryInit, tearwash::entryDeinit, tearwash::entryGetFactory
};
