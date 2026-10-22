# Linux build (work in progress)

Branch `feat/linux-support`. Goal: `urban` built natively on Linux with Clang, rendering through PCSX2's GS on
**Vulkan** (the Windows build uses DX11), in an X11 window (XWayland on a Wayland desktop).

## Status

| phase | what | state |
|---|---|---|
| 0 | Environment: dependencies (`tools/linux/install_deps.sh`), line endings | done |
| 1 | Portable CMake: Clang/GCC flags, `build/linux`, `libpcsx2gs.so`, mods as `.so` | |
| 2 | Host code: `main.cpp` (game defaults outside `_WIN32`, stack, crash report), mod loader with `dlopen` | |
| 3 | PCSX2's GS on Vulkan: Vulkan renderer, `common/Linux`, shaders, PNG through libpng, `Bridge.cpp` | |
| 4 | Runtime ↔ GS window: `dlopen` of `libpcsx2gs.so`, X11 child window over the game window | |
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
