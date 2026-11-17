/*
 * Dynamic libraries, Linux (ADR 0013). POSIX dlopen, not X11-specific.
 */
#include "platform/platform_os.h"

#include <reborn/assert.h>
#include <reborn/log.h>

#include <dlfcn.h>
#include <string.h>

rb_platform_library *rb_platform_library_open(const char *name) {
    RB_ASSERT(name != NULL);
    /*
     * RTLD_NOW resolves every symbol the library needs at once, so a broken
     * library fails here and not at its first call. RTLD_LOCAL keeps its
     * symbols out of the global namespace used by later loads.
     */
    void *handle = dlopen(name, RTLD_NOW | RTLD_LOCAL);
    if (handle == NULL) {
        /* dlerror already names the file and the reason (not found, wrong architecture...). */
        RB_LOG_ERROR("Cannot open library: %s", dlerror());
        return NULL;
    }
    return (rb_platform_library *)handle;
}

rb_platform_proc rb_platform_library_symbol(rb_platform_library *library, const char *name) {
    RB_ASSERT(library != NULL && name != NULL);
    void *symbol = dlsym(library, name);
    /*
     * ISO C has no conversion from an object pointer (void *) to a function
     * pointer: -Wpedantic flags the cast. POSIX guarantees both have the same
     * representation, so the bytes are copied instead.
     */
    _Static_assert(sizeof(symbol) == sizeof(rb_platform_proc), "dlsym needs same-size pointers");
    rb_platform_proc proc;
    memcpy(&proc, &symbol, sizeof(proc));
    return proc;
}

void rb_platform_library_close(rb_platform_library *library) {
    RB_ASSERT(library != NULL);
    if (dlclose(library) != 0) {
        RB_LOG_ERROR("dlclose failed: %s", dlerror());
    }
}
