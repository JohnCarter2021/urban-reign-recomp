#pragma once

class PS2Runtime;

namespace urban::calltrace
{
    // URBAN_CALL_TRACE=0x13b428,0x128880,...: log every entry into those guest functions ($a0-$a3, $ra).
    // URBAN_WATCH=0x34d398:232[,...]: on each traced entry also print the byte at *(u32*)0x34d398 + 232 ("ptr:offset"
    // dereferences ptr; "=addr" prints the byte at addr itself).
    // URBAN_WATCH_POLL=0x34d398:232: a host thread polls that byte continuously and logs each change together with
    // the live guest PC (generated code stores ctx->pc before every instruction, so it points at or near the writer).
    // URBAN_HW_WATCH=0x34d308[,len]: x64 debug-register write watchpoint on that guest address (len 1/2/4/8), armed on
    // the GameThread. Each hit logs the exact guest PC and the new value. Windows only.
    void installFromEnvironment(PS2Runtime &runtime);
}
