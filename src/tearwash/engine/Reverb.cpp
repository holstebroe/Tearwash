#include "Reverb.hpp"

#include <algorithm>

namespace tearwash {

namespace {

constexpr double kPi = 3.14159265358979323846;

// The programs [A]. Delays in ms at SIZE ×1: four input diffusers, then per tank half the
// modulated allpass, the first delay, the two decay diffusers and the delay into the other half.
// Hall set (02 §3): input diffusers a geometric series 5.3·1.55^k ms; tank halves of ≈ 190 and
// 176 ms (the mean free path of a large hall) split 20/45/5/8/22 % and 17/40/6/9/28 % over
// {ap, d1, dif1, dif2, d2}, every length snapped to a prime number of samples at 48 kHz, so no
// two lines share a period and L and R decorrelate. Plate set: Dattorro's plate (J. Dattorro,
// "Effect Design Part 1", JAES 1997) for the input diffusers and the tank's allpass and delay
// lengths, with shorter decay diffusers of our own.
struct Space {
    const char* name;
    int kind;            // 0 tank, 1 ensemble
    double scale;        // the program's own size factor on the delay set
    int set;             // 0 hall set, 1 plate set
    double modMs;        // modulation excursion of the tank allpasses
    double inGain;       // diffused input into the tank
    Controls def;
};

const double kSet[2][4 + 10] = {
    // in[4], half 0 {ap, d1, dif1, dif2, d2}, half 1 {…}
    { 5.229, 8.271, 12.771, 19.729, 37.979, 85.396, 9.521, 15.146, 41.729, 29.854, 70.271, 10.604, 15.854, 49.396 },
    { 4.77, 3.59, 12.7, 9.31, 22.6, 149.6, 8.93, 6.29, 125.0, 30.5, 141.7, 7.13, 5.41, 106.3 },
};

Controls def(double lowDecay, double decay, double crossover, double damping, double distance, double pre,
             double dif, double modRate, double size, double clarity, double highCut) {
    Controls c;
    c.lowDecay = lowDecay; c.decay = decay; c.crossover = crossover; c.damping = damping; c.distance = distance;
    c.predelay = pre; c.diffusion = dif; c.modRate = modRate; c.size = size; c.clarity = clarity; c.highCut = highCut;
    return c;
}

// decay v: 0.38 ≈ 1.8 s, 0.5 ≈ 3.5 s, 0.6 ≈ 6.1 s, 0.7 ≈ 10.9 s, 0.8 ≈ 19 s.
const Space kSpaces[Reverb::kPrograms] = {
    { "GRAND HALL", 0, 1.00, 0, 0.45, 0.55, def(0.58, 0.50, 0.30, 0.70, 0.40, 0.10, 0.55, 0.30, 0.50, 0.25, 0.85) },
    { "BRIGHT HALL", 0, 0.92, 0, 0.40, 0.55, def(0.50, 0.50, 0.40, 0.92, 0.40, 0.12, 0.60, 0.35, 0.50, 0.20, 0.97) },
    { "DARK HALL", 0, 1.10, 0, 0.50, 0.55, def(0.64, 0.52, 0.25, 0.52, 0.45, 0.14, 0.55, 0.25, 0.50, 0.25, 0.72) },
    { "ROOM", 0, 0.38, 0, 0.25, 0.60, def(0.30, 0.28, 0.35, 0.75, 0.25, 0.04, 0.65, 0.30, 0.50, 0.15, 0.90) },
    { "CHAMBER", 0, 0.55, 0, 0.30, 0.60, def(0.42, 0.38, 0.35, 0.72, 0.35, 0.06, 0.70, 0.25, 0.50, 0.05, 0.88) },
    { "PLATE", 0, 1.00, 1, 0.35, 0.60, def(0.42, 0.42, 0.40, 0.85, 0.20, 0.00, 0.75, 0.35, 0.50, 0.00, 0.95) },
    { "VOCAL PLATE", 0, 0.82, 1, 0.30, 0.60, def(0.36, 0.38, 0.40, 0.70, 0.25, 0.08, 0.70, 0.30, 0.50, 0.05, 0.85) },
    { "DENSE PLATE", 0, 1.20, 1, 0.55, 0.55, def(0.50, 0.50, 0.40, 0.80, 0.10, 0.02, 0.90, 0.40, 0.50, 0.00, 0.95) },
    { "ENSEMBLE", 1, 1.00, 0, 3.00, 1.00, def(0.30, 0.25, 0.40, 0.80, 0.50, 0.00, 0.40, 0.35, 0.50, 0.20, 0.95) },
    { "VAST SPACE", 0, 2.40, 0, 1.20, 0.55, def(0.78, 0.74, 0.30, 0.62, 0.70, 0.18, 0.60, 0.25, 0.50, 0.20, 0.80) },
};

// One-pole low-pass coefficient for a corner at f (Hz); 1 = open.
double onePole(double f, double fs) {
    if (f >= 0.45 * fs) return 1.0;
    return 1.0 - std::exp(-2.0 * kPi * f / fs);
}

} // namespace

const char* Reverb::programName(int i) { return kSpaces[i < 0 ? 0 : (i >= kPrograms ? kPrograms - 1 : i)].name; }
Controls Reverb::programDefaults(int i) { return kSpaces[i < 0 ? 0 : (i >= kPrograms ? kPrograms - 1 : i)].def; }

// ---- Delay line --------------------------------------------------------------------------------

void Reverb::Delay::init(double maxSamples) {
    uint32_t n = 16;
    while (n < maxSamples + 8) n <<= 1;
    buf.assign(n, 0.f);
    mask = n - 1;
    pos = 0;
}

void Reverb::Delay::clear() { std::fill(buf.begin(), buf.end(), 0.f); }

inline float Reverb::Delay::lin(double d) const {
    const double p = static_cast<double>(pos) - d;
    const double f = std::floor(p);
    const uint32_t i = static_cast<uint32_t>(static_cast<int64_t>(f));
    const float t = static_cast<float>(p - f);
    const float a = buf[i & mask], b = buf[(i + 1) & mask];
    return a + t * (b - a);
}

inline float Reverb::Delay::cub(double d) const {
    const double p = static_cast<double>(pos) - d;
    const double f = std::floor(p);
    const uint32_t i = static_cast<uint32_t>(static_cast<int64_t>(f));
    const float t = static_cast<float>(p - f);
    const float y0 = buf[(i - 1) & mask], y1 = buf[i & mask], y2 = buf[(i + 1) & mask], y3 = buf[(i + 2) & mask];
    const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * t + c2) * t + c1) * t + y1;
}

