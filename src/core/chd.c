/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/chd.h"
#include "kui/core.h"
#include "miniz.h"
#include <string.h>

/* Independent restricted CHDv4 reader. Container semantics follow the
 * project's capture_chd.c writer and documented MAME CHDv4 map definitions.
 * Only miniz's independently sourced raw inflate primitive is reused. */
#define MAP_BYTES 16u
#define META_BYTES 256u
#define WORK_MAGIC UINT32_C(0x43484434)
struct work {
    tinfl_decompressor inflate;
    uint8_t decoded[KUI_CHD_HUNK_BYTES], input[512];
    const struct kui_chd_info *owner;
    uint32_t magic, cached;
    int valid;
};
static uint32_t be32(const uint8_t *p) {
    return (uint32_t)p[0]<<24 | (uint32_t)p[1]<<16 | (uint32_t)p[2]<<8 | p[3];
}
static uint64_t be64(const uint8_t *p) { return (uint64_t)be32(p)<<32 | be32(p+4); }
static int zero(const uint8_t *p,size_t n) {
    while(n--) if(*p++) return 0;
    return 1;
}
static enum kui_chd_result read_at(const struct kui_chd_io *io,uint64_t file,
    uint64_t at,void *out,size_t bytes) {
    if(at>file || bytes>file-at) return KUI_CHD_RANGE;
    if(io->cancelled && io->cancelled(io->context)) return KUI_CHD_CANCELLED;
    return io->read(io->context,at,out,bytes);
}
static int valid(const struct kui_chd_info *info) {
    if(!info || !info->logical_bytes || info->logical_bytes%KUI_CHD_FRAME_BYTES ||
       info->logical_bytes>(uint64_t)KUI_CHD_LBA_LIMIT*KUI_CHD_FRAME_BYTES ||
       info->hunk_count!=(info->logical_bytes+KUI_CHD_HUNK_BYTES-1u)/KUI_CHD_HUNK_BYTES ||
       !info->track_count || info->track_count>KUI_CHD_TRACK_MAX ||
       info->metadata_offset<KUI_CHD_HEADER_BYTES+((uint64_t)info->hunk_count+1u)*MAP_BYTES ||
       info->metadata_offset>=info->file_bytes) return 0;
    uint32_t disc=0,storage=0;int high=0;
    for(uint32_t i=0;i<info->track_count;i++) {
        const struct kui_chd_track *t=&info->tracks[i];
        if(t->disc_lba!=disc || t->storage_frame!=storage || !t->frames || t->pad>=t->frames ||
           t->mode>2u || t->frames>KUI_CHD_LBA_LIMIT-disc ||
           t->stored_frames!=((t->frames+3u)&~3u) || t->stored_frames>KUI_CHD_LBA_LIMIT-storage)
            return 0;
        if(t->disc_lba==45000u && t->mode) high=1;
        if(t->disc_lba<45000u && t->frames>45000u-t->disc_lba) return 0;
        if(i+1u<info->track_count && info->tracks[i+1u].disc_lba!=45000u &&
           t->pad!=((!t->mode)!=(!info->tracks[i+1u].mode)?150u:0u)) return 0;
        disc+=t->frames;storage+=t->stored_frames;
    }
    return high && !info->tracks[info->track_count-1u].pad &&
        (uint64_t)storage*KUI_CHD_FRAME_BYTES==info->logical_bytes;
}
struct entry { uint64_t offset;uint32_t crc,length;uint8_t type; };
static enum kui_chd_result entry(const struct kui_chd_info *info,
    const struct kui_chd_io *io,uint32_t hunk,struct entry *out) {
    uint8_t raw[MAP_BYTES];
    enum kui_chd_result r=read_at(io,info->file_bytes,KUI_CHD_HEADER_BYTES+(uint64_t)hunk*MAP_BYTES,raw,sizeof(raw));
    if(r!=KUI_CHD_OK) return r;
    struct entry e={be64(raw),be32(raw+8),(uint32_t)raw[12]<<8|raw[13]|(uint32_t)raw[14]<<16,raw[15]};
    if(e.type!=1u && e.type!=2u) return KUI_CHD_UNSUPPORTED;
    if(!e.length || e.length>KUI_CHD_HUNK_BYTES ||
       (e.type==1u && e.length>=KUI_CHD_HUNK_BYTES) ||
       (e.type==2u && e.length!=KUI_CHD_HUNK_BYTES) ||
       e.offset<KUI_CHD_HEADER_BYTES+((uint64_t)info->hunk_count+1u)*MAP_BYTES ||
       e.offset>info->metadata_offset || e.length>info->metadata_offset-e.offset) return KUI_CHD_RANGE;
    *out=e;return KUI_CHD_OK;
}
static int literal(const char **p,const char *word) {
    size_t n=strlen(word);if(strncmp(*p,word,n)) return 0;*p+=n;return 1;
}
static int number(const char **p,uint32_t *out) {
    const char *s=*p;uint32_t value=0;
    if(*s<'0' || *s>'9') return 0;
    do {unsigned digit=(unsigned)(*s++-'0');if(value>(UINT32_MAX-digit)/10u)return 0;value=value*10u+digit;} while(*s>='0' && *s<='9');
    *out=value;*p=s;return 1;
}
static enum kui_chd_result track_metadata(const char *text,uint32_t index,struct kui_chd_track *t) {
    const char *p=text;uint32_t n,pregap,postgap;
    if(!literal(&p,"TRACK:") || !number(&p,&n) || n!=index+1u || !literal(&p," TYPE:")) return KUI_CHD_INVALID;
    if(literal(&p,"MODE1_RAW")) t->mode=1u;
    else if(literal(&p,"MODE2_RAW")) t->mode=2u;
    else if(literal(&p,"AUDIO")) t->mode=0u;
    else return KUI_CHD_UNSUPPORTED;
    if(!literal(&p," SUBTYPE:NONE FRAMES:") || !number(&p,&t->frames) ||
       !literal(&p," PAD:") || !number(&p,&t->pad) || !literal(&p," PREGAP:") ||
       !number(&p,&pregap) || !literal(&p," PGTYPE:MODE1 PGSUB:NONE POSTGAP:") ||
       !number(&p,&postgap) || *p) return KUI_CHD_UNSUPPORTED;
    if(pregap || postgap) return KUI_CHD_UNSUPPORTED;
    return KUI_CHD_OK;
}
enum kui_chd_result kui_chd_open(const struct kui_chd_io *io,uint64_t file_bytes,struct kui_chd_info *out) {
    if(!io || !io->read || !out) return KUI_CHD_INVALID;
    uint8_t header[KUI_CHD_HEADER_BYTES],raw[MAP_BYTES];
    enum kui_chd_result r=read_at(io,file_bytes,0u,header,sizeof(header));
    if(r!=KUI_CHD_OK) return r;
    if(memcmp(header,"MComprHD",8u) || be32(header+8)!=KUI_CHD_HEADER_BYTES ||
       be32(header+12)!=4u || be32(header+16) || be32(header+20)!=1u ||
       be32(header+44)!=KUI_CHD_HUNK_BYTES || !zero(header+68,20u)) return KUI_CHD_UNSUPPORTED;
    struct kui_chd_info info={0};info.file_bytes=file_bytes;info.hunk_count=be32(header+24);
    info.logical_bytes=be64(header+28);info.metadata_offset=be64(header+36);
    if(!info.logical_bytes || info.logical_bytes%KUI_CHD_FRAME_BYTES ||
       info.logical_bytes>(uint64_t)KUI_CHD_LBA_LIMIT*KUI_CHD_FRAME_BYTES ||
       info.hunk_count!=(info.logical_bytes+KUI_CHD_HUNK_BYTES-1u)/KUI_CHD_HUNK_BYTES ||
       info.metadata_offset<KUI_CHD_HEADER_BYTES+((uint64_t)info.hunk_count+1u)*MAP_BYTES ||
       info.metadata_offset>=file_bytes) return KUI_CHD_RANGE;
    uint64_t position=KUI_CHD_HEADER_BYTES+((uint64_t)info.hunk_count+1u)*MAP_BYTES;
    r=read_at(io,file_bytes,position-MAP_BYTES,raw,sizeof(raw));
    if(r!=KUI_CHD_OK) return r;
    if(memcmp(raw,"EndOfListCookie\0",MAP_BYTES)) return KUI_CHD_INVALID;
    for(uint32_t i=0;i<info.hunk_count;i++) {
        struct entry e;r=entry(&info,io,i,&e);if(r!=KUI_CHD_OK)return r;
        if(e.offset!=position) return KUI_CHD_UNSUPPORTED;
        position+=e.length;
    }
    if(position!=info.metadata_offset) return KUI_CHD_RANGE;
    uint32_t disc=0,storage=0;
    while(position) {
        if(info.track_count==KUI_CHD_TRACK_MAX) return KUI_CHD_RANGE;
        r=read_at(io,file_bytes,position,raw,sizeof(raw));if(r!=KUI_CHD_OK)return r;
        if(memcmp(raw,"CHGD",4u)) return KUI_CHD_UNSUPPORTED;
        uint32_t flags=be32(raw+4),length=flags&0xffffffu;uint64_t next=be64(raw+8);
        if(flags>>24!=1u || !length || length>META_BYTES || position>file_bytes-16u || length>file_bytes-position-16u)
            return KUI_CHD_RANGE;
        /* Own exports place metadata contiguously after hunks. This also
         * forbids cycles, duplicated extents and pointers into hunk payload. */
        uint64_t end=position+16u+length;
        if((next && next!=end) || (!next && end!=file_bytes)) return KUI_CHD_INVALID;
        char text[META_BYTES];r=read_at(io,file_bytes,position+16u,text,length);if(r!=KUI_CHD_OK)return r;
        if(text[length-1u] || memchr(text,0,length-1u)) return KUI_CHD_INVALID;
        struct kui_chd_track *t=&info.tracks[info.track_count];
        r=track_metadata(text,info.track_count,t);if(r!=KUI_CHD_OK)return r;
        if(!t->frames || t->frames>KUI_CHD_LBA_LIMIT-disc || t->pad>=t->frames ||
           t->frames>KUI_CHD_LBA_LIMIT-storage) return KUI_CHD_RANGE;
        t->disc_lba=disc;t->storage_frame=storage;t->stored_frames=(t->frames+3u)&~3u;
        if(t->stored_frames>KUI_CHD_LBA_LIMIT-storage) return KUI_CHD_RANGE;
        disc+=t->frames;storage+=t->stored_frames;info.track_count++;position=next;
    }
    if(!valid(&info)) return KUI_CHD_UNSUPPORTED;
    *out=info;return KUI_CHD_OK;
}
size_t kui_chd_workspace_bytes(void) { return sizeof(struct work); }
size_t kui_chd_workspace_alignment(void) { return _Alignof(struct work); }
enum kui_chd_result kui_chd_workspace_init(void *workspace,size_t bytes) {
    if(!workspace || bytes<sizeof(struct work) || (uintptr_t)workspace%_Alignof(struct work)) return KUI_CHD_INVALID;
    struct work *w=workspace;memset(w,0,sizeof(*w));w->magic=WORK_MAGIC;return KUI_CHD_OK;
}
static enum kui_chd_result inflate_hunk(const struct kui_chd_info *info,
    const struct kui_chd_io *io,struct work *w,const struct entry *e) {
    uint32_t left=e->length,total=0;uint64_t offset=e->offset;tinfl_init(&w->inflate);
    while(left) {
        size_t n=left>sizeof(w->input)?sizeof(w->input):left;
        enum kui_chd_result r=read_at(io,info->file_bytes,offset,w->input,n);if(r!=KUI_CHD_OK)return r;
        size_t used=n,written=KUI_CHD_HUNK_BYTES-total;
        tinfl_status status=tinfl_decompress(&w->inflate,w->input,&used,w->decoded,w->decoded+total,&written,
            TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF | (left>n?TINFL_FLAG_HAS_MORE_INPUT:0));
        if(used>n || written>KUI_CHD_HUNK_BYTES-total) return KUI_CHD_INVALID;
        total+=(uint32_t)written;offset+=used;left-=(uint32_t)used;
        if(status==TINFL_STATUS_DONE) return !left && used==n && total==KUI_CHD_HUNK_BYTES?KUI_CHD_OK:KUI_CHD_INVALID;
        if(status!=TINFL_STATUS_NEEDS_MORE_INPUT || used!=n || !left || total==KUI_CHD_HUNK_BYTES) return KUI_CHD_INVALID;
    }
    return KUI_CHD_INVALID;
}
static enum kui_chd_result load(const struct kui_chd_info *info,const struct kui_chd_io *io,
    struct work *w,uint32_t hunk) {
    if(w->valid && w->owner==info && w->cached==hunk) {
        if(io->cancelled && io->cancelled(io->context)) {w->valid=0;return KUI_CHD_CANCELLED;}
        return KUI_CHD_OK;
    }
    w->valid=0;struct entry e;enum kui_chd_result r=entry(info,io,hunk,&e);
    if(r!=KUI_CHD_OK) return r;
    if(e.type==1u) r=inflate_hunk(info,io,w,&e);
    else r=read_at(io,info->file_bytes,e.offset,w->decoded,KUI_CHD_HUNK_BYTES);
    if(r!=KUI_CHD_OK) return r;
    if(kui_crc32(0,w->decoded,sizeof(w->decoded))!=e.crc) return KUI_CHD_CRC;
    /* NONE subchannels and synthetic PAD/alignment must match their declared
     * absent/zero representation, even when a request asks for data only. */
    uint32_t first=hunk*8u;
    for(uint32_t f=0;f<8u;f++) {
        uint32_t frame=first+f;const uint8_t *p=w->decoded+f*KUI_CHD_FRAME_BYTES;
        if(!zero(p+2352u,96u)) return KUI_CHD_UNSUPPORTED;
        int captured=0;
        for(uint32_t t=0;t<info->track_count;t++) {
            const struct kui_chd_track *track=&info->tracks[t];
            if(frame>=track->storage_frame && frame-track->storage_frame<track->frames-track->pad) {
                if(track->mode && p[15]!=track->mode) return KUI_CHD_INVALID;
                captured=1;break;
            }
        }
        if(!captured && !zero(p,2352u)) return KUI_CHD_INVALID;
    }
    w->owner=info;w->cached=hunk;w->valid=1;return KUI_CHD_OK;
}
static struct work *workspace_check(void *workspace,size_t bytes) {
    if(!workspace || bytes<sizeof(struct work) || (uintptr_t)workspace%_Alignof(struct work))return NULL;
    struct work *w=workspace;return w->magic==WORK_MAGIC?w:NULL;
}
enum kui_chd_result kui_chd_read_frames(const struct kui_chd_info *info,const struct kui_chd_io *io,
    void *workspace,size_t bytes,uint32_t first,uint32_t count,void *out,size_t capacity) {
    struct work *w=workspace_check(workspace,bytes);
    if(!w || !valid(info) || !io || !io->read || !out || !count) {if(w)w->valid=0;return KUI_CHD_INVALID;}
    uint64_t frames=info->logical_bytes/KUI_CHD_FRAME_BYTES;
    if(first>=frames || count>frames-first || count>capacity/KUI_CHD_FRAME_BYTES) {w->valid=0;return KUI_CHD_RANGE;}
    uint8_t *p=out;
    while(count) {
        enum kui_chd_result r=load(info,io,w,first/8u);if(r!=KUI_CHD_OK)return r;
        uint32_t take=8u-first%8u;if(take>count)take=count;
        size_t n=(size_t)take*KUI_CHD_FRAME_BYTES;memcpy(p,w->decoded+(first%8u)*KUI_CHD_FRAME_BYTES,n);
        first+=take;count-=take;p+=n;
    }
    return KUI_CHD_OK;
}
enum kui_chd_result kui_chd_read_sectors(const struct kui_chd_info *info,const struct kui_chd_io *io,
    void *workspace,size_t bytes,uint32_t lba,uint32_t count,void *out,size_t capacity) {
    struct work *w=workspace_check(workspace,bytes);
    if(!w || !valid(info) || !io || !io->read || !out || !count) {if(w)w->valid=0;return KUI_CHD_INVALID;}
    if(lba>=KUI_CHD_LBA_LIMIT || count>KUI_CHD_LBA_LIMIT-lba || count>capacity/2352u) {w->valid=0;return KUI_CHD_RANGE;}
    /* Preflight every range before copying, so omitted PAD is never silently
     * turned into a legitimate sector in a multi-sector request. */
    uint32_t cursor=lba,left=count;
    for(uint32_t t=0;t<info->track_count && left;t++) {
        const struct kui_chd_track *track=&info->tracks[t];uint32_t end=track->disc_lba+track->frames-track->pad;
        if(end<=cursor) continue;
        if(cursor<track->disc_lba) {w->valid=0;return KUI_CHD_GAP;}
        uint32_t take=end-cursor;if(take>left)take=left;cursor+=take;left-=take;
    }
    if(left) {w->valid=0;return KUI_CHD_GAP;}
    uint8_t *p=out;
    for(uint32_t t=0;t<info->track_count && count;t++) {
        const struct kui_chd_track *track=&info->tracks[t];uint32_t end=track->disc_lba+track->frames-track->pad;
        while(lba>=track->disc_lba && lba<end && count) {
            uint32_t frame=track->storage_frame+lba-track->disc_lba;
            enum kui_chd_result r=load(info,io,w,frame/8u);if(r!=KUI_CHD_OK)return r;
            memcpy(p,w->decoded+(frame%8u)*KUI_CHD_FRAME_BYTES,2352u);
            if(!track->mode) for(unsigned b=0;b<2352u;b+=2u) {uint8_t v=p[b];p[b]=p[b+1u];p[b+1u]=v;}
            p+=2352u;lba++;count--;
        }
    }
    return KUI_CHD_OK;
}
const char *kui_chd_result_name(enum kui_chd_result result) {
    switch(result) {
    case KUI_CHD_OK:return "OK";
    case KUI_CHD_INVALID:return "Invalid CHD profile data";
    case KUI_CHD_UNSUPPORTED:return "Unsupported CHD version, codec, map or track profile";
    case KUI_CHD_RANGE:return "CHD range outside admitted bounds";
    case KUI_CHD_IO:return "CHD read failed";
    case KUI_CHD_CANCELLED:return "Cancelled";
    case KUI_CHD_CRC:return "CHD hunk CRC mismatch";
    case KUI_CHD_GAP:return "Requested CHD sectors were not captured";
    default:return "Unknown CHD error";
    }
}
