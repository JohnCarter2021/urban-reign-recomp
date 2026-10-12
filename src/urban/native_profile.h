#pragma once

#include <filesystem>

namespace urban::nativeprofile
{
    // URBAN_NATIVE_PROFILE=<start>:<duration>[:<interval_ms>] (seconds; default interval 1 ms): sampling profiler of
    // the host threads. From <start> seconds after launch, for <duration> seconds, every named thread (GameThread,
    // VUThread, GIFThread, GSThread, IOPThread...) is suspended briefly and its stack is walked. At the end the self and
    // inclusive time per function (recompiled sub_XXXXXXXX code and runtime functions alike) is printed to stderr.
    void startFromEnvironment();

    // F6 in the game window starts a capture, F6 again stops it: perfil_<n>.txt (FPS line of every second, per-thread
    // hot spots, game-thread time per subsystem) and perfil_<n>_pilhas.txt (folded stacks) are written to <directory>.
    void startCaptureHotkey(const std::filesystem::path &directory);

    // Called once per game frame (0x128dc0) from the game thread: remembers the frame's log time so an F6 capture can
    // also write every frame time and the game thread's samples on the same clock (perfil_<n>_quadros.txt and
    // perfil_<n>_linha.txt, read by tools/frame_times.py).
    void noteGameFrame();
}
