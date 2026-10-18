#include "TwGui.hpp"
#include "tearwash/gui/FileDialog.hpp"
#include <filesystem>
#include "TearwashClap.hpp"
#include "tearwash/gui/Graphics.hpp"
#include "tearwash/gui/modern/ModernDraw.hpp"
#include "tearwash/gui/modern/LabelFont.hpp"
#include "tearwash/gui/modern/DisplayFont.hpp"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>

#if defined(__linux__) && !defined(__APPLE__)
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#endif
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#endif

namespace tearwash {

using tannhauser::Graphics;
using namespace tannhauser::modern;

namespace {

constexpr int kScale = 2;
constexpr uint32_t kSilk = 0xF2E6E6E0;
constexpr uint32_t kSilkDim = 0xB09A9C98;
constexpr uint32_t kValue = 0xFFFFD58A;        // the values above the controls: light amber, readable on the charcoal
constexpr float kCap = 6.0f;

void fill(Graphics& g, float x0, float y0, float x1, float y1, uint32_t argb) {
    shadeBox(g, x0, y0, x1, y1, [argb](float, float) { return argb; });
}

// Rounded rectangle; f(u, v, d) gives the colour (u, v in −1…1 across the box, d the signed
// distance in logical pixels).
template <class F>
void roundRect(Graphics& g, float x0, float y0, float x1, float y1, float r, F&& f) {
    const float s = static_cast<float>(g.getScale());
    const float cx = 0.5f * (x0 + x1), cy = 0.5f * (y0 + y1), hx = 0.5f * (x1 - x0), hy = 0.5f * (y1 - y0);
    shadeBox(g, x0 - 1, y0 - 1, x1 + 1, y1 + 1, [&](float x, float y) {
        const float d = sdRoundRect(x - cx, y - cy, hx, hy, r);
        const float cov = clamp01(0.5f - d * s);
        if (cov <= 0.f) return 0u;
        const uint32_t c = f((x - cx) / hx, (y - cy) / hy, d);
        return toArgb(fromArgb(c), alphaOf(c) * cov);
    });
}

void softShadow(Graphics& g, float x0, float y0, float x1, float y1, float r, float spread, float alpha) {
    const float cx = 0.5f * (x0 + x1) + 1.f, cy = 0.5f * (y0 + y1) + 2.f, hx = 0.5f * (x1 - x0), hy = 0.5f * (y1 - y0);
    shadeBox(g, x0 - spread + 1, y0 - spread + 2, x1 + spread + 1, y1 + spread + 2, [&](float x, float y) {
        const float d = sdRoundRect(x - cx, y - cy, hx, hy, r);
        const float a = alpha * (1.f - smoothstep(-spread * 0.3f, spread, d));
        return a > 0.002f ? toArgb({ 0.f, 0.f, 0.f }, a) : 0u;
    });
}

void label(Graphics& g, const char* text, float cx, float capTop, uint32_t ink = kSilk, float cap = kCap) {
    drawTextCentered(g, kLabelFont, text, cx, capTop, cap, ink);
}

// A dark rounded button with a light edge; selected: a bright outline.
void button(Graphics& g, float x0, float y0, float x1, float y1, float r, bool selected, bool hot) {
    softShadow(g, x0, y0, x1, y1, r, 3.f, 0.5f);
    const Color edge = selected ? Color{ 0.88f, 0.90f, 0.92f } : Color{ 0.30f, 0.315f, 0.33f };
    const float bw = selected ? 1.5f : 1.f;
    roundRect(g, x0, y0, x1, y1, r, [&](float, float v, float d) {
        Color fill = mix(Color{ 0.215f, 0.225f, 0.24f }, Color{ 0.125f, 0.13f, 0.14f }, 0.5f + 0.5f * v);
        if (selected) fill = scale(fill, 1.1f);
        if (hot) fill = scale(fill, 1.15f);
        return toArgb(mix(fill, edge, smoothstep(-bw - 0.7f, -bw, d)), 1.f);
    });
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
    delete static_cast<tannhauser::FileDialogProcess*>(saveDialog_);
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
        roundRect(g, cx - 2.f, y0 - 2, cx + 2.f, y1 + 2, 2.f, [](float, float v, float d) {
            return toArgb(mix({ 0.015f, 0.017f, 0.02f }, { 0.28f, 0.30f, 0.32f }, smoothstep(-1.f, 0.f, d) * (v > 0 ? 0.8f : 0.3f)), 1.f);
        });
        for (int i = 0; i <= 10; ++i) {
            const float y = y0 + (y1 - y0) * i / 10.f, len = (i % 5 == 0) ? 5.f : 3.f;
            drawLineAA(g, c.x + 2.f, y, c.x + 2.f + len, y, 0.8f, 0x70A8B0B4);
            drawLineAA(g, c.x + c.w - 2.f - len, y, c.x + c.w - 2.f, y, 0.8f, 0x70A8B0B4);
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
                if (c.aux >= programCount()) break;
                const bool sel = c.aux == pg;
                button(g, c.x, c.y, c.x + c.w, c.y + c.h, 5.f, sel, hot);
                const char* text = programName(c.aux);
                float cap = 6.4f;
                while (cap > 3.6f && textWidth(kLabelFont, text, cap) > c.w - 8.f) cap -= 0.2f;
                label(g, text, c.x + c.w * 0.5f, c.y + (c.h - cap) * 0.5f, sel ? 0xFFFFFFFF : kSilk, cap);
                break;
            }
            case Kind::Fader: {
                // A dark cap with a light centre line.
                const float cx = c.x + c.w * 0.5f, y0 = c.y + 8.f, y1 = c.y + c.h - 8.f;
                const float y = y1 - static_cast<float>(norm(c.param)) * (y1 - y0);
                const float hw = 14.f, hh = 7.5f;
                softShadow(g, cx - hw, y - hh, cx + hw, y + hh, 3.f, 3.f, 0.6f);
                roundRect(g, cx - hw, y - hh, cx + hw, y + hh, 3.f, [&](float, float v, float d) {
                    Color col = mix(Color{ 0.33f, 0.345f, 0.36f }, Color{ 0.15f, 0.16f, 0.17f }, 0.5f + 0.5f * v);
                    if (hot) col = scale(col, 1.18f);
                    return toArgb(mix(col, Color{ 0.50f, 0.52f, 0.54f }, smoothstep(-1.5f, -0.7f, d) * 0.7f), 1.f);
                });
                drawLineAA(g, cx - hw + 3.f, y, cx + hw - 3.f, y, 1.6f, 0xFFE8ECEE);
                label(g, c.label, cx, c.y + c.h + 4.f, kSilk, 7.2f);
                valueAbove(c);
                break;
            }
            case Kind::Knob: {
                // A dark disc, lit from the top left, with a thin light pointer.
                const float cx = c.x + c.w * 0.5f, cy = c.y + c.h * 0.5f, R = c.w * 0.5f - 7.f;
                const float a = (120.f + 300.f * static_cast<float>(norm(c.param))) * kPi / 180.f;
                const float px = std::cos(a), py = std::sin(a), s = static_cast<float>(g.getScale());
                softShadow(g, cx - R, cy - R, cx + R, cy + R, R, 3.f, 0.6f);
                shadeBox(g, cx - R - 1, cy - R - 1, cx + R + 1, cy + R + 1, [&](float x, float y) {
                    const float dx = x - cx, dy = y - cy, d = std::sqrt(dx * dx + dy * dy);
                    const float cov = clamp01((R - d) * s + 0.5f);
                    if (cov <= 0.f) return 0u;
                    const float lit = std::hypot(dx + 0.3f * R, dy + 0.35f * R) / (1.5f * R);
                    Color col = mix(Color{ 0.26f, 0.27f, 0.29f }, Color{ 0.085f, 0.09f, 0.10f }, smoothstep(0.f, 1.f, lit));
                    if (hot) col = scale(col, 1.2f);
                    col = mix(col, Color{ 0.34f, 0.355f, 0.375f }, smoothstep(R - 1.6f, R - 0.5f, d) * 0.8f);
                    const float along = dx * px + dy * py, perp = std::fabs(-dx * py + dy * px);
                    if (along > 0.3f * R && along < R - 2.f && perp < 0.9f) col = Color{ 0.93f, 0.95f, 0.96f };
                    return toArgb(col, cov);
                });
                label(g, c.label, cx, c.y + c.h + 3.f, kSilk, 7.2f);
                valueAbove(c);
                break;
            }
            case Kind::PresetPrev:
            case Kind::PresetNext:
            case Kind::PresetSave: {
                button(g, c.x, c.y, c.x + c.w, c.y + c.h, 4.f, false, hot);
                label(g, c.label, c.x + c.w * 0.5f, c.y + (c.h - 5.f) * 0.5f, kSilk, 5.f);
                break;
            }
            case Kind::PresetName: {
                roundRect(g, c.x, c.y, c.x + c.w, c.y + c.h, 4.f, [&](float, float v, float d) {
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
    roundRect(g, kMenuX0, kMenuY0, kMenuX1, kMenuY1, 3.f, [](float, float v, float d) {
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
    tannhauser::FileDialogOptions opt;
    opt.save = true;
    opt.title = "Save Tearwash preset";
    opt.filterName = "Tearwash presets";
    opt.extension = "twpreset";
    opt.startPath = dir + "/" + base + ".twpreset";
#if defined(__linux__) && !defined(__APPLE__)
    auto* d = static_cast<tannhauser::FileDialogProcess*>(saveDialog_);
    if (!d) { d = new tannhauser::FileDialogProcess(); saveDialog_ = d; }
    if (d->running()) return;
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (d->start(opt)) return;
#elif defined(_WIN32)
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const std::filesystem::path p = tannhauser::runFileDialog(hwnd_, opt);
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
    auto* d = static_cast<tannhauser::FileDialogProcess*>(saveDialog_);
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
    // 2×2 box filter.
    for (uint32_t y = 0; y < kHeight; ++y) {
        for (uint32_t x = 0; x < kWidth; ++x) {
            uint32_t r = 0, gg = 0, b = 0;
            for (int dy = 0; dy < 2; ++dy)
                for (int dx = 0; dx < 2; ++dx) {
                    const uint32_t p = compose_[static_cast<size_t>(y * 2 + dy) * BW + x * 2 + dx];
                    r += (p >> 16) & 0xFF; gg += (p >> 8) & 0xFF; b += p & 0xFF;
                }
            pixels_[static_cast<size_t>(y) * kWidth + x] = 0xFF000000u | ((r / 4) << 16) | ((gg / 4) << 8) | (b / 4);
        }
    }
#if defined(__linux__) && !defined(__APPLE__)
    drawX11();
#elif defined(_WIN32)
    if (hwnd_) InvalidateRect(static_cast<HWND>(hwnd_), nullptr, FALSE);
#endif
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
    const double step = p.kind == TwKind::Unit ? 0.005 : (p.kind == TwKind::Decibel ? 0.5 / (p.max - p.min) : 0.01);
    plugin_->onBeginEditFromGui(static_cast<clap_id>(c.param));
    setNorm(c.param, norm(c.param) + delta * step);
    plugin_->onEndEditFromGui(static_cast<clap_id>(c.param));
    renderFrame();
}

// --- Window plumbing -----------------------------------------------------------------------------

bool TwGui::setParent(const clap_window_t* window) {
    if (!window || !window->api) return false;
#if defined(__linux__) && !defined(__APPLE__)
    if (std::strcmp(window->api, CLAP_WINDOW_API_X11) == 0) {
        parent11_ = window->x11;
        initX11();
        return true;
    }
#elif defined(_WIN32)
    if (std::strcmp(window->api, CLAP_WINDOW_API_WIN32) == 0) {
        parentHwnd_ = window->win32;
        initWin32();
        return true;
    }
#endif
    return false;
}

bool TwGui::show() { visible_ = true; renderFrame(); return true; }
bool TwGui::hide() { visible_ = false; return true; }

void TwGui::destroy() {
    running_ = false;
    if (eventThread_.joinable()) eventThread_.join();
#if defined(__linux__) && !defined(__APPLE__)
    if (display11_ && created11_) {
        Display* d = static_cast<Display*>(display11_);
        XDestroyWindow(d, window11_);
        XCloseDisplay(d);
        display11_ = nullptr;
        created11_ = false;
    }
#elif defined(_WIN32)
    if (hwnd_) {
        DestroyWindow(static_cast<HWND>(hwnd_));
        hwnd_ = nullptr;
    }
#endif
}

#if defined(__linux__) && !defined(__APPLE__)
void TwGui::initX11() {
    if (created11_) return;
    Display* d = XOpenDisplay(nullptr);
    if (!d) return;
    display11_ = d;
    const int screen = DefaultScreen(d);
    const Window parent = parent11_ ? parent11_ : RootWindow(d, screen);
    window11_ = XCreateSimpleWindow(d, parent, 0, 0, kWidth, kHeight, 0, BlackPixel(d, screen), BlackPixel(d, screen));
    XSelectInput(d, window11_, ExposureMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask);
    XMapWindow(d, window11_);
    XFlush(d);
    created11_ = true;
    drawnKey_.clear();
    renderFrame();
    running_ = true;
    eventThread_ = std::thread(&TwGui::eventLoopX11, this);
}

void TwGui::eventLoopX11() {
    try {
        Display* d = static_cast<Display*>(display11_);
        auto lastPaint = std::chrono::steady_clock::now();
        while (running_) {
            while (XPending(d) > 0) {
                XEvent ev;
                XNextEvent(d, &ev);
                if (ev.type == Expose) {
                    drawX11();
                } else if (ev.type == ButtonPress) {
                    const bool shift = (ev.xbutton.state & ShiftMask) != 0;
                    if (ev.xbutton.button == Button1) handleMouseDown(ev.xbutton.x, ev.xbutton.y, shift);
                    else if (ev.xbutton.button == Button4) handleWheel(ev.xbutton.x, ev.xbutton.y, 1);
                    else if (ev.xbutton.button == Button5) handleWheel(ev.xbutton.x, ev.xbutton.y, -1);
                } else if (ev.type == ButtonRelease && ev.xbutton.button == Button1) {
                    handleMouseUp();
                } else if (ev.type == MotionNotify) {
                    if (ev.xmotion.state & Button1Mask) handleMouseDrag(ev.xmotion.x, ev.xmotion.y, (ev.xmotion.state & ShiftMask) != 0);
                    else handleMouseMove(ev.xmotion.x, ev.xmotion.y);
                }
            }
            const auto now = std::chrono::steady_clock::now();
            if (visible_ && now - lastPaint >= std::chrono::milliseconds(33)) {
                lastPaint = now;
                renderFrame();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    } catch (...) {
        // Never let an exception escape into the host.
    }
}

void TwGui::drawX11() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!display11_ || !created11_) return;
    Display* d = static_cast<Display*>(display11_);
    const int screen = DefaultScreen(d);
    XImage* img = XCreateImage(d, DefaultVisual(d, screen), 24, ZPixmap, 0, reinterpret_cast<char*>(pixels_.data()),
                               kWidth, kHeight, 32, 0);
    if (!img) return;
    XPutImage(d, window11_, DefaultGC(d, screen), img, 0, 0, 0, 0, kWidth, kHeight);
    img->data = nullptr;
    XDestroyImage(img);
    XFlush(d);
}
#endif

#if defined(_WIN32)
static const wchar_t* kClassName = L"TearwashWindowClass";
static bool g_classRegistered = false;

static LRESULT CALLBACK wndProcImpl(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    TwGui* gui = reinterpret_cast<TwGui*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
        case WM_CREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
            SetTimer(hwnd, 1, 33, nullptr);
            return 0;
        }
        case WM_TIMER: if (gui && gui->isVisible()) gui->renderFrame(); return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);
            if (gui) gui->drawWin32();
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_LBUTTONDOWN:
            if (gui) {
                SetCapture(hwnd);
                gui->handleMouseDown(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), (wParam & MK_SHIFT) != 0);
            }
            return 0;
        case WM_MOUSEMOVE:
            if (gui) {
                if (wParam & MK_LBUTTON) gui->handleMouseDrag(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), (wParam & MK_SHIFT) != 0);
                else gui->handleMouseMove(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            }
            return 0;
        case WM_LBUTTONUP: if (gui) { ReleaseCapture(); gui->handleMouseUp(); } return 0;
        case WM_MOUSEWHEEL:
            if (gui) {
                POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                ScreenToClient(hwnd, &pt);
                gui->handleWheel(pt.x, pt.y, GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? 1 : -1);
            }
            return 0;
        case WM_DESTROY: KillTimer(hwnd, 1); return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    try { return wndProcImpl(hwnd, msg, wParam, lParam); } catch (...) { return DefWindowProcW(hwnd, msg, wParam, lParam); }
}

void TwGui::initWin32() {
    if (hwnd_) return;
    HINSTANCE inst = GetModuleHandleW(nullptr);
    if (!g_classRegistered) {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = wndProc;
        wc.hInstance = inst;
        wc.lpszClassName = kClassName;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        RegisterClassW(&wc);
        g_classRegistered = true;
    }
    hwnd_ = CreateWindowExW(0, kClassName, L"Tearwash", WS_CHILD | WS_VISIBLE, 0, 0, kWidth, kHeight,
                            static_cast<HWND>(parentHwnd_), nullptr, inst, this);
    drawnKey_.clear();
    renderFrame();
}

void TwGui::drawWin32() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!hwnd_) return;
    HDC hdc = GetDC(static_cast<HWND>(hwnd_));
    if (!hdc) return;
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = static_cast<LONG>(kWidth);
    bmi.bmiHeader.biHeight = -static_cast<LONG>(kHeight);
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    SetDIBitsToDevice(hdc, 0, 0, kWidth, kHeight, 0, 0, 0, kHeight, pixels_.data(), &bmi, DIB_RGB_COLORS);
    ReleaseDC(static_cast<HWND>(hwnd_), hdc);
}
#endif

// --- CLAP GUI extension --------------------------------------------------------------------------

static TearwashClap* selfOf(const clap_plugin_t* p) { return static_cast<TearwashClap*>(p->plugin_data); }

const clap_plugin_gui_t g_tearwashGuiExtension = {
    [](const clap_plugin_t*, const char* api, bool floating) -> bool {
#if defined(__linux__) && !defined(__APPLE__)
        return api && std::strcmp(api, CLAP_WINDOW_API_X11) == 0 && !floating;
#elif defined(_WIN32)
        return api && std::strcmp(api, CLAP_WINDOW_API_WIN32) == 0 && !floating;
#else
        (void)api; (void)floating;
        return false;
#endif
    },
    [](const clap_plugin_t*, const char** api, bool* floating) -> bool {
        if (!api || !floating) return false;
#if defined(__linux__) && !defined(__APPLE__)
        *api = CLAP_WINDOW_API_X11;
#elif defined(_WIN32)
        *api = CLAP_WINDOW_API_WIN32;
#else
        return false;
#endif
        *floating = false;
        return true;
    },
    [](const clap_plugin_t* p, const char*, bool) -> bool {
        try { selfOf(p)->createGuiWindow(); } catch (...) { return false; }
        return true;
    },
    [](const clap_plugin_t* p) { try { selfOf(p)->destroyGuiWindow(); } catch (...) {} },
    [](const clap_plugin_t*, double) -> bool { return false; },
    [](const clap_plugin_t*, uint32_t* w, uint32_t* h) -> bool {
        if (!w || !h) return false;
        *w = TwGui::kWidth;
        *h = TwGui::kHeight;
        return true;
    },
    [](const clap_plugin_t*) -> bool { return false; },
    [](const clap_plugin_t*, clap_gui_resize_hints_t*) -> bool { return false; },
    [](const clap_plugin_t*, uint32_t* w, uint32_t* h) -> bool {
        if (!w || !h) return false;
        *w = TwGui::kWidth;
        *h = TwGui::kHeight;
        return true;
    },
    [](const clap_plugin_t* p, uint32_t w, uint32_t h) -> bool {
        try { auto* g = selfOf(p)->getGuiWindow(); return g ? g->setSize(w, h) : true; } catch (...) { return false; }
    },
    [](const clap_plugin_t* p, const clap_window_t* window) -> bool {
        if (!window || !window->api) return false;
        try {
            if (!selfOf(p)->getGuiWindow()) selfOf(p)->createGuiWindow();
            return selfOf(p)->getGuiWindow()->setParent(window);
        } catch (...) { return false; }
    },
    [](const clap_plugin_t*, const clap_window_t*) -> bool { return false; },
    [](const clap_plugin_t*, const char*) {},
    [](const clap_plugin_t* p) -> bool {
        try { auto* g = selfOf(p)->getGuiWindow(); return g ? g->show() : false; } catch (...) { return false; }
    },
    [](const clap_plugin_t* p) -> bool {
        try { auto* g = selfOf(p)->getGuiWindow(); return g ? g->hide() : false; } catch (...) { return false; }
    },
};

} // namespace tearwash
