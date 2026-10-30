/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "ifs_sfs_module_v1.h"
int ifs_sfs_worker_main(int, char **);
int ifs_bind_services(const IfsDefragServicesV1 *);
void ifs_unbind_services(void);
static int execute(int argc, char **argv, const IfsDefragServicesV1 *host) {
 if(ifs_bind_services(host)!=0) return 1;
 int result=ifs_sfs_worker_main(argc,argv);
 ifs_unbind_services();
 return result;
}
static const IfsSfsModuleV1 sfs = {IFS_SFS_MODULE_V1, sizeof(IfsSfsModuleV1), "sfs", IFS_SFS_CAP_RUN, execute};
const IfsSfsModuleV1 *infiltrator_filesystem_sfs_v1(void) { return &sfs; }
