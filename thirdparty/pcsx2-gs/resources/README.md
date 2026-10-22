# Recursos do GS do PCSX2

Copiados de `bin/resources` do PCSX2 (github.com/PCSX2/pcsx2, commit `2954ebe6965669219f1bbd256e958fbd8f83a85d`) e
copiados para `bin/resources` ao lado do `urban.exe` no build (`thirdparty/pcsx2-gs/CMakeLists.txt`).

- `shaders/dx11/`: shaders HLSL dos renderizadores DX11 e DX12 (lidos e compilados em tempo de execução, com cache em
  `bin/shadercache_pcsx2`). GPL-3, como o PCSX2.
- `shaders/common/`: `fxaa.fx` (FXAA, GPL-3 no PCSX2) e `ffx_a.h`/`ffx_cas.h` (AMD FidelityFX CAS, licença MIT no
  cabeçalho de cada arquivo).
- `fonts/Roboto-Regular.ttf`: fonte do OSD (Apache 2.0, ver `fonts/Roboto-Regular-copyright`).

- `shaders/vulkan/`: shaders GLSL do renderizador Vulkan (build do Linux, `docs/LINUX.md`), compilados em tempo de
  execução pelo shaderc, com cache em `shadercache_pcsx2/vulkan_*.bin`. GPL-3, como o PCSX2.

O GS do PCSX2 procura `shaders/dx11` (Windows) ou `shaders/vulkan` (Linux) e `shaders/common`; os de OpenGL ficaram de
fora.
