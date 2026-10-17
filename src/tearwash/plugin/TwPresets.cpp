#include "TwPresets.hpp"
#include "tearwash/engine/Reverb.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace tearwash {

namespace fs = std::filesystem;

namespace {
const char* const kOrder[] = { "IN", "PG", "HL", "RM", "PL", "SP", "EC", "FX" };
const char* const kOrderNames[] = { "Init", "Programs", "Halls", "Rooms & Chambers", "Plates", "Space", "Echo & Delay",
                                    "Ensemble & Effects" };

std::string trim(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    const size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::string upper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}
} // namespace

TwValues twDefaultValues() {
    TwValues v{};
    for (uint32_t i = 0; i < TW_PARAM_COUNT; ++i) v[i] = twParamInfo(i).def;
    return v;
}

// The program's stored settings, as the plugin's program change loads them (TearwashClap::rebuild).
TwValues twProgramValues(int program) {
    TwValues v = twDefaultValues();
    program = clampProgram(program);
    v[TW_PROGRAM] = program;
    const Controls c = Reverb::programDefaults(program);
    v[TW_DECAY] = c.decay; v[TW_LOWDECAY] = c.lowDecay; v[TW_CROSSOVER] = c.crossover; v[TW_DAMPING] = c.damping;
    v[TW_DISTANCE] = c.distance; v[TW_PREDELAY] = c.predelay; v[TW_DIFFUSION] = c.diffusion; v[TW_MODRATE] = c.modRate;
    v[TW_SIZE] = c.size; v[TW_CLARITY] = c.clarity; v[TW_HIGHCUT] = c.highCut; v[TW_MODULATION] = c.modulation ? 1.0 : 0.0;
    return v;
}

int twProgramIndex(const std::string& name) {
    const std::string u = upper(trim(name));
    for (int i = 0; i < programCount(); ++i)
        if (upper(programName(i)) == u) return i;
    return -1;
}

bool parsePresetText(const std::string& text, TwValues& values, std::string* name) {
    std::istringstream in(text);
    std::string line;
    std::vector<std::pair<std::string, std::string>> kv;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        kv.emplace_back(trim(line.substr(0, eq)), trim(line.substr(eq + 1)));
    }
    if (kv.empty()) return false;
    // The program first: the preset starts from that program's stored settings.
    std::string program;
    for (const auto& p : kv)
        if (p.first == "program") program = p.second;
    int pg = program.empty() ? static_cast<int>(values[TW_PROGRAM]) : twProgramIndex(program);
    if (pg < 0) pg = std::atoi(program.c_str());
    values = twProgramValues(pg);
    for (const auto& p : kv) {
        if (p.first == "name") { if (name) *name = p.second; continue; }
        if (p.first == "program") continue;
        const uint32_t id = twParamByKey(p.first.c_str());
        if (id >= TW_PARAM_COUNT) continue;
        char* end = nullptr;
        const double d = std::strtod(p.second.c_str(), &end);
        if (end != p.second.c_str()) values[id] = twClamp(id, d);
    }
    return true;
}

std::string presetToText(const TwValues& values, const std::string& name) {
    std::string out = "# Tearwash preset v1\n";
    out += "name=" + name + "\n";
    out += std::string("program=") + programName(static_cast<int>(values[TW_PROGRAM])) + "\n";
    char buf[96];
    for (uint32_t i = 0; i < TW_PARAM_COUNT; ++i) {
        if (i == TW_PROGRAM) continue;
        std::snprintf(buf, sizeof buf, "%s=%.10g\n", twParamInfo(i).key, values[i]);
        out += buf;
    }
    return out;
}

// ---- Library -----------------------------------------------------------------------------------