inline double Reverb::Wander::run(double periodSamples, double smooth) {
    if ((phase += 1.0) >= periodSamples) {
        phase -= periodSamples;
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        target = (static_cast<double>(rng) / 4294967295.0) * 2.0 - 1.0;
    }
    s1 += smooth * (target - s1);
    s2 += smooth * (s1 - s2);
    return s2;
}

// ---- Setup -------------------------------------------------------------------------------------

void Reverb::setSampleRate(double fs) {
    fs_ = fs;
    const double ms = fs / 1000.0;
    // Each line holds its longest length over the programs at SIZE ×2, plus the allpasses'
    // modulation (at most 2 × 1.8 ms); predelay 1 s.
    double inMax = 0.0, tankMax[5] = {};
    for (const Space& s : kSpaces) {
        const double sc = s.scale * law::sizeScale(1.0) * ms;
        for (int k = 0; k < 4; ++k) inMax = std::max(inMax, kSet[s.set][k] * sc);
        for (int h = 0; h < 2; ++h)
            for (int k = 0; k < 5; ++k) tankMax[k] = std::max(tankMax[k], kSet[s.set][4 + 5 * h + k] * sc);
    }
    preL_.init(1000.0 * ms + 4); preR_.init(1000.0 * ms + 4);
    for (auto& ch : inAp_) for (auto& d : ch) d.init(inMax * 1.04 + 4);   // R runs 3.1 % longer
    for (auto& h : h_) {
        h.ap.init(tankMax[0] + 4.0 * ms);
        h.d1.init(tankMax[1] + 4);
        h.dif1.init(tankMax[2] + 4);
        h.dif2.init(tankMax[3] + 4);
        h.d2.init(tankMax[4] + 4);
    }
    for (auto& v : voice_) v.init(60.0 * 2.0 * ms + 8.0 * ms);
    allocated_ = true;
    slew_ = 1.0 - std::exp(-1.0 / (0.25 * fs));   // lengths glide over ≈ 0.25 s
    for (int k = 0; k < 8; ++k) vWander_[k].rng = 0x9E3779B9u * static_cast<uint32_t>(k + 1);
    h_[0].wander.rng = 0x51A7u; h_[1].wander.rng = 0x7A11u;
    configure();
    reset();
}

