/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef IFS_SFS_V2_READER_H
#define IFS_SFS_V2_READER_H
#include <infiltratr/filesystem_module_v2.h>
#include <stdint.h>
typedef struct IfsSfsReader {
    IfsFsHostRuntimeV2 host;
    uint32_t block_bytes;
    uint32_t total_blocks;
    uint32_t object_root;
    uint32_t extent_root;
} IfsSfsReader;
int ifs_sfs_ro_validate(const IfsSfsReader *);
int ifs_sfs_ro_label(const IfsSfsReader *,char *,uint32_t,uint32_t *);
int ifs_sfs_ro_stat(const IfsSfsReader *,const char *,IfsFsEntryV2 *);
int ifs_sfs_ro_list(const IfsSfsReader *,const char *,uint32_t,IfsFsEntryV2 *);
int ifs_sfs_ro_read(const IfsSfsReader *,const char *,uint64_t,void *,uint32_t);
#endif
