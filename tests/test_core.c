#include "libphash.h"
#include "test_macros.h"

void test_lifecycle() {
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));
    if (!ctx)
        exit(1);
    ph_free(ctx);
    printf("test_lifecycle: PASSED\n");
}

int main() {
    test_lifecycle();
    return 0;
}
