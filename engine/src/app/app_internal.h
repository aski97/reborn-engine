/* Application layer internals exposed to tests (ADR 0011). */
#ifndef RB_APP_INTERNAL_H
#define RB_APP_INTERNAL_H

#include <reborn/app.h>

/* What configure receives before changing anything. */
rb_app_config rb_app_default_config(void);

/* False, with every problem logged, when the run cannot start with `config`. */
bool rb_app_config_validate(const rb_app_config *config);

/* ============================================================================
 * Command line (ADR 0022)
 * ========================================================================== */

/* What the engine's own options asked for. */
typedef struct rb_app_options {
    const char *script_path; /* --script <file>; NULL if not given */
    bool        listen;      /* --listen <port> given */
    u16         port;        /* 0: the system chooses */
} rb_app_options;

typedef enum rb_app_args_result {
    RB_APP_ARGS_RUN,     /* parsed: start the game */
    RB_APP_ARGS_HELP,    /* --help: print the usage, exit with 0 */
    RB_APP_ARGS_INVALID, /* logged: exit with 1 */
} rb_app_args_result;

/* `argv[0]`, the program, is skipped. argc 0 with a NULL argv is no argument at all. */
rb_app_args_result rb_app_parse_args(int argc, char **argv, rb_app_options *options);

#endif /* RB_APP_INTERNAL_H */
