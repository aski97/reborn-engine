#include "rb_test.h"

#include "platform/platform_os.h"

/* ============================================================================
 * Sizes
 * ========================================================================== */

RB_TEST(memory, page_size_is_a_power_of_two) {
    usize page = rb_platform_page_size();
    RB_EXPECT(page >= 4096);
    RB_EXPECT((page & (page - 1)) == 0);
}

RB_TEST(memory, reserve_granularity_is_whole_pages) {
    usize granularity = rb_platform_reserve_granularity();
    RB_EXPECT(granularity >= rb_platform_page_size());
    RB_EXPECT(granularity % rb_platform_page_size() == 0);
}

/* ============================================================================
 * Reserve, commit, decommit, release
 * ========================================================================== */

RB_TEST(memory, reserve_is_aligned_to_granularity) {
    usize size = rb_platform_page_size();
    void *ptr = rb_platform_memory_reserve(size);
    RB_REQUIRE(ptr != NULL);
    RB_EXPECT((usize)ptr % rb_platform_reserve_granularity() == 0);
    rb_platform_memory_release(ptr, size);
}

/* Touches the first and last byte of every page: enough to fault on one not committed. */
static bool pages_hold(const u8 *base, usize size, u8 value) {
    usize page = rb_platform_page_size();
    for (usize offset = 0; offset < size; offset += page) {
        if (base[offset] != value || base[offset + page - 1] != value) {
            return false;
        }
    }
    return true;
}

static void pages_fill(u8 *base, usize size, u8 value) {
    usize page = rb_platform_page_size();
    for (usize offset = 0; offset < size; offset += page) {
        base[offset] = value;
        base[offset + page - 1] = value;
    }
}

RB_TEST(memory, commit_gives_zeroed_writable_pages) {
    usize page = rb_platform_page_size();
    u8   *base = rb_platform_memory_reserve(4 * page);
    RB_REQUIRE(base != NULL);
    /* Only the middle two pages: the commit is not required to start at the reservation. */
    RB_REQUIRE(rb_platform_memory_commit(base + page, 2 * page));
    RB_EXPECT(pages_hold(base + page, 2 * page, 0));
    pages_fill(base + page, 2 * page, 0xAB);
    RB_EXPECT(pages_hold(base + page, 2 * page, 0xAB));
    rb_platform_memory_release(base, 4 * page);
}

/* The arena commits ahead in chunks; committing an overlapping range must not wipe data. */
RB_TEST(memory, commit_again_keeps_contents) {
    usize page = rb_platform_page_size();
    u8   *base = rb_platform_memory_reserve(2 * page);
    RB_REQUIRE(base != NULL);
    RB_REQUIRE(rb_platform_memory_commit(base, page));
    pages_fill(base, page, 0x5C);
    RB_REQUIRE(rb_platform_memory_commit(base, 2 * page));
    RB_EXPECT(pages_hold(base, page, 0x5C));
    RB_EXPECT(pages_hold(base + page, page, 0));
    rb_platform_memory_release(base, 2 * page);
}

RB_TEST(memory, decommit_then_commit_reads_zero) {
    usize page = rb_platform_page_size();
    u8   *base = rb_platform_memory_reserve(2 * page);
    RB_REQUIRE(base != NULL);
    RB_REQUIRE(rb_platform_memory_commit(base, 2 * page));
    pages_fill(base, 2 * page, 0x77);
    rb_platform_memory_decommit(base + page, page);
    RB_REQUIRE(rb_platform_memory_commit(base + page, page));
    /* Only the decommitted page was dropped. */
    RB_EXPECT(pages_hold(base, page, 0x77));
    RB_EXPECT(pages_hold(base + page, page, 0));
    rb_platform_memory_release(base, 2 * page);
}

/* What the arena relies on (ADR 0010): a huge reservation is just addresses, no RAM. */
RB_TEST(memory, huge_reservation_commits_only_what_is_used) {
    usize page = rb_platform_page_size();
    usize size = (usize)RB_GIB(64);
    u8   *base = rb_platform_memory_reserve(size);
    RB_REQUIRE(base != NULL);
    /* The two ends, 64 GiB apart. */
    RB_REQUIRE(rb_platform_memory_commit(base, page));
    RB_REQUIRE(rb_platform_memory_commit(base + size - page, page));
    pages_fill(base, page, 0x11);
    pages_fill(base + size - page, page, 0x22);
    RB_EXPECT(pages_hold(base, page, 0x11));
    RB_EXPECT(pages_hold(base + size - page, page, 0x22));
    rb_platform_memory_release(base, size);
}
