// tearwash_dsp_test: checks of the reverb engine (docs/02_ENGINE.md §6): decay law,
// sample-rate independence, hold, and every program finite and bounded at extreme settings.

#include "tearwash/engine/Reverb.hpp"
#include "tearwash/analysis/Metrics.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace tearwash;

static int g_fail = 0, g_pass = 0;
#define CHECK(cond, ...) do { if (cond) { ++g_pass; } else { ++g_fail; std::printf("FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

// Impulse response of output A for a program and controls.
static std::vector<double> ir(int prog, const Controls& c, double fs, double seconds) {
    static Reverb r;
    r.setSampleRate(fs);
    r.setProgram(prog);
    r.setControls(c);
    r.reset();
    const size_t n = static_cast<size_t>(fs * seconds);
    std::vector<float> in(n, 0.f), a(n), b(n), cc(n), d(n);
    in[0] = 0.5f;
    for (size_t i = 0; i < n; i += 512) {
        const int k = static_cast<int>(std::min<size_t>(512, n - i));
        float* o[4] = { a.data() + i, b.data() + i, cc.data() + i, d.data() + i };
        r.process(in.data() + i, in.data() + i, o, k);
    }
    return std::vector<double>(a.begin(), a.end());
}

static void testReverb() {
    // Decay law: the mid band follows DECAY (LOW DECAY = DECAY, damping open, no modulation).
    for (int prog : { 0, 3, 5 })
        for (double mid : { 0.35, 0.55 }) {
            Controls c = Reverb::programDefaults(prog);
            c.lowDecay = c.decay = mid; c.damping = 1.0; c.modulation = false; c.predelay = 0.0;
            const double rt = law::decaySeconds(mid);
            const auto h = ir(prog, c, 48000.0, rt * 2.5 + 1.0);
            const double got = schroeder(bandpass(h, 707, 1414, 48000.0), 0, 48000.0).rt();
            CHECK(std::fabs(got / rt - 1.0) < 0.15, "%s: RT %.2f s for %.2f s", Reverb::programName(prog), got, rt);
        }
    // Sample-rate independence: the same decay at 44.1 and 96 kHz.
    {
        Controls c = Reverb::programDefaults(0);
        c.modulation = false;
        const double a = schroeder(bandpass(ir(0, c, 44100.0, 9.0), 707, 1414, 44100.0), 0, 44100.0).rt();
        const double b = schroeder(bandpass(ir(0, c, 96000.0, 9.0), 707, 1414, 96000.0), 0, 96000.0).rt();
        CHECK(std::fabs(a / b - 1.0) < 0.05, "RT at 44.1 kHz %.2f s vs 96 kHz %.2f s", a, b);
    }
    // Hold: an impulse decays at DECAY 0.6 for 1 s, then DECAY goes to the top: the tail stays (energy
    // 8–9 s after the impulse within 2 dB of 1.5–2.5 s) and new input is muted.
    {
        static Reverb r;
        r.setSampleRate(48000.0);
        r.setProgram(0);
        Controls c = Reverb::programDefaults(0);
        c.decay = 0.6;
        r.setControls(c);
        r.reset();
        const size_t n = 9 * 48000;
        std::vector<float> in(n, 0.f), a(n), b(n), cc(n), d(n);
        in[0] = 0.5f;
        for (size_t i = 6 * 48000; i < 6 * 48000 + 4800; ++i) in[i] = 0.5f;   // muted by the hold
        for (size_t i = 0; i < n; i += 480) {
            if (i == 48000) { c.decay = 1.0; r.setControls(c); }
            float* o[4] = { a.data() + i, b.data() + i, cc.data() + i, d.data() + i };
            r.process(in.data() + i, in.data() + i, o, 480);
        }
        auto energy = [&](double t0, double t1) {
            double e = 0;
            for (size_t i = size_t(t0 * 48000); i < size_t(t1 * 48000); ++i) e += double(a[i]) * a[i];
            return e;
        };
        const double drop = 10.0 * std::log10(energy(8.0, 9.0) / energy(1.5, 2.5));
        CHECK(drop > -2.0 && drop < 1.0, "hold: tail changes %.1f dB in 6.5 s", drop);
    }
    // Every program, extreme settings, loud noise: finite and bounded.
    for (int prog = 0; prog < Reverb::kPrograms; ++prog)
        for (int k = 0; k < 3; ++k) {
            Controls c;
            const double v = k == 0 ? 0.0 : (k == 1 ? 1.0 : 0.85);
            c.lowDecay = c.decay = v; c.crossover = c.damping = c.distance = c.predelay = c.diffusion = v;
            c.modRate = c.size = c.clarity = c.highCut = v; c.modulation = k != 0;
            static Reverb r;
            r.setSampleRate(48000.0);
            r.setProgram(prog);
            r.setControls(c);
            r.reset();
            uint32_t x = 7u + prog;
            bool finite = true;
            double peak = 0.0;
            float in[2][256], o[4][256];
            float* op[4] = { o[0], o[1], o[2], o[3] };
            for (int blk = 0; blk < 48000 * 4 / 256; ++blk) {
                for (int i = 0; i < 256; ++i) {
                    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
                    in[0][i] = blk < 375 ? static_cast<float>(static_cast<int32_t>(x) / 2147483648.0) : 0.f;
                    in[1][i] = -in[0][i];
                }
                r.process(in[0], in[1], op, 256);
                for (int ch = 0; ch < 4; ++ch)
                    for (int i = 0; i < 256; ++i) { finite = finite && std::isfinite(o[ch][i]); peak = std::max(peak, double(std::fabs(o[ch][i]))); }
            }
            CHECK(finite && peak < 8.0, "%s extremes %d: finite %d, peak %.2f", Reverb::programName(prog), k, finite, peak);
        }
}

int main() {
    testReverb();
    std::printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
