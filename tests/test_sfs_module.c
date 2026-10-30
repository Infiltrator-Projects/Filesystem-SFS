/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "ifs_sfs_module_v1.h"
#include <string.h>
#include <stdio.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"failure: %s\n",#x);return 1; } } while(0)
int main(void) {
 const IfsSfsModuleV1 *api=infiltrator_filesystem_sfs_v1();
 CHECK(api && api->abi_version==IFS_SFS_MODULE_V1);
 CHECK(api->structure_bytes>=sizeof(*api));
 CHECK(api->format_id && strcmp(api->format_id,"sfs")==0);
 CHECK(api->capabilities==(IFS_SFS_CAP_RUN | IFS_SFS_CAP_FIXTURE) && api->run && api->fixture);
 CHECK(api->run(0,NULL,NULL)!=0);
 CHECK(api->fixture(0,NULL,NULL,NULL,0,NULL,NULL)!=0);
 return 0;
}
