#pragma once

#include "ps2_runtime.h"

#include <cstdint>
#include <string_view>

namespace urban
{
    struct SdkFunction
    {
        uint32_t address;
        std::string_view name;
        std::string_view method;
        float score;
    };

    // SDK functions located by tools/find_sdk_functions.py.
    const SdkFunction *findSdkFunction(std::string_view name);
    const SdkFunction *sdkFunctionAt(uint32_t address);

    // Binds the located SDK functions whose work the runtime does at the API level (SIF/IOP, file I/O, CD/DVD,
    // MPEG/IPU, DECI2/TTY) to the runtime's handlers. URBAN_HLE_SKIP=name,name leaves those as recompiled code.
    void installHleBindings(PS2Runtime &runtime);
}
