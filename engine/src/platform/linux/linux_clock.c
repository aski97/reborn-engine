/*
 * Clock and sleep, Linux (ADR 0009). POSIX only, not X11-specific.
 */

/* clock_gettime and clock_nanosleep are POSIX, hidden by -std=c17 unless asked. */
#define _POSIX_C_SOURCE 200809L

#include <reborn/clock.h>

#include <errno.h>
#include <time.h>

u64 rb_time_now(void) {
    /*
     * Cannot fail: CLOCK_MONOTONIC always exists and the pointer is valid. Not
     * CLOCK_MONOTONIC_RAW: NTP only nudges the rate by parts per million (ADR 0009).
     * Served from user space by the vDSO, without entering the kernel.
     */
    struct timespec now = {0};
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (u64)now.tv_sec * RB_NS_PER_S + (u64)now.tv_nsec;
}

void rb_time_sleep(u64 ns) {
    if (ns == 0) {
        return;
    }
    struct timespec deadline = {0};
    clock_gettime(CLOCK_MONOTONIC, &deadline);
    deadline.tv_sec += (time_t)(ns / RB_NS_PER_S);
    deadline.tv_nsec += (long)(ns % RB_NS_PER_S);
    if (deadline.tv_nsec >= (long)RB_NS_PER_S) {
        deadline.tv_sec += 1;
        deadline.tv_nsec -= (long)RB_NS_PER_S;
    }

    /*
     * An absolute deadline on the same clock as rb_time_now: never returns
     * early, and a signal (EINTR) simply waits again for the same instant,
     * with no drift. Returns the error number itself, not -1 with errno.
     */
    while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &deadline, NULL) == EINTR) {
    }
}
