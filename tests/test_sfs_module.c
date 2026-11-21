/* SPDX-License-Identifier: GPL-3.0-or-later
 * Disposable SFS root/backup-media qualification through Common V2 only.
 */
#include <infiltratr/filesystem_module_v2.h>
#include "../native/filesystems/sfs/core/sfs_core.h"
#include <stdint.h>
#include <dlfcn.h>
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
static void make_bitmap(void) {
    unsigned char *p=media+SFS_BLOCK; /* root bitmap_base = 1 */
    memset(p,0,SFS_BLOCK);
    be32(p,UINT32_C(0x42544d50)); /* BTMP */
    be32(p+8u,1u);
    for(unsigned bit=0u;bit<SFS_TOTAL;++bit)
        p[IFS_SFS_BLOCK_HEADER_SIZE+(bit>>3u)] |=
            (unsigned char)(0x80u>>(bit&7u));
    const unsigned used[]={0u,1u,2u,3u,4u,5u,15u};
    for(unsigned i=0u;i<sizeof(used)/sizeof(used[0]);++i) {
        const unsigned bit=used[i];
        p[IFS_SFS_BLOCK_HEADER_SIZE+(bit>>3u)] &=
            (unsigned char)~(0x80u>>(bit&7u));
    }
    be32(p+4u,ifs_sfs_calculate_block_checksum(p,SFS_BLOCK));
}
/* Native SFS\0 test media with actual objects, directory chains and
 * extent B-tree leaves, not just a valid root signature. */
static void metadata(uint32_t block,uint32_t id)
{
    unsigned char *p=media+block*SFS_BLOCK;
    memset(p,0,SFS_BLOCK);
    be32(p,id);be32(p+8u,block);
    be32(p+4u,ifs_sfs_calculate_block_checksum(p,SFS_BLOCK));
}
static uint32_t add_object(unsigned char *p,uint32_t at,uint32_t node,
                           uint32_t data,uint32_t auxiliary,uint8_t bits,
                           const char *name)
{
    size_t n=strlen(name);
    be32(p+at+4u,node);be32(p+at+12u,data);
    be32(p+at+16u,auxiliary);
    p[at+24u]=bits;
    memcpy(p+at+25u,name,n+1u);
    p[at+25u+n+1u]=0; /* empty comment */
    return (uint32_t)((at+25u+n+2u+1u)&~1u);
}
static void make_namespace(void)
{
    const unsigned extra[]={6u,7u,8u,9u,11u,12u};
    memset(media,0,sizeof(media));
    make_root(0u,3u);make_root(SFS_TOTAL-1u,4u);
    for(unsigned i=0u;i<2u;++i) {
        unsigned char *p=media+(i?SFS_TOTAL-1u:0u)*SFS_BLOCK;
        be32(p+108u,6u);be32(p+112u,7u);
        be32(p+4u,ifs_sfs_calculate_block_checksum(p,SFS_BLOCK));
    }
    make_bitmap();
    unsigned char *p=media+SFS_BLOCK;
    for(unsigned i=0u;i<sizeof(extra)/sizeof(extra[0]);++i) {
        unsigned bit=extra[i];
        p[12u+(bit>>3u)]&=(unsigned char)~(0x80u>>(bit&7u));
    }
    be32(p+4u,ifs_sfs_calculate_block_checksum(p,SFS_BLOCK));
    metadata(2u,UINT32_C(0x41444d43)); /* admin-space */
    metadata(3u,UINT32_C(0x4f424a43)); /* root container */
    p=media+3u*SFS_BLOCK;
    add_object(p,24u,1u,4u,8u,128u,"VOLUME");
    be32(p+4u,ifs_sfs_calculate_block_checksum(p,SFS_BLOCK));
    metadata(4u,UINT32_C(0x48544142)); /* HTAB */
    metadata(5u,UINT32_C(0x54524f4b)); /* clean transaction */
    metadata(6u,UINT32_C(0x424e4443)); /* extent leaf */
    p=media+6u*SFS_BLOCK;
    be16(p+12u,2u);p[14u]=1u;p[15u]=14u;
    be32(p+16u,9u);be16(p+28u,1u);
    be32(p+30u,12u);be16(p+42u,1u);
    be32(p+4u,ifs_sfs_calculate_block_checksum(p,SFS_BLOCK));
    metadata(7u,UINT32_C(0x4e444320));
    p=media+7u*SFS_BLOCK;
    be32(p+12u,1u);be32(p+16u,1u);
    be32(p+4u,ifs_sfs_calculate_block_checksum(p,SFS_BLOCK));
    metadata(8u,UINT32_C(0x4f424a43));
    p=media+8u*SFS_BLOCK;be32(p+12u,1u);
    uint32_t at=add_object(p,24u,3u,9u,SFS_BLOCK,0u,"KERNEL.BIN");
    add_object(p,at,4u,0u,11u,128u,"BOOT");
    be32(p+4u,ifs_sfs_calculate_block_checksum(p,SFS_BLOCK));
    metadata(11u,UINT32_C(0x4f424a43));
    p=media+11u*SFS_BLOCK;be32(p+12u,4u);
    add_object(p,24u,5u,12u,256u,0u,"INITRD.IMG");
    be32(p+4u,ifs_sfs_calculate_block_checksum(p,SFS_BLOCK));
    for(unsigned i=0u;i<SFS_BLOCK;++i)
        media[9u*SFS_BLOCK+i]=(unsigned char)(i*31u+7u);
    for(unsigned i=0u;i<256u;++i)
        media[12u*SFS_BLOCK+i]=(unsigned char)(i^0xa5u);
}
static uint32_t logical_sector=512u;
static int memory_read(void *ctx,uint64_t at,void *buf,uint32_t n) {
    (void)ctx;
    if(at%logical_sector || n%logical_sector || n>logical_sector ||
       at>sizeof(media)||n>sizeof(media)-at)return -1;
    memcpy(buf,media+(size_t)at,n);return 0;
}
static void *memory_alloc(void *ctx,size_t bytes) {(void)ctx;return malloc(bytes);}
static void memory_free(void *ctx,void *p) {(void)ctx;free(p);}

