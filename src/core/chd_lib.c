/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/chd_lib.h"
#include <libchdr/chd.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_HUNK (512u * 1024u)
#define MAX_LOGICAL UINT64_C(0x100000000)
#define MAX_METADATA 256u
#define MAX_METADATA_BYTES (64u * 1024u)
#define MAX_TRACK_TEXT 512u
#define LBA_LIMIT 0x01000000u

struct kui_chdl {
    struct kui_chdl_io io;
    struct kui_chdl_info info;
    chd_file *file;
    uint8_t *hunk;
    uint64_t position;
    uint32_t cached;
    enum kui_chdl_result last_error;
    int valid;
};
static uint32_t be32(const uint8_t *p) {
    return (uint32_t)p[0]<<24 | (uint32_t)p[1]<<16 | (uint32_t)p[2]<<8 | p[3];
}
static uint64_t be64(const uint8_t *p) {return (uint64_t)be32(p)<<32 | be32(p+4);}
static int zeros(const uint8_t *p,size_t n) {while(n--)if(*p++)return 0;return 1;}
static enum kui_chdl_result read_at(struct kui_chdl *r,uint64_t offset,void *out,size_t size) {
    if(r->io.cancelled && r->io.cancelled(r->io.context))return KUI_CHDL_CANCELLED;
    if(offset>r->info.file_bytes || size>r->info.file_bytes-offset)return KUI_CHDL_RANGE;
    return r->io.read(r->io.context,offset,out,size);
}
static uint64_t file_size(void *p) {return ((struct kui_chdl *)p)->info.file_bytes;}
static size_t file_read(void *out,size_t size,size_t count,void *p) {
    struct kui_chdl *r=p;
    if(!size || !count)return 0;
    if(count>SIZE_MAX/size) {r->last_error=KUI_CHDL_RANGE;return 0;}
    enum kui_chdl_result result=read_at(r,r->position,out,size*count);
    if(result!=KUI_CHDL_OK) {r->last_error=result;return 0;}
    r->position+=size*count;return count;
}
static int file_close(void *p) {(void)p;return 0;}
static int file_seek(void *p,int64_t offset,int whence) {
    struct kui_chdl *r=p;
    uint64_t base;
    if(whence==SEEK_SET)base=0;
    else if(whence==SEEK_CUR)base=r->position;
    else if(whence==SEEK_END)base=r->info.file_bytes;
    else return -1;
    uint64_t value;
    if(offset<0) {
        uint64_t magnitude=(uint64_t)(-(offset+1))+1u;
        if(magnitude>base)return -1;
        value=base-magnitude;
    } else {
        if((uint64_t)offset>UINT64_MAX-base)return -1;
        value=base+(uint64_t)offset;
    }
    if(value>r->info.file_bytes)return -1;
    r->position=value;return 0;
}
static const core_file_callbacks callbacks={file_size,file_read,file_close,file_seek};

/* Strict text fields avoid scanf's numeric overflow and ambiguous duplicate
 * keys. Metadata is bounded before libchdr's header/unit-size inspection. */
