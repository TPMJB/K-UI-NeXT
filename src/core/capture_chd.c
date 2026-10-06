/* SPDX-License-Identifier: GPL-3.0-only */
#include "capture_export_internal.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* CHDv4 avoids a whole-image map allocation: reserve the fixed-width map,
 * append independent raw-DEFLATE hunks, then fill each map entry in place.
 * The input is this project's declared raw capture, not a forensic disc image.
 * CHGD metadata declares absent subchannels and zero padding for omitted
 * profile tails and the density gap; these bytes were not captured. */
#define CHD_HEADER 108u
#define CHD_FRAME 2448u
#define CHD_FRAMES 8u
#define CHD_HUNK (CHD_FRAME*CHD_FRAMES)
#define CHD_META_CAP 256u
#define CHD_MAP 16u

struct chd_sha1 { uint32_t h[5]; uint64_t bytes; uint8_t block[64]; };
struct chd_track {
    uint32_t captured,frames,pad,pregap,physical,stored;
    unsigned mode;
};
struct chd_cursor {
    unsigned track;uint32_t frame,source_crc;
    struct kui_sha256 source_sha;
};
struct chd_work {
    struct chd_track tracks[99];
    uint8_t metadata_hash[99][24];
    uint8_t expected[CHD_HUNK],decoded[CHD_HUNK],compressed[CHD_HUNK];
    uint8_t raw[CHD_FRAMES*KUI_RAW_BYTES];
};

static uint32_t be32(const uint8_t *p) {
    return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];
}
static uint64_t be64(const uint8_t *p) { return (uint64_t)be32(p)<<32|be32(p+4); }
static void put32(uint8_t *p,uint32_t value) {
    for(unsigned i=0;i<4u;i++) p[i]=(uint8_t)(value>>(24u-8u*i));
}
static void put64(uint8_t *p,uint64_t value) {
    put32(p,(uint32_t)(value>>32));put32(p+4,(uint32_t)value);
}
static uint32_t rol(uint32_t n,unsigned shift) { return n<<shift|n>>(32u-shift); }
static void sha1_block(struct chd_sha1 *s,const uint8_t *block) {
    /* A 16-word rolling schedule keeps the compressor worker's stack bounded. */
    uint32_t w[16],a=s->h[0],b=s->h[1],c=s->h[2],d=s->h[3],e=s->h[4];
    for(unsigned i=0;i<16u;i++) w[i]=be32(block+4u*i);
    for(unsigned i=0;i<80u;i++) {
        if(i>=16u) w[i&15u]=rol(w[(i-3u)&15u]^w[(i-8u)&15u]^w[(i-14u)&15u]^w[i&15u],1u);
        uint32_t f,k;
        if(i<20u) {f=(b&c)|(~b&d);k=0x5a827999u;}
        else if(i<40u) {f=b^c^d;k=0x6ed9eba1u;}
        else if(i<60u) {f=(b&c)|(b&d)|(c&d);k=0x8f1bbcdcu;}
        else {f=b^c^d;k=0xca62c1d6u;}
        uint32_t next=rol(a,5u)+f+e+k+w[i&15u];
        e=d;d=c;c=rol(b,30u);b=a;a=next;
    }
    s->h[0]+=a;s->h[1]+=b;s->h[2]+=c;s->h[3]+=d;s->h[4]+=e;
}
static void sha1_init(struct chd_sha1 *s) {
    memset(s,0,sizeof(*s));
    s->h[0]=0x67452301u;s->h[1]=0xefcdab89u;s->h[2]=0x98badcfeu;
    s->h[3]=0x10325476u;s->h[4]=0xc3d2e1f0u;
}
static void sha1_update(struct chd_sha1 *s,const void *data,size_t bytes) {
    const uint8_t *p=data;
    while(bytes) {
        size_t used=(size_t)(s->bytes&63u),take=64u-used;
        if(take>bytes) take=bytes;
        memcpy(s->block+used,p,take);s->bytes+=take;p+=take;bytes-=take;
        if((s->bytes&63u)==0u) sha1_block(s,s->block);
    }
}
static void sha1_digest(const struct chd_sha1 *state,uint8_t digest[20]) {
    struct chd_sha1 s=*state;uint64_t bits=s.bytes*8u;uint8_t last[128]={0x80};
    size_t used=(size_t)(s.bytes&63u),pad=used<56u?56u-used:120u-used;
    put64(last+pad,bits);sha1_update(&s,last,pad+8u);
    for(unsigned i=0;i<5u;i++) put32(digest+4u*i,s.h[i]);
}
static bool cancelled(const struct kui_capture_export_io *io) {
    return io->cancelled && io->cancelled(io->ctx);
}
static void progress(const struct kui_capture_export_io *io,uint64_t done,uint64_t total,bool verify) {
    if(io->progress) io->progress(io->ctx,done,total,verify);
}

