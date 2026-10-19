// Tearwash plugin and GUI verification: every program renders a finite, decaying response
// through the CLAP process call; the panel draws, its controls reach the parameters; host program
// changes load the program's settings; the state round-trips. With a path argument it writes a
// <path> snapshot for visual review. With an X display (xvfb-run) it also opens the panel in a real
// X11 window and checks that it shows the pixel buffer, and that clicks, wheel and the timer arrive.

#include "tearwash/plugin/TearwashClap.hpp"
#include "tearwash/plugin/TwGui.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
#include <chrono>
#include <cstdlib>
#include <thread>
#if defined(__linux__) && !defined(__APPLE__)
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#endif

using namespace tearwash;

static int g_fail = 0, g_pass = 0;
#define CHECK(cond, ...) do { if (cond) { ++g_pass; } else { ++g_fail; std::printf("FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static const clap_host_t g_host = {
    CLAP_VERSION, nullptr, "test host", "tearwash", "", "1.0",
    [](const clap_host_t*, const char*) -> const void* { return nullptr; },
    [](const clap_host_t*) {}, [](const clap_host_t*) {}, [](const clap_host_t*) {},
};

struct Events {
    std::vector<clap_event_param_value_t> ev;
    static uint32_t size(const clap_input_events_t* l) { return static_cast<uint32_t>(static_cast<const Events*>(l->ctx)->ev.size()); }
    static const clap_event_header_t* get(const clap_input_events_t* l, uint32_t i) {
        return &static_cast<const Events*>(l->ctx)->ev[i].header;
    }
    void add(clap_id id, double v, uint32_t time = 0) {
        clap_event_param_value_t e{};
        e.header.size = sizeof(e);
        e.header.time = time;
        e.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        e.header.type = CLAP_EVENT_PARAM_VALUE;
        e.param_id = id;
        e.note_id = -1; e.port_index = -1; e.channel = -1; e.key = -1;
        e.value = v;
        ev.push_back(e);
    }
};
static std::vector<std::vector<uint8_t>> g_out;
static bool outPush(const clap_output_events_t*, const clap_event_header_t* e) {
    g_out.emplace_back(reinterpret_cast<const uint8_t*>(e), reinterpret_cast<const uint8_t*>(e) + e->size);
    return true;
}

// Processes in host-sized blocks; in/out are interleaved per channel vectors.
static void run(TearwashClap& p, const std::vector<float>& inL, const std::vector<float>& inR, std::vector<float>& outL,
                std::vector<float>& outR, Events* first = nullptr) {
    const size_t n = inL.size();
    outL.assign(n, 0.f);
    outR.assign(n, 0.f);
    for (size_t pos = 0; pos < n; pos += 256) {
        const uint32_t m = static_cast<uint32_t>(std::min<size_t>(256, n - pos));
        float* ic[2] = { const_cast<float*>(inL.data() + pos), const_cast<float*>(inR.data() + pos) };
        float* oc[2] = { outL.data() + pos, outR.data() + pos };
        clap_audio_buffer_t in{}, out{};
        in.data32 = ic; in.channel_count = 2;
        out.data32 = oc; out.channel_count = 2;
        Events none;
        Events* evs = (pos == 0 && first) ? first : &none;
        clap_input_events_t ie{ evs, Events::size, Events::get };
        clap_output_events_t oe{ nullptr, outPush };
        clap_process_t pr{};
        pr.frames_count = m;
        pr.audio_inputs = &in; pr.audio_inputs_count = 1;
        pr.audio_outputs = &out; pr.audio_outputs_count = 1;
        pr.in_events = &ie; pr.out_events = &oe;
        p.process(&pr);
        p.onMainThread();   // the host would call it when asked; frees retired engines
    }
}

static void writePpm(const TwGui& w, const std::string& path) {
    std::ofstream f(path, std::ios::binary);
    f << "P6\n" << w.getWidth() << " " << w.getHeight() << "\n255\n";
    for (uint32_t px : w.getPixelBuffer()) {
        const char rgb[3] = { static_cast<char>((px >> 16) & 0xFF), static_cast<char>((px >> 8) & 0xFF), static_cast<char>(px & 0xFF) };
        f.write(rgb, 3);
    }
}

