/* Command scripts given with --script (ADR 0022): read whole, then line by line. */
#include "command/command_internal.h"

#include <reborn/assert.h>
#include <reborn/log.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>

void rb_command_script_init(rb_command_script *script, char *text, usize size) {
    RB_ASSERT(script != NULL && text != NULL);
    RB_ASSERT(text[size] == '\0');
    *script = (rb_command_script){.text = text, .size = size};
}

/*
 * C's stdio, not a file system layer (none exists yet): enough to read one
 * file whole. "rb" so Windows does not turn "\r\n" into "\n" behind our back;
 * the reader drops the '\r' itself, the same on every OS.
 */
bool rb_command_script_load(rb_command_script *script, rb_arena *arena, const char *path) {
    RB_ASSERT(script != NULL && arena != NULL && path != NULL);
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        RB_LOG_ERROR("Script \"%s\": cannot open it (%s)", path, strerror(errno));
        return false;
    }
    /*
     * Read up to one byte past the limit instead of asking for the size first:
     * fseek/ftell sizes are not portable for every kind of file (pipes).
     */
    char *text = rb_arena_push_uninit(arena, RB_COMMAND_SCRIPT_MAX + 2, 1);
    if (text == NULL) {
        fclose(file);
        return false;
    }
    usize size = fread(text, 1, RB_COMMAND_SCRIPT_MAX + 1, file);
    bool  failed = ferror(file) != 0;
    fclose(file);
    if (failed) {
        RB_LOG_ERROR("Script \"%s\": read failed", path);
        return false;
    }
    if (size > RB_COMMAND_SCRIPT_MAX) {
        RB_LOG_ERROR("Script \"%s\": larger than %u bytes", path, (u32)RB_COMMAND_SCRIPT_MAX);
        return false;
    }
    text[size] = '\0';
    rb_command_script_init(script, text, size);
    RB_LOG_INFO("Script \"%s\": %zu bytes", path, size);
    return true;
}

bool rb_command_script_next(rb_command_script *script, const char **line) {
    RB_ASSERT(script != NULL && line != NULL);
    if (script->position >= script->size) {
        return false;
    }
    char *start = script->text + script->position;
    char *end = memchr(start, '\n', script->size - script->position);
    if (end == NULL) {
        end = script->text + script->size;
        script->position = script->size;
    } else {
        script->position = (usize)(end - script->text) + 1;
    }
    *end = '\0';
    if (end > start && end[-1] == '\r') {
        end[-1] = '\0';
    }
    ++script->line_number;
    *line = start;
    return true;
}