static bool geometry(const struct kui_capture_plan *plan,const struct kui_checkpoint *state,
    const struct kui_capture_export_io *io,struct chd_work *work,uint64_t *frames) {
    if(!plan || !state || !plan->count || plan->count>99u || state->count!=plan->count) return false;
    bool high=false;*frames=0;
    for(unsigned i=0;i<plan->count;i++) {
        const struct kui_capture_track *source=&plan->tracks[i];
        struct chd_track *t=&work->tracks[i];
        if(source->number!=i+1u || source->session>1u || source->start<150u ||
           source->end<=source->start || source->toc_end<source->end || source->toc_end>0xffffffu ||
           (source->control!=0u && source->control!=4u) ||
           state->track[i].sectors!=source->end-source->start ||
           (uint64_t)state->track[i].sectors*KUI_RAW_BYTES>UINT32_MAX) return false;
        if((source->session==0u && (source->start>=45150u || source->toc_end>45150u)) ||
           (source->session==1u && source->start<45150u)) return false;
        if(!i) {
            if(source->start!=150u || source->session!=0u) return false;
        } else {
            const struct kui_capture_track *previous=&plan->tracks[i-1u];
            if(source->session<previous->session || source->start<previous->toc_end) return false;
            if(source->session==previous->session) {
                uint32_t gap=previous->toc_end-previous->end;
                if(source->start!=previous->toc_end || gap!=(source->control!=previous->control?150u:0u)) return false;
            } else {
                if(high || source->start!=45150u || source->control!=4u || previous->end!=previous->toc_end) return false;
                high=true;
            }
        }
        t->captured=state->track[i].sectors;t->frames=t->captured;t->physical=source->start-150u;
        if(source->control==4u) {
            t->mode=state->track[i].sector_mode;
            if(!t->mode) {
                if(!io->track_read(io->ctx,i,0u,work->raw,KUI_RAW_BYTES)) return false;
                t->mode=work->raw[15];
            }
            if(t->mode!=1u && t->mode!=2u) return false;
        }
        if(i) {
            struct chd_track *previous=&work->tracks[i-1u];
            uint64_t previous_end=(uint64_t)previous->frames+previous->physical;
            if(previous_end>t->physical) return false;
            uint32_t gap=(uint32_t)((uint64_t)t->physical-previous_end);
            /* The established GD-ROM CHDv4 convention stores every omitted
             * gap as a zero-filled PAD tail on the preceding track. PREGAP
             * stays zero: GD-ROM consumers add virtual pregaps to cumulative
             * addresses, which would shift the high-density area and CDDA.
             * PAD keeps original track starts and captured lengths exact. */
            if((uint64_t)previous->frames+gap>UINT32_MAX) return false;
            previous->frames+=gap;previous->pad=gap;
        }
    }
    if(!high || plan->tracks[plan->count-1u].end!=plan->tracks[plan->count-1u].toc_end) return false;
    for(unsigned i=0;i<plan->count;i++) {
        struct chd_track *t=&work->tracks[i];
        if(t->frames>UINT32_MAX-3u) return false;
        t->stored=(t->frames+3u)&~3u;*frames+=t->stored;
    }
    return *frames && *frames<=UINT64_MAX/CHD_FRAME;
}

static const char *track_type(const struct chd_track *t) {
    return t->mode==1u?"MODE1_RAW":t->mode==2u?"MODE2_RAW":"AUDIO";
}
static size_t metadata(const struct chd_work *work,unsigned track,char text[CHD_META_CAP]) {
    const struct chd_track *t=&work->tracks[track];
    int n=snprintf(text,CHD_META_CAP,
        "TRACK:%u TYPE:%s SUBTYPE:NONE FRAMES:%u PAD:%u PREGAP:%u PGTYPE:MODE1 PGSUB:NONE POSTGAP:0",
        track+1u,track_type(t),(unsigned)t->frames,(unsigned)t->pad,(unsigned)t->pregap);
    return n>0 && (unsigned)n<CHD_META_CAP?(size_t)n+1u:0u;
}
static uint64_t metadata_size(const struct kui_capture_plan *plan,const struct chd_work *work) {
    uint64_t bytes=0;char text[CHD_META_CAP];
    for(unsigned i=0;i<plan->count;i++) {
        size_t n=metadata(work,i,text);if(!n) return 0;
        bytes+=16u+n;
    }
    return bytes;
}

