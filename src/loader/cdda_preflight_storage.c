/* SPDX-License-Identifier: GPL-3.0-only */
#include "cdda_preflight_storage.h"
#include "cdda_storage.h"
#include "kui/hash.h"
#include "ff.h"
#include <limits.h>
#include <string.h>

/* The profile13 build provides this read-only view of its mounted SCI volume;
 * older harness profiles do not compile this adapter. */
int cdda_storage_geometry(uint64_t *card_sectors,uint32_t *partition_start,
    uint32_t *partition_sectors);

#if FF_FS_READONLY != 1 || FF_FS_MINIMIZE != 0 || FF_USE_FASTSEEK != 1
#error Profile13 requires its private read-only directory/fast-seek configuration
#endif

#define SCAN_DEPTH 8u
#define SCAN_DIR_MAX 128u
#define SCAN_ENTRY_MAX 4096u
#define TOY_GDI_BYTES 451u
#define CLMT_WORDS (2u*CDDA_PREFLIGHT_EXTENT_MAX+2u)

static const uint8_t toy_digest[32]={
    0x96,0xe3,0xa5,0x4b,0x9a,0xa5,0x28,0xc7,
    0x17,0x21,0x21,0xba,0x3c,0x6c,0xfa,0xbf,
    0x39,0x1a,0xac,0xb5,0xa8,0x4f,0x29,0x2e,
    0x89,0x84,0x3f,0xf2,0xd8,0x85,0x38,0x03};
static char selected[CDDA_PREFLIGHT_PATH_CAP];
static char folder[CDDA_PREFLIGHT_PATH_CAP];
static char scan_path[CDDA_PREFLIGHT_PATH_CAP];
static char named_path[CDDA_PREFLIGHT_PATH_CAP];
static char cached_name[KUI_GAME_NAME_CAP];
static char config[CDDA_PREFLIGHT_PATH_CAP];
static uint8_t descriptor[TOY_GDI_BYTES];
static DIR directories[SCAN_DEPTH+1u];
static size_t path_lengths[SCAN_DEPTH+1u];
static FILINFO info;
static FIL cached, candidate;
static DWORD clmt[CLMT_WORDS];
static uint32_t mapped_runs,mapped_csize,mapped_database;
static bool cached_open, candidate_open, directory_open[SCAN_DEPTH+1u];
static bool (*cancel_callback)(void *);
static void *cancel_context;
static struct cdda_preflight_storage_scan scan;
static const char *failure;
static bool found;

static int fail(const char *text) { failure=text;return -1; }
static bool cancelled(void) {
    return cancel_callback && cancel_callback(cancel_context);
}
static int check_cancel(void) {
    return cancelled()?fail("Preflight cancelled"):0;
}
static unsigned lower(unsigned ch) {return ch>='A'&&ch<='Z'?ch+('a'-'A'):ch;}
static bool same(const char *a,const char *b) {
    while(*a && lower((uint8_t)*a)==lower((uint8_t)*b)) {a++;b++;}
    return !*a && !*b;
}
static bool fixture_path(const char *path) {
    const char *base="0:/KUI/tests/cdda";
    while(*base && lower((uint8_t)*path)==lower((uint8_t)*base)) {path++;base++;}
    return !*base && (!*path || *path=='/');
}
static bool safe_component(const char *text,size_t maximum) {
    if(!text || !*text) return false;
    size_t n=0;
    while(text[n]) {
        unsigned ch=(uint8_t)text[n];
        if(++n>=maximum || ch<32u || ch==127u || ch=='/' || ch=='\\' || ch==':')
            return false;
    }
    return !(text[0]=='.' && (!text[1] || (text[1]=='.' && !text[2])));
}
static bool gdi_name(const char *name) {
    size_t n=strlen(name);
    return n>=4u && name[n-4u]=='.' && lower((uint8_t)name[n-3u])=='g' &&
        lower((uint8_t)name[n-2u])=='d' && lower((uint8_t)name[n-1u])=='i';
}
static int join(char *out,const char *base,const char *name) {
    size_t a=strlen(base),b=strlen(name);
    if(!safe_component(name,CDDA_PREFLIGHT_PATH_CAP) ||
       a+b+2u>CDDA_PREFLIGHT_PATH_CAP) return fail("Preflight path limit");
    memcpy(out,base,a);
    if(!a || base[a-1u]!='/') out[a++]='/';
    memcpy(out+a,name,b+1u);return 0;
}
static void candidate_close(void) {
    if(candidate_open) {(void)f_close(&candidate);candidate_open=false;}
}
static void close_cached(void) {
    if(cached_open) {(void)f_close(&cached);cached_open=false;}
    cached_name[0]=0;
    mapped_runs=0;
}
void cdda_preflight_storage_close(void) {
    candidate_close();close_cached();
    for(unsigned i=0;i<=SCAN_DEPTH;i++) if(directory_open[i]) {
        (void)f_closedir(&directories[i]);directory_open[i]=false;
    }
}
void cdda_preflight_storage_cancel(bool (*callback)(void *),void *context) {
    cancel_callback=callback;cancel_context=context;
}

