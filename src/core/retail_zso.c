/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/retail_zso.h"
#include <stdbool.h>
#include <string.h>

static uint32_t word(const uint8_t *p) {return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint64_t wide(const uint8_t *p) {return (uint64_t)word(p)|(uint64_t)word(p+4)<<32;}
static void put(uint8_t *p,uint32_t v) {for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(i*8));}
static void putwide(uint8_t *p,uint64_t v) {put(p,(uint32_t)v);put(p+4,(uint32_t)(v>>32));}
static bool zero(const void *data,size_t n) {const uint8_t *p=data;while(n--)if(*p++)return false;return true;}
static bool text(const char *p,size_t n,bool required) {
    if(required && !*p)return false;
    for(size_t i=0;i<n;i++) {if(!p[i])return zero(p+i,n-i);if((unsigned char)p[i]<32 || (unsigned char)p[i]>126)return false;}
    return false;
}
enum kui_game_result kui_retail_zso_image_check(const struct kui_retail_manifest *m,uint32_t lba,uint32_t count,enum kui_game_sector_format format) {
    if(!m || !count)return KUI_GAME_INVALID;
    if(format!=KUI_GAME_SECTOR_MODE1)return KUI_GAME_UNSUPPORTED;
    const struct kui_retail_track *t=&m->slots[0].track;
    if(count>KUI_RETAIL_IMAGE_MAX_SECTORS || lba<t->start_lba || lba>=t->end_lba || count>t->end_lba-lba)return KUI_GAME_RANGE;
    return KUI_GAME_OK;
}
enum kui_game_result kui_retail_zso_manifest_validate(const struct kui_retail_zso_manifest *map) {
    if(!map)return KUI_GAME_INVALID;
    const struct kui_retail_manifest *m=&map->manifest;const struct kui_zso_info *z=&map->zso;
    if(m->track_count!=1 || !m->extent_count || m->extent_count>KUI_RETAIL_ZSO_EXTENTS ||
       m->extent_count>=KUI_RETAIL_MANIFEST_SLOTS || m->storage_transport!=KUI_STORAGE_SCI || m->reader!=KUI_RETAIL_READER_STANDARD ||
       (m->flags&~(KUI_RETAIL_IMAGE_CD|KUI_RETAIL_IMAGE_SCRAMBLED|KUI_RETAIL_IMAGE_BOOT_CRC)) ||
       !(m->flags&KUI_RETAIL_IMAGE_BOOT_CRC) || (!(m->flags&KUI_RETAIL_IMAGE_CD) && (m->flags&KUI_RETAIL_IMAGE_SCRAMBLED)) ||
       !m->card_sectors || m->card_sectors>UINT64_C(0x100000000) || m->partition_start>=m->partition_end || m->partition_end>m->card_sectors ||
       !z->total_bytes || z->total_bytes>KUI_ZSO_MAX_BYTES || z->total_bytes%2048u || z->block_count!=z->total_bytes/2048u ||
       z->index_bytes!=(z->block_count+1u)*4u || z->file_bytes<KUI_ZSO_HEADER_BYTES+z->index_bytes || z->shift>KUI_ZSO_MAX_SHIFT ||
       z->codec>KUI_ZSO_DEFLATE || !(KUI_ZSO_CODECS&(1u<<z->codec)) ||
       !text(m->title,sizeof(m->title),false) || !text(m->product,sizeof(m->product),false) ||
       !text(m->bootfile,sizeof(m->bootfile),true) || !text(m->region,sizeof(m->region),false))return KUI_GAME_INVALID;
    const struct kui_retail_track *t=&m->slots[0].track;
    if(((m->flags&KUI_RETAIL_IMAGE_CD)!=0)!=(t->start_lba<45000u) ||
       t->start_lba>=KUI_GAME_LBA_LIMIT || z->block_count>KUI_GAME_LBA_LIMIT-t->start_lba || t->end_lba!=t->start_lba+z->block_count ||
       t->first_extent!=1 || t->extent_count!=m->extent_count || t->control!=(4u|KUI_RETAIL_TRACK_COOKED) || m->session_lba!=t->start_lba ||
       z->block_count<16 || !m->boot_bytes || m->boot_bytes>KUI_RETAIL_IMAGE_BOOT_MAX || m->boot_lba<t->start_lba ||
       m->boot_lba>=t->end_lba || (m->boot_bytes+2047u)/2048u>t->end_lba-m->boot_lba)return KUI_GAME_RANGE;
    uint32_t blocks=z->file_bytes/512u+(z->file_bytes%512u!=0),next=0;
    for(uint32_t i=1;i<=m->extent_count;i++) {
        const struct kui_retail_extent *e=&m->slots[i].extent;
        if(e->file_block!=next || !e->blocks || e->blocks>blocks-next || e->card_lba<m->partition_start ||
           (uint64_t)e->card_lba+e->blocks>m->partition_end)return KUI_GAME_RANGE;
        for(uint32_t j=1;j<i;j++) {const struct kui_retail_extent *q=&m->slots[j].extent;
            if((uint64_t)e->card_lba<(uint64_t)q->card_lba+q->blocks && (uint64_t)q->card_lba<(uint64_t)e->card_lba+e->blocks)return KUI_GAME_OVERLAP;}
        next+=e->blocks;
    }
    return next==blocks?KUI_GAME_OK:KUI_GAME_FILE_SIZE;
}
static uint32_t wire_crc(const uint8_t *wire) {static const uint8_t empty[4];uint32_t c=kui_retail_crc32(0,wire,16);c=kui_retail_crc32(c,empty,4);return kui_retail_crc32(c,wire+20,4096-20);}
enum kui_game_result kui_retail_zso_manifest_encode(const struct kui_retail_zso_manifest *map,uint8_t out[4096]) {
    if(!out)return KUI_GAME_INVALID;
    enum kui_game_result r=kui_retail_zso_manifest_validate(map);if(r!=KUI_GAME_OK)return r;
    const struct kui_retail_manifest *m=&map->manifest;const struct kui_zso_info *z=&map->zso;
    memset(out,0,4096);memcpy(out,z->codec==KUI_ZSO_DEFLATE?KUI_RETAIL_CSO_WIRE_MAGIC:
        z->codec==KUI_ZSO_LZO1X?KUI_RETAIL_LZO_WIRE_MAGIC:KUI_RETAIL_ZSO_WIRE_MAGIC,8);put(out+8,1);put(out+12,4096);
    put(out+20,1);put(out+24,m->extent_count);put(out+28,m->storage_transport);putwide(out+32,m->card_sectors);putwide(out+40,m->partition_start);putwide(out+48,m->partition_end);
    put(out+56,m->session_lba);put(out+60,m->boot_lba);put(out+64,m->boot_bytes);put(out+68,m->gdi_crc32);
    memcpy(out+72,m->title,128);memcpy(out+200,m->product,16);memcpy(out+216,m->bootfile,24);memcpy(out+240,m->region,16);
    put(out+256,m->boot_crc32);put(out+260,m->ip_crc32);put(out+264,m->reader);put(out+268,m->flags);
    put(out+272,z->file_bytes);put(out+276,z->total_bytes);put(out+280,z->block_count);put(out+284,z->index_bytes);put(out+288,z->shift);
    const struct kui_retail_track *t=&m->slots[0].track;put(out+320,t->start_lba);put(out+324,t->end_lba);out[328]=t->control;out[329]=t->extent_count;
    for(uint32_t i=1;i<=m->extent_count;i++) {uint8_t *p=out+320+i*12;const struct kui_retail_extent *e=&m->slots[i].extent;put(p,e->file_block);put(p+4,e->card_lba);put(p+8,e->blocks);}
    put(out+16,wire_crc(out));return KUI_GAME_OK;
}
enum kui_game_result kui_retail_zso_manifest_decode(const uint8_t wire[4096],struct kui_retail_zso_manifest *map) {
    if(!map)return KUI_GAME_INVALID;
    memset(map,0,sizeof(*map));
    if(!wire || word(wire+8)!=1 || word(wire+12)!=4096 || word(wire+16)!=wire_crc(wire) ||
       word(wire+20)!=1 || !word(wire+24) || word(wire+24)>KUI_RETAIL_ZSO_EXTENTS || !zero(wire+292,28) || !zero(wire+330,2))return KUI_GAME_INVALID;
    uint32_t used=1+word(wire+24);if(!zero(wire+320+used*12,4096-320-used*12))return KUI_GAME_INVALID;
    struct kui_retail_manifest *m=&map->manifest;struct kui_zso_info *z=&map->zso;
    if((KUI_ZSO_CODECS&1) && !memcmp(wire,KUI_RETAIL_ZSO_WIRE_MAGIC,8))z->codec=KUI_ZSO_LZ4;
    else if((KUI_ZSO_CODECS&2) && !memcmp(wire,KUI_RETAIL_LZO_WIRE_MAGIC,8))z->codec=KUI_ZSO_LZO1X;
    else if((KUI_ZSO_CODECS&4) && !memcmp(wire,KUI_RETAIL_CSO_WIRE_MAGIC,8))z->codec=KUI_ZSO_DEFLATE;
    else goto invalid;
    m->track_count=1;m->extent_count=word(wire+24);m->storage_transport=word(wire+28);m->card_sectors=wide(wire+32);m->partition_start=wide(wire+40);m->partition_end=wide(wire+48);
    m->session_lba=word(wire+56);m->boot_lba=word(wire+60);m->boot_bytes=word(wire+64);m->gdi_crc32=word(wire+68);
    memcpy(m->title,wire+72,128);memcpy(m->product,wire+200,16);memcpy(m->bootfile,wire+216,24);memcpy(m->region,wire+240,16);
    m->boot_crc32=word(wire+256);m->ip_crc32=word(wire+260);m->reader=word(wire+264);m->flags=word(wire+268);
    z->file_bytes=word(wire+272);z->total_bytes=word(wire+276);z->block_count=word(wire+280);z->index_bytes=word(wire+284);
    if(word(wire+288)>KUI_ZSO_MAX_SHIFT)goto invalid;
    z->shift=(uint8_t)word(wire+288);
    m->slots[0].track=(struct kui_retail_track){word(wire+320),word(wire+324),1,wire[329],wire[328]};
    for(uint32_t i=1;i<used;i++){const uint8_t *p=wire+320+i*12;m->slots[i].extent=(struct kui_retail_extent){word(p),word(p+4),word(p+8)};}
    enum kui_game_result r=kui_retail_zso_manifest_validate(map);if(r==KUI_GAME_OK)return r;
invalid:memset(map,0,sizeof(*map));return KUI_GAME_INVALID;
}
enum kui_game_result kui_retail_zso_image_init(struct kui_retail_zso_image *image,const struct kui_retail_zso_manifest *map,kui_retail_read_block read,void *context) {
    if(!image)return KUI_GAME_INVALID;
    memset(image,0,sizeof(*image));if(!read)return KUI_GAME_INVALID;
    enum kui_game_result r=kui_retail_zso_manifest_validate(map);if(r!=KUI_GAME_OK)return r;
    image->map=map;image->read_block=read;image->context=context;return KUI_GAME_OK;
}
static enum kui_zso_result file_read(void *context,uint32_t offset,void *output,size_t bytes) {
    struct kui_retail_zso_image *image=context;const struct kui_retail_zso_manifest *map=image->map;
    if(offset>map->zso.file_bytes || bytes>map->zso.file_bytes-offset)return KUI_ZSO_RANGE;
    uint8_t *out=output;
    while(bytes) {
        uint32_t block=offset/512u,inside=offset%512u;const struct kui_retail_extent *e=NULL;
        for(uint32_t i=1;i<=map->manifest.extent_count;i++) {const struct kui_retail_extent *candidate=&map->manifest.slots[i].extent;
            if(block>=candidate->file_block && block-candidate->file_block<candidate->blocks){e=candidate;break;}}
        if(!e)return KUI_ZSO_RANGE;
        uint32_t lba=e->card_lba+block-e->file_block;
        if(!image->cache_valid || image->cached_lba!=lba) {
            image->cache_valid=0;
            uint32_t available=(uint32_t)((bytes+inside+511u)/512u),left=e->blocks-(block-e->file_block);
            if(available>left)available=left;
            int failed=image->read_run?image->read_run(image->context,lba,available,image->block):image->read_block(image->context,lba,image->block);
            if(failed)return KUI_ZSO_IO;
            image->cached_lba=lba;image->cache_valid=1;image->blocks_read++;
        }
        size_t take=512u-inside;if(take>bytes)take=bytes;
        memcpy(out,image->block+inside,take);out+=take;offset+=(uint32_t)take;bytes-=take;
    }
    return KUI_ZSO_OK;
}
enum kui_game_result kui_retail_zso_verify_source(struct kui_retail_zso_image *image) {
    if(!image || !image->map || !image->read_block)return KUI_GAME_INVALID;
    uint8_t header[KUI_ZSO_HEADER_BYTES];struct kui_zso_info actual;
    enum kui_zso_result z=file_read(image,0,header,sizeof(header));
    if(z==KUI_ZSO_OK)z=kui_zso_header_parse_codec(header,image->map->zso.file_bytes,(enum kui_zso_codec)image->map->zso.codec,&actual);
    const struct kui_zso_info *expected=&image->map->zso;
    if(z==KUI_ZSO_OK && (actual.file_bytes!=expected->file_bytes || actual.total_bytes!=expected->total_bytes || actual.block_count!=expected->block_count || actual.index_bytes!=expected->index_bytes || actual.shift!=expected->shift))z=KUI_ZSO_INVALID;
    if(z==KUI_ZSO_OK)z=kui_zso_validate_index(expected,file_read,image);
    if(z!=KUI_ZSO_OK)image->cache_valid=0;
    return z==KUI_ZSO_OK?KUI_GAME_OK:z==KUI_ZSO_IO?KUI_GAME_IO:KUI_GAME_INVALID;
}
enum kui_game_result kui_retail_zso_image_read(struct kui_retail_zso_image *image,uint32_t lba,uint32_t count,enum kui_game_sector_format format,void *out,size_t capacity) {
    if(!image || !image->map || !image->read_block || !out)return KUI_GAME_INVALID;
    enum kui_game_result r=kui_retail_zso_image_check(&image->map->manifest,lba,count,format);if(r!=KUI_GAME_OK)return r;
    if(capacity<(size_t)count*2048u)return KUI_GAME_RANGE;
    uint8_t *p=out;uint32_t first=lba-image->map->manifest.slots[0].track.start_lba;
    for(uint32_t i=0;i<count;i++) {
        enum kui_zso_result z=kui_zso_read_block(&image->map->zso,first+i,file_read,image,p+i*2048u);
        if(z!=KUI_ZSO_OK){image->cache_valid=0;return z==KUI_ZSO_IO?KUI_GAME_IO:KUI_GAME_INVALID;}
    }
    return KUI_GAME_OK;
}
