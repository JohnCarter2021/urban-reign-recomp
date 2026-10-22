"""The Linux release (python build.py release on Linux; docs/LINUX.md): a portable .tar.gz and a .deb.

The binaries are built in the Steam Runtime 3 "sniper" SDK container (tools/linux/build_release.sh: g++ 14, static
libstdc++, glibc 2.31; they need 2.30), so they run on current distros and on SteamOS; the host only needs podman or docker.

    python tools/linux/release.py               build in the container, then package
    python tools/linux/release.py --no-build    package what build/release-linux already has

Output in build/release:
  UrbanReignPC-<version>-linux-x86_64.tar.gz   portable folder, as the Windows ZIP:
      urban-reign                                the start script (asks for the disc image the first time)
      install-shortcut.sh                        adds Urban Reign to the applications menu
      game/                                      urban, libpcsx2gs.so, the libraries it brings, resources/, mods/ (CoopTool
                                                 and ExampleMod, off until switched on), textures/
      README.txt, LICENSE.txt, THIRD-PARTY.txt, urban-reign.png
  urban-reign_<version>_amd64.deb               /usr/lib/urban-reign, /usr/bin/urban-reign, menu entry and icon; the
                                                player's files in ~/.local/share/urban-reign
Nothing the game writes at run time and nothing from the disc.
"""
import argparse
import importlib.util
import io
import shutil
import subprocess
import sys
import tarfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
IMAGE = "registry.gitlab.steamos.cloud/steamrt/sniper/sdk:latest"
OUT = ROOT / "build" / "release-linux"
BIN = OUT / "build" / "bin"
RELEASE = ROOT / "build" / "release"