/* Exact descriptor metadata is a fingerprint of the user's supplied dump,
 * not a replacement for validating its fifteen actual backing lengths. */
static int check_candidate(const char *path,bool required) {
    if(check_cancel()) return -1;
    FRESULT result=f_open(&candidate,path,FA_READ);
    if(result!=FR_OK) return fail("Cannot read GDI candidate");
    candidate_open=true;
    if(f_size(&candidate)!=TOY_GDI_BYTES) {
        candidate_close();return required?fail("Original Toy descriptor required"):0;
    }
    UINT got=0;
    result=f_read(&candidate,descriptor,TOY_GDI_BYTES,&got);
    candidate_close();
    if(result!=FR_OK || got!=TOY_GDI_BYTES) return fail("Short GDI candidate read");
    struct kui_sha256 hash;uint8_t digest[32];
    kui_sha256_init(&hash);kui_sha256_update(&hash,descriptor,TOY_GDI_BYTES);
    kui_sha256_digest(&hash,digest);
    if(memcmp(digest,toy_digest,sizeof(digest)))
        return required?fail("Original Toy descriptor required"):0;
    scan.descriptors++;
    size_t length=strlen(path),base=length;
    while(base && path[base-1u]!='/') base--;
    if(!base || length>=CDDA_PREFLIGHT_PATH_CAP) return fail("Invalid GDI path");
    memcpy(folder,path,base);folder[base]=0;
    for(unsigned n=1;n<=15u;n++) {
        if(check_cancel()) return -1;
        char name[12]="track00.raw";
        name[5]=(char)('0'+n/10u);name[6]=(char)('0'+n%10u);
        if(n==1u || n==3u || n==15u) {name[8]='b';name[9]='i';name[10]='n';}
        if(join(named_path,folder,name)) return -1;
        result=f_stat(named_path,&info);
        if(result==FR_NO_FILE || result==FR_NO_PATH ||
           (result==FR_OK && ((info.fattrib&AM_DIR) || !info.fsize ||
            info.fsize>UINT32_MAX || info.fsize%KUI_GAME_RAW_BYTES))) {
            scan.incomplete++;
            return required?fail("Original Toy image is incomplete"):0;
        }
        if(result!=FR_OK) return fail("Cannot stat Toy backing");
    }
    scan.candidates++;
    if(scan.candidates>1u) return fail("Multiple complete Toy images; use preflight.cfg");
    memcpy(selected,path,length+1u);found=true;return 0;
}

static bool valid_config_path(const char *path) {
    if(path[0]!='0' || path[1]!=':' || path[2]!='/') return false;
    size_t start=3u;
    for(size_t n=3u;;n++) {
        unsigned ch=(uint8_t)path[n];
        if(ch && (ch<32u || ch>126u || ch=='\\' || ch==':')) return false;
        if(ch=='/' || !ch) {
            size_t count=n-start;
            if(!count || (count==1u && path[start]=='.') ||
               (count==2u && path[start]=='.' && path[start+1u]=='.')) return false;
            if(!ch) return gdi_name(path+start);
            start=n+1u;
        }
    }
}
static int configured_path(void) {
    FRESULT result=f_open(&candidate,"0:/KUI/tests/cdda/preflight.cfg",FA_READ);
    if(result==FR_NO_FILE || result==FR_NO_PATH) return 0;
    if(result!=FR_OK) return fail("Cannot read preflight.cfg");
    candidate_open=true;
    FSIZE_t bytes=f_size(&candidate);
    if(!bytes || bytes>=sizeof(config)) {candidate_close();return fail("preflight.cfg size");}
    UINT got=0;result=f_read(&candidate,config,(UINT)bytes,&got);candidate_close();
    if(result!=FR_OK || got!=bytes) return fail("Short preflight.cfg read");
    config[got]=0;
    if(got && config[got-1u]=='\n') config[--got]=0;
    if(got && config[got-1u]=='\r') config[--got]=0;
    if(strlen(config)!=got || !valid_config_path(config)) return fail("Invalid preflight.cfg path");
    if(fixture_path(config))
        return fail("Use the complete original game folder");
    scan.configured=true;
    return check_candidate(config,true)?-1:1;
}

