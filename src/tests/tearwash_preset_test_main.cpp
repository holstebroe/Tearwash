// Tearwash presets (docs/03_PRESETS.md): format round trip; every preset (programs, factory library)
// renders finite and below full scale through the plugin, and the factory presets are
// loudness-matched; the panel's preset bar and menu load presets; save, rescan and file load;
// the state keeps the preset; CLAP preset-load and preset discovery.
//
//   tearwash_preset_test            run the checks
//   tearwash_preset_test --fit FILE fit the factory presets' output gains (dB) to FILE (JSON)

#include "tearwash/plugin/TearwashClap.hpp"
#ifdef TEARWASH_WITH_EDITOR
#include "GoldenFrame.hpp"
#include "tearwash/plugin/TwEditor.hpp"
#include "spux/ui/classic/ShadedRenderer.hpp"
#endif
#include "tearwash/plugin/TwPresets.hpp"

#include <clap/ext/preset-load.h>
#include <clap/factory/preset-discovery.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

using namespace tearwash;

static int g_fail = 0, g_pass = 0;
#define CHECK(cond, ...) do { if (cond) { ++g_pass; } else { ++g_fail; std::printf("FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

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

// Noise at −20 dBFS RMS for 4 s, then silence: returns the output level (dB re the input) over
// 1.5–4 s, and the peak and finiteness over the whole run.
struct Render { double levelDb = 0, peak = 0; bool finite = true; };
static Render renderPreset(TearwashClap& p) {
    const int fs = 48000;
    const size_t n = static_cast<size_t>(fs) * 5;
    std::vector<float> l(n, 0.f), r(n, 0.f), ol, orr;
    run(p, l, r, ol, orr);   // let the new engine arrive and the old tail go
    p.reset();
    uint32_t x = 0x7EA2u;
    double b0 = 0, b1 = 0, b2 = 0;   // gently pinked
    for (size_t i = 0; i < size_t(fs) * 4; ++i) {
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        const double w = static_cast<int32_t>(x) / 2147483648.0;
        b0 = 0.99765 * b0 + w * 0.0990460; b1 = 0.96300 * b1 + w * 0.2965164; b2 = 0.57000 * b2 + w * 1.0526913;
        const double v = (b0 + b1 + b2 + w * 0.1848) * 0.11;
        l[i] = static_cast<float>(v);
        r[i] = static_cast<float>(0.8 * v + 0.2 * w * 0.1);
    }
    run(p, l, r, ol, orr);
    Render out;
    double ei = 0, eo = 0;
    for (size_t i = 0; i < n; ++i) {
        out.finite = out.finite && std::isfinite(ol[i]) && std::isfinite(orr[i]);
        out.peak = std::max(out.peak, static_cast<double>(std::max(std::fabs(ol[i]), std::fabs(orr[i]))));
        if (i >= size_t(fs) * 3 / 2 && i < size_t(fs) * 4) {
            ei += double(l[i]) * l[i] + double(r[i]) * r[i];
            eo += double(ol[i]) * ol[i] + double(orr[i]) * orr[i];
        }
    }
    out.levelDb = 10.0 * std::log10((eo + 1e-30) / (ei + 1e-30));
    return out;
}

// Preset discovery: a minimal indexer and metadata receiver.
struct Index {
    std::vector<std::string> filetypes, locations, presets;
    int pluginIds = 0;
};
static Index* g_index = nullptr;

