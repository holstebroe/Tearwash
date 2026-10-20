// Tearwash state and parameter compatibility across the move onto spux (spux_params, spux_state,
// spux_clap). The fixtures in src/tests/data were written by Tearwash before the move (861300d):
//
//   state_v1_*.twstate       host states (text, "tearwash 1"): defaults, a modified factory preset, a
//                            program with host-style edits (−∞ output, rear outputs, hold), a user preset
//   preset_v1_user.twpreset  a user preset file
//   params_v1.txt            what the plugin told the host: parameter info, value text, text parsing
//   state_v2_*.twstate       the same program state saved as the spux container (TWR1, version 2), so
//                            the new format cannot drift either
//
// Every state loads through the CLAP state extension with the same values and preset; it saves as the
// container and loads back identically; the parameter info and value text are what they were.
//
//   tearwash_state_test [DATA_DIR]

#include "tearwash/plugin/TearwashClap.hpp"
#include "tearwash/plugin/TwPresets.hpp"
#include "tearwash/plugin/TwState.hpp"
#include "tearwash/engine/Reverb.hpp"

#include "spux/state/Bytes.hpp"

#include <clap/clap.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace tearwash;

static int g_fail = 0, g_pass = 0;
#define CHECK(cond, ...) do { if (cond) { ++g_pass; } else { ++g_fail; std::printf("FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

#ifndef TEARWASH_TEST_DATA_DIR
#define TEARWASH_TEST_DATA_DIR "src/tests/data"
#endif

static const clap_host_t g_host = {
    CLAP_VERSION, nullptr, "test host", "tearwash", "", "1.0",
    [](const clap_host_t*, const char*) -> const void* { return nullptr; },
    [](const clap_host_t*) {}, [](const clap_host_t*) {}, [](const clap_host_t*) {},
};

static std::string g_dir = TEARWASH_TEST_DATA_DIR;