int cdda_preflight_storage_discover(void) {
    cdda_preflight_storage_close();
    memset(&scan,0,sizeof(scan));found=false;selected[0]=0;folder[0]=0;failure=NULL;
    if(check_cancel()) return -1;
    if(cdda_storage_init()) return fail(cdda_storage_last_failure());
    int configured=configured_path();
    if(configured<0) goto failed;
    if(configured>0) goto selected;
    memcpy(scan_path,"0:/",4u);unsigned depth=0;
    path_lengths[0]=3u;
    if(f_opendir(&directories[0],scan_path)!=FR_OK) {fail("Cannot scan card root");goto failed;}
    directory_open[0]=true;scan.directories=1u;
    for(;;) {
        if(check_cancel()) goto failed;
        FRESULT result=f_readdir(&directories[depth],&info);
        if(result!=FR_OK) {fail("Directory scan I/O");goto failed;}
        if(!info.fname[0]) {
            if(f_closedir(&directories[depth])!=FR_OK) {fail("Directory close failure");goto failed;}
            directory_open[depth]=false;
            if(!depth) break;
            depth--;scan_path[path_lengths[depth]]=0;continue;
        }
        if(same(info.fname,".") || same(info.fname,"..")) continue;
        if(++scan.entries>SCAN_ENTRY_MAX) {fail("Directory entry limit; use preflight.cfg");goto failed;}
        if(join(named_path,scan_path,info.fname)) goto failed;
        if(info.fattrib&AM_DIR) {
            if(same(named_path,"0:/KUI/tests/cdda")) continue;
            if(depth==SCAN_DEPTH || scan.directories==SCAN_DIR_MAX) {
                fail("Directory scan limit; use preflight.cfg");goto failed;
            }
            size_t length=strlen(named_path);memcpy(scan_path,named_path,length+1u);
            depth++;path_lengths[depth]=length;
            if(f_opendir(&directories[depth],scan_path)!=FR_OK) {fail("Cannot scan directory");goto failed;}
            directory_open[depth]=true;scan.directories++;continue;
        }
        if(gdi_name(info.fname)) {
            /* check_candidate reuses named_path for sibling paths. Preserve
             * its input in config, independent of all metadata scratch. */
            memcpy(config,named_path,strlen(named_path)+1u);
            if(check_candidate(config,false)) goto failed;
        }
    }
    if(!found) {
        fail(scan.incomplete?"Original Toy image is incomplete":"Complete Toy image not found");goto failed;
    }
selected:
    {
        size_t base=strlen(selected);
        while(base && selected[base-1u]!='/') base--;
        if(!base) {fail("Invalid selected GDI path");goto failed;}
        memcpy(folder,selected,base);folder[base]=0;
    }
    return 0;
failed:
    cdda_preflight_storage_close();found=false;return -1;
}

const char *cdda_preflight_storage_path(void) {return found?selected:NULL;}
const struct cdda_preflight_storage_scan *cdda_preflight_storage_scan(void) {return &scan;}
const char *cdda_preflight_storage_failure(void) {return failure?failure:"Preflight storage failure";}

int cdda_preflight_storage_descriptor(void *out,size_t capacity,size_t *bytes) {
    if(!found || !out || !bytes || capacity<TOY_GDI_BYTES) return fail("Descriptor buffer");
    if(check_cancel()) return -1;
    if(f_open(&candidate,selected,FA_READ)!=FR_OK) return fail("Selected GDI missing");
    candidate_open=true;
    if(f_size(&candidate)!=TOY_GDI_BYTES) {candidate_close();return fail("Selected GDI changed");}
    UINT got=0;FRESULT result=f_read(&candidate,descriptor,TOY_GDI_BYTES,&got);candidate_close();
    if(result!=FR_OK || got!=TOY_GDI_BYTES) return fail("Selected GDI read");
    struct kui_sha256 hash;uint8_t digest[32];
    kui_sha256_init(&hash);kui_sha256_update(&hash,descriptor,TOY_GDI_BYTES);
    kui_sha256_digest(&hash,digest);
    if(memcmp(digest,toy_digest,sizeof(digest))) return fail("Selected GDI changed");
    memcpy(out,descriptor,TOY_GDI_BYTES);*bytes=TOY_GDI_BYTES;return 0;
}

