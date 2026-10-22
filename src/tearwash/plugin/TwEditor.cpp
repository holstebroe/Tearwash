#include "TwEditor.hpp"

#include "TearwashClap.hpp"
#include "spux/ui/classic/FileDialog.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>

namespace tearwash {

using namespace spux::ui;
using namespace spux::ui::classic;

namespace {

constexpr Argb kSilk = 0xF2E6E6E0;      // printed names
constexpr Argb kSilkDim = 0xB09A9C98;   // group captions, scales, the idle description

// The preset library as the editor's preset host: factory presets by family, the user's files after them.
class TwPresetHost final : public PresetHost {
public:
    explicit TwPresetHost(TearwashClap* plugin) : plugin_(plugin) {}
    std::string currentName() override { return plugin_->presetName(); }
    bool modified() override { return plugin_->presetModified(); }
    std::vector<Entry> factory() override { return entries(false); }
    std::vector<Entry> user() override {
        plugin_->rescanPresets();   // a preset saved meanwhile, or dropped into the folder
        return entries(true);
    }
    bool load(const Entry& e) override { return plugin_->loadPreset(static_cast<int>(e.index)); }

private:
    std::vector<Entry> entries(bool user) const {
        std::vector<Entry> out;
        const auto& all = plugin_->presetLibrary().presets();
        for (std::size_t i = 0; i < all.size(); ++i) {
            if (all[i].user != user) continue;
            out.push_back({ all[i].name, TwPresetLibrary::categoryName(all[i].category), all[i].path, i, !all[i].user });
        }
        return out;
    }
    TearwashClap* plugin_;
};

// What a control does, for the line at the bottom of the panel and the tooltip.
const char* describeProgram() { return "Selects a program: the room type and its starting settings."; }

// Tearwash steps a control by half a percent a notch; the mix by one, the output gain by half a dB.
double wheelStep(uint32_t id) {
    const TwParamInfo& p = twParamInfo(id);
    if (p.law == spux::params::ParamLaw::Db) return 0.5 / (p.max - p.min);
    return id == TW_MIX ? 0.01 : 0.005;
}

} // namespace

// The slate look on brushed steel, with the panel's own caps (28 x 15), faders (a 4 wide groove, ticks on both sides) and rows (the value
// above a dial or fader; the names are printed on the panel).
ClassicConfig twClassicConfig() {
    ClassicConfig c = ClassicConfig::slate();
    c.labelCap = 7.2f;
    c.knob.ticks = false;
    c.knob.topRow = 26.0f;
    c.knob.bottomRow = 0.0f;
    c.slider.capW = 28.0f;
    c.slider.capH = 15.0f;
    c.slider.slotW = 4.0f;
    c.slider.slotEnd = 2.0f;
    c.slider.scaleBothSides = true;
    c.button.radius = 4.0f;
    c.surface = PanelSurface::Brushed; // the fine horizontal grain of a brushed steel front panel
    return c;
}

const char* TwEditor::describeParam(uint32_t param) {
    switch (param) {
        case TW_HIGHCUT: return "Filters the input before the reverb: lower values darken what goes in.";
        case TW_CROSSOVER: return "The frequency that splits the tail into a low band and a high band.";
        case TW_LOWDECAY: return "How long the low band rings, from 0.2 s to 60 s.";
        case TW_DECAY: return "How long the high band rings; at the top the tail holds forever.";
        case TW_DAMPING: return "A low-pass inside the loop: each pass darkens the tail. Open turns it off.";
        case TW_SIZE: return "Scales every delay together: a bigger room at higher values.";
        case TW_PREDELAY: return "Silence before the reverb starts, up to 1 s.";
        case TW_DIFFUSION: return "How smeared the echoes are: higher is denser and smoother.";
        case TW_CLARITY: return "How much the tank smears its tail: higher is clearer.";
        case TW_DISTANCE: return "Early reflections (0 %) against the late tail (100 %): higher sounds further away.";
        case TW_MODRATE: return "Speed of the slow drift in the delay lines.";
        case TW_MODDEPTH: return "How far the drift moves the delays. 0 % is no modulation.";
        case TW_SHIMMER: return "Feeds a pitch-shifted copy of the tail back in. 0 % is off.";
        case TW_SHIMMERINT: return "Pitch of the shimmer, in semitones: -12, -7, +5, +7, +12, +19 or +24.";
        case TW_MIX: return "Balance of dry sound and reverb: 0 % dry, 100 % reverb only.";
        case TW_OUTGAIN: return "Level after the reverb.";
        default: return "";
    }
}

TwEditor::TwEditor(TearwashClap* plugin)
    : plugin_(plugin), style_(twClassicConfig()), presets_(std::make_unique<TwPresetHost>(plugin)) {
    EditorOptions o;
    o.width = kPanelWidth;
    o.height = kPanelHeight;
    o.keyboard = false;   // the panel has no key input; the host keeps its shortcuts
    EditorHost& host = *this;                                   // the bases are private: convert here
    RootView<GuiType>& root = *this;
    editor_.reset(new Editor<Style>(host, style_, root, o, classicRenderer(2)));
}

TwEditor::~TwEditor() {
    destroy();
#if defined(__linux__) && !defined(__APPLE__)
    delete static_cast<FileDialogProcess*>(saveDialog_);
#endif
}

// --- Window --------------------------------------------------------------------------------------

bool TwEditor::setParent(const clap_window_t* window) {
    if (!window || !window->api) return false;
    spux::platform::ParentWindow parent;
#if defined(__linux__) && !defined(__APPLE__)
    if (std::strcmp(window->api, CLAP_WINDOW_API_X11) != 0) return false;
    parent = { spux::platform::WindowApi::X11, static_cast<std::uintptr_t>(window->x11) };
#elif defined(_WIN32)
    if (std::strcmp(window->api, CLAP_WINDOW_API_WIN32) != 0) return false;
    parent = { spux::platform::WindowApi::Win32, reinterpret_cast<std::uintptr_t>(window->win32) };
#else
    return false;
#endif
    return editor_->setParent(parent);
}

bool TwEditor::show() {
    visible_ = true;
    editor_->show();
    return true;
}

bool TwEditor::hide() {
    visible_ = false;
    editor_->hide();
    return true;
}

void TwEditor::destroy() {
    if (editor_) editor_->destroy();
}

// --- EditorHost ----------------------------------------------------------------------------------

const spux::params::ParamTable& TwEditor::paramTable() const { return twParams(); }
double TwEditor::paramHost(std::size_t index) const { return plugin_->paramHostValue(static_cast<clap_id>(index)); }
void TwEditor::setParamHost(std::size_t index, double host) { plugin_->setHostValueFromGui(static_cast<clap_id>(index), host); }
std::uint32_t TwEditor::paramVersion() const { return plugin_->paramVersion(); }
bool TwEditor::pushEdit(const spux::params::ParamEdit& e, std::size_t reserve) { return plugin_->pushEditFromGui(e, reserve); }
void TwEditor::requestFlush() { plugin_->requestFlush(); }

void TwEditor::formatParam(std::size_t index, double plain, char* buf, std::size_t size) {
    twValueText(static_cast<uint32_t>(index), plain, buf, static_cast<uint32_t>(size));
}

bool TwEditor::parseParam(std::size_t index, const char* text, double& plain) {
    return twTextToValue(static_cast<uint32_t>(index), text, &plain);
}

const char* TwEditor::paramHint(std::size_t index) {
    const char* d = describeParam(static_cast<uint32_t>(index));
    return *d ? d : nullptr;
}

PresetHost* TwEditor::presets() { return presets_.get(); }

// --- Root view -----------------------------------------------------------------------------------

std::uint64_t TwEditor::contentVersion() {
    std::uint64_t v = std::hash<std::string>{}(plugin_->presetName());
    v = v * 31 + (plugin_->presetModified() ? 1 : 0);
    v = v * 31 + static_cast<std::uint64_t>(plugin_->presetIndex() + 1);
    return v;
}

void TwEditor::tick(double) { pollSave(); }

// The printed panel: the surface, the title, the group captions with their rules, and the names under the
// faders and knobs. Drawn once into the background layer.
void TwEditor::background(GuiType& g) {
    ShadedRenderer* sr = asShaded(g.r());
    if (!sr) return;
    const float W = static_cast<float>(kPanelWidth), H = static_cast<float>(kPanelHeight);
    g.panel({ 0, 0, kPanelWidth, kPanelHeight }, "");
    sr->fillRect({ 0, 0, kPanelWidth, 2 }, 0xFF2C2D30);
    sr->fillRect({ 0, kPanelHeight - 2, kPanelWidth, 2 }, 0xFF08090A);
    sr->ink(ShadedRenderer::displayFont(), "TEARWASH", 20.f, 11.f, 20.f, 0xFFF0EEE6);
    sr->ink(ShadedRenderer::labelFont(), "ALGORITHMIC REVERB", 200.f, 19.f, 6.f, kSilkDim);
    sr->ink(ShadedRenderer::labelFont(), "PROGRAM", 20.f, 47.f, 5.5f, kSilkDim);

    Graphics gr = sr->graphics();
    auto caption = [&](const char* text, float cx, float capTop) {
        sr->inkCentered(ShadedRenderer::labelFont(), text, cx, capTop, 6.f, kSilkDim);
    };
    caption("DECAY", 320.f, 148.f);
    drawLineAA(gr, 20, 157, 620, 157, 0.8f, kSilkDim);
    caption("SPACE", 320.f, 314.f);
    drawLineAA(gr, 20, 323, 620, 323, 0.8f, kSilkDim);
    static const char* kGroups[3] = { "MOTION", "SHIMMER", "LEVEL" };
    for (int i = 0; i < 3; ++i) {
        caption(kGroups[i], 20.f + static_cast<float>(i) * 205.f + 95.f, 480.f);
        drawLineAA(gr, 20.f + static_cast<float>(i) * 205.f, 489.f, 210.f + static_cast<float>(i) * 205.f, 489.f, 0.8f, kSilkDim);
    }
    drawLineAA(gr, 20, 598, 620, 598, 0.8f, kSilkDim);
    (void)W;
    (void)H;

    auto name = [&](const TwControl& c, float below) {
        sr->inkCentered(ShadedRenderer::labelFont(), c.label, static_cast<float>(c.box.x) + static_cast<float>(c.box.w) * 0.5f,
                        static_cast<float>(c.box.y + c.box.h) + below, 7.2f, kSilk);
    };
    for (const TwControl& c : decayFaders()) name(c, 4.f);
    for (const TwControl& c : spaceFaders()) name(c, 4.f);
    for (const TwControl& c : knobs()) name(c, 3.f);
}

void TwEditor::frame(GuiType& g) {
    description_.clear();
    const int program = plugin_->program();

    // Programs.
    for (int i = 0; i < programCount(); ++i) {
        char id[16];
        std::snprintf(id, sizeof id, "program%d", i);
        const Rect r = programButton(i);
        if (g.button(id, r, programName(i), { .on = i == program })) plugin_->selectProgram(i);
        if (g.hovered(r)) description_ = describeProgram();
    }

    // The decay and space faders: a press jumps the cap to the pointer, a drag moves it from there.
    auto fader = [&](const TwControl& c) {
        g.slider(c.param, faderBounds(c), { .label = c.label, .vertical = true, .jump = true, .dragPixels = 84.0f,
                                            .wheelStep = wheelStep(c.param) });
    };
    for (const TwControl& c : decayFaders()) fader(c);
    for (const TwControl& c : spaceFaders()) fader(c);

    // The knobs.
    for (const TwControl& c : knobs())
        g.knob(c.param, knobBounds(c), { .label = c.label, .valueEntry = false, .dragPixels = 180.0f, .wheelStep = wheelStep(c.param) });

    // The preset bar: arrows, the name opening the menu by family, SAVE.
    PresetBarOptions bar;
    bar.byCategory = true;
    const PresetBarResult res = presetBar(g, "presets", kPresetBar, bar);
    if (res.save) startSave();
    {
        const int arrow = bar.arrowWidth, saveW = bar.saveWidth + bar.gap;
        const Rect prev{ kPresetBar.x, kPresetBar.y, arrow, kPresetBar.h };
        const Rect next{ kPresetBar.right() - saveW - arrow, kPresetBar.y, arrow, kPresetBar.h };
        const Rect save{ kPresetBar.right() - bar.saveWidth, kPresetBar.y, bar.saveWidth, kPresetBar.h };
        const Rect name{ prev.right() + bar.gap, kPresetBar.y, next.x - prev.right() - 2 * bar.gap, kPresetBar.h };
        if (g.hovered(prev)) description_ = "Steps back to the previous preset.";
        else if (g.hovered(next)) description_ = "Steps on to the next preset.";
        else if (g.hovered(name)) description_ = "Opens the preset menu.";
        else if (g.hovered(save)) description_ = "Saves the current settings as a user preset.";
    }

    // The description of the control under the pointer, on the line at the bottom.
    if (description_.empty() && g.hoveredParam() != kNoParamId) description_ = describeParam(g.hoveredParam());
    const bool idle = description_.empty();
    const char* text = idle ? "Hover over a control to see what it does." : description_.c_str();
    const Rect line{ 16, 600, 608, 20 };
    if (g.view(g.id("description"), line, std::hash<std::string>{}(std::string(text)))) {
        if (ShadedRenderer* sr = asShaded(g.r())) {
            float cap = 7.f;
            while (cap > 4.6f && ShadedRenderer::inkWidth(ShadedRenderer::labelFont(), text, cap) > 600.f) cap -= 0.2f;
            sr->pushClip(line);
            sr->ink(ShadedRenderer::labelFont(), text, 20.f, 606.f, cap, idle ? kSilkDim : kSilk);
            sr->popClip();
        }
    }
}

// --- Saving a preset -----------------------------------------------------------------------------

bool TwEditor::savePresetAs(const std::string& path) {
    std::string err;
    return plugin_->savePreset(path, err);
}

// Save: the system file dialog in the user folder (Linux: zenity / kdialog, polled; Windows: modal); without a
// dialog program, the next free "User NNN" name in the folder.
void TwEditor::startSave() {
    const std::string dir = TwPresetLibrary::userPresetDirectory();
    if (dir.empty()) return;
    std::string base = plugin_->presetName();
    if (base.size() > 3 && base[2] == ' ') base = base.substr(3);
    FileDialogOptions opt;
    opt.save = true;
    opt.title = "Save Tearwash preset";
    opt.filterName = "Tearwash presets";
    opt.extension = "twpreset";
    opt.startPath = dir + "/" + base + ".twpreset";
#if defined(__linux__) && !defined(__APPLE__)
    auto* d = static_cast<FileDialogProcess*>(saveDialog_);
    if (!d) { d = new FileDialogProcess(); saveDialog_ = d; }
    if (d->running()) return;
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (d->start(opt)) return;
#elif defined(_WIN32)
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const std::filesystem::path p = runFileDialog(nullptr, opt);
    if (!p.empty()) savePresetAs(p.string());
    return;
#endif
    for (int n = 1; n < 1000; ++n) {
        char name[32];
        std::snprintf(name, sizeof name, "/User %03d.twpreset", n);
        std::error_code e2;
        if (!std::filesystem::exists(dir + name, e2)) { savePresetAs(dir + name); return; }
    }
}

void TwEditor::pollSave() {
#if defined(__linux__) && !defined(__APPLE__)
    auto* d = static_cast<FileDialogProcess*>(saveDialog_);
    if (!d || !d->running()) return;
    std::string path;
    bool save = false;
    if (d->poll(path, save) && save && !path.empty()) savePresetAs(path);
#endif
}

} // namespace tearwash
