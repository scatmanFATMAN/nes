#pragma once

#include <stdbool.h>

#define log_err(fmt, ...)   log_write(LOG_LEVEL_ERR,   fmt, ##__VA_ARGS__)
#define log_warn(fmt, ...)  log_write(LOG_LEVEL_WARN,  fmt, ##__VA_ARGS__)
#define log_info(fmt, ...)  log_write(LOG_LEVEL_INFO,  fmt, ##__VA_ARGS__)
#define log_debug(fmt, ...) log_write(LOG_LEVEL_DEBUG, fmt, ##__VA_ARGS__)

typedef enum {
    LOG_LEVEL_ERR   = 0,
    LOG_LEVEL_WARN  = 1,
    LOG_LEVEL_INFO  = 2,
    LOG_LEVEL_DEBUG = 3
} log_level_t;

void log_init();
void log_free();

void log_set_level(log_level_t level);
void log_set_stdout(bool value);
void log_set_file(const char *file);

const char * log_get_error();

bool log_open();
void log_close();

void log_write(log_level_t level, const char *fmt, ...);