static std::vector<uint8_t> readFile(const std::string& name) {
    std::ifstream in(g_dir + "/" + name, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    const std::string s = ss.str();
    return std::vector<uint8_t>(s.begin(), s.end());
}

// The key=value lines of a v1 text state, read independently of the plugin.
struct TextState { std::string preset; std::map<std::string, double> values; };
static TextState readText(const std::vector<uint8_t>& bytes) {
    TextState t;
    std::istringstream in(std::string(bytes.begin(), bytes.end()));
    std::string line;
    std::getline(in, line);
    while (std::getline(in, line)) {
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        if (line.compare(0, eq, "preset") == 0) t.preset = line.substr(eq + 1);
        else t.values[line.substr(0, eq)] = std::strtod(line.c_str() + eq + 1, nullptr);
    }
    return t;
}

struct Instance {
    const clap_plugin_t* cp = nullptr;
    TearwashClap* p = nullptr;
    Instance() {
        const auto* f = static_cast<const clap_plugin_factory_t*>(clap_entry.get_factory(CLAP_PLUGIN_FACTORY_ID));
        cp = f->create_plugin(f, &g_host, "com.holstebroe.tearwash");
        cp->init(cp);
        p = static_cast<TearwashClap*>(cp->plugin_data);
    }
    ~Instance() { cp->destroy(cp); }
    const clap_plugin_state_t* state() const { return static_cast<const clap_plugin_state_t*>(cp->get_extension(cp, CLAP_EXT_STATE)); }
    const clap_plugin_params_t* params() const { return static_cast<const clap_plugin_params_t*>(cp->get_extension(cp, CLAP_EXT_PARAMS)); }

    bool load(const std::vector<uint8_t>& bytes) {
        struct Src { const std::vector<uint8_t>* d; size_t pos; } src{ &bytes, 0 };
        const clap_istream_t is{ &src, [](const clap_istream_t* s, void* buf, uint64_t n) -> int64_t {
            auto* r = static_cast<Src*>(s->ctx);
            const size_t k = std::min<size_t>(static_cast<size_t>(n > 7 ? 7 : n), r->d->size() - r->pos);   // in small parts
            std::memcpy(buf, r->d->data() + r->pos, k);
            r->pos += k;
            return static_cast<int64_t>(k);
        } };
        return state()->load(cp, &is);
    }
    std::vector<uint8_t> save() const {
        std::vector<uint8_t> out;
        const clap_ostream_t os{ &out, [](const clap_ostream_t* s, const void* d, uint64_t n) -> int64_t {
            auto* b = static_cast<std::vector<uint8_t>*>(s->ctx);
            b->insert(b->end(), static_cast<const uint8_t*>(d), static_cast<const uint8_t*>(d) + n);
            return static_cast<int64_t>(n);
        } };
        return state()->save(cp, &os) ? out : std::vector<uint8_t>{};
    }
    double value(uint32_t id) const { double v = 0; params()->get_value(cp, id, &v); return v; }
};

static void checkV1(const char* file, bool modified) {
    const std::vector<uint8_t> bytes = readFile(file);
    CHECK(!bytes.empty(), "%s: fixture present (data dir %s)", file, g_dir.c_str());
    if (bytes.empty()) return;
    const TextState t = readText(bytes);
    CHECK(t.values.size() == TW_PARAM_COUNT, "%s: %zu values in the fixture", file, t.values.size());

    Instance a;
    CHECK(a.load(bytes), "%s: loads through the CLAP state extension", file);
    bool same = true;
    for (const auto& [key, v] : t.values) {
        const uint32_t id = twParamByKey(key.c_str());
        same = same && id < TW_PARAM_COUNT && a.value(id) == v && a.p->paramValue(id) == v;
    }
    CHECK(same, "%s: every parameter has its saved value, bit for bit", file);
    CHECK(a.p->presetName() == t.preset, "%s: preset '%s' (got '%s')", file, t.preset.c_str(), a.p->presetName().c_str());
    CHECK(a.p->presetModified() == modified, "%s: preset modified marker %d", file, modified ? 1 : 0);
    CHECK(a.p->program() == static_cast<int>(t.values.at("program")), "%s: program", file);

    // Saved again: the spux container, which loads back identically in a fresh instance.
    const std::vector<uint8_t> v2 = a.save();
    CHECK(v2.size() > 8 && std::memcmp(v2.data(), "TWR1", 4) == 0 && v2[4] == kStateVersion, "%s: saves as TWR1 v%u", file, kStateVersion);
    Instance b;
    CHECK(b.load(v2), "%s: the container loads", file);
    bool back = true;
    for (uint32_t i = 0; i < TW_PARAM_COUNT; ++i) back = back && b.value(i) == a.value(i);
    CHECK(back, "%s: container round trip", file);
    CHECK(b.p->presetName() == a.p->presetName() && b.p->presetModified() == a.p->presetModified(), "%s: container keeps the preset", file);
    CHECK(b.save() == v2, "%s: container bytes stable", file);
}

int main(int argc, char** argv) {
    if (argc > 1) g_dir = argv[1];

    // States saved before spux.
    checkV1("state_v1_default.twstate", false);
    checkV1("state_v1_preset_modified.twstate", true);
    checkV1("state_v1_program_edits.twstate", true);
    checkV1("state_v1_user_preset.twstate", false);   // a preset not in the library: the state is its preset

    // The container written by this version.
    {
        const std::vector<uint8_t> v1 = readFile("state_v1_program_edits.twstate");
        const std::vector<uint8_t> v2 = readFile("state_v2_program_edits.twstate");
        Instance a, b;
        CHECK(a.load(v1) && b.load(v2), "v2 fixture loads");
        bool same = true;
        for (uint32_t i = 0; i < TW_PARAM_COUNT; ++i) same = same && a.value(i) == b.value(i);
        CHECK(same && b.p->presetName() == "PG VAST SPACE", "v2 fixture holds the v1 state");
        CHECK(a.save() == v2, "the container format is unchanged (bytes equal the v2 fixture)");

        TwState st;
        CHECK(parseState(v2, st) && st.version == 2 && st.unknownChunks == 0 && st.damagedChunks == 0, "v2 parses cleanly");
        CHECK(parseState(v1, st) && st.version == 1, "v1 is read as version 1 and migrated");

        // A chunk from a later version is skipped; a damaged chunk is counted and its data left at defaults.
        std::vector<uint8_t> later = v2;
        const uint8_t hi[] = { 'h', 'i' };
        const uint32_t crc = spux::state::crc32(hi, 2);
        const uint8_t next[] = { 'N', 'E', 'X', 'T', 2, 0, 0, 0, uint8_t(crc), uint8_t(crc >> 8), uint8_t(crc >> 16), uint8_t(crc >> 24), 'h', 'i' };
        later.insert(later.end(), next, next + sizeof next);
        CHECK(parseState(later, st) && st.unknownChunks == 1 && st.damagedChunks == 0 && st.values[TW_DECAY] == b.value(TW_DECAY),
              "a chunk of a later version is skipped");
        later.back() ^= 1;
        CHECK(parseState(later, st) && st.unknownChunks == 0 && st.damagedChunks == 1 && st.values[TW_DECAY] == b.value(TW_DECAY),
              "a chunk with a wrong CRC is damaged, the chunks before it still load");
        std::vector<uint8_t> broken = v2;
        broken[8 + 12 + 4 + 4] ^= 0x01;   // the first PARM value
        CHECK(parseState(broken, st) && st.damagedChunks == 1 && st.values[TW_PROGRAM] == 0.0 && st.preset == "PG VAST SPACE",
              "a damaged PARM keeps the defaults, the preset name still loads");
        const std::string foreign = "LMN1\1\0\0\0";
        CHECK(!parseState(std::vector<uint8_t>(foreign.begin(), foreign.end()), st), "another plugin's container is refused");
        CHECK(!b.load(std::vector<uint8_t>{}), "an empty state is refused");
    }

    // A user preset file saved before spux.
    {
        Instance a;
        const std::string path = g_dir + "/preset_v1_user.twpreset";
        CHECK(a.p->loadPresetFile(path), "v1 preset file loads");
        CHECK(a.p->presetName() == "My Lost Space" && a.p->program() == 3, "preset name and program");
        CHECK(a.p->paramValue(TW_DIFFUSION) == 0.123456789 && a.p->paramValue(TW_MIX) == 0.77 &&
              a.p->paramValue(TW_SHIMMERINT) == 0.6666666667, "preset values");
    }

    // Parameters: what the host saw before spux, line for line.
    {
        Instance a;
        std::ifstream in(g_dir + "/params_v1.txt");
        std::string line;
        int infos = 0, texts = 0, badInfo = 0, badText = 0;
        char buf[256];
        while (std::getline(in, line)) {
            if (line.compare(0, 5, "info ") == 0) {
                const uint32_t i = static_cast<uint32_t>(std::atoi(line.c_str() + 5));
                clap_param_info_t info;
                if (!a.params()->get_info(a.cp, i, &info)) { ++badInfo; continue; }
                std::snprintf(buf, sizeof buf, "info %u %s|%s|%s %.17g %.17g %.17g %u", info.id, twParamInfo(i).key, info.name,
                              info.module, info.min_value, info.max_value, info.default_value, info.flags);
                if (line != buf) { ++badInfo; std::printf("  was: %s\n  now: %s\n", line.c_str(), buf); }
                ++infos;
            } else if (line.compare(0, 5, "text ") == 0) {
                char* end = nullptr;
                const uint32_t i = static_cast<uint32_t>(std::strtoul(line.c_str() + 5, &end, 10));
                const double v = std::strtod(end, nullptr);
                char text[64];
                a.params()->value_to_text(a.cp, i, v, text, sizeof text);
                double back = -1;
                const bool ok = a.params()->text_to_value(a.cp, i, text, &back);
                std::snprintf(buf, sizeof buf, "text %u %.17g %s|%d %.17g", i, v, text, ok ? 1 : 0, ok ? back : 0.0);
                if (line != buf) { ++badText; if (badText < 5) std::printf("  was: %s\n  now: %s\n", line.c_str(), buf); }
                ++texts;
            }
        }
        CHECK(infos == TW_PARAM_COUNT && badInfo == 0, "parameter info unchanged (%d of %d differ)", badInfo, infos);
        CHECK(texts > 0 && badText == 0, "value text and parsing unchanged (%d of %d differ)", badText, texts);
        CHECK(a.params()->count(a.cp) == TW_PARAM_COUNT, "parameter count");
    }

    // The table's defaults are the first program's stored settings (TwParams.def), as before.
    {
        const TwValues pg = twProgramValues(0);
        bool same = true;
        for (uint32_t i = 0; i < TW_PARAM_COUNT; ++i)
            if (i != TW_MIX && i != TW_INGAIN && i != TW_OUTGAIN && i != TW_REAR && i != TW_SHIMMER && i != TW_SHIMMERINT)
                same = same && twParamInfo(i).def == pg[i];
        CHECK(same, "defaults equal Reverb::programDefaults(0)");
    }

    // Extensions: spux_clap offers what the plugin implements, plus Tearwash's preset-load.
    {
        Instance a;
        for (const char* id : { CLAP_EXT_PARAMS, CLAP_EXT_STATE, CLAP_EXT_AUDIO_PORTS, CLAP_EXT_GUI, CLAP_EXT_PRESET_LOAD })
            CHECK(a.cp->get_extension(a.cp, id) != nullptr, "extension %s", id);
        CHECK(a.cp->get_extension(a.cp, CLAP_EXT_NOTE_PORTS) == nullptr, "no note ports");
        CHECK(a.cp->desc != nullptr && std::strcmp(a.cp->desc->id, "com.holstebroe.tearwash") == 0, "descriptor");
    }

    std::printf("tearwash state test: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
