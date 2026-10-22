#include "TwGui.hpp"
#include "spux/ui/classic/FileDialog.hpp"
#include <filesystem>
#include "TearwashClap.hpp"
#include "spux/ui/classic/Graphics.hpp"
#include "spux/ui/classic/ClassicDraw.hpp"
#include "spux/ui/classic/LabelFont.hpp"
#include "spux/ui/classic/DisplayFont.hpp"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>


namespace tearwash {

using spux::ui::classic::Graphics;
using namespace spux::ui::classic;

namespace {

constexpr int kScale = 2;
constexpr uint32_t kSilk = 0xF2E6E6E0;
constexpr uint32_t kSilkDim = 0xB09A9C98;
constexpr uint32_t kValue = 0xFFFFD58A;        // the values above the controls: light amber, readable on the charcoal
constexpr float kCap = 6.0f;

void fill(Graphics& g, float x0, float y0, float x1, float y1, uint32_t argb) {
    shadeBox(g, x0, y0, x1, y1, [argb](float, float) { return argb; });
}

void label(Graphics& g, const char* text, float cx, float capTop, uint32_t ink = kSilk, float cap = kCap) {
    drawTextCentered(g, kLabelFont, text, cx, capTop, cap, ink);
}

// A dark rounded button with a light edge; selected: a bright outline.
void button(Graphics& g, float x0, float y0, float x1, float y1, float r, bool selected, bool hot, bool shadow = true) {
    if (shadow) softShadow(g, x0, y0, x1, y1, r, 3.f, 0.5f);
    const Color edge = selected ? Color{ 0.88f, 0.90f, 0.92f } : Color{ 0.30f, 0.315f, 0.33f };
    const float bw = selected ? 1.5f : 1.f;
    shadeRoundRect(g, x0, y0, x1, y1, r, [&](float, float v, float d) {
        Color fill = mix(Color{ 0.215f, 0.225f, 0.24f }, Color{ 0.125f, 0.13f, 0.14f }, 0.5f + 0.5f * v);
        if (selected) fill = scale(fill, 1.1f);
        if (hot) fill = scale(fill, 1.15f);
        return toArgb(mix(fill, edge, smoothstep(-bw - 0.7f, -bw, d)), 1.f);
    });
}

void programLabel(Graphics& g, const TwGui::Ctl& c, bool sel) {
    const char* text = programName(c.aux);
    float cap = 6.4f;
    while (cap > 3.6f && textWidth(kLabelFont, text, cap) > c.w - 8.f) cap -= 0.2f;
    label(g, text, c.x + c.w * 0.5f, c.y + (c.h - cap) * 0.5f, sel ? 0xFFFFFFFF : kSilk, cap);
}

// What a control does, for the line at the bottom of the panel.
const char* describe(const TwGui::Ctl& c) {
    switch (c.kind) {
        case TwGui::Kind::Program: return "Selects a program: the room type and its starting settings.";
        case TwGui::Kind::PresetPrev: return "Steps back to the previous preset.";
        case TwGui::Kind::PresetNext: return "Steps on to the next preset.";
        case TwGui::Kind::PresetName: return "Opens the preset menu.";
        case TwGui::Kind::PresetSave: return "Saves the current settings as a user preset.";
        default: break;
    }
    switch (c.param) {
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

} // namespace

TwGui::TwGui(TearwashClap* plugin) : plugin_(plugin) {
    layout();
    pixels_.assign(static_cast<size_t>(kWidth) * kHeight, 0xFF161718);
}

TwGui::~TwGui() {
    destroy();
#if defined(__linux__) && !defined(__APPLE__)
    delete static_cast<spux::ui::classic::FileDialogProcess*>(saveDialog_);
#endif
}

// --- Layout --------------------------------------------------------------------------------------

void TwGui::layout() {
    ctls_.clear();
    // Programs in two rows of five.
    for (int i = 0; i < programCount(); ++i)
        ctls_.push_back({ Kind::Program, -1, 20.f + (i % 5) * 122.f, 60.f + (i / 5) * 38.f, 112.f, 32.f, "", i });
    // Preset bar: step, name (click: menu), save.
    ctls_.push_back({ Kind::PresetPrev, -1, 352.f, 13.f, 20.f, 22.f, "<", 0 });
    ctls_.push_back({ Kind::PresetName, -1, 376.f, 13.f, 180.f, 22.f, "", 0 });
    ctls_.push_back({ Kind::PresetNext, -1, 560.f, 13.f, 20.f, 22.f, ">", 0 });
    ctls_.push_back({ Kind::PresetSave, -1, 584.f, 13.f, 36.f, 22.f, "SAVE", 0 });
    // Decay, in signal order: the input cut, the band split, the two band decays, the loop damping.
    const struct { int p; const char* l; } decay[5] = {
        { TW_HIGHCUT, "IN CUT" }, { TW_CROSSOVER, "SPLIT FREQ" }, { TW_LOWDECAY, "LOW DECAY" }, { TW_DECAY, "HIGH DECAY" },
        { TW_DAMPING, "DAMP CUT" },
    };
    for (int i = 0; i < 5; ++i) ctls_.push_back({ Kind::Fader, decay[i].p, 60.f + i * 120.f, 184.f, 40.f, 100.f, decay[i].l, 0 });
    // Space, in signal order: the room's size, the predelay, the input diffusion, the tank's clarity, the early/late balance.
    const struct { int p; const char* l; } space[5] = {
        { TW_SIZE, "SIZE" }, { TW_PREDELAY, "PREDELAY" }, { TW_DIFFUSION, "DIFFUSION" }, { TW_CLARITY, "CLARITY" },
        { TW_DISTANCE, "DISTANCE" },
    };
    for (int i = 0; i < 5; ++i) ctls_.push_back({ Kind::Fader, space[i].p, 60.f + i * 120.f, 348.f, 40.f, 100.f, space[i].l, 0 });
    // Three groups of two knobs: motion, shimmer, level.
    const struct { int p; const char* l; } knobs[6] = {
        { TW_MODRATE, "RATE" }, { TW_MODDEPTH, "DEPTH" }, { TW_SHIMMER, "AMOUNT" }, { TW_SHIMMERINT, "INTERVAL" },
        { TW_MIX, "MIX" }, { TW_OUTGAIN, "OUTPUT" },
    };
    for (int i = 0; i < 6; ++i)
        ctls_.push_back({ Kind::Knob, knobs[i].p, 20.f + (i / 2) * 205.f + (i % 2) * 95.f + 20.f, 520.f, 54.f, 54.f, knobs[i].l, 0 });
}

int TwGui::controlAt(int x, int y) const {
    for (size_t i = 0; i < ctls_.size(); ++i) {
        const Ctl& c = ctls_[i];
        if (x >= c.x && x < c.x + c.w && y >= c.y && y < c.y + c.h) return static_cast<int>(i);
    }
    return -1;
}

int TwGui::findControl(Kind kind, int v) const {
    for (size_t i = 0; i < ctls_.size(); ++i) {
        const Ctl& c = ctls_[i];
        if (c.kind == kind && (kind == Kind::Program ? c.aux == v : (c.param == v || c.param < 0)))
            return static_cast<int>(i);
    }
    return -1;
}

bool TwGui::enabled(const Ctl& c) const {
    if (c.kind == Kind::Program) return c.aux < programCount();
    return true;
}

double TwGui::norm(int param) const {
    const TwParamInfo& p = twParamInfo(static_cast<uint32_t>(param));
    return (plugin_->paramValue(static_cast<clap_id>(param)) - p.min) / (p.max - p.min);
}

void TwGui::setNorm(int param, double n) {
    const TwParamInfo& p = twParamInfo(static_cast<uint32_t>(param));
    n = std::max(0.0, std::min(1.0, n));
    const double v = p.min + n * (p.max - p.min);
    plugin_->onParamValueFromGui(static_cast<clap_id>(param), v);
}

std::string TwGui::tooltipText() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    const int i = active_ >= 0 ? active_ : hover_;
    return i >= 0 ? std::string(describe(ctls_[static_cast<size_t>(i)])) : std::string();
}

// --- Drawing -------------------------------------------------------------------------------------

std::string TwGui::stateKey() const {
    std::string k;
    char buf[48];
    for (uint32_t i = 0; i < TW_PARAM_COUNT; ++i) {
        std::snprintf(buf, sizeof buf, "%.6g,", plugin_->paramValue(i));
        k += buf;
    }
    std::snprintf(buf, sizeof buf, "|%d|%d|%d|%d|%d|%d|", hover_, active_, menuOpen_ ? 1 : 0, menuCat_, menuScroll_, menuHover_);
    k += buf;
    k += plugin_->presetName() + (plugin_->presetModified() ? "*" : "");
    return k;
}

void TwGui::drawStatic(Graphics& g) {
    const float W = static_cast<float>(kWidth), H = static_cast<float>(kHeight);
    // Blue-grey panel: a soft diagonal gradient, lighter at the top left, with a faint grain.
    shadeBox(g, 0, 0, W, H, [&](float x, float y) {
        const float grain = 0.008f * (hash2(static_cast<int>(x * 2), static_cast<int>(y * 0.5f), 7) - 0.5f);
        const float t = clamp01(0.45f * x / W + 0.8f * y / H);
        const Color c = mix(Color{ 0.205f, 0.225f, 0.245f }, Color{ 0.07f, 0.077f, 0.086f }, t);
        return toArgb({ c.r + grain, c.g + grain, c.b + grain }, 1.f);
    });
    fill(g, 0, 0, W, 2, 0xFF2C2D30);
    fill(g, 0, H - 2, W, H, 0xFF08090A);
    drawText(g, kDisplayFont, "TEARWASH", 20.f, 11.f, 20.f, 0xFFF0EEE6);
    drawText(g, kLabelFont, "ALGORITHMIC REVERB", 200.f, 19.f, 6.f, kSilkDim);
    // Programs, then decay and space faders, then three groups of knobs.
    drawText(g, kLabelFont, "PROGRAM", 20.f, 47.f, 5.5f, kSilkDim);
    label(g, "DECAY", 320.f, 148.f, kSilkDim, 6.f);
    drawLineAA(g, 20, 157, 620, 157, 0.8f, kSilkDim);
    label(g, "SPACE", 320.f, 314.f, kSilkDim, 6.f);
    drawLineAA(g, 20, 323, 620, 323, 0.8f, kSilkDim);
    static const char* kGroups[3] = { "MOTION", "SHIMMER", "LEVEL" };
    for (int i = 0; i < 3; ++i) {
        label(g, kGroups[i], 20.f + i * 205.f + 95.f, 480.f, kSilkDim, 6.f);
        drawLineAA(g, 20.f + i * 205.f, 489.f, 210.f + i * 205.f, 489.f, 0.8f, kSilkDim);
    }
    drawLineAA(g, 20, 598, 620, 598, 0.8f, kSilkDim);
    // Fader slots and scales.
    for (const Ctl& c : ctls_) {
        if (c.kind != Kind::Fader) continue;
        const float cx = c.x + c.w * 0.5f, y0 = c.y + 8.f, y1 = c.y + c.h - 8.f;
        shadeRoundRect(g, cx - 2.f, y0 - 2, cx + 2.f, y1 + 2, 2.f, [](float, float v, float d) {
            return toArgb(mix({ 0.015f, 0.017f, 0.02f }, { 0.28f, 0.30f, 0.32f }, smoothstep(-1.f, 0.f, d) * (v > 0 ? 0.8f : 0.3f)), 1.f);
        });
        for (int i = 0; i <= 10; ++i) {
            const float y = y0 + (y1 - y0) * i / 10.f, len = (i % 5 == 0) ? 5.f : 3.f;
            drawLineAA(g, c.x + 2.f, y, c.x + 2.f + len, y, 0.8f, 0x70A8B0B4);
            drawLineAA(g, c.x + c.w - 2.f - len, y, c.x + c.w - 2.f, y, 0.8f, 0x70A8B0B4);
        }
    }
    // What does not change with a value or a selection, so that a redraw need not repeat it: the knobs'
    // shadows, the labels, and the idle buttons (the selected or hovered one is drawn over its idle self).
    for (const Ctl& c : ctls_) {
        switch (c.kind) {
            case Kind::Fader: label(g, c.label, c.x + c.w * 0.5f, c.y + c.h + 4.f, kSilk, 7.2f); break;
            case Kind::Knob: {
                const float cx = c.x + c.w * 0.5f, cy = c.y + c.h * 0.5f, R = c.w * 0.5f - 7.f;
                softShadow(g, cx - R, cy - R, cx + R, cy + R, R, 3.f, 0.6f);
                label(g, c.label, cx, c.y + c.h + 3.f, kSilk, 7.2f);
                break;
            }
            case Kind::Program:
                if (c.aux >= programCount()) break;
                button(g, c.x, c.y, c.x + c.w, c.y + c.h, 5.f, false, false);
                programLabel(g, c, false);
                break;
            case Kind::PresetPrev:
            case Kind::PresetNext:
            case Kind::PresetSave:
                button(g, c.x, c.y, c.x + c.w, c.y + c.h, 4.f, false, false);
                label(g, c.label, c.x + c.w * 0.5f, c.y + (c.h - 5.f) * 0.5f, kSilk, 5.f);
                break;
            default: break;
        }
    }
}

void TwGui::drawControls(Graphics& g) {
    const int pg = plugin_->program();
    // The current value, in the display font, above a fader or knob.
    auto valueAbove = [&](const Ctl& c) {
        char buf[48];
        twValueText(static_cast<uint32_t>(c.param), plugin_->paramValue(static_cast<clap_id>(c.param)), buf, sizeof buf);
        drawTextCentered(g, kDisplayFont, buf, c.x + c.w * 0.5f, c.y - 16.f, 8.f, kValue);
    };
    for (size_t i = 0; i < ctls_.size(); ++i) {
        const Ctl& c = ctls_[i];
        const bool hot = static_cast<int>(i) == hover_ || static_cast<int>(i) == active_;
        switch (c.kind) {
            case Kind::Program: {
                const bool sel = c.aux == pg;
                if (c.aux >= programCount() || !(sel || hot)) break;
                button(g, c.x, c.y, c.x + c.w, c.y + c.h, 5.f, sel, hot, false);
                programLabel(g, c, sel);
                break;
            }
            case Kind::Fader: {
                // A dark cap with a light centre line.
                const float cx = c.x + c.w * 0.5f, y0 = c.y + 8.f, y1 = c.y + c.h - 8.f;
                const float y = y1 - static_cast<float>(norm(c.param)) * (y1 - y0);
                const float hw = 14.f, hh = 7.5f;
                softShadow(g, cx - hw, y - hh, cx + hw, y + hh, 3.f, 3.f, 0.6f);
                shadeRoundRect(g, cx - hw, y - hh, cx + hw, y + hh, 3.f, [&](float u, float v, float d) {
                    Color col = mix(Color{ 0.33f, 0.345f, 0.36f }, Color{ 0.15f, 0.16f, 0.17f }, 0.5f + 0.5f * v);
                    if (hot) col = scale(col, 1.18f);
                    // The edge is lit along the top and left, away from the shadow.
                    const float lit = clamp01(0.5f - 0.5f * v + 0.35f * (u < -0.6f ? 1.f : 0.f));
                    const Color edge = mix(Color{ 0.28f, 0.295f, 0.31f }, Color{ 0.66f, 0.68f, 0.70f }, lit);
                    return toArgb(mix(col, edge, smoothstep(-1.5f, -0.7f, d) * 0.8f), 1.f);
                });
                drawLineAA(g, cx - hw + 3.f, y, cx + hw - 3.f, y, 1.6f, 0xFFE8ECEE);
                valueAbove(c);
                break;
            }
            case Kind::Knob: {
                // A dark disc, lit from the top left, with a thin light pointer.
                const float cx = c.x + c.w * 0.5f, cy = c.y + c.h * 0.5f, R = c.w * 0.5f - 7.f;
                const float a = (120.f + 300.f * static_cast<float>(norm(c.param))) * kPi / 180.f;
                const float px = std::cos(a), py = std::sin(a), s = static_cast<float>(g.getScale());
                shadeBox(g, cx - R - 1, cy - R - 1, cx + R + 1, cy + R + 1, [&](float x, float y) {
                    const float dx = x - cx, dy = y - cy, d = std::sqrt(dx * dx + dy * dy);
                    const float cov = clamp01((R - d) * s + 0.5f);
                    if (cov <= 0.f) return 0u;
                    // A subtle gradient, and a faint highlight on the rim at the top left (the shadow falls bottom right).
                    const float lit = std::hypot(dx + 0.3f * R, dy + 0.35f * R) / (1.5f * R);
                    Color col = mix(Color{ 0.165f, 0.175f, 0.19f }, Color{ 0.11f, 0.117f, 0.128f }, smoothstep(0.f, 1.f, lit));
                    if (hot) col = scale(col, 1.2f);
                    const float rim = smoothstep(R - 1.6f, R - 0.5f, d);
                    const float facing = d > 1e-4f ? clamp01(-0.707f * (dx + dy) / d) : 0.f;
                    col = mix(col, Color{ 0.56f, 0.58f, 0.60f }, rim * (0.12f + 0.55f * facing * facing));
                    const float along = dx * px + dy * py, perp = std::fabs(-dx * py + dy * px);
                    if (along > 0.3f * R && along < R - 2.f && perp < 0.9f) col = Color{ 0.93f, 0.95f, 0.96f };
                    return toArgb(col, cov);
                });
                valueAbove(c);
                break;
            }
            case Kind::PresetPrev:
            case Kind::PresetNext:
            case Kind::PresetSave: {
                if (!hot) break;
                button(g, c.x, c.y, c.x + c.w, c.y + c.h, 4.f, false, true, false);
                label(g, c.label, c.x + c.w * 0.5f, c.y + (c.h - 5.f) * 0.5f, kSilk, 5.f);
                break;
            }
            case Kind::PresetName: {
                shadeRoundRect(g, c.x, c.y, c.x + c.w, c.y + c.h, 4.f, [&](float, float v, float d) {
                    Color col = mix(Color{ 0.045f, 0.05f, 0.055f }, Color{ 0.085f, 0.09f, 0.10f }, 0.5f + 0.5f * v);
                    if (hot) col = scale(col, 1.4f);
                    return toArgb(mix(col, Color{ 0.30f, 0.315f, 0.33f }, smoothstep(-1.5f, -0.5f, d)), 1.f);
                });
                std::string name = plugin_->presetName();
                if (plugin_->presetModified()) name += " *";
                float cap = 8.f;
                while (cap > 4.5f && textWidth(kDisplayFont, name.c_str(), cap) > c.w - 14.f) cap -= 0.25f;
                drawText(g, kDisplayFont, name.c_str(), c.x + 8.f, c.y + (c.h - cap) * 0.5f, cap, 0xFFF0F2F3);
                break;
            }
        }
    }
    // The description of the control under the pointer, on the line at the bottom.
    const std::string tip = tooltipText();
    const char* text = tip.empty() ? "Hover over a control to see what it does." : tip.c_str();
    float cap = 7.f;
    while (cap > 4.6f && textWidth(kLabelFont, text, cap) > 600.f) cap -= 0.2f;
    drawText(g, kLabelFont, text, 20.f, 606.f, cap, tip.empty() ? kSilkDim : kSilk);
    if (menuOpen_) drawMenu(g);
}

// --- Preset menu ---------------------------------------------------------------------------------

namespace {
constexpr float kMenuX0 = 20.f, kMenuY0 = 42.f, kMenuX1 = 620.f, kMenuY1 = 314.f;
constexpr float kCatX = 30.f, kCatY = 54.f, kCatH = 22.f, kCatW = 130.f;
constexpr float kItemX = 176.f, kItemY = 54.f, kItemH = 16.f, kItemW = 220.f;
constexpr int kItemRows = 15, kItemCols = 2, kItemPage = kItemRows * kItemCols;
}

void TwGui::openMenu() {
    plugin_->rescanPresets();
    menuCats_ = plugin_->presetLibrary().categories();
    menuCat_ = 0;
    const int cur = plugin_->presetIndex();
    if (cur >= 0) {
        const std::string& cat = plugin_->presetLibrary().presets()[static_cast<size_t>(cur)].category;
        for (size_t i = 0; i < menuCats_.size(); ++i) if (menuCats_[i] == cat) menuCat_ = static_cast<int>(i);
    }
    fillMenuItems();
    for (size_t i = 0; i < menuItems_.size(); ++i)
        if (menuItems_[i] == cur) menuScroll_ = static_cast<int>(i) / kItemPage * kItemPage;
    menuOpen_ = true;
    menuHover_ = -1;
}

void TwGui::fillMenuItems() {
    menuItems_.clear();
    menuScroll_ = 0;
    if (menuCats_.empty()) return;
    const std::string& cat = menuCats_[static_cast<size_t>(menuCat_)];
    const auto& ps = plugin_->presetLibrary().presets();
    for (size_t i = 0; i < ps.size(); ++i) if (ps[i].category == cat) menuItems_.push_back(static_cast<int>(i));
}

int TwGui::menuHit(int x, int y) const {
    for (size_t i = 0; i < menuCats_.size(); ++i) {
        const float cy = kCatY + i * kCatH;
        if (x >= kCatX && x < kCatX + kCatW && y >= cy && y < cy + kCatH - 2.f) return -2 - static_cast<int>(i);
    }
    if (x < kItemX || y < kItemY) return -1;
    const int col = static_cast<int>((x - kItemX) / kItemW), row = static_cast<int>((y - kItemY) / kItemH);
    if (col >= kItemCols || row >= kItemRows) return -1;
    const int k = menuScroll_ + col * kItemRows + row;
    return k < static_cast<int>(menuItems_.size()) ? menuItems_[static_cast<size_t>(k)] : -1;
}

void TwGui::drawMenu(Graphics& g) {
    softShadow(g, kMenuX0, kMenuY0, kMenuX1, kMenuY1, 3.f, 6.f, 0.7f);
    shadeRoundRect(g, kMenuX0, kMenuY0, kMenuX1, kMenuY1, 3.f, [](float, float v, float d) {
        const Color c = scale(Color{ 0.06f, 0.068f, 0.078f }, 1.f + 0.3f * v);
        return toArgb(mix(c, { 0.30f, 0.315f, 0.33f }, smoothstep(-1.5f, 0.f, d)), 0.98f);
    });
    const auto& ps = plugin_->presetLibrary().presets();
    for (size_t i = 0; i < menuCats_.size(); ++i) {
        const float cy = kCatY + i * kCatH;
        const bool sel = static_cast<int>(i) == menuCat_, hot = menuHover_ == -2 - static_cast<int>(i);
        if (sel || hot) fill(g, kCatX - 4.f, cy - 1.f, kCatX + kCatW, cy + kCatH - 3.f, sel ? 0x70A8B8C8 : 0x38A8B8C8);
        const std::string n = TwPresetLibrary::categoryName(menuCats_[i]);
        drawText(g, kDisplayFont, n.c_str(), kCatX, cy + 4.f, 8.f, sel ? 0xFFFFFFFF : 0xFFC0C8CE);
    }
    drawLineAA(g, kItemX - 10.f, kCatY, kItemX - 10.f, kMenuY1 - 12.f, 0.8f, 0x80708090);
    const int cur = plugin_->presetIndex();
    for (int k = 0; k < kItemPage; ++k) {
        const int idx = menuScroll_ + k;
        if (idx >= static_cast<int>(menuItems_.size())) break;
        const int pi = menuItems_[static_cast<size_t>(idx)];
        const float x = kItemX + (k / kItemRows) * kItemW, y = kItemY + (k % kItemRows) * kItemH;
        if (pi == cur || pi == menuHover_) fill(g, x - 3.f, y - 1.f, x + kItemW - 8.f, y + kItemH - 2.f, pi == cur ? 0x70A8B8C8 : 0x38A8B8C8);
        std::string n = ps[static_cast<size_t>(pi)].name;
        if (n.size() > 3 && n[2] == ' ') n = n.substr(3);   // the category is the column on the left
        float cap = 7.f;
        while (cap > 4.f && textWidth(kDisplayFont, n.c_str(), cap) > kItemW - 14.f) cap -= 0.25f;
        drawText(g, kDisplayFont, n.c_str(), x, y + (kItemH - cap) * 0.5f - 1.f, cap, pi == cur ? 0xFFFFFFFF : 0xFFD0D6DA);
    }
    if (static_cast<int>(menuItems_.size()) > kItemPage) {
        char more[48];
        std::snprintf(more, sizeof more, "%d-%d OF %d   WHEEL: MORE", menuScroll_ + 1,
                      std::min(static_cast<int>(menuItems_.size()), menuScroll_ + kItemPage), static_cast<int>(menuItems_.size()));
        drawText(g, kLabelFont, more, kItemX, kMenuY1 - 14.f, 4.8f, 0xFFA8B0B4);
    }
}

bool TwGui::savePresetAs(const std::string& path) {
    std::string err;
    return plugin_->savePreset(path, err);
}

// Save: the system file dialog in the user folder (Linux: zenity / kdialog, polled; Windows:
// modal); without a dialog program, the next free "User NNN" name in the folder.
void TwGui::startSave() {
    const std::string dir = TwPresetLibrary::userPresetDirectory();
    if (dir.empty()) return;
    std::string base = plugin_->presetName();
    if (base.size() > 3 && base[2] == ' ') base = base.substr(3);
    spux::ui::classic::FileDialogOptions opt;
    opt.save = true;
    opt.title = "Save Tearwash preset";
    opt.filterName = "Tearwash presets";
    opt.extension = "twpreset";
    opt.startPath = dir + "/" + base + ".twpreset";
#if defined(__linux__) && !defined(__APPLE__)
    auto* d = static_cast<spux::ui::classic::FileDialogProcess*>(saveDialog_);
    if (!d) { d = new spux::ui::classic::FileDialogProcess(); saveDialog_ = d; }
    if (d->running()) return;
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (d->start(opt)) return;
#elif defined(_WIN32)
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const std::filesystem::path p = spux::ui::classic::runFileDialog(window_ ? reinterpret_cast<void*>(window_->nativeHandle().handle) : nullptr, opt);
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

void TwGui::pollSave() {
#if defined(__linux__) && !defined(__APPLE__)
    auto* d = static_cast<spux::ui::classic::FileDialogProcess*>(saveDialog_);
    if (!d || !d->running()) return;
    std::string path;
    bool save = false;
    if (d->poll(path, save) && save && !path.empty()) savePresetAs(path);
#endif
}

void TwGui::renderFrame() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    pollSave();
    const int BW = static_cast<int>(kWidth) * kScale, BH = static_cast<int>(kHeight) * kScale;
    if (!staticValid_) {
        static_.assign(static_cast<size_t>(BW) * BH, 0xFF000000);
        Graphics g(static_.data(), kWidth, kHeight, kScale);
        drawStatic(g);
        staticValid_ = true;
        drawnKey_.clear();
    }
    const std::string key = stateKey();
    if (key == drawnKey_) return;
    drawnKey_ = key;
    ++redraws_;
    compose_ = static_;
    Graphics g(compose_.data(), kWidth, kHeight, kScale);
    drawControls(g);
    // 2×2 box filter, two channels at a time (red and blue, then green and alpha, in 16-bit lanes).
    constexpr uint32_t kLanes = 0x00FF00FFu;
    for (uint32_t y = 0; y < kHeight; ++y) {
        const uint32_t* top = &compose_[static_cast<size_t>(y) * 2 * BW];
        const uint32_t* bottom = top + BW;
        uint32_t* out = &pixels_[static_cast<size_t>(y) * kWidth];
        for (uint32_t x = 0; x < kWidth; ++x) {
            const uint32_t a = top[2 * x], b = top[2 * x + 1], c = bottom[2 * x], d = bottom[2 * x + 1];
            const uint32_t rb = (a & kLanes) + (b & kLanes) + (c & kLanes) + (d & kLanes);
            const uint32_t ga = ((a >> 8) & kLanes) + ((b >> 8) & kLanes) + ((c >> 8) & kLanes) + ((d >> 8) & kLanes);
            out[x] = 0xFF000000u | ((rb >> 2) & kLanes) | (((ga >> 2) & kLanes) << 8);
        }
    }
    present();
}

void TwGui::present() {
    if (window_) window_->present(pixels_.data(), static_cast<int>(kWidth), static_cast<int>(kHeight),
                                  {0, 0, static_cast<int>(kWidth), static_cast<int>(kHeight)});
}

// --- Input ---------------------------------------------------------------------------------------

void TwGui::handleMouseDown(int x, int y, bool shift) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (menuOpen_) {
        const int h = menuHit(x, y);
        if (h <= -2) { menuCat_ = -2 - h; fillMenuItems(); }
        else if (h >= 0) { plugin_->loadPreset(h); menuOpen_ = false; }
        else if (x < kMenuX0 || x >= kMenuX1 || y < kMenuY0 || y >= kMenuY1) menuOpen_ = false;
        renderFrame();
        return;
    }
    const int i = controlAt(x, y);
    if (i < 0 || !enabled(ctls_[static_cast<size_t>(i)])) return;
    const Ctl& c = ctls_[static_cast<size_t>(i)];
    switch (c.kind) {
        case Kind::PresetPrev: plugin_->stepPreset(-1); break;
        case Kind::PresetNext: plugin_->stepPreset(1); break;
        case Kind::PresetName: openMenu(); break;
        case Kind::PresetSave: startSave(); break;
        case Kind::Program: plugin_->selectProgram(c.aux); break;
        case Kind::Fader:
        case Kind::Knob: {
            active_ = i;
            dragX_ = x; dragY_ = y;
            dragShift_ = shift;
            plugin_->onBeginEditFromGui(static_cast<clap_id>(c.param));
            if (c.kind == Kind::Fader) {
                // Click on the slot jumps the cap there; on the cap it just grabs it.
                const float y0 = c.y + 8.f, y1 = c.y + c.h - 8.f;
                const float capY = y1 - static_cast<float>(norm(c.param)) * (y1 - y0);
                if (std::fabs(y - capY) > 8.f && !shift) setNorm(c.param, (y1 - y) / (y1 - y0));
            }
            dragValue_ = norm(c.param);
            break;
        }
    }
    renderFrame();
}

void TwGui::handleMouseDrag(int x, int y, bool shift) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    (void)x;
    if (active_ < 0) return;
    const Ctl& c = ctls_[static_cast<size_t>(active_)];
    if (shift != dragShift_) {   // re-anchor so fine mode does not jump
        dragShift_ = shift;
        dragY_ = y;
        dragValue_ = norm(c.param);
    }
    const double range = c.kind == Kind::Fader ? c.h - 16.0 : 180.0;
    setNorm(c.param, dragValue_ + (dragY_ - y) / range * (shift ? 0.1 : 1.0));
    renderFrame();
}

