# Logging

@page logging Logging

`<threadmaxx/Logger.hpp>` is the engine's tiny pluggable log sink.
The engine routes lifecycle notices (initialize / shutdown), system
registration messages, and renderer / loader errors through whichever
`ILogger` is currently installed. If none is installed, a default
sink writes to `std::cerr` at `Warn` and above; finer levels are
dropped.

## Installing a sink

```cpp
class MyLogger : public threadmaxx::ILogger {
public:
    void log(threadmaxx::LogLevel level, std::string_view msg) override {
        // Route to spdlog, a file, your engine HUD, whatever.
        myEngineLog(level, msg);
    }
};

MyLogger logger;
engine.setLogger(&logger);
// ... lifecycle calls now route through logger.log(...)
```

The engine does NOT take ownership — the logger must outlive the
engine. Pass `nullptr` to restore the default `std::cerr` sink.

## Levels

```cpp
enum class LogLevel : std::uint8_t {
    Trace = 0,
    Debug = 1,
    Info  = 2,
    Warn  = 3,
    Error = 4,
};
```

What the engine emits today:

- **Info** — `engine initialize: N worker(s), fixedStep=...s`
- **Info** — `registered system 'name' (now K total, W wave(s))`
- **Info** — `engine shutdown after K tick(s), N command(s) committed`
- **Error** — `renderer initialize() returned false`

The default sink silently drops `Trace` / `Debug` / `Info`, so a
freshly-initialized engine without a custom logger stays quiet —
matches the behavior of all prior batches. Install a custom logger
(or call `DefaultLogger(std::cerr, LogLevel::Info)`) to see the
lifecycle stream.

## Thread safety

The engine itself only logs from the simulation thread today. Loaders
and third-party callers may invoke `log` from helper threads;
implementations should serialize their own output stream if they
share one across threads.

## Why not `std::format` / `spdlog` / `boost::log`?

The engine ships zero dependencies and the public surface is
intentionally minimal. `ILogger` is a single virtual call; adapt to
whichever logging library your game already uses.
