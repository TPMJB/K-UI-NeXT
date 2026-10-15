/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/game_launch_report.h"
#include "kui/game_launch_settings.h"
#include "kui/game_ram_patches.h"
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const paths[]={KUI_GAME_LAUNCH_REPORT_PATH_A,KUI_GAME_LAUNCH_REPORT_PATH_B};
static const char magic[]="KUI launch report v1\nsequence: ";
static const char state[]="state: prepared before handoff; game start unconfirmed\n";
static const char hex[]="0123456789ABCDEF";
struct codec {
    char text[KUI_GAME_LAUNCH_REPORT_MAX];
    char image[KUI_GAME_LAUNCH_SETTINGS_PATH_CAP],package[KUI_GAME_LAUNCH_SETTINGS_PATH_CAP];
};
struct builder {char *text;size_t used,capacity;bool ok;};
struct slot {bool valid;uint64_t sequence;size_t size;char text[KUI_GAME_LAUNCH_REPORT_MAX];};
struct workspace {struct codec codec;struct slot slots[2];FIL file;FILINFO info;};

static size_t bounded(const char *text,size_t limit) {
    size_t n=0;if(text) while(n<=limit && text[n]) ++n;return n;
}
static bool text_valid(const char *text,size_t limit,bool empty) {
    return text && bounded(text,limit)<=limit && (empty || text[0]);
}
static void append(struct builder *b,const char *format,...) {
    if(!b->ok) return;
    va_list args;va_start(args,format);
    int n=vsnprintf(b->text+b->used,b->capacity-b->used,format,args);va_end(args);
    if(n<0 || (size_t)n>=b->capacity-b->used) b->ok=false;else b->used+=(size_t)n;
}
static void text_field(struct builder *b,const char *name,const char *value) {
    append(b,"%s: ",name);
    for(size_t i=0;value[i];i++) {
        unsigned char c=(unsigned char)value[i];
        if(c<32 || c==127 || c=='\\') append(b,"\\x%c%c",hex[c>>4],hex[c&15]);
        else append(b,"%c",(int)c);
    }
    append(b,"\n");
}
static bool format(struct codec *codec,size_t *size,const struct kui_game_launch_report *r,uint64_t sequence) {
    if(!r || !sequence || !r->ip_bytes || !r->executable_bytes ||
       !text_valid(r->version,64,false) || !text_valid(r->build_id,64,false) ||
       !text_valid(r->reader,64,false) || !text_valid(r->storage,32,false) ||
       !text_valid(r->encoding,32,false) || !text_valid(r->cdda_profile,64,false) ||
       !text_valid(r->product,64,true) || !text_valid(r->title,128,true) ||
       !text_valid(r->bootfile,128,false) ||
       !kui_game_launch_settings_normalize(codec->image,r->image_path) ||
       !kui_game_launch_settings_normalize(codec->package,r->package_path)) return false;
    struct builder b={codec->text,0,sizeof(codec->text),true};
    append(&b,"%s%020" PRIu64 "\n%s",magic,sequence,state);
    text_field(&b,"version",r->version);text_field(&b,"build",r->build_id);
    append(&b,"image: %s\n",codec->image);
    text_field(&b,"reader",r->reader);text_field(&b,"storage",r->storage);
    text_field(&b,"encoding",r->encoding);append(&b,"cd music: %s\n",r->cdda?"on":"off");
    text_field(&b,"profile",r->cdda_profile);append(&b,"package: %s\n",codec->package);
    text_field(&b,"product",r->product);text_field(&b,"title",r->title);text_field(&b,"bootfile",r->bootfile);
    append(&b,"ip bytes: %" PRIu32 "\nip crc32: %08" PRIX32 "\n",r->ip_bytes,r->ip_crc32);
    append(&b,"source executable bytes: %" PRIu32 "\nsource executable crc32: %08" PRIX32 "\n",
           r->executable_bytes,r->executable_crc32);
    for(unsigned i=0;i<2;i++) {
        bool enabled=r->patches && r->patches->entries[i].enabled;
        uint32_t address=r->patches?r->patches->entries[i].address:0;
        uint32_t value=r->patches?r->patches->entries[i].value:0;
        append(&b,"patch %u: %s address=0x%08" PRIX32 " value=0x%08" PRIX32 "\n",
               i+1,enabled?"on":"off",address,value);
    }
    if(!b.ok) return false;
    uint32_t crc=kui_crc32(0,b.text,b.used);append(&b,"crc32: %08" PRIX32 "\n",crc);
    if(!b.ok) return false;
    *size=b.used;return true;
}
bool kui_game_launch_report_encode(void *record,size_t capacity,size_t *size,
        const struct kui_game_launch_report *report,uint64_t sequence) {
    if(!record || !size) return false;
    struct codec *codec=malloc(sizeof(*codec));if(!codec) return false;
    size_t bytes=0;bool ok=format(codec,&bytes,report,sequence) && bytes<=capacity;
    if(ok) {memcpy(record,codec->text,bytes);*size=bytes;}
    free(codec);return ok;
}

