#pragma once

#include <cstdint>
#include <iostream>
#include <ostream>
#include <string_view>

namespace threadmaxx {

/// Coarse severity bucket. The engine emits at @ref LogLevel::Info or
/// finer for lifecycle messages; loader / shutdown errors are
/// @ref LogLevel::Error.
enum class LogLevel : std::uint8_t {
    Trace = 0,
    Debug = 1,
    Info  = 2,
    Warn  = 3,
    Error = 4,
};

/// Pluggable log sink. The engine routes startup / shutdown notices,
/// system registration, and loader-error reports through whichever
/// logger is installed via @ref Engine::setLogger. If none is installed,
/// the engine falls back to the default sink that writes to
/// `std::cerr` for @ref LogLevel::Warn and above, and silently drops
/// finer levels.
///
/// @par Thread safety
///      The engine calls @ref log only from the simulation thread today,
///      but loaders or third-party callers may invoke it from helper
///      threads. Implementations should serialize their own output
///      stream if they share one across threads.
class ILogger {
public:
    virtual ~ILogger() = default;

    /// Emit one record. The engine never appends a newline; the
    /// implementation owns presentation. The string is a stable view
    /// for the duration of the call and should be copied if retained.
    virtual void log(LogLevel level, std::string_view message) = 0;
};

/// Default sink shipped with the engine. Writes records to
/// `std::cerr` at @ref LogLevel::Warn and above; quieter levels are
/// dropped so a no-knob default doesn't spam terminal output. Use a
/// custom @ref ILogger to capture everything.
class DefaultLogger : public ILogger {
public:
    /// Sink to write to. Defaults to `std::cerr`.
    explicit DefaultLogger(std::ostream& sink = std::cerr,
                           LogLevel threshold = LogLevel::Warn) noexcept
        : sink_(&sink), threshold_(threshold) {}

    void log(LogLevel level, std::string_view message) override {
        if (static_cast<std::uint8_t>(level) <
            static_cast<std::uint8_t>(threshold_)) return;
        const char* tag = "?";
        switch (level) {
        case LogLevel::Trace: tag = "trace"; break;
        case LogLevel::Debug: tag = "debug"; break;
        case LogLevel::Info:  tag = "info";  break;
        case LogLevel::Warn:  tag = "warn";  break;
        case LogLevel::Error: tag = "error"; break;
        }
        (*sink_) << "[threadmaxx][" << tag << "] " << message << '\n';
    }

private:
    std::ostream* sink_;
    LogLevel      threshold_;
};

} // namespace threadmaxx