TwPresetLibrary::TwPresetLibrary() {
    TwPreset init;
    init.name = "IN Init";
    init.category = "IN";
    init.values = twProgramValues(0);
    presets_.push_back(init);
    // Every program with its stored settings.
    for (int pg = 0; pg < programCount(); ++pg) {
        TwPreset p;
        p.name = std::string("PG ") + programName(pg);
        p.category = "PG";
        p.values = twProgramValues(pg);
        presets_.push_back(p);
    }
    for (int i = 0; i < kTwBuiltinPresetCount; ++i) {
        const TwBuiltinPreset& b = kTwBuiltinPresets[i];
        TwPreset p;
        p.name = b.name;
        p.category = categoryOf(p.name);
        int pg = twProgramIndex(b.program);
        p.values = twProgramValues(pg < 0 ? 0 : pg);
        for (int k = 0; k < b.count; ++k) {
            const uint32_t id = twParamByKey(b.values[k].key);
            if (id < TW_PARAM_COUNT) p.values[id] = twClamp(id, b.values[k].value);
        }
        presets_.push_back(p);
    }
    builtinCount_ = static_cast<int>(presets_.size());
    scanUserPresets();
}

int TwPresetLibrary::indexOf(const std::string& name) const {
    for (size_t i = 0; i < presets_.size(); ++i)
        if (presets_[i].name == name) return static_cast<int>(i);
    return -1;
}

std::string TwPresetLibrary::categoryOf(const std::string& name) {
    if (name.size() >= 3 && name[2] == ' ')
        for (const char* c : kOrder) if (name.compare(0, 2, c) == 0) return c;
    return "USER";
}

std::vector<std::string> TwPresetLibrary::categories() const {
    std::vector<std::string> out;
    for (const char* c : kOrder)
        for (const auto& p : presets_) if (!p.user && p.category == c) { out.push_back(c); break; }
    for (const auto& p : presets_) if (p.user) { out.push_back("USER"); break; }
    return out;
}

std::string TwPresetLibrary::categoryName(const std::string& code) {
    for (size_t i = 0; i < sizeof kOrder / sizeof kOrder[0]; ++i) if (code == kOrder[i]) return kOrderNames[i];
    return "User";
}

std::string TwPresetLibrary::userPresetDirectory() {
    const char* home = std::getenv("TEARWASH_PRESET_DIR");   // tests and portable setups
    if (home && *home) return home;
#if defined(_WIN32)
    home = std::getenv("USERPROFILE");
#else
    home = std::getenv("HOME");
#endif
    if (!home || !*home) return "";
    return (fs::path(home) / "Documents" / "Tearwash" / "Presets").string();
}

void TwPresetLibrary::scanUserPresets() {
    presets_.resize(static_cast<size_t>(builtinCount_));
    const std::string dir = userPresetDirectory();
    std::error_code ec;
    if (dir.empty() || !fs::is_directory(dir, ec)) return;
    std::vector<fs::path> files;
    for (const auto& e : fs::directory_iterator(dir, ec))
        if (e.is_regular_file(ec) && e.path().extension() == ".twpreset") files.push_back(e.path());
    std::sort(files.begin(), files.end());
    for (const auto& f : files) {
        std::ifstream in(f, std::ios::binary);
        std::stringstream ss;
        ss << in.rdbuf();
        TwPreset p;
        p.values = twDefaultValues();
        std::string name;
        if (!parsePresetText(ss.str(), p.values, &name)) continue;
        p.name = name.empty() ? f.stem().string() : name;
        p.category = "USER";
        p.user = true;
        p.path = f.string();
        presets_.push_back(p);
    }
}

bool TwPresetLibrary::saveUserPreset(const std::string& path, const TwValues& values, std::string& name, std::string& error) {
    fs::path p(path);
    if (p.extension() != ".twpreset") p += ".twpreset";
    if (name.empty()) name = p.stem().string();
    std::error_code ec;
    if (p.has_parent_path()) fs::create_directories(p.parent_path(), ec);
    std::ofstream out(p, std::ios::binary);
    if (!out) { error = "cannot write " + p.string(); return false; }
    out << presetToText(values, name);
    if (!out) { error = "write failed: " + p.string(); return false; }
    out.close();
    scanUserPresets();
    return true;
}

} // namespace tearwash