void Reverb::setProgram(int i) {
    prog_ = i < 0 ? 0 : (i >= kPrograms ? kPrograms - 1 : i);
    configure();
    reset();
}

void Reverb::setControls(const Controls& c) {
    c_ = c;
    configure();
}

void Reverb::reset() {
    if (!allocated_) return;
    preL_.clear(); preR_.clear();
    for (auto& ch : inAp_) for (auto& d : ch) d.clear();
    for (auto& h : h_) {
        h.ap.clear(); h.d1.clear(); h.dif1.clear(); h.dif2.clear(); h.d2.clear();
        h.xo.y = h.damp.y = 0.0;
        h.apBase = tgt_[&h - h_][0]; h.d1Len = tgt_[&h - h_][1]; h.dif1Len = tgt_[&h - h_][2];
        h.dif2Len = tgt_[&h - h_][3]; h.d2Len = tgt_[&h - h_][4];
    }
    for (auto& v : voice_) v.clear();
    bwL_.y = bwR_.y = 0.0;
    preLen_ = preTarget_;
    for (int k = 0; k < 4; ++k) inLen_[k] = inTarget_[k];
    fb_[0] = fb_[1] = 0.0;
    inGain_ = inGainTarget_;
}

// Targets from the program and the controls; gains from the target lengths.
void Reverb::configure() {
    const Space& s = kSpaces[prog_];
    const double ms = fs_ / 1000.0;
    const double sc = s.scale * law::sizeScale(c_.size);
    const double* set = kSet[s.set];
    // Whole samples at rest, so the loop's static delays are exact (only glides interpolate).
    for (int k = 0; k < 4; ++k) inTarget_[k] = std::max(1.0, std::round(set[k] * sc * ms));
    for (int h = 0; h < 2; ++h)
        for (int k = 0; k < 5; ++k) tgt_[h][k] = std::max(2.0, std::round(set[4 + 5 * h + k] * sc * ms));
    preTarget_ = std::max(1.0, law::predelayMs(c_.predelay) * ms);
    gIn_ = 0.75 * c_.diffusion;
    gDif_ = 0.70 * c_.diffusion;
    gClar_ = 0.70 - 0.55 * c_.clarity;
    modDepth_ = c_.modulation ? s.modMs * ms * (0.5 + c_.modRate) : 0.0;
    modPeriod_ = fs_ / law::modRateHz(c_.modRate);
    wLate_ = 0.35 + 0.65 * c_.distance;
    wEarly_ = 1.0 - c_.distance;
    hold_ = c_.decay >= law::kHold;
    inGainTarget_ = hold_ ? 0.0 : s.inGain;
    const double bw = onePole(c_.highCut >= 0.995 ? 1e9 : law::highCutHz(c_.highCut), fs_);
    bwL_.a = bwR_.a = bw;
    const double xo = onePole(law::crossoverHz(c_.crossover), fs_);
    const double damp = hold_ || c_.damping >= 0.995 ? 1.0 : onePole(law::dampingHz(c_.damping), fs_);
    const double rtLow = law::decaySeconds(c_.lowDecay), rtMid = law::decaySeconds(c_.decay);
    for (int h = 0; h < 2; ++h) {
        // Seconds per half, with the energy the allpasses hold (measured: the hall set decays ≈ 8 %
        // longer than its plain delay sum predicts).
        const double t = (tgt_[h][0] + tgt_[h][1] + tgt_[h][2] + tgt_[h][3] + tgt_[h][4]) / fs_ * (s.set == 0 ? 1.08 : 1.01);
        h_[h].gMid = hold_ ? 1.0 : std::pow(10.0, -3.0 * t / rtMid);
        h_[h].gLow = hold_ ? 1.0 : std::pow(10.0, -3.0 * t / rtLow);
        h_[h].xo.a = xo;
        h_[h].damp.a = damp;
    }
    // Ensemble: eight voices 7–38 ms (× SIZE), panned across; DECAY sets the echo feedback.
    static const double kBase[8] = { 7.1, 11.3, 15.7, 19.9, 23.3, 27.9, 33.1, 37.7 };
    static const double kPan[8] = { -0.9, 0.7, -0.5, 0.3, -0.2, 0.5, -0.7, 0.9 };
    for (int k = 0; k < 8; ++k) { vBase_[k] = kBase[k] * law::sizeScale(c_.size) * ms; vPan_[k] = kPan[k]; }
    fbGain_ = hold_ ? 0.0 : 0.85 * std::min(1.0, c_.decay * 1.1);
}

