#include "rb_test.h"

#include "platform/platform_os.h"

/* A library every machine has, exporting the C standard library's abs. */
#if defined(RB_PLATFORM_WINDOWS)
    #define TEST_SYSTEM_LIBRARY "ucrtbase.dll"
#else
    #define TEST_SYSTEM_LIBRARY "libc.so.6"
#endif

typedef int (*test_abs_fn)(int);

RB_TEST(library, symbol_is_callable) {
    rb_platform_library *library = rb_platform_library_open(TEST_SYSTEM_LIBRARY);
    RB_REQUIRE(library != NULL);
    rb_platform_proc proc = rb_platform_library_symbol(library, "abs");
    RB_EXPECT(proc != NULL);
    if (proc != NULL) {
        /* Cast back to the real type before calling (platform_os.h). */
        RB_EXPECT_EQ_INT(((test_abs_fn)proc)(-5), 5);
    }
    rb_platform_library_close(library);
}

RB_TEST(library, missing_symbol_is_null) {
    rb_platform_library *library = rb_platform_library_open(TEST_SYSTEM_LIBRARY);
    RB_REQUIRE(library != NULL);
    RB_EXPECT(rb_platform_library_symbol(library, "rb_no_such_symbol") == NULL);
    rb_platform_library_close(library);
}

RB_TEST(library, missing_library_is_null) {
#if defined(RB_PLATFORM_WINDOWS)
    RB_EXPECT(rb_platform_library_open("rb_no_such_library.dll") == NULL);
#else
    RB_EXPECT(rb_platform_library_open("librb_no_such_library.so") == NULL);
#endif
}
