/*
 * Dynamic libraries, Win32 (ADR 0013).
 */
#include "platform/platform_os.h"
#include "platform/win32/win32_error.h"

#include <reborn/assert.h>
#include <reborn/log.h>

#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _UNICODE
#include <windows.h>

#include <stdio.h>

/* File names, not paths: far below this. */
#define WIN32_LIBRARY_NAME_MAX 512

rb_platform_library *rb_platform_library_open(const char *name) {
    RB_ASSERT(name != NULL);
    wchar_t wide[WIN32_LIBRARY_NAME_MAX];
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name, -1, wide,
                            WIN32_LIBRARY_NAME_MAX) == 0) {
        RB_LOG_ERROR("Library name \"%s\" is not valid UTF-8 or is too long", name);
        return NULL;
    }
    /*
     * Plain LoadLibrary also searches the current folder and PATH, where anyone
     * can drop a DLL with the expected name (DLL planting). The default dirs are
     * the application's folder, System32 (where the Vulkan loader lives) and
     * folders added with AddDllDirectory.
     */
    HMODULE module = LoadLibraryExW(wide, NULL, LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (module == NULL) {
        char what[WIN32_LIBRARY_NAME_MAX + 32];
        snprintf(what, sizeof(what), "LoadLibraryExW(\"%s\")", name);
        WIN32_LOG_LAST_ERROR(what);
        return NULL;
    }
    return (rb_platform_library *)module;
}

rb_platform_proc rb_platform_library_symbol(rb_platform_library *library, const char *name) {
    RB_ASSERT(library != NULL && name != NULL);
    /*
     * FARPROC is a function pointer, so a cast suffices. void (*)(void) is the
     * type compilers accept as "any function" without -Wcast-function-type.
     */
    return (rb_platform_proc)GetProcAddress((HMODULE)library, name);
}

void rb_platform_library_close(rb_platform_library *library) {
    RB_ASSERT(library != NULL);
    if (!FreeLibrary((HMODULE)library)) {
        WIN32_LOG_LAST_ERROR("FreeLibrary");
    }
}
