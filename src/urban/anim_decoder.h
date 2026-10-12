#pragma once

class PS2Runtime;

namespace urban::animdecoder
{
    // Hooks sub_00292670 (animation track decoder) when one of its URBAN_ANIM_* options is set.
    void install(PS2Runtime &runtime);
}
