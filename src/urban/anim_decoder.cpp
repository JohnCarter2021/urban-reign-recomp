// Urban Reign's animation track decoder, sub_00292670(out, anim, bone, frame): it samples one bone's track of an
// animation at a frame and writes 16 bytes (a quaternion or vector) to out, returning out. ~5,700 calls/s at the
// 4-fighter select, ~15% of the game thread, and the select screen's idle loops repeat the same (anim, bone, frame)
// over and over: 4 animations x 21 bones x 90 frames, every call a repeat after the first loop.
//
// Measured write set (URBAN_ANIM_PROBE): the 16 bytes at out and the decoder's own stack frame below sp, nothing else
// in RAM or the scratchpad; v0 = out. The read set is the problem: the same (anim, bone, frame) sometimes gives another
// result. URBAN_ANIM_VERIFY=2 traced that to inputs the decoder never sets: its stack frame (a local left unset on some
// paths), $t4 including the upper 64 bits (128-bit MMI use) and, after keying on those too, something still unidentified.
// The experimental cache keys on: anim, bone, frame, the bone's track (sub_00299130: *(anim+0x10) + *(*(anim+0x0C) +
// bone*4)), the flag word *(*(anim+8) + bone*4), the caller, the halfword at 0x662AA8, hashes of the animation object and
// of the track's first 64 bytes, the 0xF0-byte stack frame and all of $t4; a hit writes the 16 bytes and the frame.
// Keying on $t4's upper half drops the hit rate to ~40%, and ~1 in 100,000 hits still differs, so it stays off.
//
// URBAN_ANIM_CACHE=1 turns the cache on (off by default). URBAN_ANIM_VERIFY=1 runs the decoder on every call and
// compares a hit's bytes with it (logs mismatches; once per second a summary); =2 also names the entry registers and
// track bytes that changed. URBAN_ANIM_PROBE=<n>: for n calls after the first 20 s (or the calls matching
// URBAN_ANIM_PROBE_MATCH=anim,bone[,frame]), snapshot RAM and scratchpad around the call and log what it changed.
#include "urban/anim_decoder.h"

#include "ps2_runtime.h"
#include "runtime/ee_scheduler.h"
#include "urban/function_hooks.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <vector>

// Guest instructions executed by sub_00292670, when its generated code is instrumented for an experiment (each
// instruction increments it); stays 0 otherwise.
uint64_t g_urbanDecoderInsns = 0;

namespace urban::animdecoder
{
    namespace
    {
        PS2Runtime::RecompiledFunction g_decoder = nullptr;
        bool g_cache = true;
        // URBAN_ANIM_STATS=1: per second, calls, host time per call and emulated EE cycles per call (what the call costs
        // on the PS2's 294.912 MHz EE, at one cycle per instruction).
        bool g_timing = false;
        struct Timing
        {
            uint64_t calls = 0, ns = 0, cycles = 0, insns = 0;
            std::chrono::steady_clock::time_point last = std::chrono::steady_clock::now();
        } g_timingStats;
        bool g_verify = false;
        int g_probeLeft = 0;
        // URBAN_ANIM_PROBE_MATCH=anim,bone[,frame] (hex anim): probe only the calls with these arguments, at any time.
        bool g_probeMatch = false;
        uint32_t g_matchAnim = 0u, g_matchBone = 0u, g_matchFrame = ~0u;
        std::chrono::steady_clock::time_point g_start;
        std::vector<uint8_t> g_before;

        struct Key
        {
            uint32_t anim, bone, frame, track, flags, caller, global;
            uint64_t contentHash;
            uint64_t frameHash; // the 0xF0 bytes below sp before the call
            uint64_t t4, t4High; // all 128 bits of $t4 at entry: some paths read it without setting it (found with
                                 // URBAN_ANIM_VERIFY=2; the decoder uses 128-bit MMI operations, so the upper half counts)
            bool operator==(const Key &other) const
            {
                return anim == other.anim && bone == other.bone && frame == other.frame && track == other.track &&
                       flags == other.flags && caller == other.caller && global == other.global && contentHash == other.contentHash &&
                       frameHash == other.frameHash && t4 == other.t4 && t4High == other.t4High;
            }
        };

