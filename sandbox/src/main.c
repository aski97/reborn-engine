#include <stdio.h>

#include <reborn/version.h>

int main(void) {
    printf("Reborn Engine v%s\n", rb_version_string());

    if (rb_version_number() != RB_VERSION_NUMBER) {
        fprintf(stderr, "Version mismatch: header %s, library %s\n", RB_VERSION_STRING,
                rb_version_string());
        return 1;
    }
    return 0;
}
