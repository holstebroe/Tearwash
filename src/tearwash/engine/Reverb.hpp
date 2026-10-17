#ifndef TEARWASH_REVERB_HPP
#define TEARWASH_REVERB_HPP

// Tearwash [A]: an algorithmic stereo reverb (docs/02_ENGINE.md). Floating point at the
// host rate, delays in milliseconds (scaled by SIZE, independent of the sample rate), smoothly
// interpolated modulation, continuous control laws with decays in seconds up to 60 s and a hold
// (freeze) at the top of DECAY.
//
// Structure (halls, rooms, chamber, plates): input high-cut low-pass, predelay (glides when it
// changes), four input diffusers, then a figure-of-eight tank: each half runs a modulated allpass,
// a delay, a two-band decay (the crossover low-pass splits it; LOW DECAY and DECAY set the loop
// gains from the half's delay so the decay time is what the control says), damping, two
// diffusion allpasses and a delay into the other half. Outputs A–D take decorrelated taps from
// both halves; DISTANCE moves the balance from the early (input-diffuser) taps to the tank.
// ENSEMBLE: eight voices on gliding delays with a little diffusion; DECAY sets the echo feedback.
// SHIMMER (tank programs only): a two-tap crossfaded pitch shifter reads the tank output, is band-limited
// and soft-limited, and is fed back into the tank input.
// Real-time safe: setSampleRate() allocates, process() and setControls() do not.

#include <cmath>
#include <cstdint>
#include <vector>

namespace tearwash {

// The controls, each 0…1 as the plugin stores them (laws below give the units).
struct Controls {
    double lowDecay = 0.5, decay = 0.5, crossover = 0.4, damping = 0.7, distance = 0.4, predelay = 0.0;
    double diffusion = 0.6, modRate = 0.3, size = 0.5, clarity = 0.2, highCut = 0.9;
    bool modulation = true;
    // Shimmer (docs/02_ENGINE.md §7): a pitch-shifted copy of the tank output fed back into the tank
    // input. Off unless shimmerMode is set; shimmer is the amount 0…1, shimmerInterval the shift in
    // semitones (-24…+24; the usual set is -12, -7, +5, +7, +12, +19, +24).
    double shimmer = 0.0;
    int shimmerInterval = 12;
    bool shimmerMode = false;
    bool operator==(const Controls& o) const {
        return lowDecay == o.lowDecay && decay == o.decay && crossover == o.crossover && damping == o.damping
               && distance == o.distance && predelay == o.predelay && diffusion == o.diffusion && modRate == o.modRate
               && size == o.size && clarity == o.clarity && highCut == o.highCut && modulation == o.modulation
               && shimmer == o.shimmer && shimmerInterval == o.shimmerInterval && shimmerMode == o.shimmerMode;
    }
    bool operator!=(const Controls& o) const { return !(*this == o); }
};

namespace law {
constexpr double kHold = 0.997;   // DECAY at or above: hold (infinite decay, input muted)
// Decay time 0.2 … 60 s, exponential.
inline double decaySeconds(double v) { return 0.2 * std::pow(300.0, v < 0 ? 0 : (v > 1 ? 1 : v)); }
inline double crossoverHz(double v) { return 80.0 * std::pow(125.0, v); }       // 80 Hz … 10 kHz
inline double dampingHz(double v) { return 1000.0 * std::pow(20.0, v); }        // 1 … 20 kHz (top: open)
inline double highCutHz(double v) { return 1000.0 * std::pow(20.0, v); }        // 1 … 20 kHz (top: open)
inline double predelayMs(double v) { return 1000.0 * v * v; }                    // 0 … 1000 ms
inline double sizeScale(double v) { return 0.5 * std::pow(4.0, v); }             // ×0.5 … ×2
inline double modRateHz(double v) { return 0.1 * std::pow(50.0, v); }            // 0.1 … 5 Hz
inline double shimmerRatio(int semitones) { return std::pow(2.0, semitones / 12.0); }   // frequency ratio
} // namespace law

class Reverb {
public:
    static constexpr int kPrograms = 10;
    static const char* programName(int i);
    // Stored settings of a program as 0…1 control values.
    static Controls programDefaults(int i);

    void setSampleRate(double fs);
    void setProgram(int i);
    int program() const { return prog_; }
    void setControls(const Controls& c);
    void reset();
    // in: L, R; out: A (front L), B (rear L), C (front R), D (rear R); any may be null.
    void process(const float* inL, const float* inR, float* const* out, int n);

private:
    struct Delay {
        std::vector<float> buf;
        uint32_t mask = 0, pos = 0;
        void init(double maxSamples);
        inline void push(float x) { buf[pos & mask] = x; ++pos; }
        // Delay of d ≥ 1 samples behind the last push, linear or cubic (Hermite) interpolation.
        inline float lin(double d) const;
        inline float cub(double d) const;
        void clear();
    };
    struct OnePole {   // y += a·(x − y)
        double a = 1.0, y = 0.0;
        inline double run(double x) { y += a * (x - y); return y; }
    };
    // Smooth random walk for modulation: a new random target every period, followed by two
    // cascaded one-poles (no steps, no clicks).
    struct Wander {
        double target = 0.0, s1 = 0.0, s2 = 0.0, phase = 0.0;
        uint32_t rng = 0x5EEDu;
        inline double run(double periodSamples, double smooth);
    };
    struct Half {
        Delay ap, d1, dif1, dif2, d2;
        OnePole xo, damp;
        Wander wander;
        double apBase = 0, d1Len = 0, dif1Len = 0, dif2Len = 0, d2Len = 0;   // samples (smoothed)
        double gLow = 0, gMid = 0;
    };

    // Dual-tap (Hann-crossfaded) delay-line pitch shifter for the shimmer path, one line per channel.
    struct Shifter {
        Delay line[2];
        OnePole hp[2], lp1[2], lp2[2];
        double phase = 0.0, window = 1.0;
    };

    void configure();             // targets from program + controls
    void runTank(double inL, double inR, double& a, double& b, double& c, double& d);
    void runEnsemble(double inL, double inR, double& a, double& b, double& c, double& d);

    double fs_ = 48000.0;
    int prog_ = 0;
    Controls c_;
    bool allocated_ = false;
    // Input section.
    OnePole bwL_, bwR_;
    Delay preL_, preR_;
    Delay inAp_[2][4];
    double preLen_ = 0.0, preTarget_ = 0.0;
    double inLen_[4] = {}, inTarget_[4] = {};
    double gIn_ = 0.6;
    // Tank.
    Half h_[2];
    double tgt_[2][5] = {};       // target lengths (samples): ap, d1, dif1, dif2, d2
    double gClar_ = 0.5, gDif_ = 0.5, modDepth_ = 0.0, modPeriod_ = 1.0, wEarly_ = 0.5, wLate_ = 0.5;
    double slew_ = 0.0;           // per-sample length glide coefficient
    bool hold_ = false;
    double inGain_ = 1.0, inGainTarget_ = 1.0;
    // Shimmer.
    Shifter shim_;
    double shimRate_ = 0.0;                  // tap phase increment per sample, (1 - ratio) / window
    double shimGain_ = 0.0, shimGainTarget_ = 0.0, shimFb_[2] = {};
    bool shimOn_ = false;                    // path running (amount > 0, or still fading out)
    // Ensemble voices.
    Delay voice_[2];
    Wander vWander_[8];
    double vBase_[8] = {}, vPan_[8] = {}, fbGain_ = 0.0, fb_[2] = {};
};

} // namespace tearwash

#endif
