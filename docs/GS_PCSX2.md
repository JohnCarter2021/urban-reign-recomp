# Troca do back end do GS pelo renderizador do PCSX2 — estudo de viabilidade

Escrito em 2026-10-11 (ROADMAP §7.2). Fonte de referência: `H:\JOGOS\pcsx2-ref` (clone raso de
github.com/PCSX2/pcsx2, GPL-3; o projeto é GPL, compatível). Desde 2026-10-12 (PROGRESS §90) o build usa só os
arquivos necessários, copiados para `thirdparty/pcsx2` (commit `2954ebe`, ver `thirdparty/README.md`); o wrapper
está em `thirdparty/pcsx2-gs`.

## Por que trocar

O nosso GS emula o GS pixel a pixel num fragment shader (VRAM do PS2 num buffer de 4 MB, interlock, variantes de
shader por estado). Resolução interna = supersampling: 2x = 4 amostras por pixel, 4x = 16. Com 8 lutadores o raster
da cena leva ~6 ms em 1x e ~13 ms em 2x (B1, PROGRESS §73); 4x ficaria em ~50 ms. Texturas HD não cabem no modelo
(a textura só existe na VRAM emulada de 4 MB). O renderizador de hardware do PCSX2 converte cada desenho do GS num
desenho nativo de GPU (texturas decodificadas e guardadas como texturas de verdade, Z e mistura do hardware, alvos de
desenho já na escala) e traz a substituição de texturas pronta.

## O que é o GS do PCSX2

- `pcsx2/GS/` (~88 mil linhas): `GSState` (estado e parser de GIF), `GSLocalMemory` (VRAM), `Renderers/HW`
  (`GSRendererHW`, `GSTextureCache`, `GSTextureReplacements`), `Renderers/SW` (software, opcional), e um device por API:
  `DX11`, `DX12`, `Vulkan`, `OpenGL` (`Metal` só no Mac).
- API (`GS/GS.h`), a mesma do antigo plugin GSdx:

  | função | uso |
  |---|---|
  | `GSopen(config, renderer, basemem)` | cria o device; `basemem` = bloco de 8 KB com os registradores privilegiados no layout do PS2 (PMODE 0x0000 ... CSR 0x1000) |
  | `GSgifTransfer(mem, size)` / `1/2/3` | pacotes GIF (qwords) |
  | `GSvsync(field, registers_written)` | fim de quadro: compõe a saída (circuitos, PMODE) e apresenta |
  | `GSwriteCSR(csr)` | escrita no CSR (reset, FLUSH etc.) |
  | `GSInitAndReadFIFO(mem, size)` | leitura GS → EE (TRXDIR=1; o flare do sol) |
  | `GSfreeze(mode, data)` | savestate |
  | `GSUpdateConfig(GSOptions)` | escala, filtros, correções por jogo, substituição de texturas |
  | `GSResizeDisplayWindow`, `GSPresentCurrentFrame` | janela |

- **SIGNAL, FINISH e LABEL não são do renderizador:** no `GSState` os handlers deles são `GIFRegHandlerNull`; quem
  trata é o núcleo do emulador (`pcsx2/GS.cpp`, `Gif_Unit`). Nós já tratamos isso no nosso caminho do GIF (MTVU,
  §43-§50), então essa lógica fica como está.
- Shaders: lidos de `bin/resources/shaders/{dx11,opengl,vulkan}` (`tfx.fx`, `convert.fx`, `merge.fx`...) e compilados
  em tempo de execução, com cache em disco. Poucas centenas de variantes pequenas (milissegundos cada).
- Compilação: C++20, MSVC v143 (o nosso VS 2022 serve).

## Dependências do código do GS (fora de `GS/`)

- `common/` (biblioteca utilitária do PCSX2: Console, StringUtil, Path, FileSystem, Error, Threading...): vem junto.
- `Config.h` (`Pcsx2Config::GSOptions`, a estrutura de configuração do GS): vem junto; a parte de ler/gravar `.ini`
  do PCSX2 fica de fora (nós preenchemos a estrutura a partir do `config.ini`).
- Funções de host e da VM: `Host::*` (13 arquivos incluem `Host.h`: janela, mensagens, traduções), `VMManager` (5),
  `PerformanceMetrics` (2), `ImGuiManager`/`FullscreenUI` (textos na tela), `SaveState` (2), `MTGS` (1),
  `Counters` (1). O `pcsx2-gsrunner` (programa que roda só o GS, para dumps) mostra exatamente quais funções de host o
  GS precisa: nós fornecemos versões nossas (a maioria vazias).
- Terceiros: `fmt`, `imgui`, `glad`, `vulkan` (headers), `d3d12memalloc`, `xbyak` (renderizador por software),
  `lzma` (dumps do GS), `fast_float`, `cpuinfo`, `simpleini`; `winwil`/`winpixeventruntime` no Windows.

