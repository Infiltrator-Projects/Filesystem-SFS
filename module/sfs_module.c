/* SPDX-License-Identifier: GPL-3.0-or-later
 * Independent SFS root/probe implementation, strict Common V2 export only.
 * The probe is backed by the same canonical on-media checksum/layout policy
 * used by the source-owned kernel driver. No fallback ABI is exported.
 */
#include <infiltratr/filesystem_module_v2.h>
#include <infiltratr/filesystem_ro_sessions_v2.h>
#include "sfs_reader.h"
/* Readable without executing untrusted ELF code; tied to the V2 export. */
__attribute__((used,retain,section(".ifsmeta")))
static const char v2_module_identity[]="IFS-MODULE:sfs:common2";

#include "../native/filesystems/sfs/core/sfs_core.h"
#include <stdint.h>
#include <stdatomic.h>
#include <limits.h>
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
                    uint32_t block_number, SfsRoot *out, int *valid) {
    unsigned char *block;
    int rc;
    *valid = 0;
    uint64_t position;
    if(!h || !h->allocate || !h->release || !h->read_at || !out ||
       block_size<512u || block_size>65536u ||
       (block_size&(block_size-1u)))return IFS_FS_INVALID;
    position=(uint64_t)block_number*block_size;
    if(position>h->volume_bytes ||
       block_size>h->volume_bytes-position)return IFS_FS_OK;
    block=h->allocate(h->context,block_size);
    if(!block)return IFS_FS_NO_MEMORY;
    /* A transport failure is not evidence of an absent root. Preserve
     * the Common V2 status separately from the on-media validity bit. */
    rc=h->read_at(h->context,position,block,block_size);
    if(rc==IFS_FS_OK &&
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
            *valid=1;
        }
    }
    h->release(h->context,block);
    return rc;
}
static int sfs_probe_native(const SfsCommonIo *h,uint32_t *variant,
                            SfsRoot *selected) {
    SfsRoot first={0}, last={0};
    int primary=0, backup=0, rc;
    if(!h||!h->read_at||!variant ||
       !h->allocate||!h->release||h->volume_bytes<3u*512u)
        return IFS_FS_INVALID;
    /* Prefer true root geometry, then try a verified last-block backup for
     * media with a damaged primary. Never reinterpret SFS2 as SFS0. */
    for(uint32_t bs=512u;bs<=65536u;bs<<=1u) {
        rc=sfs_root(h,bs,0u,&first,&primary);
        if(rc!=IFS_FS_OK)return rc;
        if(primary) {
            rc=sfs_root(h,bs,first.total_blocks-1u,&last,&backup);
            if(rc!=IFS_FS_OK)return rc;
            break;
        }
    }
    if(!primary) {
        for(uint32_t bs=512u;bs<=65536u;bs<<=1u) {
            uint64_t blocks=h->volume_bytes/bs;
            if(h->volume_bytes%bs || blocks<3u || blocks>UINT32_MAX)
                continue;
            int candidate_valid=0;
            rc=sfs_root(h,bs,(uint32_t)blocks-1u,&last,&candidate_valid);
            if(rc!=IFS_FS_OK)return rc;
            if(candidate_valid && last.total_blocks==blocks) {
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
    return ifs_fs_host_read_bytes_v2(host,at,buffer,bytes);
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

/* Read authoritative SFS allocation bitmap blocks with the same canonical
 * bitmap geometry and checksummed metadata header used by the independent
 * native SFS scanner. All reads remain partition-relative Common V2 IO. */
static int sfs_verified_free_blocks(const SfsCommonIo *io,
                                    const SfsRoot *root,uint64_t *free_count)
{
    uint32_t bits_per,bitmap_blocks;
    uint64_t total=0u;
    unsigned char *block;
    int rc=IFS_FS_OK;
    if(!io || !root || !free_count ||
       ifs_sfs_compute_bitmap_layout(root->block_size,root->total_blocks,
                                     &bits_per,&bitmap_blocks)!=0 ||
       (uint64_t)root->bitmap+bitmap_blocks>root->total_blocks)
        return IFS_FS_CORRUPT;
    block=io->allocate(io->context,root->block_size);
    if(!block)return IFS_FS_NO_MEMORY;
    for(uint32_t group=0u;group<bitmap_blocks;++group) {
        const uint32_t number=root->bitmap+group;
        uint64_t first=(uint64_t)group*bits_per;
        uint64_t covered=root->total_blocks-first;
        if(covered>bits_per)covered=bits_per;
        rc=io->read_at(io->context,(uint64_t)number*root->block_size,
                       block,root->block_size);
        if(rc!=IFS_FS_OK)break;
        if(!ifs_sfs_validate_block_header(block,root->block_size,number,
                                         UINT32_C(0x42544d50))) {
            rc=IFS_FS_CORRUPT;break;
        }
        /* A valid bitmap checksum cannot make in-use format metadata
         * safely reusable. Reject allocation maps freeing any root,
         * allocation bitmap, administration, object, transaction or
         * extent-tree block required by the mounted volume. */
        for(uint64_t bit=0u;bit<covered;++bit) {
            const uint64_t index=first+bit;
            const int free_bit=(block[IFS_SFS_BLOCK_HEADER_SIZE+(bit>>3u)] &
                                (unsigned char)(0x80u>>(bit&7u)))!=0u;
            if(free_bit &&
               (index==0u || index==root->total_blocks-1u ||
                (index>=root->bitmap &&
                 index<(uint64_t)root->bitmap+bitmap_blocks) ||
                index==root->admin || index==root->objects ||
                index==root->extents || index==root->nodes ||
                (root->objects<=root->total_blocks-3u &&
                 index==(uint64_t)root->objects+2u))) {
                rc=IFS_FS_CORRUPT;
                break;
            }
            total+=(uint64_t)free_bit;
        }
        if(rc!=IFS_FS_OK)break;
    }
    io->release(io->context,block);
    if(rc==IFS_FS_OK)*free_count=total;
    return rc;
}
/* The canonical Common V2 registry stores immutable validated metadata,
 * never a singleton. Its opaque generation handles cannot be reused, and
 * metadata queries after unmount are rejected without touching stale memory. */
static IfsFsReadOnlySessionsV2 sessions = IFS_FS_RO_SESSIONS_INIT_V2;
typedef struct {
    uintptr_t cookie;
    unsigned active;
    IfsSfsReader reader;
    uint32_t bitmap_base;
} SfsSession;
static SfsSession readers[IFS_FS_RO_MAX_SESSIONS_V2];

/* Pin the reader before dropping the registry lock. Holding the lock over
 * host I/O would serialize independent volumes and deadlock a reentrant
 * callback. The active count prevents teardown until I/O returns. */
static SfsSession *acquire_reader(void *handle)
{
    const uintptr_t token=(uintptr_t)handle;
    SfsSession *result=NULL;
    if (!token) return NULL;
    ifs_fs_ro_sessions_lock_v2(&sessions);
    for (uint32_t i=0;i<IFS_FS_RO_MAX_SESSIONS_V2;++i)
        if (readers[i].cookie==token &&
            sessions.slots[i].cookie==token &&
            readers[i].active!=UINT_MAX) {
            ++readers[i].active;
            result=&readers[i];
            break;
        }
    ifs_fs_ro_sessions_unlock_v2(&sessions);
    return result;
}
static int finish_reader(SfsSession *session,int rc)
{
    ifs_fs_ro_sessions_lock_v2(&sessions);
    --session->active;
    ifs_fs_ro_sessions_unlock_v2(&sessions);
    return rc;
}
static int v2_mount_impl(const IfsFsHostRuntimeV2 *host,void **volume,
                    IfsFsVolumeInfoV2 *info)
{
    SfsCommonIo io={0};
    SfsRoot root={0};
    IfsFsVolumeInfoV2 measured={0};
    uint64_t counted_free=0u;
    uint32_t variant=0u;
    int rc;
    if(!volume || !info || info->structure_bytes<sizeof(*info))
        return IFS_FS_INVALID;
    *volume=NULL;
    if(!ifs_fs_host_v2_valid(host))return IFS_FS_INVALID;
    if(!(host->capabilities & IFS_FS_HOST_READ_ONLY))return IFS_FS_ACCESS;
    rc=make_io(host,&io);
    if(rc!=IFS_FS_OK)return rc;
    rc=sfs_probe_native(&io,&variant,&root);
    if(rc!=IFS_FS_OK)return rc;
    rc=sfs_verified_free_blocks(&io,&root,&counted_free);
    if(rc!=IFS_FS_OK)return rc;
    IfsSfsReader verify={0};
    size_t n=host->structure_bytes<sizeof(verify.host)?
             host->structure_bytes:sizeof(verify.host);
    memcpy(&verify.host,host,n);
    verify.host.structure_bytes=(uint32_t)n;
    verify.block_bytes=root.block_size;
    verify.total_blocks=root.total_blocks;
    verify.object_root=root.objects;
    verify.extent_root=root.extents;
    rc=ifs_sfs_ro_validate(&verify);
    if(rc!=IFS_FS_OK)return rc;
    measured.structure_bytes=sizeof(measured);
    measured.variant=IFS_SFS_STRUCTURE_VERSION;
    measured.block_bytes=root.block_size;
    measured.max_name_bytes=IFS_SFS_MAX_FILENAME;
    measured.total_bytes=(uint64_t)root.total_blocks*root.block_size;
    measured.free_bytes=counted_free*root.block_size;
    void *token=NULL;
    rc=ifs_fs_ro_session_open_v2(&sessions,&measured,&token,info);
    if(rc!=IFS_FS_OK)return rc;
    ifs_fs_ro_sessions_lock_v2(&sessions);
    for(uint32_t i=0u;i<IFS_FS_RO_MAX_SESSIONS_V2;++i) {
        if(sessions.slots[i].cookie==(uintptr_t)token) {
            IfsSfsReader *reader=&readers[i].reader;
            size_t n=host->structure_bytes<sizeof(reader->host)?
                     host->structure_bytes:sizeof(reader->host);
            memset(reader,0,sizeof(*reader));
            memcpy(&reader->host,host,n);
            reader->host.structure_bytes=(uint32_t)n;
            reader->block_bytes=root.block_size;
            reader->total_blocks=root.total_blocks;
            reader->object_root=root.objects;
            reader->extent_root=root.extents;
            readers[i].bitmap_base=root.bitmap;
            readers[i].cookie=(uintptr_t)token;
            *volume=token;
            break;
        }
    }
    ifs_fs_ro_sessions_unlock_v2(&sessions);
    return *volume ? IFS_FS_OK : IFS_FS_INVALID;
}
static int v2_unmount(void *volume)
{
    if (!volume) return IFS_FS_INVALID;
    int rc=IFS_FS_INVALID;
    ifs_fs_ro_sessions_lock_v2(&sessions);
    for(uint32_t i=0u;i<IFS_FS_RO_MAX_SESSIONS_V2;++i)
        if(readers[i].cookie==(uintptr_t)volume &&
           sessions.slots[i].cookie==(uintptr_t)volume) {
            if (readers[i].active) rc=IFS_FS_BUSY;
            else {
                /* Retire both records atomically; slot reuse cannot race
                 * cleanup of a previous generation's reader. */
                sessions.slots[i]=(IfsFsReadOnlySessionSlotV2){0};
                readers[i]=(SfsSession){0};
                rc=IFS_FS_OK;
            }
            break;
        }
    ifs_fs_ro_sessions_unlock_v2(&sessions);
    return rc;
}
static int v2_volume_query(void *volume,IfsFsVolumeInfoV2 *info)
{
    return ifs_fs_ro_session_query_v2(&sessions,volume,info);
}
static int stat_path(void *volume,const char *path,IfsFsEntryV2 *entry)
{
    SfsSession *session=acquire_reader(volume);
    return session?finish_reader(session,ifs_sfs_ro_stat(&session->reader,path,entry)):IFS_FS_INVALID;
}
static int list_path(void *volume,const char *path,uint32_t nth,IfsFsEntryV2 *entry)
{
    SfsSession *session=acquire_reader(volume);
    return session?finish_reader(session,ifs_sfs_ro_list(&session->reader,path,nth,entry)):IFS_FS_INVALID;
}
static int read_file(void *volume,const char *path,uint64_t offset,
                     void *dest,uint32_t bytes)
{
    SfsSession *session=acquire_reader(volume);
    return session?finish_reader(session,ifs_sfs_ro_read(&session->reader,path,offset,dest,bytes)):IFS_FS_INVALID;
}
#if !defined(IFS_SFS_FIRMWARE_BUILD)
/* SmartFilesystem 1.279 empty-volume layout: two roots, a 32-block
 * administration region, root/recycled objects, hash table, clean
 * transaction marker, extent and object-node trees, and free bitmaps.
 * Layout reference: amipart nativefmt.c (retained in Filesystem-SFS2).
 * This implementation uses the provider's canonical checksum and
 * Common V2 byte transport; no pathname or external formatter.
 * The host supplies partition-relative geometry, not disk offsets. */
enum { FMT_ADMIN=1u, FMT_ROOT=2u, FMT_HASH=3u, FMT_TRANS=4u,
       FMT_EXTENT=5u, FMT_NODES=6u, FMT_RECYCLED=7u,
       FMT_ADMIN_COUNT=32u, FMT_BITMAP=33u, FMT_BLOCK=4096u };
static atomic_flag format_gate=ATOMIC_FLAG_INIT;
static void fmt16(uint8_t *p,uint32_t x)
{p[0]=(uint8_t)(x>>8);p[1]=(uint8_t)x;}
static void fmt32(uint8_t *p,uint32_t x)
{fmt16(p,x>>16);fmt16(p+2,x);}
static void fmt64(uint8_t *p,uint64_t x)
{fmt32(p,(uint32_t)(x>>32));fmt32(p+4,(uint32_t)x);}
static void fmt_header(uint8_t *p,uint32_t id,uint32_t own)
{
    memset(p,0,FMT_BLOCK);
    fmt32(p,id);fmt32(p+8,own);
}
static int fmt_write(const IfsFsHostRuntimeV2 *h,uint32_t own,uint8_t *p,int stamp)
{
    if(h->cancelled && h->cancelled(h->context))return IFS_FS_BUSY;
    if(stamp) {
        fmt32(p+8,own);
        fmt32(p+4,ifs_sfs_calculate_block_checksum(p,FMT_BLOCK));
    }
    return ifs_fs_host_write_bytes_v2(h,(uint64_t)own*FMT_BLOCK,p,FMT_BLOCK);
}
static uint16_t fmt_hash(const char *name)
{
    uint16_t hash=(uint16_t)strlen(name);
    for(;*name;++name) {
        unsigned char ch=(unsigned char)*name;
        if(ch>='a' && ch<='z')ch=(unsigned char)(ch-32);
        hash=(uint16_t)(hash*13u+ch);
    }
    return hash;
}
static void fmt_object(uint8_t *p,uint32_t node,uint32_t protection,
                       uint32_t hash,uint32_t children,uint32_t date,
                       uint8_t bits,const char *name)
{
    fmt32(p+4,node);fmt32(p+8,protection);
    fmt32(p+12,hash);fmt32(p+16,children);
    fmt32(p+20,date);p[24]=bits;
    memcpy(p+25,name,strlen(name)+1u);
}
static int format_impl(const IfsFsHostRuntimeV2 *h,const char *label)
{
    const uint32_t total=(uint32_t)(h->volume_bytes/FMT_BLOCK);
    uint32_t bits=0u,maps=0u;
    if(ifs_sfs_compute_bitmap_layout(FMT_BLOCK,total,&bits,&maps)!=0 ||
       !bits || !maps || (uint64_t)FMT_BITMAP+maps+17u>=total)
        return IFS_FS_UNSUPPORTED;
    const uint32_t first_free=FMT_BITMAP+maps;
    const uint32_t free_blocks=total-first_free-1u;
    uint32_t date=0u;
    if(h->realtime_ns) {
        uint64_t t=h->realtime_ns(h->context)/1000000000ull;
        if(t>=252460800ull && t-252460800ull<=UINT32_MAX)
            date=(uint32_t)(t-252460800ull);
    }
    uint8_t block[FMT_BLOCK]={0};
    int rc;
    /* Revoke both old root copies, then persist metadata before publishing
     * either new identity. Never expose a new root during partial format. */
    rc=fmt_write(h,0u,block,0);if(rc!=IFS_FS_OK)return rc;
    rc=fmt_write(h,total-1u,block,0);if(rc!=IFS_FS_OK)return rc;
    if(h->flush(h->context)!=0)return IFS_FS_IO_ERROR;
    for(uint32_t i=FMT_ADMIN;i<FMT_BITMAP;++i) {
        rc=fmt_write(h,i,block,0);if(rc!=IFS_FS_OK)return rc;
    }
    fmt_header(block,0x41444d43u,FMT_ADMIN);
    block[20]=FMT_ADMIN_COUNT;fmt32(block+24,FMT_ADMIN);
    /* The first seven admin slots are allocated; the remaining 25
     * belong to the admin allocator, not the general free-space map. */
    fmt32(block+28,0xfe000000u);
    rc=fmt_write(h,FMT_ADMIN,block,1);if(rc!=IFS_FS_OK)return rc;
    fmt_header(block,0x4f424a43u,FMT_ROOT);
    fmt_object(block+24,1u,15u,FMT_HASH,FMT_RECYCLED,date,128u,label);
    fmt32(block+FMT_BLOCK-28u,free_blocks);
    fmt32(block+FMT_BLOCK-24u,date);
    rc=fmt_write(h,FMT_ROOT,block,1);if(rc!=IFS_FS_OK)return rc;
    fmt_header(block,0x48544142u,FMT_HASH);
    fmt32(block+12,1u);
    const uint16_t hash=fmt_hash(".recycled");
    fmt32(block+16u+4u*(hash%((FMT_BLOCK-16u)/4u)),2u);
    rc=fmt_write(h,FMT_HASH,block,1);if(rc!=IFS_FS_OK)return rc;
    fmt_header(block,0x54524f4bu,FMT_TRANS);
    rc=fmt_write(h,FMT_TRANS,block,1);if(rc!=IFS_FS_OK)return rc;
    fmt_header(block,0x424e4443u,FMT_EXTENT);
    block[14]=1u;block[15]=14u;
    rc=fmt_write(h,FMT_EXTENT,block,1);if(rc!=IFS_FS_OK)return rc;
    fmt_header(block,0x4e444320u,FMT_NODES);
    fmt32(block+12,1u);fmt32(block+16,1u);
    fmt32(block+20,FMT_ROOT);fmt32(block+30,FMT_RECYCLED);
    fmt16(block+38,hash);
    for(uint32_t i=0;i<4u;++i)fmt32(block+40u+10u*i,UINT32_MAX);
    rc=fmt_write(h,FMT_NODES,block,1);if(rc!=IFS_FS_OK)return rc;
    fmt_header(block,0x4f424a43u,FMT_RECYCLED);
    fmt32(block+12,1u);
    fmt_object(block+24,2u,12u,0u,0u,date,135u,".recycled");
    rc=fmt_write(h,FMT_RECYCLED,block,1);if(rc!=IFS_FS_OK)return rc;
    for(uint32_t i=0;i<maps;++i) {
        fmt_header(block,0x42544d50u,FMT_BITMAP+i);
        for(uint32_t j=0;j<bits;++j) {
            uint64_t number=(uint64_t)i*bits+j;
            if(number>=first_free && number<total-1u)
                block[12u+j/8u]|=(uint8_t)(0x80u>>(j%8u));
        }
        rc=fmt_write(h,FMT_BITMAP+i,block,1);if(rc!=IFS_FS_OK)return rc;
    }
    if(h->flush(h->context)!=0)return IFS_FS_IO_ERROR;
    fmt_header(block,IFS_SFS_ROOT_ID,total-1u);
    fmt16(block+12,IFS_SFS_STRUCTURE_VERSION);
    fmt32(block+16,date);block[20]=64u;
    fmt64(block+40,h->volume_bytes);
    fmt32(block+48,total);fmt32(block+52,FMT_BLOCK);
    fmt32(block+96,FMT_BITMAP);fmt32(block+100,FMT_ADMIN);
    fmt32(block+104,FMT_ROOT);fmt32(block+108,FMT_EXTENT);
    fmt32(block+112,FMT_NODES);
    rc=fmt_write(h,total-1u,block,1);if(rc!=IFS_FS_OK)return rc;
    if(h->flush(h->context)!=0)return IFS_FS_IO_ERROR;
    rc=fmt_write(h,0u,block,1);if(rc!=IFS_FS_OK)return rc;
    if(h->flush(h->context)!=0) {
        memset(block,0,sizeof(block));
        (void)ifs_fs_host_write_bytes_v2(h,0u,block,sizeof(block));
        (void)ifs_fs_host_write_bytes_v2(h,h->volume_bytes-FMT_BLOCK,block,sizeof(block));
        (void)h->flush(h->context);return IFS_FS_IO_ERROR;
    }
    return IFS_FS_OK;
}
static int format_volume(const IfsFsHostRuntimeV2 *h,uint32_t variant,
                          const char *label,uint32_t flags)
{
    if(!ifs_fs_host_v2_valid(h))return IFS_FS_INVALID;
    if((h->capabilities&IFS_FS_HOST_READ_ONLY) ||
       !(h->capabilities&IFS_FS_HOST_EXCLUSIVE) ||
       !(h->capabilities&IFS_FS_HOST_DURABLE))return IFS_FS_ACCESS;
    if(variant!=IFS_SFS_STRUCTURE_VERSION || flags ||
       h->logical_sector_bytes>FMT_BLOCK ||
       h->volume_bytes%FMT_BLOCK || h->volume_bytes<16ull*1024u*1024u ||
       h->volume_bytes>16ull*1024u*1024u*1024u)return IFS_FS_UNSUPPORTED;
    if(!label || !*label)label="Untitled";
    const size_t n=strlen(label);
    if(n>30u)return IFS_FS_INVALID;
    for(size_t i=0;i<n;++i)
        if((unsigned char)label[i]<32u || (unsigned char)label[i]>126u ||
           label[i]==':' || label[i]=='/')return IFS_FS_INVALID;
    if(atomic_flag_test_and_set_explicit(&format_gate,memory_order_acquire))
        return IFS_FS_BUSY;
    int rc=IFS_FS_OK;
    ifs_fs_ro_sessions_lock_v2(&sessions);
    for(uint32_t i=0;i<IFS_FS_RO_MAX_SESSIONS_V2;++i)
        if(sessions.slots[i].cookie){rc=IFS_FS_BUSY;break;}
    ifs_fs_ro_sessions_unlock_v2(&sessions);
    if(rc==IFS_FS_OK)rc=format_impl(h,label);
    atomic_flag_clear_explicit(&format_gate,memory_order_release);
    return rc;
}
#endif

/* Serialize mount publication against the hosted exclusive formatter.
 * Do not hold a registry spinlock while invoking a caller's I/O. */
static int v2_mount(const IfsFsHostRuntimeV2 *h,void **volume,IfsFsVolumeInfoV2 *info)
{
#if !defined(IFS_SFS_FIRMWARE_BUILD)
    if(atomic_flag_test_and_set_explicit(&format_gate,memory_order_acquire)) {
        if(volume)*volume=NULL;
        return IFS_FS_BUSY;
    }
#endif
    int rc=v2_mount_impl(h,volume,info);
#if !defined(IFS_SFS_FIRMWARE_BUILD)
    atomic_flag_clear_explicit(&format_gate,memory_order_release);
#endif
    return rc;
}

#if !defined(IFS_SFS_FIRMWARE_BUILD)
static int map_path(void *volume,const char *path,uint64_t first,
                    IfsFsExtentV2 *out,uint32_t capacity,uint32_t *written)
{
    if(!written)return IFS_FS_INVALID;
    *written=0u;
    SfsSession *session=acquire_reader(volume);
    return session?finish_reader(session,ifs_sfs_ro_map(&session->reader,path,
                       first,out,capacity,written)):IFS_FS_INVALID;
}
/* The read-only mount has already verified every bitmap checksum and
 * required metadata ownership. Retain its bitmap geometry in the pinned
 * session. Read only the relevant bitmap blocks per page; the host's
 * immutable media lease prevents a concurrent allocation change. */
static int allocation_page(const SfsSession *session,uint64_t first,
                          IfsFsAllocationRangeV2 *out,uint32_t capacity,uint32_t *written)
{
    const IfsSfsReader *r=&session->reader;
    const IfsFsHostRuntimeV2 *h=&r->host;
    const uint64_t limit=h->volume_bytes;
    const uint64_t fs_bytes=(uint64_t)r->total_blocks*r->block_bytes;
    if(first>limit || fs_bytes>limit)return IFS_FS_INVALID;
    if(first==limit)return IFS_FS_END;
    uint32_t bits=0u,groups=0u;
    if(ifs_sfs_compute_bitmap_layout(r->block_bytes,r->total_blocks,&bits,&groups)!=0 ||
       !bits || !groups || (uint64_t)session->bitmap_base+groups>r->total_blocks)
        return IFS_FS_CORRUPT;
    uint8_t *raw=h->allocate(h->context,r->block_bytes);
    if(!raw)return IFS_FS_NO_MEMORY;
    IfsFsAllocationRangeV2 page[64]={{0}};
    uint32_t count=0u,loaded=UINT32_MAX,scanned=0u;
    uint64_t at=first;
    int rc=IFS_FS_OK;
    while(at<limit && scanned<65536u) {
        if(h->cancelled && h->cancelled(h->context)){rc=IFS_FS_BUSY;goto done;}
        uint32_t state;
        uint64_t end;
        if(at>=fs_bytes) {
            state=IFS_FS_SPACE_RESERVED;end=limit;
        } else {
            const uint64_t block=at/r->block_bytes;
            const uint32_t group=(uint32_t)(block/bits),bit=(uint32_t)(block%bits);
            if(group>=groups){rc=IFS_FS_CORRUPT;goto done;}
            if(group!=loaded) {
                const uint32_t number=session->bitmap_base+group;
                rc=ifs_fs_host_read_bytes_v2(h,(uint64_t)number*r->block_bytes,
                                             raw,r->block_bytes);
                if(rc!=IFS_FS_OK)goto done;
                if(!ifs_sfs_validate_block_header(raw,r->block_bytes,number,
                                                   UINT32_C(0x42544d50))) {
                    rc=IFS_FS_CORRUPT;goto done;
                }
                loaded=group;
            }
            state=(raw[IFS_SFS_BLOCK_HEADER_SIZE+(bit>>3u)]&
                   (uint8_t)(0x80u>>(bit&7u)))?IFS_FS_SPACE_FREE:IFS_FS_SPACE_ALLOCATED;
            end=(block+1u)*r->block_bytes;
        }
        if(count && page[count-1u].state==state)page[count-1u].length+=end-at;
        else {
            if(count==capacity)break;
            page[count++]=(IfsFsAllocationRangeV2){
                .physical_offset=at,.length=end-at,.state=state};
        }
        at=end;++scanned;
    }
    memcpy(out,page,count*sizeof(*page));*written=count;
done:
    h->release(h->context,raw);
    return rc;
}
static int allocation_map(void *volume,uint64_t first,IfsFsAllocationRangeV2 *out,
                          uint32_t capacity,uint32_t *written)
{
    if(!written)return IFS_FS_INVALID;
    *written=0u;
    if(!out||!capacity||capacity>64u)return IFS_FS_INVALID;
    SfsSession *session=acquire_reader(volume);
    return session?finish_reader(session,allocation_page(session,first,out,capacity,written)):
        IFS_FS_INVALID;
}
static const IfsFsAllocationOpsV2 allocation_ops={
    .header={IFS_FS_GROUP_ABI_V2,sizeof(allocation_ops),IFS_FS_GROUP_ALLOCATION,0u,
             IFS_FS_ALLOCATION_MAP},
    .map=allocation_map
};
static const IfsFsDefragOpsV2 defrag_ops={
    .header={IFS_FS_GROUP_ABI_V2,sizeof(defrag_ops),IFS_FS_GROUP_DEFRAG,0u,
             IFS_FS_DEFRAG_MAP},
    .map=map_path
};
#endif
static const IfsFsCoreOpsV2 core={
    .header={IFS_FS_GROUP_ABI_V2,sizeof(IfsFsCoreOpsV2),
             IFS_FS_GROUP_CORE,0u,IFS_FS_CORE_PROBE|IFS_FS_CORE_MOUNT|
             IFS_FS_CORE_STAT|IFS_FS_CORE_LIST|IFS_FS_CORE_READ},
    .probe=v2_probe,.mount=v2_mount,.unmount=v2_unmount,
    .stat=stat_path,.directory_entry=list_path,.read=read_file
};
/* The same lifetime pin as CORE protects the reader during host I/O. */
static int v2_label(void *volume,char *out,uint32_t capacity,uint32_t *bytes)
{
    if(!bytes)return IFS_FS_INVALID;
    *bytes=0u;
    SfsSession *session=acquire_reader(volume);
    return session?finish_reader(session,ifs_sfs_ro_label(&session->reader,out,capacity,bytes)):IFS_FS_INVALID;
}
static const IfsFsVolumeOpsV2 volume_ops={
    .header={IFS_FS_GROUP_ABI_V2,sizeof(IfsFsVolumeOpsV2),
             IFS_FS_GROUP_VOLUME,0u,IFS_FS_VOLUME_QUERY|IFS_FS_VOLUME_LABEL_GET
#if !defined(IFS_SFS_FIRMWARE_BUILD)
             |IFS_FS_VOLUME_FORMAT
#endif
    },
    .query=v2_volume_query,.label=v2_label
#if !defined(IFS_SFS_FIRMWARE_BUILD)
    ,.format=format_volume
#endif
};
static const IfsFsGroupHeaderV2 *query(
    uint32_t id,uint32_t minimum,uint32_t maximum)
{
    if(minimum>IFS_FS_GROUP_ABI_V2 || maximum<IFS_FS_GROUP_ABI_V2)
        return NULL;
    if(id==IFS_FS_GROUP_CORE)return &core.header;
    if(id==IFS_FS_GROUP_VOLUME)return &volume_ops.header;
#if !defined(IFS_SFS_FIRMWARE_BUILD)
    if(id==IFS_FS_GROUP_DEFRAG)return &defrag_ops.header;
    if(id==IFS_FS_GROUP_ALLOCATION)return &allocation_ops.header;
#endif
    return NULL;
}
static const IfsFsModuleApiV2 api={
    IFS_FS_COMMON_ABI_V2,sizeof(IfsFsModuleApiV2),"sfs",
#if defined(IFS_SFS_FIRMWARE_BUILD)
    IFS_FS_PROFILE_FREESTANDING|IFS_FS_PROFILE_FIRMWARE,
#else
    IFS_FS_PROFILE_HOSTED,
#endif
    IFS_FS_GROUP_BIT(IFS_FS_GROUP_CORE)|
        IFS_FS_GROUP_BIT(IFS_FS_GROUP_VOLUME)
#if !defined(IFS_SFS_FIRMWARE_BUILD)
        |IFS_FS_GROUP_BIT(IFS_FS_GROUP_DEFRAG)|IFS_FS_GROUP_BIT(IFS_FS_GROUP_ALLOCATION)
#endif
        ,query
};
__attribute__((visibility("default")))
const IfsFsModuleApiV2 *infiltrator_filesystem_api_v2(void)
{
    return &api;
}
