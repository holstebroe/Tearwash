// Reference-render null test (spux S5): curated cases are rendered through the plugin's CLAP process call
// and compared with the stored 32-bit float WAVs in src/tests/reference/ (spux::testing::runNullTest).
// A case with no stored reference fails.
//
//   tearwash_null_test                       compare; exit status 1 on a failure
//   SPUX_UPDATE_REFERENCES=1 tearwash_null_test   record the references (in the same commit as the change)
//   tearwash_null_test --diff DIR            also write <case>.render.wav and <case>.diff.wav of failures to DIR
//
// Tolerances: worst short-time spectrum difference below 1 dB and worst 10 ms level difference below 0.5 dB.

#include "tearwash/plugin/TearwashClap.hpp"
#include "tearwash/plugin/TwPresets.hpp"

#include "spux/testing/NullTest.hpp"

#include <cmath>
#include <limits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

using namespace tearwash;
using spux::testing::NullOptions;
using spux::testing::NullResult;
using spux::testing::WavData;

static const clap_host_t g_host = {
    CLAP_VERSION, nullptr, "test host", "tearwash", "", "1.0",
    [](const clap_host_t*, const char*) -> const void* { return nullptr; },
    [](const clap_host_t*) {}, [](const clap_host_t*) {}, [](const clap_host_t*) {},
};

static bool outPush(const clap_output_events_t*, const clap_event_header_t*) { return true; }

static void run(TearwashClap& p, const std::vector<float>& inL, const std::vector<float>& inR, std::vector<float>& outL,
                std::vector<float>& outR) {
    const size_t n = inL.size();
    outL.assign(n, 0.f);
    outR.assign(n, 0.f);
    for (size_t pos = 0; pos < n; pos += 512) {
        const uint32_t m = static_cast<uint32_t>(std::min<size_t>(512, n - pos));
        float* ic[2] = { const_cast<float*>(inL.data() + pos), const_cast<float*>(inR.data() + pos) };
        float* oc[2] = { outL.data() + pos, outR.data() + pos };
        clap_audio_buffer_t in{}, out{};
        in.data32 = ic; in.channel_count = 2;
        out.data32 = oc; out.channel_count = 2;
        clap_input_events_t ie{ nullptr, [](const clap_input_events_t*) -> uint32_t { return 0; },
                                [](const clap_input_events_t*, uint32_t) -> const clap_event_header_t* { return nullptr; } };
        clap_output_events_t oe{ nullptr, outPush };
        clap_process_t pr{};
        pr.frames_count = m;
        pr.audio_inputs = &in; pr.audio_inputs_count = 1;
        pr.audio_outputs = &out; pr.audio_outputs_count = 1;
        pr.in_events = &ie; pr.out_events = &oe;
        p.process(&pr);
        p.onMainThread();
    }
}

namespace {

constexpr int kRate = 48000;
constexpr size_t kFrames = 72000;   // 1.5 s

// A gated burst of gently pinked noise (0.25 s, about -14 dBFS), then a click at 0.4 s on the left.
void stimulus(std::vector<float>& l, std::vector<float>& r) {
    l.assign(kFrames, 0.f);
    r.assign(kFrames, 0.f);
    uint32_t x = 0x7EA2u;
    double b0 = 0, b1 = 0, b2 = 0;
    for (size_t i = 0; i < kRate / 4; ++i) {
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        const double w = static_cast<int32_t>(x) / 2147483648.0;
        b0 = 0.99765 * b0 + w * 0.0990460; b1 = 0.96300 * b1 + w * 0.2965164; b2 = 0.57000 * b2 + w * 1.0526913;
        const double v = (b0 + b1 + b2 + w * 0.1848) * 0.22;
        l[i] = static_cast<float>(v);
        r[i] = static_cast<float>(0.8 * v + 0.2 * w * 0.2);
    }
    l[static_cast<size_t>(kRate * 0.4)] = 0.5f;
}

struct Override { uint32_t param; double value; };
struct Case {
    const char* name;      // file name
    const char* preset;    // library preset to load first
    std::vector<Override> overrides;
};

// Three short cases (a program, shimmer, hold); wider checks render with the previous spux pin on the fly.
const std::vector<Case>& cases() {
    static const std::vector<Case> c = {
        { "program_0", "PG GRAND HALL", {} },
        { "shimmer_up", "SP Deep Space Relay", { { TW_SHIMMER, 0.6 }, { TW_SHIMMERINT, 1.0 }, { TW_MIX, 0.6 } } },
        { "hold", "SP Monolith At Jupiter", { { TW_DECAY, 1.0 }, { TW_LOWDECAY, 1.0 } } },
    };
    return c;
}

} // namespace

int main(int argc, char** argv) {
    NullOptions opt;
    opt.referenceDir = TEARWASH_REFERENCE_DIR;
    opt.peakThresholdDb = std::numeric_limits<double>::infinity();   // the peak is a report only
    opt.maxSpectralDb = 1.0;   // sound tolerances (spux NullTest.hpp): last-bit rounding passes, a changed sound does not
    opt.maxEnvelopeDb = 0.5;
    for (int i = 1; i + 1 < argc; ++i)
        if (std::strcmp(argv[i], "--diff") == 0) opt.diffDir = argv[i + 1];

    const auto* factory = static_cast<const clap_plugin_factory_t*>(clap_entry.get_factory(CLAP_PLUGIN_FACTORY_ID));
    const clap_plugin_t* cp = factory->create_plugin(factory, &g_host, "com.holstebroe.tearwash");
    auto& p = *static_cast<TearwashClap*>(cp->plugin_data);
    cp->init(cp);
    cp->activate(cp, kRate, 32, 512);

    std::vector<float> inL, inR;
    stimulus(inL, inR);

    std::vector<NullResult> results;
    int failed = 0;
    for (const Case& c : cases()) {
        const int idx = p.presetLibrary().indexOf(c.preset);
        if (idx < 0 || !p.loadPreset(idx)) {
            std::printf("FAIL %s: no preset '%s'\n", c.name, c.preset);
            ++failed;
            continue;
        }
        for (const Override& o : c.overrides) p.onParamValueFromGui(o.param, o.value);
        std::vector<float> junkL(kFrames, 0.f), junkR(kFrames, 0.f), ol, orr;
        run(p, junkL, junkR, ol, orr);   // let the new engine arrive and the old tail go
        p.reset();
        run(p, inL, inR, ol, orr);
        WavData w;
        w.channels = 2;
        w.sampleRate = kRate;
        w.samples.resize(kFrames * 2);
        for (size_t i = 0; i < kFrames; ++i) { w.samples[2 * i] = ol[i]; w.samples[2 * i + 1] = orr[i]; }
        results.push_back(spux::testing::runNullTest(c.name, w, opt));
        if (!results.back().passed()) {
            ++failed;
            std::printf("FAIL %s\n", spux::testing::nullMessage(results.back()).c_str());
        }
    }
    std::printf("%s", spux::testing::formatNullReport(results, opt).c_str());
    std::printf("%zu cases, %d failed\n", results.size(), failed);
    return failed ? 1 : 0;
}
