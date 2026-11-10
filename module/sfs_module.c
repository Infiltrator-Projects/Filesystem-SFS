/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "ifs_sfs_module_v1.h"
#include "ifs_sfs_fixture_host_v1.h"
#include <infiltratr/filesystem_module_v2.h>
#include "../native/filesystems/sfs/core/sfs_core.h"
#include <stdint.h>
#include <string.h>
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

/* Format probing is backed by the exact portable root validation and
 * checksum engine already linked into SFS.so. The kernel and this provider
 * share that canonical code; this callback knows only block offsets. */
static uint16_t sfs_be16(const unsigned char *p) {
    return (uint16_t)(((uint16_t)p[0]<<8)|(uint16_t)p[1]);
}
static uint32_t sfs_be32(const unsigned char *p) {
    return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|
           ((uint32_t)p[2]<<8)|(uint32_t)p[3];
}
typedef struct {
    uint32_t block_size, total_blocks, bitmap, admin, objects, extents, nodes;
    uint16_t sequence;
} SfsRoot;
typedef struct {
    void *context;
    uint64_t volume_bytes;
    int (*read_at)(void *,uint64_t,void *,uint32_t);
    void *(*allocate)(void *,size_t);
    void (*release)(void *,void *);
} SfsCommonIo;
static int sfs_root(const SfsCommonIo *h, uint32_t block_size,
                    uint32_t block_number, SfsRoot *out) {
    unsigned char *block;
    int valid = 0;
    uint64_t position;
    if(!h || !h->allocate || !h->release || !h->read_at || !out ||
       block_size<512u || block_size>65536u ||
       (block_size&(block_size-1u)))return 0;
    position=(uint64_t)block_number*block_size;
    if(position>h->volume_bytes ||
       block_size>h->volume_bytes-position)return 0;
    block=h->allocate(h->context,block_size);
    if(!block)return 0;
    if(h->read_at(h->context,position,block,block_size)==0 &&
       ifs_sfs_validate_block_header(block,block_size,block_number,
                                    IFS_SFS_ROOT_ID)) {
        SfsRoot root={sfs_be32(block+52u),sfs_be32(block+48u),
             sfs_be32(block+96u),sfs_be32(block+100u),
             sfs_be32(block+104u),sfs_be32(block+108u),
             sfs_be32(block+112u),sfs_be16(block+14u)};
        if(ifs_sfs_validate_root_layout(IFS_SFS_ROOT_ID,
                sfs_be16(block+12u),root.block_size,root.total_blocks,
                root.bitmap,root.admin,root.objects,root.extents,
                root.nodes)==IFS_SFS_ROOT_OK &&
           root.block_size==block_size &&
           root.total_blocks>=3u &&
           (uint64_t)root.total_blocks*block_size<=h->volume_bytes &&
           (block_number==0u || block_number==root.total_blocks-1u)) {
            *out=root;
            valid=1;
        }
    }
    h->release(h->context,block);
    return valid;
}
static int sfs_probe_native(const SfsCommonIo *h,uint32_t *variant) {
    SfsRoot first={0}, last={0};
    int primary=0, backup=0;
    if(!h||!h->read_at||!variant ||
       !h->allocate||!h->release||h->volume_bytes<3u*512u)
        return IFS_FS_INVALID;
    /* Prefer true root geometry, then try a verified last-block backup for
     * media with a damaged primary. Never reinterpret SFS2 as SFS0. */
    for(uint32_t bs=512u;bs<=65536u;bs<<=1u) {
        if(sfs_root(h,bs,0u,&first)) {
            primary=1;
            backup=sfs_root(h,bs,first.total_blocks-1u,&last);
            break;
        }
    }
    if(!primary) {
        for(uint32_t bs=512u;bs<=65536u;bs<<=1u) {
            uint64_t blocks=h->volume_bytes/bs;
            if(h->volume_bytes%bs || blocks<3u || blocks>UINT32_MAX)
                continue;
            if(sfs_root(h,bs,(uint32_t)blocks-1u,&last) &&
               last.total_blocks==blocks) {
                backup=1;
                break;
            }
        }
    }
    if(!primary&&!backup)return IFS_FS_NOT_FOUND;
    if(primary&&backup&&
       (first.block_size!=last.block_size ||
        first.total_blocks!=last.total_blocks ||
        first.bitmap!=last.bitmap || first.admin!=last.admin ||
        first.objects!=last.objects || first.extents!=last.extents ||
        first.nodes!=last.nodes))return IFS_FS_CORRUPT;
    *variant=IFS_SFS_STRUCTURE_VERSION;
    return IFS_FS_OK;
}
/* Host-independent parser is shared by both entry points.
 * No V1 ABI call is made when a V2 consumer probes the filesystem. */
static int common_probe(const IfsFsHostIoV1 *h,uint32_t *variant) {
    SfsCommonIo io;
    if(!h || h->structure_bytes<sizeof(*h))return IFS_FS_INVALID;
    io.context=h->context;
    io.volume_bytes=h->volume_bytes;
    io.read_at=h->read_at;
    io.allocate=h->allocate;
    io.release=h->release;
    return sfs_probe_native(&io,variant);
}
static int v2_probe(const IfsFsHostRuntimeV2 *h,uint32_t *variant) {
    SfsCommonIo io;
    if(!ifs_fs_host_v2_valid(h))return IFS_FS_INVALID;
    io.context=h->context;
    io.volume_bytes=h->volume_bytes;
    io.read_at=h->read_at;
    io.allocate=h->allocate;
    io.release=h->release;
    return sfs_probe_native(&io,variant);
}
/* This native SFS.so already contains a worker and fixture format engine,
 * but has no qualified host-callback file API. Advertise probe only. */
static const IfsFsModuleApiV1 common_v1={
    .abi_version=IFS_FS_COMMON_ABI_V1,
    .structure_bytes=sizeof(IfsFsModuleApiV1),
    .format_id="sfs",
    .capabilities=IFS_FS_CAP_PROBE,
    .probe=common_probe
};
__attribute__((visibility("default")))
const IfsFsModuleApiV1 *infiltrator_filesystem_api_v1(void) {
    return &common_v1;
}
static const IfsFsCoreOpsV1 v2_core={
    .header={IFS_FS_GROUP_ABI_V1,sizeof(IfsFsCoreOpsV1),
             IFS_FS_GROUP_CORE,0u,IFS_FS_CORE_PROBE},
    .probe=v2_probe
};
static const IfsFsGroupHeaderV2 *query_core(
    uint32_t id,uint32_t minimum,uint32_t maximum) {
    return id==IFS_FS_GROUP_CORE && minimum<=IFS_FS_GROUP_ABI_V1 &&
        maximum>=IFS_FS_GROUP_ABI_V1 ? &v2_core.header : NULL;
}
static const IfsFsModuleApiV2 common_v2={
    IFS_FS_COMMON_ABI_V2,sizeof(IfsFsModuleApiV2),"sfs",
    IFS_FS_PROFILE_HOSTED,IFS_FS_GROUP_BIT(IFS_FS_GROUP_CORE),NULL,query_core
};
__attribute__((visibility("default")))
const IfsFsModuleApiV2 *infiltrator_filesystem_api_v2(void) {
    return &common_v2;
}
