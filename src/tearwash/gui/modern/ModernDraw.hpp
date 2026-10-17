#ifndef TANNHAUSER_MODERN_DRAW_HPP
#define TANNHAUSER_MODERN_DRAW_HPP

// Drawing helpers shared by the modern skins (TANNHAUSER_GUI_STYLE=MODERN): the
// Acidus panel (ModernSkin.cpp) and Burette (ModernSequencerSkin.cpp).
//
// Both GUIs paint the modern look into a 2x supersampled buffer and
// box-filter it down. These helpers shade at that buffer resolution
// (Graphics::blendPixel) with analytic edge coverage, so edges come out
// smooth after the downsample. Coordinates are logical (window) pixels.

#include "tearwash/gui/Graphics.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace tannhauser {
namespace modern {

constexpr float kPi = 3.14159265358979f;

struct Vec3 { float x, y, z; };
struct Color { float r, g, b; };

inline float clamp01(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }
inline float smoothstep(float e0, float e1, float x) {
    const float t = clamp01((x - e0) / (e1 - e0));
    return t * t * (3.f - 2.f * t);
}
inline Vec3 normalize(Vec3 v) {
    const float l = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    return { v.x / l, v.y / l, v.z / l };
}
inline float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Color mix(Color a, Color b, float t) {
    return { a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t };
}
inline Color scale(Color c, float k) { return { c.r * k, c.g * k, c.b * k }; }
inline Color fromArgb(uint32_t c) {
    return { ((c >> 16) & 0xFF) / 255.f, ((c >> 8) & 0xFF) / 255.f, (c & 0xFF) / 255.f };
}
inline float alphaOf(uint32_t c) { return ((c >> 24) & 0xFF) / 255.f; }
inline uint32_t toArgb(Color c, float alpha) {
    auto ch = [](float v) { return static_cast<uint32_t>(clamp01(v) * 255.f + 0.5f); };
    return (ch(alpha) << 24) | (ch(c.r) << 16) | (ch(c.g) << 8) | ch(c.b);
}

// Deterministic hash noise, so the wear looks the same on every open.
inline uint32_t hashU(uint32_t x) {
    x ^= x >> 16; x *= 0x7FEB352Du; x ^= x >> 15; x *= 0x846CA68Bu; x ^= x >> 16;
    return x;
}
inline float hash2(int x, int y, uint32_t seed) {
    return (hashU(static_cast<uint32_t>(x) * 0x1F1F1F1Fu ^ hashU(static_cast<uint32_t>(y) + seed * 0x9E3779B9u)) & 0xFFFFFF)
           / 16777216.f;
}
float valueNoise(float x, float y, uint32_t seed);
float fbm(float x, float y, uint32_t seed);

// One light, upper left and in front, for every shaded part.
extern const Vec3 kLight;
extern const float kLightAzimuth;

struct Material {
    Color albedo;
    float ambient, diffuse, specular, shininess;   // shininess: a whole number
};
Color shadeLit(const Material& m, Vec3 n);

// A surface tilted `tilt` radians away from the viewer towards (ux, uy).
inline Vec3 tiltedNormal(float ux, float uy, float tilt) {
    const float s = std::sin(tilt);
    return { ux * s, uy * s, std::cos(tilt) };
}

// Calls f(lx, ly) for every buffer pixel whose centre lies in the logical
// box, and blends the colour it returns (alpha 0 = skip).
template <class F>
void shadeBox(Graphics& g, float x0, float y0, float x1, float y1, F&& f) {
    const int s = g.getScale();
    const int bx0 = std::max(0, static_cast<int>(std::floor(x0 * s)));
    const int by0 = std::max(0, static_cast<int>(std::floor(y0 * s)));
    const int bx1 = std::min(static_cast<int>(g.getWidth()) * s, static_cast<int>(std::ceil(x1 * s)));
    const int by1 = std::min(static_cast<int>(g.getHeight()) * s, static_cast<int>(std::ceil(y1 * s)));
    for (int by = by0; by < by1; ++by) {
        const float ly = (by + 0.5f) / s;
        for (int bx = bx0; bx < bx1; ++bx) {
            const uint32_t c = f((bx + 0.5f) / s, ly);
            if (c >> 24) g.blendPixel(bx, by, c);
        }
    }
}

// Anti-aliased line with round caps, `width` logical pixels wide.
void drawLineAA(Graphics& g, float x0, float y0, float x1, float y1, float width, uint32_t argb);

// Signed distance to a rounded rectangle centred at the origin, half size
// (hx, hy), corner radius r: negative inside.
inline float sdRoundRect(float x, float y, float hx, float hy, float r) {
    const float qx = std::fabs(x) - hx + r, qy = std::fabs(y) - hy + r;
    const float ox = std::max(qx, 0.f), oy = std::max(qy, 0.f);
    return std::sqrt(ox * ox + oy * oy) + std::min(std::max(qx, qy), 0.f) - r;
}
// Outward unit direction of the rounded rectangle's distance field.
void sdRoundRectDir(float x, float y, float hx, float hy, float r, float& ux, float& uy);

// --- Lettering ---------------------------------------------------------------

// A pre-rasterized font (tools/gen_gui_fonts.py): 4-bit glyphs at the
// GUI's 2x resolution. Glyph: offset into the alpha data, bitmap size,
// bitmap offset from the pen at the top of the line box, advance in 1/16 px.
struct AtlasGlyph { uint16_t offset; uint8_t w, h; int8_t dx, dy; uint16_t advance16; };
struct AtlasFont {
    const AtlasGlyph* glyphs;
    const uint8_t* alpha;
    int first, last;
    int capTop;      // top of the line box to the top of a capital, 2x px
    int capHeight;   // 2x px
};

// Text width in logical pixels at the given cap height.
float textWidth(const AtlasFont& font, const char* text, float capHeight);
// Draws text with its left edge at x and its capitals' tops at capTop.
// `wear` (0..1) makes the ink patchy, like old silk-screen print.
void drawText(Graphics& g, const AtlasFont& font, const char* text, float x, float capTop,
              float capHeight, uint32_t argb, float wear = 0.f);
inline void drawTextCentered(Graphics& g, const AtlasFont& font, const char* text, float centerX,
                             float capTop, float capHeight, uint32_t argb, float wear = 0.f) {
    drawText(g, font, text, centerX - textWidth(font, text, capHeight) / 2.f, capTop, capHeight, argb, wear);
}

// --- Panel -------------------------------------------------------------------

constexpr uint32_t kInk = 0xF0222326;              // printed panel ink
const Color kPaint{ 0.785f, 0.790f, 0.775f };      // aged silver paint

// Fills the whole buffer with the worn silver panel: brushed grain, grime,
// stains, rubbed paint, scratches and chips. `trims` adds the darker
// extrusions along the top and bottom edges (the Acidus panel).
void paintWornPanel(Graphics& g, int width, int height, bool trims);

} // namespace modern
} // namespace tannhauser

#endif // TANNHAUSER_MODERN_DRAW_HPP
