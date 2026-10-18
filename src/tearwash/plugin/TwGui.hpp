#ifndef TEARWASH_TWGUI_HPP
#define TEARWASH_TWGUI_HPP

// Tearwash panel: drawn procedurally at 2× and box-filtered (Tannhäuser GUI rules), top to bottom:
// the preset bar, the program buttons (two rows), the decay faders, the space faders, the motion,
// shimmer and level knobs, and a one-line description of the control under the pointer. Each
// control shows its value above it. X11 / Win32 glue as in Tannhäuser's GuiWindow.

#include <clap/clap.h>
#include <clap/ext/gui.h>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace tannhauser { class Graphics; }

namespace tearwash {

class TearwashClap;

extern const clap_plugin_gui_t g_tearwashGuiExtension;

class TwGui {
public:
    static constexpr uint32_t kWidth = 640, kHeight = 628;

    enum class Kind { Fader, Knob, Program, PresetPrev, PresetName, PresetNext, PresetSave };
    struct Ctl {
        Kind kind;
        int param;      // -1 for program and preset buttons
        float x, y, w, h;
        const char* label;
        int aux;        // program index
    };

    explicit TwGui(TearwashClap* plugin);
    ~TwGui();

    bool setParent(const clap_window_t* window);
    bool setSize(uint32_t width, uint32_t height) { return width == kWidth && height == kHeight; }
    bool show();
    bool hide();
    void destroy();
    bool isVisible() const { return visible_; }

    // Polls the plugin and repaints when anything shown changed.
    void renderFrame();
    const std::vector<uint32_t>& getPixelBuffer() const { return pixels_; }
    uint32_t getWidth() const { return kWidth; }
    uint32_t getHeight() const { return kHeight; }

    // Input (logical pixels). Exposed for tests.
    void handleMouseDown(int x, int y, bool shift);
    void handleMouseDrag(int x, int y, bool shift);
    void handleMouseUp();
    void handleMouseMove(int x, int y);
    void handleWheel(int x, int y, int delta);

    const std::vector<Ctl>& controls() const { return ctls_; }
    int controlAt(int x, int y) const;
    int findControl(Kind kind, int paramOrAux) const;
    std::string tooltipText() const;   // the description of the control under the pointer, or ""
    int redraws() const { return redraws_; }
    // Preset menu (an overlay over the panel): open state, and for tests the item under a point.
    bool presetMenuOpen() const { return menuOpen_; }
    // Saves the current settings as a user preset without the file dialog (tests, fallback).
    bool savePresetAs(const std::string& path);

private:
    TearwashClap* plugin_;
    mutable std::recursive_mutex mutex_;
    std::vector<Ctl> ctls_;
    std::vector<uint32_t> static_, compose_, pixels_;
    bool staticValid_ = false;
    std::string drawnKey_;
    int redraws_ = 0;

    int hover_ = -1, active_ = -1;
    int dragX_ = 0, dragY_ = 0;
    double dragValue_ = 0.0;
    bool dragShift_ = false;

    void layout();
    std::string stateKey() const;
    // Preset menu: categories on the left, the selected category's presets in three columns.
    bool menuOpen_ = false;
    int menuCat_ = 0, menuScroll_ = 0, menuHover_ = -1;   // hover: −2 − category, or preset index
    std::vector<std::string> menuCats_;
    std::vector<int> menuItems_;                          // library indices of the shown category
    void openMenu();
    void fillMenuItems();
    int menuHit(int x, int y) const;                      // as menuHover_; −1: outside items
    void drawMenu(tannhauser::Graphics& g);
    void startSave();
    void pollSave();
    void* saveDialog_ = nullptr;                          // Linux: the running file dialog process
    void drawStatic(tannhauser::Graphics& g);
    void drawControls(tannhauser::Graphics& g);
    double norm(int param) const;
    void setNorm(int param, double n);
    bool enabled(const Ctl& c) const;

    std::atomic<bool> visible_{ true };
    std::atomic<bool> running_{ false };
    std::thread eventThread_;

#if defined(__linux__) && !defined(__APPLE__)
    void* display11_ = nullptr;
    unsigned long window11_ = 0;
    unsigned long parent11_ = 0;
    bool created11_ = false;
    void initX11();
    void drawX11();
    void eventLoopX11();
#elif defined(_WIN32)
    void* hwnd_ = nullptr;
    void* parentHwnd_ = nullptr;
public:
    void initWin32();
    void drawWin32();
private:
#endif
};

} // namespace tearwash

#endif