// ---- Processing ----------------------------------------------------------------------------------

namespace {
// A safety limit inside the loops; inaudible at normal levels (the tank is lossless only in hold).
// Tiny values are flushed to zero (no denormals, and no offset for a lossless hold to integrate).
inline double guard(double x) {
    if (std::fabs(x) < 1e-25) return 0.0;
    return x > 8.0 ? 8.0 : (x < -8.0 ? -8.0 : x);
}
}

void Reverb::runTank(double xl, double xr, double& oa, double& ob, double& oc, double& od) {
    // Input diffusers (per channel, two-and-two: the early taps sit between them).
    double e[2][2];
    double x[2] = { xl, xr };
    for (int ch = 0; ch < 2; ++ch) {
        double v = x[ch];
        for (int k = 0; k < 4; ++k) {
            Delay& d = inAp_[ch][k];
            const double len = ch ? std::round(inLen_[k] * 1.031) : inLen_[k];
            const double z = d.lin(len);
            const double w = v + gIn_ * z;
            v = z - gIn_ * w;
            d.push(static_cast<float>(w));
            if (k == 1) e[ch][0] = v;
            if (k == 3) e[ch][1] = v;
        }
        x[ch] = v;
    }
    // Figure-of-eight tank.
    const double cross[2] = { h_[1].d2.lin(h_[1].d2Len), h_[0].d2.lin(h_[0].d2Len) };
    for (int k = 0; k < 2; ++k) {
        Half& h = h_[k];
        double u = inGain_ * x[k] + cross[k];
        // Modulated allpass (cubic interpolation, smooth random glide).
        const double m = modDepth_ > 0.0 ? modDepth_ * h.wander.run(modPeriod_, 4.0 / modPeriod_) : 0.0;
        const double z = h.ap.cub(h.apBase + modDepth_ + m);
        const double w = u + gClar_ * z;
        u = z - gClar_ * w;
        h.ap.push(static_cast<float>(guard(w)));
        h.d1.push(static_cast<float>(u));
        double b = h.d1.lin(h.d1Len);
        // Two-band decay and damping.
        const double lo = h.xo.run(b);
        b = h.gLow * lo + h.gMid * (b - lo);
        b = guard(h.damp.run(b));
        // Decay diffusers.
        double z1 = h.dif1.lin(h.dif1Len), w1 = b + gDif_ * z1;
        b = z1 - gDif_ * w1;
        h.dif1.push(static_cast<float>(w1));
        double z2 = h.dif2.lin(h.dif2Len), w2 = b + gDif_ * z2;
        b = z2 - gDif_ * w2;
        h.dif2.push(static_cast<float>(w2));
        h.d2.push(static_cast<float>(guard(b)));
    }
    // Output taps (fractions of the tank delays; signs alternate so the taps decorrelate).
    auto t = [&](const Delay& d, double len, double f) { return static_cast<double>(d.lin(std::max(1.0, len * f))); };
    const Half& a = h_[0];
    const Half& c = h_[1];
    const double lateA = t(c.d1, c.d1Len, 0.12) + t(c.d1, c.d1Len, 0.71) - t(c.dif2, c.dif2Len, 0.63) + t(c.d2, c.d2Len, 0.41)
                         - t(a.d1, a.d1Len, 0.47) - t(a.dif2, a.dif2Len, 0.23) - t(a.d2, a.d2Len, 0.29);
    const double lateC = t(a.d1, a.d1Len, 0.17) + t(a.d1, a.d1Len, 0.79) - t(a.dif2, a.dif2Len, 0.55) + t(a.d2, a.d2Len, 0.37)
                         - t(c.d1, c.d1Len, 0.53) - t(c.dif2, c.dif2Len, 0.19) - t(c.d2, c.d2Len, 0.31);
    const double lateB = t(c.d1, c.d1Len, 0.33) - t(c.d2, c.d2Len, 0.83) + t(a.d1, a.d1Len, 0.91) - t(a.d2, a.d2Len, 0.61)
                         + t(a.dif1, a.dif1Len, 0.5);
    const double lateD = t(a.d1, a.d1Len, 0.37) - t(a.d2, a.d2Len, 0.77) + t(c.d1, c.d1Len, 0.89) - t(c.d2, c.d2Len, 0.67)
                         + t(c.dif1, c.dif1Len, 0.5);
    const double gl = 0.60 * wLate_, ge = 0.90 * wEarly_ * inGain_;   // wet ≈ −9 dB of dry
    oa = gl * lateA + ge * (0.7 * e[0][0] + 0.5 * e[0][1]);
    oc = gl * lateC + ge * (0.7 * e[1][0] + 0.5 * e[1][1]);
    ob = gl * lateB + ge * 0.6 * e[0][1];
    od = gl * lateD + ge * 0.6 * e[1][1];
}

