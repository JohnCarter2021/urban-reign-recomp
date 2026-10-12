#pragma once

namespace urban::threadpolicy
{
    // PS2X_THREAD_POLICY=off|auto|spread|ccx (default auto): places the emulator's heavy threads on the CPU by the
    // topology read at run time (physical cores, SMT siblings, L3 groups/CCX, hybrid P/E cores), inside the process
    // affinity mask:
    // - GameThread alone on a physical core (not core 0, which takes most interrupts), priority above normal;
    // - VUThread, GIFThread, GSThread, the main (window/present) thread and IOPThread on other cores, SMT siblings only
    //   when the cores run out; the game core's sibling is left free.
    // auto = ideal processor hints (SetThreadIdealProcessorEx), VU/GIF first in the game core's L3 group, and hard
    // masks (one physical core) on the two OpenGL threads, MainThread and GSThread (PS2X_THREAD_HARD=<names>|none);
    // spread = the same, VU/GIF first in other L3 groups; ccx = hard affinity masks on every thread.
    // PS2X_THREAD_PRIORITY=0 keeps every thread at normal priority.
    // Call from the main thread before the runtime starts: it names it "MainThread" and starts a small thread that looks
    // for the named threads every second or two and applies the policy to new ones.
    void startFromEnvironment();
}