        struct KeyHash
        {
            size_t operator()(const Key &k) const
            {
                uint64_t h = k.contentHash ^ k.frameHash ^ (static_cast<uint64_t>(k.anim) << 1);
                h = h * 0x100000001B3ull ^ k.bone;
                h = h * 0x100000001B3ull ^ k.frame;
                h = h * 0x100000001B3ull ^ k.track;
                h = h * 0x100000001B3ull ^ k.flags;
                h = h * 0x100000001B3ull ^ k.caller;
                h = h * 0x100000001B3ull ^ k.global;
                h = h * 0x100000001B3ull ^ k.t4;
                h = h * 0x100000001B3ull ^ k.t4High;
                return static_cast<size_t>(h ^ (h >> 29));
            }
        };

        constexpr uint32_t kFrameBytes = 0xF0u;
        struct Value
        {
            std::array<uint8_t, 16> out;
            std::array<uint8_t, kFrameBytes> frame; // the decoder's stack frame after the call
        };
        std::unordered_map<Key, Value, KeyHash> g_entries;
        // URBAN_ANIM_VERIFY=2: also keep 4 KB of the track data with each entry and, on a mismatch, report which of
        // those bytes changed since the entry was made (to find inputs the key misses).
        std::unordered_map<Key, std::vector<uint8_t>, KeyHash> g_trackCopies;
        bool g_diagnose = false;
        // ... and the registers at entry, to name the ones that differ when the same key gives another result.
        struct EntryRegs
        {
            __m128i r[32];
            uint64_t hi, lo, hi1, lo1;
            uint32_t sa;
            float f[32];
            float fAcc;
            __m128 vf[32];
            __m128 vacc;
            float q, p, i;
        };
        std::unordered_map<Key, EntryRegs, KeyHash> g_entryRegs;

        EntryRegs captureRegs(const R5900Context *ctx)
        {
            EntryRegs e;
            std::memcpy(e.r, ctx->r, sizeof(e.r));
            e.hi = ctx->hi;
            e.lo = ctx->lo;
            e.hi1 = ctx->hi1;
            e.lo1 = ctx->lo1;
            e.sa = ctx->sa;
            std::memcpy(e.f, ctx->f, sizeof(e.f));
            e.fAcc = ctx->f_acc;
            std::memcpy(e.vf, ctx->vu0_vf, sizeof(e.vf));
            e.vacc = ctx->vu0_acc;
            e.q = ctx->vu0_q;
            e.p = ctx->vu0_p;
            e.i = ctx->vu0_i;
            return e;
        }

        std::string regDiff(const EntryRegs &a, const EntryRegs &b)
        {
            std::string out;
            for (int i = 0; i < 32; ++i)
                if (std::memcmp(&a.r[i], &b.r[i], 16) != 0)
                    out += " r" + std::to_string(i);
            if (a.hi != b.hi || a.lo != b.lo)
                out += " hi/lo";
            if (a.hi1 != b.hi1 || a.lo1 != b.lo1)
                out += " hi1/lo1";
            if (a.sa != b.sa)
                out += " sa";
            for (int i = 0; i < 32; ++i)
                if (std::memcmp(&a.f[i], &b.f[i], 4) != 0)
                    out += " f" + std::to_string(i);
            if (std::memcmp(&a.fAcc, &b.fAcc, 4) != 0)
                out += " facc";
            for (int i = 0; i < 32; ++i)
                if (std::memcmp(&a.vf[i], &b.vf[i], 16) != 0)
                    out += " vf" + std::to_string(i);
            if (std::memcmp(&a.vacc, &b.vacc, 16) != 0)
                out += " vacc";
            if (std::memcmp(&a.q, &b.q, 4) != 0 || std::memcmp(&a.p, &b.p, 4) != 0 || std::memcmp(&a.i, &b.i, 4) != 0)
                out += " q/p/i";
            return out;
        }
        struct Stats
        {
            uint64_t calls = 0, hits = 0, misses = 0, uncacheable = 0, verified = 0, mismatches = 0;
            std::chrono::steady_clock::time_point last = std::chrono::steady_clock::now();
        } g_stats;

        bool inRam(uint32_t address, uint32_t size)
        {
            const uint32_t physical = address & 0x1FFFFFFFu;
            return physical < PS2_RAM_SIZE && size <= PS2_RAM_SIZE - physical && address < 0x20000000u;
        }

        uint64_t fnv(const uint8_t *data, size_t size, uint64_t h = 0xCBF29CE484222325ull)
        {
            for (size_t i = 0; i < size; ++i)
                h = (h ^ data[i]) * 0x100000001B3ull;
            return h;
        }

        uint32_t word(const uint8_t *rdram, uint32_t address)
        {
            uint32_t value = 0u;
            std::memcpy(&value, rdram + (address & 0x1FFFFFFFu), 4u);
            return value;
        }

