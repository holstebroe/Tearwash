#ifndef TEARWASH_CLAP_HPP
#define TEARWASH_CLAP_HPP

// Tearwash CLAP plugin (docs/01_ARCHITECTURE.md): stereo audio effect around
// tearwash::Reverb, on spux's CLAP glue (spux_clap: the extensions below are offered because this class
// has their methods). Parameters from TwParams in a spux ParamStore, state in a spux chunk container
// (TwState.hpp; the text state of earlier versions still loads), host gestures for GUI edits through a
// spux EditQueue.
//
// Threads: the engine allocates its delay lines in activate() for every program and setting, so
// program and control changes are applied on the audio thread without allocating. A host program
// change also asks the main thread to load the program's stored settings into the parameters.

#include <clap/clap.h>
#include "TwParams.hpp"
#include "TwPresets.hpp"
#include "tearwash/engine/Reverb.hpp"

#include "spux/clap/ClapPlugin.hpp"
#include "spux/core/Smoothing.hpp"
#include "spux/params/EditQueue.hpp"
#include "spux/params/ParamStore.hpp"

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace tearwash {

class TwEditor;

class TearwashClap : public spux::clap::Plugin<TearwashClap> {
public:
    explicit TearwashClap(const clap_host_t* host);
    ~TearwashClap();

    static const clap_plugin_descriptor_t& descriptor();
    const clap_plugin_t* getClapPlugin() const { return clapPlugin(); }

    void destroy();
    bool activate(double sampleRate, uint32_t minFrames, uint32_t maxFrames);
    void deactivate() { active_ = false; }
    void reset();
    clap_process_status process(const clap_process_t* process);
    const void* getExtension(const char* id);   // preset-load, then spux's defaults
    void onMainThread();

    // Audio ports: one stereo input, one stereo output.
    uint32_t audioPortsCount(bool) const { return 1; }
    bool audioPortsGet(uint32_t index, bool isInput, clap_audio_port_info_t* info) const;

    // Params.
    uint32_t paramsCount() const { return TW_PARAM_COUNT; }
    bool paramsInfo(uint32_t index, clap_param_info_t* info) const;
    bool paramsValue(clap_id id, double* out) const;
    double paramValue(clap_id id) const { return id < TW_PARAM_COUNT ? params_.plain(id) : 0.0; }
    bool paramsValueToText(clap_id id, double value, char* buf, uint32_t cap) const;
    bool paramsTextToValue(clap_id id, const char* text, double* out) const;
    void paramsFlush(const clap_input_events_t* in, const clap_output_events_t* out);

    // GUI edits (gestures to the host).
    void onBeginEditFromGui(clap_id id);
    void onParamValueFromGui(clap_id id, double value);
    void onEndEditFromGui(clap_id id);
    // Selects a program from the GUI (main thread): loads its stored settings and tells the host
    // about every changed parameter.
    void selectProgram(int program);

    int program() const { return clampProgram(static_cast<int>(paramValue(TW_PROGRAM))); }

    // Presets (main or GUI thread, like selectProgram). Loading sets every parameter and tells the
    // host about each change.
    const TwPresetLibrary& presetLibrary() const { return presets_; }
    void rescanPresets();
    bool loadPreset(int index);
    bool loadPresetValues(const TwValues& values, const std::string& name);
    bool loadPresetFile(const std::string& path);
    bool savePreset(const std::string& path, std::string& error);
    void stepPreset(int delta);
    std::string presetName() const;
    bool presetModified() const;
    int presetIndex() const;

    // State: the container of TwState.hpp; loadState also reads the text state of earlier versions.
    bool stateSave(const clap_ostream_t* stream);
    bool stateLoad(const clap_istream_t* stream);
    std::vector<uint8_t> stateBytes() const;
    bool loadState(const std::vector<uint8_t>& bytes);

    // The editor's side (the GUI thread): host values, the version that tells it to draw, the edits to the host.
    double paramHostValue(clap_id id) const { return id < TW_PARAM_COUNT ? params_.host(id) : 0.0; }
    uint32_t paramVersion() const { return params_.version(); }
    void setHostValueFromGui(clap_id id, double host);
    bool pushEditFromGui(const spux::params::ParamEdit& e, size_t reserve);
    void requestFlush() { requestParamsFlush(); }

#ifdef TEARWASH_WITH_EDITOR
    // GUI (the CLAP gui extension through spux_clap; the platform's embedded window API, fixed size).
    bool guiCreate(const char* api, bool floating);
    void guiDestroy() { destroyGuiWindow(); }
    bool guiSetParent(const clap_window_t* window);
    bool guiGetSize(uint32_t* width, uint32_t* height) const;
    bool guiSetSize(uint32_t width, uint32_t height);
    bool guiShow();
    bool guiHide();
    TwEditor* getGuiWindow() { return gui_.get(); }
    void createGuiWindow();
    void destroyGuiWindow();
#endif

private:
#ifdef TEARWASH_WITH_EDITOR
    std::unique_ptr<TwEditor> gui_;
#endif
    spux::params::ParamStore<TW_PARAM_COUNT> params_{ twParams() };
    TwPresetLibrary presets_;
    mutable std::mutex presetMutex_;
    std::string presetName_ = "IN Init";
    TwValues presetValues_{};

    Reverb rev_;                                 // audio thread once active
    Controls revControls_;
    Controls controls() const;                   // the engine's controls from the parameter values
    int runningProgram_ = -1;                    // audio thread
    std::atomic<int> targetProgram_{ -1 };       // program whose settings the parameters hold
    int askedProgram_ = -1;                      // audio thread: last change it asked for
    std::atomic<bool> programRequest_{ false };
    void loadProgramSettings(int program);       // main thread
    void applyControls();

    bool active_ = false;
    double fs_ = 48000.0;

    // Scratch buffers, sized in activate().
    std::vector<float> scratch_[6];
    uint32_t maxFrames_ = 0;
    // Input gain, mix and output gain follow their targets with a 10 ms one-pole.
    using GainSmoother = spux::Smoothed<spux::OnePole<0.010>, double>;
    GainSmoother inGain_, mix_, outGain_;
    void render(const float* inL, const float* inR, float* outL, float* outR, uint32_t n);

    std::atomic<bool> stateDirty_{ false };
    std::atomic<bool> rescanValues_{ false };
    void markDirty();

    // Outgoing param events (GUI and main thread → host): a spux EditQueue, drained by process and
    // flush. The GUI's event thread and the main thread both push, so pushes take queueMutex_; the
    // audio thread only pops and never waits.
    spux::params::EditQueue edits_;
    std::mutex queueMutex_;
    void queueChange(clap_id id);   // begin, value, end, or nothing when they do not fit
    std::array<bool, TW_PARAM_COUNT> gestureOpen_{};   // queueMutex_
    void setValue(clap_id id, double v) { params_.setPlain(id, v); }
    void handleEvent(const clap_event_header_t* hdr);
    void checkProgramChange();
};

} // namespace tearwash

#endif
