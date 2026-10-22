# Linux build (work in progress)

Branch `feat/linux-support`. Goal: `urban` built natively on Linux with Clang, rendering through PCSX2's GS on
**Vulkan** (the Windows build uses DX11), in an X11 window (XWayland on a Wayland desktop).

## Status

| phase | what | state |
|---|---|---|
| 0 | Environment: dependencies (`tools/linux/install_deps.sh`), line endings | done |
| 1 | Portable CMake: Clang/GCC flags, `build/linux`, SDL3 and raylib (X11) on Linux; the runtime and the generated code compile | done |
| 2 | Host code: `main.cpp` (game defaults outside `_WIN32`, stack, crash report), mod loader with `dlopen`; boots to the menus with the software GS | done |
| 3 | PCSX2's GS on Vulkan (`libpcsx2gs.so`): Vulkan renderer, `common/Linux`, shaders, PNG through libpng, `Bridge.cpp`; device test | done |
| 4 | Runtime ↔ GS window: `dlopen` of `libpcsx2gs.so`, X11 child window over the game window; MTVU validated with it; fights at 60 | done |
| 5 | `build.py` on Linux (build, play, generate, bench); release: `.tar.gz` and `.deb` built in the Steam Runtime | done |
| 6 | Mods (CoopTool, ExampleMod) as `.so` | |
| 7 | Validation: boot, menus, fights, save states, HD textures, audio, controllers | |

## Setting up

```sh
sh tools/linux/install_deps.sh   # apt, dnf or pacman (uses sudo)
```

Tested on Debian 13 (GNOME on Wayland, NVIDIA GTX 1660 SUPER, driver 610). Notes:

- **Line endings.** A checkout made on Windows (`core.autocrlf=true`) has CRLF files; Git on Linux then lists every
  file as modified. In such a shared checkout set the same option locally: `git config core.autocrlf true`. Shell
  scripts are always LF (`.gitattributes`).
- **shaderc.** PCSX2 loads `libshaderc_shared.so.1`; Debian's `libshaderc1` installs it as `libshaderc.so.1` (same C
  API). The Linux build of the GS tries both names (phase 3).
- **Debian with backports.** The Wayland/xkbcommon and udev headers are left out on purpose: they are not needed, and
  on a system with newer libraries from backports their `-dev` packages do not install.

## Building

```sh
python3 build.py            # the menu, as on Windows
python3 build.py build      # configure (Clang, Ninja, LLD; build/linux) and build urban + libpcsx2gs.so
python3 build.py play       # run build/linux/bin/urban
python3 build.py generate   # recompile the game code from your disc image (builds ps2_recomp first)
python3 build.py release    # the .tar.gz and the .deb (below)
```

By hand: `cmake -S . -B build/linux -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
-DCMAKE_LINKER_TYPE=LLD -DCMAKE_BUILD_TYPE=RelWithDebInfo && ninja -C build/linux urban`.

What the CMake files do on Linux (phase 1):

