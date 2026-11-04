/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/report_file.h"
#include <string.h>

static uint32_t crc_byte(uint32_t crc,uint8_t value) {
    crc^=value;
    for(unsigned i=0;i<8u;i++)crc=(crc>>1)^((0u-(crc&1u))&UINT32_C(0xedb88320));
    return crc;
}
uint32_t kui_report_file_descriptor_checksum(const struct kui_report_file_descriptor *d) {
    const uint8_t *p=(const void *)d;uint32_t crc=UINT32_MAX;
    for(size_t i=0;i<sizeof(*d);i++)crc=crc_byte(crc,
        i>=offsetof(struct kui_report_file_descriptor,checksum) &&
        i<offsetof(struct kui_report_file_descriptor,checksum)+4u?0u:p[i]);
    return ~crc;
}
bool kui_report_file_descriptor_valid(const struct kui_report_file_descriptor *d) {
    if(!d || d->magic!=KUI_REPORT_FILE_MAGIC || d->version!=1u || d->bytes!=sizeof(*d) ||
       !d->profile || !d->card_sectors || d->card_sectors>(UINT64_C(1)<<32) ||
       d->high_capacity>1u || (!d->high_capacity && d->card_sectors>(UINT64_C(1)<<23)) ||
       !d->extent_count || d->extent_count>KUI_REPORT_FILE_EXTENTS ||
       d->checksum!=kui_report_file_descriptor_checksum(d))return false;
    uint32_t zero=0u,ones=0u,total=0u;
    for(unsigned i=0;i<16u;i++){zero+=d->cid[i]==0u;ones+=d->cid[i]==0xffu;}
    if(zero==16u || ones==16u)return false;
    for(unsigned i=0;i<19u;i++)if(d->reserved[i])return false;
    for(unsigned i=0;i<KUI_REPORT_FILE_EXTENTS;i++) {
        const struct kui_report_file_extent *e=&d->extents[i];
        if(i>=d->extent_count) {if(e->lba || e->blocks)return false;continue;}
        if(!e->lba || !e->blocks || e->lba>=d->card_sectors ||
           e->blocks>d->card_sectors-e->lba || e->blocks>64u-total)return false;
        total+=e->blocks;
        for(unsigned j=0;j<i;j++) {
            const struct kui_report_file_extent *a=&d->extents[j];
            if((uint64_t)e->lba<a->lba+(uint64_t)a->blocks &&
               (uint64_t)a->lba<e->lba+(uint64_t)e->blocks)return false;
        }
        if(i && (uint64_t)d->extents[i-1u].lba+d->extents[i-1u].blocks==e->lba)return false;
    }
    return total==KUI_REPORT_FILE_BYTES/512u;
}
static bool overlap(const void *a,size_t n,const void *b,size_t m) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return x>UINTPTR_MAX-n || y>UINTPTR_MAX-m || (x<y+m && y<x+n);
}
static bool lba_at(const struct kui_report_file_descriptor *d,unsigned logical,uint32_t *out) {
    for(unsigned i=0;i<d->extent_count;i++) {
        if(logical<d->extents[i].blocks){*out=d->extents[i].lba+logical;return true;}
        logical-=d->extents[i].blocks;
    }
    return false;
}
static int identity(const struct kui_report_file_descriptor *d,const struct kui_report_file_io *io) {
    uint8_t cid[16];uint64_t sectors=0u;uint32_t hc=2u;
    return io->identity(io->context,cid,&sectors,&hc) || sectors!=d->card_sectors ||
        hc!=d->high_capacity || memcmp(cid,d->cid,sizeof(cid))?KUI_REPORT_FILE_IDENTITY:0;
}
static int write_one(const struct kui_report_file_descriptor *d,const struct kui_report_file_io *io,
    unsigned logical,const uint8_t sector[512]) {
    int result=identity(d,io);if(result)return result;
    uint32_t lba;
    if(!lba_at(d,logical,&lba))return KUI_REPORT_FILE_ARGUMENT;
    return io->write_sector(io->context,lba,sector)?KUI_REPORT_FILE_IO:0;
}
static int body_sector(uint8_t out[512],unsigned logical,size_t bytes,
    kui_report_file_read_text read_text,void *source,uint32_t *crc) {
    memset(out,' ',512u);size_t offset=(size_t)logical*512u;
    if(offset>=bytes)return 0;
    size_t n=bytes-offset;if(n>512u)n=512u;
    /* Zeroing actual text catches a callback that claims success without
     * filling the requested bytes; NUL is never valid report text. */
    memset(out,0,n);
    if(read_text(source,offset,out,n)!=n)return KUI_REPORT_FILE_SOURCE;
    for(size_t i=0;i<n;i++) {
        unsigned c=out[i];
        if((c<32u && c!='\n' && c!='\r' && c!='\t') || c>126u)return KUI_REPORT_FILE_SOURCE;
        if(crc)*crc=crc_byte(*crc,(uint8_t)c);
    }
    return 0;
}
static void hex(uint8_t *out,uint32_t value) {
    static const char digits[]="0123456789ABCDEF";
    for(unsigned i=0;i<8u;i++)out[i]=(uint8_t)digits[(value>>(28u-i*4u))&15u];
}
static void footer(uint8_t out[512],bool complete,uint32_t profile,size_t bytes,uint32_t crc) {
    static const char done[]="KUI REPORT COMPLETE V1\nPROFILE=";
    static const char pending[]="KUI REPORT INCOMPLETE V1\n";
    memset(out,' ',512u);
    if(!complete){memcpy(out,pending,sizeof(pending)-1u);return;}
    unsigned at=sizeof(done)-1u;memcpy(out,done,at);hex(out+at,profile);at+=8u;
    memcpy(out+at,"\nBYTES=",7u);at+=7u;hex(out+at,(uint32_t)bytes);at+=8u;
    memcpy(out+at,"\nCRC32=",7u);at+=7u;hex(out+at,crc);out[at+8u]='\n';
}
static int verify_one(const struct kui_report_file_descriptor *d,const struct kui_report_file_io *io,
    unsigned logical,struct kui_report_file_work *work) {
    uint32_t lba;
    if(!lba_at(d,logical,&lba) || io->read_sector(io->context,lba,work->check))return KUI_REPORT_FILE_IO;
    return memcmp(work->sector,work->check,512u)?KUI_REPORT_FILE_VERIFY:0;
}
int kui_report_file_save_source(const struct kui_report_file_descriptor *d,bool frozen,
    size_t bytes,kui_report_file_read_text read_text,void *source,
    const struct kui_report_file_io *io,struct kui_report_file_work *work) {
    if(!frozen)return KUI_REPORT_FILE_NOT_FROZEN;
    if(!kui_report_file_descriptor_valid(d) || !io || !work || !read_text ||
       bytes>KUI_REPORT_TEXT_BYTES || !io->begin || !io->identity || !io->read_sector ||
       !io->write_sector || !io->sync || !io->end ||
       overlap(d,sizeof(*d),work,sizeof(*work)) || overlap(io,sizeof(*io),work,sizeof(*work)))
        return KUI_REPORT_FILE_ARGUMENT;
    uint32_t crc=UINT32_MAX;
    for(unsigned logical=0u;(size_t)logical*512u<bytes;logical++) {
        int result=body_sector(work->sector,logical,bytes,read_text,source,&crc);
        if(result)return result;
    }
    crc=~crc;
    if(io->begin(io->context))return KUI_REPORT_FILE_CLAIM;
    /* Invalidate an older completion before touching its body. Interrupted
     * writes cannot leave a valid completion marker for a mixed report. */
    footer(work->sector,false,d->profile,bytes,crc);
    int result=write_one(d,io,63u,work->sector);
    if(!result && io->sync(io->context))result=KUI_REPORT_FILE_IO;
    if(!result)result=verify_one(d,io,63u,work);
    uint32_t written_crc=UINT32_MAX;
    for(unsigned logical=0u;!result && logical<63u;logical++) {
        result=body_sector(work->sector,logical,bytes,read_text,source,&written_crc);
        if(!result)result=write_one(d,io,logical,work->sector);
    }
    if(!result && ~written_crc!=crc)result=KUI_REPORT_FILE_SOURCE;
    if(!result && io->sync(io->context))result=KUI_REPORT_FILE_IO;
    for(unsigned logical=0u;!result && logical<63u;logical++) {
        result=body_sector(work->sector,logical,bytes,read_text,source,NULL);
        if(!result)result=verify_one(d,io,logical,work);
    }
    if(!result) {
        footer(work->sector,true,d->profile,bytes,crc);result=write_one(d,io,63u,work->sector);
        if(!result && io->sync(io->context))result=KUI_REPORT_FILE_IO;
        if(!result)result=verify_one(d,io,63u,work);
    }
    int released=io->end(io->context,result==0);
    return released?KUI_REPORT_FILE_RELEASE:result;
}
struct string_source {const char *text;size_t bytes;};
static size_t string_read(void *context,size_t offset,uint8_t *out,size_t bytes) {
    const struct string_source *s=context;
    if(offset>s->bytes || bytes>s->bytes-offset)return SIZE_MAX;
    memcpy(out,s->text+offset,bytes);return bytes;
}
int kui_report_file_save(const struct kui_report_file_descriptor *d,bool frozen,
    const char *text,size_t bytes,const struct kui_report_file_io *io,struct kui_report_file_work *work) {
    if(!frozen)return KUI_REPORT_FILE_NOT_FROZEN;
    if((!text && bytes) || (bytes && ((work && overlap(text,bytes,work,sizeof(*work))) ||
       (d && overlap(text,bytes,d,sizeof(*d))) || (io && overlap(text,bytes,io,sizeof(*io))))))
        return KUI_REPORT_FILE_ARGUMENT;
    struct string_source source={text,bytes};
    int result=kui_report_file_save_source(d,frozen,bytes,string_read,&source,io,work);
    return result==KUI_REPORT_FILE_SOURCE?KUI_REPORT_FILE_ARGUMENT:result;
}
