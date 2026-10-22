#ifndef TEARWASH_TWEDITOR_HPP
#define TEARWASH_TWEDITOR_HPP

// Tearwash editor: the panel as page functions on spux's immediate-mode Gui with the Classic style
// (spux_ui_core, spux_ui_classic), in spux's native child window. TwEditor is the editor host (the parameters and the
// edits to the host, value text and tooltips in the engine's units, the preset library as a PresetHost) and the root
// view (the printed panel as the background, the widgets every frame). The panel is in TwLayout.hpp.

#include <clap/clap.h>

#include <atomic>
#include <memory>
#include <string>

#include "TwLayout.hpp"
#include "spux/ui/Editor.hpp"
#include "spux/ui/PresetBar.hpp"
#include "spux/ui/classic/ClassicStyle.hpp"

namespace tearwash {

class TearwashClap;

// The Classic look of the panel: the slate look with Tearwash's caps, faders and rows (twClassicConfig).
spux::ui::classic::ClassicConfig twClassicConfig();

class TwEditor final : private spux::ui::EditorHost,
                       private spux::ui::RootView<spux::ui::Gui<spux::ui::classic::ClassicStyle>> {
public:
    using Style = spux::ui::classic::ClassicStyle;
    using GuiType = spux::ui::Gui<Style>;

    static constexpr uint32_t kWidth = kPanelWidth, kHeight = kPanelHeight;

    explicit TwEditor(TearwashClap* plugin);
    ~TwEditor() override;

    // The window (CLAP gui extension). The panel has one size.
    bool setParent(const clap_window_t* window);
    bool setSize(uint32_t width, uint32_t height) const { return width == kWidth && height == kHeight; }
    bool show();
    bool hide();
    void destroy();
    [[nodiscard]] bool isVisible() const { return visible_; }

    // Tests: the editor shell (frames, pixels, input in logical pixels) and the Gui.
    spux::ui::Editor<Style>& editor() { return *editor_; }
    GuiType& gui() { return editor_->gui(); }
    // Draws a frame if something changed (true), or always with force.
    bool drawFrame(bool force = false) { return editor_->frame(force); }
    // The description of the control under the pointer in the last frame, or "".
    [[nodiscard]] std::string description() const { return description_; }
    // Saves the current settings as a user preset without the file dialog (tests, fallback).
    bool savePresetAs(const std::string& path);
    // The text of a control's description (also the tooltip's hint).
    static const char* describeParam(uint32_t param);

private:
    // --- EditorHost ---
    [[nodiscard]] const spux::params::ParamTable& paramTable() const override;
    [[nodiscard]] double paramHost(std::size_t index) const override;
    void setParamHost(std::size_t index, double host) override;
    [[nodiscard]] std::uint32_t paramVersion() const override;
    bool pushEdit(const spux::params::ParamEdit& e, std::size_t reserve) override;
    void requestFlush() override;
    void formatParam(std::size_t index, double plain, char* buf, std::size_t size) override;
    bool parseParam(std::size_t index, const char* text, double& plain) override;
    [[nodiscard]] const char* paramHint(std::size_t index) override;
    [[nodiscard]] spux::ui::PresetHost* presets() override;

    // --- RootView ---
    [[nodiscard]] std::uint64_t contentVersion() override;
    void tick(double now) override;
    void background(GuiType& g) override;
    void frame(GuiType& g) override;

    void startSave();
    void pollSave();

    TearwashClap* plugin_;
    Style style_;
    std::unique_ptr<spux::ui::Editor<Style>> editor_;
    std::unique_ptr<spux::ui::PresetHost> presets_;
    std::string description_;
    std::atomic<bool> visible_{ true };
    [[maybe_unused]] void* saveDialog_ = nullptr;   // Linux: the running file dialog process
};

} // namespace tearwash

#endif
