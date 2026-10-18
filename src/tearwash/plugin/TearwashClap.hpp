#ifndef TEARWASH_CLAP_HPP
#define TEARWASH_CLAP_HPP

// Tearwash CLAP plugin (docs/01_ARCHITECTURE.md): stereo audio effect around
// tearwash::Reverb. Parameters from TwParams, text state, host gestures for GUI edits.
//
// Threads: the engine allocates its delay lines in activate() for every program and setting, so
// program and control changes are applied on the audio thread without allocating. A host program
// change also asks the main thread to load the program's stored settings into the parameters.

#include <clap/clap.h>
#include "TwParams.hpp"
#include "TwPresets.hpp"
#include "tearwash/engine/Reverb.hpp"

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace tearwash {

class TwGui;

class TearwashClap {
public:
    explicit TearwashClap(const clap_host_t* host);
    ~TearwashClap();

    const clap_plugin_t* getClapPlugin() const { return &clapPlugin_; }

    bool init() { return true; }
    void destroy();
    bool activate(double sampleRate, uint32_t minFrames, uint32_t maxFrames);
    void deactivate() { active_ = false; }
    bool startProcessing() { return true; }
    void stopProcessing() {}
    void reset();
    clap_process_status process(const clap_process_t* process);
    const void* getExtension(const char* id);
    void onMainThread();

    // Params.
    uint32_t paramsCount() const { return TW_PARAM_COUNT; }
    bool paramsInfo(uint32_t index, clap_param_info_t* info) const;
    bool paramsValue(clap_id id, double* out) const;
    double paramValue(clap_id id) const { return id < TW_PARAM_COUNT ? values_[id].load(std::memory_order_relaxed) : 0.0; }
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

    // State.
    bool stateSave(const clap_ostream_t* stream);
    bool stateLoad(const clap_istream_t* stream);
    std::string stateText() const;
    bool loadStateText(const std::string& text);

    const clap_host_t* host() const { return host_; }
    TwGui* getGuiWindow() { return gui_.get(); }
    void createGuiWindow();
    void destroyGuiWindow();

private:
    const clap_host_t* host_ = nullptr;
    clap_plugin_t clapPlugin_{};
    std::unique_ptr<TwGui> gui_;
    std::atomic<double> values_[TW_PARAM_COUNT];
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
    double inGain_ = 1.0, outGain_ = 1.0, mix_ = 0.35;
    double gainCoef_ = 0.001;
    void render(const float* inL, const float* inR, float* outL, float* outR, uint32_t n);

    std::atomic<bool> stateDirty_{ false };
    std::atomic<bool> rescanValues_{ false };
    void markDirty();

    // Outgoing param events (GUI thread → host).
    struct OutEvent { uint16_t type; clap_id paramId; double value; uint32_t flags; };
    static constexpr size_t kOutCapacity = 512;
    std::mutex outMutex_;
    std::array<OutEvent, kOutCapacity> outQueue_{};
    size_t outCount_ = 0;
    std::array<OutEvent, kOutCapacity> drain_{};
    void queueOut(const OutEvent& ev);
    void pushOut(const clap_output_events_t* out);
    void requestFlush();

    void setValue(clap_id id, double v) { values_[id].store(twClamp(id, v), std::memory_order_relaxed); }
    void handleEvent(const clap_event_header_t* hdr);
    void checkProgramChange();
};

} // namespace tearwash

#endif