static bool hunk(const struct kui_capture_plan *plan,const struct kui_checkpoint *state,
    const struct kui_capture_export_io *io,struct chd_work *work,
    struct chd_cursor *cursor,unsigned *logical_frames) {
    unsigned filled=0;memset(work->expected,0,CHD_HUNK);
    while(filled<CHD_FRAMES && cursor->track<plan->count) {
        const struct chd_track *t=&work->tracks[cursor->track];
        uint32_t left=t->stored-cursor->frame;
        unsigned take=CHD_FRAMES-filled;if(take>left) take=(unsigned)left;
        unsigned raw_frames=0;
        if(cursor->frame<t->captured) {
            if(!cursor->frame) {cursor->source_crc=0u;kui_sha256_init(&cursor->source_sha);}
            uint32_t raw_left=t->captured-cursor->frame;
            raw_frames=take;if(raw_frames>raw_left) raw_frames=(unsigned)raw_left;
            if(!io->track_read(io->ctx,cursor->track,(uint64_t)cursor->frame*KUI_RAW_BYTES,
                work->raw,(size_t)raw_frames*KUI_RAW_BYTES)) return false;
            size_t source_bytes=(size_t)raw_frames*KUI_RAW_BYTES;
            cursor->source_crc=kui_crc32(cursor->source_crc,work->raw,source_bytes);
            if(!state->crc_only) kui_sha256_update(&cursor->source_sha,work->raw,source_bytes);
            if(cursor->frame+raw_frames==t->captured) {
                if(cursor->source_crc!=state->track[cursor->track].crc32) return false;
                if(!state->crc_only) {
                    uint8_t digest[32];kui_sha256_digest(&cursor->source_sha,digest);
                    if(memcmp(digest,state->track[cursor->track].sha256,sizeof(digest))) return false;
                }
            }
            for(unsigned i=0;i<raw_frames;i++) {
                uint8_t *dest=work->expected+(filled+i)*CHD_FRAME;
                const uint8_t *raw=work->raw+i*KUI_RAW_BYTES;
                if(t->mode && raw[15]!=t->mode) return false;
                memcpy(dest,raw,KUI_RAW_BYTES);
                /* GDI/CUE CDDA files have little-endian samples; CHGD stores
                 * canonical big-endian samples. This is exactly reversible. */
                if(!t->mode) for(unsigned j=0;j<KUI_RAW_BYTES;j+=2u) {
                    uint8_t temp=dest[j];dest[j]=dest[j+1u];dest[j+1u]=temp;
                }
            }
        }
        filled+=take;cursor->frame+=take;
        if(cursor->frame==t->stored) {cursor->track++;cursor->frame=0;}
    }
    *logical_frames=filled;return filled!=0u;
}

