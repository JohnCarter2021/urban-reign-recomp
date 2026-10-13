#include "urban/aspect.h"

#include "runtime/gs/gs_aspect.h"
#include "runtime/ps2_pnach.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace urban::aspect
{
    namespace
    {
        // The screen settings ([0x34D394] = 0x34D220). +0x50 is the game's 16:9 option, saved with the profile:
        // sub_002016B8 copies it there, sub_00128880 clears it at boot. With it the camera (sub_00125958) multiplies
        // the horizontal scale of its projection by 0.8, parts of the HUD by 0.75 / 1.25 (sub_0012AE18, sub_001EA5D8...)
        // and the boot videos are the _wide ones (sub_001E1B48). +0x38 is the horizontal scale of the 4:3 picture (1.1).
        constexpr uint32_t kSettingsPtr = 0x0034D394u;
        constexpr uint32_t kWideOffset = 0x50u;
        constexpr uint32_t kScaleOffset = 0x38u;
        // The camera (sub_00125918 sets +0x288 = +0x2A0 * settings+0x38, sub_00125958 projects with +0x288 * 0.8).
        constexpr uint32_t kCamera = 0x0034D3A0u;
        constexpr uint32_t kCameraScale = 0x288u;
        constexpr uint32_t kCameraBase = 0x2A0u;
        // 16:9 is 3/4 of the width of 4:3: the camera's 0.8 makes the scene 7% too wide, so its scale is set to
        // 0.75 / 0.8 of the 4:3 one (the HUD already uses 0.75).
        constexpr float kExactWide = 0.75f / 0.8f;
        constexpr uint32_t kRamMask = 0x01FFFFFFu;

        const char *name(ps2x::gs::Aspect aspect)
        {
            return aspect == ps2x::gs::Aspect::R16_9 ? "16:9" : aspect == ps2x::gs::Aspect::Stretch ? "fill the window" : "4:3";
        }

        uint32_t read32(const uint8_t *rdram, uint32_t address)
        {
            uint32_t value = 0;
            std::memcpy(&value, rdram + (address & kRamMask), sizeof(value));
            return value;
        }

        float readFloat(const uint8_t *rdram, uint32_t address)
        {
            float value = 0.0f;
            std::memcpy(&value, rdram + (address & kRamMask), sizeof(value));
            return value;
        }

        void writeFloat(uint8_t *rdram, uint32_t address, float value)
        {
            std::memcpy(rdram + (address & kRamMask), &value, sizeof(value));
        }
    }

    void install(uint8_t *rdram)
    {
        const char *stretch = std::getenv("PS2X_PCSX2_STRETCH");
        const char *value = std::getenv("URBAN_ASPECT");
        std::string mode = value && *value ? value : "16:9";
        std::transform(mode.begin(), mode.end(), mode.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        const bool fill = stretch && *stretch == '1';
        // 16:9 (the default) and 4:3 set the game's option; "auto" leaves it to the game's Options menu.
        const int force = mode == "auto" ? -1 : mode == "4:3" ? 0 : 1;
        std::fprintf(stderr, "[aspect] %s%s\n", force < 0 ? "auto: as the game's 4:3 / 16:9 option" : force ? "16:9" : "4:3",
                     fill ? ", the picture fills the window (Stretch=1)" : "");
        if (!rdram)
            return;
        // Every frame: the game clears the option at boot and sets it again from the profile, and the GS opens after
        // the first frames with its own setting.
        ps2x::pnach::addFrameHook([rdram, fill, force, last = -1]() mutable
        {
            const uint32_t settings = read32(rdram, kSettingsPtr);
            int wide = 0;
            if (settings)
            {
                if (force >= 0)
                    std::memcpy(rdram + ((settings + kWideOffset) & kRamMask), &force, sizeof(force));
                wide = read32(rdram, settings + kWideOffset) ? 1 : 0;
                const float base = readFloat(rdram, kCamera + kCameraBase);
                const float scale = readFloat(rdram, settings + kScaleOffset);
                if (std::isfinite(base) && std::isfinite(scale) && base > 0.0f && scale > 0.0f)
                    writeFloat(rdram, kCamera + kCameraScale, base * scale * (wide ? kExactWide : 1.0f));
            }
            const ps2x::gs::Aspect aspect = fill ? ps2x::gs::Aspect::Stretch : wide ? ps2x::gs::Aspect::R16_9 : ps2x::gs::Aspect::R4_3;
            ps2x::gs::setAspect(aspect);
            if (wide != last)
            {
                last = wide;
                std::fprintf(stderr, "[aspect] the game's screen is %s\n", wide ? "16:9" : "4:3");
            }
        });
    }
}
