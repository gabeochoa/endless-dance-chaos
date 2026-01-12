
#pragma once

#define FMT_HEADER_ONLY
#include <fmt/args.h>
#include <fmt/format.h>
#include <fmt/ostream.h>

#define AFTER_HOURS_LOG_WITH_COLOR
#include "log/log.h"

// Afterhours compatibility layer
// When AFTER_HOURS_REPLACE_LOGGING is defined, afterhours expects log
// functions. Our log_macros.h provides: log_trace, log_info, log_warn,
// log_error, log_clean as macros. Afterhours also calls log_once_per which we
// need to provide.
#ifdef AFTER_HOURS_REPLACE_LOGGING

// log_once_per - afterhours calls this directly, our macro is named
// LOG_ONCE_PER Just provide a no-op since it's used for rate-limited warnings
namespace afterhours_compat {
inline void log_once_per_impl(...) {
    // Rate-limited logging disabled for afterhours calls
}
}  // namespace afterhours_compat
#define log_once_per(...) afterhours_compat::log_once_per_impl(__VA_ARGS__)

#endif  // AFTER_HOURS_REPLACE_LOGGING
