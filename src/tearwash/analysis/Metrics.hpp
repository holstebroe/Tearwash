#ifndef TEARWASH_METRICS_HPP
#define TEARWASH_METRICS_HPP

// Room-acoustic metrics for the reverb tests (docs/02_ENGINE.md §6): filters, spectra,
// band levels and Schroeder decay times. Offline only; allocates freely.

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <string>
#include <vector>

namespace tearwash {

constexpr double kMPi = 3.14159265358979323846;

// --- Filters ------------------------------------------------------------------------------

struct BiquadD {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    static BiquadD lowpass(double fc, double q, double fs) { return rbj(fc, q, fs, false); }
    static BiquadD highpass(double fc, double q, double fs) { return rbj(fc, q, fs, true); }
    static BiquadD rbj(double fc, double q, double fs, bool hp) {
        BiquadD f;
        const double w = 2.0 * kMPi * std::min(fc, 0.49 * fs) / fs, c = std::cos(w), al = std::sin(w) / (2.0 * q);
        const double a0 = 1.0 + al;
        f.b0 = (hp ? (1.0 + c) : (1.0 - c)) / 2.0 / a0;
        f.b1 = (hp ? -(1.0 + c) : (1.0 - c)) / a0;
        f.b2 = f.b0;
        f.a1 = -2.0 * c / a0;
        f.a2 = (1.0 - al) / a0;
        return f;
    }
    void run(std::vector<double>& x) const {
        double z1 = 0, z2 = 0;
        for (double& v : x) {
            const double y = b0 * v + z1;
            z1 = b1 * v - a1 * y + z2;
            z2 = b2 * v - a2 * y;
            v = y;
        }
    }
};

// Zero-phase application (forward, then backward) of a cascade.
inline void filtfilt(std::vector<double>& x, const std::vector<BiquadD>& cascade) {
    for (const auto& f : cascade) f.run(x);
    std::reverse(x.begin(), x.end());
    for (const auto& f : cascade) f.run(x);
    std::reverse(x.begin(), x.end());
}

// 4th-order Butterworth sections.
inline std::vector<BiquadD> butter4(double fc, double fs, bool hp) {
    return { BiquadD::rbj(fc, 0.54119610, fs, hp), BiquadD::rbj(fc, 1.30656296, fs, hp) };
}

// DC blocker: the real unit's output transformers remove the ARU's truncation offset.
inline void dcBlock(std::vector<double>& x, double fs) { filtfilt(x, { BiquadD::highpass(15.0, 0.7071, fs) }); }

// Band [lo, hi] as Butterworth HP·LP (4th order each, zero phase).
inline std::vector<double> bandpass(const std::vector<double>& x, double lo, double hi, double fs) {
    std::vector<double> y = x;
    std::vector<BiquadD> c;
    if (lo > 0) for (auto& f : butter4(lo, fs, true)) c.push_back(f);
    if (hi < 0.49 * fs) for (auto& f : butter4(hi, fs, false)) c.push_back(f);
    filtfilt(y, c);
    return y;
}

// --- FFT ------------------------------------------------------------------------------------

inline void fft(std::vector<std::complex<double>>& a) {
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const std::complex<double> wl = std::polar(1.0, -2.0 * kMPi / double(len));
        for (size_t i = 0; i < n; i += len) {
            std::complex<double> w(1.0);
            for (size_t k = 0; k < len / 2; ++k, w *= wl) {
                const auto u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
            }
        }
    }
}

// Power spectrum of x[a, b) with a Hann window, zero-padded to >= pad samples. Bin k is k·fs/N.
inline std::vector<double> powerSpectrum(const std::vector<double>& x, size_t a, size_t b, size_t pad = 0) {
    b = std::min(b, x.size());
    const size_t len = b > a ? b - a : 0;
    size_t n = 1;
    while (n < std::max(len, pad)) n <<= 1;
    std::vector<std::complex<double>> buf(n);
    for (size_t i = 0; i < len; ++i) buf[i] = x[a + i] * (0.5 - 0.5 * std::cos(2.0 * kMPi * (i + 0.5) / len));
    fft(buf);
    std::vector<double> p(n / 2 + 1);
    for (size_t k = 0; k <= n / 2; ++k) p[k] = std::norm(buf[k]);
    return p;
}

// --- Bands --------------------------------------------------------------------------------

inline const std::vector<double>& octaveBands() {
    static const std::vector<double> b = { 125, 250, 500, 1000, 2000, 4000, 8000 };
    return b;
}
inline const std::vector<double>& thirdOctaveBands() {
    static const std::vector<double> b = { 63, 80, 100, 125, 160, 200, 250, 315, 400, 500, 630, 800, 1000, 1250, 1600,
                                           2000, 2500, 3150, 4000, 5000, 6300, 8000, 10000, 12500 };
    return b;
}