## Como liga no nosso runtime

| nosso hoje | com o GS do PCSX2 |
|---|---|
| `GSFrontend` interpreta o GIF e manda primitivas ao back end | o frontend só olha os GIFtags para SIGNAL/FINISH/LABEL/CSR (como hoje) e repassa os qwords crus a `GSgifTransfer` na GSThread |
| registradores privilegiados em `GSRegisters` | espelhados num bloco de 8 KB no layout do PS2 (`basemem`) |
| `GSGpuBackend::Present` + textura compartilhada + raylib | `GSvsync` na GSThread; o PCSX2 apresenta numa janela-filha que cobre a área do jogo (raylib continua dono da janela, entrada e áudio) |
| leitura assíncrona do flare (§50, §72) | `GSInitAndReadFIFO`; o cache assíncrono do threaded backend continua valendo |
| savestate (`GSBackendState`) | `GSfreeze` dentro da seção do GS no nosso savestate |
| `config.ini` `InternalResolution` | `GSOptions.UpscaleMultiplier`, mais renderizador, filtros, correções e texturas |
| contador de FPS e mensagens (raylib) | OSD do ImGui do próprio PCSX2 |

O nosso GS atual continua no código como opção (`[Video] Renderer=` no `config.ini`), útil como referência exata
em 1x.

## Fases

1. **Biblioteca `pcsx2_gs`:** compilar `GS/` + `common/` + terceiros num alvo CMake nosso, só DX11 primeiro (o mais
   simples e o mais estável no Windows; DX12/Vulkan depois), com as funções de host/VM fornecidas por nós. Critério:
   linka e abre o device numa janela de teste. **Feita** (PROGRESS §81; `thirdparty/pcsx2-gs`).
2. **Ligação no runtime:** repasse do GIF, registradores privilegiados, vsync, CSR, leituras GS → EE, janela-filha.
   Critério: boot, menus e luta desenhando certo em 1x; comparar imagens com o nosso GS e com o PCSX2. **Feita**
   (PROGRESS §82; `PS2X_GS_RENDERER=pcsx2` / `Renderer=pcsx2`).
3. **Desempenho e escala:** luta de 8 em 1x/2x/4x/6x; critério de 60 liso (o PCSX2 faz 6x nessa cena na mesma
   placa com ~25% da thread do GS). **Feita** (PROGRESS §83: 60 fps de 1x a 8x, GSThread ~28%).
4. **Savestate, configuração no `config.ini`, lista de shaders pré-compilados.** Savestate feito na fase 2 (§82);
   GS do PCSX2 como padrão e filtros no `config.ini` (§85); OSD, shaders e cache de shaders no repositório (§86).
   **Feita.**
5. **Depois:** widescreen de verdade, texturas HD, Vulkan/DX12, teste na RX 580. Texturas HD feitas (PROGRESS §87).

## Correções do PCSX2 para o Urban Reign (GameIndex.yaml, `SLUS-21209`)

```yaml
gsHWFixes:
  alignSprite: 1          # Fixes vertical lines.     <- as faixas de 64 px do §72
  textureInsideRT: 1      # Fixes corruption.
  halfPixelOffset: 4      # Aligns post bloom.        <- o meio texel do §72
  nativeScaling: 2        # Fixes post effects.
  getSkipCount: "GSC_UrbanReign"
gameFixes: [EETimingHack]  # do núcleo do PCSX2, não se aplica a nós
```

Os dois primeiros problemas que achei no nosso upscale (§72) são exatamente os que o PCSX2 corrige para este jogo. As
correções vêm junto com o GS dele; nós as ligamos fixas na configuração.

## Riscos

- **Tamanho:** é o maior trabalho do projeto até agora (semanas). A fase 1 é a que mais pode surpreender (dependências
  do `common/` e da configuração).
- **Correções por jogo:** o PCSX2 aplica correções do GameDB para alguns jogos; o Urban não deve precisar de muitas,
  mas as que existirem para o `SLUS-21209` precisam ser copiadas para a nossa configuração.
- **Upscale:** os problemas típicos do PCSX2 (linhas, meio pixel, efeitos de tela cheia) também aparecem aqui; as
  correções dele (Half Pixel Offset, Round Sprite...) vêm junto e podem ser ligadas por jogo.
- **Ordem e sincronia:** o nosso MTVU/GIF tem regras de sincronia finas (§43-§52). O repasse dos qwords tem de
  acontecer na mesma ordem em que hoje o nosso frontend os interpreta.
- **Janela:** duas APIs na mesma janela (OpenGL do raylib + DX11 do PCSX2) não convivem bem; por isso a janela-filha.
