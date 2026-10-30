/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/games_library.h"
#include "kui/games_covers.h"
#include "kui/games_group.h"
#include "kui/games_storage.h"
#include "kui/hash.h"
#include "kui/media.h"
#include "kui/storage.h"
#include "platform.h"
#include "diskio.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LIBRARY_FOLDER "0:/KUI/games-library"
#define LIBRARY_HEADER 96u
#define LIBRARY_ROWS 1024u
#define LIBRARY_MAX_BYTES (2u*1024u*1024u)
#define RECORD_HEADER 12u
#define EPOCH_BYTES 64u
struct library_group {char key[33],name[KUI_DEST_NAME_CAP];unsigned count,first,representative;bool valid;};
struct library_member {unsigned group,disc;};
struct library {
    uint8_t *wire;
    uint32_t *offsets;
    struct library_group *groups;
    struct library_member *members;
    uint32_t *display,*group_display;
    size_t bytes;
    unsigned count,group_count,display_count,slot;
    uint64_t sequence,epoch;
    uint8_t identity[32];
    char root[KUI_DEST_ROOT_CAP];
    bool resource_error;
};
static struct library saved;
static bool distrust_disk;
static bool stopped(kui_cancel_fn cancel) {return cancel && cancel();}
static uint16_t get16(const uint8_t *p) {return (uint16_t)(p[0]|(unsigned)p[1]<<8);}
static uint32_t get32(const uint8_t *p) {return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint64_t get64(const uint8_t *p) {return get32(p)|(uint64_t)get32(p+4)<<32;}
static void put16(uint8_t *p,unsigned n) {p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8);}
static void put32(uint8_t *p,uint32_t n) {for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(n>>(8*i));}
static void put64(uint8_t *p,uint64_t n) {put32(p,(uint32_t)n);put32(p+4,(uint32_t)(n>>32));}
static bool fold_equal(const char *a,const char *b) {
    while(*a && *b) {
        unsigned x=(unsigned char)*a++,y=(unsigned char)*b++;
        if(x>='A' && x<='Z')x+='a'-'A';
        if(y>='A' && y<='Z')y+='a'-'A';
        if(x!=y)return false;
    }
    return !*a && !*b;
}
static void library_free(struct library *lib) {
    free(lib->wire);free(lib->offsets);free(lib->groups);free(lib->members);free(lib->display);free(lib->group_display);memset(lib,0,sizeof(*lib));
}
void kui_games_library_clear(void) {library_free(&saved);}
bool kui_games_library_ready(const char *root) {return root && saved.wire && !strcmp(saved.root,root);}
static bool safe_path(const char *root,const char *path,bool directory) {
    size_t n=strlen(root);
    if(!path[0] || (strcmp(root,"/") && (strncmp(root,path,n) || path[n]!='/')))return false;
    if(directory) {char normalized[KUI_DEST_ROOT_CAP];return kui_destination_normalize(normalized,path) && !strcmp(normalized,path);}
    const char *leaf=strrchr(path,'/');char parent[KUI_DEST_ROOT_CAP],normalized[KUI_DEST_ROOT_CAP];
    if(!leaf || !leaf[1] || !kui_destination_name_valid(leaf+1) || (size_t)(leaf-path)>=sizeof(parent))return false;
    if(leaf==path)strcpy(parent,"/");else {memcpy(parent,path,(size_t)(leaf-path));parent[leaf-path]=0;}
    return kui_destination_normalize(normalized,parent) && !strcmp(parent,normalized);
}
/* Bind display data to the mounted filesystem's stable BPB identity. The
 * transport's mounted view is already partition-bounded by diskio.c. Avoid
 * exFAT's mutable volume flags/percent-in-use and FAT32's free-space hints.
 * This is a consistency fence, not detection of arbitrary host-side edits. */