def version() -> str:
    """The version of the Windows release script (tools/release.py), so both releases carry the same number."""
    spec = importlib.util.spec_from_file_location("windows_release", ROOT / "tools" / "release.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.VERSION


VERSION = version()
NAME = f"UrbanReignPC-{VERSION}-linux-x86_64"

# What goes in game/ (the .deb: /usr/lib/urban-reign): file in the release <- file built.
GAME_FILES = {
    "urban": BIN / "urban",
    "libpcsx2gs.so": BIN / "libpcsx2gs.so",
    "libSDL3.so.0": BIN / "libSDL3.so.0",
    "libshaderc_shared.so.1": OUT / "deps" / "shaderc" / "lib" / "libshaderc_shared.so.1",
    "libpng16.so.16": OUT / "deps" / "libpng" / "libpng16.so.16",
}

README = f"""URBAN REIGN - PC PORT {VERSION} (Linux)
=============================================
Created by / Criado por thiagordsfx
GitHub:  https://github.com/thiagordsfx
YouTube: https://www.youtube.com/@thiagordsfx

EN
- Run "urban-reign" (from a file manager or a terminal). The first time it asks for your own Urban Reign disc image
  (USA, SLUS-21209, .iso); the game is not included. Or put the .iso in game/.
- ./install-shortcut.sh adds Urban Reign to the applications menu (this folder stays where it is).
- Settings: game/config.ini ([Video] InternalResolution, AspectRatio, Renderer...) and game/controls.ini (controllers,
  PCSX2 format). Both are written with comments the first time the game runs.
- HD textures: put a PCSX2 texture pack in game/textures/SLUS-21209 (the folder with "replacements").
- Mods: each mod is a folder in game/mods; switch them on in game/config.ini, [Mods] Dlls=CoopTool (several:
  Dlls=CoopTool;ExampleMod); their options are in the in-game menu (F2). CoopTool comes with it: Multiplayer with up
  to 8 fighters (up to 8 humans) and Free Mode missions with up to 4 players. For 6 to 8 players switch the multitap on
  in both ports (game/controls.ini, [Pad] MultitapPort1 = true).
- In game: F11 fullscreen, F3 FPS counter, F5/F9 quick save/load state, F2 in-game menu.
- Needs a 64-bit Linux with glibc 2.30 or newer (Ubuntu 20.04+, Debian 11+, Fedora, Arch, SteamOS...), a Vulkan
  graphics driver and a CPU with SSE4.1. The game window is X11 (XWayland on Wayland desktops).
- Steam Deck: in Desktop Mode run urban-reign once to pick the disc image; then add urban-reign to Steam as a
  non-Steam game.

PT
- Rode o "urban-reign" (pelo gerenciador de arquivos ou terminal). Na primeira vez ele pede a sua imagem do Urban Reign
  (EUA, SLUS-21209, .iso); o jogo não vem junto. Ou coloque o .iso em game/.
- ./install-shortcut.sh coloca o Urban Reign no menu de aplicativos (esta pasta fica onde está).
- Configurações: game/config.ini ([Video] InternalResolution, AspectRatio, Renderer...) e game/controls.ini (controles,
  formato do PCSX2). Os dois são criados com comentários na primeira vez que o jogo roda.
- Texturas HD: coloque um pacote do PCSX2 em game/textures/SLUS-21209 (a pasta com "replacements").
- Mods: cada mod é uma pasta em game/mods; ligue no game/config.ini, [Mods] Dlls=CoopTool (vários:
  Dlls=CoopTool;ExampleMod); as opções ficam no menu do jogo (F2). O CoopTool já vem junto: Multiplayer com até 8
  lutadores (até 8 humanos) e missões do Free Mode com até 4 jogadores. Para 6 a 8 jogadores ligue o multitap nas duas
  portas (game/controls.ini, [Pad] MultitapPort1 = true).
- No jogo: F11 tela cheia, F3 contador de FPS, F5/F9 salvar/carregar estado rápido, F2 menu do jogo.
- Precisa de Linux 64 bits com glibc 2.30 ou mais nova (Ubuntu 20.04+, Debian 11+, Fedora, Arch, SteamOS...), driver
  de vídeo com Vulkan e processador com SSE4.1. A janela do jogo é X11 (XWayland em desktops Wayland).
- Steam Deck: no Modo Desktop rode o urban-reign uma vez para escolher a imagem; depois adicione o urban-reign na Steam
  como jogo não-Steam.

The .deb (Debian, Ubuntu, Mint...) installs the same files in /usr/lib/urban-reign and keeps your files in
~/.local/share/urban-reign (put the .iso in its game/ folder, or pick it the first time; config.ini and controls.ini are
there too, and your own mods go in its mods/ folder).
O .deb (Debian, Ubuntu, Mint...) instala os mesmos arquivos em /usr/lib/urban-reign e guarda os seus arquivos em
~/.local/share/urban-reign (coloque o .iso na pasta game/ de lá, ou escolha na primeira vez; o config.ini e o
controls.ini ficam lá também, e os seus próprios mods vão na pasta mods/ de lá).

Fan project, not affiliated with Bandai Namco. / Projeto de fãs, sem ligação com a Bandai Namco.
"""

THIRD_PARTY = """Third-party software in this release
- PCSX2 GS renderer (libpcsx2gs.so): GPL-3.0-or-later, https://pcsx2.net; with Vulkan headers and
  VulkanMemoryAllocator (Apache-2.0 / MIT), fmt (MIT), Dear ImGui (MIT), cpuinfo (BSD-2-Clause), xbyak (BSD-3-Clause)
- PS2Recomp runtime: see LICENSE.txt, https://github.com/ran-j/PS2Recomp
- SDL3 (libSDL3.so.0): zlib license, https://libsdl.org
- shaderc (libshaderc_shared.so.1): Apache-2.0, https://github.com/google/shaderc; with glslang (BSD-3-Clause and
  others), SPIRV-Tools and SPIRV-Headers (Apache-2.0 / MIT)
- libpng (libpng16.so.16): libpng license, http://www.libpng.org
- raylib and its GLFW: zlib license; Dear ImGui: MIT
"""

MODS_NOTE = "Mods go here, one folder each (the mod's .so and mod.json); switch them on in config.ini [Mods] Dlls=<Name>.\n" \
            "Their options are in the in-game menu (F2). How to make one: modsdk/ in the source repository.\n" \
            "Cada mod fica numa pasta aqui (o .so e o mod.json); ligue no config.ini [Mods] Dlls=<Nome>.\n" \
            "As opções ficam no menu do jogo (F2). Como criar: modsdk/ no repositório do código.\n"
TEXTURES_NOTE = "HD texture packs go in SLUS-21209/ (PCSX2 format: SLUS-21209/replacements/...).\n" \
                "Pacotes de texturas HD vão em SLUS-21209/ (formato do PCSX2: SLUS-21209/replacements/...).\n"

INSTALL_SHORTCUT = """#!/bin/sh
# Adds Urban Reign (this folder) to the applications menu: ~/.local/share/applications/urban-reign.desktop.
HERE=$(dirname "$(readlink -f "$0")")
APPS="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
mkdir -p "$APPS"
cat > "$APPS/urban-reign.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=Urban Reign
Comment=Urban Reign PC port
Exec="$HERE/urban-reign"
Icon=$HERE/urban-reign.png
Terminal=false
Categories=Game;ActionGame;
EOF
echo "Added: $APPS/urban-reign.desktop"
"""

# glibc 2.30 is the newest symbol version the binaries use; raylib's GLFW loads libGL and the X11 extensions with dlopen,
# PCSX2's GS the Vulkan loader.
DEB_DEPENDS = ("libc6 (>= 2.30), libx11-6, libxrandr2, libxcursor1, libxi6, libxinerama1, libxext6, libgl1, libvulkan1, "
               "zlib1g")


def run(cmd, **kwargs):
    print("+", " ".join(str(c) for c in cmd), flush=True)
    return subprocess.run([str(c) for c in cmd], **kwargs).returncode


def container_tool() -> str:
    for tool in ("podman", "docker"):
        if shutil.which(tool):
            return tool
    sys.exit("The Linux release is built in the Steam Runtime container: install podman (or docker).")


def in_container(tool: str, *cmd) -> int:
    return run([tool, "run", "--rm", "-v", f"{ROOT}:/src", "-w", "/src", IMAGE, *cmd])


def copy_executable(source: Path, target: Path):
    """Copies a binary without its debug info (strip)."""
    shutil.copy2(source, target)
    if shutil.which("strip"):
        subprocess.run(["strip", "--strip-debug", str(target)], check=False)


# The archives get their modes and owner from here, not from the staging folder: the repository may be on a file system
# without Unix permissions (NTFS), where every file reads 755 or 644 whatever chmod did.
EXECUTABLES = {"urban", "urban-reign", "install-shortcut.sh"}


def normalized(info: tarfile.TarInfo) -> tarfile.TarInfo:
    name = info.name.rsplit("/", 1)[-1]
    if info.isdir() or name in EXECUTABLES or ".so" in name:
        info.mode = 0o755
    else:
        info.mode = 0o644
    info.uid = info.gid = 0
    info.uname = info.gname = "root"
    info.mtime = int(time.time())
    return info


def stage_game(game: Path):
    game.mkdir(parents=True)
    for name, source in GAME_FILES.items():
        if not source.exists():
            sys.exit(f"missing {source}: build first (python tools/linux/release.py without --no-build)")
        copy_executable(source, game / name)
        print("  " + str((game / name).relative_to(game.parent)))
    shutil.copytree(BIN / "resources", game / "resources")
    # The mods built with the game (mods/ and the mod kit's example): off until the player switches them on.
    (game / "mods").mkdir()
    for folder in sorted((BIN / "mods").glob("*/")) if (BIN / "mods").is_dir() else []:
        library = folder / f"{folder.name}.so"
        if not library.exists() or not (folder / "mod.json").exists():
            continue
        (game / "mods" / folder.name).mkdir()
        copy_executable(library, game / "mods" / folder.name / library.name)
        shutil.copy2(folder / "mod.json", game / "mods" / folder.name / "mod.json")
        print(f"  {game.name}/mods/{folder.name}/")


def make_tarball() -> Path:
    staging = RELEASE / "linux-staging"
    shutil.rmtree(staging, ignore_errors=True)
    out = staging / NAME
    game = out / "game"
    stage_game(game)
    (game / "mods" / "README.txt").write_text(MODS_NOTE, encoding="utf-8")
    (game / "textures").mkdir()
    (game / "textures" / "README.txt").write_text(TEXTURES_NOTE, encoding="utf-8")
    (game / "textures" / "SLUS-21209").mkdir()
    shutil.copy2(HERE / "urban-reign.sh", out / "urban-reign")
    (out / "install-shortcut.sh").write_text(INSTALL_SHORTCUT, encoding="utf-8")
    shutil.copy2(HERE / "urban-reign.png", out / "urban-reign.png")
    (out / "README.txt").write_text(README, encoding="utf-8")
    (out / "THIRD-PARTY.txt").write_text(THIRD_PARTY, encoding="utf-8")
    shutil.copy2(ROOT / "LICENSE", out / "LICENSE.txt")
    archive = RELEASE / f"{NAME}.tar.gz"
    with tarfile.open(archive, "w:gz") as tar:
        tar.add(out, arcname=NAME, filter=normalized)
    shutil.rmtree(staging)
    return archive


def ar_member(name: str, data: bytes) -> bytes:
    """One member of an ar archive (the .deb container format)."""
    header = f"{name:<16}{int(time.time()):<12}{0:<6}{0:<6}{'100644':<8}{len(data):<10}`\n".encode("ascii")
    return header + data + (b"\n" if len(data) % 2 else b"")


def tar_xz(add) -> bytes:
    buffer = io.BytesIO()
    with tarfile.open(fileobj=buffer, mode="w:xz") as tar:
        add(tar)
    return buffer.getvalue()


def make_deb() -> Path:
    staging = RELEASE / "deb-staging"
    shutil.rmtree(staging, ignore_errors=True)
    stage_game(staging / "usr" / "lib" / "urban-reign")
    (staging / "usr" / "bin").mkdir(parents=True)
    shutil.copy2(HERE / "urban-reign.sh", staging / "usr" / "bin" / "urban-reign")
    applications = staging / "usr" / "share" / "applications"
    applications.mkdir(parents=True)
    shutil.copy2(HERE / "urban-reign.desktop", applications / "urban-reign.desktop")
    icons = staging / "usr" / "share" / "icons" / "hicolor" / "256x256" / "apps"
    icons.mkdir(parents=True)
    shutil.copy2(HERE / "urban-reign.png", icons / "urban-reign.png")
    doc = staging / "usr" / "share" / "doc" / "urban-reign"
    doc.mkdir(parents=True)
    shutil.copy2(ROOT / "LICENSE", doc / "copyright")
    (doc / "README.txt").write_text(README, encoding="utf-8")
    (doc / "THIRD-PARTY.txt").write_text(THIRD_PARTY, encoding="utf-8")
    size_kb = sum(p.stat().st_size for p in staging.rglob("*") if p.is_file()) // 1024
    control = (
        "Package: urban-reign\n"
        f"Version: {VERSION}\n"
        "Architecture: amd64\n"
        "Maintainer: thiagordsfx <https://github.com/thiagordsfx>\n"
        f"Installed-Size: {size_kb}\n"
        f"Depends: {DEB_DEPENDS}\n"
        "Recommends: zenity | kdialog\n"
        "Section: games\n"
        "Priority: optional\n"
        "Homepage: https://github.com/thiagordsfx/urban-reign-recomp\n"
        "Description: Urban Reign PC port (needs your own PS2 disc image)\n"
        " Urban Reign (PS2) recompiled to run natively: 60 FPS, high internal resolution through PCSX2's\n"
        " GS on Vulkan, HD texture packs, up to 8 players. The game is not included: it asks for your own\n"
        " Urban Reign (USA, SLUS-21209) disc image. Fan project, not affiliated with Bandai Namco.\n").encode("utf-8")

    def add_control(tar):
        info = normalized(tarfile.TarInfo("./control"))
        info.size = len(control)
        tar.addfile(info, io.BytesIO(control))

    def add_data(tar):
        tar.add(staging, arcname=".", filter=normalized)

    # A .deb is an ar archive: debian-binary, control.tar.xz, data.tar.xz (what dpkg-deb --root-owner-group writes).
    deb = RELEASE / f"urban-reign_{VERSION}_amd64.deb"
    deb.write_bytes(b"!<arch>\n" + ar_member("debian-binary", b"2.0\n") + ar_member("control.tar.xz", tar_xz(add_control)) +
                    ar_member("data.tar.xz", tar_xz(add_data)))
    shutil.rmtree(staging)
    return deb


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--no-build", action="store_true", help="package what build/release-linux already has")
    args = parser.parse_args()
    if not (ROOT / "generated" / "register_functions.cpp").exists():
        sys.exit("The recompiled game code is missing: run python build.py generate first.")
    tool = container_tool()
    if not args.no_build:
        if run([tool, "pull", IMAGE]) or in_container(tool, "/src/tools/linux/build_release.sh"):
            return 1
    RELEASE.mkdir(parents=True, exist_ok=True)
    archive = make_tarball()
    deb = make_deb()
    for path in (archive, deb):
        print(f"{path} ({path.stat().st_size / (1024 * 1024):.1f} MB)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