static bool write_metadata(const struct kui_capture_plan *plan,const struct kui_capture_export_io *io,
    struct chd_work *work,uint64_t offset) {
    uint8_t header[16];char text[CHD_META_CAP];
    for(unsigned i=0;i<plan->count;i++) {
        size_t n=metadata(work,i,text);if(!n) return false;
        uint64_t next=i+1u<plan->count?offset+16u+n:0u;
        memcpy(header,"CHGD",4);put32(header+4,0x01000000u|(uint32_t)n);put64(header+8,next);
        if(!io->write(io->ctx,offset,header,sizeof(header)) ||
           !io->write(io->ctx,offset+16u,text,n)) return false;
        struct chd_sha1 sha;sha1_init(&sha);sha1_update(&sha,text,n);
        memcpy(work->metadata_hash[i],"CHGD",4);sha1_digest(&sha,work->metadata_hash[i]+4);
        offset+=16u+n;
    }
    return true;
}
static void overall_sha1(struct chd_work *work,unsigned tracks,const uint8_t raw[20],uint8_t digest[20]) {
    /* Metadata hashes are sorted by tag and digest, not track number. */
    for(unsigned i=1;i<tracks;i++) {
        uint8_t record[24];memcpy(record,work->metadata_hash[i],sizeof(record));unsigned j=i;
        while(j && memcmp(work->metadata_hash[j-1u],record,sizeof(record))>0) {
            memcpy(work->metadata_hash[j],work->metadata_hash[j-1u],sizeof(record));j--;
        }
        memcpy(work->metadata_hash[j],record,sizeof(record));
    }
    struct chd_sha1 sha;sha1_init(&sha);sha1_update(&sha,raw,20u);
    sha1_update(&sha,work->metadata_hash,(size_t)tracks*24u);sha1_digest(&sha,digest);
}
static bool verify_metadata(const struct kui_capture_plan *plan,const struct kui_capture_export_io *io,
    const struct chd_work *work,uint64_t offset) {
    uint8_t actual[16],expected[16];char text[CHD_META_CAP],readback[CHD_META_CAP];
    for(unsigned i=0;i<plan->count;i++) {
        size_t n=metadata(work,i,text);if(!n) return false;
        uint64_t next=i+1u<plan->count?offset+16u+n:0u;
        memcpy(expected,"CHGD",4);put32(expected+4,0x01000000u|(uint32_t)n);put64(expected+8,next);
        if(!io->read(io->ctx,offset,actual,sizeof(actual)) || memcmp(actual,expected,sizeof(actual)) ||
           !io->read(io->ctx,offset+16u,readback,n) || memcmp(text,readback,n)) return false;
        offset+=16u+n;
    }
    return true;
}

