/* The smallest program against an installed libphash: prints the version and the build
 * it links. See CMakeLists.txt next to this file for the build. */
#include <libphash.h>
#include <stdio.h>

int main(void) {
    ph_context_t *ctx = NULL;
    if (ph_create(&ctx) != PH_SUCCESS) {
        fprintf(stderr, "ph_create failed\n");
        return 1;
    }
    printf("libphash %s (header %s)\n", ph_version(), PH_VERSION_STRING);
    printf("%s\n", ph_get_build_info());
    ph_free(ctx);
    return 0;
}
