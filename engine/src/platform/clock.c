/*
 * Portable part of the clock (ADR 0009). Reading the clock and sleeping are
 * per OS: platform/win32/win32_clock.c, platform/linux/linux_clock.c.
 */
#include <reborn/clock.h>

#include "platform/clock_internal.h"

u64 rb_time_ticks_to_ns(u64 ticks, u64 frequency) {
    /*
     * The obvious ticks * 1e9 / frequency overflows u64 once ticks passes
     * 1.8e10: after 30 minutes at 10 MHz (the usual QueryPerformanceFrequency),
     * and uptime counts from the boot. Whole seconds and the remainder are
     * converted apart: the remainder is below `frequency`, so its product stays
     * below 2^64 up to 18 GHz. The sum equals the exact rounded-down result.
     */
    u64 seconds = ticks / frequency;
    u64 rest = ticks % frequency;
    return seconds * RB_NS_PER_S + rest * RB_NS_PER_S / frequency;
}

f64 rb_time_to_seconds(u64 ns) {
    /*
     * A double holds integers exactly only up to 2^53 (104 days in ns): split
     * whole seconds from the fraction so long durations keep their nanoseconds.
     * Dividing by 1e9 rounds once; multiplying by 1e-9 would round twice, as
     * 1e-9 has no exact binary form.
     */
    return (f64)(ns / RB_NS_PER_S) + (f64)(ns % RB_NS_PER_S) / 1e9;
}
