/* SPDX-License-Identifier: GPL-3.0-only */
/* Independently authored fixed-extent redo store. The VMU image stays a plain
 * 256-sector VMD; the adjacent sidecar is not part of that file's format. */
#include "kui/vmu_store.h"
#include "kui/retail_image.h"
#include <string.h>

#define MARKER_CRC_OFFSET 508u
static const uint8_t marker_magic[8]={'K','U','I','V','M','U','J','1'};
_Static_assert(sizeof(struct kui_vmu_store)<=2048u,"VMU store bounded RAM");
static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static uint64_t get64(const uint8_t *p) {
    return (uint64_t)get32(p)|(uint64_t)get32(p+4)<<32;
}
static void store_put32(uint8_t *p,uint32_t v) {
    for(unsigned i=0;i<4;i++) p[i]=(uint8_t)(v>>(8u*i));
}
static void put64(uint8_t *p,uint64_t v) {
    store_put32(p,(uint32_t)v); store_put32(p+4,(uint32_t)(v>>32));
}
static int zeroes(const uint8_t *p,size_t n) {
    while(n--) if(*p++) return 0;
    return 1;
}
static int extents_valid(const struct kui_vmu_store_extent *e,size_t count,
        uint64_t capacity,unsigned expected) {
    unsigned total=0;
    if(!e||!count||count>KUI_VMU_STORE_MAX_EXTENTS) return 0;
    for(size_t i=0;i<count;i++) {
        uint32_t n=e[i].sectors;
        if(!n||n>expected-total||n>capacity||e[i].first_lba>capacity-n) return 0;
        total+=n;
        for(size_t j=0;j<i;j++)
            if(e[i].first_lba<e[j].first_lba+e[j].sectors &&
               e[j].first_lba<e[i].first_lba+n) return 0;
    }
    return total==expected;
}
static int maps_valid(const struct kui_vmu_store_config *c) {
    if(!c->card_sectors || zeroes(c->image_id,sizeof(c->image_id)) ||
       !extents_valid(c->image_extents,c->image_extent_count,c->card_sectors,
                      KUI_VMU_STORE_BLOCKS) ||
       !extents_valid(c->journal_extents,c->journal_extent_count,c->card_sectors,
                      KUI_VMU_STORE_JOURNAL_SECTORS)) return 0;
    for(size_t i=0;i<c->image_extent_count;i++)
        for(size_t j=0;j<c->journal_extent_count;j++) {
            const struct kui_vmu_store_extent *a=c->image_extents+i,
                                              *b=c->journal_extents+j;
            if(a->first_lba<b->first_lba+b->sectors &&
               b->first_lba<a->first_lba+a->sectors) return 0;
        }
    return 1;
}
static uint32_t map_crc(const struct kui_vmu_store *s) {
    uint8_t wire[16];
    uint32_t crc=kui_retail_crc32(0,s->image_id,sizeof(s->image_id));
    put64(wire,s->card_sectors);
    store_put32(wire+8,(uint32_t)s->image_count);
    store_put32(wire+12,(uint32_t)s->journal_count);
    crc=kui_retail_crc32(crc,wire,sizeof(wire));
    for(unsigned kind=0;kind<2;kind++) {
        const struct kui_vmu_store_extent *e=kind?s->journal:s->image;
        size_t count=kind?s->journal_count:s->image_count;
        for(size_t i=0;i<count;i++) {
            put64(wire,e[i].first_lba); store_put32(wire+8,e[i].sectors);
            crc=kui_retail_crc32(crc,wire,12);
        }
    }
    return crc;
}
static uint64_t sector_lba(const struct kui_vmu_store_extent *e,size_t count,
        unsigned logical) {
    for(size_t i=0;i<count;i++) {
        if(logical<e[i].sectors) return e[i].first_lba+logical;
        logical-=e[i].sectors;
    }
    /* All callers validate their logical range and maps before I/O. */
    return UINT64_MAX;
}
static int io_error(struct kui_vmu_store *s) {
    s->ready=0; return KUI_VMU_STORE_IO;
}
static int write_sync(struct kui_vmu_store *s,uint64_t lba,const void *data) {
    if(s->io.write_sector(s->io.context,lba,data) || s->io.sync(s->io.context))
        return io_error(s);
    return KUI_VMU_STORE_OK;
}
static int recover(struct kui_vmu_store *s) {
    uint64_t marker_lba=sector_lba(s->journal,s->journal_count,1);
    if(s->io.read_sector(s->io.context,marker_lba,s->marker)) return io_error(s);
    /* Invalidation is durably ordered before new data, and commit markers
     * are durably ordered before base writes. Thus a torn marker cannot
     * accompany an in-progress base overwrite in this protocol. */
    if(get32(s->marker+MARKER_CRC_OFFSET)!=
       kui_retail_crc32(0,s->marker,MARKER_CRC_OFFSET)) return KUI_VMU_STORE_OK;
    if(memcmp(s->marker,marker_magic,sizeof(marker_magic)) ||
       get32(s->marker+8)!=1 || get32(s->marker+12)!=512 ||
       get32(s->marker+44)>=KUI_VMU_STORE_BLOCKS || get32(s->marker+52)!=1 ||
       !zeroes(s->marker+64,MARKER_CRC_OFFSET-64)) return KUI_VMU_STORE_CORRUPT;
    if(memcmp(s->marker+16,s->image_id,sizeof(s->image_id)) ||
       get64(s->marker+32)!=s->card_sectors ||
       get32(s->marker+40)!=s->mapping_crc) return KUI_VMU_STORE_IDENTITY;
    uint64_t data_lba=sector_lba(s->journal,s->journal_count,0);
    if(s->io.read_sector(s->io.context,data_lba,s->data)) return io_error(s);
    if(get32(s->marker+48)!=kui_retail_crc32(0,s->data,sizeof(s->data)))
        return KUI_VMU_STORE_CORRUPT;
    s->sequence=get64(s->marker+56);
    /* A reopen after an I/O error need not follow an actual power cycle.
     * A valid record read through the driver's cache might still be volatile.
     * Make that record durable before ANY base replay can start. */
    if(s->io.sync(s->io.context)) return io_error(s);
    return write_sync(s,sector_lba(s->image,s->image_count,get32(s->marker+44)),
                       s->data);
}
int kui_vmu_store_open(struct kui_vmu_store *s,const struct kui_vmu_store_io *io,
        const struct kui_vmu_store_config *c) {
    if(!s) return KUI_VMU_STORE_ARGUMENT;
    memset(s,0,sizeof(*s));
    if(!io||!c||!io->read_sector||!io->write_sector||!io->sync)
        return KUI_VMU_STORE_ARGUMENT;
    if(!maps_valid(c)) return KUI_VMU_STORE_MAPPING;
    s->io=*io; s->card_sectors=c->card_sectors;
    s->image_count=c->image_extent_count; s->journal_count=c->journal_extent_count;
    memcpy(s->image,c->image_extents,s->image_count*sizeof(*s->image));
    memcpy(s->journal,c->journal_extents,s->journal_count*sizeof(*s->journal));
    memcpy(s->image_id,c->image_id,sizeof(s->image_id));
    s->mapping_crc=map_crc(s);
    int result=recover(s);
    if(result==KUI_VMU_STORE_OK) s->ready=1;
    return result;
}
int kui_vmu_store_read(struct kui_vmu_store *s,unsigned block,void *sector) {
    if(!s||!sector||block>=KUI_VMU_STORE_BLOCKS) return KUI_VMU_STORE_ARGUMENT;
    if(!s->ready) return KUI_VMU_STORE_BLOCKED;
    uint64_t lba=sector_lba(s->image,s->image_count,block);
    if(s->io.read_sector(s->io.context,lba,sector)) return io_error(s);
    return KUI_VMU_STORE_OK;
}
int kui_vmu_store_write(struct kui_vmu_store *s,unsigned block,const void *sector) {
    if(!s||!sector||block>=KUI_VMU_STORE_BLOCKS) return KUI_VMU_STORE_ARGUMENT;
    if(!s->ready) return KUI_VMU_STORE_BLOCKED;
    memcpy(s->data,sector,sizeof(s->data));
    memset(s->marker,0,sizeof(s->marker));
    uint64_t marker_lba=sector_lba(s->journal,s->journal_count,1);
    int result=write_sync(s,marker_lba,s->marker);
    if(result) return result;
    result=write_sync(s,sector_lba(s->journal,s->journal_count,0),s->data);
    if(result) return result;
    memcpy(s->marker,marker_magic,sizeof(marker_magic));
    store_put32(s->marker+8,1); store_put32(s->marker+12,512);
    memcpy(s->marker+16,s->image_id,sizeof(s->image_id));
    put64(s->marker+32,s->card_sectors); store_put32(s->marker+40,s->mapping_crc);
    store_put32(s->marker+44,block);
    store_put32(s->marker+48,kui_retail_crc32(0,s->data,sizeof(s->data)));
    store_put32(s->marker+52,1);
    s->sequence=s->sequence==UINT64_MAX?1:s->sequence+1;
    put64(s->marker+56,s->sequence);
    store_put32(s->marker+MARKER_CRC_OFFSET,kui_retail_crc32(0,s->marker,MARKER_CRC_OFFSET));
    result=write_sync(s,marker_lba,s->marker);
    if(result) return result;
    /* Keep the complete redo record even after this succeeds. Its marker is
     * invalidated+synced before the next payload can overwrite this one. */
    return write_sync(s,sector_lba(s->image,s->image_count,block),s->data);
}
int kui_vmu_store_sync(struct kui_vmu_store *s) {
    if(!s) return KUI_VMU_STORE_ARGUMENT;
    if(!s->ready) return KUI_VMU_STORE_BLOCKED;
    if(s->io.sync(s->io.context)) return io_error(s);
    return KUI_VMU_STORE_OK;
}

int kui_vmu_store_checkpoint(struct kui_vmu_store *s) {
    int result=kui_vmu_store_sync(s);
    if(result) return result;
    memset(s->marker,0,sizeof(s->marker));
    return write_sync(s,sector_lba(s->journal,s->journal_count,1),s->marker);
}
