/* SPDX-License-Identifier: GPL-3.0-only */
#include "capture_export_internal.h"
#include "miniz.h"
#include "minilzo.h"
#include <stdio.h>
#include <string.h>

/* The sole capture worker owns these fixed buffers. No image or complete
 * index is held in RAM; codec state is independent of disc size. */
static tdefl_compressor deflater;
static tinfl_decompressor inflater;
static _Alignas(max_align_t) uint8_t lzo_work[LZO1X_1_MEM_COMPRESS];
static uint8_t raw[KUI_RAW_BYTES],cooked[KUI_DATA_BYTES],decoded[KUI_DATA_BYTES];
static uint8_t packed[KUI_DATA_BYTES+KUI_DATA_BYTES/16u+67u],chunk[4096];

size_t kui_export_deflate(const uint8_t *src,size_t bytes,uint8_t *out,size_t capacity) {
    size_t used=bytes,written=capacity;
    int flags=(int)tdefl_create_comp_flags_from_zip_params(1,-15,0);
    if(tdefl_init(&deflater,NULL,NULL,flags)!=TDEFL_STATUS_OKAY) return 0;
    tdefl_status result=tdefl_compress(&deflater,src,&used,out,&written,TDEFL_FINISH);
    return result==TDEFL_STATUS_DONE && used==bytes?written:0;
}
bool kui_export_inflate(const uint8_t *src,size_t bytes,uint8_t *out,size_t expected) {
    size_t used=bytes,written=expected;tinfl_init(&inflater);
    tinfl_status status=tinfl_decompress(&inflater,src,&used,out,out,&written,
        TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
    return status==TINFL_STATUS_DONE && used==bytes && written==expected;
}
static void put32(uint8_t *p,uint32_t v) {
    for(unsigned i=0;i<4;i++) p[i]=(uint8_t)(v>>(8u*i));
}
static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static bool stopped(const struct kui_capture_export_io *io) {
    return io->cancelled && io->cancelled(io->ctx);
}
static bool refuse(char *why,size_t cap,const char *text) {
    if(why && cap) snprintf(why,cap,"%s",text);
    return false;
}
bool kui_capture_export_preflight(const struct kui_capture_plan *plan,const struct kui_checkpoint *state,
    enum kui_capture_format format,char *why,size_t capacity) {
    if(why && capacity) why[0]=0;
    if(!plan || !state || !plan->count || plan->count>99u || state->count!=plan->count)
        return refuse(why,capacity,"Invalid capture plan/checkpoint");
    if(format!=KUI_CAPTURE_FORMAT_CSO && format!=KUI_CAPTURE_FORMAT_ZSO && format!=KUI_CAPTURE_FORMAT_CHD)
        return refuse(why,capacity,"Unsupported derivative format");
    unsigned high_data=0;
    for(unsigned i=0;i<plan->count;i++) {
        const struct kui_capture_track *t=&plan->tracks[i];
        if(t->number!=i+1u || t->control>4u || (t->control!=0u && t->control!=4u) ||
           t->session>1u || t->start<150u || t->end<=t->start || t->toc_end<t->end ||
           (i && (t->start<plan->tracks[i-1u].end || t->session<plan->tracks[i-1u].session)))
            return refuse(why,capacity,"The raw capture track layout is invalid");
        if(t->session==1u && t->control==4u) {
            ++high_data;
            if(format!=KUI_CAPTURE_FORMAT_CHD && t->start!=45150u)
                return refuse(why,capacity,"CSO/ZSO require the high-density data track at FAD 45150; raw capture retained");
        }
    }
    if(format!=KUI_CAPTURE_FORMAT_CHD && high_data!=1u)
        return refuse(why,capacity,"CSO/ZSO support one high-density data track; use GDI/CUE/CHD for this layout");
    if(format==KUI_CAPTURE_FORMAT_CHD) {
        bool high=false;
        if(plan->tracks[0].start!=150u || plan->tracks[0].session!=0u)
            return refuse(why,capacity,"CHD requires the declared two-density GD capture profile");
        for(unsigned i=1;i<plan->count;i++) {
            const struct kui_capture_track *t=&plan->tracks[i],*p=&plan->tracks[i-1u];
            if(t->start<p->toc_end) return refuse(why,capacity,"CHD track geometry overlaps");
            if(t->session==p->session) {
                if(t->start!=p->toc_end || p->toc_end-p->end!=(t->control!=p->control?150u:0u))
                    return refuse(why,capacity,"CHD requires the declared omitted-pregap capture profile");
            } else {
                if(high || t->start!=45150u || t->control!=4u || p->end!=p->toc_end)
                    return refuse(why,capacity,"CHD high-density transition is unsupported");
                high=true;
            }
        }
        const struct kui_capture_track *last=&plan->tracks[plan->count-1u];
        if(!high || last->end!=last->toc_end)
            return refuse(why,capacity,"CHD requires a complete high-density session");
    }
    uint64_t bound=0;
    if(!kui_capture_export_bound(plan,format,&bound))
        return refuse(why,capacity,"Container exceeds its bounded offset/size limit");
    return true;
}
bool kui_capture_export_bound(const struct kui_capture_plan *plan,enum kui_capture_format format,uint64_t *bytes) {
    if(!plan || !bytes || !plan->count || plan->count>99u) return false;
    if(format==KUI_CAPTURE_FORMAT_CSO || format==KUI_CAPTURE_FORMAT_ZSO) {
        unsigned found=0;uint64_t blocks=0;
        for(unsigned i=0;i<plan->count;i++) if(plan->tracks[i].session==1u && plan->tracks[i].control==4u) {
            if(plan->tracks[i].end<=plan->tracks[i].start) return false;
            ++found;blocks=plan->tracks[i].end-plan->tracks[i].start;
        }
        if(found!=1u) return false;
        *bytes=24u+4u*(blocks+1u)+blocks*KUI_DATA_BYTES;
        return *bytes<UINT64_C(0x80000000);
    }
    if(format==KUI_CAPTURE_FORMAT_CHD) {
        /* Captured frames + density padding never exceed disc LBA span;
         * at most three alignment frames per track plus seven final hunk
         * frames. Metadata is bounded by 256 bytes plus its 16-byte header. */
        uint32_t end=plan->tracks[plan->count-1u].end;
        if(end<=150u) return false;
        uint64_t frames=(uint64_t)end-150u+3u*plan->count;
        uint64_t hunks=(frames+7u)/8u;
        *bytes=108u+16u*(hunks+1u)+hunks*8u*2448u+272u*plan->count;
        return *bytes<=UINT32_MAX;
    }
    return false;
}
static bool source_sector(const struct kui_capture_export_io *io,unsigned track,uint32_t sector) {
    if(!io->track_read(io->ctx,track,(uint64_t)sector*KUI_RAW_BYTES,raw,sizeof(raw))) return false;
    int offset=kui_data_offset(raw);
    if(offset<0 || !kui_sector_edc_valid(raw)) return false;
    memcpy(cooked,raw+(unsigned)offset,sizeof(cooked));return true;
}
static enum kui_capture_export_result ciso_write(const struct kui_capture_plan *plan,
    const struct kui_checkpoint *state,enum kui_capture_format format,const struct kui_capture_export_io *io,
    struct kui_capture_export_report *report) {
    unsigned track=0;
    while(track<plan->count && !(plan->tracks[track].session==1u && plan->tracks[track].control==4u)) ++track;
    if(track==plan->count) return KUI_EXPORT_FAILED;
    uint32_t blocks=state->track[track].sectors;
    uint64_t logical=(uint64_t)blocks*KUI_DATA_BYTES;
    uint64_t position=24u+4u*((uint64_t)blocks+1u);
    /* Even an incompressible image must fit the 31-bit offset field, align=0. */
    if(position+logical>=UINT64_C(0x80000000)) return KUI_EXPORT_FAILED;
    if(format==KUI_CAPTURE_FORMAT_ZSO && lzo_init()!=LZO_E_OK) return KUI_EXPORT_FAILED;
    uint8_t header[24]={0};memcpy(header,format==KUI_CAPTURE_FORMAT_CSO?"CISO":"ZISO",4);
    put32(header+4,24u);for(unsigned i=0;i<8;i++) header[8u+i]=(uint8_t)(logical>>(8u*i));
    put32(header+16,KUI_DATA_BYTES);header[20]=1u;
    if(!io->write(io->ctx,0,header,sizeof(header))) return KUI_EXPORT_FAILED;
    memset(chunk,0,sizeof(chunk));
    for(uint64_t offset=24u;offset<position;) {
        if(stopped(io)) return KUI_EXPORT_STOPPED;
        size_t n=position-offset>sizeof(chunk)?sizeof(chunk):(size_t)(position-offset);
        if(!io->write(io->ctx,offset,chunk,n)) return KUI_EXPORT_FAILED;
        offset+=n;
    }
    uint32_t source_crc=0;struct kui_sha256 source_sha;kui_sha256_init(&source_sha);
    for(uint32_t block=0;block<blocks;block++) {
        if(stopped(io)) return KUI_EXPORT_STOPPED;
        if(!source_sector(io,track,block)) return KUI_EXPORT_FAILED;
        source_crc=kui_crc32(source_crc,raw,sizeof(raw));kui_sha256_update(&source_sha,raw,sizeof(raw));
        size_t n=0;
        if(format==KUI_CAPTURE_FORMAT_CSO) n=kui_export_deflate(cooked,sizeof(cooked),packed,sizeof(packed));
        else {
            lzo_uint written=sizeof(packed);
            if(lzo1x_1_compress(cooked,sizeof(cooked),packed,&written,lzo_work)!=LZO_E_OK)
                return KUI_EXPORT_FAILED;
            n=(size_t)written;
        }
        if(!n) return KUI_EXPORT_FAILED;
        bool plain=n>=sizeof(cooked);if(plain) n=sizeof(cooked);
        uint8_t entry[4];put32(entry,(uint32_t)position|(plain?UINT32_C(0x80000000):0u));
        if(!io->write(io->ctx,position,plain?cooked:packed,n) ||
           !io->write(io->ctx,24u+(uint64_t)block*4u,entry,sizeof(entry))) return KUI_EXPORT_FAILED;
        position+=n;
        if(io->progress) io->progress(io->ctx,(uint64_t)(block+1u)*KUI_DATA_BYTES,logical,false);
    }
    uint8_t digest[32];kui_sha256_digest(&source_sha,digest);
    if(source_crc!=state->track[track].crc32 || (!state->crc_only && memcmp(digest,state->track[track].sha256,32)))
        return KUI_EXPORT_FAILED;
    uint8_t end[4];put32(end,(uint32_t)position);
    if(!io->write(io->ctx,24u+(uint64_t)blocks*4u,end,4u) || !io->sync(io->ctx)) return KUI_EXPORT_FAILED;
    uint8_t read_header[24];
    if(!io->read(io->ctx,0,read_header,sizeof(read_header)) || memcmp(header,read_header,sizeof(header)))
        return KUI_EXPORT_FAILED;
    /* Read every on-card index and payload back, decode with the safe decoder,
     * and compare directly with the authoritative raw sector's data bytes. */
    uint64_t expected=24u+4u*((uint64_t)blocks+1u);
    source_crc=0;kui_sha256_init(&source_sha);
    for(uint32_t block=0;block<blocks;block++) {
        if(stopped(io)) return KUI_EXPORT_STOPPED;
        uint8_t indices[8];
        if(!io->read(io->ctx,24u+(uint64_t)block*4u,indices,sizeof(indices))) return KUI_EXPORT_FAILED;
        uint32_t first=get32(indices),last=get32(indices+4)&UINT32_C(0x7fffffff);
        uint64_t offset=first&UINT32_C(0x7fffffff);
        if(offset!=expected || last<=offset || last>position || last-offset>sizeof(packed)) return KUI_EXPORT_FAILED;
        size_t n=(size_t)(last-offset);
        if(!io->read(io->ctx,offset,packed,n)) return KUI_EXPORT_FAILED;
        if(first&UINT32_C(0x80000000)) {
            if(n!=sizeof(decoded)) return KUI_EXPORT_FAILED;
            memcpy(decoded,packed,sizeof(decoded));
        } else if(format==KUI_CAPTURE_FORMAT_CSO) {
            if(!kui_export_inflate(packed,n,decoded,sizeof(decoded))) return KUI_EXPORT_FAILED;
        } else {
            lzo_uint written=sizeof(decoded);
            if(lzo1x_decompress_safe(packed,n,decoded,&written,NULL)!=LZO_E_OK || written!=sizeof(decoded))
                return KUI_EXPORT_FAILED;
        }
        if(!source_sector(io,track,block) || memcmp(decoded,cooked,sizeof(cooked))) return KUI_EXPORT_FAILED;
        source_crc=kui_crc32(source_crc,raw,sizeof(raw));kui_sha256_update(&source_sha,raw,sizeof(raw));
        expected=last;
        if(io->progress) io->progress(io->ctx,(uint64_t)(block+1u)*KUI_DATA_BYTES,logical,true);
    }
    kui_sha256_digest(&source_sha,digest);
    if(expected!=position || source_crc!=state->track[track].crc32 ||
       (!state->crc_only && memcmp(digest,state->track[track].sha256,32))) return KUI_EXPORT_FAILED;
    report->bytes=position;report->logical_bytes=logical;report->data_track=plan->tracks[track].number;
    return KUI_EXPORT_COMPLETE;
}
enum kui_capture_export_result kui_capture_export_write(const struct kui_capture_plan *plan,
    const struct kui_checkpoint *state,enum kui_capture_format format,const struct kui_capture_export_io *io,
    struct kui_capture_export_report *report) {
    if(!report) return KUI_EXPORT_FAILED;
    memset(report,0,sizeof(*report));
    if(!io || !io->track_read || !io->read || !io->write || !io->sync ||
       !kui_capture_export_preflight(plan,state,format,NULL,0)) return KUI_EXPORT_FAILED;
    for(unsigned i=0;i<plan->count;i++)
        if(state->track[i].sectors!=plan->tracks[i].end-plan->tracks[i].start) return KUI_EXPORT_FAILED;
    enum kui_capture_export_result result=format==KUI_CAPTURE_FORMAT_CHD?
        kui_capture_chd_write(plan,state,io,report):ciso_write(plan,state,format,io,report);
    if(result!=KUI_EXPORT_COMPLETE) {
        memset(report,0,sizeof(*report));
        return result==KUI_EXPORT_FAILED && stopped(io)?KUI_EXPORT_STOPPED:result;
    }
    /* Hash the complete finalized container, including the header/map/metadata.
     * These hashes are independent of the captured track digests. */
    struct kui_sha256 hash;kui_sha256_init(&hash);uint32_t crc=0;
    for(uint64_t offset=0;offset<report->bytes;) {
        if(stopped(io)) {memset(report,0,sizeof(*report));return KUI_EXPORT_STOPPED;}
        size_t n=report->bytes-offset>sizeof(chunk)?sizeof(chunk):(size_t)(report->bytes-offset);
        if(!io->read(io->ctx,offset,chunk,n)) {memset(report,0,sizeof(*report));return KUI_EXPORT_FAILED;}
        kui_sha256_update(&hash,chunk,n);crc=kui_crc32(crc,chunk,n);offset+=n;
    }
    if(!io->sync(io->ctx)) {memset(report,0,sizeof(*report));return KUI_EXPORT_FAILED;}
    report->crc32=crc;kui_sha256_digest(&hash,report->sha256);return KUI_EXPORT_COMPLETE;
}