struct cursor {const char *at,*end;};
static bool literal(struct cursor *c,const char *value) {
    size_t n=strlen(value);
    if((size_t)(c->end-c->at)<n || memcmp(c->at,value,n)) return false;
    c->at+=n;return true;
}
static int nibble(char c) {
    if(c>='0' && c<='9') return c-'0';
    return c>='A' && c<='F'?c-'A'+10:-1;
}
static bool hex32(struct cursor *c,uint32_t *out) {
    if(c->end-c->at<8) return false;
    uint32_t n=0;
    for(unsigned i=0;i<8;i++) {int digit=nibble(c->at[i]);if(digit<0) return false;n=(n<<4)|(unsigned)digit;}
    c->at+=8;*out=n;return true;
}
static bool decimal(struct cursor *c,uint64_t *out,unsigned width,uint64_t max) {
    uint64_t n=0;unsigned digits=0;const char *start=c->at;
    while(c->at<c->end && *c->at>='0' && *c->at<='9') {
        unsigned digit=(unsigned)(*c->at-'0');
        if(n>max/10 || (n==max/10 && digit>max%10)) return false;
        n=n*10+digit;++c->at;++digits;
    }
    if(!digits || (width && digits!=width) || (!width && digits>1 && *start=='0')) return false;
    *out=n;return true;
}
static bool field(struct cursor *c,const char *name,size_t max,bool empty) {
    if(!literal(c,name) || !literal(c,": ")) return false;
    size_t used=0;
    while(c->at<c->end && *c->at!='\n') {
        unsigned char v=(unsigned char)*c->at++;
        if(v<32 || v==127 || ++used>max) return false;
        if(v=='\\') {
            if(c->end-c->at<3 || c->at[0]!='x') return false;
            int hi=nibble(c->at[1]),lo=nibble(c->at[2]);
            if(hi<0 || lo<0) return false;
            v=(unsigned char)((hi<<4)|lo);c->at+=3;
            if(!(v<32 || v==127 || v=='\\')) return false;
        }
    }
    return (used || empty) && literal(c,"\n");
}
/* This checks only already-canonical, non-NUL text. The encoder uses the
 * shared full path normalizer; this bounded scan never allocates on read. */
