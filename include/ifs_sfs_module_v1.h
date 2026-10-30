/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef IFS_SFS_MODULE_V1_H
#define IFS_SFS_MODULE_V1_H
#include "ifs_defrag_services_v1.h"
#define IFS_SFS_MODULE_V1 1u
#define IFS_SFS_CAP_RUN UINT64_C(1)
typedef struct {
 uint32_t abi_version;
 uint32_t structure_bytes;
 const char *format_id;
 uint64_t capabilities;
 int (*run)(int argc, char **argv, const IfsDefragServicesV1 *services);
} IfsSfsModuleV1;
const IfsSfsModuleV1 *infiltrator_filesystem_sfs_v1(void);
#endif