int main(int argc, char** argv) {
    const std::string tmp = (std::filesystem::temp_directory_path() / "tearwash_preset_test").string();
    std::filesystem::remove_all(tmp);
#ifdef _WIN32
    _putenv_s("TEARWASH_PRESET_DIR", tmp.c_str());
#else
    setenv("TEARWASH_PRESET_DIR", tmp.c_str(), 1);
#endif
    const bool fit = argc > 2 && std::strcmp(argv[1], "--fit") == 0;

    // Format round trip.
    {
        TwValues v = twProgramValues(5);
        v[TW_DECAY] = 0.5; v[TW_MIX] = 0.42; v[TW_OUTGAIN] = -3.5;
        const std::string text = presetToText(v, "HL Round Trip");
        TwValues w = twDefaultValues();
        std::string name;
        CHECK(parsePresetText(text, w, &name) && name == "HL Round Trip", "parse preset text");
        bool same = true;
        for (uint32_t i = 0; i < TW_PARAM_COUNT; ++i) same = same && std::fabs(v[i] - w[i]) < 1e-9;
        CHECK(same, "preset round trip keeps every value");
        CHECK(text.find("program=PLATE") != std::string::npos, "program stored by name");
    }
    // Every built-in preset names a program; the library has the categories.
    for (int i = 0; i < kTwBuiltinPresetCount; ++i)
        CHECK(twProgramIndex(kTwBuiltinPresets[i].program) >= 0, "%s: program %s",
              kTwBuiltinPresets[i].name, kTwBuiltinPresets[i].program);
    TwPresetLibrary lib;
    {
        int sp = 0, progs = 0;
        for (const auto& p : lib.presets()) { sp += p.category == "SP"; progs += p.category == "PG"; }
        CHECK(sp >= 20, "space presets: %d", sp);
        CHECK(progs == programCount(), "one preset per program (%d)", progs);
        CHECK(lib.categories().size() >= 8, "categories");
    }

    const auto* factory = static_cast<const clap_plugin_factory_t*>(clap_entry.get_factory(CLAP_PLUGIN_FACTORY_ID));
    const clap_plugin_t* cp = factory->create_plugin(factory, &g_host, "com.holstebroe.tearwash");
    auto& p = *static_cast<TearwashClap*>(cp->plugin_data);
    cp->init(cp);
    cp->activate(cp, 48000.0, 32, 512);

    // Every preset renders; factory presets are loudness-matched (the PG presets are the programs
    // as they are, Mix 35 %).
    std::map<std::string, double> fitted;
    for (int i = 0; i < lib.count(); ++i) {
        const TwPreset& pr = lib.presets()[static_cast<size_t>(i)];
        if (pr.user) continue;
        CHECK(p.loadPreset(p.presetLibrary().indexOf(pr.name)) && p.presetName() == pr.name, "load %s", pr.name.c_str());
        const Render r = renderPreset(p);
        CHECK(r.finite && r.peak < 1.0, "%s: finite %d, peak %.3f", pr.name.c_str(), r.finite, r.peak);
        const bool factoryPreset = pr.category != "PG" && pr.category != "IN";
        if (fit && factoryPreset) fitted[pr.name] = std::max(-12.0, std::min(12.0, p.paramValue(TW_OUTGAIN) - r.levelDb));
        else if (factoryPreset) CHECK(std::fabs(r.levelDb) < 1.5, "%s: level %+.2f dB re the input", pr.name.c_str(), r.levelDb);
    }
    if (fit) {
        std::ofstream f(argv[2]);
        f << "{\n";
        size_t k = 0;
        for (const auto& kv : fitted) f << "  \"" << kv.first << "\": " << kv.second << (++k < fitted.size() ? ",\n" : "\n");
        f << "}\n";
        std::printf("fitted %zu presets -> %s\n", fitted.size(), argv[2]);
        return 0;
    }

    // Stepping, the modified marker, save / rescan / file load, the state.
    {
        const int a = p.presetLibrary().indexOf("SP Deep Space Relay");
        CHECK(a >= 0 && p.loadPreset(a), "load by name");
        CHECK(!p.presetModified(), "fresh preset is not modified");
        p.onParamValueFromGui(TW_DECAY, 0.3);
        CHECK(p.presetModified(), "edit marks the preset modified");
        p.stepPreset(1);
        CHECK(p.presetIndex() == a + 1 && !p.presetModified(), "next preset");
        p.stepPreset(-1);
        CHECK(p.presetIndex() == a, "previous preset");
        p.onParamValueFromGui(TW_MIX, 0.77);
        std::string err;
        CHECK(p.savePreset(tmp + "/My Space.twpreset", err), "save user preset: %s", err.c_str());
        CHECK(p.presetName() == "My Space" && !p.presetModified(), "saved preset becomes the current one");
        const int u = p.presetLibrary().indexOf("My Space");
        CHECK(u >= 0 && p.presetLibrary().presets()[static_cast<size_t>(u)].user, "rescan finds the user preset");
        p.loadPreset(0);
        CHECK(p.loadPresetFile(tmp + "/My Space.twpreset") && std::fabs(p.paramValue(TW_MIX) - 0.77) < 1e-9, "load preset file");
        const std::vector<uint8_t> st = p.stateBytes();
        p.loadPreset(0);
        CHECK(p.loadState(st) && p.presetName() == "My Space" && !p.presetModified(), "state keeps the preset");
    }
#ifdef TEARWASH_WITH_EDITOR
    // The panel: preset bar and menu.
    {
        using namespace spux::ui;
        p.createGuiWindow();
        TwEditor& gui = *p.getGuiWindow();
        gui.drawFrame(true);
        auto click = [&](Point at) {
            gui.editor().mouseMove(at.x, at.y);
            gui.drawFrame(true);
            gui.editor().mouseDown(at.x, at.y, MouseButton::Left, false);
            gui.drawFrame(true);
            gui.editor().mouseUp(at.x, at.y, MouseButton::Left);
            gui.drawFrame(true);
        };
        auto move = [&](Point at) {
            gui.editor().mouseMove(at.x, at.y);
            gui.drawFrame(true);
        };
        // The bar's parts (18 wide arrows, SAVE 44 wide, 8 between).
        const Rect bar = kPresetBar;
        const Rect next{ bar.right() - 52 - 18, bar.y, 18, bar.h };
        const Rect name{ bar.x + 18 + 8, bar.y, next.x - (bar.x + 18) - 16, bar.h };
        p.loadPreset(p.presetLibrary().indexOf("HL Grand Rotunda"));
        click(next.center());
        CHECK(p.presetName() != "HL Grand Rotunda", "next button steps (%s)", p.presetName().c_str());
        CHECK(!gui.gui().popupOpen(), "no menu yet");
        click(name.center());
        CHECK(gui.gui().popupOpen(), "click on the name opens the menu");
        if (argc > 1 && !fit) {
            std::ofstream f(argv[1], std::ios::binary);
            f << "P6\n" << gui.editor().width() << " " << gui.editor().height() << "\n255\n";
            const uint32_t* px = gui.editor().pixels();
            for (int i = 0; i < gui.editor().width() * gui.editor().height(); ++i) {
                const char rgb[3] = { char((px[i] >> 16) & 0xFF), char((px[i] >> 8) & 0xFF), char(px[i] & 0xFF) };
                f.write(rgb, 3);
            }
        }
        {   // the open menu against its golden frame (the pointer rests on the name)
            const goldenframe::Result g = goldenframe::check(TEARWASH_GOLDEN_DIR, "menu", gui.editor().pixels(), gui.editor().width(), gui.editor().height());
            CHECK(g.ok, "menu golden frame: mean difference %.3f, moved %.3f %% (%s)", g.meanDiff, 100.0 * g.movedShare, g.note.c_str());
        }
        // The menu lists the families, each opening its presets beside it: lay the lists out as the Gui does.
        spux::ui::classic::ClassicStyle style(twClassicConfig());
        spux::ui::classic::ShadedRenderer r(kPanelWidth, kPanelHeight, 1);
        std::vector<PopupItem> families;
        std::string first;   // the first preset of the first family
        for (const auto& pr : lib.presets()) {
            const std::string fam = TwPresetLibrary::categoryName(pr.category);
            bool seen = false;
            for (const auto& f : families) seen = seen || f.label == fam;
            if (!seen) {
                families.push_back({ fam });
                if (families.size() == 1) first = pr.name;
            }
        }
        PopupState ps;
        ps.theme = &style.theme;
        ps.items = &families;
        ps.anchor = name;
        std::vector<Rect> rows;
        const Rect box = style.popup.layout(r, ps, kPanelWidth, kPanelHeight, rows);
        CHECK(rows.size() == families.size() && rows.size() >= 8, "a row per family (%zu)", rows.size());
        std::vector<PopupItem> kids = { { first } };
        PopupState sub = ps;
        sub.items = &kids;
        sub.anchor = Rect{ box.right() - 2, rows[0].y - 3, 0, 0 };
        std::vector<Rect> kidRows;
        style.popup.layout(r, sub, kPanelWidth, kPanelHeight, kidRows);
        move(rows[0].center());
        move(kidRows[0].center());
        CHECK(gui.gui().popupOpen(), "the family opens its presets");
        click(kidRows[0].center());
        CHECK(!gui.gui().popupOpen(), "choosing a preset closes the menu");
        CHECK(p.presetName() == first, "menu loads the first preset of the first family (%s)", p.presetName().c_str());
        CHECK(gui.savePresetAs(tmp + "/From Panel.twpreset") && p.presetName() == "From Panel", "panel save");
        p.destroyGuiWindow();
    }
#endif
    // CLAP preset-load.
    {
        const auto* ext = static_cast<const clap_plugin_preset_load_t*>(cp->get_extension(cp, CLAP_EXT_PRESET_LOAD));
        CHECK(ext && ext->from_location(cp, CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, nullptr, "SP Frozen Nebula")
                  && p.presetName() == "SP Frozen Nebula",
              "preset-load: factory preset by key");
        const std::string f = tmp + "/My Space.twpreset";
        CHECK(ext && ext->from_location(cp, CLAP_PRESET_DISCOVERY_LOCATION_FILE, f.c_str(), nullptr) && p.presetName() == "My Space",
              "preset-load: user file");
    }
    // Preset discovery.
    {
        Index idx;
        g_index = &idx;
        const auto* df = static_cast<const clap_preset_discovery_factory_t*>(clap_entry.get_factory(CLAP_PRESET_DISCOVERY_FACTORY_ID));
        CHECK(df && df->count(df) == 1, "discovery factory");
        clap_preset_discovery_indexer_t ix{};
        ix.clap_version = CLAP_VERSION;
        ix.name = "test";
        ix.declare_filetype = [](const clap_preset_discovery_indexer_t*, const clap_preset_discovery_filetype_t* f) {
            g_index->filetypes.push_back(f->file_extension); return true; };
        ix.declare_location = [](const clap_preset_discovery_indexer_t*, const clap_preset_discovery_location_t* l) {
            g_index->locations.push_back(l->name); return true; };
        ix.declare_soundpack = [](const clap_preset_discovery_indexer_t*, const clap_preset_discovery_soundpack_t*) { return true; };
        ix.get_extension = [](const clap_preset_discovery_indexer_t*, const char*) -> const void* { return nullptr; };
        const auto* prov = df->create(df, &ix, df->get_descriptor(df, 0)->id);
        CHECK(prov && prov->init(prov), "provider init");
        CHECK(idx.filetypes.size() == 1 && idx.filetypes[0] == "twpreset" && idx.locations.size() == 2, "filetype and locations");
        clap_preset_discovery_metadata_receiver_t rc{};
        rc.begin_preset = [](const clap_preset_discovery_metadata_receiver_t*, const char* name, const char*) {
            g_index->presets.push_back(name); return true; };
        rc.add_plugin_id = [](const clap_preset_discovery_metadata_receiver_t*, const clap_universal_plugin_id_t*) { ++g_index->pluginIds; };
        rc.set_flags = [](const clap_preset_discovery_metadata_receiver_t*, uint32_t) {};
        rc.add_feature = [](const clap_preset_discovery_metadata_receiver_t*, const char*) {};
        CHECK(prov->get_metadata(prov, CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, nullptr, &rc), "factory metadata");
        int builtin = 0;
        for (const auto& pr : lib.presets()) builtin += !pr.user;
        CHECK(static_cast<int>(idx.presets.size()) == builtin && idx.pluginIds == builtin, "every factory preset listed (%zu)",
              idx.presets.size());
        const std::string f = tmp + "/My Space.twpreset";
        idx.presets.clear();
        CHECK(prov->get_metadata(prov, CLAP_PRESET_DISCOVERY_LOCATION_FILE, f.c_str(), &rc) && idx.presets.size() == 1
                  && idx.presets[0] == "My Space",
              "user file metadata");
        prov->destroy(prov);
    }
    cp->destroy(cp);
    std::filesystem::remove_all(tmp);
    std::printf("tearwash preset test: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
