/* SPDX-License-Identifier: GPL-3.0-or-later
 * Independent SFS root/probe implementation, strict Common V2 export only.
 * The probe is backed by the same canonical on-media checksum/layout policy
 * used by the source-owned kernel driver. No fallback ABI is exported.
 */
#include <infiltratr/filesystem_module_v2.h>
/* Readable without executing untrusted ELF code; tied to the V2 export. */
__attribute__((used,section(".ifsmeta")))
static const char v2_module_identity[]="IFS-MODULE:sfs:common2";

#include "../native/filesystems/sfs/core/sfs_core.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>

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
/* Only qualified read-only CORE probing is advertised. The existing
 * native format and relocation engines remain local to this provider,
 * but have not been adapted to the V2 host callbacks yet. */
static const IfsFsCoreOpsV2 v2_core={
    .header={IFS_FS_GROUP_ABI_V2,sizeof(IfsFsCoreOpsV2),
             IFS_FS_GROUP_CORE,0u,IFS_FS_CORE_PROBE},
    .probe=v2_probe
};
static const IfsFsGroupHeaderV2 *query_core(
    uint32_t id,uint32_t minimum,uint32_t maximum) {
    return id==IFS_FS_GROUP_CORE && minimum<=IFS_FS_GROUP_ABI_V2 &&
        maximum>=IFS_FS_GROUP_ABI_V2 ? &v2_core.header : NULL;
}
static const IfsFsModuleApiV2 common_v2={
    .abi_version=IFS_FS_COMMON_ABI_V2,
    .structure_bytes=sizeof(IfsFsModuleApiV2),
    .format_id="sfs",
    .runtime_profiles=IFS_FS_PROFILE_HOSTED,
    .group_mask=IFS_FS_GROUP_BIT(IFS_FS_GROUP_CORE),
    .query_group=query_core
};
__attribute__((visibility("default")))
const IfsFsModuleApiV2 *infiltrator_filesystem_api_v2(void) {
    return &common_v2;
}
