/* Application layer internals exposed to tests (ADR 0011). */
#ifndef RB_APP_INTERNAL_H
#define RB_APP_INTERNAL_H

#include <reborn/app.h>

/* What configure receives before changing anything. */
rb_app_config rb_app_default_config(void);

/* False, with every problem logged, when the run cannot start with `config`. */
bool rb_app_config_validate(const rb_app_config *config);

#endif /* RB_APP_INTERNAL_H */
