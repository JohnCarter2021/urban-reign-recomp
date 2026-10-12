# textures/

HD texture packs in PCSX2's format go here, in a folder named after the game's serial:

```
textures/
└─ SLUS-21209/
   └─ replacements/   ← the pack's .png/.dds files
```

`urban.exe` finds `textures/SLUS-21209` next to itself or in this folder of the repository. To use a pack from
anywhere else (for example PCSX2's own `Documents/PCSX2/textures/SLUS-21209`), set `HDTexturesFolder` in
`config.ini`. `HDTextures=0` turns them off.

The packs are not part of this repository.