static bool path_field(struct cursor *c,const char *name) {
    if(!literal(c,name) || !literal(c,": ")) return false;
    const char *start=c->at,*component=NULL;size_t used=0;
    while(c->at<c->end && *c->at!='\n') {
        unsigned char v=(unsigned char)*c->at;
        if(++used>=KUI_GAME_LAUNCH_SETTINGS_PATH_CAP || v<32 || v==127 ||
           (v>='A' && v<='Z') || strchr("\\:*?\"<>|",v)) return false;
        if(v=='/') {
            if(c->at!=start && (!component || c->at==component || c->at[-1]==' ' || c->at[-1]=='.')) return false;
            component=c->at+1;
        } else if(c->at==start || (c->at==component && v==' ')) return false;
        ++c->at;
    }
    return used>1 && component && c->at>component && c->at[-1]!=' ' && c->at[-1]!='.' && literal(c,"\n");
}
static bool number_field(struct cursor *c,const char *name) {
    uint64_t value=0;
    return literal(c,name) && literal(c,": ") && decimal(c,&value,0,UINT32_MAX) && value && literal(c,"\n");
}
static bool crc_field(struct cursor *c,const char *name) {
    uint32_t value=0;return literal(c,name) && literal(c,": ") && hex32(c,&value) && literal(c,"\n");
}
bool kui_game_launch_report_inspect(const void *record,size_t size,uint64_t *sequence) {
    if(!record || !sequence || !size || size>=KUI_GAME_LAUNCH_REPORT_MAX) return false;
    struct cursor c={record,(const char *)record+size};uint64_t n=0;
    if(!literal(&c,magic) || !decimal(&c,&n,20,UINT64_MAX) || !n || !literal(&c,"\n") || !literal(&c,state) ||
       !field(&c,"version",64,false) || !field(&c,"build",64,false) || !path_field(&c,"image") ||
       !field(&c,"reader",64,false) || !field(&c,"storage",32,false) || !field(&c,"encoding",32,false) ||
       !literal(&c,"cd music: ")) return false;
    if(!literal(&c,"on\n") && !literal(&c,"off\n")) return false;
    if(!field(&c,"profile",64,false) || !path_field(&c,"package") ||
       !field(&c,"product",64,true) || !field(&c,"title",128,true) || !field(&c,"bootfile",128,false) ||
       !number_field(&c,"ip bytes") || !crc_field(&c,"ip crc32") ||
       !number_field(&c,"source executable bytes") || !crc_field(&c,"source executable crc32")) return false;
    for(unsigned i=0;i<2;i++) {
        uint32_t value=0;
        if(!literal(&c,i?"patch 2: ":"patch 1: ")) return false;
        if(!literal(&c,"on") && !literal(&c,"off")) return false;
        if(!literal(&c," address=0x") || !hex32(&c,&value) ||
           !literal(&c," value=0x") || !hex32(&c,&value) || !literal(&c,"\n")) return false;
    }
    uint32_t crc=kui_crc32(0,record,(size_t)(c.at-(const char *)record)),stored=0;
    if(!literal(&c,"crc32: ") || !hex32(&c,&stored) || !literal(&c,"\n") || c.at!=c.end || crc!=stored) return false;
    *sequence=n;return true;
}

