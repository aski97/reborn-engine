#include "rb_test.h"

#include <string.h>

#include <reborn/memory.h>
#include <reborn/random.h>

#include "compression/deflate.h"
#include "failing_allocator.h"
#include "inflate.h"

/*
 * Compresses `in`, checks the output fits the bound, inflates it back and
 * compares. Returns the compressed size, 0 on any failure (already reported).
 */
static usize round_trip(const u8 *in, usize size) {
    usize capacity = rb_deflate_bound(size);
    u8   *packed = RB_ALLOC_ARRAY(rb_heap_allocator(), u8, capacity, RB_MEMORY_TAG_GENERAL);
    u8   *unpacked = RB_ALLOC_ARRAY(rb_heap_allocator(), u8, size + 1, RB_MEMORY_TAG_GENERAL);
    usize written = 0;
    bool  ok = packed != NULL && unpacked != NULL;
    if (ok) {
        written = rb_deflate_compress(in, size, packed, capacity, rb_heap_allocator());
        usize unpacked_size = 0;
        usize consumed = 0;
        ok = written > 0 && written <= capacity &&
             rb_test_inflate(packed, written, unpacked, size + 1, &unpacked_size, &consumed) &&
             unpacked_size == size && consumed == written &&
             (size == 0 || memcmp(unpacked, in, size) == 0);
    }
    RB_EXPECT(ok);
    RB_FREE_ARRAY(rb_heap_allocator(), packed, u8, capacity, RB_MEMORY_TAG_GENERAL);
    RB_FREE_ARRAY(rb_heap_allocator(), unpacked, u8, size + 1, RB_MEMORY_TAG_GENERAL);
    return ok ? written : 0;
}

RB_TEST(deflate, empty_input_is_a_header_and_an_end) {
    u8 dummy = 0;
    /* 3 header bits and the 7-bit end symbol: 2 bytes. */
    RB_EXPECT_EQ_UINT(round_trip(&dummy, 0), 2u);
}

RB_TEST(deflate, short_inputs_stay_literals) {
    const char *text = "ab";
    RB_EXPECT(round_trip((const u8 *)text, 2) > 0);
    RB_EXPECT(round_trip((const u8 *)"x", 1) > 0);
}

RB_TEST(deflate, text_round_trips) {
    const char *text = "the quick brown fox jumps over the lazy dog; the quick brown fox again";
    RB_EXPECT(round_trip((const u8 *)text, strlen(text)) > 0);
}

/* One byte repeated: matches at distance 1 overlap the bytes they produce. */
RB_TEST(deflate, long_run_compresses_to_almost_nothing) {
    static u8 run[100000];
    memset(run, 0x2A, sizeof(run));
    usize written = round_trip(run, sizeof(run));
    RB_EXPECT(written > 0);
    /* 258 bytes per match of about 2 bytes: under 1 KB. */
    RB_EXPECT(written < 1000);
}

/* A repeating pattern longer than a match, shorter than the window. */
RB_TEST(deflate, repeated_pattern_compresses) {
    static u8 data[64 * 1024];
    for (usize i = 0; i < sizeof(data); ++i) {
        data[i] = (u8)((i % 1000) * 31);
    }
    usize written = round_trip(data, sizeof(data));
    RB_EXPECT(written > 0);
    RB_EXPECT(written < sizeof(data) / 20);
}

/* Random bytes have no matches: the output nears the 9/8 worst case and must stay in the bound. */
RB_TEST(deflate, random_bytes_stay_within_the_bound) {
    static u8 data[50000];
    rb_rng    rng = rb_rng_create(1234);
    for (usize i = 0; i < sizeof(data); ++i) {
        data[i] = (u8)rb_rng_u32(&rng);
    }
    RB_EXPECT(round_trip(data, sizeof(data)) > 0);
}

/* Matches far back, beyond the window, and everything in between; every length class. */
RB_TEST(deflate, mixed_data_with_far_matches_round_trips) {
    static u8 data[200000];
    rb_rng    rng = rb_rng_create(99);
    for (usize i = 0; i < sizeof(data);) {
        u32 choice = rb_rng_u32(&rng) % 3;
        u32 count = 1 + rb_rng_u32(&rng) % 300;
        for (u32 k = 0; k < count && i < sizeof(data); ++k, ++i) {
            if (choice == 0 || i < 40000) {
                data[i] = (u8)rb_rng_u32(&rng);
            } else {
                /* A copy from 1 to 40000 bytes back: some within the window, some past it. */
                usize distance = choice == 1 ? 1 + rb_rng_u32(&rng) % 40000 : 3;
                data[i] = data[i - distance];
            }
        }
    }
    RB_EXPECT(round_trip(data, sizeof(data)) > 0);
}

RB_TEST(deflate, scratch_failure_returns_zero) {
    rb_failing_allocator failing;
    rb_failing_allocator_init(&failing, rb_heap_allocator(), 1);
    u8 in[16] = {0};
    u8 out[64];
    RB_EXPECT_EQ_UINT(
        rb_deflate_compress(in, sizeof(in), out, sizeof(out), rb_failing_allocator_get(&failing)),
        0u);
    RB_EXPECT_EQ_UINT(failing.live_bytes, 0u);
}