void TwGui::handleMouseUp() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (active_ >= 0) plugin_->onEndEditFromGui(static_cast<clap_id>(ctls_[static_cast<size_t>(active_)].param));
    active_ = -1;
    renderFrame();
}

void TwGui::handleMouseMove(int x, int y) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (menuOpen_) { menuHover_ = menuHit(x, y); hover_ = -1; return; }
    const int i = controlAt(x, y);
    hover_ = (i >= 0 && enabled(ctls_[static_cast<size_t>(i)])) ? i : -1;
}

void TwGui::handleWheel(int x, int y, int delta) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (menuOpen_) {   // a column per notch
        const int n = static_cast<int>(menuItems_.size());
        const int maxScroll = std::max(0, ((n - 1) / kItemRows - (kItemCols - 1)) * kItemRows);
        menuScroll_ = std::max(0, std::min(menuScroll_ - delta * kItemRows, maxScroll));
        renderFrame();
        return;
    }
    const int i = controlAt(x, y);
    if (i < 0 || !enabled(ctls_[static_cast<size_t>(i)])) return;
    const Ctl& c = ctls_[static_cast<size_t>(i)];
    if (c.kind == Kind::PresetName) { plugin_->stepPreset(delta > 0 ? -1 : 1); renderFrame(); return; }
    if (c.kind != Kind::Fader && c.kind != Kind::Knob) return;
    const TwParamInfo& p = twParamInfo(static_cast<uint32_t>(c.param));
    // 0.5 % per notch; level knobs 0.5 dB or 1 %.
    // 0.5 % for the 0…1 controls, 0.5 dB for the gains, 1 % for the mix
    const bool db = p.law == spux::params::ParamLaw::Db;
    const bool unit = p.kind == spux::params::ParamKind::Float && !db && c.param != TW_MIX;
    const double step = unit ? 0.005 : (db ? 0.5 / (p.max - p.min) : 0.01);
    plugin_->onBeginEditFromGui(static_cast<clap_id>(c.param));
    setNorm(c.param, norm(c.param) + delta * step);
    plugin_->onEndEditFromGui(static_cast<clap_id>(c.param));
    renderFrame();
}