void Reverb::runEnsemble(double xl, double xr, double& oa, double& ob, double& oc, double& od) {
    // Eight voices read two gliding delay lines (L and R inputs plus the echo feedback).
    voice_[0].push(static_cast<float>(inGain_ * xl + fbGain_ * fb_[0]));
    voice_[1].push(static_cast<float>(inGain_ * xr + fbGain_ * fb_[1]));
    double l = 0.0, r = 0.0;
    const double depth = modDepth_ > 0.0 ? modDepth_ : 0.0;
    for (int k = 0; k < 8; ++k) {
        const double m = depth > 0.0 ? depth * (0.5 + 0.5 * vWander_[k].run(modPeriod_ * (0.7 + 0.08 * k), 4.0 / modPeriod_)) : 0.0;
        const double y = voice_[k & 1].cub(vBase_[k] + m + 2.0);
        const double pl = 0.5 * (1.0 - vPan_[k]), pr = 0.5 * (1.0 + vPan_[k]);
        l += pl * y;
        r += pr * y;
    }
    l *= 0.35; r *= 0.35;
    // A little diffusion on the echoes (the first input diffusers of each channel).
    double v[2] = { l, r };
    for (int ch = 0; ch < 2; ++ch)
        for (int k = 0; k < 2; ++k) {
            Delay& d = inAp_[ch][k];
            const double z = d.lin(inLen_[k] * (ch ? 1.031 : 1.0));
            const double w = v[ch] + gIn_ * z;
            v[ch] = z - gIn_ * w;
            d.push(static_cast<float>(w));
        }
    fb_[0] = guard(h_[0].damp.run(v[1]));   // echoes cross over
    fb_[1] = guard(h_[1].damp.run(v[0]));
    oa = v[0]; oc = v[1];
    ob = l; od = r;
}

void Reverb::process(const float* inL, const float* inR, float* const* out, int n) {
    if (!allocated_) return;
    const bool ensemble = kSpaces[prog_].kind == 1;
    const double gainStep = 1.0 / (0.05 * fs_);
    for (int i = 0; i < n; ++i) {
        // Glides: lengths, predelay, input gain (no zipper noise when SIZE or PREDELAY move).
        preLen_ += slew_ * (preTarget_ - preLen_);
        for (int k = 0; k < 4; ++k) inLen_[k] += slew_ * (inTarget_[k] - inLen_[k]);
        for (int h = 0; h < 2; ++h) {
            Half& hh = h_[h];
            hh.apBase += slew_ * (tgt_[h][0] - hh.apBase);
            hh.d1Len += slew_ * (tgt_[h][1] - hh.d1Len);
            hh.dif1Len += slew_ * (tgt_[h][2] - hh.dif1Len);
            hh.dif2Len += slew_ * (tgt_[h][3] - hh.dif2Len);
            hh.d2Len += slew_ * (tgt_[h][4] - hh.d2Len);
        }
        if (inGain_ < inGainTarget_) inGain_ = std::min(inGainTarget_, inGain_ + gainStep);
        else if (inGain_ > inGainTarget_) inGain_ = std::max(inGainTarget_, inGain_ - gainStep);
        const double l = bwL_.run(inL ? inL[i] : 0.0), r = bwR_.run(inR ? inR[i] : (inL ? inL[i] : 0.0));
        preL_.push(static_cast<float>(l));
        preR_.push(static_cast<float>(r));
        const double pl = preL_.lin(preLen_), pr = preR_.lin(preLen_);
        double a, b, c, d;
        if (ensemble) runEnsemble(pl, pr, a, b, c, d);
        else runTank(pl, pr, a, b, c, d);
        if (out[0]) out[0][i] = static_cast<float>(a);
        if (out[1]) out[1][i] = static_cast<float>(b);
        if (out[2]) out[2][i] = static_cast<float>(c);
        if (out[3]) out[3][i] = static_cast<float>(d);
    }
}

} // namespace tearwash
