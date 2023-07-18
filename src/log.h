#ifndef LOG_H_
#define LOG_H_

#include <stdarg.h>
#include <stdbool.h>

#define LOG_ALLOCATION_FAIL(var, block) \
    if (!var) { \
        wlc_logln(LOG_ERROR, "Allocation Failed"); \
        block \
    }

enum wlc_log_level {
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARNING,
    LOG_ERROR,
    LOG_FATAL,
};

void wlc_log(const enum wlc_log_level level, const char *fmt, ...);
void wlc_logv(const enum wlc_log_level level, const char *fmt, va_list ap);
void wlc_logln(const enum wlc_log_level level, const char *fmt, ...);
bool wlc_log_create(const char *file);
void wlc_log_destroy(void);

#endif // LOG_H_
