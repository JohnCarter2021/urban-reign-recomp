# Linux build (work in progress)

Branch `feat/linux-support`. Goal: `urban` built natively on Linux with Clang, rendering through PCSX2's GS on
**Vulkan** (the Windows build uses DX11), in an X11 window (XWayland on a Wayland desktop).

## Status

| phase | what | state |
|---|---|---|
| 0 | Environment: dependencies (`tools/linux/install_deps.sh`), line endings | done |
| 1 | Portable CMake: Clang/GCC flags, `build/linux`, SDL3 and raylib (X11) on Linux; the runtime and the generated code compile | done |
| 2 | Host code: `main.cpp` (game defaults outside `_WIN32`, stack, crash report), mod loader with `dlopen`; boots to the menus with the software GS | done |
| 3 | PCSX2's GS on Vulkan (`libpcsx2gs.so`): Vulkan renderer, `common/Linux`, shaders, PNG through libpng, `Bridge.cpp` | |
| 4 | Runtime ↔ GS window: `dlopen` of `libpcsx2gs.so`, X11 child window over the game window; MTVU validated with it | |
| 5 | `build.py` on Linux | |
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

## Building (until `build.py` knows Linux, phase 5)

```sh
cmake -S . -B build/linux -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_LINKER_TYPE=LLD \
      -DCMAKE_BUILD_TYPE=RelWithDebInfo
ninja -C build/linux urban
```

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

### Renderer until phase 3

The port's own GPU GS shares OpenGL textures between contexts through WGL only (`gs_gl.cpp`): on Linux it does not
start and nothing was drawn. Until PCSX2's GS on Vulkan is there, Linux defaults to the **software GS**
(`PS2X_GS_GPU=0`): correct image, but 2-3 images per second on screen and ~64% game speed. A stand-in only.

### Open issue: MTVU with the software GS

With MTVU on (the Windows default), the intro movie stopped after its first image: the game thread waits at
`0x1298a0` for the frame-done flag that the GS interrupt handler sets on a SIGNAL, and no GS interrupt comes any more
(`PS2X_PACING_STATS=1`: `gsWake=0 gsAtEntry=0`; `PS2X_GS_SIGNAL_LOG=2` shows the last SIGNALs being delivered and
cleared, then nothing). The VU and GIF threads are idle, nobody is stuck in a lock (gdb). It is the SIGNAL/SIGLBLID
area `gs_handler_exclusive` already fixed on Windows, where MTVU only ever ran with the GPU or PCSX2 GS; whether the
software GS's timing or Linux causes it is open. So on Linux MTVU is on by default only together with PCSX2's GS
(`PS2X_MTVU=1/0` still decides); phase 4 checks it with the Vulkan renderer.

### Later (performance)

- The IOP thread waits with `WaitOnAddress` on Windows; elsewhere it uses the older spin + condition variable policy
  (more CPU, same behaviour). A futex/`std::atomic::wait` version would match Windows.
- `host_vsync.cpp` (the emulated vblank locked to the monitor's, D3DKMT) has no Linux version: the clock runs free.
- Thread priorities (`ThreadPriority.h`) are not raised on Linux (it needs `CAP_SYS_NICE`).
