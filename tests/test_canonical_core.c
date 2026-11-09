/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Exercise the exact format-policy implementation linked in SFS.so. */
#include "sfs_core.h"
#include <stdio.h>

#define CHECK(c) do { if (!(c)) {     fprintf(stderr, "SFS shared core failed: %s\n", #c); return 1; } } while (0)

int main(void)
{
    ifs_sfs_u8 bounded[IFS_SFS_MAX_FILENAME];
    ifs_sfs_u16 expected = IFS_SFS_MAX_FILENAME;
    ifs_sfs_u32 i;

    CHECK(ifs_sfs_validate_root_probe(IFS_SFS_ROOT_ID,
            IFS_SFS_STRUCTURE_VERSION, 512U, 100U) == IFS_SFS_ROOT_OK);
    CHECK(ifs_sfs_validate_root_probe(0U,
            IFS_SFS_STRUCTURE_VERSION, 512U, 100U) == IFS_SFS_ROOT_BAD_ID);
    CHECK(ifs_sfs_validate_root_probe(IFS_SFS_ROOT_ID,
            IFS_SFS_STRUCTURE_VERSION + 1U, 512U, 100U) ==
          IFS_SFS_ROOT_BAD_VERSION);
    CHECK(ifs_sfs_validate_root_probe(IFS_SFS_ROOT_ID,
            IFS_SFS_STRUCTURE_VERSION, 513U, 100U) ==
          IFS_SFS_ROOT_INVALID_BLOCK_SIZE);
    CHECK(ifs_sfs_validate_root_probe(IFS_SFS_ROOT_ID,
            IFS_SFS_STRUCTURE_VERSION, 512U, 2U) ==
          IFS_SFS_ROOT_INVALID_TOTAL_BLOCKS);
    CHECK(ifs_sfs_validate_root_layout(IFS_SFS_ROOT_ID,
            IFS_SFS_STRUCTURE_VERSION, 512U, 100U,
            1U, 2U, 3U, 4U, 5U) == IFS_SFS_ROOT_OK);
    CHECK(ifs_sfs_validate_root_layout(IFS_SFS_ROOT_ID,
            IFS_SFS_STRUCTURE_VERSION, 512U, 100U,
            0U, 2U, 3U, 4U, 5U) ==
          IFS_SFS_ROOT_BLOCK_REFERENCE_OUT_OF_RANGE);
    CHECK(ifs_sfs_validate_root_layout(IFS_SFS_ROOT_ID,
            IFS_SFS_STRUCTURE_VERSION, 512U, 100U,
            1U, 2U, 98U, 4U, 5U) ==
          IFS_SFS_ROOT_TRANSACTION_BLOCK_OUT_OF_RANGE);
    {
        ifs_sfs_u32 capacity = 0U;
        ifs_sfs_u32 blocks = 0U;
        ifs_sfs_u32 free_count = 0U;
        ifs_sfs_u32 mask = 0U;
        CHECK(ifs_sfs_compute_bitmap_layout(512U, 8001U,
                  &capacity, &blocks) == 0);
        CHECK(capacity == 4000U && blocks == 3U);
        CHECK(ifs_sfs_compute_bitmap_layout(512U, 8000U,
                  &capacity, &blocks) == 0);
        CHECK(capacity == 4000U && blocks == 2U);
        CHECK(ifs_sfs_compute_bitmap_layout(512U, 8001U,
                  0, &blocks) != 0);
        CHECK(ifs_sfs_compute_bitmap_layout(12U, 8001U,
                  &capacity, &blocks) != 0);
        CHECK(ifs_sfs_adminspace_block_mask(10U, 10U, &mask) == 0);
        CHECK(mask == 0x80000000U);
        CHECK(ifs_sfs_adminspace_block_mask(10U, 41U, &mask) == 0);
        CHECK(mask == 1U);
        CHECK(ifs_sfs_adminspace_block_mask(10U, 42U, &mask) != 0);
        CHECK(ifs_sfs_has_allocation_headroom(10U, 7U, 3U) != 0);
        CHECK(!ifs_sfs_has_allocation_headroom(10U, 8U, 3U));
        CHECK(ifs_sfs_free_count_after_allocate(10U, 3U, &free_count) == 0);
        CHECK(free_count == 7U);
        CHECK(ifs_sfs_free_count_after_allocate(7U, 8U, &free_count) != 0);
        CHECK(ifs_sfs_free_count_after_release(7U, 3U, 10U,
                  &free_count) == 0);
        CHECK(free_count == 10U);
        CHECK(ifs_sfs_free_count_after_release(7U, 4U, 10U,
                  &free_count) != 0);
        CHECK(ifs_sfs_bitmap_word_set(0U, 0U, 2U) == 0xc0000000U);
        CHECK(ifs_sfs_bitmap_word_find_set(0xc0000000U, 1U) == 1);
        CHECK(ifs_sfs_bitmap_word_find_zero(0xc0000000U, 0U) == 2);
        CHECK(ifs_sfs_bitmap_word_clear(0xc0000000U, 1U, 1U) ==
              0x80000000U);
    }
    CHECK(ifs_sfs_validate_name((const ifs_sfs_u8 *)"abc", 3U) ==
          IFS_SFS_NAME_OK);
    CHECK(ifs_sfs_validate_name((const ifs_sfs_u8 *)"abc", 108U) ==
          IFS_SFS_NAME_TOO_LONG);

    /* Deliberately no NUL terminator. Hash must read only this array. */
    for (i = 0U; i < IFS_SFS_MAX_FILENAME; ++i) {
        bounded[i] = (ifs_sfs_u8)'A';
        expected = (ifs_sfs_u16)(expected * 13U + (ifs_sfs_u8)'A');
    }
    CHECK(ifs_sfs_component_hash(bounded, 0) == expected);
    CHECK(ifs_sfs_component_hash(bounded, 1) == expected);
    CHECK(ifs_sfs_component_hash((const ifs_sfs_u8 *)"abc/ignored", 0) ==
          ifs_sfs_component_hash((const ifs_sfs_u8 *)"ABC", 1));
    return 0;
}
