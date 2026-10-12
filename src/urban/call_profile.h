#pragma once

class PS2Runtime;

namespace urban::callprofile
{
    // URBAN_CALL_PROFILE=1: count calls to every recompiled function entry (wraps the dispatch table slots, so it
    // must run after all other bindings). Costs one extra indirect call per guest call.
    void installFromEnvironment(PS2Runtime &runtime);

    // Prints the most called guest functions since the previous report (no-op when profiling is off).
    void report(int top);
}
