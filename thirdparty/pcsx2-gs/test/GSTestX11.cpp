// Linux device test (docs/LINUX.md, phase 3): opens an X11 window, loads libpcsx2gs.so from the executable's folder
// through the C interface (dlopen + pcsx2gs_get_api, as urban does), sends a GIF packet (a blue background and two
// sprites, as test/GSTest.cpp) and presents it on Vulkan for a few seconds; optionally writes the GS image as a .bmp.
//   pcsx2_gs_test_x11 [scale] [seconds] [snapshot.bmp]
#include "pcsx2gs_api.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <dlfcn.h>
#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace
{
    constexpr unsigned kWidth = 1280;
    constexpr unsigned kHeight = 896;

    // A+D packed GIF data.
    struct Packet
    {
        std::vector<uint64_t> words;
        void ad(uint8_t reg, uint64_t value)
        {
            words.push_back(value);
            words.push_back(reg);
        }
        std::vector<uint64_t> finish() const
        {
            const uint64_t nloop = words.size() / 2u;
            std::vector<uint64_t> out{nloop | (1ull << 15) | (1ull << 60), 0xEull}; // NLOOP, EOP, PACKED, NREG=1, REGS=A+D
            out.insert(out.end(), words.begin(), words.end());
            return out;
        }
    };

    uint64_t Rgbaq(uint8_t r, uint8_t g, uint8_t b)
    {
        return r | (uint64_t(g) << 8) | (uint64_t(b) << 16) | (0x80ull << 24) | (uint64_t(0x3F800000u) << 32);
    }

    uint64_t Xyz(int x, int y)
    {
        return uint64_t(x * 16) | (uint64_t(y * 16) << 16);
    }

    void Sprite(Packet &p, int x0, int y0, int x1, int y1, uint64_t color)
    {
        p.ad(0x00, 6);     // PRIM: sprite
        p.ad(0x01, color); // RGBAQ
        p.ad(0x05, Xyz(x0, y0));
        p.ad(0x05, Xyz(x1, y1));
    }
}

int main(int argc, char *argv[])
{
    const float scale = argc > 1 ? static_cast<float>(std::atof(argv[1])) : 2.0f;
    const int seconds = argc > 2 ? std::atoi(argv[2]) : 3;
    const char *snapshot = argc > 3 ? argv[3] : nullptr;

    std::error_code error;
    const std::filesystem::path exeDir = std::filesystem::read_symlink("/proc/self/exe", error).parent_path();
    void *library = dlopen((exeDir / "libpcsx2gs.so").c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!library)
    {
        std::fprintf(stderr, "dlopen: %s\n", dlerror());
        return 1;
    }
    const auto getApi = reinterpret_cast<Pcsx2GsGetApiFn>(dlsym(library, "pcsx2gs_get_api"));
    const Pcsx2GsApi *api = getApi ? getApi(PCSX2GS_API_VERSION) : nullptr;
    if (!api)
    {
        std::fprintf(stderr, "no pcsx2gs_get_api for version %u\n", PCSX2GS_API_VERSION);
        return 1;
    }

    Display *display = XOpenDisplay(nullptr);
    if (!display)
    {
        std::fprintf(stderr, "cannot open the X11 display\n");
        return 1;
    }
    const Window window = XCreateSimpleWindow(display, DefaultRootWindow(display), 0, 0, kWidth, kHeight, 0, 0, 0);
    XStoreName(display, window, "pcsx2_gs_test_x11");
    Atom deleteWindow = XInternAtom(display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(display, window, &deleteWindow, 1);
    XSelectInput(display, window, StructureNotifyMask);
    XMapWindow(display, window);
    XSync(display, False);

    const std::string resources = (exeDir / "resources").string();
    const std::string cache = (exeDir / "shadercache_pcsx2").string();
    std::filesystem::create_directories(cache, error);
    Pcsx2GsOpenParams params{};
    params.hwnd = reinterpret_cast<void *>(window);
    params.width = kWidth;
    params.height = kHeight;
    params.scale = scale;
    params.resources = resources.c_str();
    params.cache = cache.c_str();
    params.vsync = 1;
    params.stretch = 0;
    params.options = "fxaa=0";
    if (!api->open(&params))
    {
        std::fprintf(stderr, "open failed\n");
        return 2;
    }

    // Privileged registers. Urban Reign's 480p display: PMODE circuit 1, SMODE1 CMOD 0 / LC 32 (480p), DISPLAY1 as the
    // game sets it, DISPFB1 = frame buffer 0, 640 wide, PSMCT32.
    Pcsx2GsPrivRegs regs{};
    regs.regset1[0] = 0xFF21ull;            // PMODE: EN1, MMOD, ALP=FF
    regs.regset1[1] = 32ull << 3;           // SMODE1: LC=32, CMOD=0
    regs.regset1[2] = 0ull;                 // SMODE2: progressive
    regs.regset1[7] = 10ull << 9;           // DISPFB1: FBP 0, FBW 10, PSMCT32
    regs.regset1[8] = 0x1BF4FF00834138ull; // DISPLAY1

    Packet p;
    p.ad(0x4C, 10ull << 16);                              // FRAME_1: FBP 0, FBW 10, PSMCT32
    p.ad(0x4E, 1ull << 32);                               // ZBUF_1: ZMSK
    p.ad(0x40, 639ull << 16 | 0ull << 32 | 447ull << 48); // SCISSOR_1
    p.ad(0x18, 0ull);                                     // XYOFFSET_1
    p.ad(0x47, (1ull << 16) | (1ull << 17));              // TEST_1: ZTE, ZTST always
    p.ad(0x1A, 1ull);                                     // PRMODECONT
    Sprite(p, 0, 0, 640, 448, Rgbaq(0x10, 0x30, 0x90));   // background
    Sprite(p, 100, 100, 300, 250, Rgbaq(0xE0, 0x20, 0x20)); // red
    Sprite(p, 340, 180, 600, 400, Rgbaq(0x20, 0xC0, 0x40)); // green
    const std::vector<uint64_t> packet = p.finish();

    const auto start = std::chrono::steady_clock::now();
    const auto end = start + std::chrono::seconds(seconds);
    uint64_t fields = 0;
    bool running = true;
    while (running && std::chrono::steady_clock::now() < end)
    {
        while (XPending(display))
        {
            XEvent event;
            XNextEvent(display, &event);
            if (event.type == ClientMessage && static_cast<Atom>(event.xclient.data.l[0]) == deleteWindow)
                running = false;
            else if (event.type == ConfigureNotify)
                api->resize(static_cast<uint32_t>(event.xconfigure.width), static_cast<uint32_t>(event.xconfigure.height));
        }
        api->gif(reinterpret_cast<const uint8_t *>(packet.data()), static_cast<uint32_t>(packet.size() / 2u));
        regs.csr = (fields & 1u) ? 0x2000u : 0u; // CSR.FIELD
        api->vsync(&regs);
        ++fields;
    }
    const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::fprintf(stderr, "%llu fields in %.2f s (%.1f per second)\n", static_cast<unsigned long long>(fields), elapsed,
                 static_cast<double>(fields) / elapsed);
    int result = 0;
    if (snapshot)
    {
        result = api->snapshot(snapshot) ? 0 : 3;
        std::fprintf(stderr, "snapshot %s: %s\n", snapshot, result == 0 ? "written" : "failed");
    }
    api->close();
    XDestroyWindow(display, window);
    XCloseDisplay(display);
    return result;
}
