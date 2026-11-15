/*
 * Stateless OS services, Linux (ADR 0008). Not X11-specific, hence the
 * `linux_` prefix rather than `xlib_`.
 */

/*
 * isatty and STDERR_FILENO are POSIX, not C17: with -std=c17 glibc hides them
 * unless asked. Must come before any system header, included ones too.
 */
#define _POSIX_C_SOURCE 200809L

#include "platform/platform_os.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

bool rb_platform_console_init(void) {
    /* Terminals that cannot show colors announce themselves with TERM=dumb. */
    const char *term = getenv("TERM");
    if (term != NULL && strcmp(term, "dumb") == 0) {
        return false;
    }
    /* Terminals are UTF-8 and understand ANSI sequences: nothing to set up. */
    return isatty(STDERR_FILENO) == 1;
}

void rb_platform_console_write(const char *text, usize length) {
    fwrite(text, 1, length, stderr);
}

/*
 * The kernel lists the process tracing us (a debugger) as "TracerPid:" in
 * /proc/self/status; 0 means none.
 */
bool rb_platform_debugger_attached(void) {
    FILE *status = fopen("/proc/self/status", "r");
    if (status == NULL) {
        return false;
    }
    static const char key[] = "TracerPid:";
    bool              attached = false;
    char              line[256];
    while (fgets(line, sizeof(line), status) != NULL) {
        if (strncmp(line, key, sizeof(key) - 1) == 0) {
            attached = strtol(line + sizeof(key) - 1, NULL, 10) != 0;
            break;
        }
    }
    fclose(status);
    return attached;
}