struct fields {
    uint32_t track,frames,pad,pregap,postgap;
    char type[32],sub[16],pgtype[32],pgsub[16];
    unsigned seen;
};
static int number(const char *p,uint32_t *out) {
    uint32_t n=0;if(!*p)return 0;
    do {if(*p<'0'||*p>'9'||n>(UINT32_MAX-(unsigned)(*p-'0'))/10u)return 0;
        n=n*10u+(unsigned)(*p++-'0');}while(*p);
    *out=n;return 1;
}
static int copy_field(char *out,size_t capacity,const char *value) {
    size_t n=strlen(value);if(!n||n>=capacity)return 0;memcpy(out,value,n+1u);return 1;
}
static int parse_fields(char *text,struct fields *f) {
    memset(f,0,sizeof(*f));char *p=text;
    while(*p) {
        while(*p==' '||*p=='\t')p++;
        if(!*p)break;
        char *key=p,*colon=NULL;
        while(*p && *p!=' ' && *p!='\t') {if(*p==':') {if(colon)return 0;colon=p;}p++;}
        if(!colon || colon==key || !colon[1])return 0;
        if(*p)*p++=0;
        *colon++=0;
        unsigned bit;int ok;
        if(!strcmp(key,"TRACK")) {bit=1u;ok=number(colon,&f->track);}
        else if(!strcmp(key,"FRAMES")) {bit=2u;ok=number(colon,&f->frames);}
        else if(!strcmp(key,"PAD")) {bit=4u;ok=number(colon,&f->pad);}
        else if(!strcmp(key,"PREGAP")) {bit=8u;ok=number(colon,&f->pregap);}
        else if(!strcmp(key,"POSTGAP")) {bit=16u;ok=number(colon,&f->postgap);}
        else if(!strcmp(key,"TYPE")) {bit=32u;ok=copy_field(f->type,sizeof(f->type),colon);}
        else if(!strcmp(key,"SUBTYPE")) {bit=64u;ok=copy_field(f->sub,sizeof(f->sub),colon);}
        else if(!strcmp(key,"PGTYPE")) {bit=128u;ok=copy_field(f->pgtype,sizeof(f->pgtype),colon);}
        else if(!strcmp(key,"PGSUB")) {bit=256u;ok=copy_field(f->pgsub,sizeof(f->pgsub),colon);}
        else return 0;
        if(!ok || (f->seen&bit))return 0;
        f->seen|=bit;
    }
    return 1;
}
static int codec_supported(uint32_t codec) {
    return !codec || codec==CHD_CODEC_ZLIB || codec==CHD_CODEC_LZMA ||
        codec==CHD_CODEC_HUFFMAN || codec==CHD_CODEC_FLAC || codec==CHD_CODEC_ZSTD ||
        codec==CHD_CODEC_CD_ZLIB || codec==CHD_CODEC_CD_LZMA ||
        codec==CHD_CODEC_CD_FLAC || codec==CHD_CODEC_CD_ZSTD;
}
static enum kui_chdl_result header(struct kui_chdl *r,uint64_t *metadata) {
    uint8_t raw[CHD_MAX_HEADER_SIZE];
    enum kui_chdl_result result=read_at(r,0,raw,16);if(result)return result;
    if(memcmp(raw,"MComprHD",8))return KUI_CHDL_INVALID;
    uint32_t version=be32(raw+12),length=be32(raw+8);
    if(version!=4u&&version!=5u)return KUI_CHDL_UNSUPPORTED;
    if(length!=(version==4u?108u:124u))return KUI_CHDL_INVALID;
    result=read_at(r,16,raw+16,length-16u);if(result)return result;
    struct kui_chdl_info *i=&r->info;i->version=version;
    if(version==4u) {
        uint32_t flags=be32(raw+16),codec=be32(raw+20);
        if(flags&~2u || !zeros(raw+68,20))return KUI_CHDL_UNSUPPORTED;
        if(codec>CHDCOMPRESSION_ZLIB_PLUS)return KUI_CHDL_UNSUPPORTED;
        i->codecs[0]=codec;i->logical_bytes=be64(raw+28);i->hunk_bytes=be32(raw+44);
        i->hunk_count=be32(raw+24);*metadata=be64(raw+36);
    } else {
        if(!zeros(raw+104,20))return KUI_CHDL_UNSUPPORTED;
        for(unsigned n=0;n<4;n++) {i->codecs[n]=be32(raw+16+4u*n);
            if(!codec_supported(i->codecs[n]))return KUI_CHDL_UNSUPPORTED;}
        if(!i->codecs[0] && (i->codecs[1]||i->codecs[2]||i->codecs[3]))return KUI_CHDL_INVALID;
        i->logical_bytes=be64(raw+32);*metadata=be64(raw+48);i->hunk_bytes=be32(raw+56);
        if(be32(raw+60)!=KUI_CHDL_FRAME_BYTES)return KUI_CHDL_UNSUPPORTED;
    }
    if(!i->logical_bytes || i->logical_bytes>MAX_LOGICAL || i->logical_bytes%KUI_CHDL_FRAME_BYTES ||
       !i->hunk_bytes || i->hunk_bytes>MAX_HUNK || i->hunk_bytes%KUI_CHDL_FRAME_BYTES)
        return KUI_CHDL_UNSUPPORTED;
    uint64_t count=(i->logical_bytes+i->hunk_bytes-1u)/i->hunk_bytes;
    if(count>UINT32_MAX || (version==4u && count!=i->hunk_count))return KUI_CHDL_INVALID;
    i->hunk_count=(uint32_t)count;return KUI_CHDL_OK;
}
static enum kui_chdl_result metadata(struct kui_chdl *r,uint64_t at) {
    uint64_t visited[MAX_METADATA];unsigned entries=0,total=0;uint32_t track_tag=0;
    uint32_t disc=0,storage=0;
    while(at) {
        if(entries==MAX_METADATA)return KUI_CHDL_RANGE;
        for(unsigned j=0;j<entries;j++)if(visited[j]==at)return KUI_CHDL_INVALID;
        if(at<(r->info.version==4u?108u:124u))return KUI_CHDL_INVALID;
        visited[entries++]=at;uint8_t raw[16];
        enum kui_chdl_result result=read_at(r,at,raw,sizeof(raw));if(result)return result;
        uint32_t tag=be32(raw),size=be32(raw+4)&0xffffffu;
        if(!size || size>MAX_METADATA_BYTES-total || at+16u>r->info.file_bytes ||
           size>r->info.file_bytes-at-16u)return KUI_CHDL_RANGE;
        total+=size;
        int gd=tag==GDROM_TRACK_METADATA_TAG || tag==GDROM_OLD_METADATA_TAG;
        int cd=tag==CDROM_TRACK_METADATA_TAG || tag==CDROM_TRACK_METADATA2_TAG;
        if(gd || cd) {
            if(track_tag && track_tag!=tag)return KUI_CHDL_UNSUPPORTED;
            track_tag=tag;
            if(size>MAX_TRACK_TEXT || r->info.track_count==KUI_CHDL_TRACK_MAX)return KUI_CHDL_RANGE;
            char text[MAX_TRACK_TEXT+1u];result=read_at(r,at+16u,text,size);if(result)return result;
            if(text[size-1u]==0)size--;
            if(memchr(text,0,size))return KUI_CHDL_INVALID;
            text[size]=0;struct fields f;if(!parse_fields(text,&f))return KUI_CHDL_INVALID;
            unsigned required=1u|2u|32u|64u;
            if(tag!=CDROM_TRACK_METADATA_TAG)required|=8u|16u|128u|256u;
            if(gd)required|=4u;
            if(f.seen!=required || f.track!=r->info.track_count+1u)return KUI_CHDL_INVALID;
            if(f.pregap || f.postgap || strcmp(f.sub,"NONE") ||
               ((f.seen&256u)&&strcmp(f.pgsub,"NONE")))return KUI_CHDL_UNSUPPORTED;
            if(!f.frames || f.frames>LBA_LIMIT-disc || f.pad>=f.frames)return KUI_CHDL_RANGE;
            struct kui_chdl_track *t=&r->info.tracks[r->info.track_count];
            t->frames=f.frames;t->pad=f.pad;t->disc_lba=disc;t->storage_frame=storage;
            t->stored_frames=(f.frames+3u)&~3u;
            if(!strcmp(f.type,"AUDIO")) {t->sector_bytes=2352;t->control=0;t->mode=0;}
            else if(!strcmp(f.type,"MODE1")) {t->sector_bytes=2048;t->control=4;t->mode=1;}
            else if(!strcmp(f.type,"MODE1_RAW")) {t->sector_bytes=2352;t->control=4;t->mode=1;}
            else if(!strcmp(f.type,"MODE2_FORM1")) {t->sector_bytes=2048;t->control=4;t->mode=2;}
            else if(!strcmp(f.type,"MODE2_RAW")) {t->sector_bytes=2352;t->control=4;t->mode=2;}
            else return KUI_CHDL_UNSUPPORTED;
            if(t->stored_frames>LBA_LIMIT-storage)return KUI_CHDL_RANGE;
            disc+=f.frames;storage+=t->stored_frames;r->info.track_count++;
            r->info.gdrom=(uint8_t)gd;
            r->info.legacy_audio_little_endian=(uint8_t)(tag==GDROM_OLD_METADATA_TAG);
        } else {
            /* Optical session extensions and old binary TOCs need explicit
             * interpretation; unknown metadata cannot alter geometry silently. */
            return KUI_CHDL_UNSUPPORTED;
        }
        at=be64(raw+8);
    }
    if(!r->info.track_count || (uint64_t)storage*KUI_CHDL_FRAME_BYTES!=r->info.logical_bytes)
        return KUI_CHDL_INVALID;
    if(r->info.gdrom) {
        int high=0;
        for(uint32_t n=0;n<r->info.track_count;n++) {
            const struct kui_chdl_track *t=&r->info.tracks[n];
            if(t->disc_lba<45000u && t->frames>45000u-t->disc_lba)return KUI_CHDL_UNSUPPORTED;
            if(t->disc_lba==45000u && t->control==4u)high=1;
        }
        if(!high || r->info.tracks[r->info.track_count-1u].pad)return KUI_CHDL_UNSUPPORTED;
    }
    return KUI_CHDL_OK;
}
static enum kui_chdl_result legacy_map(struct kui_chdl *r) {
    uint64_t end=108u+((uint64_t)r->info.hunk_count+1u)*16u;
    if(end>r->info.file_bytes)return KUI_CHDL_RANGE;
    uint8_t raw[2048];uint32_t first=0;
    while(first<r->info.hunk_count) {
        uint32_t count=r->info.hunk_count-first;
        if(count>sizeof(raw)/16u)count=sizeof(raw)/16u;
        enum kui_chdl_result result=read_at(r,108u+(uint64_t)first*16u,raw,count*16u);
        if(result)return result;
        for(uint32_t n=0;n<count;n++) {
            const uint8_t *p=raw+n*16u;uint8_t flags=p[15],kind=flags&15u;
            uint64_t offset=be64(p);
            uint32_t size=(uint32_t)p[12]<<8|p[13]|(uint32_t)p[14]<<16;
            if(flags&~31u || kind<1u || kind>4u)return KUI_CHDL_UNSUPPORTED;
            if(kind<=3u && (flags&16u))return KUI_CHDL_UNSUPPORTED;
            if(kind<=2u) {
                if(!size || size>r->info.hunk_bytes || offset<end ||
                   offset>r->info.file_bytes || size>r->info.file_bytes-offset)
                    return KUI_CHDL_RANGE;
                if(kind==2u && size!=r->info.hunk_bytes)return KUI_CHDL_INVALID;
                if(kind==1u && !r->info.codecs[0])return KUI_CHDL_UNSUPPORTED;
            } else {
                if(size)return KUI_CHDL_INVALID;
                if(kind==4u && offset>=first+n)return KUI_CHDL_INVALID;
            }
        }
        first+=count;
    }
    return KUI_CHDL_OK;
}
static enum kui_chdl_result translate(struct kui_chdl *r,chd_error e) {
    if(r->last_error)return r->last_error;
    if(e==CHDERR_NONE)return KUI_CHDL_OK;
    if(e==CHDERR_OUT_OF_MEMORY)return KUI_CHDL_MEMORY;
    if(e==CHDERR_REQUIRES_PARENT || e==CHDERR_INVALID_PARENT ||
       e==CHDERR_UNSUPPORTED_VERSION || e==CHDERR_NOT_SUPPORTED || e==CHDERR_UNSUPPORTED_FORMAT)
        return KUI_CHDL_UNSUPPORTED;
    if(e==CHDERR_READ_ERROR)return KUI_CHDL_IO;
    return KUI_CHDL_DECODE;
}
enum kui_chdl_result kui_chdl_open(const struct kui_chdl_io *io,uint64_t size,struct kui_chdl **out) {
    if(!out)return KUI_CHDL_INVALID;
    *out=NULL;
    if(!io || !io->read || size<108u)return KUI_CHDL_INVALID;
    struct kui_chdl *r=calloc(1,sizeof(*r));if(!r)return KUI_CHDL_MEMORY;
    r->io=*io;r->info.file_bytes=size;uint64_t meta=0;
    enum kui_chdl_result result=header(r,&meta);
    if(result==KUI_CHDL_OK)result=metadata(r,meta);
    if(result==KUI_CHDL_OK && r->info.version==4u)result=legacy_map(r);
    if(result==KUI_CHDL_OK)result=translate(r,chd_open_core_file_callbacks(&callbacks,r,CHD_OPEN_READ,NULL,&r->file));
    if(result==KUI_CHDL_OK) {
        r->hunk=malloc(r->info.hunk_bytes);if(!r->hunk)result=KUI_CHDL_MEMORY;
    }
    if(result!=KUI_CHDL_OK) {kui_chdl_close(r);return result;}
    *out=r;return KUI_CHDL_OK;
}
void kui_chdl_close(struct kui_chdl *r) {
    if(!r)return;
    if(r->file)chd_close(r->file);
    free(r->hunk);free(r);
}
const struct kui_chdl_info *kui_chdl_get_info(const struct kui_chdl *r) {return r?&r->info:NULL;}
static enum kui_chdl_result frame(struct kui_chdl *r,uint32_t number,const uint8_t **out) {
    if(r->io.cancelled&&r->io.cancelled(r->io.context)) {r->valid=0;return KUI_CHDL_CANCELLED;}
    uint32_t per_hunk=r->info.hunk_bytes/KUI_CHDL_FRAME_BYTES,hunk=number/per_hunk;
    if(!r->valid || r->cached!=hunk) {
        r->valid=0;r->last_error=KUI_CHDL_OK;
        enum kui_chdl_result result=translate(r,chd_read(r->file,hunk,r->hunk));
        if(result)return result;
        if(r->io.cancelled&&r->io.cancelled(r->io.context))return KUI_CHDL_CANCELLED;
        r->cached=hunk;r->valid=1;
    }
    *out=r->hunk+(size_t)(number%per_hunk)*KUI_CHDL_FRAME_BYTES;return KUI_CHDL_OK;
}
enum kui_chdl_result kui_chdl_read_frames(struct kui_chdl *r,uint32_t first,uint32_t count,
                                        void *output,size_t capacity) {
    if(!r)return KUI_CHDL_INVALID;
    if(!output&&count) {r->valid=0;return KUI_CHDL_INVALID;}
    uint64_t total=r->info.logical_bytes/KUI_CHDL_FRAME_BYTES;
    if(first>total || count>total-first || count>capacity/KUI_CHDL_FRAME_BYTES) {
        r->valid=0;return KUI_CHDL_RANGE;
    }
    uint8_t *dest=output;
    for(uint32_t n=0;n<count;n++) {
        const uint8_t *data;enum kui_chdl_result result=frame(r,first+n,&data);
        if(result)return result;
        memcpy(dest,data,KUI_CHDL_FRAME_BYTES);dest+=KUI_CHDL_FRAME_BYTES;
    }
    return KUI_CHDL_OK;
}
enum kui_chdl_result kui_chdl_read_track(struct kui_chdl *r,uint32_t index,uint32_t first,
                                       uint32_t count,void *output,size_t capacity) {
    if(!r)return KUI_CHDL_INVALID;
    if(!output&&count) {r->valid=0;return KUI_CHDL_INVALID;}
    if(index>=r->info.track_count) {r->valid=0;return KUI_CHDL_RANGE;}
    const struct kui_chdl_track *t=&r->info.tracks[index];uint32_t captured=t->frames-t->pad;
    if(first>captured || count>captured-first || count>capacity/t->sector_bytes) {
        r->valid=0;return KUI_CHDL_RANGE;
    }
    uint8_t *dest=output;
    for(uint32_t n=0;n<count;n++) {
        const uint8_t *data;enum kui_chdl_result result=frame(r,t->storage_frame+first+n,&data);
        if(result)return result;
        if(!zeros(data+2352u,96u)) {r->valid=0;return KUI_CHDL_UNSUPPORTED;}
        memcpy(dest,data,t->sector_bytes);
        if(!t->control && !r->info.legacy_audio_little_endian) {
            for(unsigned b=0;b<t->sector_bytes;b+=2u) {uint8_t a=dest[b];dest[b]=dest[b+1u];dest[b+1u]=a;}
        }
        dest+=t->sector_bytes;
    }
    return KUI_CHDL_OK;
}
const char *kui_chdl_result_name(enum kui_chdl_result result) {
    static const char *const names[]={"ok","invalid CHD","unsupported CHD profile","CHD range error",
        "CHD read error","cancelled","CHD memory allocation failed","CHD checksum/decode error"};
    return (unsigned)result<sizeof(names)/sizeof(names[0])?names[result]:"unknown CHD error";
}
