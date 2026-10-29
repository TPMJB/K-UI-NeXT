/* SPDX-License-Identifier: GPL-3.0-only */
/* Exercises the catalog through real FatFs file allocation and power-cut
 * boundaries. Native card/journal/identity files are compared byte-for-byte. */
#include "kui/vmu_library.h"
#include "kui/vmu_collection.h"
#include "kui/vmu_game_card_service.h"
#include "kui/vmu_persist_profile.h"
#include "kui/vmu_store.h"
#include "kui/game_cover.h"
#include "kui/media.h"
#include "kui/core.h"
#include "diskio.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks,writes;
static int fail_after=-1;
static bool raw_open,cancelled_flag;
static uint8_t *disk;
static size_t disk_bytes;
static FATFS fs;
static struct kui_volume volume;
static struct kui_vmu_game_card_work work;
static char last_log[256];
#define CHECK(x) do {++checks;if(!(x)) {fprintf(stderr,"%s:%d: %s (%s)\n",__FILE__,__LINE__,#x,last_log);exit(1);}} while(0)
static void log_line(const char *format,...) {
    va_list args;va_start(args,format);vsnprintf(last_log,sizeof(last_log),format,args);va_end(args);
}
static bool cancelled(void) {return cancelled_flag;}
static uint64_t blocks(void *context) {(void)context;return disk_bytes/512u;}
static int read_media(void *context,uint32_t lba,size_t count,uint8_t *out) {
    (void)context;if((uint64_t)lba+count>disk_bytes/512u) return -1;
    memcpy(out,disk+(size_t)lba*512u,count*512u);return 0;
}
static int write_media(void *context,uint32_t lba,size_t count,const uint8_t *data) {
    (void)context;CHECK(!raw_open);
    unsigned attempt=writes++;
    if(fail_after>=0 && attempt>=(unsigned)fail_after) return -1;
    if((uint64_t)lba+count>disk_bytes/512u) return -1;
    memcpy(disk+(size_t)lba*512u,data,count*512u);return 0;
}
static int sync_media(void *context) {(void)context;return 0;}
static const struct kui_media_ops media={NULL,blocks,read_media,write_media,sync_media};
static void mount(void) {
    kui_media_set(&media);CHECK(f_mount(&fs,"0:",1)==FR_OK);
    volume=*kui_media_volume();CHECK(volume.partitioned && volume.start==2048u);
}
static void unmount(void) {CHECK(f_mount(NULL,"0:",0)==FR_OK);}
static int raw_read(void *context,uint32_t lba,uint8_t out[512]) {
    CHECK(context==&disk && raw_open);return read_media(NULL,lba,1u,out);
}
static bool identity(void *context,struct kui_vmu_game_card_raw *out) {
    CHECK(context==&volume && !raw_open);
    static const uint8_t cid[16]={3,'S','D','L','I','B','R','A','R','Y',2,3,4,5,6,7};
    memcpy(out->cid,cid,16);out->card_sectors=disk_bytes/512u;out->high_capacity=true;return true;
}
static bool raw_begin(void *context,struct kui_vmu_game_card_raw *out) {
    CHECK(context==&volume && !raw_open);if(f_mount(NULL,"0:",0)!=FR_OK) return false;
    raw_open=true;out->read_sector=raw_read;out->context=&disk;return true;
}
static bool raw_end(void *context) {
    CHECK(context==&volume && raw_open);raw_open=false;
    return f_mount(&fs,"0:",1)==FR_OK;
}
static void salt(void *context,uint8_t out[16]) {
    static unsigned serial;CHECK(context==&volume);memset(out,0x6c,16u);++serial;memcpy(out,&serial,sizeof(serial));
}
static const struct kui_vmu_game_card_platform platform={&volume,identity,raw_begin,raw_end,salt};
static void put32(uint8_t *p,uint32_t value) {
    for(unsigned i=0;i<4;i++) p[i]=(uint8_t)(value>>(8u*i));
}
static void put64(uint8_t *p,uint64_t value) {put32(p,(uint32_t)value);put32(p+4,(uint32_t)(value>>32));}
static void put16(uint8_t *p,unsigned value) {p[0]=(uint8_t)value;p[1]=(uint8_t)(value>>8);}
static size_t file_read(const char *path,void *out,size_t capacity) {
    FIL file;UINT got=0;
    if(f_open(&file,path,FA_READ)!=FR_OK) return SIZE_MAX;
    size_t bytes=(size_t)f_size(&file);
    CHECK(bytes<=capacity && f_read(&file,out,(UINT)bytes,&got)==FR_OK && got==bytes && f_close(&file)==FR_OK);
    return bytes;
}
static void file_write(const char *path,const void *data,size_t bytes) {
    FIL file;UINT done=0;CHECK(f_open(&file,path,FA_CREATE_ALWAYS|FA_WRITE)==FR_OK);
    CHECK(f_write(&file,data,(UINT)bytes,&done)==FR_OK && done==bytes && f_sync(&file)==FR_OK && f_close(&file)==FR_OK);
}
static void pair_path(char out[96],unsigned pair,const char *leaf) {
    snprintf(out,96,"0:/KUI/vmu-test/p%04u/%s",pair,leaf);
}
struct saved_file {char path[96];uint8_t *data;size_t bytes;};
static struct saved_file protected_files[32];
static unsigned protected_count;
static void protect(const char *path,size_t bytes) {
    CHECK(protected_count<32);struct saved_file *saved=protected_files+protected_count++;
    snprintf(saved->path,sizeof(saved->path),"%s",path);saved->data=malloc(bytes);saved->bytes=bytes;
    CHECK(saved->data && file_read(path,saved->data,bytes)==bytes);
}
static void protect_pair(unsigned pair,bool profile) {
    char path[96];const char *names[]={"card.vmd","card.journal","identity.bin"};
    const size_t sizes[]={131072u,1024u,16u};
    for(unsigned i=0;i<3u;i++) {pair_path(path,pair,names[i]);protect(path,sizes[i]);}
    if(profile) {char leaf[48];snprintf(leaf,sizeof(leaf),"retail-persistent-profile-%04u.bin",pair);pair_path(path,pair,leaf);protect(path,4096u);}
}
static void protected_unchanged(void) {
    uint8_t *scratch=malloc(131072u);CHECK(scratch);
    for(unsigned i=0;i<protected_count;i++) {
        const struct saved_file *saved=protected_files+i;
        CHECK(file_read(saved->path,scratch,131072u)==saved->bytes && !memcmp(scratch,saved->data,saved->bytes));
    }
    free(scratch);
}
static unsigned create_game(const char *product,uint8_t profile[4096]) {
    struct kui_vmu_game_card_choice choice={0};
    CHECK(kui_vmu_game_card_select(product,volume.start,(uint64_t)volume.start+volume.count,
          NULL,0u,&platform,&work,profile,&choice,log_line,cancelled)==KUI_VMU_GAME_CARD_READY);
    CHECK(choice.created && choice.pair);return choice.pair;
}
static const struct kui_vmu_backup_entry *find_row(const struct kui_vmu_backup_view *view,const char *path) {
    for(unsigned i=0;i<view->count;i++) if(!strcmp(view->entries[i].path,path)) return view->entries+i;
    return NULL;
}
static void cover_record(const char *file,const char *product,const char *title,const char *image) {
    struct kui_cover_record record={0};FILINFO info;uint8_t header[KUI_COVER_HEADER_BYTES];
    char source[192];snprintf(source,sizeof(source),"0:%s",image);CHECK(f_stat(source,&info)==FR_OK);
    snprintf(record.gdi_path,sizeof(record.gdi_path),"%s",image);
    snprintf(record.product,sizeof(record.product),"%s",product);
    snprintf(record.title,sizeof(record.title),"%s",title);
    record.gdi.bytes=info.fsize;record.gdi.date=info.fdate;record.gdi.time=info.ftime;
    /* No artwork is required to learn a cached disc title. */
    record.source=KUI_COVER_SOURCE_NONE;CHECK(kui_cover_header_encode(header,&record));
    file_write(file,header,sizeof(header));
}
static void display_metadata(const char *game_path,const char *import_path,const char *orphan_path,
    const struct kui_vmu_game_assignment assigns[2],const uint8_t blank[131072]) {
    struct kui_vmu_backup_view view;const struct kui_vmu_backup_entry *row;
    CHECK(f_mkdir("0:/Games")==FR_OK && f_mkdir("0:/KUI/covers")==FR_OK);
    file_write("0:/Games/one.gdi","a",1u);file_write("0:/Games/two.gdi","bb",2u);
    file_write("0:/Games/three.gdi","ccc",3u);
    cover_record("0:/KUI/covers/one.kcv","MK-51000","Resident Evil Code Veronica","/Games/one.gdi");
    cover_record("0:/KUI/covers/two.kcv","T-00001","Sonic Adventure","/Games/two.gdi");
    cover_record("0:/KUI/covers/three.kcv","T-00002","Crazy Taxi","/Games/three.gdi");
    writes=0;CHECK(!kui_vmu_library_list(0,&view,log_line,cancelled) && !writes);
    row=find_row(&view,game_path);CHECK(row && !strcmp(row->label,"Resident Evil Code Veronica") && !row->custom_label);
    CHECK(strstr(row->titles,"Resident Evil Code Veronica") && !row->contents[0]);
    row=find_row(&view,import_path);CHECK(row && row->users==2u && strstr(row->titles,"Sonic Adventure") && strstr(row->titles,"Crazy Taxi") && strstr(row->label,"+1 game"));
    row=find_row(&view,orphan_path);CHECK(row && !row->users && strstr(row->label,"Empty") && !row->titles[0]);

    /* A cover filename is never a product binding, and a broken header is
     * optional display metadata rather than a reason to hide a valid VMU. */
    uint8_t header[512];CHECK(file_read("0:/KUI/covers/two.kcv",header,sizeof(header))==sizeof(header));
    header[64]^=1;file_write("0:/KUI/covers/two.kcv",header,sizeof(header));writes=0;
    CHECK(!kui_vmu_library_list(0,&view,log_line,cancelled) && !writes);
    row=find_row(&view,import_path);CHECK(row && row->readable && strstr(row->titles,"T-00001") && !strstr(row->titles,"Sonic Adventure"));
    cover_record("0:/KUI/covers/two.kcv","T-00001","Sonic Adventure","/Games/two.gdi");
    file_write("0:/Games/two.gdi","changed",7u);writes=0;
    CHECK(!kui_vmu_library_list(0,&view,log_line,cancelled) && !writes);
    row=find_row(&view,import_path);CHECK(row && strstr(row->titles,"T-00001") && !strstr(row->titles,"Sonic Adventure"));
    cover_record("0:/KUI/covers/two.kcv","T-00001","Sonic Adventure","/Games/two.gdi");

    /* Conflicting records do not let directory order choose a false title. */
    cover_record("0:/KUI/covers/conflict.kcv","T-00001","A different title","/Games/three.gdi");writes=0;
    CHECK(!kui_vmu_library_list(0,&view,log_line,cancelled) && !writes);
    row=find_row(&view,import_path);CHECK(row && strstr(row->titles,"T-00001") && !strstr(row->titles,"Sonic Adventure") && !strstr(row->titles,"A different title"));
    CHECK(f_unlink("0:/KUI/covers/conflict.kcv")==FR_OK);
    uint8_t original[KUI_VMU_GAME_ASSIGN_WIRE_BYTES],wire[KUI_VMU_GAME_ASSIGN_WIRE_BYTES];
    struct kui_vmu_game_assign_table table;
    CHECK(file_read("0:/KUI/vmu-test/assign-a.bin",original,sizeof(original))==sizeof(original));
    CHECK(kui_vmu_game_assign_decode(original,sizeof(original),&table));
    table.entries[0].identity[0]^=1;CHECK(kui_vmu_game_assign_encode(&table,wire));
    file_write("0:/KUI/vmu-test/assign-a.bin",wire,sizeof(wire));writes=0;
    CHECK(!kui_vmu_library_list(0,&view,log_line,cancelled) && !writes);
    row=find_row(&view,import_path);CHECK(row && row->users==1u && !strstr(row->titles,"Sonic Adventure") && strstr(row->titles,"Crazy Taxi"));
    file_write("0:/KUI/vmu-test/assign-a.bin",original,sizeof(original));
    CHECK(!memcmp(assigns[0].identity,assigns[1].identity,16u));

    /* Existing saves give otherwise unbound cards useful context. Follow the
     * admitted file's FAT and header offset, rather than guessing block 0. */
    uint8_t *saved=malloc(131072u);CHECK(saved);memcpy(saved,blank,131072u);
    uint8_t *entry=saved+253u*512u;
    entry[0]=0x33;put16(entry+2,5u);memcpy(entry+4,"FILE_ONE",8u);put16(entry+24,2u);put16(entry+26,1u);
    put16(saved+254u*512u+5u*2u,9u);put16(saved+254u*512u+9u*2u,0xfffau);
    memcpy(saved+9u*512u+16u," Code Veronica save ",20u);
    entry+=32u;entry[0]=0x33;put16(entry+2,17u);memcpy(entry+4,"FILE_TWO",8u);put16(entry+24,1u);
    put16(saved+254u*512u+17u*2u,0xfffau);memcpy(saved+17u*512u+16u,"Sonic Adventure save",20u);
    file_write(orphan_path,saved,131072u);writes=0;
    CHECK(!kui_vmu_library_list(0,&view,log_line,cancelled) && !writes);
    row=find_row(&view,orphan_path);CHECK(row && !row->users && !row->titles[0]);
    CHECK(strstr(row->label,"Code Veronica") && strstr(row->contents,"Code Veronica save") && strstr(row->contents,"Sonic Adventure save"));
    struct kui_vmu_collection_card backup;
    CHECK(!kui_vmu_collection_publish(saved,131072u,NULL,&backup,log_line,cancelled));writes=0;
    CHECK(!kui_vmu_library_list(0,&view,log_line,cancelled) && !writes);
    row=find_row(&view,backup.path);CHECK(row && !row->active && !row->users && !row->titles[0]);
    CHECK(strstr(row->label,"Code Veronica") && strstr(row->contents,"Sonic Adventure save"));
    static const char *const leaves[]={"card.vmd","card.journal","identity.bin","card.vmd.crc","ready.bin"};
    char removed[192];
    for(unsigned i=0;i<sizeof(leaves)/sizeof(leaves[0]);i++) {
        snprintf(removed,sizeof(removed),"0:/KUI/vmu/cards/v%04u/%s",backup.id,leaves[i]);CHECK(f_unlink(removed)==FR_OK);
    }
    snprintf(removed,sizeof(removed),"0:/KUI/vmu/cards/v%04u",backup.id);CHECK(f_unlink(removed)==FR_OK);
    file_write(orphan_path,blank,131072u);free(saved);
}
static void journal_make(uint8_t journal[1024],const struct kui_vmu_persist_profile *p,unsigned block,uint8_t byte) {
    memset(journal,byte,512u);memset(journal+512u,0,512u);uint8_t *m=journal+512u;
    memcpy(m,"KUIVMUJ1",8u);put32(m+8,1u);put32(m+12,512u);memcpy(m+16,p->image_id,16u);
    put64(m+32,p->card_sectors);put32(m+44,block);put32(m+48,kui_crc32(0,journal,512u));
    put32(m+52,1u);put64(m+56,9u);
    uint8_t data[16];put64(data,p->card_sectors);put32(data+8,p->files[0].useful_count);put32(data+12,p->files[1].useful_count);
    uint32_t crc=kui_crc32(0,p->image_id,16u);crc=kui_crc32(crc,data,sizeof(data));
    for(unsigned role=0;role<2u;role++) for(unsigned i=0;i<p->files[role].useful_count;i++) {
        put64(data,p->files[role].useful[i].lba);put32(data+8,p->files[role].useful[i].blocks);
        crc=kui_crc32(crc,data,12u);
    }
    put32(m+40,crc);put32(m+508,kui_crc32(0,m,508u));
}
struct store_cut {uint64_t base_lba;unsigned writes,syncs;};
static int store_read(void *context,uint64_t lba,void *out) {
    (void)context;CHECK(raw_open && lba<disk_bytes/512u);
    memcpy(out,disk+(size_t)lba*512u,512u);return 0;
}
static int store_write(void *context,uint64_t lba,const void *data) {
    struct store_cut *cut=context;CHECK(raw_open && lba<disk_bytes/512u);++cut->writes;
    if(lba==cut->base_lba) return -1;
    memcpy(disk+(size_t)lba*512u,data,512u);return 0;
}
static int store_sync(void *context) {
    struct store_cut *cut=context;CHECK(raw_open);++cut->syncs;return 0;
}
static void actual_store_protocol(const char *path,const struct kui_vmu_persist_profile *p,
    const uint8_t original_image[131072],uint8_t result[131072]) {
    struct kui_vmu_store_extent image_map[KUI_VMU_STORE_MAX_EXTENTS],journal_map[KUI_VMU_STORE_MAX_EXTENTS];
    for(unsigned role=0;role<2u;role++) for(unsigned i=0;i<p->files[role].useful_count;i++) {
        struct kui_vmu_store_extent *entry=role?journal_map+i:image_map+i;
        entry->first_lba=p->files[role].useful[i].lba;entry->sectors=p->files[role].useful[i].blocks;
    }
    struct kui_vmu_store_config config={.card_sectors=p->card_sectors,
        .image_extents=image_map,.image_extent_count=p->files[0].useful_count,
        .journal_extents=journal_map,.journal_extent_count=p->files[1].useful_count};
    memcpy(config.image_id,p->image_id,16u);
    uint32_t base_lba;CHECK(!kui_vmu_persist_profile_map_lba(p,KUI_VMU_PERSIST_BASE,100u,&base_lba));
    struct store_cut cut={.base_lba=base_lba};
    struct kui_vmu_store_io io={&cut,store_read,store_write,store_sync};
    struct kui_vmu_store store;uint8_t payload[512];memset(payload,0xa7,sizeof(payload));
    char journal_path[192];size_t path_bytes=strlen(path);CHECK(path_bytes>8u && path_bytes+5u<=sizeof(journal_path));
    memcpy(journal_path,path,path_bytes-8u);memcpy(journal_path+path_bytes-8u,"card.journal",13u);
    uint8_t before[1024],committed[1024],checked[1024];
    CHECK(file_read(journal_path,before,sizeof(before))==sizeof(before));
    unmount();raw_open=true;
    CHECK(kui_vmu_store_open(&store,&io,&config)==KUI_VMU_STORE_OK);
    /* Actual native store writes invalidate, payload, committed marker and
     * then base. Interrupt exactly that final base write. The test never
     * authors or computes the marker format/hash for this fixture. */
    CHECK(kui_vmu_store_write(&store,100u,payload)==KUI_VMU_STORE_IO);
    CHECK(cut.writes==4u && cut.syncs==3u && !store.ready);
    raw_open=false;mount();CHECK(file_read(journal_path,committed,sizeof(committed))==sizeof(committed));
    writes=0;CHECK(!kui_vmu_library_read(path,result,log_line,cancelled) && !writes);
    CHECK(!memcmp(result,original_image,100u*512u));
    CHECK(!memcmp(result+100u*512u,payload,512u));
    CHECK(!memcmp(result+101u*512u,original_image+101u*512u,(256u-101u)*512u));
    CHECK(file_read(journal_path,checked,sizeof(checked))==sizeof(checked) && !memcmp(checked,committed,sizeof(checked)));
    CHECK(file_read(path,result,131072u)==131072u && !memcmp(result,original_image,131072u));
    file_write(journal_path,before,sizeof(before));protected_unchanged();
}
static void paths(void) {
    CHECK(kui_vmu_library_path("0:/KUI/vmu-test/p0005/card.vmd"));
    CHECK(kui_vmu_library_path("0:/KUI/vmu/cards/v9999/card.vmd"));
    static const char *const bad[]={NULL,"","/KUI/vmu-test/p0005/card.vmd","0:/KUI/vmu-test/p0000/card.vmd",
        "0:/KUI/vmu-test/p00050/card.vmd","0:/KUI/vmu-test/p0005/../card.vmd", "0:/KUI/vmu-test/P0005/card.vmd",
        "0:/KUI/vmu/cards/v0000/card.vmd","0:/KUI/vmu-test/p0005/card.vmd/extra","1:/KUI/vmu-test/p0005/card.vmd"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++) CHECK(!kui_vmu_library_path(bad[i]));
}
static void allocation_guard(const char *damaged_path,bool directory,const char *renamed_path,const uint8_t rename_identity[16]) {
    uint32_t cluster;
    if(directory) {
        DIR dir;CHECK(f_opendir(&dir,damaged_path)==FR_OK);cluster=dir.obj.sclust;CHECK(f_closedir(&dir)==FR_OK);
    } else {
        FIL file;CHECK(f_open(&file,damaged_path,FA_READ)==FR_OK);cluster=file.obj.sclust;CHECK(f_close(&file)==FR_OK);
    }
    CHECK(cluster>=2u);
    size_t positions[2];uint8_t old[2][4];unsigned count;
    if(fs.fs_type==FS_EXFAT) {
        uint32_t bit=cluster-2u;
        positions[0]=((size_t)volume.start+fs.bitbase)*512u+bit/8u;
        old[0][0]=disk[positions[0]];CHECK(old[0][0]&(1u<<(bit%8u)));
        disk[positions[0]]&=(uint8_t)~(1u<<(bit%8u));count=1u;
    } else {
        count=fs.n_fats;CHECK(count && count<=2u);
        for(unsigned i=0;i<count;i++) {
            positions[i]=((size_t)volume.start+fs.fatbase+(size_t)i*fs.fsize)*512u+(size_t)cluster*4u;
            memcpy(old[i],disk+positions[i],4u);memset(disk+positions[i],0,4u);
        }
    }
    bool exfat=fs.fs_type==FS_EXFAT;
    unmount();mount();writes=0;
    CHECK(kui_vmu_library_rename(renamed_path,"Do not allocate over another card",rename_identity,log_line,cancelled)<0 && !writes);
    CHECK(kui_vmu_library_check_allocation(cancelled)<0 && !writes);
    unmount();
    for(unsigned i=0;i<count;i++) memcpy(disk+positions[i],old[i],exfat?1u:4u);
    mount();protected_unchanged();
}
static void file_cycle_guard(const char *damaged_path,const char *renamed_path,
    const uint8_t rename_identity[16]) {
    if(fs.fs_type==FS_EXFAT) return; /* A contiguous exFAT file has no FAT chain. */
    FIL file;CHECK(f_open(&file,damaged_path,FA_READ)==FR_OK);
    uint32_t cluster=file.obj.sclust;CHECK(cluster>=2u && f_close(&file)==FR_OK);
    size_t positions[2];uint8_t old[2][4];unsigned count=fs.n_fats;CHECK(count && count<=2u);
    for(unsigned i=0;i<count;i++) {
        positions[i]=((size_t)volume.start+fs.fatbase+(size_t)i*fs.fsize)*512u+(size_t)cluster*4u;
        memcpy(old[i],disk+positions[i],4u);put32(disk+positions[i],cluster);
    }
    unmount();mount();writes=0;
    CHECK(kui_vmu_library_check_allocation(cancelled)<0 && !writes);
    CHECK(kui_vmu_library_rename(renamed_path,"Refuse another card's cyclic FAT chain",rename_identity,log_line,cancelled)<0 && !writes);
    unmount();for(unsigned i=0;i<count;i++) memcpy(disk+positions[i],old[i],4u);
    mount();protected_unchanged();
}
static uint16_t exfat_entry_checksum(const uint8_t *entries,size_t bytes) {
    uint16_t sum=0;
    for(size_t i=0;i<bytes;i++) {
        if(i==2u || i==3u) continue;
        sum=(uint16_t)((sum>>1)|((sum&1u)?0x8000u:0u));
        sum=(uint16_t)(sum+entries[i]);
    }
    return sum;
}
static void alias_guard(const char *protected_path,bool directory,unsigned renamed_pair,const char *renamed_path,
    const uint8_t rename_identity[16]) {
    FIL file;uint32_t borrowed_cluster;
    if(directory) {
        DIR dir;CHECK(f_opendir(&dir,protected_path)==FR_OK);borrowed_cluster=dir.obj.sclust;CHECK(f_closedir(&dir)==FR_OK);
    } else {
        CHECK(f_open(&file,protected_path,FA_READ)==FR_OK);borrowed_cluster=file.obj.sclust;CHECK(f_close(&file)==FR_OK);
    }
    CHECK(borrowed_cluster>=2u);
    char name_path[96];pair_path(name_path,renamed_pair,"name-a.bin");
    CHECK(f_open(&file,name_path,FA_READ)==FR_OK);
    bool exfat=fs.fs_type==FS_EXFAT;size_t position;
    if(exfat) {
        CHECK(file.obj.c_scl>=2u);
        CHECK(file.obj.c_ofs+128u<=(size_t)fs.csize*512u);
        position=((size_t)volume.start+fs.database+(size_t)(file.obj.c_scl-2u)*fs.csize)*512u+file.obj.c_ofs;
    } else {
        CHECK(file.dir_ptr>=fs.win && file.dir_ptr+32u<=fs.win+sizeof(fs.win));
        position=((size_t)volume.start+file.dir_sect)*512u+(size_t)(file.dir_ptr-fs.win);
    }
    CHECK(f_close(&file)==FR_OK);unmount();
    uint8_t original[128];size_t bytes=exfat?((size_t)disk[position+1u]+1u)*32u:32u;
    CHECK(bytes<=sizeof(original));memcpy(original,disk+position,bytes);
    if(exfat) {
        CHECK(disk[position]==0x85u && disk[position+32u]==0xc0u);
        put32(disk+position+32u+20u,borrowed_cluster);
        uint16_t sum=exfat_entry_checksum(disk+position,bytes);
        disk[position+2u]=(uint8_t)sum;disk[position+3u]=(uint8_t)(sum>>8);
    } else {
        disk[position+20u]=(uint8_t)(borrowed_cluster>>16);
        disk[position+21u]=(uint8_t)(borrowed_cluster>>24);
        disk[position+26u]=(uint8_t)borrowed_cluster;
        disk[position+27u]=(uint8_t)(borrowed_cluster>>8);
    }
    mount();writes=0;
    /* name-b is the current complete name, so the attempted update targets
     * inactive name-a. Its forged directory entry aliases another card. */
    CHECK(kui_vmu_library_rename(renamed_path,"Refuse aliased inactive name file",rename_identity,log_line,cancelled)<0 && !writes);
    CHECK(kui_vmu_library_check_allocation(cancelled)<0 && !writes);
    unmount();memcpy(disk+position,original,bytes);mount();protected_unchanged();
}
static bool ascii_fold_equal(unsigned char a,unsigned char b) {
    if(a>='a' && a<='z') a=(unsigned char)(a-'a'+'A');
    if(b>='a' && b<='z') b=(unsigned char)(b-'a'+'A');
    return a==b;
}
static void directory_alias_guard(unsigned aliased_pair,const char *protected_path,const uint8_t rename_identity[16]) {
    FIL file;CHECK(f_open(&file,protected_path,FA_READ)==FR_OK);
    uint32_t borrowed_cluster=file.obj.sclust;CHECK(borrowed_cluster>=2u && f_close(&file)==FR_OK);
    DIR dir;CHECK(f_opendir(&dir,"0:/KUI/vmu-test")==FR_OK);uint32_t parent_cluster=dir.obj.sclust;CHECK(f_closedir(&dir)==FR_OK);
    CHECK(parent_cluster>=2u);bool exfat=fs.fs_type==FS_EXFAT;
    size_t base=((size_t)volume.start+fs.database+(size_t)(parent_cluster-2u)*fs.csize)*512u;
    size_t position=SIZE_MAX,bytes=0,cluster_bytes=(size_t)fs.csize*512u;
    char name[16];CHECK(aliased_pair && aliased_pair<=9999u);snprintf(name,sizeof(name),"p%04u",aliased_pair);
    for(size_t at=0;at+32u<=cluster_bytes;at+=32u) {
        const uint8_t *entry=disk+base+at;bool matches=true;
        if(exfat) {
            if(entry[0]!=0x85u || entry[1]!=2u || at+96u>cluster_bytes || entry[32]!=0xc0u || entry[35]!=5u || entry[64]!=0xc1u) continue;
            for(unsigned i=0;i<5u;i++) if(!ascii_fold_equal(entry[66u+i*2u],(unsigned char)name[i]) || entry[67u+i*2u]) matches=false;
            if(matches) {position=base+at;bytes=96u;break;}
        } else {
            if(entry[11]!=AM_DIR) continue;
            for(unsigned i=0;i<5u;i++) if(!ascii_fold_equal(entry[i],(unsigned char)name[i])) matches=false;
            for(unsigned i=5u;i<11u;i++) if(entry[i]!=' ') matches=false;
            if(matches) {position=base+at;bytes=32u;break;}
        }
    }
    CHECK(position!=SIZE_MAX);uint8_t original[96];memcpy(original,disk+position,bytes);unmount();
    if(exfat) {
        put32(disk+position+52u,borrowed_cluster);uint16_t sum=exfat_entry_checksum(disk+position,bytes);
        disk[position+2u]=(uint8_t)sum;disk[position+3u]=(uint8_t)(sum>>8);
    } else {
        disk[position+20u]=(uint8_t)(borrowed_cluster>>16);disk[position+21u]=(uint8_t)(borrowed_cluster>>24);
        disk[position+26u]=(uint8_t)borrowed_cluster;disk[position+27u]=(uint8_t)(borrowed_cluster>>8);
    }
    mount();writes=0;
    CHECK(kui_vmu_library_check_allocation(cancelled)<0 && !writes);
    CHECK(kui_vmu_library_rename(protected_path,"Refuse directory alias into another card",rename_identity,log_line,cancelled)<0 && !writes);
    unmount();memcpy(disk+position,original,bytes);mount();protected_unchanged();
}
int main(int argc,char **argv) {
    CHECK(argc==2);FILE *input=fopen(argv[1],"rb");CHECK(input && !fseek(input,0,SEEK_END));
    long size=ftell(input);CHECK(size>0 && !fseek(input,0,SEEK_SET));disk_bytes=(size_t)size;disk=malloc(disk_bytes);
    CHECK(disk && fread(disk,1,disk_bytes,input)==disk_bytes && !fclose(input));mount();paths();
    bool exfat=fs.fs_type==FS_EXFAT;
    struct kui_vmu_backup_view listing={0};
    writes=0;CHECK(!kui_vmu_library_list(0u,&listing,log_line,cancelled) && !listing.total && !writes);
    uint8_t profile[4096];unsigned game=create_game("MK-51000",profile);
    char game_path[96];pair_path(game_path,game,"card.vmd");
    uint8_t *image=malloc(131072u),*result=malloc(131072u);CHECK(image && result);
    CHECK(file_read(game_path,image,131072u)==131072u);
    struct kui_vmu_game_card_choice imported={0};uint8_t import_id[16],fresh[16];memset(fresh,0x91,16);
    CHECK(kui_vmu_game_card_import_publish(image,131072u,fresh,&work,&imported,import_id,log_line,cancelled)==KUI_VMU_GAME_CARD_READY);
    unsigned import_pair=imported.pair;
    struct kui_vmu_game_assignment assigns[2]={0};
    CHECK(kui_vmu_game_card_key("T-00001",assigns[0].key));assigns[0].pair=imported.pair;memcpy(assigns[0].identity,import_id,16u);
    CHECK(kui_vmu_game_card_key("T-00002",assigns[1].key));assigns[1].pair=imported.pair;memcpy(assigns[1].identity,import_id,16u);
    CHECK(kui_vmu_game_card_assignments_apply(assigns,2u,&work,&imported,log_line,cancelled)==KUI_VMU_GAME_CARD_READY);
    uint8_t orphan_profile[4096];unsigned orphan=create_game("T-ORPHAN",orphan_profile);
    char path[96];pair_path(path,orphan,"game.bin");CHECK(f_unlink(path)==FR_OK);
    struct kui_vmu_collection_card snapshot;
    CHECK(!kui_vmu_collection_publish(image,131072u,"Old backup",&snapshot,log_line,cancelled));
    char import_path[96];pair_path(import_path,import_pair,"card.vmd");
    char orphan_path[96];pair_path(orphan_path,orphan,"card.vmd");
    display_metadata(game_path,import_path,orphan_path,assigns,image);
    protect_pair(game,true);protect_pair(import_pair,false);protect_pair(orphan,true);
    protect("0:/KUI/vmu-test/assign-a.bin",KUI_VMU_GAME_ASSIGN_WIRE_BYTES);
    writes=0;CHECK(!kui_vmu_library_list(0u,&listing,log_line,cancelled) && listing.total==4u && listing.count==4u && !writes);
    const struct kui_vmu_backup_entry *row=find_row(&listing,game_path);CHECK(row && row->active && row->free_blocks==200u && row->users==1u);
    row=find_row(&listing,import_path);CHECK(row && row->active && row->users==2u && strstr(row->games,"T-00001") && strstr(row->games,"T-00002"));
    row=find_row(&listing,orphan_path);CHECK(row && row->active && row->free_blocks==200u);
    row=find_row(&listing,snapshot.path);CHECK(row && !row->active && row->free_blocks==200u && row->custom_label && !strcmp(row->label,"Old backup"));
    CHECK(!kui_vmu_library_read(game_path,result,log_line,cancelled) && !memcmp(result,image,131072u) && !writes);
    CHECK(!kui_vmu_library_read(snapshot.path,result,log_line,cancelled) && !memcmp(result,image,131072u) && !writes);
    protected_unchanged();

    /* A durable redo record can name data not yet in card.vmd. The manager
     * displays it from RAM, never replays it to the card or journal file. */
    struct kui_vmu_persist_profile p;CHECK(!kui_vmu_persist_profile_decode(&p,profile,sizeof(profile)));
    actual_store_protocol(game_path,&p,image,result);
    uint8_t journal[1024],check_journal[1024];journal_make(journal,&p,100u,0x5a);
    pair_path(path,game,"card.journal");file_write(path,journal,sizeof(journal));writes=0;
    CHECK(!kui_vmu_library_read(game_path,result,log_line,cancelled) && !writes);
    CHECK(!memcmp(result,image,100u*512u));for(unsigned i=0;i<512u;i++) CHECK(result[100u*512u+i]==0x5a);
    CHECK(file_read(path,check_journal,sizeof(check_journal))==sizeof(journal) && !memcmp(journal,check_journal,sizeof(journal)));
    CHECK(file_read(game_path,result,131072u)==131072u && !memcmp(result,image,131072u));
    uint8_t invalid[1024];memcpy(invalid,journal,sizeof(invalid));invalid[512u+16u]^=1;put32(invalid+1020,kui_crc32(0,invalid+512u,508u));
    file_write(path,invalid,sizeof(invalid));writes=0;CHECK(kui_vmu_library_read(game_path,result,log_line,cancelled)<0 && !writes);
    memcpy(invalid,journal,sizeof(invalid));invalid[512u+40u]^=1;put32(invalid+1020,kui_crc32(0,invalid+512u,508u));
    file_write(path,invalid,sizeof(invalid));CHECK(kui_vmu_library_read(game_path,result,log_line,cancelled)<0);
    memcpy(invalid,journal,sizeof(invalid));invalid[0]^=1;file_write(path,invalid,sizeof(invalid));CHECK(kui_vmu_library_read(game_path,result,log_line,cancelled)<0);
    memcpy(invalid,journal,sizeof(invalid));invalid[1020]^=1;file_write(path,invalid,sizeof(invalid));writes=0;
    CHECK(!kui_vmu_library_read(game_path,result,log_line,cancelled) && !memcmp(result,image,131072u) && !writes);
    memset(journal,0,sizeof(journal));file_write(path,journal,sizeof(journal));protected_unchanged();

    /* One damaged assignment record retains the other complete generation;
     * no complete generation must not be advertised as an unassigned card. */
    uint8_t assignment_wire[KUI_VMU_GAME_ASSIGN_WIRE_BYTES];
    CHECK(file_read("0:/KUI/vmu-test/assign-a.bin",assignment_wire,sizeof(assignment_wire))==sizeof(assignment_wire));
    file_write("0:/KUI/vmu-test/assign-b.bin",assignment_wire,sizeof(assignment_wire));
    assignment_wire[100]^=1;file_write("0:/KUI/vmu-test/assign-b.bin",assignment_wire,sizeof(assignment_wire));
    CHECK(!kui_vmu_library_list(0u,&listing,log_line,cancelled));
    file_write("0:/KUI/vmu-test/assign-a.bin",assignment_wire,sizeof(assignment_wire));writes=0;
    CHECK(kui_vmu_library_list(0u,&listing,log_line,cancelled)<0 && !writes);
    assignment_wire[100]^=1;file_write("0:/KUI/vmu-test/assign-a.bin",assignment_wire,sizeof(assignment_wire));
    CHECK(f_unlink("0:/KUI/vmu-test/assign-b.bin")==FR_OK);protected_unchanged();

    CHECK(!kui_vmu_library_rename(game_path,"Code Veronica",p.image_id,log_line,cancelled));
    CHECK(!kui_vmu_library_rename(game_path,"Shared saves",p.image_id,log_line,cancelled));
    CHECK(!kui_vmu_library_list(0u,&listing,log_line,cancelled));row=find_row(&listing,game_path);CHECK(row && !strcmp(row->label,"Shared saves") && row->custom_label && strstr(row->titles,"Resident Evil Code Veronica"));
    char game_label[49];writes=0;CHECK(!kui_vmu_library_label(game_path,p.image_id,game_label,cancelled) && !strcmp(game_label,"Shared saves") && !writes);
    uint8_t name_wire[128];pair_path(path,game,"name-b.bin");CHECK(file_read(path,name_wire,sizeof(name_wire))==sizeof(name_wire));
    name_wire[60]^=1;file_write(path,name_wire,sizeof(name_wire));
    CHECK(!kui_vmu_library_list(0u,&listing,log_line,cancelled));row=find_row(&listing,game_path);CHECK(row && !strcmp(row->label,"Code Veronica"));
    CHECK(!kui_vmu_library_rename(game_path,"Shared saves",p.image_id,log_line,cancelled));
    pair_path(path,game,"name-b.bin");CHECK(file_read(path,name_wire,sizeof(name_wire))==sizeof(name_wire));
    name_wire[24]^=1;put32(name_wire+124,kui_crc32(0,name_wire,124u));file_write(path,name_wire,sizeof(name_wire));
    CHECK(!kui_vmu_library_list(0u,&listing,log_line,cancelled));row=find_row(&listing,game_path);CHECK(row && !strcmp(row->label,"Code Veronica"));
    CHECK(!kui_vmu_library_rename(game_path,"Shared saves",p.image_id,log_line,cancelled));
    CHECK(kui_vmu_library_rename(game_path,"bad\nname",p.image_id,log_line,cancelled)<0);
    CHECK(kui_vmu_library_rename(game_path,"",p.image_id,log_line,cancelled)<0);
    uint8_t old_identity[16];memcpy(old_identity,p.image_id,16u);old_identity[0]^=1;writes=0;
    CHECK(kui_vmu_library_rename(game_path,"Stale selection",old_identity,log_line,cancelled)<0 && !writes);
    CHECK(kui_vmu_library_label(game_path,old_identity,game_label,cancelled)<0 && !game_label[0] && !writes);
    protected_unchanged();
    allocation_guard(import_path,false,game_path,p.image_id);
    file_cycle_guard(import_path,game_path,p.image_id);
    alias_guard(import_path,false,game,game_path,p.image_id);
    char import_dir[96];snprintf(import_dir,sizeof(import_dir),"%.*s",(int)(strlen(import_path)-9u),import_path);
    allocation_guard(import_dir,true,game_path,p.image_id);
    alias_guard(import_dir,true,game,game_path,p.image_id);
    directory_alias_guard(import_pair,game_path,p.image_id);

    unmount();uint8_t *seed=malloc(disk_bytes);CHECK(seed);memcpy(seed,disk,disk_bytes);
    mount();writes=0;CHECK(!kui_vmu_library_rename(game_path,"After power cut",p.image_id,log_line,cancelled));unsigned complete_writes=writes;
    unmount();CHECK(complete_writes && complete_writes<128u);
    for(unsigned cut=0;cut<=complete_writes;cut++) {
        memcpy(disk,seed,disk_bytes);fail_after=-1;mount();writes=0;fail_after=(int)cut;
        int saved=kui_vmu_library_rename(game_path,"After power cut",p.image_id,log_line,cancelled);
        fail_after=-1;unmount();mount();
        CHECK(!kui_vmu_library_list(0u,&listing,log_line,cancelled));row=find_row(&listing,game_path);CHECK(row);
        CHECK(!strcmp(row->label,"Shared saves") || !strcmp(row->label,"After power cut"));
        if(!saved) CHECK(!strcmp(row->label,"After power cut"));
        protected_unchanged();unmount();
    }
    memcpy(disk,seed,disk_bytes);mount();
    for(unsigned i=0;i<10u;i++) {snprintf(path,sizeof(path),"0:/KUI/vmu-test/p%04u",1000u+i);CHECK(f_mkdir(path)==FR_OK);}
    writes=0;CHECK(!kui_vmu_library_list(0u,&listing,log_line,cancelled) && listing.total==14u && listing.count==8u && !writes);
    bool unavailable=false;for(unsigned i=0;i<listing.count;i++) unavailable|=!listing.entries[i].readable;
    CHECK(unavailable);
    CHECK(!kui_vmu_library_list(1u,&listing,log_line,cancelled) && listing.total==14u && listing.count==6u && !writes);
    CHECK(find_row(&listing,snapshot.path)!=NULL);
    CHECK(kui_vmu_library_list(2u,&listing,log_line,cancelled)<0 && !writes);
    cancelled_flag=true;writes=0;
    CHECK(kui_vmu_library_read(game_path,result,log_line,cancelled)<0 && !writes);
    CHECK(kui_vmu_library_list(0u,&listing,log_line,cancelled)<0 && !writes);
    CHECK(kui_vmu_library_rename(game_path,"Cancelled",p.image_id,log_line,cancelled)<0 && !writes);
    cancelled_flag=false;protected_unchanged();unmount();
    printf("VMU library (%s): %u checks, %u write boundaries passed\n",exfat?"exFAT":"FAT32",checks,complete_writes+1u);
    for(unsigned i=0;i<protected_count;i++) free(protected_files[i].data);
    free(seed);free(image);free(result);free(disk);return 0;
}
