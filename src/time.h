#pragma once

#include <time.h>

#if defined(_WIN32)
# define CLOCK_MONOTONIC     1
# define CLOCK_MONOTONIC_RAW CLOCK_MONOTONIC
# define CLOCK_REALTIME      2

typedef int clockid_t;

struct tm * localtime_r(const time_t *timep, struct tm *result);
int clock_gettime(clockid_t clk_id, struct timespec *tp);
#endif