/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/games_scan_log.h"
#include "ff.h"
#include <stdlib.h>
#include <string.h>

#define PREFIX "KUI games scan log v1\nSequence: "
#define MIDDLE "\nBytes: "
#define TRAILER "\nCRC32: "
#define SEQUENCE_AT (sizeof(PREFIX)-1u)
#define LENGTH_AT (SEQUENCE_AT+16u+sizeof(MIDDLE)-1u)
#define HEADER_BYTES (LENGTH_AT+4u+2u)
#define TRAILER_BYTES (sizeof(TRAILER)-1u+8u+1u)
_Static_assert(HEADER_BYTES+KUI_GAMES_SCAN_LOG_TEXT_MAX+TRAILER_BYTES<=KUI_GAMES_SCAN_LOG_WIRE_MAX,
    "scan log bounds");

static const char *const paths[]={"0:/KUI/games-library/scan-a.txt","0:/KUI/games-library/scan-b.txt"};
struct slot {bool exists,valid;uint64_t sequence;size_t bytes,text;};
struct workspace {
    FIL file;
    FILINFO info;
    uint8_t wire[KUI_GAMES_SCAN_LOG_WIRE_MAX],check[KUI_GAMES_SCAN_LOG_WIRE_MAX],best[KUI_GAMES_SCAN_LOG_WIRE_MAX];
    struct slot slots[2];
    uint64_t sequence;
    int current;
    size_t bytes,text;
};
static bool text_valid(const char *text,size_t bytes) {
    if(bytes>KUI_GAMES_SCAN_LOG_TEXT_MAX || (!text && bytes))return false;
    for(size_t i=0;i<bytes;i++) {
        unsigned char c=(unsigned char)text[i];
        if((c<32u && c!='\t' && c!='\r' && c!='\n') || c==127u)return false;
    }
    return true;
}
static bool read_hex(const uint8_t *input,unsigned digits,uint64_t *out) {
    uint64_t value=0;
    for(unsigned i=0;i<digits;i++) {
        unsigned c=input[i],digit;
        if(c>='0' && c<='9')digit=c-'0';
        else if(c>='a' && c<='f')digit=c-'a'+10u;
        else return false;
        value=(value<<4)|digit;
    }
    *out=value;return true;
}
static void write_hex(uint8_t *out,uint64_t value,unsigned digits) {
    static const char hex[]="0123456789abcdef";
    for(unsigned i=0;i<digits;i++)out[i]=(uint8_t)hex[(value>>((digits-1u-i)*4u))&15u];
}
static bool decode(const uint8_t *wire,size_t bytes,struct slot *slot) {
    if(bytes<HEADER_BYTES+TRAILER_BYTES || bytes>KUI_GAMES_SCAN_LOG_WIRE_MAX ||
       memcmp(wire,PREFIX,SEQUENCE_AT) || memcmp(wire+SEQUENCE_AT+16u,MIDDLE,sizeof(MIDDLE)-1u) ||
       memcmp(wire+LENGTH_AT+4u,"\n\n",2u))return false;
    uint64_t sequence=0,crc=0;
    if(!read_hex(wire+SEQUENCE_AT,16u,&sequence) || !sequence)return false;
    size_t text=0;
    for(unsigned i=0;i<4u;i++) {
        unsigned c=wire[LENGTH_AT+i];if(c<'0' || c>'9')return false;
        text=text*10u+c-'0';
    }
    if(text>KUI_GAMES_SCAN_LOG_TEXT_MAX || bytes!=HEADER_BYTES+text+TRAILER_BYTES ||
       !text_valid((const char *)wire+HEADER_BYTES,text) ||
       memcmp(wire+HEADER_BYTES+text,TRAILER,sizeof(TRAILER)-1u) || wire[bytes-1u]!='\n' ||
       !read_hex(wire+HEADER_BYTES+text+sizeof(TRAILER)-1u,8u,&crc) ||
       crc!=kui_crc32(0,wire,HEADER_BYTES+text))return false;
    slot->sequence=sequence;slot->bytes=bytes;slot->text=text;return true;
}
static bool read_slot(struct workspace *work,unsigned which) {
    struct slot *slot=&work->slots[which];
    FRESULT result=f_open(&work->file,paths[which],FA_READ);
    if(result==FR_NO_FILE || result==FR_NO_PATH)return true;
    if(result!=FR_OK)return false;
    slot->exists=true;FSIZE_t size=f_size(&work->file);UINT got=0;
    bool sized=size>=HEADER_BYTES+TRAILER_BYTES && size<=KUI_GAMES_SCAN_LOG_WIRE_MAX;
    bool ok=!sized || (f_read(&work->file,work->check,(UINT)size,&got)==FR_OK && got==size);
    if(f_close(&work->file)!=FR_OK)ok=false;
    if(!ok)return false;
    slot->valid=sized && decode(work->check,(size_t)size,slot);
    if(!slot->valid)return true;
    if(work->current>=0 && slot->sequence==work->sequence &&
       (slot->bytes!=work->bytes || memcmp(work->best,work->check,slot->bytes)))return false;
    if(work->current<0 || slot->sequence>work->sequence) {
        work->current=(int)which;work->sequence=slot->sequence;work->bytes=slot->bytes;work->text=slot->text;
        memcpy(work->best,work->check,slot->bytes);
    }
    return true;
}
static bool latest(struct workspace *work,bool recover_invalid) {
    work->current=-1;
    return read_slot(work,0u) && read_slot(work,1u) &&
        (recover_invalid || work->current>=0 || (!work->slots[0].exists && !work->slots[1].exists));
}
bool kui_games_scan_log_load(char *out,size_t cap,size_t *bytes,kui_log_fn log) {
    if(!out || !cap || !bytes)return false;
    struct workspace *work=calloc(1,sizeof(*work));if(!work)return false;
    bool ok=latest(work,false) && work->text<cap;
    if(ok) {
        if(work->text)memcpy(out,work->best+HEADER_BYTES,work->text);
        out[work->text]=0;*bytes=work->text;
    } else if(log)log("Games scan log unavailable; existing records retained");
    free(work);return ok;
}
static bool parents(struct workspace *work) {
    static const char *const folders[]={"0:/KUI","0:/KUI/games-library"};
    for(unsigned i=0;i<2u;i++) {
        FRESULT result=f_stat(folders[i],&work->info);
        if(result==FR_OK && (work->info.fattrib&AM_DIR))continue;
        if(result!=FR_NO_FILE && result!=FR_NO_PATH)return false;
        if(f_mkdir(folders[i])!=FR_OK)return false;
    }
    return true;
}
static size_t encode(uint8_t *wire,const char *text,size_t bytes,uint64_t sequence) {
    memcpy(wire,PREFIX,SEQUENCE_AT);write_hex(wire+SEQUENCE_AT,sequence,16u);
    memcpy(wire+SEQUENCE_AT+16u,MIDDLE,sizeof(MIDDLE)-1u);
    size_t remaining=bytes;
    for(unsigned i=4u;i>0u;i--) {wire[LENGTH_AT+i-1u]=(uint8_t)('0'+remaining%10u);remaining/=10u;}
    memcpy(wire+LENGTH_AT+4u,"\n\n",2u);
    if(bytes)memcpy(wire+HEADER_BYTES,text,bytes);
    memcpy(wire+HEADER_BYTES+bytes,TRAILER,sizeof(TRAILER)-1u);
    write_hex(wire+HEADER_BYTES+bytes+sizeof(TRAILER)-1u,kui_crc32(0,wire,HEADER_BYTES+bytes),8u);
    size_t total=HEADER_BYTES+bytes+TRAILER_BYTES;wire[total-1u]='\n';return total;
}
static bool verify(struct workspace *work,const char *path) {
    if(f_open(&work->file,path,FA_READ)!=FR_OK)return false;
    UINT got=0;bool ok=f_size(&work->file)==work->bytes &&
        f_read(&work->file,work->check,(UINT)work->bytes,&got)==FR_OK && got==work->bytes &&
        !memcmp(work->wire,work->check,work->bytes);
    if(f_close(&work->file)!=FR_OK)ok=false;
    return ok;
}
static bool write_file(struct workspace *work,const char *path,bool commit) {
    BYTE flags=FA_WRITE|(commit?0u:FA_CREATE_ALWAYS);
    if(f_open(&work->file,path,flags)!=FR_OK)return false;
    UINT put=0,want=commit?1u:(UINT)work->bytes;
    bool ok=f_write(&work->file,work->wire,want,&put)==FR_OK && put==want;
    if(f_sync(&work->file)!=FR_OK)ok=false;
    if(f_close(&work->file)!=FR_OK)ok=false;
    return ok;
}
bool kui_games_scan_log_write(const char *text,size_t bytes,kui_log_fn log) {
    if(!text_valid(text,bytes))return false;
    struct workspace *work=calloc(1,sizeof(*work));if(!work)return false;
    bool ok=latest(work,true) && work->sequence!=UINT64_MAX && parents(work);
    unsigned target=work->current<0?(work->slots[0].exists?1u:0u):1u-(unsigned)work->current;
    if(ok) {
        work->bytes=encode(work->wire,text,bytes,work->sequence+1u);
        /* The prefix stays invalid until the pending bytes survive a complete
         * sync/close/readback. Final one-byte publication is not cancellable. */
        work->wire[0]='?';ok=write_file(work,paths[target],false) && verify(work,paths[target]);
        if(ok) {work->wire[0]='K';ok=write_file(work,paths[target],true) && verify(work,paths[target]);}
    }
    if(!ok && log)log("Games scan log write failed; prior record retained");
    free(work);return ok;
}
void kui_games_scan_log_show(kui_log_fn log) {
    if(!log)return;
    char *text=malloc(KUI_GAMES_SCAN_LOG_TEXT_MAX+1u);if(!text)return;
    size_t bytes=0;
    if(kui_games_scan_log_load(text,KUI_GAMES_SCAN_LOG_TEXT_MAX+1u,&bytes,log) && bytes) {
        log("Previous Games scan report:");
        for(size_t at=0;at<bytes;) {
            size_t end=at;while(end<bytes && text[end]!='\n' && end-at<200u)++end;
            if(end>at)log("%.*s",(int)(end-at),text+at);
            at=end+(end<bytes && text[end]=='\n'?1u:0u);
        }
    }
    free(text);
}
