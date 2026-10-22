// Golden frames of the panel (quarter-size portable pixmaps in src/tests/golden/): the editor draws shaded text and
// lighting with libm, so a frame is compared by its mean difference and the share of pixels that moved, not bit for bit.
// SPUX_UPDATE_GOLDEN=1 writes the frames; change one only when the look is meant to change, and say so in the commit.
#ifndef TEARWASH_TESTS_GOLDENFRAME_HPP
#define TEARWASH_TESTS_GOLDENFRAME_HPP

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace goldenframe {

// The mean of each 4 x 4 block (width and height multiples of 4 are cut down), as 0xAARRGGBB.
inline std::vector<uint32_t> quarter(const uint32_t* px, int w, int h) {
    std::vector<uint32_t> out(static_cast<size_t>(w / 4) * static_cast<size_t>(h / 4));
    for (int y = 0; y < h / 4; ++y)
        for (int x = 0; x < w / 4; ++x) {
            unsigned r = 0, g = 0, b = 0;
            for (int dy = 0; dy < 4; ++dy)
                for (int dx = 0; dx < 4; ++dx) {
                    const uint32_t p = px[static_cast<size_t>(y * 4 + dy) * static_cast<size_t>(w) + static_cast<size_t>(x * 4 + dx)];
                    r += (p >> 16) & 255u; g += (p >> 8) & 255u; b += p & 255u;
                }
            out[static_cast<size_t>(y) * static_cast<size_t>(w / 4) + static_cast<size_t>(x)] = 0xFF000000u | ((r / 16) << 16) | ((g / 16) << 8) | (b / 16);
        }
    return out;
}

inline bool write(const std::string& path, const std::vector<uint32_t>& px, int w, int h) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    std::fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (uint32_t p : px) {
        const unsigned char c[3] = { static_cast<unsigned char>(p >> 16), static_cast<unsigned char>(p >> 8), static_cast<unsigned char>(p) };
        std::fwrite(c, 1, 3, f);
    }
    std::fclose(f);
    return true;
}

inline bool read(const std::string& path, std::vector<uint32_t>& px, int& w, int& h) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    int maxv = 0;
    bool ok = std::fscanf(f, "P6 %d %d %d", &w, &h, &maxv) == 3 && std::fgetc(f) != EOF;
    if (ok) {
        px.resize(static_cast<size_t>(w) * static_cast<size_t>(h));
        for (auto& p : px) {
            unsigned char c[3];
            if (std::fread(c, 1, 3, f) != 3) { ok = false; break; }
            p = 0xFF000000u | (uint32_t(c[0]) << 16) | (uint32_t(c[1]) << 8) | c[2];
        }
    }
    std::fclose(f);
    return ok;
}

struct Result { bool ok = false; double meanDiff = 0, movedShare = 0; std::string note; };

// Compares a full-size frame with <dir>/<name>.ppm (or writes it when SPUX_UPDATE_GOLDEN is set and the frame is not
// a record of an earlier look: `record`, with its own tolerances, is never rewritten).
inline Result check(const std::string& dir, const std::string& name, const uint32_t* px, int w, int h,
                    double maxMean = 1.5, double maxMoved = 0.005, bool record = false) {
    Result r;
    const std::string path = dir + "/" + name + ".ppm";
    const std::vector<uint32_t> now = quarter(px, w, h);
    const char* update = std::getenv("SPUX_UPDATE_GOLDEN");
    if (!record && update && *update && *update != '0') {
        r.ok = write(path, now, w / 4, h / 4);
        r.note = r.ok ? "written" : "cannot write " + path;
        return r;
    }
    std::vector<uint32_t> ref;
    int rw = 0, rh = 0;
    if (!read(path, ref, rw, rh)) { r.note = "no golden frame " + path + " (SPUX_UPDATE_GOLDEN=1 writes it)"; return r; }
    if (rw != w / 4 || rh != h / 4) { r.note = "golden frame has another size"; return r; }
    double sum = 0;
    size_t moved = 0;
    for (size_t i = 0; i < now.size(); ++i) {
        int worst = 0;
        for (int sh = 0; sh < 24; sh += 8) {
            const int d = std::abs(int((now[i] >> sh) & 255u) - int((ref[i] >> sh) & 255u));
            sum += d;
            worst = d > worst ? d : worst;
        }
        moved += worst > 24;
    }
    r.meanDiff = sum / (3.0 * static_cast<double>(now.size()));
    r.movedShare = static_cast<double>(moved) / static_cast<double>(now.size());
    r.ok = r.meanDiff < maxMean && r.movedShare < maxMoved;
    return r;
}

} // namespace goldenframe

#endif
