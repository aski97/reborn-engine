/*
 * Test runner: owns main() for the test executable.
 *
 *   reborn_tests                  run every test in this process
 *   reborn_tests --list           print one "suite.name" per line (used by CTest discovery)
 *   reborn_tests --run suite.name run a single test
 *
 * Exit codes: 0 all passed, 1 a test failed, 2 usage error / unknown test / duplicate names.
 */
#include "rb_test.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

/* ============================================================================
 * Registry
 * ========================================================================== */

/* Filled by constructors before main(): static storage only, no allocation. */
static rb_test_case       *g_tests;
static const rb_test_case *g_duplicate; /* first test registered twice, reported by main() */

/* Per-test failure count, reset by run_test(). */
static u32 g_failures;

static int compare_tests(const rb_test_case *a, const rb_test_case *b) {
    int c = strcmp(a->suite, b->suite);
    return c != 0 ? c : strcmp(a->name, b->name);
}

/*
 * Constructors run in an unspecified order across translation units: keeping
 * the list sorted makes --list and full runs deterministic.
 */
void rb_test_register(rb_test_case *test) {
    rb_test_case **link = &g_tests;
    while (*link && compare_tests(*link, test) < 0) {
        link = &(*link)->next;
    }
    /* Same name in two files: CTest would reject the second add_test(). */
    if (*link && compare_tests(*link, test) == 0 && !g_duplicate) {
        g_duplicate = test;
    }
    test->next = *link;
    *link = test;
}

/* ============================================================================
 * Checks
 * ========================================================================== */

/* "file:line: error:" is the format editors and LLM agents already parse. */
static void report_failure(const char *file, int line, const char *check) {
    g_failures++;
    printf("%s:%d: error: %s\n", file, line, check);
}

bool rb_test_check(bool ok, const char *expr, const char *file, int line) {
    if (!ok) {
        report_failure(file, line, "condition is false");
        printf("    %s\n", expr);
    }
    return ok;
}

bool rb_test_check_int(i64 a, i64 b, const char *ea, const char *eb, const char *file, int line) {
    if (a != b) {
        report_failure(file, line, "integers differ");
        printf("    %s = %" PRId64 "\n    %s = %" PRId64 "\n", ea, a, eb, b);
    }
    return a == b;
}

bool rb_test_check_uint(u64 a, u64 b, const char *ea, const char *eb, const char *file, int line) {
    if (a != b) {
        report_failure(file, line, "integers differ");
        printf("    %s = %" PRIu64 "\n    %s = %" PRIu64 "\n", ea, a, eb, b);
    }
    return a == b;
}

bool rb_test_check_str(const char *a, const char *b, const char *ea, const char *eb,
                       const char *file, int line) {
    /* NULL is a legitimate value under test, not a reason to crash the runner. */
    bool equal = (a == b) || (a && b && strcmp(a, b) == 0);
    if (!equal) {
        report_failure(file, line, "strings differ");
        printf("    %s = %s%s%s\n", ea, a ? "\"" : "", a ? a : "NULL", a ? "\"" : "");
        printf("    %s = %s%s%s\n", eb, b ? "\"" : "", b ? b : "NULL", b ? "\"" : "");
    }
    return equal;
}

bool rb_test_check_near(f64 a, f64 b, f64 eps, const char *ea, const char *eb, const char *file,
                        int line) {
    f64 diff = a > b ? a - b : b - a;
    /* Written so that NaN fails: every comparison with NaN is false. */
    bool near = diff <= eps;
    if (!near) {
        report_failure(file, line, "values not within tolerance");
        printf("    %s = %.17g\n    %s = %.17g\n    |diff| = %.17g > %.17g\n", ea, a, eb, b, diff,
               eps);
    }
    return near;
}

/* ============================================================================
 * Runner
 * ========================================================================== */

static bool run_test(const rb_test_case *test) {
    printf("[ RUN  ] %s.%s\n", test->suite, test->name);
    g_failures = 0;
    test->fn();
    printf("[ %s ] %s.%s\n", g_failures == 0 ? " OK " : "FAIL", test->suite, test->name);
    return g_failures == 0;
}

static int run_all(void) {
    u32 passed = 0;
    u32 failed = 0;
    for (const rb_test_case *t = g_tests; t; t = t->next) {
        if (run_test(t)) {
            passed++;
        } else {
            failed++;
        }
    }
    printf("\n%" PRIu32 " passed, %" PRIu32 " failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}

static int run_one(const char *full_name) {
    for (const rb_test_case *t = g_tests; t; t = t->next) {
        usize suite_len = strlen(t->suite);
        if (strncmp(full_name, t->suite, suite_len) == 0 && full_name[suite_len] == '.' &&
            strcmp(full_name + suite_len + 1, t->name) == 0) {
            return run_test(t) ? 0 : 1;
        }
    }
    printf("error: unknown test '%s'\n", full_name);
    return 2;
}

static void list_tests(void) {
    for (const rb_test_case *t = g_tests; t; t = t->next) {
        printf("%s.%s\n", t->suite, t->name);
    }
}

int main(int argc, char **argv) {
    /*
     * Unbuffered: if a test crashes, everything it printed so far (including
     * the failure that probably explains the crash) must already be out.
     */
    setvbuf(stdout, NULL, _IONBF, 0);

    if (g_duplicate) {
        printf("error: test %s.%s is registered more than once\n", g_duplicate->suite,
               g_duplicate->name);
        return 2;
    }

    if (argc == 1) {
        return run_all();
    }
    if (argc == 2 && strcmp(argv[1], "--list") == 0) {
        list_tests();
        return 0;
    }
    if (argc == 3 && strcmp(argv[1], "--run") == 0) {
        return run_one(argv[2]);
    }

    printf("usage: %s [--list | --run suite.name]\n", argv[0]);
    return 2;
}