static bool volume_identity(uint8_t out[32]) {
    const struct kui_volume *volume=kui_media_volume();uint8_t boot[512];
    if(!volume || !volume->count || disk_read(0,boot,0,1)!=RES_OK)return false;
    memset(out,0,32);put32(out,kui_storage_active());put32(out+4,volume->start);put32(out+8,volume->count);
    if(!memcmp(boot+3,"EXFAT   ",8)) {
        put32(out+12,2);memcpy(out+16,boot+100,4); /* serial */
        put32(out+20,kui_crc32(0,boot+64,36)); /* offsets/length/FAT/heap/root */
        memcpy(out+24,boot+108,4); /* sector/cluster shifts/FAT count/drive */
    } else if(!memcmp(boot+82,"FAT32   ",8)) {
        put32(out+12,1);memcpy(out+16,boot+67,4);
        put32(out+20,kui_crc32(0,boot+11,53)); /* BPB through FSInfo/backups */
    } else return false;
    put32(out+28,kui_crc32(0,out,28));return true;
}
static bool mkdirs(void) {
    FRESULT r=f_mkdir("0:/KUI");if(r!=FR_OK && r!=FR_EXIST)return false;
    r=f_mkdir(LIBRARY_FOLDER);return r==FR_OK || r==FR_EXIST;
}
static void epoch_path(char out[80],unsigned slot) {snprintf(out,80,LIBRARY_FOLDER "/generation-%c.bin",slot?'b':'a');}
static bool epoch_valid(const uint8_t wire[EPOCH_BYTES],const uint8_t identity[32],uint64_t *sequence) {
    if(memcmp(wire,"KUIGGEN1",8) || memcmp(wire+16,identity,32) || get32(wire+60)!=kui_crc32(0,wire,60))return false;
    for(unsigned i=48;i<60;i++)if(wire[i])return false;
    *sequence=get64(wire+8);return *sequence!=0;
}
/* Missing slots establish the initial generation. A damaged generation with
 * no surviving slot is an error, never permission to accept an older index. */
static bool epoch_load(const uint8_t identity[32],uint64_t *sequence,unsigned *slot,bool *corrupt) {
    *sequence=0;*slot=1;bool damaged=false;if(corrupt)*corrupt=false;
    for(unsigned i=0;i<2;i++) {
        char path[80];epoch_path(path,i);FIL file;FRESULT r=f_open(&file,path,FA_READ);
        if(r==FR_NO_FILE || r==FR_NO_PATH)continue;
        if(r!=FR_OK)return false;
        uint8_t wire[EPOCH_BYTES];UINT got=0;
        bool ok=f_size(&file)==sizeof(wire),io_error=false;
        if(ok) {ok=f_read(&file,wire,sizeof(wire),&got)==FR_OK && got==sizeof(wire);io_error=!ok;}
        if(f_close(&file)!=FR_OK)return false;
        if(io_error)return false;
        uint64_t seq=0;
        if(ok && epoch_valid(wire,identity,&seq)) {if(seq>*sequence) {*sequence=seq;*slot=i;}}
        else if(ok && memcmp(wire+16,identity,32)) { /* another filesystem's generation is irrelevant */ }
        else damaged=true;
    }
    if(corrupt)*corrupt=damaged && !*sequence;
    return !damaged || *sequence!=0;
}
static bool write_checked(const char *path,const uint8_t *wire,size_t bytes,kui_cancel_fn cancel) {
    if(stopped(cancel) || bytes>UINT_MAX)return false;
    FIL file;if(f_open(&file,path,FA_WRITE|FA_CREATE_ALWAYS)!=FR_OK)return false;
    bool ok=true;size_t at=0;
    while(ok && at<bytes) {
        if(stopped(cancel)) {ok=false;break;}
        UINT count=(UINT)(bytes-at>4096?4096:bytes-at),done=0;
        ok=f_write(&file,wire+at,count,&done)==FR_OK && done==count;at+=done;
    }
    if(f_sync(&file)!=FR_OK)ok=false;
    if(f_close(&file)!=FR_OK)ok=false;
    if(!ok || stopped(cancel))return false;
    if(f_open(&file,path,FA_READ)!=FR_OK)return false;
    ok=f_size(&file)==bytes;at=0;uint8_t buffer[512];
    while(ok && at<bytes) {
        if(stopped(cancel)) {ok=false;break;}
        UINT count=(UINT)(bytes-at>sizeof(buffer)?sizeof(buffer):bytes-at),done=0;
        ok=f_read(&file,buffer,count,&done)==FR_OK && done==count && !memcmp(buffer,wire+at,count);at+=done;
    }
    if(f_close(&file)!=FR_OK)ok=false;
    return ok && !stopped(cancel);
}
bool kui_games_library_invalidate(const char *root,kui_log_fn log) {
    (void)root;
    distrust_disk=true;kui_games_library_clear();kui_games_cache_clear();
    if(kui_games_storage_open(log)!=KUI_GAMES_STORAGE_OK) {
        if(log)log("Games library: cannot invalidate saved catalogue before changing game files");
        return false;
    }
    uint8_t identity[32],wire[EPOCH_BYTES]={0};uint64_t sequence=0;unsigned slot=0;
    bool ok=volume_identity(identity) && epoch_load(identity,&sequence,&slot,NULL) && sequence!=UINT64_MAX && mkdirs();
    if(ok) {
        memcpy(wire,"KUIGGEN1",8);put64(wire+8,sequence+1);memcpy(wire+16,identity,32);
        put32(wire+60,kui_crc32(0,wire,60));char path[80];epoch_path(path,slot^1u);
        ok=write_checked(path,wire,sizeof(wire),NULL);
    }
    if(!kui_games_storage_done(ok))ok=false;
    distrust_disk=!ok;
    if(!ok && log)log("Games library: cannot invalidate saved catalogue before changing game files");
    return ok;
}
static void library_path(char out[96],const char *root,unsigned slot) {
    uint32_t hash=kui_crc32(0,root,strlen(root));snprintf(out,96,LIBRARY_FOLDER "/%08lx-%c.bin",(unsigned long)hash,slot?'b':'a');
}
static bool index_filename(const char *name) {
    if(strlen(name)!=14 || name[8]!='-' || (name[9]!='a' && name[9]!='b' && name[9]!='A' && name[9]!='B') || !fold_equal(name+10,".bin"))return false;
    for(unsigned i=0;i<8;i++)if(!((name[i]>='0' && name[i]<='9') || (name[i]>='a' && name[i]<='f') || (name[i]>='A' && name[i]<='F')))return false;
    return true;
}
/* A damaged generation ledger can be repaired only after every generated
 * root index has been removed. Deletion is restricted to our exact filenames;
 * unknown user files stay untouched. A failed reset leaves the damaged ledger
 * in place, so old indices remain inadmissible. */
