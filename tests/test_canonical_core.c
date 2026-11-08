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
