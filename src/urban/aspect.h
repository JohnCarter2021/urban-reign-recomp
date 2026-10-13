#pragma once

#include <cstdint>

// The picture's aspect ratio (config.ini [Video] AspectRatio, URBAN_ASPECT): "16:9" (default) and "4:3" set the
// game's own screen option (16:9 is a real widescreen: the camera shows more of the scene), "auto" follows its
// Options menu. [Video] Stretch=1 (PS2X_PCSX2_STRETCH) fills the window instead. The output size: PCSX2's GS only.
namespace urban::aspect
{
    void install(uint8_t *rdram);
}