static bool epoch_recover(kui_cancel_fn cancel) {
    for(unsigned removed=0;removed<=4096;removed++) {
        if(stopped(cancel))return false;
        DIR dir;FRESULT r=f_opendir(&dir,LIBRARY_FOLDER);if(r!=FR_OK)return false;
        bool ok=true,found=false;char name[32]={0};
        for(unsigned scanned=0;scanned<32768;scanned++) {
            FILINFO info;r=f_readdir(&dir,&info);
            if(r!=FR_OK) {ok=false;break;}
            if(!info.fname[0])break;
            if(!(info.fattrib&AM_DIR) && index_filename(info.fname)) {strcpy(name,info.fname);found=true;break;}
            if(scanned==32767)ok=false;
        }
        if(f_closedir(&dir)!=FR_OK)ok=false;
        if(!ok)return false;
        if(!found) {
            for(unsigned i=0;i<2;i++) {char path[80];epoch_path(path,i);r=f_unlink(path);if(r!=FR_OK && r!=FR_NO_FILE && r!=FR_NO_PATH)return false;}
            return true;
        }
        if(removed==4096)return false;
        char path[96];snprintf(path,sizeof(path),LIBRARY_FOLDER "/%s",name);if(f_unlink(path)!=FR_OK)return false;
    }
    return false;
}
static bool decode_record(const struct library *lib,unsigned index,struct kui_games_entry *entry) {
    if(index>=lib->count)return false;
    size_t at=lib->offsets[index];if(at>lib->bytes || lib->bytes-at<RECORD_HEADER)return false;
    const uint8_t *p=lib->wire+at;unsigned lengths[4];size_t total=RECORD_HEADER;
    for(unsigned i=0;i<4;i++) {lengths[i]=get16(p+2*i);total+=lengths[i];}
    if(total>lib->bytes-at || (p[8]&~3u) || p[9] || p[10] || p[11])return false;
    memset(entry,0,sizeof(*entry));char *strings[]={entry->name,entry->path,entry->variant_2048_path,entry->title};
    const size_t caps[]={sizeof(entry->name),sizeof(entry->path),sizeof(entry->variant_2048_path),sizeof(entry->title)};
    p+=RECORD_HEADER;
    for(unsigned i=0;i<4;i++) {
        if(lengths[i]>=caps[i] || memchr(p,0,lengths[i]))return false;
        memcpy(strings[i],p,lengths[i]);strings[i][lengths[i]]=0;p+=lengths[i];
    }
    entry->directory=(lib->wire[at+8]&1u)!=0;entry->disabled=(lib->wire[at+8]&2u)!=0;
    if(!kui_destination_name_valid(entry->name))return false;
    if(!entry->disabled && !safe_path(lib->root,entry->path,entry->directory))return false;
    if(entry->disabled && entry->path[0] && !safe_path(lib->root,entry->path,entry->directory))return false;
    if(entry->variant_2048_path[0] && (entry->directory || entry->disabled ||
        !safe_path(lib->root,entry->variant_2048_path,false) || !strcmp(entry->path,entry->variant_2048_path)))return false;
    for(const unsigned char *c=(const unsigned char *)entry->title;*c;c++)if(*c<32 || *c==127)return false;
    return true;
}
static bool library_validate(struct library *lib,const char *root,const uint8_t identity[32],uint64_t epoch) {
    const uint8_t *p=lib->wire;
    if(lib->bytes<LIBRARY_HEADER || memcmp(p,"KUIGLIB2",8) || get32(p+8)!=2 || get32(p+12)!=lib->bytes ||
       !get64(p+16) || get64(p+24)!=epoch || memcmp(p+32,identity,32) ||
       get32(p+68)!=kui_crc32(0,p+LIBRARY_HEADER,lib->bytes-LIBRARY_HEADER) || get32(p+72)!=kui_crc32(0,p,72))return false;
    for(unsigned i=76;i<LIBRARY_HEADER;i++)if(p[i])return false;
    unsigned root_bytes=get16(p+64);lib->count=get16(p+66);
    if(root_bytes!=strlen(root) || root_bytes>=sizeof(lib->root) || lib->count>LIBRARY_ROWS ||
       root_bytes>lib->bytes-LIBRARY_HEADER || memcmp(p+LIBRARY_HEADER,root,root_bytes))return false;
    if(lib->root!=root)strcpy(lib->root,root);
    memmove(lib->identity,identity,32);lib->epoch=epoch;lib->sequence=get64(p+16);
    lib->offsets=calloc(lib->count?lib->count:1,sizeof(*lib->offsets));
    if(!lib->offsets) {lib->resource_error=true;return false;}
    size_t at=LIBRARY_HEADER+root_bytes;
    for(unsigned i=0;i<lib->count;i++) {
        if(lib->bytes-at<RECORD_HEADER)return false;
        lib->offsets[i]=(uint32_t)at;struct kui_games_entry entry;
        if(!decode_record(lib,i,&entry))return false;
        for(unsigned j=0;j<4;j++)at+=get16(p+lib->offsets[i]+2*j);
        at+=RECORD_HEADER;
    }
    return at==lib->bytes;
}
static const struct library *sorting;
static int display_order(const void *left,const void *right) {
    unsigned a=*(const uint32_t *)left,b=*(const uint32_t *)right;
    unsigned ag=sorting->members[a].group,bg=sorting->members[b].group;
    struct kui_games_entry ae,be;
    if(!decode_record(sorting,a,&ae) || !decode_record(sorting,b,&be))return 0;
    const char *an=ag && sorting->groups[ag-1].valid?sorting->groups[ag-1].name:ae.name;
    const char *bn=bg && sorting->groups[bg-1].valid?sorting->groups[bg-1].name:be.name;
    const unsigned char *x=(const unsigned char *)an,*y=(const unsigned char *)bn;
    while(*x && *y) {unsigned p=*x++,q=*y++;if(p>='A'&&p<='Z')p+='a'-'A';if(q>='A'&&q<='Z')q+='a'-'A';if(p!=q)return p<q?-1:1;}
    if(*x!=*y)return *x?1:-1;
    int cmp=strcmp(an,bn);return cmp?cmp:a<b?-1:a>b?1:0;
}
static int member_order(const void *left,const void *right) {
    unsigned a=*(const uint32_t *)left,b=*(const uint32_t *)right;
    const struct library_member *am=&sorting->members[a],*bm=&sorting->members[b];
    if(am->group!=bm->group)return am->group<bm->group?-1:1;
    return am->disc<bm->disc?-1:am->disc>bm->disc?1:0;
}
static bool library_groups(struct library *lib) {
    lib->groups=calloc(lib->count?lib->count:1,sizeof(*lib->groups));
    lib->members=calloc(lib->count?lib->count:1,sizeof(*lib->members));
    if(!lib->groups || !lib->members)return false;
    for(unsigned i=0;i<lib->count;i++) {
        struct kui_games_entry entry;if(!decode_record(lib,i,&entry))return false;
        char base[KUI_DEST_NAME_CAP],key[33];unsigned disc=0;
        if(entry.directory || entry.disabled || !kui_games_group_parse(entry.name,base,&disc) || !kui_games_group_key(base,key))continue;
        unsigned g=0;for(;g<lib->group_count;g++)if(fold_equal(base,lib->groups[g].name))break;
        if(g==lib->group_count) {
            strcpy(lib->groups[g].key,key);strcpy(lib->groups[g].name,base);lib->groups[g].representative=i;lib->groups[g].valid=true;++lib->group_count;
            for(unsigned j=0;j<g;j++)if(!strcmp(lib->groups[j].key,key))lib->groups[g].valid=lib->groups[j].valid=false;
        }
        for(unsigned j=0;j<i;j++)if(lib->members[j].group==g+1 && lib->members[j].disc==disc)lib->groups[g].valid=false;
        lib->members[i]=(struct library_member){g+1,disc};++lib->groups[g].count;
    }
    for(unsigned i=0;i<lib->group_count;i++)if(lib->groups[i].count<2)lib->groups[i].valid=false;
    lib->display=calloc(lib->count?lib->count:1,sizeof(*lib->display));
    lib->group_display=calloc(lib->count?lib->count:1,sizeof(*lib->group_display));
    if(!lib->display || !lib->group_display)return false;
    unsigned member_count=0;
    for(unsigned i=0;i<lib->count;i++) {
        unsigned g=lib->members[i].group;
        if(g && lib->groups[g-1].valid) {
            lib->group_display[member_count++]=i;
            if(lib->groups[g-1].representative!=i)continue;
        }
        lib->display[lib->display_count++]=i;
    }
    sorting=lib;qsort(lib->display,lib->display_count,sizeof(*lib->display),display_order);
    qsort(lib->group_display,member_count,sizeof(*lib->group_display),member_order);sorting=NULL;
    for(unsigned i=0;i<member_count;i++) {
        unsigned g=lib->members[lib->group_display[i]].group;
        if(!i || lib->members[lib->group_display[i-1]].group!=g)lib->groups[g-1].first=i;
    }
    return true;
}
/* A damaged newer slot cannot hide a surviving complete generation. I/O
 * errors are distinguished from malformed files to avoid publishing a fresh
 * scan over an uncertain filesystem. */
