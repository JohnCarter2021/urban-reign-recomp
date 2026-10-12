#pragma once

#include <cstdint>
#include <string>
#include <vector>

// PCSX2 .pnach patches (patch=<place>,EE,<address>,<type>,<value>), applied to EE RAM like PCSX2 does:
// place 0 once after the ELF is loaded, place 1 at every vblank, place 2 both. Types byte/short/word/double and
// extended (CodeBreaker codes 0/1/2: 8/16/32-bit writes). Other extended codes are counted as unsupported and skipped.
//
// PS2X_PNACH=<file>[;<file>...] lists the enabled files (controles.ini or a launcher sets it); "none" turns it off.
namespace ps2x::pnach
{
    struct Patch
    {
        uint8_t place = 1u;
        uint8_t bytes = 4u; // 1, 2, 4 or 8
        uint32_t address = 0u;
        uint64_t value = 0u;
    };

    // Loads the files listed in PS2X_PNACH (once; later calls return the cached count). Returns the patches loaded.
    size_t loadFromEnvironment();
    // Loads one file and adds its patches. Returns how many it added; unsupported lines are logged.
    size_t loadFile(const std::string &path);
    void clear();
    [[nodiscard]] bool active() noexcept;
    [[nodiscard]] const std::vector<Patch> &patches() noexcept;

    // Writes the patches of that kind into EE RAM (rdram = 32 MB main memory).
    void applyOnce(uint8_t *rdram);       // place 0 and 2
    void applyContinuous(uint8_t *rdram); // place 1 and 2, every vblank
    [[nodiscard]] uint64_t writesApplied() noexcept;
}
