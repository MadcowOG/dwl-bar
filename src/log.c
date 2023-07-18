#include "log.h"
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

FILE *log_file = NULL;

void wlc_logv(const enum wlc_log_level level, const char *fmt, va_list ap){
    switch (level) {
        case LOG_DEBUG:
            fprintf(log_file, "[wlclient] debug: ");
            break;
        case LOG_INFO:
            fprintf(log_file, "[wlclient] info: ");
            break;
        case LOG_WARNING:
            fprintf(log_file, "[wlclient] warning: ");
            break;
        case LOG_ERROR:
            fprintf(log_file, "[wlclient] error: ");
            break;
        case LOG_FATAL:
            fprintf(log_file, "[wlclient] FATAL: ");
            break;
    }

    vfprintf(log_file, fmt, ap);
    fflush(log_file);
}

void wlc_log(const enum wlc_log_level level, const char *fmt, ...) {
    if (!log_file || !fmt) return;

    va_list ap;
    va_start(ap, fmt);
    wlc_logv(level, fmt, ap);
    va_end(ap);
}

void wlc_logln(const enum wlc_log_level level, const char *fmt, ...) {
    if (!log_file || !fmt) return;

    va_list ap;
    va_start(ap, fmt);
    wlc_logv(level, fmt, ap);
    fputc('\n', log_file);
    va_end(ap);
}

void wlc_log_destroy(void) {
    if (!log_file) return;
    wlc_logln(LOG_INFO, "LOG STOP");
    fclose(log_file);
}

bool wlc_log_create(const char *file) {
    if (!file || !strlen(file)) file = "wlclient.log";

    log_file = fopen(file, "w");
    if (!log_file) return false;

    wlc_logln(LOG_INFO, "LOG START");

    return true;
}