// Band levels (dB, mean power density) of a power spectrum over third-octave bands.
inline std::vector<double> bandLevels(const std::vector<double>& p, double fs, const std::vector<double>& centres) {
    const size_t n = (p.size() - 1) * 2;
    std::vector<double> out;
    for (double fc : centres) {
        const double lo = fc / std::pow(2.0, 1.0 / 6.0), hi = fc * std::pow(2.0, 1.0 / 6.0);
        // Mean power per bin, so a flat (white) response reads flat across bands.
        double e = 0;
        size_t cnt = 0;
        for (size_t k = static_cast<size_t>(std::ceil(lo * n / fs)); k <= static_cast<size_t>(hi * n / fs) && k < p.size(); ++k, ++cnt) e += p[k];
        out.push_back(10.0 * std::log10(e / std::max<size_t>(cnt, 1) + 1e-30));
    }
    return out;
}

// --- Decay ----------------------------------------------------------------------------------

struct DecayFit {
    double edt = 0, t20 = 0, t30 = 0;   // s, 0 when not measurable
    double rangeDb = 0;                 // peak-to-noise range of the decay
    double rt() const { return t30 > 0 ? t30 : t20; }
};

// Least-squares slope (dB/s) of y over t, for samples whose y lies within [hiDb, loDb] (hiDb > loDb),
// taken from the first entry below hiDb to the first below loDb.
inline double fitSlope(const std::vector<double>& y, double dt, double hiDb, double loDb, bool& ok) {
    size_t a = y.size(), b = y.size();
    for (size_t i = 0; i < y.size(); ++i) {
        if (a == y.size() && y[i] <= hiDb) a = i;
        if (y[i] <= loDb) { b = i; break; }
    }
    ok = a < b && b < y.size() && b - a >= 3;
    if (!ok) return 0;
    double st = 0, sy = 0, stt = 0, sty = 0;
    const double n = double(b - a + 1);
    for (size_t i = a; i <= b; ++i) { const double t = i * dt; st += t; sy += y[i]; stt += t * t; sty += t * y[i]; }
    const double den = n * stt - st * st;
    return den != 0 ? (n * sty - st * sy) / den : 0;
}

// 5 ms block levels (dB, energy per sample) of x from `from`.
inline std::vector<double> blockLevels(const std::vector<double>& x, size_t from, double fs, double blockSec = 0.005) {
    const size_t blk = std::max<size_t>(1, static_cast<size_t>(blockSec * fs));
    std::vector<double> lv;
    for (size_t i = from; i + blk <= x.size(); i += blk) {
        double e = 0;
        for (size_t k = 0; k < blk; ++k) e += x[i + k] * x[i + k];
        lv.push_back(10.0 * std::log10(e / blk + 1e-30));
    }
    return lv;
}

// Impulse response decay: noise-floor truncation (10 dB above the floor of the last 20 %),
// Schroeder backward integration, EDT (0..-10), T20 (-5..-25), T30 (-5..-35).
inline DecayFit schroeder(const std::vector<double>& h, size_t onset, double fs) {
    DecayFit f;
    if (onset >= h.size()) return f;
    const double blkSec = 0.005;
    const auto lv = blockLevels(h, onset, fs, blkSec);
    if (lv.size() < 20) return f;
    // Noise floor: mean energy of the last 10 %; truncation where the 20 ms average level first
    // falls within 5 dB of it (a late limit-cycle burst must not extend the integration).
    double ne = 0;
    const size_t nt = std::max<size_t>(1, lv.size() / 10);
    for (size_t i = lv.size() - nt; i < lv.size(); ++i) ne += std::pow(10.0, lv[i] / 10.0);
    const double noise = std::max(10.0 * std::log10(ne / nt + 1e-30), -200.0);
    const size_t pk = static_cast<size_t>(std::max_element(lv.begin(), lv.end()) - lv.begin());
    const double peak = lv[pk];
    f.rangeDb = peak - noise;
    size_t trunc = lv.size();
    for (size_t i = pk; i + 4 <= lv.size(); ++i) {
        double e = 0;
        for (size_t k = 0; k < 4; ++k) e += std::pow(10.0, lv[i + k] / 10.0);
        if (10.0 * std::log10(e / 4) < noise + 5.0) { trunc = i; break; }
    }
    const size_t blk = static_cast<size_t>(blkSec * fs);
    const size_t end = std::min(h.size(), onset + trunc * blk);
    std::vector<double> edc(end - onset);
    double acc = 0;
    for (size_t i = end; i-- > onset;) { acc += h[i] * h[i]; edc[i - onset] = acc; }
    const double e0 = edc.empty() ? 0 : edc[0];
    if (!(e0 > 0)) return f;
    // Decimate the EDC to 1 ms for the regression.
    const size_t step = std::max<size_t>(1, static_cast<size_t>(0.001 * fs));
    std::vector<double> y;
    for (size_t i = 0; i < edc.size(); i += step) y.push_back(10.0 * std::log10(edc[i] / e0 + 1e-30));
    const double dt = double(step) / fs;
    bool ok;
    double s = fitSlope(y, dt, 0.0, -10.0, ok);
    if (ok && s < 0) f.edt = -60.0 / s;
    const double usable = f.rangeDb - 5.0;
    if (usable >= 25.0) { s = fitSlope(y, dt, -5.0, -25.0, ok); if (ok && s < 0) f.t20 = -60.0 / s; }
    if (usable >= 35.0) { s = fitSlope(y, dt, -5.0, -35.0, ok); if (ok && s < 0) f.t30 = -60.0 / s; }
    return f;
}

} // namespace tearwash

#endif
