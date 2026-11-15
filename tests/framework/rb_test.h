/*
 * Minimal unit test framework (ADR 0004).
 *
 *   RB_TEST(suite, name) {
 *       RB_REQUIRE(ptr != NULL);           // stops the test on failure
 *       RB_EXPECT_EQ_INT(ptr->count, 3);   // records the failure and continues
 *   }
 *
 * Tests register themselves before main() and are run by rb_test.c, which
 * also lists them for CTest so that every test runs in its own process.
 */
#ifndef RB_TEST_H
#define RB_TEST_H

#include <reborn/defines.h>

/* ============================================================================
 * Registration
 * ========================================================================== */

typedef void (*rb_test_fn)(void);

typedef struct rb_test_case {
    const char          *suite;
    const char          *name;
    rb_test_fn           fn;
    struct rb_test_case *next;
} rb_test_case;

void rb_test_register(rb_test_case *test);

/*
 * Runs `fn` before main(). Clang targeting MSVC also honours the attribute
 * (it emits the pointer into .CRT$XCU itself), so one branch covers both of
 * our platforms. MSVC cl.exe would need the .CRT$XCU section written by hand:
 * not supported until the engine supports that compiler.
 */
#if defined(RB_COMPILER_CLANG) || defined(RB_COMPILER_GCC)
    #define RB_TEST_CONSTRUCTOR_(fn)                                                               \
        static void fn(void) __attribute__((constructor));                                         \
        static void fn(void)
#else
    #error "rb_test.h: test auto-registration is only implemented for Clang and GCC"
#endif

/*
 * The double underscore keeps `RB_TEST(a_b, c)` and `RB_TEST(a, b_c)` from
 * producing the same identifiers.
 */
#define RB_TEST(suite, name)                                                                       \
    static void         rb_test_fn_##suite##__##name(void);                                        \
    static rb_test_case rb_test_case_##suite##__##name = {#suite, #name,                           \
                                                          rb_test_fn_##suite##__##name, NULL};     \
    RB_TEST_CONSTRUCTOR_(rb_test_register_##suite##__##name) {                                     \
        rb_test_register(&rb_test_case_##suite##__##name);                                         \
    }                                                                                              \
    static void rb_test_fn_##suite##__##name(void)

/* ============================================================================
 * Checks
 * ========================================================================== */

/*
 * Each check reports its own failure and returns whether it passed; the
 * macros only decide what happens next. RB_REQUIRE_* stops the test with a
 * plain `return`, so it only works in the test body itself, not in helper
 * functions (ADR 0004).
 */
bool rb_test_check(bool ok, const char *expr, const char *file, int line);
bool rb_test_check_int(i64 a, i64 b, const char *ea, const char *eb, const char *file, int line);
bool rb_test_check_uint(u64 a, u64 b, const char *ea, const char *eb, const char *file, int line);
bool rb_test_check_str(const char *a, const char *b, const char *ea, const char *eb,
                       const char *file, int line);
bool rb_test_check_near(f64 a, f64 b, f64 eps, const char *ea, const char *eb, const char *file,
                        int line);

#define RB_TEST_CHECK_(passed, on_fail)                                                            \
    do {                                                                                           \
        if (!(passed)) {                                                                           \
            on_fail;                                                                               \
        }                                                                                          \
    } while (0)

#define RB_TEST_IF_(cond)        rb_test_check((cond), #cond, __FILE__, __LINE__)
#define RB_TEST_EQ_INT_(a, b)    rb_test_check_int((a), (b), #a, #b, __FILE__, __LINE__)
#define RB_TEST_EQ_UINT_(a, b)   rb_test_check_uint((a), (b), #a, #b, __FILE__, __LINE__)
#define RB_TEST_EQ_STR_(a, b)    rb_test_check_str((a), (b), #a, #b, __FILE__, __LINE__)
#define RB_TEST_NEAR_(a, b, eps) rb_test_check_near((a), (b), (eps), #a, #b, __FILE__, __LINE__)

/* Floats must never be compared with ==: use RB_EXPECT_NEAR with a tolerance. */
#define RB_EXPECT(cond)           RB_TEST_CHECK_(RB_TEST_IF_(cond), (void)0)
#define RB_EXPECT_EQ_INT(a, b)    RB_TEST_CHECK_(RB_TEST_EQ_INT_(a, b), (void)0)
#define RB_EXPECT_EQ_UINT(a, b)   RB_TEST_CHECK_(RB_TEST_EQ_UINT_(a, b), (void)0)
#define RB_EXPECT_EQ_STR(a, b)    RB_TEST_CHECK_(RB_TEST_EQ_STR_(a, b), (void)0)
#define RB_EXPECT_NEAR(a, b, eps) RB_TEST_CHECK_(RB_TEST_NEAR_(a, b, eps), (void)0)

#define RB_REQUIRE(cond)           RB_TEST_CHECK_(RB_TEST_IF_(cond), return)
#define RB_REQUIRE_EQ_INT(a, b)    RB_TEST_CHECK_(RB_TEST_EQ_INT_(a, b), return)
#define RB_REQUIRE_EQ_UINT(a, b)   RB_TEST_CHECK_(RB_TEST_EQ_UINT_(a, b), return)
#define RB_REQUIRE_EQ_STR(a, b)    RB_TEST_CHECK_(RB_TEST_EQ_STR_(a, b), return)
#define RB_REQUIRE_NEAR(a, b, eps) RB_TEST_CHECK_(RB_TEST_NEAR_(a, b, eps), return)

#endif /* RB_TEST_H */