// Impulse response of the current program: returns the wet energy in the tail window and
// checks finiteness and level.
static double impulseCheck(TearwashClap& p, const char* what) {
    const int fs = 48000, n = fs * 2;
    std::vector<float> l(n, 0.f), r(n, 0.f), ol, orr;
    run(p, l, r, ol, orr);   // let the previous tail pass
    l[0] = r[0] = 0.5f;
    run(p, l, r, ol, orr);
    double tail = 0.0, peak = 0.0;
    bool finite = true;
    for (int i = 0; i < n; ++i) {
        finite = finite && std::isfinite(ol[i]) && std::isfinite(orr[i]);
        peak = std::max(peak, static_cast<double>(std::max(std::fabs(ol[i]), std::fabs(orr[i]))));
        if (i > fs / 10 && i < fs / 2) tail += double(ol[i]) * ol[i] + double(orr[i]) * orr[i];
    }
    CHECK(finite, "%s: finite output", what);
    CHECK(peak < 1.0, "%s: peak %.3f < 1", what, peak);
    CHECK(tail > 1e-6, "%s: reverberant tail (energy %.3g)", what, tail);
    return tail;
}

#if defined(__linux__) && !defined(__APPLE__)
// The panel in a real X11 window: it shows the pixel buffer, repaints on its own, and takes input.
static void windowTest(TearwashClap& p) {
    if (!std::getenv("DISPLAY")) { std::printf("window test skipped (no DISPLAY)\n"); return; }
    using namespace std::chrono_literals;
    p.createGuiWindow();
    TwGui& gui = *p.getGuiWindow();
    clap_window_t parent = {};
    parent.api = CLAP_WINDOW_API_X11;
    parent.x11 = 0;   // the root window
    bool opened = false;
    for (int i = 0; i < 20 && !opened; ++i) {   // an X server under load can refuse a connection for a moment
        opened = gui.setParent(&parent);
        if (!opened) std::this_thread::sleep_for(50ms);
    }
    CHECK(opened, "panel opens in an X11 window");
    if (!opened) { p.destroyGuiWindow(); return; }
    CHECK(gui.setSize(TwGui::kWidth, TwGui::kHeight) && !gui.setSize(100, 100), "fixed size");
    std::this_thread::sleep_for(300ms);

    Display* d = XOpenDisplay(nullptr);
    CHECK(d != nullptr, "second X connection");
    if (!d) { p.destroyGuiWindow(); return; }
    Window root, par, *kids = nullptr, found = 0;
    unsigned n = 0;
    XQueryTree(d, DefaultRootWindow(d), &root, &par, &kids, &n);
    for (unsigned i = 0; i < n; ++i) {
        XWindowAttributes a;
        if (XGetWindowAttributes(d, kids[i], &a) && a.width == int(TwGui::kWidth) && a.height == int(TwGui::kHeight) && a.map_state == IsViewable) found = kids[i];
    }
    if (kids) XFree(kids);
    CHECK(found != 0, "the panel window is mapped at %ux%u", TwGui::kWidth, TwGui::kHeight);
    if (!found) { XCloseDisplay(d); p.destroyGuiWindow(); return; }

    // The window shows the pixel buffer.
    {
        XImage* img = XGetImage(d, found, 0, 0, TwGui::kWidth, TwGui::kHeight, AllPlanes, ZPixmap);
        CHECK(img != nullptr, "read the window back");
        if (img) {
            const auto& px = gui.getPixelBuffer();
            long bad = 0;
            for (uint32_t y = 0; y < TwGui::kHeight; y += 7)
                for (uint32_t x = 0; x < TwGui::kWidth; x += 5)
                    if ((XGetPixel(img, int(x), int(y)) & 0xFFFFFFul) != (px[y * TwGui::kWidth + x] & 0xFFFFFFu)) ++bad;
            CHECK(bad == 0, "window pixels equal the panel (%ld differ)", bad);
            XDestroyImage(img);
        }
    }

    auto centre = [&](TwGui::Kind k, int id, int& x, int& y) {
        const auto& c = gui.controls()[static_cast<size_t>(gui.findControl(k, id))];
        x = static_cast<int>(c.x + c.w / 2); y = static_cast<int>(c.y + c.h / 2);
    };
    auto button = [&](int type, unsigned which, int x, int y, unsigned state = 0) {
        XEvent e = {};
        e.xbutton.type = type; e.xbutton.window = found; e.xbutton.x = x; e.xbutton.y = y; e.xbutton.button = which; e.xbutton.state = state;
        XSendEvent(d, found, False, type == ButtonPress ? ButtonPressMask : ButtonReleaseMask, &e);
        XFlush(d);
    };
    auto motion = [&](int x, int y, unsigned state) {
        XMotionEvent m = {};
        m.type = MotionNotify; m.window = found; m.x = x; m.y = y; m.state = state;
        XSendEvent(d, found, False, PointerMotionMask, reinterpret_cast<XEvent*>(&m));
        XFlush(d);
    };
    auto settle = [&] { std::this_thread::sleep_for(150ms); };
    int x, y;

    // Click: a program button selects its program.
    centre(TwGui::Kind::Program, 2, x, y);
    button(ButtonPress, Button1, x, y); button(ButtonRelease, Button1, x, y);
    settle();
    CHECK(p.program() == 2, "X11 click on a program button (%d)", p.program());

    // Wheel (buttons 4 and 5): 0.5 % per notch, up and down.
    centre(TwGui::Kind::Fader, TW_PREDELAY, x, y);
    const double w0 = p.paramValue(TW_PREDELAY);
    button(ButtonPress, Button4, x, y); button(ButtonRelease, Button4, x, y);
    settle();
    CHECK(std::fabs(p.paramValue(TW_PREDELAY) - (w0 + 0.005)) < 1e-9, "X11 wheel up (%.4f → %.4f)", w0, p.paramValue(TW_PREDELAY));
    button(ButtonPress, Button5, x, y); button(ButtonRelease, Button5, x, y);
    settle();
    CHECK(std::fabs(p.paramValue(TW_PREDELAY) - w0) < 1e-9, "X11 wheel down");

    // Drag (motion with button 1 held) moves a knob; hover (motion without) gives the tooltip.
    centre(TwGui::Kind::Knob, TW_MODDEPTH, x, y);
    const double m0 = p.paramValue(TW_MODDEPTH);
    button(ButtonPress, Button1, x, y);
    for (int i = 1; i <= 6; ++i) { motion(x, y - 5 * i, Button1Mask); std::this_thread::sleep_for(30ms); }
    button(ButtonRelease, Button1, x, y - 30);
    settle();
    CHECK(p.paramValue(TW_MODDEPTH) > m0, "X11 drag raises the knob (%.3f → %.3f)", m0, p.paramValue(TW_MODDEPTH));
    motion(x, y, 0);
    settle();
    CHECK(!gui.tooltipText().empty(), "X11 hover gives a tooltip");

    // The timer repaints a changed panel without a call from outside: the change above reached the window.
    {
        XImage* img = XGetImage(d, found, 0, 0, TwGui::kWidth, TwGui::kHeight, AllPlanes, ZPixmap);
        CHECK(img != nullptr, "read back after input");
        if (img) {
            const auto& px = gui.getPixelBuffer();
            long bad = 0;
            for (uint32_t yy = 0; yy < TwGui::kHeight; yy += 7)
                for (uint32_t xx = 0; xx < TwGui::kWidth; xx += 5)
                    if ((XGetPixel(img, int(xx), int(yy)) & 0xFFFFFFul) != (px[yy * TwGui::kWidth + xx] & 0xFFFFFFu)) ++bad;
            CHECK(bad == 0, "window follows the panel after input (%ld differ)", bad);
            XDestroyImage(img);
        }
    }

    // Hidden: no repaints. Shown again: repaints.
    gui.hide();
    std::this_thread::sleep_for(100ms);
    const int r0 = gui.redraws();
    p.onParamValueFromGui(TW_SIZE, p.paramValue(TW_SIZE) > 0.5 ? 0.25 : 0.75);   // a change the panel would show
    settle();
    CHECK(gui.redraws() == r0, "the timer does not repaint a hidden panel");
    gui.show();
    CHECK(gui.redraws() > r0, "a shown panel repaints");

    XCloseDisplay(d);
    p.destroyGuiWindow();
    CHECK(p.getGuiWindow() == nullptr, "destroyed");
}
#endif

