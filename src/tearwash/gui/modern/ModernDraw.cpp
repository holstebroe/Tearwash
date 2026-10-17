#include "ModernDraw.hpp"
#include <memory>
#include <mutex>
#include <vector>

namespace tannhauser {
namespace modern {

float valueNoise(float x, float y, uint32_t seed) {
    const int xi = static_cast<int>(std::floor(x)), yi = static_cast<int>(std::floor(y));
    float fx = x - xi, fy = y - yi;
    fx = fx * fx * (3.f - 2.f * fx);
    fy = fy * fy * (3.f - 2.f * fy);
    const float a = hash2(xi, yi, seed), b = hash2(xi + 1, yi, seed);
    const float c = hash2(xi, yi + 1, seed), d = hash2(xi + 1, yi + 1, seed);
    return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy;
}

float fbm(float x, float y, uint32_t seed) {
    return 0.5f * valueNoise(x, y, seed) + 0.3f * valueNoise(2.1f * x, 2.1f * y, seed + 1)
           + 0.2f * valueNoise(4.3f * x, 4.3f * y, seed + 2);
}

const Vec3 kLight = normalize({ -0.45f, -0.62f, 0.66f });
static const Vec3 kHalf = normalize({ kLight.x, kLight.y, kLight.z + 1.f });
const float kLightAzimuth = std::atan2(kLight.y, kLight.x);

// x^n for a whole n >= 1, by squaring: much cheaper than powf, and the
// materials' shininess values are whole numbers.
static float powInt(float x, int n) {
    float r = 1.f;
    while (n > 0) {
        if (n & 1) r *= x;
        x *= x;
        n >>= 1;
    }
    return r;
}

Color shadeLit(const Material& m, Vec3 n) {
    const float diff = std::max(0.f, dot(n, kLight));
    const float spec = powInt(std::max(0.f, dot(n, kHalf)), static_cast<int>(m.shininess)) * m.specular;
    const float k = m.ambient + m.diffuse * diff;
    return { m.albedo.r * k + spec, m.albedo.g * k + spec, m.albedo.b * k + spec };
}

void drawLineAA(Graphics& g, float x0, float y0, float x1, float y1, float width, uint32_t argb) {
    const float hw = 0.5f * width;
    const float s = static_cast<float>(g.getScale());
    const float dx = x1 - x0, dy = y1 - y0;
    const float len2 = std::max(dx * dx + dy * dy, 1e-6f);
    const float a = alphaOf(argb);
    const Color c = fromArgb(argb);
    shadeBox(g, std::min(x0, x1) - hw - 1, std::min(y0, y1) - hw - 1,
             std::max(x0, x1) + hw + 1, std::max(y0, y1) + hw + 1,
             [&](float x, float y) {
                 const float t = clamp01(((x - x0) * dx + (y - y0) * dy) / len2);
                 const float ex = x - (x0 + t * dx), ey = y - (y0 + t * dy);
                 const float cov = clamp01((hw - std::sqrt(ex * ex + ey * ey)) * s + 0.5f);
                 return cov > 0.f ? toArgb(c, a * cov) : 0u;
             });
}

void sdRoundRectDir(float x, float y, float hx, float hy, float r, float& ux, float& uy) {
    const float e = 0.05f;
    ux = sdRoundRect(x + e, y, hx, hy, r) - sdRoundRect(x - e, y, hx, hy, r);
    uy = sdRoundRect(x, y + e, hx, hy, r) - sdRoundRect(x, y - e, hx, hy, r);
    const float l = std::sqrt(ux * ux + uy * uy);
    if (l > 1e-6f) { ux /= l; uy /= l; } else { ux = 0.f; uy = 0.f; }
}

// --- Lettering ---------------------------------------------------------------

static const AtlasGlyph& glyphFor(const AtlasFont& font, char c) {
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    if (c < font.first || c > font.last) c = ' ';
    return font.glyphs[c - font.first];
}

float textWidth(const AtlasFont& font, const char* text, float capHeight) {
    if (!text) return 0.f;
    float w = 0.f;
    for (const char* p = text; *p; ++p) w += glyphFor(font, *p).advance16 / 16.f;
    return w * capHeight / font.capHeight;
}

static int glyphAlpha(const AtlasFont& font, const AtlasGlyph& gl, int x, int y) {
    if (x < 0 || y < 0 || x >= gl.w || y >= gl.h) return 0;
    const int i = y * gl.w + x;
    const uint8_t byte = font.alpha[gl.offset + i / 2];
    return (i & 1) ? (byte & 0x0F) : (byte >> 4);
}

namespace {

// A glyph resampled to one size, in buffer pixels: 8-bit coverage.
struct ScaledGlyph {
    int dx{0}, dy{0}, w{0}, h{0};
    std::vector<uint8_t> cov;
};

// Resampling is the costly part of drawing text, and a GUI draws the same
// few sizes over and over: keep each size's glyphs. Shared by every GUI
// instance in the process (hence the lock); a size's set is never freed.
struct GlyphSet {
    const AtlasFont* font;
    float k;
    ScaledGlyph glyphs[128];
};

const GlyphSet& scaledGlyphs(const AtlasFont& font, float k) {
    static std::mutex lock;
    static std::vector<std::unique_ptr<GlyphSet>> sets;
    std::lock_guard<std::mutex> guard(lock);
    for (const auto& set : sets) {
        if (set->font == &font && std::fabs(set->k - k) < 1e-4f) return *set;
    }
    auto set = std::make_unique<GlyphSet>();
    set->font = &font;
    set->k = k;
    for (int c = font.first; c <= font.last; ++c) {
        const AtlasGlyph& gl = font.glyphs[c - font.first];
        ScaledGlyph& sg = set->glyphs[c];
        if (!gl.w || !gl.h) continue;
        // Placed at whole buffer pixels; the 2x2 bilinear taps keep it
        // smooth at any scale.
        sg.dx = static_cast<int>(std::round(gl.dx * k));
        sg.dy = static_cast<int>(std::round(gl.dy * k));
        sg.w = static_cast<int>(std::ceil(gl.w * k)) + 1;
        sg.h = static_cast<int>(std::ceil(gl.h * k)) + 1;
        sg.cov.assign(static_cast<size_t>(sg.w) * sg.h, 0);
        for (int y = 0; y < sg.h; ++y) {
            for (int x = 0; x < sg.w; ++x) {
                float sum = 0.f;
                for (int sy = 0; sy < 2; ++sy) {
                    for (int sx = 0; sx < 2; ++sx) {
                        const float u = (x + 0.25f + 0.5f * sx) / k - 0.5f;
                        const float v = (y + 0.25f + 0.5f * sy) / k - 0.5f;
                        const int u0 = static_cast<int>(std::floor(u)), v0 = static_cast<int>(std::floor(v));
                        const float fu = u - u0, fv = v - v0;
                        const float t0 = glyphAlpha(font, gl, u0, v0) * (1 - fu) + glyphAlpha(font, gl, u0 + 1, v0) * fu;
                        const float t1 = glyphAlpha(font, gl, u0, v0 + 1) * (1 - fu) + glyphAlpha(font, gl, u0 + 1, v0 + 1) * fu;
                        sum += t0 * (1 - fv) + t1 * fv;
                    }
                }
                sg.cov[static_cast<size_t>(y) * sg.w + x] = static_cast<uint8_t>(std::min(255.f, sum / 60.f * 255.f + 0.5f));
            }
        }
    }
    sets.push_back(std::move(set));
    return *sets.back();
}

} // namespace

void drawText(Graphics& g, const AtlasFont& font, const char* text, float x, float capTop,
              float capHeight, uint32_t argb, float wear) {
    if (!text) return;
    const int s = g.getScale();
    const float k = capHeight * s / font.capHeight;   // buffer pixels per glyph pixel
    const bool native = std::fabs(k - 1.f) < 1e-3f;
    const GlyphSet* set = native ? nullptr : &scaledGlyphs(font, k);
    const float a = alphaOf(argb);
    const Color c = fromArgb(argb);
    float penX = std::round(x * s);
    const float penY = std::round(capTop * s - font.capTop * k);
    for (const char* p = text; *p; ++p) {
        const AtlasGlyph& gl = glyphFor(font, *p);
        const char ch = static_cast<char>(&gl - font.glyphs + font.first);
        if (native) {
            const int x0 = static_cast<int>(penX) + gl.dx, y0 = static_cast<int>(penY) + gl.dy;
            for (int y = 0; y < gl.h; ++y) {
                for (int xx = 0; xx < gl.w; ++xx) {
                    const int nib = glyphAlpha(font, gl, xx, y);
                    if (!nib) continue;
                    float ink = a * nib / 15.f;
                    if (wear > 0.f) ink *= 1.f - wear * valueNoise((x0 + xx) * 0.35f, (y0 + y) * 0.35f, 77);
                    g.blendPixel(x0 + xx, y0 + y, toArgb(c, ink));
                }
            }
        } else {
            const ScaledGlyph& sg = set->glyphs[static_cast<unsigned char>(ch)];
            const int x0 = static_cast<int>(penX) + sg.dx, y0 = static_cast<int>(penY) + sg.dy;
            for (int y = 0; y < sg.h; ++y) {
                for (int xx = 0; xx < sg.w; ++xx) {
                    const uint8_t cv = sg.cov[static_cast<size_t>(y) * sg.w + xx];
                    if (!cv) continue;
                    float ink = a * cv / 255.f;
                    if (wear > 0.f) ink *= 1.f - wear * valueNoise((x0 + xx) * 0.35f, (y0 + y) * 0.35f, 77);
                    g.blendPixel(x0 + xx, y0 + y, toArgb(c, ink));
                }
            }
        }
        penX += gl.advance16 / 16.f * k;
    }
}

// --- Panel -------------------------------------------------------------------

void paintWornPanel(Graphics& g, int width, int height, bool trims) {
    const int s = g.getScale();
    const int bw = width * s, bh = height * s;
    uint32_t* buf = g.getBuffer();
    const Color grimeTint{ 0.60f, 0.56f, 0.47f };
    // The smooth fields (grime, stains, rubbed paint, vignette) are worked
    // out once per window pixel; only the grain varies per buffer pixel.
    for (int ly = 0; ly < height; ++ly) {
        const float y = ly + 0.5f;
        for (int lx = 0; lx < width; ++lx) {
            const float x = lx + 0.5f;
            Color c = kPaint;
            float shade = 1.f;
            // Big soft patches of grime, heavier towards the edges.
            const float edge = std::min(std::min(y, height - y) / 40.f, 1.f);
            const float grime = smoothstep(0.45f, 0.85f, fbm(x / 70.f, y / 45.f, 9)) * 0.55f
                                + (1.f - edge) * 0.35f;
            c = mix(c, grimeTint, 0.24f * grime);
            // Old stains: a few faint brownish blotches with darker rims,
            // where something was spilled and dried.
            const float stain = fbm(x / 34.f, y / 30.f, 13);
            const float blot = smoothstep(0.70f, 0.73f, stain) * (1.f - 0.5f * smoothstep(0.73f, 0.78f, stain));
            c = mix(c, Color{ 0.52f, 0.47f, 0.38f }, 0.12f * blot);
            // Paint rubbed thin by hands along the panel's edges and in a
            // few worn patches: brighter, smoother metal shows through.
            const float rubBand = 1.f - smoothstep(14.f, 24.f, std::min(y, height - y));
            const float rub = smoothstep(0.55f, 0.75f, fbm(x / 9.f, y / 30.f, 17)) * (0.35f + 0.65f * rubBand);
            c = mix(c, Color{ 0.86f, 0.87f, 0.86f }, 0.30f * rub);
            // Gentle vignette.
            const float vx = (x / width - 0.5f) * 2.f, vy = (y / height - 0.5f) * 2.f;
            shade *= 1.f - 0.06f * (vx * vx * 0.5f + vy * vy);
            const bool trim = trims && (y < 12.f || y > height - 13.f);
            if (trim) c = { 0.66f, 0.67f, 0.665f };
            for (int sy = 0; sy < s; ++sy) {
                const int by = ly * s + sy;
                for (int sx = 0; sx < s; ++sx) {
                    const int bx = lx * s + sx;
                    float sh;
                    if (trim) {
                        // Top and bottom extrusions: a darker, rounded metal trim.
                        const float yy = (by + 0.5f) / s;
                        const float t = yy < 12.f ? yy / 12.f : (height - yy) / 13.f;
                        sh = (0.82f + 0.22f * std::sin(t * kPi * 0.9f + 0.2f))
                             * (1.f + 0.03f * (valueNoise(x * 0.01f, by * 0.7f, 4) - 0.5f));
                    } else {
                        // Brushed grain runs along the panel.
                        sh = shade + 0.028f * (valueNoise((bx + 0.5f) / s * 0.015f, by * 0.9f, 3) - 0.5f)
                             + 0.018f * (hash2(bx, by, 5) - 0.5f);
                    }
                    buf[static_cast<size_t>(by) * bw + bx] = toArgb(scale(c, sh), 1.f);
                }
            }
        }
    }
    const float w = static_cast<float>(width);
    if (trims) {
        // Trim edges: a dark seam and a lit lip where the trim meets the panel.
        drawLineAA(g, 0, 12.f, w, 12.f, 1.f, 0xC0303234);
        drawLineAA(g, 0, 13.f, w, 13.f, 1.f, 0xA0FFFFFF);
        drawLineAA(g, 0, height - 14.f, w, height - 14.f, 1.f, 0x80FFFFFF);
        drawLineAA(g, 0, height - 13.f, w, height - 13.f, 1.f, 0xC0303234);
        drawLineAA(g, 0, 0.5f, w, 0.5f, 1.f, 0x70FFFFFF);
    }

    // Light scratches and scuffs from years of use, about one per 100 px
    // of panel width.
    uint32_t seed = 0xAC1D;
    auto rnd = [&seed]() { seed = hashU(seed + 0x9E3779B9u); return (seed & 0xFFFFFF) / 16777216.f; };
    const float margin = trims ? 16.f : 4.f;
    const int scratches = static_cast<int>(width * height / 2200);
    for (int i = 0; i < scratches; ++i) {
        const float x = rnd() * w, y = margin + rnd() * (height - 2 * margin);
        const float len = 3.f + 22.f * rnd() * rnd();
        const float ang = (rnd() - 0.5f) * 0.9f + (rnd() < 0.3f ? kPi / 2 : 0.f);
        const uint32_t col = rnd() < 0.7f ? 0x22FFFFFFu : 0x14000000u;
        drawLineAA(g, x, y, x + len * std::cos(ang), y + len * std::sin(ang), 0.35f, col);
    }

    // Small chips where the paint has flaked off down to the dark primer,
    // with a lit lower-right edge where the paint layer stands proud.
    const int chips = static_cast<int>(width * height / 5500);
    for (int i = 0; i < chips; ++i) {
        const float x = 4.f + rnd() * (w - 8.f), y = margin - 1.f + rnd() * (height - 2 * margin);
        const float r = 0.5f + 1.6f * rnd() * rnd();
        const uint32_t chipSeed = 100 + i;
        shadeBox(g, x - r - 2, y - r - 2, x + r + 2, y + r + 2, [&](float px, float py) {
            const float dx = px - x, dy = py - y;
            const float wobble = 1.f + 0.45f * (valueNoise(px * 1.3f, py * 1.3f, chipSeed) - 0.5f);
            const float d = std::sqrt(dx * dx + dy * dy) / (r * wobble);
            if (d < 1.f) return toArgb({ 0.42f, 0.42f, 0.41f }, 0.85f * smoothstep(1.f, 0.8f, d));
            if (d < 1.35f && dx + dy > 0.f) return toArgb({ 1.f, 1.f, 1.f }, 0.35f * (1.35f - d) / 0.35f);
            return 0u;
        });
    }
}

} // namespace modern
} // namespace tannhauser
