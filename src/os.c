#if defined(_WIN32)
# include <Windows.h>
# else
# include <unistd.h>
#endif
#include "os.h"

void
os_sleep_sec(unsigned int sec) {
#if defined(_WIN32)
    Sleep(sec * 1000);
#else
    usleep(sec * 1000 * 1000);
#endif
}

void
os_sleep_ms(unsigned int ms) {
#if defined(_WIN32)
    Sleep(ms);
#else
    usleep(ms * 1000);
#endif
}