enum kui_capture_export_result kui_capture_chd_write(const struct kui_capture_plan *plan,
    const struct kui_checkpoint *state,const struct kui_capture_export_io *io,
    struct kui_capture_export_report *report) {
    if(!io || !io->track_read || !io->read || !io->write || !io->sync || !report) return KUI_EXPORT_FAILED;
    memset(report,0,sizeof(*report));
    if(cancelled(io)) return KUI_EXPORT_STOPPED;
    struct chd_work *work=calloc(1u,sizeof(*work));if(!work) return KUI_EXPORT_FAILED;
    enum kui_capture_export_result result=KUI_EXPORT_FAILED;uint64_t frames=0;
    if(!geometry(plan,state,io,work,&frames)) goto done;
    uint64_t logical=frames*CHD_FRAME,hunks=(frames+CHD_FRAMES-1u)/CHD_FRAMES;
    if(hunks>UINT32_MAX) goto done;
    uint64_t payload=CHD_HEADER+(hunks+1u)*CHD_MAP,offset=payload;
    uint8_t header[CHD_HEADER]={0},entry[CHD_MAP],raw_sha[20],combined_sha[20];
    /* Reserve the map in bounded writes; never require sparse-file behavior. */
    memset(work->decoded,0,CHD_HUNK);
    for(uint64_t at=0;at<payload;) {
        if(cancelled(io)) {result=KUI_EXPORT_STOPPED;goto done;}
        size_t n=(size_t)(payload-at);if(n>CHD_HUNK) n=CHD_HUNK;
        if(!io->write(io->ctx,at,work->decoded,n)) goto done;
        at+=n;
    }
    if(!io->write(io->ctx,payload-CHD_MAP,"EndOfListCookie\0",CHD_MAP)) goto done;
    struct chd_sha1 sha;sha1_init(&sha);struct chd_cursor cursor={0};uint64_t processed=0;
    for(uint64_t i=0;i<hunks;i++) {
        if(cancelled(io)) {result=KUI_EXPORT_STOPPED;goto done;}
        unsigned logical_frames=0;if(!hunk(plan,state,io,work,&cursor,&logical_frames)) goto done;
        size_t logical_bytes=(size_t)logical_frames*CHD_FRAME;
        sha1_update(&sha,work->expected,logical_bytes);
        size_t compressed=kui_export_deflate(work->expected,CHD_HUNK,work->compressed,CHD_HUNK);
        bool use_compressed=compressed && compressed<CHD_HUNK;
        size_t stored=use_compressed?compressed:CHD_HUNK;
        if(offset>UINT64_MAX-stored || !io->write(io->ctx,offset,
            use_compressed?work->compressed:work->expected,stored)) goto done;
        put64(entry,offset);put32(entry+8,kui_crc32(0,work->expected,CHD_HUNK));
        entry[12]=(uint8_t)(stored>>8);entry[13]=(uint8_t)stored;
        entry[14]=(uint8_t)(stored>>16);entry[15]=(uint8_t)(use_compressed?1u:2u);
        if(!io->write(io->ctx,CHD_HEADER+i*CHD_MAP,entry,sizeof(entry))) goto done;
        offset+=stored;processed+=logical_bytes;progress(io,processed,logical,false);
    }
    if(processed!=logical || cursor.track!=plan->count) goto done;
    sha1_digest(&sha,raw_sha);
    uint64_t meta_offset=offset,meta_bytes=metadata_size(plan,work);
    if(!meta_bytes || offset>UINT64_MAX-meta_bytes || !write_metadata(plan,io,work,offset)) goto done;
    offset+=meta_bytes;overall_sha1(work,plan->count,raw_sha,combined_sha);
    memcpy(header,"MComprHD",8);put32(header+8,CHD_HEADER);put32(header+12,4u);
    put32(header+16,0u);put32(header+20,1u);put32(header+24,(uint32_t)hunks);
    put64(header+28,logical);put64(header+36,meta_offset);put32(header+44,CHD_HUNK);
    memcpy(header+48,combined_sha,20);memcpy(header+88,raw_sha,20);
    if(!io->write(io->ctx,0u,header,sizeof(header)) || !io->sync(io->ctx)) goto done;

    /* Semantic readback rebuilds every source frame, decodes the saved hunk,
     * checks CRC and both SHA1s, and verifies all geometry metadata exactly. */
    if(cancelled(io)) {result=KUI_EXPORT_STOPPED;goto done;}
    if(!io->read(io->ctx,0u,work->decoded,sizeof(header)) || memcmp(header,work->decoded,sizeof(header)) ||
       !io->read(io->ctx,payload-CHD_MAP,entry,sizeof(entry)) || memcmp(entry,"EndOfListCookie\0",CHD_MAP)) goto done;
    cursor=(struct chd_cursor){0};sha1_init(&sha);processed=0;uint64_t read_offset=payload;
    for(uint64_t i=0;i<hunks;i++) {
        if(cancelled(io)) {result=KUI_EXPORT_STOPPED;goto done;}
        unsigned logical_frames=0;if(!hunk(plan,state,io,work,&cursor,&logical_frames) ||
            !io->read(io->ctx,CHD_HEADER+i*CHD_MAP,entry,sizeof(entry))) goto done;
        uint32_t length=(uint32_t)entry[12]<<8|entry[13]|(uint32_t)entry[14]<<16;
        if(be64(entry)!=read_offset || !length || length>CHD_HUNK ||
           (entry[15]!=1u && entry[15]!=2u) ||
           (entry[15]==2u && length!=CHD_HUNK) || (entry[15]==1u && length>=CHD_HUNK) ||
           read_offset>meta_offset || length>meta_offset-read_offset) goto done;
        if(!io->read(io->ctx,read_offset,work->compressed,length)) goto done;
        if(entry[15]==1u) {
            if(!kui_export_inflate(work->compressed,length,work->decoded,CHD_HUNK)) goto done;
        } else memcpy(work->decoded,work->compressed,CHD_HUNK);
        if(memcmp(work->decoded,work->expected,CHD_HUNK) ||
           be32(entry+8)!=kui_crc32(0,work->decoded,CHD_HUNK)) goto done;
        size_t logical_bytes=(size_t)logical_frames*CHD_FRAME;sha1_update(&sha,work->decoded,logical_bytes);
        read_offset+=length;processed+=logical_bytes;progress(io,processed,logical,true);
    }
    sha1_digest(&sha,combined_sha);
    if(processed!=logical || cursor.track!=plan->count || read_offset!=meta_offset ||
       memcmp(combined_sha,raw_sha,20) || !verify_metadata(plan,io,work,meta_offset) ||
       !io->sync(io->ctx)) goto done;
    report->bytes=offset;report->logical_bytes=logical;report->data_track=0u;result=KUI_EXPORT_COMPLETE;
done:
    if(result==KUI_EXPORT_FAILED && cancelled(io)) result=KUI_EXPORT_STOPPED;
    free(work);return result;
}
