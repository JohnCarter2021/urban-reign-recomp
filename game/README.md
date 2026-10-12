# game/

Put your own **Urban Reign (USA)** disc image here (`SLUS-21209`, `.iso`). It is not part of this repository and is
never uploaded.

- `python build.py generate` reads it to recompile the game code (it checks the SHA-256 of `SLUS_212.09`).
- `urban.exe` reads the game data from it at every start.

Any file name works; the image is recognized by its contents.