- **Code model of the MSVC build** for every target (top-level `CMakeLists.txt`): `-msse4.1` (the runtime's SSE4.1
  intrinsics need the ISA enabled; MSVC takes them without `/arch`), `-fno-strict-aliasing` and `-fwrapv` (the
  recompiled code assumes MSVC's semantics), `-ffp-contract=off` (no FMA, as `/fp:precise`).
- **Generated code** (`src/CMakeLists.txt`): `-w`, no debug info by default (`URBAN_GENERATED_DEBUG_INFO=ON` adds it),
  `register_functions.cpp` at `-O0` (as `/Od`), `URBAN_ARCH=AVX2` = `-mavx2`. The huge sources keep their own library
  and job pool, as on Windows.
- **`urban`**: `-rdynamic` (names in crash backtraces), threads and `dl`; RPATH `$ORIGIN` so the libraries built next
  to it (SDL3, later the PCSX2 GS) are found. The 16 MB stack of `/STACK` is set at run time (phase 2).
- **raylib's GLFW** is X11 only. **SDL3** is built for the controllers only (no video: `SDL_VIDEO=OFF`,
  `SDL_UNIX_CONSOLE_BUILD=ON`), into `libSDL3.so` next to `urban`. **FFmpeg** is off by default (`URBAN_FFMPEG=ON`
  uses the system's through pkg-config).
- Portability fixes the MSVC build did not need: `iop_pad.h` includes `<cstddef>`; `state_io.h` loads an
  `std::optional` with `emplace(T{})` (Clang/libstdc++ can see a nested struct with member initializers as not
  default-constructible).
- Time: the whole generated code (9,900 build steps, the huge files included) compiles in about 4 minutes with
  Clang 19 on 12 threads (over an hour with MSVC).

## Host code on Linux (phase 2)

`urban` built from the same sources as `urban.exe`; what differs:

- **Game settings that were Windows only.** `main.cpp` set several of the game's defaults inside `#if _WIN32`: the
  game's own MCMAN/MCSERV over the emulated card (`PS2X_IOP_HLE_MCSERV=0`), `PS2X_SIO2_RECV1=2` (pads),
  `PS2X_EE_LOOP_CYCLES=16`, `PS2X_GS_DIRECT_PRESENT=1`, `PS2X_GS_ASYNC_READBACK=all` and the shader cache folder. They
  apply on every platform now.
- **Stack.** `/STACK:16777216` is the default stack of every thread of `urban.exe` (the game runs on a `std::thread`).
  On Linux `pthread_setattr_default_np` gives the threads created after start-up the same 16 MB.
- **CPU check and crash report.** SSE4.1/AVX2 through `__builtin_cpu_supports` (message on the console); fatal signals
  and `std::terminate` print a backtrace (`backtrace_symbols_fd`, names from `-rdynamic`) and exit with 3; the main
  thread has an alternate signal stack. `executableDirectory()` reads `/proc/self/exe`.
- **Mods.** `mods/<Name>/<Name>.so` through `dlopen`/`dlsym`. A crash inside a mod stops that mod, not the game, as with
  the structured exceptions on Windows: a `SIGSEGV`/`SIGBUS`/`SIGILL`/`SIGFPE` while a mod's code runs jumps back
  (`sigsetjmp`); any other fault goes on to the crash report.
- **Thread names** (`ThreadNaming.h`): at most 15 characters and NUL-terminated, as `pthread_setname_np` needs.

### Renderer without PCSX2's GS

The port's own GPU GS shares OpenGL textures between contexts through WGL only (`gs_gl.cpp`): on Linux it does not
start and nothing was drawn. When `libpcsx2gs.so` is missing (or `[Video] Renderer=native`), Linux falls back to the
**software GS** (`PS2X_GS_GPU=0`): correct image, but 2-3 images per second on screen and ~64% game speed; the window
then also reads the keyboard only that often (a quick tap can be missed). A stand-in only.

### MTVU with the software GS

With MTVU on (the Windows default), the intro movie stopped after its first image: the game thread waits at
`0x1298a0` for the frame-done flag that the GS interrupt handler sets on a SIGNAL, and no GS interrupt comes any more
(`PS2X_PACING_STATS=1`: `gsWake=0 gsAtEntry=0`; `PS2X_GS_SIGNAL_LOG=2` shows the last SIGNALs being delivered and
cleared, then nothing). The VU and GIF threads are idle, nobody is stuck in a lock (gdb). It is the SIGNAL/SIGLBLID
area `gs_handler_exclusive` already fixed on Windows, where MTVU only ever ran with the GPU or PCSX2 GS. With PCSX2's GS
on Vulkan (phase 4) MTVU runs as on Windows (intro, menus, fights at 60, no `vblank waited`): it is the software GS's
timing, not Linux. So on Linux MTVU is on by default together with PCSX2's GS and off with the software GS
(`PS2X_MTVU=1/0` still decides).

### Later (performance)

- `host_vsync.cpp` (the emulated vblank locked to the monitor's, D3DKMT) has no Linux version: the clock runs free.
- Thread priorities (`ThreadPriority.h`) are not raised on Linux (it needs `CAP_SYS_NICE`).

## PCSX2's GS on Vulkan (phase 3)

`thirdparty/pcsx2-gs` builds `build/linux/bin/libpcsx2gs.so` (with `urban`, or `ninja -C build/linux pcsx2gs`), next
to `resources/` (the shaders) and `shadercache_pcsx2/`. Same C interface as `pcsx2gs.dll` (`include/pcsx2gs_api.h`);
on Linux `Pcsx2GsOpenParams::hwnd` is the X11 window (XID) to render into.

- **Sources** (`thirdparty/README.md`): the Vulkan device of the same PCSX2 commit, `common/Linux`, the Vulkan headers
  and VulkanMemoryAllocator. `compat/LnxMisc.cpp` is PCSX2's `LnxMisc.cpp` without dbus/XInput2.
- **Shaders**: `resources/shaders/vulkan` (GLSL), compiled at run time by shaderc's shared library, loaded with dlopen:
  `libshaderc_shared.so.1`, or `libshaderc.so.1` (Debian/Ubuntu; the one local change in a PCSX2 file). Cache:
  `shadercache_pcsx2/vulkan_shaders.bin` and `vulkan_pipelines.bin`. The shipped cache (`shadercache/`) is DX11 only:
  the first run on Linux compiles the shaders as the game uses them.
- **HD textures**: PNG through libpng (PCSX2's own handlers, `compat/PngLibpng.inl`, written by
  `tools/make_loaders.py`; Windows keeps WIC, `compat/PngLoader.inl` picks); DDS as on Windows.
- **Bridge** (`compat/Bridge.cpp`): `GSRendererType::VK` with `WindowInfo::Type::X11`; the library opens its own X11
  connection on the GS thread (never shares GLFW's). `PS2X_PRESENT_STATS` (DXGI frame statistics) is Windows only.
- **Library hygiene**: built with `-fvisibility=hidden`, linked with `--no-undefined` (a missing host stub is a link
  error), `--exclude-libs,ALL` and `-Bsymbolic`: it exports only `pcsx2gs_get_api` and `pcsx2gs_set_aspect`, so its
  ImGui/fmt never bind to urban's.
- **New host functions** the Vulkan device needs: `Host::ReportFormattedErrorAsync` (`compat/HostStubs.cpp`) and
  `GSOptions::TriStateToOptionalBoolean` (`tools/extract_config.py` → `compat/Pcsx2ConfigGS.cpp`).
- Also fixed: the post-build copy of the shader cache broke on paths with spaces (`VERBATIM`).

**Device test**: `build/linux/bin/pcsx2_gs_test_x11 [scale] [seconds] [snapshot.bmp]` opens an X11 window, loads
`libpcsx2gs.so` through the C interface as urban will, and presents test/GSTest.cpp's scene (blue background, red and
green sprites). GTX 1660 SUPER, driver 610: correct image at 2x (1280x896), 75 presents a second with FIFO vsync
(the monitor's rate).

## PCSX2's GS in the game (phase 4)

`gs_pcsx2_backend.cpp` on Linux: `libpcsx2gs.so` next to `urban` is loaded with `dlopen` (`[Video] Renderer=pcsx2`, the
default), and the GS renders into an **X11 child window** of the game window, as the Win32 child window on Windows:

- raylib's `GetWindowHandle()` is the `GLFWwindow*` on Linux; `glfwGetX11Window` (raylib's GLFW is built with X11) gives
  the parent. The child is created on the main thread's own X11 connection, covers the client area (resized in
  `HostTick`, hidden while the F1 debugger is open) and selects no input: pointer events go on to the game window,
  the keyboard focus stays there. The library makes its Vulkan surface on its own connection, on the GS thread.
- **Presentation**: `PS2X_PCSX2_VSYNC` defaults to 0 (immediate) on Linux. NVIDIA offers no mailbox for an X11
  window under XWayland, so the Windows default (mailbox) became FIFO: the GS thread waited for the compositor and a
  4-fighter fight ran at ~52 frames/s (minimum 30). Immediate gives 59.9; in a composited window the desktop shows the
  latest image, as mailbox would. `PS2X_PCSX2_VSYNC=1` (FIFO) / `2` (mailbox) in `config.ini [Runtime]` still apply.
- **IOP thread**: it now sleeps on a futex where Windows uses `WaitOnAddress` (`iop_subsystem.cpp`; the busy flag is a
  32-bit word on Linux). The old spin policy, which Linux used before, cost a whole core (31% of all samples in
  `perf`) and is still there with `PS2X_IOP_SPIN=1`. The thread is named `IOPThread`.

Measured on the repository's save states (`bench/states`, saved on Windows; they load on Linux), window 640x480, 1x,
15 s after the load (Ryzen 5 3600, GTX 1660 SUPER, driver 610):

| state | frames/s (median, min) | game thread | VU | IOP | GS |
|---|---|---|---|---|---|
| fight1v1 | 59.9 (60, 59) | 43% | 23% | 26% | 18% |
| fight4_heavy | 59.9 (60, 59) | 46% | 28% | 26% | 20% |
| fight8_heli | 59.1 (60, 57) | 67% | 53% | 32% | 27% |
| select4 | 59.9 (60, 59) | 37% | 21% | 22% | 19% |

On Windows `fight4_heavy` runs at 60.0 with the game thread at 58.5% (docs/PROGRESS.md).

## build.py, the recompiler and the release (phase 5)

**build.py** picks the platform: `build/linux`, Clang + Ninja (+ LLD when there), no Visual Studio environment, `urban`,
`pgrep`/`xdg-open`. `URBAN_CC`/`URBAN_CXX` choose other compilers, `URBAN_JOBS` the parallel jobs (default: all
threads). `tools/autotest/bench_state.py` works too (capture with ImageMagick `import`, `--threads` from `/proc`).

**Recompiler**: `ps2_recomp` builds on Linux (`-DURBAN_BUILD_RECOMP=ON`, what `build.py generate` does) and
`tools/generate.py` uses `build/linux/runtime/ps2xRecomp/ps2_recomp` (never the Windows `.exe`). From the same disc it
writes the same 9,881 files as the Windows one, byte for byte (line endings aside).

**Player's folder** (`URBAN_USER_DIR`, `src/main.cpp`): an installed copy cannot write next to its executable. When
`URBAN_USER_DIR` is set, config.ini, controls.ini, memcards, states, game_data, the PCSX2 GS's shader cache and HD
textures (`PS2X_DATA_DIR`), the disc image (`<dir>/game`) and the player's mods (`<dir>/mods`, before the ones installed
with the game; a mod's settings.ini is always written there) use it. Unset: the executable's folder, as before (the
Windows ZIP, the Linux .tar.gz).

**Release** (`python3 build.py release` = `tools/linux/release.py`; needs podman or docker):

- The binaries are built in the **Steam Runtime 3 "sniper" SDK** container (`tools/linux/build_release.sh`, into
  `build/release-linux`): glibc 2.31, **g++ 14** (the container's Clang is 11, too old for the code) with a static
  libstdc++/libgcc. The whole generated code takes ~16 minutes there (the worst file 73 s, 1.6 GB). `librt` is linked
  for glibc < 2.34 (`shm_open`).
- **shaderc** is built there too: the versions PCSX2 ships (shaderc 2026.2 and its glslang/SPIRV-Headers/SPIRV-Tools
  commits, checked by SHA-256) without PCSX2's patch (the stock C API, as Debian's), as `libshaderc_shared.so.1`.
- Shipped with the game (found through RPATH `$ORIGIN`): `libpcsx2gs.so`, `libSDL3.so.0`, `libshaderc_shared.so.1`,
  `libpng16.so.16`. From the system: glibc, X11 (libX11, Xrandr, Xext, Xrender), libGL, the Vulkan loader, zlib.
- `UrbanReignPC-<version>-linux-x86_64.tar.gz`: portable, as the Windows ZIP: `urban-reign` (start script),
  `install-shortcut.sh` (menu entry for that folder), `game/` (the binaries, `resources/`, `mods/`, `textures/`; the
  .iso can go there too).
- `urban-reign_<version>_amd64.deb`: `/usr/lib/urban-reign`, `/usr/bin/urban-reign`, the menu entry and icon; the
  player's files in `~/.local/share/urban-reign`. Built with the container's `dpkg-deb`.
- `tools/linux/urban-reign.sh` (both): with no disc image configured it asks for one with zenity or kdialog and
  writes it to `config.ini [Game] Iso` (after `urban --write-settings` made the file).
- The archives' modes and owners are set by `release.py` (root, 755 for binaries and scripts, 644 for the rest), not
  read from the staging folder: on NTFS every file looks executable. The .deb is written by Python (an `ar` archive of
  `control.tar.xz` and `data.tar.xz`), so packaging needs no Debian tools either.

Checked on 2026-10-22 (version 1.3.0: `.tar.gz` 29.5 MB, `.deb` 20.1 MB):

- The binaries need glibc 2.30 and no system libstdc++; `ldd` finds every library on Ubuntu 20.04 (glibc 2.31) and
  Debian 12 (the .deb installed with `apt`, dependencies resolved, `~/.local/share/urban-reign` created by the start
  script) and on Fedora 44 and Arch (the .tar.gz; their container images only lacked libX11, which every desktop has).
- On this machine (GNOME/Wayland, NVIDIA): the extracted .tar.gz runs through `urban-reign` with the shipped
  `libshaderc_shared.so.1`, libpng and SDL3 (`/proc/<pid>/maps`); fight4_heavy at 60.0 frames/s, fight8_heli at 59.7
  (the g++ build's game thread 75% there, Clang's 67%).
- Not checked yet: a Steam Deck (Gaming Mode runs games in gamescope; the GS's X11 child window there is untested).