enum load_result {LOAD_MISSING,LOAD_OK,LOAD_ERROR};
static enum load_result library_load(struct library *out,const char *root,const uint8_t identity[32],uint64_t epoch,kui_cancel_fn cancel) {
    enum load_result result=LOAD_MISSING;
    for(unsigned slot=0;slot<2;slot++) {
        if(stopped(cancel)) {library_free(out);return LOAD_ERROR;}
        char path[96];library_path(path,root,slot);FIL file;FRESULT r=f_open(&file,path,FA_READ);
        if(r==FR_NO_FILE || r==FR_NO_PATH)continue;
        if(r!=FR_OK) {library_free(out);return LOAD_ERROR;}
        struct library candidate={0};FSIZE_t bytes=f_size(&file);
        bool ok=bytes>=LIBRARY_HEADER && bytes<=LIBRARY_MAX_BYTES;
        bool memory_error=false;
        if(ok) {candidate.bytes=(size_t)bytes;candidate.wire=malloc(candidate.bytes);ok=candidate.wire!=NULL;memory_error=!ok;}
        bool io_error=false;
        for(size_t at=0;ok && at<candidate.bytes;) {
            if(stopped(cancel)) {ok=false;break;}
            UINT got=0,count=(UINT)(candidate.bytes-at>4096?4096:candidate.bytes-at);
            ok=f_read(&file,candidate.wire+at,count,&got)==FR_OK && got==count;
            io_error=!ok;at+=got;
        }
        if(f_close(&file)!=FR_OK) {ok=false;io_error=true;}
        if(stopped(cancel)) {library_free(&candidate);library_free(out);return LOAD_ERROR;}
        if(io_error || memory_error) {library_free(&candidate);library_free(out);return LOAD_ERROR;}
        if(!ok) {library_free(&candidate);continue;}
        if(library_validate(&candidate,root,identity,epoch) &&
           (!out->wire || candidate.sequence>out->sequence)) {
            library_free(out);*out=candidate;memset(&candidate,0,sizeof(candidate));out->slot=slot;result=LOAD_OK;
        }
        bool resource_error=candidate.resource_error;library_free(&candidate);
        if(resource_error) {library_free(out);return LOAD_ERROR;}
    }
    if(result==LOAD_OK && !library_groups(out)) {library_free(out);return LOAD_ERROR;}
    return result;
}
static bool append_record(struct library *lib,const struct kui_games_entry *entry) {
    const char *strings[]={entry->name,entry->path,entry->variant_2048_path,entry->title};size_t lengths[4],bytes=RECORD_HEADER;
    for(unsigned i=0;i<4;i++) {lengths[i]=strlen(strings[i]);bytes+=lengths[i];}
    if(bytes>LIBRARY_MAX_BYTES-lib->bytes)return false;
    uint8_t *grown=realloc(lib->wire,lib->bytes+bytes);if(!grown)return false;lib->wire=grown;
    uint8_t *p=grown+lib->bytes;memset(p,0,RECORD_HEADER);
    for(unsigned i=0;i<4;i++)put16(p+2*i,(unsigned)lengths[i]);
    p[8]=(uint8_t)((entry->directory?1u:0u)|(entry->disabled?2u:0u));p+=RECORD_HEADER;
    for(unsigned i=0;i<4;i++) {memcpy(p,strings[i],lengths[i]);p+=lengths[i];}
    lib->bytes+=bytes;++lib->count;return true;
}
static bool build_catalogue(struct library *out,const char *root,const uint8_t identity[32],uint64_t epoch,kui_log_fn log,kui_cancel_fn cancel) {
    memset(out,0,sizeof(*out));strcpy(out->root,root);memcpy(out->identity,identity,32);out->epoch=epoch;
    out->bytes=LIBRARY_HEADER+strlen(root);out->wire=calloc(1,out->bytes);if(!out->wire)return false;
    memcpy(out->wire+LIBRARY_HEADER,root,strlen(root));
    for(unsigned offset=0;;offset+=KUI_GAMES_ROWS) {
        struct kui_games_page page;
        if(stopped(cancel) || !kui_games_list_rows(root,offset,KUI_GAMES_VIEW_SAVED,&page,log,cancel) ||
           page.total>LIBRARY_ROWS || (page.has_more && !page.count))return false;
        (void)kui_games_prefetch_titles(&page,log,cancel);
        if(stopped(cancel) || !kui_games_list_rows(root,offset,KUI_GAMES_VIEW_SAVED,&page,log,cancel))return false;
        for(unsigned i=0;i<page.count;i++) {
            if(out->count==LIBRARY_ROWS || !append_record(out,&page.entries[i]))return false;
        }
        if(!page.has_more)break;
    }
    return !stopped(cancel) && kui_games_cache_complete(root);
}
static bool prepare_catalogue(struct library *draft,uint64_t sequence,unsigned slot) {
    uint8_t *p=draft->wire;memcpy(p,"KUIGLIB2",8);put32(p+8,2);put32(p+12,(uint32_t)draft->bytes);
    put64(p+16,sequence);put64(p+24,draft->epoch);memcpy(p+32,draft->identity,32);
    put16(p+64,(unsigned)strlen(draft->root));put16(p+66,draft->count);
    put32(p+68,kui_crc32(0,p+LIBRARY_HEADER,draft->bytes-LIBRARY_HEADER));put32(p+72,kui_crc32(0,p,72));
    if(!library_validate(draft,draft->root,draft->identity,draft->epoch) || !library_groups(draft))return false;
    draft->sequence=sequence;draft->slot=slot;return true;
}
static bool publish_catalogue(struct library *draft,kui_cancel_fn cancel) {
    if(!mkdirs())return false;
    char path[96];library_path(path,draft->root,draft->slot);
    uint8_t header[LIBRARY_HEADER];memcpy(header,draft->wire,sizeof(header));
    memcpy(draft->wire,"KUIGTMP2",8);put32(draft->wire+72,kui_crc32(0,draft->wire,72));
    bool ok=write_checked(path,draft->wire,draft->bytes,cancel);
    memcpy(draft->wire,header,sizeof(header));
    if(!ok || stopped(cancel))return false;
    /* Commit only after the whole pending file was synced and read back.
     * This tiny final header transaction is not cancellable: a stop arriving
     * here observes a complete committed generation and success. */
    FIL file;if(f_open(&file,path,FA_WRITE)!=FR_OK)return false;UINT done=0;
    ok=f_write(&file,header,sizeof(header),&done)==FR_OK && done==sizeof(header);
    if(f_sync(&file)!=FR_OK)ok=false;
    if(f_close(&file)!=FR_OK)ok=false;
    if(!ok || f_open(&file,path,FA_READ)!=FR_OK)return false;
    uint8_t check[LIBRARY_HEADER];done=0;ok=f_size(&file)==draft->bytes &&
        f_read(&file,check,sizeof(check),&done)==FR_OK && done==sizeof(check) && !memcmp(header,check,sizeof(header));
    if(f_close(&file)!=FR_OK)ok=false;
    return ok;
}
static bool scan_mounted(const char *root,bool rebuild,bool allow_unsaved,const uint8_t identity[32],uint64_t epoch,kui_log_fn log,kui_cancel_fn cancel) {
    struct library previous={0},draft={0};
    if(saved.wire && !strcmp(saved.root,root) && !memcmp(saved.identity,identity,32) && saved.epoch==epoch) {
        /* Retain the complete RAM generation until the replacement is saved. */
        previous.sequence=saved.sequence;previous.slot=saved.slot;
    } else {
        enum load_result loaded=library_load(&previous,root,identity,epoch,cancel);
        if(loaded==LOAD_ERROR)return false;
    }
    (void)rebuild;
    /* Either explicit scan discovers host edits, even in a warm session.
     * Saved cover records remain on card; only stale RAM labels are dropped. */
    kui_games_cache_clear();kui_games_covers_cache_clear();
    bool built=previous.sequence!=UINT64_MAX && build_catalogue(&draft,root,identity,epoch,log,cancel);
    uint8_t current_identity[32];uint64_t current_epoch=0;unsigned current_slot=0;
    bool unchanged=built && kui_games_storage_open(log)==KUI_GAMES_STORAGE_OK &&
        volume_identity(current_identity) && !memcmp(identity,current_identity,32) &&
        epoch_load(identity,&current_epoch,&current_slot,NULL) && current_epoch==epoch;
    bool prepared=unchanged && prepare_catalogue(&draft,previous.sequence+1,previous.sequence?previous.slot^1u:0u);
    bool persisted=prepared && publish_catalogue(&draft,cancel);
    bool ok=persisted || (allow_unsaved && prepared && !stopped(cancel));
    if(!persisted && prepared) (void)kui_games_storage_release();
    library_free(&previous);
    if(ok) {library_free(&saved);saved=draft;memset(&draft,0,sizeof(draft));kui_games_cache_clear();if(log)log("Games library: %s %u entries (%lu bytes)",persisted?"saved":"using RAM only; could not save",saved.count,(unsigned long)saved.bytes);}
    library_free(&draft);return ok;
}
bool kui_games_library_scan(const char *root,bool rebuild,kui_log_fn log,kui_cancel_fn cancel) {
    char normalized[KUI_DEST_ROOT_CAP];
    if(!root || !kui_destination_normalize(normalized,root) || strcmp(root,normalized) || stopped(cancel))return false;
    if(kui_games_storage_open(log)!=KUI_GAMES_STORAGE_OK)return false;
    uint8_t identity[32];uint64_t epoch=0;unsigned slot=0;bool corrupt=false;
    bool ok=volume_identity(identity);
    if(ok && !epoch_load(identity,&epoch,&slot,&corrupt)) {
        ok=corrupt && rebuild && epoch_recover(cancel);epoch=0;slot=1;
    }
    if(ok && distrust_disk) {
        uint8_t wire[EPOCH_BYTES]={0};ok=epoch!=UINT64_MAX && mkdirs() && !stopped(cancel);
        if(ok) {memcpy(wire,"KUIGGEN1",8);put64(wire+8,epoch+1);memcpy(wire+16,identity,32);put32(wire+60,kui_crc32(0,wire,60));char path[80];epoch_path(path,slot^1u);ok=write_checked(path,wire,sizeof(wire),cancel);if(ok)++epoch;}
    }
    if(ok)ok=scan_mounted(root,rebuild,false,identity,epoch,log,cancel);
    if(ok)distrust_disk=false;
    if(!kui_games_storage_done(ok || stopped(cancel)))ok=false;
    if(!ok && log)log("Games library: %s; previous complete catalogue retained",stopped(cancel)?"scan stopped":"scan failed");
    return ok;
}
static bool use_live_catalogue(const char *root,const uint8_t identity[32],const char *reason,kui_log_fn log,kui_cancel_fn cancel) {
    struct library live={0};kui_games_cache_clear();kui_games_covers_cache_clear();
    bool ok=build_catalogue(&live,root,identity,0,log,cancel) && prepare_catalogue(&live,1,0);
    if(ok) {
        library_free(&saved);saved=live;memset(&live,0,sizeof(live));kui_games_cache_clear();
        if(log)log("Games library: using fresh RAM list; %s",reason);
    }
    library_free(&live);return ok;
}
static bool ensure_library(const char *root,kui_log_fn log,kui_cancel_fn cancel) {
    if(saved.wire && !strcmp(saved.root,root))return !stopped(cancel);
    if(kui_games_storage_open(log)!=KUI_GAMES_STORAGE_OK)return false;
    uint8_t identity[32];uint64_t epoch=0;unsigned slot=0;bool corrupt=false;
    bool identity_ok=volume_identity(identity);
    if(identity_ok && distrust_disk) {
        bool ok=use_live_catalogue(root,identity,"saved catalogue invalidation could not be confirmed",log,cancel);
        if(!kui_games_storage_done(ok || stopped(cancel)))ok=false;
        return ok && !stopped(cancel);
    }
    bool ok=identity_ok && epoch_load(identity,&epoch,&slot,&corrupt);
    if(identity_ok && !ok && corrupt && !stopped(cancel)) {
        ok=use_live_catalogue(root,identity,"rebuild repairs damaged saved catalogue",log,cancel);
        if(!kui_games_storage_done(ok || stopped(cancel)))ok=false;
        return ok && !stopped(cancel);
    }
    if(ok) {
        struct library loaded={0};enum load_result result=library_load(&loaded,root,identity,epoch,cancel);
        if(result==LOAD_OK) {library_free(&saved);saved=loaded;memset(&loaded,0,sizeof(loaded));kui_games_cache_clear();}
        else if(result==LOAD_MISSING && !stopped(cancel))ok=scan_mounted(root,false,true,identity,epoch,log,cancel);
        else ok=false;
        library_free(&loaded);
    }
    if(!kui_games_storage_done(ok || stopped(cancel)))ok=false;
    return ok && !stopped(cancel);
}
static unsigned active_group(const struct library *lib,unsigned index) {
    unsigned group=lib->members[index].group;
    return group && lib->groups[group-1].valid?group:0;
}
static bool fill_page(const char *group,unsigned offset,struct kui_games_page *out,kui_cancel_fn cancel) {
    unsigned requested_group=0;
    if(group && group[0]) {
        for(unsigned i=0;i<saved.group_count;i++)if(saved.groups[i].valid && !strcmp(saved.groups[i].key,group)) {requested_group=i+1;break;}
        if(!requested_group) {snprintf(out->message,sizeof(out->message),"Disc group changed; return to Games and scan again.");return false;}
        strcpy(out->group,group);strcpy(out->group_name,saved.groups[requested_group-1].name);
    }
    unsigned total=requested_group?saved.groups[requested_group-1].count:saved.display_count;
    /* Group members are sorted once when the catalogue is admitted. */
    for(unsigned at=offset;at<total && at-offset<KUI_GAMES_ROWS;at++) {
        if(stopped(cancel))return false;
        unsigned index=requested_group?saved.group_display[saved.groups[requested_group-1].first+at]:saved.display[at];
        unsigned g=active_group(&saved,index);
        struct kui_games_entry *entry=&out->entries[out->count++];if(!decode_record(&saved,index,entry))return false;
        if(requested_group) {
            entry->disc=saved.members[index].disc;
            snprintf(entry->title,sizeof(entry->title),"Disc %u",entry->disc);
        } else if(g) {
            strcpy(entry->group,saved.groups[g-1].key);
            strcpy(entry->title,saved.groups[g-1].name);entry->variant_2048_path[0]=0;entry->disc=saved.groups[g-1].count;
        }
    }
    out->total=total;out->has_more=offset<total && total-offset>out->count;
    snprintf(out->message,sizeof(out->message),"%s",requested_group?"Choose a disc.":"Select a game. X scans for changes.");return true;
}
bool kui_games_library_rows(const char *root,const char *group,unsigned offset,unsigned view,
    struct kui_games_page *out,kui_log_fn log,kui_cancel_fn cancel) {
    if(!out)return false;
    memset(out,0,sizeof(*out));
    if(!root || !kui_destination_normalize(out->root,root) || strcmp(root,out->root) || offset>LIBRARY_ROWS ||
       (group && strlen(group)>32))return false;
    if(group)strcpy(out->group,group);
    if(stopped(cancel)) {snprintf(out->message,sizeof(out->message),"Games library stopped.");return false;}
    if(!ensure_library(root,log,cancel)) {snprintf(out->message,sizeof(out->message),"Cannot load Games library; scan or check storage.");return false;}
    out->view=kui_games_library_view(view,log,cancel);out->artwork=true;
    return out->view<KUI_GAMES_VIEW_COUNT && fill_page(group,offset,out,cancel);
}
bool kui_games_library_find(const char *root,const char *group,const char *name,unsigned *offset,kui_log_fn log,kui_cancel_fn cancel) {
    char normalized[KUI_DEST_ROOT_CAP];
    if(!root || !name || !offset || !kui_destination_normalize(normalized,root) || strcmp(root,normalized) ||
       (group && strlen(group)>32) || !kui_destination_name_valid(name) || !ensure_library(root,log,cancel))return false;
    for(unsigned at=0;at<=LIBRARY_ROWS;at+=KUI_GAMES_ROWS) {
        struct kui_games_page page={0};if(!fill_page(group,at,&page,cancel))return false;
        for(unsigned i=0;i<page.count;i++) {
            const struct kui_games_entry *entry=&page.entries[i];bool match=fold_equal(entry->name,name);
            if(!match && (!group || !group[0]) && entry->group[0]) {
                for(unsigned j=0;j<saved.count;j++)if(active_group(&saved,j)) {
                    unsigned g=active_group(&saved,j);struct kui_games_entry member;
                    if(!strcmp(saved.groups[g-1].key,entry->group) && decode_record(&saved,j,&member) && fold_equal(member.name,name)) {match=true;break;}
                }
            }
            if(match) {*offset=at+i;return true;}
        }
        if(!page.has_more)break;
    }
    return false;
}
