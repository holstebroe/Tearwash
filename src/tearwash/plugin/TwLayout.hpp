#ifndef TEARWASH_TWLAYOUT_HPP
#define TEARWASH_TWLAYOUT_HPP

// Tearwash panel layout as data (640 x 628 logical pixels), top to bottom: the preset bar, the program buttons (two
// rows of five), the decay faders, the space faders, the motion, shimmer and level knobs, and a one-line description
// of the control under the pointer. `box` is the rectangle a control occupies on the panel (a fader 40 x 100, a
// knob 54 x 54); the widget's bounds in the Gui reach beyond it for the value above and the scale, and are derived
// from it (faderBounds, knobBounds) with the rows of the look (twClassicConfig in TwEditor.cpp).

#include <array>
#include <cstdint>

#include "TwParams.hpp"
#include "spux/ui/Geometry.hpp"

namespace tearwash {

constexpr int kPanelWidth = 640, kPanelHeight = 628;

struct TwControl {
    uint32_t param;
    const char* label;
    spux::ui::Rect box;
};

// Programs: ten buttons, five to a row.
inline spux::ui::Rect programButton(int i) {
    return { 20 + (i % 5) * 122, 60 + (i / 5) * 41, 112, 32 };
}

// The preset bar: arrows, the name (a click opens the menu) and SAVE.
constexpr spux::ui::Rect kPresetBar{ 352, 13, 268, 22 };

// Decay, in signal order: the input cut, the band split, the two band decays, the loop damping.
inline const std::array<TwControl, 5>& decayFaders() {
    static const std::array<TwControl, 5> f = { {
        { TW_HIGHCUT, "IN CUT", { 60, 184, 40, 100 } },
        { TW_CROSSOVER, "SPLIT FREQ", { 180, 184, 40, 100 } },
        { TW_LOWDECAY, "LOW DECAY", { 300, 184, 40, 100 } },
        { TW_DECAY, "HIGH DECAY", { 420, 184, 40, 100 } },
        { TW_DAMPING, "DAMP CUT", { 540, 184, 40, 100 } },
    } };
    return f;
}

// Space, in signal order: the room's size, the predelay, the input diffusion, the tank's clarity, the early/late balance.
inline const std::array<TwControl, 5>& spaceFaders() {
    static const std::array<TwControl, 5> f = { {
        { TW_SIZE, "SIZE", { 60, 348, 40, 100 } },
        { TW_PREDELAY, "PREDELAY", { 180, 348, 40, 100 } },
        { TW_DIFFUSION, "DIFFUSION", { 300, 348, 40, 100 } },
        { TW_CLARITY, "CLARITY", { 420, 348, 40, 100 } },
        { TW_DISTANCE, "DISTANCE", { 540, 348, 40, 100 } },
    } };
    return f;
}

// Three groups of two knobs: motion, shimmer, level.
inline const std::array<TwControl, 6>& knobs() {
    static const std::array<TwControl, 6> k = { {
        { TW_MODRATE, "RATE", { 40, 520, 54, 54 } },
        { TW_MODDEPTH, "DEPTH", { 135, 520, 54, 54 } },
        { TW_SHIMMER, "AMOUNT", { 245, 520, 54, 54 } },
        { TW_SHIMMERINT, "INTERVAL", { 340, 520, 54, 54 } },
        { TW_MIX, "MIX", { 450, 520, 54, 54 } },
        { TW_OUTGAIN, "OUTPUT", { 545, 520, 54, 54 } },
    } };
    return k;
}

// The widget bounds: a fader's value row above and its groove and cap inside; a knob's value row above its dial.
inline spux::ui::Rect faderBounds(const TwControl& c) { return { c.box.x, c.box.y - 27, c.box.w, c.box.h + 28 }; }
inline spux::ui::Rect knobBounds(const TwControl& c) { return { c.box.x, c.box.y - 23, c.box.w, 74 }; }

} // namespace tearwash

#endif