        // Builds the key; false when an input lies outside RAM (then the call is not cached). The bone's track data is
        // what sub_00299130 returns: *(anim + 0x10) + *(*(anim + 0x0C) + bone * 4) when bone < **(anim + 4), else 0.
        bool makeKey(const uint8_t *rdram, uint32_t anim, uint32_t bone, uint32_t frame, uint32_t caller, Key &key)
        {
            if (!inRam(anim, 64u) || bone >= 0x10000u)
                return false;
            const uint32_t countPtr = word(rdram, anim + 4u);
            const uint32_t offsets = word(rdram, anim + 0x0Cu);
            const uint32_t base = word(rdram, anim + 0x10u);
            const uint32_t flagTable = word(rdram, anim + 8u);
            if (!inRam(countPtr, 4u) || !inRam(flagTable + bone * 4u, 4u))
                return false;
            uint32_t track = 0u;
            if (bone < word(rdram, countPtr))
            {
                if (!inRam(offsets + bone * 4u, 4u))
                    return false;
                track = base + word(rdram, offsets + bone * 4u);
                if (!inRam(track, 64u))
                    return false;
            }
            uint16_t global = 0u; // the decoder reads the halfword at 0x662AA8 (lhu 0x2000 off 0x660AA8)
            std::memcpy(&global, rdram + 0x662AA8u, 2u);
            uint64_t hash = fnv(rdram + (anim & 0x1FFFFFFFu), 64u);
            if (track != 0u)
                hash = fnv(rdram + (track & 0x1FFFFFFFu), 64u, hash);
            key = Key{anim, bone, frame, track, word(rdram, flagTable + bone * 4u), caller, global, hash};
            return true;
        }

        void report()
        {
            const auto now = std::chrono::steady_clock::now();
            if (now - g_stats.last < std::chrono::seconds(1))
                return;
            g_stats.last = now;
            if (!g_verify)
                return;
            std::fprintf(stderr, "[anim-cache] %llu calls, %llu hits, %llu misses, %llu not cached, %zu entries; verify: %llu checked, %llu mismatches\n",
                         static_cast<unsigned long long>(g_stats.calls), static_cast<unsigned long long>(g_stats.hits),
                         static_cast<unsigned long long>(g_stats.misses), static_cast<unsigned long long>(g_stats.uncacheable), g_entries.size(),
                         static_cast<unsigned long long>(g_stats.verified), static_cast<unsigned long long>(g_stats.mismatches));
            std::fflush(stderr);
        }

        void probe(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            const uint32_t out = getRegU32(ctx, 4), anim = getRegU32(ctx, 5), bone = getRegU32(ctx, 6), frame = getRegU32(ctx, 7);
            const uint32_t sp = getRegU32(ctx, 29), ra = getRegU32(ctx, 31);
            g_before.assign(rdram, rdram + PS2_RAM_SIZE);
            uint8_t *spr = runtime->memory().getScratchpad();
            std::vector<uint8_t> sprBefore(spr, spr + PS2_SCRATCHPAD_SIZE);
            g_decoder(rdram, ctx, runtime);
            std::string sprRanges;
            uint32_t sprChanged = 0u;
            for (uint32_t at = 0; at < PS2_SCRATCHPAD_SIZE;)
            {
                if (spr[at] == sprBefore[at])
                {
                    ++at;
                    continue;
                }
                uint32_t end = at;
                while (end < PS2_SCRATCHPAD_SIZE && (spr[end] != sprBefore[end] ||
                                                     (end + 8u < PS2_SCRATCHPAD_SIZE && std::memcmp(spr + end, sprBefore.data() + end, 8u) != 0)))
                    ++end;
                sprChanged += end - at;
                char text[48];
                std::snprintf(text, sizeof(text), " [spr %04x-%04x]", at, end);
                if (sprRanges.size() < 600u)
                    sprRanges += text;
                at = end;
            }
            std::string ranges;
            uint32_t changedBytes = 0u;
            for (uint32_t at = 0; at < PS2_RAM_SIZE;)
            {
                if (rdram[at] == g_before[at])
                {
                    ++at;
                    continue;
                }
                uint32_t end = at;
                while (end < PS2_RAM_SIZE && (rdram[end] != g_before[end] || (end + 8u < PS2_RAM_SIZE &&
                                                                               std::memcmp(rdram + end, g_before.data() + end, 8u) != 0)))
                    ++end;
                changedBytes += end - at;
                char text[96];
                const auto rel = [&](uint32_t addr, uint32_t base) { return static_cast<int32_t>(addr - base); };
                std::snprintf(text, sizeof(text), " [%06x-%06x out%+d sp%+d]", at, end, rel(at, out), rel(at, sp));
                if (ranges.size() < 1200u)
                    ranges += text;
                at = end;
            }
            std::fprintf(stderr, "[anim-probe] out=%06x anim=%06x bone=%u frame=%u ra=%06x sp=%06x v0=%08x changed=%u:%s scratchpad=%u:%s\n",
                         out, anim, bone, frame, ra, sp, getRegU32(ctx, 2), changedBytes, ranges.c_str(), sprChanged, sprRanges.c_str());
            std::fflush(stderr);
        }

