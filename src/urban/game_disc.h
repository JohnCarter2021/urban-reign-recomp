#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace urban
{
    // Urban Reign (USA), SLUS-21209.
    inline constexpr const char *kSerial = "SLUS-21209";
    inline constexpr const char *kBootElfPath = "SLUS_212.09";
    inline constexpr const char *kBootElfSha256 = "60399090fc69ffdb4c39afd30977ab50890b0e33ed6bd9374e4d9497f4246081";

    struct DiscCheckResult
    {
        bool ok = false;
        std::string error;
        std::filesystem::path bootElfPath;
    };

    std::filesystem::path findDiscImage(const std::vector<std::filesystem::path> &searchRoots);
    DiscCheckResult verifyDiscAndPrepare(const std::filesystem::path &image, const std::filesystem::path &cacheDir);
}
