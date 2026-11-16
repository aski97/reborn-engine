/*
 * Clock and sleep, Win32 (ADR 0009).
 */
#include <reborn/clock.h>

#include "platform/clock_internal.h"

#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _UNICODE
#include <windows.h>

/* Windows 10 1803 and later; older SDK headers lack the name. */
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
    #define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

u64 rb_time_now(void) {
    /*
     * Both calls cannot fail since Windows XP. The frequency is fixed at boot:
     * asking every time costs a few nanoseconds and needs no state (ADR 0009, C1).
     */
    LARGE_INTEGER counter;
    LARGE_INTEGER frequency;
    QueryPerformanceCounter(&counter);
    QueryPerformanceFrequency(&frequency);
    return rb_time_ticks_to_ns((u64)counter.QuadPart, (u64)frequency.QuadPart);
}

/* One wait on a waitable timer. False if the timer could not be armed. */
static bool win32_wait_timer(HANDLE timer, u64 ns) {
    /* Due time in 100 ns units, rounded up; negative means relative to now. */
    LARGE_INTEGER due;
    due.QuadPart = -(LONGLONG)((ns + 99) / 100);
    if (!SetWaitableTimerEx(timer, &due, 0, NULL, NULL, NULL, 0)) {
        return false;
    }
    return WaitForSingleObject(timer, INFINITE) == WAIT_OBJECT_0;
}

void rb_time_sleep(u64 ns) {
    if (ns == 0) {
        return;
    }
    u64 start = rb_time_now();
    u64 deadline = ns > UINT64_MAX - start ? UINT64_MAX : start + ns;

    /*
     * Sleep() wakes on the system timer tick, every 15.6 ms by default: Sleep(1)
     * can last a whole 60 Hz frame. A high-resolution waitable timer wakes
     * within about half a millisecond without timeBeginPeriod, which changes
     * the tick for the whole process. Created per call to keep the function
     * stateless (a few microseconds).
     */
    HANDLE timer =
        CreateWaitableTimerExW(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);

    /* Timers may fire a little early: loop until the deadline has really passed. */
    for (u64 now = start; now < deadline; now = rb_time_now()) {
        u64 remaining = deadline - now;
        if (timer == NULL || !win32_wait_timer(timer, remaining)) {
            /* Windows before 1803: coarse, but still at least the time asked. */
            u64 ms = (remaining + RB_NS_PER_MS - 1) / RB_NS_PER_MS;
            Sleep(ms < INFINITE ? (DWORD)ms : INFINITE - 1);
        }
    }

    if (timer != NULL) {
        CloseHandle(timer);
    }
}
