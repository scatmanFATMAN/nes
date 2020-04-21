#if defined(_WIN32)
# include <Windows.h>
#endif
#include "time.h"

#if defined(_WIN32)
struct tm *
localtime_r(const time_t *timep, struct tm *result) {
    localtime_s(result, timep);
    return result;
}


//http://openvswitch.org/pipermail/dev/2014-March/037249.html
static void
clock_gettime_unix_epoch(ULARGE_INTEGER * unix_epoch) {
    SYSTEMTIME unix_epoch_st = {1970, 1, 0, 1, 0, 0, 0, 0};
    FILETIME unix_epoch_ft;

    SystemTimeToFileTime(&unix_epoch_st, &unix_epoch_ft);

    unix_epoch->LowPart = unix_epoch_ft.dwLowDateTime;
    unix_epoch->HighPart = unix_epoch_ft.dwHighDateTime;
}

int
clock_gettime(clockid_t clk_id, struct timespec *tp) {
    static ULARGE_INTEGER unix_epoch = {0, 0};
    static LARGE_INTEGER freq = {0, 0};
    LARGE_INTEGER now;
    long long int ns;
    FILETIME now_ft;

    switch (clk_id) {
        case CLOCK_REALTIME:
            if (unix_epoch.QuadPart == 0) {
                clock_gettime_unix_epoch(&unix_epoch);
            }

            //get the current time in UTC as a 64-bit value representing the number of
            //100-nanosecond intervals since Jan 1, 1601
            GetSystemTimePreciseAsFileTime(&now_ft);
            now.LowPart = now_ft.dwLowDateTime;
            now.HighPart = now_ft.dwHighDateTime;

            tp->tv_sec = (now.QuadPart - unix_epoch.QuadPart) / 10000000;
            tp->tv_nsec = ((now.QuadPart - unix_epoch.QuadPart) % 10000000) * 100;

            return 0;
        case CLOCK_MONOTONIC:
            //number of counts per second, only initialize once
            if (freq.QuadPart == 0) {
                QueryPerformanceFrequency(&freq);
            }

            //total number of counts from a starting point
            QueryPerformanceCounter(&now);

            //total nano seconds from a starting point
            ns = now.QuadPart / freq.QuadPart * 1000000000;

            tp->tv_sec = now.QuadPart / freq.QuadPart;
            tp->tv_nsec = ns % 1000000000;

            return 0;
    }

    return EINVAL;
}
#endif