static enum kui_game_result file_stat(void *context,const char *name,uint64_t *bytes) {
    (void)context;
    if(!found || !bytes || !safe_component(name,KUI_GAME_NAME_CAP)) return KUI_GAME_INVALID;
    if(check_cancel()) return KUI_GAME_CANCELLED;
    if(join(named_path,folder,name)) return KUI_GAME_RANGE;
    FRESULT result=f_stat(named_path,&info);
    if(result==FR_NO_FILE || result==FR_NO_PATH) {fail("Backing file missing");return KUI_GAME_NOT_FOUND;}
    if(result!=FR_OK) {fail("Backing stat failure");return KUI_GAME_IO;}
    if((info.fattrib&AM_DIR) || !info.fsize || info.fsize>UINT32_MAX) {
        fail("Backing file size");return KUI_GAME_FILE_SIZE;
    }
    *bytes=info.fsize;return KUI_GAME_OK;
}
static enum kui_game_result open_cached(const char *name) {
    if(!found || !safe_component(name,KUI_GAME_NAME_CAP)) return KUI_GAME_INVALID;
    if(cached_open && same(cached_name,name)) return KUI_GAME_OK;
    close_cached();
    if(join(named_path,folder,name)) return KUI_GAME_RANGE;
    if(f_open(&cached,named_path,FA_READ)!=FR_OK) {fail("Backing open failure");return KUI_GAME_IO;}
    cached_open=true;
    if(!f_size(&cached) || f_size(&cached)>UINT32_MAX) {
        close_cached();fail("Backing file size");return KUI_GAME_FILE_SIZE;
    }
    memcpy(cached_name,name,strlen(name)+1u);return KUI_GAME_OK;
}
static enum kui_game_result file_read(void *context,const char *name,uint64_t offset,void *out,size_t bytes) {
    (void)context;
    if((!out && bytes) || offset>UINT32_MAX || bytes>UINT_MAX) return KUI_GAME_INVALID;
    if(check_cancel()) return KUI_GAME_CANCELLED;
    enum kui_game_result opened=open_cached(name);if(opened!=KUI_GAME_OK) return opened;
    if(offset>f_size(&cached) || bytes>f_size(&cached)-offset) return KUI_GAME_RANGE;
    if(!bytes) return KUI_GAME_OK;
    FRESULT result=FR_OK;
    if(f_tell(&cached)!=(FSIZE_t)offset) result=f_lseek(&cached,(FSIZE_t)offset);
    if(result!=FR_OK || f_tell(&cached)!=(FSIZE_t)offset) {fail("Backing seek failure");return KUI_GAME_IO;}
    UINT got=0;result=f_read(&cached,out,(UINT)bytes,&got);
    if(result!=FR_OK || got!=bytes) {fail("Backing read failure");return KUI_GAME_IO;}
    return KUI_GAME_OK;
}
static const struct kui_game_file_ops file_ops={NULL,file_stat,file_read};
const struct kui_game_file_ops *cdda_preflight_storage_files(void) {return &file_ops;}

