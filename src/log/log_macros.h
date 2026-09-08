#pragma once

// Every macro here is a single statement (do/while), so `if (x) log_warn(...)
// else ...` means what it reads as. Unbraced, log_error's assert(false) sat
// outside the caller's `if` and fired on every call -- afterhours' e2e runner
// has an `if (bad) log_error(...)` that aborted the whole suite at load.
#define log_at(level, ...)                                  \
    do {                                                    \
        if (static_cast<int>(level) >=                      \
            static_cast<int>(AFTER_HOURS_LOG_LEVEL))        \
            log_me(level, __FILE__, __LINE__, __VA_ARGS__); \
    } while (0)

#define log_trace(...) log_at(LogLevel::LOG_TRACE, __VA_ARGS__)
#define log_info(...) log_at(LogLevel::LOG_INFO, __VA_ARGS__)
#define log_warn(...) log_at(LogLevel::LOG_WARN, __VA_ARGS__)
#define log_error(...)                            \
    do {                                          \
        log_at(LogLevel::LOG_ERROR, __VA_ARGS__); \
        assert(false);                            \
    } while (0)

#define log_clean(level, ...)                        \
    do {                                             \
        if (static_cast<int>(level) >=               \
            static_cast<int>(AFTER_HOURS_LOG_LEVEL)) \
            log_me(level, "", -1, __VA_ARGS__);      \
    } while (0)

#define log_if(x, ...)                                                    \
    {                                                                     \
        if (x) log_me(LogLevel::LOG_IF, __FILE__, __LINE__, __VA_ARGS__); \
    }

#define log_ifx(x, level, ...)                                 \
    {                                                          \
        if (x) log_me(level, __FILE__, __LINE__, __VA_ARGS__); \
    }

#define LOG_ONCE_PER(interval, level, ...) \
    log_once_per(interval, level, __FILE__, __LINE__, __VA_ARGS__)
