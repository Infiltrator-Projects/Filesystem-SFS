/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "ifs_sfs_module_v1.h"
#include <infiltratr/filesystem_module_v2.h>
#include "../native/filesystems/sfs/core/sfs_core.h"
#include <stdint.h>
#include <stdlib.h>
extern const IfsFsModuleApiV1 *infiltrator_filesystem_api_v1(void);
extern const IfsFsModuleApiV2 *infiltrator_filesystem_api_v2(void);
#include <string.h>
#include <stdio.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"failure: %s\n",#x);return 1; } } while(0)
enum { SFS_BLOCK = 512, SFS_TOTAL = 16 };
static unsigned char media[SFS_BLOCK*SFS_TOTAL];
static void be16(unsigned char *p,uint16_t v) {
 p[0]=(unsigned char)(v>>8);p[1]=(unsigned char)v;
}
static void be32(unsigned char *p,uint32_t v) {
 p[0]=(unsigned char)(v>>24);p[1]=(unsigned char)(v>>16);
 p[2]=(unsigned char)(v>>8);p[3]=(unsigned char)v;
}
static void make_root(uint32_t number,uint16_t serial) {
 unsigned char *p=media+(size_t)number*SFS_BLOCK;
 memset(p,0,SFS_BLOCK);
 be32(p,IFS_SFS_ROOT_ID);
 be32(p+8u,number);
 be16(p+12u,IFS_SFS_STRUCTURE_VERSION);
 be16(p+14u,serial);
 be32(p+48u,SFS_TOTAL);be32(p+52u,SFS_BLOCK);
 be32(p+96u,1u);be32(p+100u,2u);be32(p+104u,3u);
 be32(p+108u,4u);be32(p+112u,5u);
 be32(p+4u,ifs_sfs_calculate_block_checksum(p,SFS_BLOCK));
}
static int memory_read(void *ctx,uint64_t at,void *buf,uint32_t n) {
 (void)ctx;
 if(at>sizeof(media)||n>sizeof(media)-at)return -1;
 memcpy(buf,media+(size_t)at,n);
 return 0;
}
static void *memory_alloc(void *ctx,size_t bytes) {
 (void)ctx;return malloc(bytes);
}
static void memory_free(void *ctx,void *p) {
 (void)ctx;free(p);
}
int main(void) {
 const IfsSfsModuleV1 *api=infiltrator_filesystem_sfs_v1();
 CHECK(api && api->abi_version==IFS_SFS_MODULE_V1);
 CHECK(api->structure_bytes>=sizeof(*api));
 CHECK(api->format_id && strcmp(api->format_id,"sfs")==0);
 CHECK(api->capabilities==(IFS_SFS_CAP_RUN | IFS_SFS_CAP_FIXTURE) && api->run && api->fixture);
 CHECK(api->run(0,NULL,NULL)!=0);
 CHECK(api->fixture(0,NULL,NULL,NULL,0,NULL,NULL)!=0);

 {
    const IfsFsModuleApiV1 *common=infiltrator_filesystem_api_v1();
    const IfsFsModuleApiV2 *v2=infiltrator_filesystem_api_v2();
    IfsFsHostIoV1 host={0};
    uint32_t variant=0u;
    host.structure_bytes=sizeof(host);
    host.flags=IFS_FS_IO_READ_ONLY;
    host.volume_bytes=sizeof(media);
    host.read_at=memory_read;
    host.allocate=memory_alloc;
    host.release=memory_free;
    CHECK(ifs_fs_common_api_valid(common,"sfs"));
    CHECK(ifs_fs_api_v2_valid(v2,"sfs"));
    CHECK(ifs_fs_api_v2_native_valid(v2,"sfs"));
    CHECK(v2->base_v1==NULL &&
          v2->group_mask==IFS_FS_GROUP_BIT(IFS_FS_GROUP_CORE));
    CHECK(common->capabilities==IFS_FS_CAP_PROBE);
    CHECK(!common->mount && !common->format && !common->read);
    {
      const IfsFsCoreOpsV1 *core =
          (const IfsFsCoreOpsV1 *)ifs_fs_group_v2(v2,IFS_FS_GROUP_CORE);
      IfsFsHostRuntimeV2 h2={0};
      h2.abi_version=IFS_FS_HOST_ABI_V2;
      h2.structure_bytes=sizeof(h2);
      h2.capabilities=IFS_FS_HOST_READ_ONLY;
      h2.volume_bytes=sizeof(media);
      h2.logical_sector_bytes=SFS_BLOCK;
      h2.physical_sector_bytes=SFS_BLOCK;
      h2.maximum_transfer_bytes=SFS_BLOCK;
      h2.read_at=memory_read;
      h2.allocate=memory_alloc;
      h2.release=memory_free;
      CHECK(core && core->header.capabilities==IFS_FS_CORE_PROBE &&
            core->probe && !core->mount && !core->read);
      CHECK(ifs_fs_host_v2_valid(&h2));
      make_root(0u,2u);make_root(SFS_TOTAL-1u,3u);
      CHECK(core->probe(&h2,&variant)==IFS_FS_OK &&
            variant==IFS_SFS_STRUCTURE_VERSION);
      media[4u]^=0x40u;
      CHECK(core->probe(&h2,&variant)==IFS_FS_OK);
      media[SFS_BLOCK*(SFS_TOTAL-1u)+4u]^=0x40u;
      CHECK(core->probe(&h2,&variant)!=IFS_FS_OK);
      memset(media,0,sizeof(media));
    }
    make_root(0u,2u);make_root(SFS_TOTAL-1u,3u);
    CHECK(common->probe(&host,&variant)==IFS_FS_OK &&
          variant==IFS_SFS_STRUCTURE_VERSION);
    media[4u]^=0x40u; /* damaged primary, valid backup still accepted */
    CHECK(common->probe(&host,&variant)==IFS_FS_OK);
    media[SFS_BLOCK*(SFS_TOTAL-1u)+4u]^=0x40u;
    CHECK(common->probe(&host,&variant)!=IFS_FS_OK);
    memset(media,0,sizeof(media));
    make_root(0u,1u);make_root(SFS_TOTAL-1u,2u);
    be32(media,0x53465302u); /* foreign SFS2 root is never SFS0 */
    CHECK(common->probe(&host,&variant)==IFS_FS_OK); /* valid SFS backup */
    memset(media,0,sizeof(media));
    CHECK(common->probe(&host,&variant)!=IFS_FS_OK);
 }
 return 0;
}
