/* SPDX-License-Identifier: GPL-3.0-or-later
 * Disposable SFS root/backup-media qualification through Common V2 only.
 */
#include <infiltratr/filesystem_module_v2.h>
#include "../native/filesystems/sfs/core/sfs_core.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const IfsFsModuleApiV2 *infiltrator_filesystem_api_v2(void);
#define CHECK(x) do { if(!(x)) {fprintf(stderr,"SFS V2 failure: %s\n",#x);return 1;} } while(0)
enum { SFS_BLOCK=512, SFS_TOTAL=16 };
static unsigned char media[SFS_BLOCK*SFS_TOTAL];
static void be16(unsigned char *p,uint16_t v) {
    p[0]=(unsigned char)(v>>8);p[1]=(unsigned char)v;
}
static void be32(unsigned char *p,uint32_t v) {
    p[0]=(unsigned char)(v>>24);p[1]=(unsigned char)(v>>16);
    p[2]=(unsigned char)(v>>8);p[3]=(unsigned char)v;
}
static void make_root(uint32_t block,uint16_t serial) {
    unsigned char *p=media+(size_t)block*SFS_BLOCK;
    memset(p,0,SFS_BLOCK);
    be32(p,IFS_SFS_ROOT_ID);
    be32(p+8u,block);
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
    memcpy(buf,media+(size_t)at,n);return 0;
}
static void *memory_alloc(void *ctx,size_t bytes) {(void)ctx;return malloc(bytes);}
static void memory_free(void *ctx,void *p) {(void)ctx;free(p);}

int main(void) {
    const IfsFsModuleApiV2 *api=infiltrator_filesystem_api_v2();
    CHECK(ifs_fs_api_v2_valid(api,"sfs"));
    CHECK(api->runtime_profiles==IFS_FS_PROFILE_HOSTED);
    CHECK(api->group_mask==IFS_FS_GROUP_BIT(IFS_FS_GROUP_CORE));
    const IfsFsCoreOpsV2 *core=
        (const IfsFsCoreOpsV2 *)ifs_fs_group_v2(api,IFS_FS_GROUP_CORE);
    CHECK(core && core->header.capabilities==IFS_FS_CORE_PROBE);
    CHECK(core->probe && !core->mount && !core->read && !core->write);
    CHECK(!ifs_fs_group_v2(api,IFS_FS_GROUP_VOLUME));
    IfsFsHostRuntimeV2 host={0};
    host.abi_version=IFS_FS_HOST_ABI_V2;
    host.structure_bytes=sizeof(host);
    host.capabilities=IFS_FS_HOST_READ_ONLY;
    host.volume_bytes=sizeof(media);
    host.logical_sector_bytes=SFS_BLOCK;
    host.physical_sector_bytes=SFS_BLOCK;
    host.maximum_transfer_bytes=SFS_BLOCK;
    host.read_at=memory_read;
    host.allocate=memory_alloc;
    host.release=memory_free;
    CHECK(ifs_fs_host_v2_valid(&host));
    uint32_t variant=0u;
    CHECK(core->probe(&host,&variant)==IFS_FS_NOT_FOUND);
    make_root(0u,2u);
    make_root(SFS_TOTAL-1u,3u);
    CHECK(core->probe(&host,&variant)==IFS_FS_OK &&
          variant==IFS_SFS_STRUCTURE_VERSION);
    media[4u]^=0x40u;
    CHECK(core->probe(&host,&variant)==IFS_FS_OK); /* valid backup */
    media[SFS_BLOCK*(SFS_TOTAL-1u)+4u]^=0x40u;
    CHECK(core->probe(&host,&variant)!=IFS_FS_OK); /* both corrupt */
    memset(media,0,sizeof(media));
    make_root(0u,1u);
    make_root(SFS_TOTAL-1u,2u);
    be32(media,0x53465302u); /* foreign SFS2 primary */
    CHECK(core->probe(&host,&variant)==IFS_FS_OK); /* SFS backup */
    memset(media,0,sizeof(media));
    CHECK(core->probe(&host,&variant)==IFS_FS_NOT_FOUND);
    host.logical_sector_bytes=1000u;
    CHECK(core->probe(&host,&variant)==IFS_FS_INVALID);
    puts("SFS-STRICT-COMMON-V2-PROBE-PASS");
    return 0;
}
