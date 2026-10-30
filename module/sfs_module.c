/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "ifs_sfs_module_v1.h"
#include "ifs_sfs_fixture_host_v1.h"
#include <errno.h>
int ifs_sfs_worker_main(int, char **);
int ldtm_format_sfs_volume(const char *);
int ldtm_populate_sfs_volume(const char *,const LdtmFragmentProfile *);
int ldtm_verify_sfs_payload(const char *,const LdtmFragmentProfile *,char *,size_t);
int ldtm_verify_sfs_payload_after_defrag(const char *,const LdtmFragmentProfile *,char *,size_t);
int ifs_bind_services(const IfsDefragServicesV1 *);
void ifs_unbind_services(void);
int ifs_bind_fixture(const IfsSfsFixtureHostV1 *);
void ifs_unbind_fixture(void);
static int execute(int argc, char **argv, const IfsDefragServicesV1 *host) {
 if(ifs_bind_services(host)!=0) return 1;
 int result=ifs_sfs_worker_main(argc,argv);
 ifs_unbind_services();
 return result;
}
static int fixture(uint32_t op, const char *path, const void *profile,
 char *detail, size_t cap, const IfsDefragServicesV1 *services, const void *host) {
 if(!path || ifs_bind_services(services)!=0) return -1;
 if(ifs_bind_fixture((const IfsSfsFixtureHostV1 *)host)!=0) {
    ifs_unbind_services(); return -1;
 }
 const LdtmFragmentProfile *p=(const LdtmFragmentProfile *)profile;
 int rc=-1;
 switch(op) {
  case IFS_SFS_FIXTURE_FORMAT: rc=ldtm_format_sfs_volume(path); break;
  case IFS_SFS_FIXTURE_POPULATE: if(p) rc=ldtm_populate_sfs_volume(path,p); break;
  case IFS_SFS_FIXTURE_VERIFY: if(p) rc=ldtm_verify_sfs_payload(path,p,detail,cap); break;
  case IFS_SFS_FIXTURE_VERIFY_DEFRAG:
   if(p) rc=ldtm_verify_sfs_payload_after_defrag(path,p,detail,cap);
   break;
  default: errno=EINVAL; break;
 }
 ifs_unbind_fixture(); ifs_unbind_services();
 return rc;
}
static const IfsSfsModuleV1 sfs = {IFS_SFS_MODULE_V1,sizeof(IfsSfsModuleV1),"sfs",
 IFS_SFS_CAP_RUN | IFS_SFS_CAP_FIXTURE,execute,fixture};
const IfsSfsModuleV1 *infiltrator_filesystem_sfs_v1(void) { return &sfs; }