        void decoderHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            if (g_timing)
            {
                const uint64_t c0 = runtime->eeScheduler().currentCycle();
                const uint64_t i0 = g_urbanDecoderInsns;
                const auto t0 = std::chrono::steady_clock::now();
                g_decoder(rdram, ctx, runtime);
                const auto t1 = std::chrono::steady_clock::now();
                ++g_timingStats.calls;
                g_timingStats.ns += static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
                g_timingStats.cycles += runtime->eeScheduler().currentCycle() - c0;
                g_timingStats.insns += g_urbanDecoderInsns - i0;
                if (t1 - g_timingStats.last >= std::chrono::seconds(2))
                {
                    const double calls = static_cast<double>(g_timingStats.calls);
                    std::fprintf(stderr, "[anim-stats] %.0f calls/s, %.1f us/call on the host, %.0f EE cycles/call (%.1f us on a PS2), %.2f ns per EE cycle\n",
                                 calls / std::chrono::duration<double>(t1 - g_timingStats.last).count(), g_timingStats.ns / calls / 1000.0,
                                 g_timingStats.cycles / calls, g_timingStats.cycles / calls / 294.912,
                                 static_cast<double>(g_timingStats.ns) / static_cast<double>(g_timingStats.cycles));
                    if (g_timingStats.insns != 0u)
                        std::fprintf(stderr, "[anim-stats] %.0f guest instructions/call (%.1f us on a PS2 at 1 instruction/cycle), %.2f ns per instruction on the host\n",
                                     g_timingStats.insns / calls, g_timingStats.insns / calls / 294.912,
                                     static_cast<double>(g_timingStats.ns) / static_cast<double>(g_timingStats.insns));
                    std::fflush(stderr);
                    g_timingStats = Timing{};
                }
                return;
            }
            const bool matched = g_probeMatch && getRegU32(ctx, 5) == g_matchAnim && getRegU32(ctx, 6) == g_matchBone &&
                                 (g_matchFrame == ~0u || getRegU32(ctx, 7) == g_matchFrame);
            if (g_probeLeft > 0 && (matched || (!g_probeMatch && std::chrono::steady_clock::now() - g_start > std::chrono::seconds(20))))
            {
                --g_probeLeft;
                probe(rdram, ctx, runtime);
                return;
            }
            ++g_stats.calls;
            const uint32_t out = getRegU32(ctx, 4);
            const uint32_t frameBase = getRegU32(ctx, 29) - kFrameBytes;
            Key key{};
            const bool overlaps = out + 16u > frameBase && out < frameBase + kFrameBytes;
            if (!g_cache || !inRam(out, 16u) || !inRam(frameBase, kFrameBytes) || overlaps ||
                !makeKey(rdram, getRegU32(ctx, 5), getRegU32(ctx, 6), getRegU32(ctx, 7), getRegU32(ctx, 31), key))
            {
                ++g_stats.uncacheable;
                g_decoder(rdram, ctx, runtime);
                report();
                return;
            }
            uint8_t *dst = rdram + (out & 0x1FFFFFFFu);
            uint8_t *frame = rdram + (frameBase & 0x1FFFFFFFu);
            key.frameHash = fnv(frame, kFrameBytes);
            key.t4 = static_cast<uint64_t>(_mm_cvtsi128_si64(ctx->r[12]));
            key.t4High = static_cast<uint64_t>(_mm_extract_epi64(ctx->r[12], 1));
            EntryRegs entryRegs;
            if (g_diagnose)
                entryRegs = captureRegs(ctx);
            const auto it = g_entries.find(key);
            if (it != g_entries.end() && !g_verify)
            {
                ++g_stats.hits;
                std::memcpy(dst, it->second.out.data(), 16u);
                std::memcpy(frame, it->second.frame.data(), kFrameBytes);
                setReturnU32(ctx, out);
                ctx->pc = getRegU32(ctx, 31);
                report();
                return;
            }
            g_decoder(rdram, ctx, runtime);
            if (it == g_entries.end())
            {
                ++g_stats.misses;
                if (g_entries.size() >= 262144u) // ~70 MB at most; start over rather than grow without bound
                    g_entries.clear();
                Value value;
                std::memcpy(value.out.data(), dst, 16u);
                std::memcpy(value.frame.data(), frame, kFrameBytes);
                g_entries.emplace(key, value);
                if (g_diagnose)
                    g_entryRegs[key] = entryRegs;
                if (g_diagnose && key.track != 0u && inRam(key.track, 4096u))
                    g_trackCopies[key].assign(rdram + (key.track & 0x1FFFFFFFu), rdram + (key.track & 0x1FFFFFFFu) + 4096u);
            }
            else
            {
                ++g_stats.hits;
                ++g_stats.verified;
                if (std::memcmp(dst, it->second.out.data(), 16u) != 0 || std::memcmp(frame, it->second.frame.data(), kFrameBytes) != 0)
                {
                    if (++g_stats.mismatches <= 20u)
                    {
                        uint32_t want[4], got[4];
                        std::memcpy(want, dst, 16u);
                        std::memcpy(got, it->second.out.data(), 16u);
                        std::fprintf(stderr, "[anim-cache] MISMATCH anim=%06x bone=%u frame=%u track=%06x ra=%06x: decoder %08x %08x %08x %08x, cache %08x %08x %08x %08x\n",
                                     key.anim, key.bone, key.frame, key.track, key.caller, want[0], want[1], want[2], want[3], got[0], got[1], got[2], got[3]);
                    }
                    if (g_diagnose)
                    {
                        if (const auto regs = g_entryRegs.find(key); regs != g_entryRegs.end())
                        {
                            std::fprintf(stderr, "[anim-cache]   registers at entry that differ:%s\n", regDiff(regs->second, entryRegs).c_str());
                            regs->second = entryRegs;
                        }
                        const auto copy = g_trackCopies.find(key);
                        if (copy != g_trackCopies.end())
                        {
                            const uint8_t *now = rdram + (key.track & 0x1FFFFFFFu);
                            std::string where;
                            uint32_t changed = 0u;
                            for (uint32_t i = 0; i < 4096u; ++i)
                                if (now[i] != copy->second[i])
                                {
                                    if (changed++ < 12u)
                                    {
                                        char text[24];
                                        std::snprintf(text, sizeof(text), " +%x", i);
                                        where += text;
                                    }
                                }
                            std::fprintf(stderr, "[anim-cache]   track bytes changed since cached: %u%s\n", changed, where.c_str());
                            copy->second.assign(now, now + 4096u);
                        }
                    }
                    std::memcpy(it->second.out.data(), dst, 16u);
                    std::memcpy(it->second.frame.data(), frame, kFrameBytes);
                }
            }
            report();
        }
    }

    void install(PS2Runtime &runtime)
    {
        // Off by default: the decoder is not a pure function of (anim, bone, frame). Some paths read $t4 (both 64-bit
        // halves) and other state they never set, so a cache keyed on everything visible still misses ~1 in 100,000
        // (docs/PROGRESS.md §46). URBAN_ANIM_CACHE=1 turns it on for experiments.
        const char *cache = std::getenv("URBAN_ANIM_CACHE");
        g_cache = cache && *cache == '1';
        const char *verify = std::getenv("URBAN_ANIM_VERIFY");
        g_verify = verify && (*verify == '1' || *verify == '2');
        g_diagnose = verify && *verify == '2';
        const char *probeSpec = std::getenv("URBAN_ANIM_PROBE");
        g_probeLeft = probeSpec ? std::atoi(probeSpec) : 0;
        if (const char *match = std::getenv("URBAN_ANIM_PROBE_MATCH"); match && *match)
        {
            g_probeMatch = std::sscanf(match, "%x,%u,%u", &g_matchAnim, &g_matchBone, &g_matchFrame) >= 2;
            if (g_probeLeft <= 0)
                g_probeLeft = 50;
        }
        const char *timing = std::getenv("URBAN_ANIM_STATS");
        g_timing = timing && *timing == '1';
        if (!g_cache && g_probeLeft <= 0 && !g_timing)
            return;
        g_decoder = runtime.lookupFunction(0x292670u);
        if (!g_decoder)
            return;
        g_start = std::chrono::steady_clock::now();
        FunctionHooks::instance().replace(0x292670u, "anim_decoder_cache", &decoderHook, ReplacementStatus::Native,
                                          "animation decoder: cache of (anim, bone, frame) -> 16 bytes");
    }
}