// --- Window plumbing -----------------------------------------------------------------------------

bool TwGui::setParent(const clap_window_t* window) {
    if (!window || !window->api) return false;
    if (window_) return true;   // already open
    spux::platform::ParentWindow parent;
#if defined(__linux__) && !defined(__APPLE__)
    if (std::strcmp(window->api, CLAP_WINDOW_API_X11) != 0) return false;
    parent = {spux::platform::WindowApi::X11, static_cast<std::uintptr_t>(window->x11)};
#elif defined(_WIN32)
    if (std::strcmp(window->api, CLAP_WINDOW_API_WIN32) != 0) return false;
    parent = {spux::platform::WindowApi::Win32, reinterpret_cast<std::uintptr_t>(window->win32)};
#else
    return false;
#endif
    // Set before open(): the window may call back as soon as it is open.
    window_ = spux::platform::NativeWindow::create();
    if (!window_) return false;
    spux::platform::WindowOptions options;
    options.keyboard = false;   // Tearwash has no key input; the host keeps its shortcuts
    if (!window_->open(parent, static_cast<int>(kWidth), static_cast<int>(kHeight), this, options)) {
        window_.reset();
        return false;
    }
    window_->setTickRate(30);
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        drawnKey_.clear();
        renderFrame();
    }
    return true;
}

bool TwGui::show() { visible_ = true; renderFrame(); return true; }
bool TwGui::hide() { visible_ = false; return true; }

void TwGui::destroy() {
    // close() stops the callbacks (and joins the event thread on X11): do not hold the mutex here.
    if (window_) window_->close();
    window_.reset();
}

// --- Native window events ------------------------------------------------------------------------

void TwGui::onMouseDown(int x, int y, spux::platform::MouseButton button, bool shift) {
    if (button == spux::platform::MouseButton::Left) handleMouseDown(x, y, shift);
}

void TwGui::onMouseUp(int, int, spux::platform::MouseButton button) {
    if (button == spux::platform::MouseButton::Left) handleMouseUp();
}

void TwGui::onMouseMove(int x, int y, bool leftDown, bool shift) {
    if (leftDown) handleMouseDrag(x, y, shift);
    else handleMouseMove(x, y);
}

void TwGui::onMouseWheel(int x, int y, float steps, bool) {
    if (steps != 0.0f) handleWheel(x, y, steps > 0.0f ? 1 : -1);
}

void TwGui::onTick() {
    if (visible_) renderFrame();
}

void TwGui::onExpose() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    present();
}

} // namespace tearwash
