#pragma once

#include <cstdint>
#include <sstream>
#include <string>
#include <string_view>

namespace urban::log
{
    enum class Category : uint32_t
    {
        Boot = 1u << 0,
        Loader = 1u << 1,
        File = 1u << 2,
        Ee = 1u << 3,
        Iop = 1u << 4,
        Gs = 1u << 5,
        Vu = 1u << 6,
        Spu2 = 1u << 7,
        Input = 1u << 8,
        Game = 1u << 9,
        Hook = 1u << 10,
    };

    enum class Level
    {
        Error,
        Warn,
        Info,
        Trace,
    };

    void configureFromEnvironment();
    void setTraceMask(uint32_t mask);
    bool traceEnabled(Category category);
    void write(Category category, Level level, std::string_view message);
    // Seconds since the log started: the time stamp of every log line ("[  12.3456]").
    double elapsedSeconds();
    std::string_view name(Category category);
}

#define URBAN_LOG(cat, lvl, expr)                                                   \
    do                                                                             \
    {                                                                              \
        if ((lvl) != ::urban::log::Level::Trace || ::urban::log::traceEnabled(cat)) \
        {                                                                          \
            std::ostringstream urbanLogStream_;                                     \
            urbanLogStream_ << expr;                                                \
            ::urban::log::write((cat), (lvl), urbanLogStream_.str());                \
        }                                                                          \
    } while (0)

#define URBAN_INFO(cat, expr) URBAN_LOG(::urban::log::Category::cat, ::urban::log::Level::Info, expr)
#define URBAN_WARN(cat, expr) URBAN_LOG(::urban::log::Category::cat, ::urban::log::Level::Warn, expr)
#define URBAN_ERROR(cat, expr) URBAN_LOG(::urban::log::Category::cat, ::urban::log::Level::Error, expr)
#define URBAN_TRACE(cat, expr) URBAN_LOG(::urban::log::Category::cat, ::urban::log::Level::Trace, expr)