static void hash_word(struct kui_sha256 *hash,uint32_t value) {
    uint8_t bytes[4]={(uint8_t)value,(uint8_t)(value>>8),(uint8_t)(value>>16),(uint8_t)(value>>24)};
    kui_sha256_update(hash,bytes,sizeof(bytes));
}
int cdda_preflight_storage_extents(const char *name,struct cdda_preflight_storage_extents *out) {
    if(!out || check_cancel()) return fail("Extent arguments/cancellation");
    if(open_cached(name)!=KUI_GAME_OK) return -1;
    cached.cltbl=NULL;
    mapped_runs=0;
    uint64_t card_sectors=0;
    uint32_t partition_start=0,partition_sectors=0;
    if(cdda_storage_geometry(&card_sectors,&partition_start,&partition_sectors) ||
       !partition_sectors || partition_start>=card_sectors ||
       partition_sectors>card_sectors-partition_start)
        return fail("Backing volume/card geometry");
    FATFS *fs=cached.obj.fs;
    if(!fs || !fs->csize || (fs->csize&(fs->csize-1u)) || fs->n_fatent<3u)
        return fail("Invalid backing allocation geometry");
    struct cdda_preflight_storage_extents report;
    memset(&report,0,sizeof(report));
    report.file_bytes=(uint32_t)f_size(&cached);
    report.cluster_bytes=(uint32_t)fs->csize*512u;
    report.clusters=(report.file_bytes-1u)/report.cluster_bytes+1u;
    if(report.clusters>fs->n_fatent-2u) return fail("Backing allocation coverage range");
    report.rounded_bytes=(uint64_t)report.clusters*report.cluster_bytes;
    if(f_lseek(&cached,0)!=FR_OK || f_tell(&cached)) return fail("Allocation start seek");
    uint32_t previous=0,runs=0;
    for(uint32_t n=0;n<report.clusters;n++) {
        if(check_cancel()) goto failed;
        uint64_t end=(uint64_t)(n+1u)*report.cluster_bytes;
        if(end>report.file_bytes) end=report.file_bytes;
        if(f_lseek(&cached,(FSIZE_t)end)!=FR_OK || f_tell(&cached)!=(FSIZE_t)end) {
            fail("Backing allocation seek");goto failed;
        }
        uint32_t cluster=cached.clust;
        if(cluster<2u || cluster>=fs->n_fatent) {fail("Backing cluster range");goto failed;}
        /* Every previously covered interval is checked before extending or
         * appending. A corrupt cycle cannot turn into an unbounded FAT walk. */
        for(uint32_t r=0;r<runs;r++) if(cluster>=clmt[2u+2u*r] &&
            cluster-clmt[2u+2u*r]<clmt[1u+2u*r]) {
            fail("Repeated backing allocation cluster");goto failed;
        }
        if(n && cluster==previous+1u) clmt[1u+2u*(runs-1u)]++;
        else {
            if(runs==CDDA_PREFLIGHT_EXTENT_MAX) {fail("Backing extent map exceeds 160 runs");goto failed;}
            clmt[1u+2u*runs]=1u;clmt[2u+2u*runs]=cluster;runs++;
        }
        previous=cluster;
    }
    report.extents=runs;report.map_words=2u*runs+2u;
    clmt[0]=report.map_words;clmt[1u+2u*runs]=0;
    struct kui_sha256 hash;kui_sha256_init(&hash);
    hash_word(&hash,report.file_bytes);hash_word(&hash,512u);
    hash_word(&hash,fs->csize);hash_word(&hash,(uint32_t)fs->database);
    hash_word(&hash,report.clusters);hash_word(&hash,report.extents);
    uint32_t covered=0;
    for(uint32_t r=0;r<runs;r++) {
        uint32_t count=clmt[1u+2u*r],start=clmt[2u+2u*r];
        uint64_t first=(uint64_t)fs->database+(uint64_t)(start-2u)*fs->csize;
        uint64_t end=first+(uint64_t)count*fs->csize;
        if(start<2u || start>=fs->n_fatent || count>fs->n_fatent-start ||
           first>UINT32_MAX || end>UINT32_MAX || end>partition_sectors ||
           (uint64_t)partition_start+end>card_sectors ||
           count>report.clusters || covered>report.clusters-count) {
            fail("Backing extent coverage range");goto failed;
        }
        if(!r) report.first_volume_lba=(uint32_t)first;
        covered+=count;hash_word(&hash,count);hash_word(&hash,start);hash_word(&hash,(uint32_t)first);
    }
    if(covered!=report.clusters) {fail("Incomplete backing allocation map");goto failed;}
    kui_sha256_digest(&hash,report.sha256);
    cached.cltbl=clmt;
    if(f_lseek(&cached,0)!=FR_OK || f_tell(&cached)) {fail("Mapped backing rewind");goto failed;}
    mapped_runs=runs;mapped_csize=fs->csize;mapped_database=(uint32_t)fs->database;
    *out=report;return 0;
failed:
    cached.cltbl=NULL;mapped_runs=0;return -1;
}
int cdda_preflight_storage_extent_run(unsigned index,
    struct cdda_preflight_storage_extent_run *out) {
    if(!out || !cached_open || cached.cltbl!=clmt || index>=mapped_runs)
        return fail("Mapped extent index");
    uint32_t before=0;
    for(unsigned r=0;r<index;r++) before+=clmt[1u+2u*r];
    struct cdda_preflight_storage_extent_run run;
    run.first_cluster=clmt[2u+2u*index];run.clusters=clmt[1u+2u*index];
    run.first_volume_lba=mapped_database+(run.first_cluster-2u)*mapped_csize;
    run.file_sector=before*mapped_csize;run.sectors=run.clusters*mapped_csize;
    *out=run;return 0;
}
