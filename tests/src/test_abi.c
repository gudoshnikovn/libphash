#include "libphash.h"
#include "test_macros.h"
#include <stddef.h>
#include <stdio.h>

/* Layout of the public structs is part of the ABI: a consumer compiled against one
 * libphash.h and linked with another shared library of the same soname reads these
 * fields at the offsets it was compiled with. This file pins them, so that a layout
 * change shows up as a failing test in the diff that makes it rather than as a
 * consumer reading garbage. Until the 2.0.0 tag a deliberate change updates the
 * expected values here; after it, a change here is a new major version. */

/* The hash arrays in the batch structs are sized by their own capacity, not by
 * PH_HASH_FLAGS_COUNT, so adding a uint64_t algorithm inside 2.x does not move `status`
 * or change the struct size. */
static void test_batch_hash_capacity_is_decoupled_from_flag_count(void) {
    ASSERT_INT_EQ(8, PH_BATCH_HASHES_CAPACITY);
    ASSERT(PH_HASH_FLAGS_COUNT <= PH_BATCH_HASHES_CAPACITY);

    ph_batch_item_t item;
    ph_batch_buffer_item_t buffer_item;
    ASSERT_INT_EQ(PH_BATCH_HASHES_CAPACITY, (int)(sizeof(item.hashes) / sizeof(item.hashes[0])));
    ASSERT_INT_EQ(PH_BATCH_HASHES_CAPACITY,
                  (int)(sizeof(buffer_item.hashes) / sizeof(buffer_item.hashes[0])));
    PASS("test_batch_hash_capacity_is_decoupled_from_flag_count");
}

/* Field order holds on every target; the exact byte offsets below depend on pointer width
 * and on the target's uint64_t alignment, so they are pinned only for 64-bit targets,
 * which is every platform in the CI matrix. */
static void test_batch_struct_layout(void) {
    ASSERT_INT_EQ(0, (int)offsetof(ph_batch_item_t, path));
    ASSERT(offsetof(ph_batch_item_t, hashes) >= sizeof(const char *));
    ASSERT_INT_EQ(
        (int)(offsetof(ph_batch_item_t, hashes) + PH_BATCH_HASHES_CAPACITY * sizeof(uint64_t)),
        (int)offsetof(ph_batch_item_t, status));

    ASSERT_INT_EQ(0, (int)offsetof(ph_batch_buffer_item_t, buffer));
    ASSERT(offsetof(ph_batch_buffer_item_t, length) >= sizeof(const uint8_t *));
    ASSERT(offsetof(ph_batch_buffer_item_t, hashes) >=
           offsetof(ph_batch_buffer_item_t, length) + sizeof(size_t));
    ASSERT_INT_EQ((int)(offsetof(ph_batch_buffer_item_t, hashes) +
                        PH_BATCH_HASHES_CAPACITY * sizeof(uint64_t)),
                  (int)offsetof(ph_batch_buffer_item_t, status));

    if (sizeof(void *) == 8) {
        ASSERT_INT_EQ(8, (int)offsetof(ph_batch_item_t, hashes));
        ASSERT_INT_EQ(72, (int)offsetof(ph_batch_item_t, status));
        ASSERT_INT_EQ(80, (int)sizeof(ph_batch_item_t));

        ASSERT_INT_EQ(8, (int)offsetof(ph_batch_buffer_item_t, length));
        ASSERT_INT_EQ(16, (int)offsetof(ph_batch_buffer_item_t, hashes));
        ASSERT_INT_EQ(80, (int)offsetof(ph_batch_buffer_item_t, status));
        ASSERT_INT_EQ(88, (int)sizeof(ph_batch_buffer_item_t));
    } else {
        printf("[INFO] %d-bit target: exact batch struct offsets not pinned\n",
               (int)(sizeof(void *) * 8));
    }
    PASS("test_batch_struct_layout");
}

int main(void) {
    test_batch_hash_capacity_is_decoupled_from_flag_count();
    test_batch_struct_layout();
    printf("test_abi: PASSED\n");
    return 0;
}
