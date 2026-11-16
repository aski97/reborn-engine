/*
 * Monotonic clock and sleep (ADR 0009).
 *
 *   u64 start = rb_time_now();
 *   ...
 *   f64 seconds = rb_time_to_seconds(rb_time_now() - start);
 *
 * Times are nanoseconds since an arbitrary origin fixed by the OS (usually the
 * boot): a single reading means nothing, only differences between readings do.
 * The clock never goes back, unlike the wall clock. Stateless: callable from
 * the first line of main(), before any startup, from any thread.
 */
#ifndef RB_CLOCK_H
#define RB_CLOCK_H

#include <reborn/defines.h>

#define RB_NS_PER_US UINT64_C(1000)
#define RB_NS_PER_MS UINT64_C(1000000)
#define RB_NS_PER_S  UINT64_C(1000000000)

/* Nanoseconds from the OS origin. Resolution is below a microsecond on both platforms. */
RB_API u64 rb_time_now(void);

/*
 * Blocks the calling thread for at least `ns` nanoseconds; 0 returns at once.
 * The OS may wake it later than asked (typically tens of microseconds, more
 * on a loaded machine): measure the time that really passed, never assume it.
 */
RB_API void rb_time_sleep(u64 ns);

/* Nanoseconds to seconds, for durations used in arithmetic (frame delta, rates). */
RB_API f64 rb_time_to_seconds(u64 ns);

#endif /* RB_CLOCK_H */
