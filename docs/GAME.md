# Sobre o jogo (referência técnica)

- **Executável**: `SLUS_212.09` (NTSC-U), SHA-256 `60399090fc69ffdb4c39afd30977ab50890b0e33ed6bd9374e4d9497f4246081`.
- **SDK da Sony 2.8** (assinaturas `PsIIlib*` no ELF, nesta ordem de linkagem): `libdma`, `libgraph`, `libpkt`,
  `libpad`, `libmtap`, `libsdr`, `libcdvd`, `libscf`, `libipu`, `libmpeg`, `libmc` (2.8.1), `libpc`, `libkernl`;
  depois a libc/libm do GCC.
- **Middleware CRI**: ADX (áudio, streaming no IOP via ADMA do SPU2), Sofdec/MWSFD (vídeo pelo IPU), AFS
  (`DVD000.AFS` e `DVD001.AFS`).
- **IRX**: `SIO2MAN`, `MTAPMAN`, `PADMAN`, `MCMAN`, `MCSERV`, `LIBSD`, `SDRDRV`, `NUSOUND` (driver próprio),
  `CRI_ADXI`, e o reboot com `IOPRP280.IMG`.
- **Renderização**: microprogramas do VU1 (`.vutext`, 0x4720 bytes, ~50 overlays `.DVP`); skinning em 0x1D98, cena
  em 0x3568 (endereços do microcódigo carregado durante a luta).
- **VU0**: microcódigo carregado por um pacote MPG em 0x34aefc (172 instruções): seno (0x000, 0x038), atan2 (0x0F0),
  transformação com perspectiva (0x360), matriz × ponto (0x3C8, 0x438). Desmonte com `tools/vu_disasm.py`.
- **Controles**: libpad (RPC 0x80000100) com multitap (libmtap, RPC 0x80000901..905); o jogo abre 8 portas/slots.
- **Memory card**: libmc 2.8.1 com o MCSERV antigo (RPC 0x80000400).
