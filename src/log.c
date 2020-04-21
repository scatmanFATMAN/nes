#include <stdio.h>
#include <stdarg.h>
#include "string.h"
#include <errno.h>
#include "time.h"
#include "log.h"

typedef struct {
    log_level_t level;
    bool to_stdout;
    char file[256];
    FILE *f;
    char error[256];
} log_t;

static log_t log;

void
log_init() {
    memset(&log, 0, sizeof(log));

    log.level = LOG_LEVEL_INFO;
    log.to_stdout = true;
}

void
log_free() {
    if (log.f != NULL) {
        fclose(log.f);
    }
}

void
log_set_level(log_level_t level) {
    log.level = level;
}

void
log_set_stdout(bool value) {
    log.to_stdout = value;
}

void
log_set_file(const char *file) {
    if (file == NULL) {
        log.file[0] = '\0';
    }
    else {
        strlcpy(log.file, file, sizeof(log.file));
    }
}

const char *
log_get_error() {
    return log.error;
}

bool
log_open() {
    if (log.f != NULL) {
        fclose(log.f);
    }

    if (log.file[0] != '\0') {
        log.f = fopen(log.file, "w");
        if (log.f == NULL) {
            strlcpy(log.error, strerror(errno), sizeof(log.error));
            return false;
        }
    }

    return true;
}

void
log_close() {
    if (log.f != NULL) {
        fclose(log.f);
        log.f = NULL;
    }
}

void
log_write(log_level_t level, const char *fmt, ...) {
    char time_buf[32], level_abbrev, msg[512];
    struct timespec ts;
    struct tm tm;
    va_list ap;

    //make sure we have a place to log
    if (!log.to_stdout && log.f == NULL) {
        return;
    }

    //make sure our log level passes
    if (level > log.level) {
        return;
    }

    clock_gettime(CLOCK_REALTIME, &ts);
    localtime_r(&ts.tv_sec, &tm);
    snprintf(time_buf, sizeof(time_buf), "%02d:%02d:%02d.%03d", tm.tm_hour, tm.tm_min, tm.tm_sec, ts.tv_nsec / 1000000);

    switch (level) {
        case LOG_LEVEL_ERR:   level_abbrev = 'E'; break;
        case LOG_LEVEL_WARN:  level_abbrev = 'W'; break;
        case LOG_LEVEL_INFO:  level_abbrev = 'I'; break;
        case LOG_LEVEL_DEBUG: level_abbrev = 'D'; break;
        default:              level_abbrev = 'U'; break;
    }

    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    if (log.to_stdout) {
        printf("[%s] %c %s\n", time_buf, level_abbrev, msg);
    }
    if (log.f != NULL) {
        fprintf(log.f, "[%s] %c %s\n", time_buf, level_abbrev, msg);
    }
}