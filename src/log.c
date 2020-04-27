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

static log_t logger;

void
log_init() {
    memset(&logger, 0, sizeof(logger));

    logger.level = LOG_LEVEL_INFO;
    logger.to_stdout = true;
}

void
log_free() {
    if (logger.f != NULL) {
        fclose(logger.f);
    }
}

void
log_set_level(log_level_t level) {
    logger.level = level;
}

void
log_set_stdout(bool value) {
    logger.to_stdout = value;
}

void
log_set_file(const char *file) {
    if (file == NULL) {
        logger.file[0] = '\0';
    }
    else {
        strlcpy(logger.file, file, sizeof(logger.file));
    }
}

const char *
log_get_error() {
    return logger.error;
}

bool
log_open() {
    if (logger.f != NULL) {
        fclose(logger.f);
    }

    if (logger.file[0] != '\0') {
        logger.f = fopen(logger.file, "w");
        if (logger.f == NULL) {
            strlcpy(logger.error, strerror(errno), sizeof(logger.error));
            return false;
        }
    }

    return true;
}

void
log_close() {
    if (logger.f != NULL) {
        fclose(logger.f);
        logger.f = NULL;
    }
}

void
log_write(log_level_t level, const char *module, const char *fmt, ...) {
    char time_buf[32], level_abbrev, msg[512];
    struct timespec ts;
    struct tm tm;
    va_list ap;

    //make sure we have a place to log
    if (!logger.to_stdout && logger.f == NULL) {
        return;
    }

    //make sure our log level passes
    if (level > logger.level) {
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

    if (logger.to_stdout) {
        printf("[%s] %c [%-9s] %s\n", time_buf, level_abbrev, module, msg);
    }
    if (logger.f != NULL) {
        fprintf(logger.f, "[%s] %c [%-9s] %s\n", time_buf, level_abbrev, module, msg);
    }
}