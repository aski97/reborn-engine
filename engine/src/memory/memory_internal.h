/* Allocation tracking internals (ADR 0012): called by the allocator helpers and the app. */
#ifndef RB_MEMORY_INTERNAL_H
#define RB_MEMORY_INTERNAL_H

#include <reborn/memory.h>

/* Successful calls only: a failed one changes nothing. */
void rb_memory_track_alloc(rb_memory_tag tag, usize size);
void rb_memory_track_resize(rb_memory_tag tag, usize old_size, usize new_size);
void rb_memory_track_free(rb_memory_tag tag, usize size);

/*
 * One INFO line per tag that allocated, one ERROR line per tag with live
 * blocks. True when nothing leaked.
 */
bool rb_memory_log_report(void);

/* Back to zero, for tests that check exact counters. */
void rb_memory_stats_reset(void);

/*
 * "64 MiB" when the size is a whole number of the unit, "33.25 KiB" otherwise,
 * "100 bytes" below 1 KiB. Returns `buf`.
 */
const char *rb_memory_format_size(char *buf, usize buf_size, usize bytes);

#endif /* RB_MEMORY_INTERNAL_H */