int main(int argc, char** argv) {
    const auto* factory = static_cast<const clap_plugin_factory_t*>(clap_entry.get_factory(CLAP_PLUGIN_FACTORY_ID));
    CHECK(factory && factory->get_plugin_count(factory) == 1, "factory");
    const clap_plugin_t* cp = factory->create_plugin(factory, &g_host, "com.holstebroe.tearwash");
    CHECK(cp != nullptr, "create plugin");
    if (!cp) return 1;
    auto& p = *static_cast<TearwashClap*>(cp->plugin_data);
    cp->init(cp);
    cp->activate(cp, 48000.0, 32, 512);

    // No latency: the dry path is not delayed.
    {
        p.onParamValueFromGui(TW_MIX, 0.0);
        std::vector<float> l(4800, 0.f), r(4800, 0.f), ol, orr;
        run(p, l, r, ol, orr);
        l[10] = r[10] = 0.5f;
        run(p, l, r, ol, orr);
        int at = -1;
        for (int i = 0; i < 4800; ++i) if (std::fabs(ol[i]) > 0.25f) { at = i; break; }
        CHECK(at == 10, "dry impulse at %d, expected 10", at);
        p.onParamValueFromGui(TW_MIX, 1.0);
    }

    // Every program.
    for (int pg = 0; pg < programCount(); ++pg) {
        p.selectProgram(pg);
        impulseCheck(p, programName(pg));
        CHECK(p.program() == pg, "%s selected", programName(pg));
    }

    // Stored settings on program change; host automation of the program.
    p.selectProgram(5);   // PLATE
    const double plateDecay = p.paramValue(TW_DECAY);
    CHECK(plateDecay == Reverb::programDefaults(5).decay, "PLATE settings loaded (%.3f)", plateDecay);
    {
        Events ev;
        ev.add(TW_PROGRAM, 9);   // VAST SPACE
        std::vector<float> l(2048, 0.f), r(2048, 0.f), ol, orr;
        run(p, l, r, ol, orr, &ev);
        p.onMainThread();
        CHECK(p.program() == 9, "host program change");
        CHECK(p.paramValue(TW_DECAY) == Reverb::programDefaults(9).decay, "VAST SPACE settings after host change (%.3f, plate %.3f)",
              p.paramValue(TW_DECAY), plateDecay);
        CHECK(p.presetName() == "PG VAST SPACE" && !p.presetModified(), "the preset follows a host program change ('%s')",
              p.presetName().c_str());
    }
    // A control change through the host keeps the program.
    {
        Events ev;
        ev.add(TW_DECAY, 0.66);
        std::vector<float> l(1024, 0.f), r(1024, 0.f), ol, orr;
        run(p, l, r, ol, orr, &ev);
        p.onMainThread();
        CHECK(p.paramValue(TW_DECAY) == 0.66 && p.program() == 9, "host control change keeps the program");
    }

    // GUI.
    p.createGuiWindow();
    TwGui& gui = *p.getGuiWindow();
    gui.renderFrame();
    {
        const auto& px = gui.getPixelBuffer();
        double sum = 0, sum2 = 0;
        for (uint32_t v : px) { const double y = ((v >> 16) & 0xFF) + ((v >> 8) & 0xFF) + (v & 0xFF); sum += y; sum2 += y * y; }
        const double mean = sum / px.size(), var = sum2 / px.size() - mean * mean;
        CHECK(var > 100.0, "panel has content (variance %.1f)", var);
    }
    auto centre = [&](int i, int& x, int& y) {
        const auto& c = gui.controls()[static_cast<size_t>(i)];
        x = static_cast<int>(c.x + c.w / 2); y = static_cast<int>(c.y + c.h / 2);
    };
    int x, y;
    // Fader drag moves DECAY up.
    const int dec = gui.findControl(TwGui::Kind::Fader, TW_DECAY);
    CHECK(dec >= 0, "DECAY fader");
    {
        const auto& c = gui.controls()[static_cast<size_t>(dec)];
        const double before = p.paramValue(TW_DECAY);
        const float y1 = c.y + c.h - 8.f, y0 = c.y + 8.f;
        const int capY = static_cast<int>(y1 - before * (y1 - y0));
        gui.handleMouseDown(static_cast<int>(c.x + c.w / 2), capY, false);
        gui.handleMouseDrag(static_cast<int>(c.x + c.w / 2), capY - 20, false);
        gui.handleMouseUp();
        CHECK(p.paramValue(TW_DECAY) > before, "fader drag raises DECAY (%.3f → %.3f)", before, p.paramValue(TW_DECAY));
        gui.handleMouseMove(static_cast<int>(c.x + c.w / 2), capY);
        CHECK(gui.tooltipText().find("high band") != std::string::npos, "tooltip '%s'", gui.tooltipText().c_str());
    }
    // Wheel: 0.5 % per notch.
    {
        const int pre = gui.findControl(TwGui::Kind::Fader, TW_PREDELAY);
        centre(pre, x, y);
        const double before = p.paramValue(TW_PREDELAY);
        gui.handleWheel(x, y, 1);
        CHECK(std::fabs(p.paramValue(TW_PREDELAY) - std::min(1.0, before + 0.005)) < 1e-9, "wheel steps 0.5 %%");
        gui.handleMouseMove(x, y);
        CHECK(gui.tooltipText().find("Silence") == 0, "predelay description '%s'", gui.tooltipText().c_str());
        char shown[48];
        twValueText(TW_PREDELAY, p.paramValue(TW_PREDELAY), shown, sizeof shown);
        CHECK(std::string(shown).find(" ms") != std::string::npos, "predelay value in ms: '%s'", shown);
    }
    // Mod depth knob: drag up raises the depth.
    {
        const int k = gui.findControl(TwGui::Kind::Knob, TW_MODDEPTH);
        CHECK(k >= 0, "mod depth knob");
        centre(k, x, y);
        const double before = p.paramValue(TW_MODDEPTH);
        gui.handleMouseDown(x, y, false);
        gui.handleMouseDrag(x, y - 30, false);
        gui.handleMouseUp();
        CHECK(p.paramValue(TW_MODDEPTH) > before, "mod depth knob drag (%.3f → %.3f)", before, p.paramValue(TW_MODDEPTH));
    }
    // Program buttons.
    {
        centre(gui.findControl(TwGui::Kind::Program, 4), x, y);
        gui.handleMouseDown(x, y, false);
        gui.handleMouseUp();
        CHECK(p.program() == 4, "program button selects CHAMBER (%d)", p.program());
        CHECK(p.presetName() == "PG CHAMBER" && !p.presetModified(), "the preset follows the program button ('%s')",
              p.presetName().c_str());
        g_out.clear();
        std::vector<float> l(256, 0.f), r(256, 0.f), ol, orr;
        run(p, l, r, ol, orr);
        CHECK(!g_out.empty(), "program change reported to the host (%zu events)", g_out.size());
        CHECK(gui.findControl(TwGui::Kind::Program, programCount() - 1) >= 0, "a button per program");
    }
    gui.renderFrame();
    if (argc > 1) writePpm(gui, argv[1]);
    // No redraw when nothing changed.
    {
        gui.renderFrame();
        const int r0 = gui.redraws();
        gui.renderFrame();
        CHECK(gui.redraws() == r0, "idle frame does not redraw");
    }
    p.destroyGuiWindow();
#if defined(__linux__) && !defined(__APPLE__)
    windowTest(p);
#endif

    // State round trip.
    p.selectProgram(3);   // ROOM
    p.onParamValueFromGui(TW_SIZE, 0.25);
    p.onParamValueFromGui(TW_OUTGAIN, -6.0);
    const std::string st = p.stateText();
    {
        const clap_plugin_t* cp2 = factory->create_plugin(factory, &g_host, "com.holstebroe.tearwash");
        auto& q = *static_cast<TearwashClap*>(cp2->plugin_data);
        cp2->init(cp2);
        cp2->activate(cp2, 44100.0, 32, 512);
        CHECK(q.loadStateText(st), "state loads");
        bool same = true;
        for (uint32_t i = 0; i < TW_PARAM_COUNT; ++i) same = same && q.paramValue(i) == p.paramValue(i);
        CHECK(same, "state round trip");
        CHECK(q.stateText() == st, "state text stable");
        impulseCheck(q, "restored ROOM at 44.1 kHz");
        CHECK(!q.loadStateText("garbage"), "rejects foreign state");
        cp2->destroy(cp2);
    }

    // Parameter text.
    {
        char buf[64];
        twValueText(TW_PREDELAY, 0.5, buf, sizeof buf);
        CHECK(std::strcmp(buf, "250 ms") == 0, "predelay text '%s'", buf);
        twValueText(TW_DECAY, 1.0, buf, sizeof buf);
        CHECK(std::strcmp(buf, "Hold") == 0, "hold text '%s'", buf);
        double v = 0;
        CHECK(twTextToValue(TW_DECAY, "3.46", &v) && std::fabs(law::decaySeconds(v) - 3.46) < 0.01, "decay from text");
        CHECK(twTextToValue(TW_PROGRAM, "plate", &v) && v == 5, "program from name");
        twValueText(TW_OUTGAIN, kOutGainFloorDb, buf, sizeof buf);
        CHECK(std::strcmp(buf, "-inf dB") == 0, "output floor text '%s'", buf);
    }

    cp->destroy(cp);
    std::printf("tearwash gui test: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
