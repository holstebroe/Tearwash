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

// Energy of x at frequency f (Goertzel), per sample.
static double goertzel(const std::vector<float>& x, size_t a, size_t b, double f, double fs) {
    const double w = 2.0 * M_PI * f / fs, c = 2.0 * std::cos(w);
    double s1 = 0, s2 = 0;
    for (size_t i = a; i < b; ++i) { const double s0 = x[i] + c * s1 - s2; s2 = s1; s1 = s0; }
    const double re = s1 - s2 * std::cos(w), im = s2 * std::sin(w);
    return (re * re + im * im) / double((b - a) * (b - a));
}

// Shimmer (docs/02_ENGINE.md §7): off is the old reverb bit for bit, a +12 interval puts energy an octave above the
// input, and the feedback stays finite and bounded at decay 60 s, amount 1, for every interval.
static void testShimmer() {
    constexpr double fs = 48000.0;
    auto run = [&](int prog, Controls c, double seconds, auto input, std::vector<float>* outA) {
        static Reverb r;
        r.setSampleRate(fs);
        r.setProgram(prog);
        r.setControls(c);
        r.reset();
        const size_t n = static_cast<size_t>(fs * seconds);
        std::vector<float> in(n), a(n), b(n), cc(n), d(n);
        for (size_t i = 0; i < n; ++i) in[i] = input(i);
        bool ok = true;
        double peak = 0.0;
        for (size_t i = 0; i < n; i += 480) {
            const int k = static_cast<int>(std::min<size_t>(480, n - i));
            float* o[4] = { a.data() + i, b.data() + i, cc.data() + i, d.data() + i };
            r.process(in.data() + i, in.data() + i, o, k);
            for (int j = 0; j < k; ++j)
                for (int ch = 0; ch < 4; ++ch) { ok = ok && std::isfinite(o[ch][j]); peak = std::max(peak, double(std::fabs(o[ch][j]))); }
        }
        if (outA) *outA = a;
        return std::make_pair(ok, peak);
    };
    const auto sine = [&](size_t i) { return 0.3f * static_cast<float>(std::sin(2.0 * M_PI * 440.0 * double(i) / fs)); };
    // Mode off (or amount 0) changes nothing.
    {
        Controls c = Reverb::programDefaults(0);
        std::vector<float> ref, off, zero;
        run(0, c, 1.0, sine, &ref);
        c.shimmer = 1.0; c.shimmerMode = false;
        run(0, c, 1.0, sine, &off);
        c.shimmer = 0.0; c.shimmerMode = true;
        run(0, c, 1.0, sine, &zero);
        CHECK(ref == off && ref == zero, "shimmer off must be bit-identical to the plain reverb");
    }
    // Pitch: +12 and +7 put energy at the shifted frequency; the unshifted reverb has none there.
    for (int st : { 12, 7, -12 }) {
        Controls c = Reverb::programDefaults(0);
        c.shimmerMode = true; c.shimmer = 0.8; c.shimmerInterval = st;
        std::vector<float> a, base;
        run(0, c, 4.0, sine, &a);
        c.shimmerMode = false;
        run(0, c, 4.0, sine, &base);
        const double f = 440.0 * law::shimmerRatio(st);
        const size_t t0 = size_t(1.5 * fs), t1 = size_t(4.0 * fs);
        const double gain = 10.0 * std::log10(goertzel(a, t0, t1, f, fs) / (goertzel(base, t0, t1, f, fs) + 1e-30));
        std::printf("shimmer %+d: %.1f dB at %.0f Hz\n", st, gain, f);
        CHECK(gain > 15.0, "shimmer %+d: only %.1f dB more energy at %.0f Hz", st, gain, f);
    }
    // Stability: noise burst, then 60 s of tail at decay 60 s (just below hold), amount 1, each interval.
    uint32_t x = 12345u;
    const auto noise = [&](size_t i) {
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        return i < size_t(fs) ? static_cast<float>(static_cast<int32_t>(x) / 2147483648.0) : 0.f;
    };
    for (int prog : { 0, 5, 9 })
        for (int st : { -12, -7, 5, 7, 12, 19, 24 }) {
            Controls c = Reverb::programDefaults(prog);
            c.lowDecay = 1.0; c.decay = 0.99; c.damping = 1.0; c.highCut = 1.0;
            c.shimmerMode = true; c.shimmer = 1.0; c.shimmerInterval = st;
            const auto res = run(prog, c, 60.0, noise, nullptr);
            std::printf("%s shimmer %+d: peak %.2f\n", Reverb::programName(prog), st, res.second);
            CHECK(res.first && res.second < 8.0, "%s shimmer %+d at decay 60 s: finite %d, peak %.2f", Reverb::programName(prog), st, res.first, res.second);
        }
}

int main() {
    testReverb();
    testShimmer();
    std::printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