static bool stopped(kui_cancel_fn cancelled,kui_log_fn log) {
    if(!cancelled || !cancelled()) return false;
    if(log) log("Game launch report cancelled before write");
    return true;
}
static bool read_slot(struct workspace *w,unsigned index,kui_log_fn log) {
    struct slot *s=&w->slots[index];s->valid=false;s->size=0;s->sequence=0;
    FRESULT r=f_open(&w->file,paths[index],FA_READ);
    if(r==FR_NO_FILE || r==FR_NO_PATH) return true;
    if(r!=FR_OK) {if(log) log("Cannot read %s: FatFs=%u",paths[index]+2,(unsigned)r);return false;}
    FSIZE_t bytes=f_size(&w->file);bool sized=bytes>0 && bytes<KUI_GAME_LAUNCH_REPORT_MAX;UINT got=0;
    if(sized) {s->size=(size_t)bytes;r=f_read(&w->file,s->text,(UINT)s->size,&got);}
    FRESULT closed=f_close(&w->file);
    if(r!=FR_OK || closed!=FR_OK || (sized && got!=s->size)) {
        if(log) log("Cannot read/close %s: FatFs=%u/%u bytes=%u",paths[index]+2,(unsigned)r,(unsigned)closed,(unsigned)got);
        return false;
    }
    if(sized) s->valid=kui_game_launch_report_inspect(s->text,s->size,&s->sequence);
    if(!s->valid && log) log("Ignoring invalid game launch report: %s",paths[index]+2);
    return true;
}
static bool make_parents(struct workspace *w,kui_log_fn log,kui_cancel_fn cancelled) {
    static const char *const parents[]={"0:/KUI","0:/KUI/apps","0:/KUI/apps/games"};
    for(unsigned i=0;i<3;i++) {
        FRESULT r=f_stat(parents[i],&w->info);
        if(r==FR_OK && (w->info.fattrib&AM_DIR)) continue;
        if(r==FR_NO_FILE || r==FR_NO_PATH) {
            if(stopped(cancelled,log)) return false;
            r=f_mkdir(parents[i]);if(r==FR_OK) continue;
        }
        if(log) log("Cannot create launch report folder %s: FatFs=%u",parents[i]+2,(unsigned)r);
        return false;
    }
    return true;
}
bool kui_game_launch_report_save(const struct kui_game_launch_report *report,kui_log_fn log,kui_cancel_fn cancelled) {
    if(stopped(cancelled,log)) return false;
    struct workspace *w=malloc(sizeof(*w));
    if(!w) {if(log) log("Game launch report unavailable: not enough memory");return false;}
    size_t bytes=0;bool ok=format(&w->codec,&bytes,report,1);unsigned target=0;uint64_t sequence=1;
    if(!ok) {if(log) log("Invalid game launch report metadata");goto done;}
    if(!read_slot(w,0,log) || !read_slot(w,1,log)) {ok=false;goto done;}
    int current=w->slots[0].valid?0:-1;
    if(w->slots[1].valid && (current<0 || w->slots[1].sequence>w->slots[current].sequence)) current=1;
    if(w->slots[0].valid && w->slots[1].valid && w->slots[0].sequence==w->slots[1].sequence &&
       (w->slots[0].size!=w->slots[1].size || memcmp(w->slots[0].text,w->slots[1].text,w->slots[0].size))) {
        if(log) log("Conflicting equal-sequence game launch reports; save refused");
        ok=false;goto done;
    }
    if(current>=0) {
        if(w->slots[current].sequence==UINT64_MAX) {
            if(log) log("Game launch report sequence exhausted; save refused");
            ok=false;goto done;
        }
        sequence=w->slots[current].sequence+1;target=(unsigned)(1-current);
        if(!format(&w->codec,&bytes,report,sequence)) {ok=false;goto done;}
    }
    if(stopped(cancelled,log) || !make_parents(w,log,cancelled) || stopped(cancelled,log)) {ok=false;goto done;}
    FRESULT r=f_open(&w->file,paths[target],FA_WRITE|FA_CREATE_ALWAYS);
    if(r!=FR_OK) {if(log) log("Cannot save %s: FatFs=%u",paths[target]+2,(unsigned)r);ok=false;goto done;}
    UINT written=0;r=f_write(&w->file,w->codec.text,(UINT)bytes,&written);ok=r==FR_OK && written==bytes;
    if(ok) {r=f_sync(&w->file);ok=r==FR_OK;}
    FRESULT closed=f_close(&w->file);
    if(!ok || closed!=FR_OK) {
        if(log) log("Game launch report write/sync/close failed: FatFs=%u/%u bytes=%u; prior report retained",
                    (unsigned)r,(unsigned)closed,(unsigned)written);
        ok=false;goto done;
    }
    ok=read_slot(w,target,log) && w->slots[target].valid && w->slots[target].sequence==sequence &&
       w->slots[target].size==bytes && !memcmp(w->slots[target].text,w->codec.text,bytes);
    if(ok && log) log("Prepared launch report %s sequence=%" PRIu64,paths[target]+2,sequence);
    else if(log) log("Game launch report reread failed; prior report retained");
done:
    free(w);return ok;
}
