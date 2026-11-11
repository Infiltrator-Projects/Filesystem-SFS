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
static int sfs_probe_native(const SfsCommonIo *h,uint32_t *variant,
                            SfsRoot *selected) {
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
    if(selected) {
        const int newer=ifs_sfs_select_root_copy(
            primary,first.sequence,backup,last.sequence);
        if(newer<0)return IFS_FS_CORRUPT;
        *selected=newer ? last:first;
    }
    return IFS_FS_OK;
}
/* The host's read_at contract is sector-aligned, while SFS roots and
 * bitmap/object metadata are byte-addressed. Common V2 owns bounded,
 * aligned, host-buffered reads; no standalone V1 I/O service is used. */
static int v2_read(void *context,uint64_t at,void *buffer,uint32_t bytes)
{
    const IfsFsHostRuntimeV2 *host=context;
    return ifs_fs_host_read_bytes_v2(host,at,buffer,bytes)==IFS_FS_OK?0:-1;
}
static void *v2_allocate(void *context,size_t bytes)
{
    const IfsFsHostRuntimeV2 *host=context;
    return host->allocate(host->context,bytes);
}
static void v2_release(void *context,void *memory)
{
    const IfsFsHostRuntimeV2 *host=context;
    host->release(host->context,memory);
}
static int make_io(const IfsFsHostRuntimeV2 *host,SfsCommonIo *io)
{
    if(!host || !io || !ifs_fs_host_v2_valid(host))
        return IFS_FS_INVALID;
    io->context=(void *)host;
    io->volume_bytes=host->volume_bytes;
    io->read_at=v2_read;
    io->allocate=v2_allocate;
    io->release=v2_release;
    return IFS_FS_OK;
}
static int v2_probe(const IfsFsHostRuntimeV2 *host,uint32_t *variant)
{
    SfsCommonIo io={0};
    int rc=make_io(host,&io);
    if(rc!=IFS_FS_OK)return rc;
    return sfs_probe_native(&io,variant,NULL);
}

/* One module instance owns one immutable, validated root geometry snapshot.
 * Generation tokens prevent a stale handle from being reused after unmount. */
static struct {
    uintptr_t generation,active;
    SfsRoot root;
} mounted;
static int live(void *volume)
{
    return mounted.active!=0u && (uintptr_t)volume==mounted.active;
}
static void describe(IfsFsVolumeInfoV2 *info)
{
    info->structure_bytes=sizeof(*info);
    info->variant=IFS_SFS_STRUCTURE_VERSION;
    info->block_bytes=mounted.root.block_size;
    info->max_name_bytes=IFS_SFS_MAX_FILENAME;
    info->total_bytes=(uint64_t)mounted.root.total_blocks*
                      mounted.root.block_size;
    /* This root layout does not independently qualify a free-block count. */
    info->free_bytes=0u;
}
static int v2_mount(const IfsFsHostRuntimeV2 *host,void **volume,
                    IfsFsVolumeInfoV2 *info)
{
    SfsCommonIo io={0};
    SfsRoot root={0};
    uint32_t variant=0u;
    int rc;
    if(!volume || !info || info->structure_bytes<sizeof(*info))
        return IFS_FS_INVALID;
    *volume=NULL;
    if(mounted.active || mounted.generation==UINTPTR_MAX)
        return IFS_FS_BUSY;
    rc=make_io(host,&io);
    if(rc!=IFS_FS_OK)return rc;
    rc=sfs_probe_native(&io,&variant,&root);
    if(rc!=IFS_FS_OK)return rc;
    mounted.root=root;
    mounted.active=++mounted.generation;
    describe(info);
    *volume=(void *)mounted.active;
    return IFS_FS_OK;
}
static int v2_unmount(void *volume)
{
    if(!live(volume))return IFS_FS_INVALID;
    mounted.active=0u;
    memset(&mounted.root,0,sizeof(mounted.root));
    return IFS_FS_OK;
}
static int v2_volume_query(void *volume,IfsFsVolumeInfoV2 *info)
{
    if(!live(volume)||!info||info->structure_bytes<sizeof(*info))
        return IFS_FS_INVALID;
    describe(info);
    return IFS_FS_OK;
}
static const IfsFsCoreOpsV2 core={
    .header={IFS_FS_GROUP_ABI_V2,sizeof(IfsFsCoreOpsV2),
             IFS_FS_GROUP_CORE,0u,IFS_FS_CORE_PROBE|IFS_FS_CORE_MOUNT},
    .probe=v2_probe,.mount=v2_mount,.unmount=v2_unmount
};
static const IfsFsVolumeOpsV2 volume_ops={
    .header={IFS_FS_GROUP_ABI_V2,sizeof(IfsFsVolumeOpsV2),
             IFS_FS_GROUP_VOLUME,0u,IFS_FS_VOLUME_QUERY},
    .query=v2_volume_query
};
static const IfsFsGroupHeaderV2 *query(
    uint32_t id,uint32_t minimum,uint32_t maximum)
{
    if(minimum>IFS_FS_GROUP_ABI_V2 || maximum<IFS_FS_GROUP_ABI_V2)
        return NULL;
    if(id==IFS_FS_GROUP_CORE)return &core.header;
    if(id==IFS_FS_GROUP_VOLUME)return &volume_ops.header;
    return NULL;
}
static const IfsFsModuleApiV2 api={
    IFS_FS_COMMON_ABI_V2,sizeof(IfsFsModuleApiV2),"sfs",
    IFS_FS_PROFILE_HOSTED,
    IFS_FS_GROUP_BIT(IFS_FS_GROUP_CORE)|
        IFS_FS_GROUP_BIT(IFS_FS_GROUP_VOLUME),query
};
__attribute__((visibility("default")))
const IfsFsModuleApiV2 *infiltrator_filesystem_api_v2(void)
{
    return &api;
}
