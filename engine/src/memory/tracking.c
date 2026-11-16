/*
 * Allocation tracking by tag (ADR 0012, A2, B1). The allocator helpers call
 * in here after every successful operation, so any allocator is counted, once.
 */
#include "memory/memory_internal.h"

#include <reborn/assert.h>
#include <reborn/log.h>

#include <stdio.h>

/*
 * Global, like the logger: every allocator value, whatever its source, counts
 * into the same table. Not thread-safe: needs atomics or per-thread tables
 * once threads exist.
 */
static rb_memory_stats g_memory_stats[RB_MEMORY_TAG_COUNT];

static const char *const g_memory_tag_names[RB_MEMORY_TAG_COUNT] = {
    [RB_MEMORY_TAG_GENERAL] = "general",
    [RB_MEMORY_TAG_PLATFORM] = "platform",
    [RB_MEMORY_TAG_GAME] = "game",
};

/*
 * The cast to unsigned folds a negative value into a huge one: one comparison
 * rejects both ends (an enum may be signed).
 */
static bool memory_tag_is_valid(rb_memory_tag tag) {
    return (unsigned)tag < RB_MEMORY_TAG_COUNT;
}

/* ============================================================================
 * Queries
 * ========================================================================== */

const char *rb_memory_tag_name(rb_memory_tag tag) {
    return memory_tag_is_valid(tag) ? g_memory_tag_names[tag] : "unknown";
}

rb_memory_stats rb_memory_get_stats(rb_memory_tag tag) {
    RB_ASSERT(memory_tag_is_valid(tag));
    return g_memory_stats[tag];
}

/* ============================================================================
 * Counting
 * ========================================================================== */

static void memory_add_bytes(rb_memory_stats *stats, usize size) {
    stats->live_bytes += size;
    if (stats->live_bytes > stats->peak_bytes) {
        stats->peak_bytes = stats->live_bytes;
    }
}

/*
 * A tag giving back more than it holds can only come from a wrong size or tag
 * passed back: the unsigned counters would wrap to huge values instead.
 */
static void memory_remove_bytes(rb_memory_tag tag, rb_memory_stats *stats, usize size) {
    RB_ASSERT_MSG(size <= stats->live_bytes, "%s frees %zu bytes, only %zu allocated",
                  rb_memory_tag_name(tag), size, stats->live_bytes);
    stats->live_bytes -= size;
}

void rb_memory_track_alloc(rb_memory_tag tag, usize size) {
    rb_memory_stats *stats = &g_memory_stats[tag];
    memory_add_bytes(stats, size);
    ++stats->live_blocks;
    ++stats->alloc_count;
}

void rb_memory_track_resize(rb_memory_tag tag, usize old_size, usize new_size) {
    rb_memory_stats *stats = &g_memory_stats[tag];
    if (new_size >= old_size) {
        memory_add_bytes(stats, new_size - old_size);
    } else {
        memory_remove_bytes(tag, stats, old_size - new_size);
    }
}

void rb_memory_track_free(rb_memory_tag tag, usize size) {
    rb_memory_stats *stats = &g_memory_stats[tag];
    RB_ASSERT_MSG(stats->live_blocks > 0, "%s frees a block, none allocated",
                  rb_memory_tag_name(tag));
    memory_remove_bytes(tag, stats, size);
    --stats->live_blocks;
    ++stats->free_count;
}

void rb_memory_stats_reset(void) {
    for (usize i = 0; i < RB_MEMORY_TAG_COUNT; ++i) {
        g_memory_stats[i] = (rb_memory_stats){0};
    }
}

/* ============================================================================
 * Report
 * ========================================================================== */

const char *rb_memory_format_size(char *buf, usize buf_size, usize bytes) {
    static const struct {
        usize       size;
        const char *name;
    } units[] = {{RB_GIB(1), "GiB"}, {RB_MIB(1), "MiB"}, {RB_KIB(1), "KiB"}};

    for (usize i = 0; i < RB_ARRAY_COUNT(units); ++i) {
        if (bytes < units[i].size) {
            continue;
        }
        /* Exact for the round sizes configs use, so "256 MiB" does not read "256.00 MiB". */
        if (bytes % units[i].size == 0) {
            snprintf(buf, buf_size, "%zu %s", bytes / units[i].size, units[i].name);
        } else {
            snprintf(buf, buf_size, "%.2f %s", (double)bytes / (double)units[i].size,
                     units[i].name);
        }
        return buf;
    }
    snprintf(buf, buf_size, "%zu bytes", bytes);
    return buf;
}

/* "1 block", "2 blocks": the report reads as a sentence. */
static const char *memory_plural(u64 count, const char *one, const char *many) {
    return count == 1 ? one : many;
}

bool rb_memory_log_report(void) {
    bool clean = true;
    for (usize i = 0; i < RB_MEMORY_TAG_COUNT; ++i) {
        const rb_memory_stats *stats = &g_memory_stats[i];
        const char            *name = g_memory_tag_names[i];
        /* A tag nothing used would be a line of zeros. */
        if (stats->alloc_count == 0) {
            continue;
        }
        char peak[32];
        RB_LOG_INFO("Memory %s: peak %s, %llu %s, %llu %s", name,
                    rb_memory_format_size(peak, sizeof(peak), stats->peak_bytes),
                    (unsigned long long)stats->alloc_count,
                    memory_plural(stats->alloc_count, "allocation", "allocations"),
                    (unsigned long long)stats->free_count,
                    memory_plural(stats->free_count, "free", "frees"));
        if (stats->live_blocks > 0) {
            char live[32];
            RB_LOG_ERROR("Memory leak (%s): %zu %s, %s never freed", name, stats->live_blocks,
                         memory_plural(stats->live_blocks, "block", "blocks"),
                         rb_memory_format_size(live, sizeof(live), stats->live_bytes));
            clean = false;
        }
    }
    return clean;
}