static int forbidden_write(void *ctx,uint64_t at,const void *src,uint32_t n)
{ (void)ctx;(void)at;(void)src;(void)n;return -1; }
static int forbidden_flush(void *ctx)
{ (void)ctx;return -1; }
int main(int argc,char **argv) {
    void *firmware=NULL;
    const IfsFsModuleApiV2 *api=NULL;
    if(argc==2) {
        firmware=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);
        CHECK(firmware!=NULL);
        void *symbol=dlsym(firmware,IFS_FS_COMMON_EXPORT_V2);
        IfsFsModuleApiEntryV2 resolve=NULL;
        CHECK(symbol!=NULL && sizeof(resolve)==sizeof(symbol));
        memcpy(&resolve,&symbol,sizeof(resolve));
        api=resolve?resolve():NULL;
    } else {
        CHECK(argc==1);
        api=infiltrator_filesystem_api_v2();
    }
    CHECK(ifs_fs_api_v2_valid(api,"sfs"));
    CHECK(api->runtime_profiles==(firmware?
          (IFS_FS_PROFILE_FREESTANDING|IFS_FS_PROFILE_FIRMWARE):
          IFS_FS_PROFILE_HOSTED));
    CHECK(api->group_mask==(IFS_FS_GROUP_BIT(IFS_FS_GROUP_CORE)|
                            IFS_FS_GROUP_BIT(IFS_FS_GROUP_VOLUME)));
    const IfsFsCoreOpsV2 *core=
        (const IfsFsCoreOpsV2 *)ifs_fs_group_v2(api,IFS_FS_GROUP_CORE);
    const IfsFsVolumeOpsV2 *ops=
        (const IfsFsVolumeOpsV2 *)ifs_fs_group_v2(api,IFS_FS_GROUP_VOLUME);
    CHECK(core && core->header.capabilities==
          (IFS_FS_CORE_PROBE|IFS_FS_CORE_MOUNT|IFS_FS_CORE_STAT|
           IFS_FS_CORE_LIST|IFS_FS_CORE_READ));
    CHECK(core->probe && core->mount && core->unmount &&
          core->stat && core->directory_entry && core->read &&
          !core->write);
    CHECK(ops && ops->header.capabilities==IFS_FS_VOLUME_QUERY &&
          ops->query && !ops->format && !ops->repair);
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
    make_bitmap();
    CHECK(core->probe(&host,&variant)==IFS_FS_OK &&
          variant==IFS_SFS_STRUCTURE_VERSION);
    IfsFsVolumeInfoV2 info={0},snapshot={0};
    void *volume=NULL,*stale=NULL;
    info.structure_bytes=sizeof(info);
    host.capabilities=IFS_FS_HOST_EXCLUSIVE|IFS_FS_HOST_DURABLE;
    host.write_at=forbidden_write;host.flush=forbidden_flush;
    CHECK(ifs_fs_host_v2_valid(&host));
    CHECK(core->probe(&host,&variant)==IFS_FS_OK);
    CHECK(core->mount(&host,&volume,&info)==IFS_FS_ACCESS && !volume);
    host.capabilities=IFS_FS_HOST_READ_ONLY;
    host.write_at=NULL;host.flush=NULL;
    CHECK(ifs_fs_host_v2_valid(&host));
    CHECK(core->mount(&host,&volume,&info)==IFS_FS_OK && volume);
    stale=volume;
    CHECK(info.variant==IFS_SFS_STRUCTURE_VERSION &&
          info.block_bytes==SFS_BLOCK &&
          info.total_bytes==sizeof(media) &&
          info.free_bytes==9u*SFS_BLOCK &&
          info.max_name_bytes==IFS_SFS_MAX_FILENAME);
    snapshot.structure_bytes=sizeof(snapshot);
    CHECK(ops->query(volume,&snapshot)==IFS_FS_OK &&
          snapshot.total_bytes==info.total_bytes);
    {
        void *other=NULL;
        IfsFsVolumeInfoV2 measured={.structure_bytes=sizeof(measured)};
        CHECK(core->mount(&host,&other,&measured)==IFS_FS_OK &&
              other && other!=volume);
        CHECK(measured.total_bytes==info.total_bytes &&
              measured.free_bytes==info.free_bytes);
        measured.structure_bytes=sizeof(measured);
        CHECK(ops->query(other,&measured)==IFS_FS_OK);
        CHECK(core->unmount(other)==IFS_FS_OK);
        measured.structure_bytes=sizeof(measured);
        CHECK(ops->query(other,&measured)==IFS_FS_INVALID);
        CHECK(core->unmount(other)==IFS_FS_INVALID);
    }
    CHECK(core->unmount(volume)==IFS_FS_OK);
    /* Corrupted allocation header cannot masquerade as zero free space. */
    media[SFS_BLOCK+4u]^=1u;
    info.structure_bytes=sizeof(info);
    CHECK(core->mount(&host,&volume,&info)==IFS_FS_CORRUPT);
    CHECK(!volume);
    media[SFS_BLOCK+4u]^=1u;
    CHECK(core->unmount(stale)==IFS_FS_INVALID);
    CHECK(ops->query(stale,&snapshot)==IFS_FS_INVALID);
    /* Both roots still validate with 4Kn I/O even though the on-disk
     * SFS block size is only 512 bytes. No sub-sector host reads. */
    host.logical_sector_bytes=4096u;
    host.physical_sector_bytes=4096u;
    host.maximum_transfer_bytes=4096u;
    logical_sector=4096u;
    CHECK(ifs_fs_host_v2_valid(&host));
    CHECK(core->probe(&host,&variant)==IFS_FS_OK);
    info.structure_bytes=sizeof(info);
    CHECK(core->mount(&host,&volume,&info)==IFS_FS_OK &&
          volume!=stale && info.block_bytes==SFS_BLOCK);
    CHECK(core->unmount(volume)==IFS_FS_OK);
    host.logical_sector_bytes=SFS_BLOCK;
    host.physical_sector_bytes=SFS_BLOCK;
    host.maximum_transfer_bytes=SFS_BLOCK;
    logical_sector=SFS_BLOCK;
    {
        unsigned char readback[SFS_BLOCK]={0},snapshot_disk[sizeof(media)];
        char name[256]={0};
        IfsFsEntryV2 entry={.structure_bytes=sizeof(entry),
                            .name=name,.name_capacity=sizeof(name)};
        make_namespace();
        memcpy(snapshot_disk,media,sizeof(media));
        info.structure_bytes=sizeof(info);
        CHECK(core->mount(&host,&volume,&info)==IFS_FS_OK && volume);
        CHECK(core->stat(volume,"/",&entry)==IFS_FS_OK &&
              entry.type==IFS_FS_ENTRY_DIRECTORY);
        CHECK(core->stat(volume,"/KERNEL.BIN",&entry)==IFS_FS_OK &&
              entry.size_bytes==SFS_BLOCK);
        CHECK(core->stat(volume,"/BOOT/INITRD.IMG",&entry)==IFS_FS_OK &&
              entry.size_bytes==256u);
        CHECK(core->stat(volume,"/MISSING",&entry)==IFS_FS_NOT_FOUND);
        CHECK(core->directory_entry(volume,"/",0u,&entry)==IFS_FS_OK &&
              strcmp(entry.name,"KERNEL.BIN")==0);
        CHECK(core->directory_entry(volume,"/",1u,&entry)==IFS_FS_OK &&
              strcmp(entry.name,"BOOT")==0);
        CHECK(core->directory_entry(volume,"/",2u,&entry)==IFS_FS_END);
        CHECK(core->directory_entry(volume,"/BOOT",0u,&entry)==IFS_FS_OK &&
              strcmp(entry.name,"INITRD.IMG")==0);
        CHECK(core->read(volume,"/KERNEL.BIN",0u,readback,SFS_BLOCK)==IFS_FS_OK);
        for(unsigned i=0u;i<SFS_BLOCK;++i)
            CHECK(readback[i]==(unsigned char)(i*31u+7u));
        CHECK(core->read(volume,"/BOOT/INITRD.IMG",0u,readback,256u)==IFS_FS_OK);
        for(unsigned i=0u;i<256u;++i)
            CHECK(readback[i]==(unsigned char)(i^0xa5u));
        CHECK(core->read(volume,"/MISSING",0u,readback,1u)==IFS_FS_NOT_FOUND);
        CHECK(core->read(volume,"/KERNEL.BIN",511u,readback,2u)==IFS_FS_INVALID);
        CHECK(memcmp(snapshot_disk,media,sizeof(media))==0);
        media[6u*SFS_BLOCK+4u]^=1u; /* extent metadata corruption */
        CHECK(core->read(volume,"/KERNEL.BIN",0u,readback,1u)==IFS_FS_CORRUPT);
        media[6u*SFS_BLOCK+4u]^=1u;
        be32(media+5u*SFS_BLOCK,UINT32_C(0x54524641)); /* pending tx */
        be32(media+5u*SFS_BLOCK+4u,
             ifs_sfs_calculate_block_checksum(media+5u*SFS_BLOCK,SFS_BLOCK));
        CHECK(core->stat(volume,"/KERNEL.BIN",&entry)==IFS_FS_UNSUPPORTED);
        CHECK(core->unmount(volume)==IFS_FS_OK);
        volume=NULL;
        memset(media,0,sizeof(media));
        make_root(0u,2u);make_root(SFS_TOTAL-1u,3u);make_bitmap();
    }
    media[4u]^=0x40u;
    CHECK(core->probe(&host,&variant)==IFS_FS_OK); /* valid backup */
    media[SFS_BLOCK*(SFS_TOTAL-1u)+4u]^=0x40u;
    CHECK(core->probe(&host,&variant)!=IFS_FS_OK); /* both corrupt */
    memset(media,0,sizeof(media));
    make_root(0u,1u);
    make_root(SFS_TOTAL-1u,2u);
    make_bitmap();
    be32(media,0x53465302u); /* foreign SFS2 primary */
    CHECK(core->probe(&host,&variant)==IFS_FS_OK); /* SFS backup */
    memset(media,0,sizeof(media));
    CHECK(core->probe(&host,&variant)==IFS_FS_NOT_FOUND);
    host.logical_sector_bytes=1000u;
    CHECK(core->probe(&host,&variant)==IFS_FS_INVALID);
    puts("SFS-STRICT-COMMON-V2-NAMESPACE-READ-PASS");
    if(firmware)CHECK(dlclose(firmware)==0);
    return 0;
}
