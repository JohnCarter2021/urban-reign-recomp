#include "runtime/ps2_pnach.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

namespace ps2x::pnach
{
    namespace
    {
        constexpr uint32_t kRamSize = 32u * 1024u * 1024u;

        std::vector<Patch> g_patches;
        bool g_loaded = false;
        std::atomic<uint64_t> g_writes{0u};
        bool g_hasContinuous = false;

        std::string trim(std::string text)
        {
            const auto notSpace = [](unsigned char c) { return !std::isspace(c); };
            text.erase(text.begin(), std::find_if(text.begin(), text.end(), notSpace));
            text.erase(std::find_if(text.rbegin(), text.rend(), notSpace).base(), text.end());
            return text;
        }

        std::string lower(std::string text)
        {
            for (char &c : text)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return text;
        }

        bool parseHex(const std::string &text, uint64_t &out)
        {
            if (text.empty())
                return false;
            char *end = nullptr;
            out = std::strtoull(text.c_str(), &end, 16);
            return end && *end == '\0';
        }

        void write(uint8_t *rdram, const Patch &patch)
        {
            const uint32_t address = patch.address & 0x01FFFFFFu;
            if (address + patch.bytes > kRamSize)
                return;
            // EE RAM is little endian, like the host.
            std::memcpy(rdram + address, &patch.value, patch.bytes);
        }
    }

    size_t loadFile(const std::string &path)
    {
        std::ifstream file(path);
        if (!file)
        {
            std::fprintf(stderr, "[pnach] cannot open %s\n", path.c_str());
            return 0u;
        }
        size_t added = 0u;
        size_t unsupported = 0u;
        size_t lineNumber = 0u;
        std::string line;
        while (std::getline(file, line))
        {
            ++lineNumber;
            if (const size_t comment = line.find("//"); comment != std::string::npos)
                line.resize(comment);
            line = trim(line);
            if (line.rfind("patch", 0) != 0u)
                continue; // gametitle=, comment=, author=, [groups], blank lines
            const size_t equals = line.find('=');
            if (equals == std::string::npos)
                continue;
            std::vector<std::string> fields;
            std::stringstream rest(line.substr(equals + 1u));
            std::string field;
            while (std::getline(rest, field, ','))
                fields.push_back(trim(field));
            if (fields.size() < 5u)
            {
                ++unsupported;
                continue;
            }
            uint64_t place = 0u, address = 0u, value = 0u;
            const std::string cpu = lower(fields[1]);
            const std::string type = lower(fields[3]);
            if (cpu != "ee" || !parseHex(fields[0], place) || place > 2u || !parseHex(fields[2], address) ||
                !parseHex(fields[4], value))
            {
                ++unsupported;
                continue;
            }
            Patch patch;
            patch.place = static_cast<uint8_t>(place);
            patch.address = static_cast<uint32_t>(address);
            patch.value = value;
            if (type == "byte")
                patch.bytes = 1u;
            else if (type == "short")
                patch.bytes = 2u;
            else if (type == "word")
                patch.bytes = 4u;
            else if (type == "double")
                patch.bytes = 8u;
            else if (type == "extended")
            {
                // CodeBreaker raw codes: the top nibble of the address is the code type.
                const uint32_t code = static_cast<uint32_t>(address >> 28u);
                patch.address = static_cast<uint32_t>(address & 0x0FFFFFFFu);
                if (code == 0u)
                    patch.bytes = 1u;
                else if (code == 1u)
                    patch.bytes = 2u;
                else if (code == 2u)
                    patch.bytes = 4u;
                else
                {
                    if (unsupported++ < 5u)
                        std::fprintf(stderr, "[pnach] %s:%zu: extended code type %X not supported, skipped\n", path.c_str(),
                                     lineNumber, code);
                    continue;
                }
            }
            else
            {
                ++unsupported;
                continue;
            }
            patch.value &= patch.bytes == 8u ? ~0ull : ((1ull << (patch.bytes * 8u)) - 1u);
            g_hasContinuous |= patch.place != 0u;
            g_patches.push_back(patch);
            ++added;
        }
        std::fprintf(stderr, "[pnach] %s: %zu patches loaded, %zu lines skipped\n", path.c_str(), added, unsupported);
        return added;
    }

    size_t loadFromEnvironment()
    {
        if (g_loaded)
            return g_patches.size();
        g_loaded = true;
        const char *list = std::getenv("PS2X_PNACH");
        if (!list || !*list || std::strcmp(list, "none") == 0 || std::strcmp(list, "0") == 0)
            return 0u; // "none": off even when controls.ini lists files (benchmarks of unpatched scenes)
        std::stringstream files(list);
        std::string path;
        while (std::getline(files, path, ';'))
        {
            path = trim(path);
            if (path.size() >= 2u && path.front() == '"' && path.back() == '"')
                path = path.substr(1u, path.size() - 2u);
            if (!path.empty())
                (void)loadFile(path);
        }
        return g_patches.size();
    }

    void clear()
    {
        g_patches.clear();
        g_hasContinuous = false;
    }

    bool active() noexcept
    {
        return !g_patches.empty();
    }

    const std::vector<Patch> &patches() noexcept
    {
        return g_patches;
    }

    void applyOnce(uint8_t *rdram)
    {
        if (!rdram)
            return;
        uint64_t count = 0u;
        for (const Patch &patch : g_patches)
        {
            if (patch.place == 0u || patch.place == 2u)
            {
                write(rdram, patch);
                ++count;
            }
        }
        g_writes.fetch_add(count, std::memory_order_relaxed);
        if (count != 0u)
            std::fprintf(stderr, "[pnach] %llu boot patches applied\n", static_cast<unsigned long long>(count));
    }

    void applyContinuous(uint8_t *rdram)
    {
        if (!rdram || !g_hasContinuous)
            return;
        uint64_t count = 0u;
        for (const Patch &patch : g_patches)
        {
            if (patch.place != 0u)
            {
                write(rdram, patch);
                ++count;
            }
        }
        const uint64_t total = g_writes.fetch_add(count, std::memory_order_relaxed) + count;
        // Every ~10 s: proof that the patches keep being applied (benchmarks check this line).
        static auto lastLog = std::chrono::steady_clock::time_point{};
        const auto now = std::chrono::steady_clock::now();
        if (now - lastLog >= std::chrono::seconds(10))
        {
            lastLog = now;
            std::fprintf(stderr, "[pnach] %llu writes applied so far (%llu per vblank)\n", static_cast<unsigned long long>(total),
                         static_cast<unsigned long long>(count));
        }
    }

    uint64_t writesApplied() noexcept
    {
        return g_writes.load(std::memory_order_relaxed);
    }
}
