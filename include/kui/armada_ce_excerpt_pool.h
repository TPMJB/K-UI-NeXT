/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_ARMADA_CE_EXCERPT_POOL_H
#define KUI_ARMADA_CE_EXCERPT_POOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Independently authored metadata contract for the exact admitted CE
 * kernel. No owner executable, ARM driver or PCM bytes are compiled here.
 * Original whole-kernel SHA admission must precede this helper. The caller
 * applies the returned single-word plan before NK executes its first code;
 * this header never writes physical memory or changes CE/MMU state. */
#define KUI_ARMADA_CE_EXCERPT_ROMHDR_BYTES 84u
#if defined(KUI_WINCE_CDDA_WORMS) && KUI_WINCE_CDDA_WORMS == 1
#define KUI_ARMADA_CE_EXCERPT_ROMHDR UINT32_C(0x8c11de70)
#define KUI_ARMADA_CE_EXCERPT_ROMHDR_CRC UINT32_C(0xf7c57d43)
#define KUI_ARMADA_CE_EXCERPT_RAM_START UINT32_C(0x8c11f000)
#define KUI_ARMADA_CE_EXCERPT_RAM_FREE UINT32_C(0x8c129000)
#elif defined(KUI_WINCE_CDDA_WWP) && KUI_WINCE_CDDA_WWP == 1
#include "kui/wince_cdda_wwp_kernel.h"
#define KUI_ARMADA_CE_EXCERPT_ROMHDR KUI_WWP_CE_ROMHDR
#define KUI_ARMADA_CE_EXCERPT_ROMHDR_CRC KUI_WWP_CE_ROMHDR_CRC
#define KUI_ARMADA_CE_EXCERPT_RAM_START KUI_WWP_CE_RAM_START
#define KUI_ARMADA_CE_EXCERPT_RAM_FREE KUI_WWP_CE_RAM_FREE
#elif defined(KUI_WINCE_CDDA_BAM4) && KUI_WINCE_CDDA_BAM4 == 1
#define KUI_ARMADA_CE_EXCERPT_ROMHDR UINT32_C(0x8c1ce19c)
#define KUI_ARMADA_CE_EXCERPT_ROMHDR_CRC UINT32_C(0xe3b81ab3)
#define KUI_ARMADA_CE_EXCERPT_RAM_START UINT32_C(0x8c1cf000)
#define KUI_ARMADA_CE_EXCERPT_RAM_FREE UINT32_C(0x8c1dc000)
#else
#define KUI_ARMADA_CE_EXCERPT_ROMHDR UINT32_C(0x8c141420)
#define KUI_ARMADA_CE_EXCERPT_ROMHDR_CRC UINT32_C(0x7ddd77a1)
#define KUI_ARMADA_CE_EXCERPT_RAM_START UINT32_C(0x8c142000)
#define KUI_ARMADA_CE_EXCERPT_RAM_FREE UINT32_C(0x8c150000)
#endif
#define KUI_ARMADA_CE_EXCERPT_RAM_END_WORD (KUI_ARMADA_CE_EXCERPT_ROMHDR+28u)
#define KUI_ARMADA_CE_EXCERPT_STAGE_BEGIN UINT32_C(0x8ce10000)
#if defined(KUI_WINCE_CDDA_WORMS) && KUI_WINCE_CDDA_WORMS == 1
#define KUI_ARMADA_CE_EXCERPT_POOL_BEGIN UINT32_C(0x8ce70000)
#define KUI_ARMADA_CE_EXCERPT_POOL_END UINT32_C(0x8cef0000)
#define KUI_ARMADA_CE_EXCERPT_OEM_BYTES UINT32_C(0x00110000)
#elif defined(KUI_WINCE_CDDA_WWP) && KUI_WINCE_CDDA_WWP == 1
#define KUI_ARMADA_CE_EXCERPT_POOL_BEGIN KUI_WWP_CE_POOL_BEGIN
#define KUI_ARMADA_CE_EXCERPT_POOL_END KUI_WWP_CE_POOL_END
#define KUI_ARMADA_CE_EXCERPT_OEM_BYTES KUI_WWP_CE_OEM_BYTES
#else
#define KUI_ARMADA_CE_EXCERPT_POOL_BEGIN UINT32_C(0x8cf50000)
#define KUI_ARMADA_CE_EXCERPT_POOL_END UINT32_C(0x8cfd0000)
#define KUI_ARMADA_CE_EXCERPT_OEM_BYTES UINT32_C(0x00030000)
#endif
#define KUI_ARMADA_CE_EXCERPT_SEED_WORDS 16u
#define KUI_ARMADA_CE_EXCERPT_SEED_BYTES 64u
#define KUI_ARMADA_CE_EXCERPT_MAGIC UINT32_C(0x31584543) /* CEX1 */
#define KUI_ARMADA_CE_EXCERPT_VERSION 1u
#define KUI_ARMADA_CE_EXCERPT_SECTORS 150u
#define KUI_ARMADA_CE_EXCERPT_SECTOR_BYTES 2352u
/* Start two seconds into track4, rather than at its initial boundary. */
#define KUI_ARMADA_CE_EXCERPT_TRACK_START_LBA UINT32_C(149842)
#define KUI_ARMADA_CE_EXCERPT_START_LBA (KUI_ARMADA_CE_EXCERPT_TRACK_START_LBA+150u)
#define KUI_ARMADA_CE_EXCERPT_PCM_BEGIN (KUI_ARMADA_CE_EXCERPT_POOL_BEGIN+64u)
#define KUI_ARMADA_CE_EXCERPT_PCM_BYTES (150u*2352u)

enum kui_armada_ce_excerpt_seed_word {
    KUI_ARMADA_CE_EXCERPT_SEED_MAGIC,
    KUI_ARMADA_CE_EXCERPT_SEED_VERSION,
    KUI_ARMADA_CE_EXCERPT_SEED_PCM_BYTES,
    KUI_ARMADA_CE_EXCERPT_SEED_PCM_CRC32,
    KUI_ARMADA_CE_EXCERPT_SEED_SECTORS,
    KUI_ARMADA_CE_EXCERPT_SEED_START_LBA,
    KUI_ARMADA_CE_EXCERPT_SEED_RESERVED
};

enum kui_armada_ce_excerpt_pool_status {
    KUI_ARMADA_CE_EXCERPT_POOL_OK,
    KUI_ARMADA_CE_EXCERPT_POOL_ARGUMENT,
    KUI_ARMADA_CE_EXCERPT_POOL_HEADER,
    KUI_ARMADA_CE_EXCERPT_POOL_STAGE,
    KUI_ARMADA_CE_EXCERPT_POOL_DESTINATION,
    KUI_ARMADA_CE_EXCERPT_POOL_SEED,
    KUI_ARMADA_CE_EXCERPT_POOL_PCM_BYTES,
    KUI_ARMADA_CE_EXCERPT_POOL_PCM_CRC,
    KUI_ARMADA_CE_EXCERPT_POOL_CANCELLED,
    KUI_ARMADA_CE_EXCERPT_POOL_READ
};

struct kui_armada_ce_excerpt_pool_limits {
    uint32_t stage_end; /* Final linked code/file end, exclusive P1. */
    uint32_t bss_end;   /* Final linked state end, exclusive P1. */
    bool gd_destination_excluded; /* Integration has installed the guard. */
};
struct kui_armada_ce_excerpt_pool_inputs {
    const uint8_t *original_header;
    size_t header_bytes;
    const uint32_t *seed;
    size_t seed_bytes;
    uint32_t pcm_bytes; /* Independently measured source length/CRC. */
    uint32_t pcm_crc32;
    struct kui_armada_ce_excerpt_pool_limits limits;
    bool cancelled;
};
struct kui_armada_ce_excerpt_pool_plan {
    uint32_t patch_address, original_ram_end, reserved_ram_end;
    uint32_t pool_begin, pool_end, pcm_begin, pcm_bytes;
    uint32_t pcm_crc32, sectors, start_lba;
};
typedef bool (*kui_armada_ce_excerpt_pool_read32)(void *, uint32_t, uint32_t *);
typedef bool (*kui_armada_ce_excerpt_pool_cancel)(void *);

/* IEEE CRC32, byte order as stored on this little-endian target. A zero CRC
 * is legal: it is never used as a substitute for checking actual PCM. */
static inline uint32_t kui_armada_ce_excerpt_crc32_word(uint32_t crc,uint32_t word) {
    crc=~crc;
    for(unsigned byte=0;byte<4u;byte++) {
        crc^=(word>>(byte*8u))&255u;
        for(unsigned bit=0;bit<8u;bit++)
            crc=(crc>>1)^((0u-(crc&1u))&UINT32_C(0xedb88320));
    }
    return ~crc;
}
static inline uint32_t kui_armada_ce_excerpt_crc32(const uint8_t *bytes,size_t count) {
    uint32_t crc=UINT32_MAX;
    for(size_t i=0;i<count;i++) {
        crc^=bytes[i];
        for(unsigned bit=0;bit<8u;bit++)
            crc=(crc>>1)^((0u-(crc&1u))&UINT32_C(0xedb88320));
    }
    return ~crc;
}
static inline uint32_t kui_armada_ce_excerpt_le32(const uint8_t *p) {
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static inline bool kui_armada_ce_excerpt_seed_make(
    uint32_t out[KUI_ARMADA_CE_EXCERPT_SEED_WORDS],uint32_t pcm_crc32) {
    if(!out) return false;
    for(unsigned i=0;i<KUI_ARMADA_CE_EXCERPT_SEED_WORDS;i++) out[i]=0;
    out[KUI_ARMADA_CE_EXCERPT_SEED_MAGIC]=KUI_ARMADA_CE_EXCERPT_MAGIC;
    out[KUI_ARMADA_CE_EXCERPT_SEED_VERSION]=KUI_ARMADA_CE_EXCERPT_VERSION;
    out[KUI_ARMADA_CE_EXCERPT_SEED_PCM_BYTES]=KUI_ARMADA_CE_EXCERPT_PCM_BYTES;
    out[KUI_ARMADA_CE_EXCERPT_SEED_PCM_CRC32]=pcm_crc32;
    out[KUI_ARMADA_CE_EXCERPT_SEED_SECTORS]=KUI_ARMADA_CE_EXCERPT_SECTORS;
    out[KUI_ARMADA_CE_EXCERPT_SEED_START_LBA]=KUI_ARMADA_CE_EXCERPT_START_LBA;
    return true;
}
static inline bool kui_armada_ce_excerpt_seed_valid(
    const uint32_t seed[KUI_ARMADA_CE_EXCERPT_SEED_WORDS]) {
    if(!seed || seed[KUI_ARMADA_CE_EXCERPT_SEED_MAGIC]!=KUI_ARMADA_CE_EXCERPT_MAGIC ||
       seed[KUI_ARMADA_CE_EXCERPT_SEED_VERSION]!=KUI_ARMADA_CE_EXCERPT_VERSION ||
       seed[KUI_ARMADA_CE_EXCERPT_SEED_PCM_BYTES]!=KUI_ARMADA_CE_EXCERPT_PCM_BYTES ||
       seed[KUI_ARMADA_CE_EXCERPT_SEED_SECTORS]!=KUI_ARMADA_CE_EXCERPT_SECTORS ||
       seed[KUI_ARMADA_CE_EXCERPT_SEED_START_LBA]!=KUI_ARMADA_CE_EXCERPT_START_LBA)
        return false;
    for(unsigned i=KUI_ARMADA_CE_EXCERPT_SEED_RESERVED;i<KUI_ARMADA_CE_EXCERPT_SEED_WORDS;i++)
        if(seed[i]) return false;
    return true;
}
static inline bool kui_armada_ce_excerpt_seed_matches(
    const uint32_t seed[KUI_ARMADA_CE_EXCERPT_SEED_WORDS],uint32_t bytes,uint32_t crc32) {
    return kui_armada_ce_excerpt_seed_valid(seed) &&
        bytes==KUI_ARMADA_CE_EXCERPT_PCM_BYTES &&
        seed[KUI_ARMADA_CE_EXCERPT_SEED_PCM_CRC32]==crc32;
}

/* Call only on final physical main-RAM backing, after CE virtual translation.
 * Both cached/uncached aliases and a physical address identify the same pool.
 * Unknown segments, zero lengths and arithmetic overflow fail closed. */
static inline bool kui_armada_ce_excerpt_pool_destination_safe(uint32_t address,uint32_t bytes) {
    uint32_t segment=address&UINT32_C(0xe0000000);
    uint32_t physical=address&UINT32_C(0x1fffffff);
    if((segment!=0 && segment!=UINT32_C(0x80000000) && segment!=UINT32_C(0xa0000000)) ||
       !bytes || physical<UINT32_C(0x0c000000) || physical>=UINT32_C(0x0d000000) ||
       bytes>UINT32_C(0x0d000000)-physical) return false;
    uint32_t begin=KUI_ARMADA_CE_EXCERPT_POOL_BEGIN&UINT32_C(0x1fffffff);
    uint32_t end=KUI_ARMADA_CE_EXCERPT_POOL_END&UINT32_C(0x1fffffff);
    return physical>=end || physical+bytes<=begin;
}
static inline enum kui_armada_ce_excerpt_pool_status kui_armada_ce_excerpt_pool_limits_valid(
    const struct kui_armada_ce_excerpt_pool_limits *limits) {
    if(!limits) return KUI_ARMADA_CE_EXCERPT_POOL_ARGUMENT;
    if(limits->stage_end<KUI_ARMADA_CE_EXCERPT_STAGE_BEGIN ||
       limits->stage_end>KUI_ARMADA_CE_EXCERPT_POOL_BEGIN ||
       limits->bss_end<limits->stage_end || limits->bss_end>=KUI_ARMADA_CE_EXCERPT_POOL_BEGIN)
        return KUI_ARMADA_CE_EXCERPT_POOL_STAGE;
    if(!limits->gd_destination_excluded) return KUI_ARMADA_CE_EXCERPT_POOL_DESTINATION;
    return KUI_ARMADA_CE_EXCERPT_POOL_OK;
}
static inline bool kui_armada_ce_excerpt_pool_header_valid(const uint8_t *header,size_t bytes) {
    return header && bytes==KUI_ARMADA_CE_EXCERPT_ROMHDR_BYTES &&
        kui_armada_ce_excerpt_crc32(header,bytes)==KUI_ARMADA_CE_EXCERPT_ROMHDR_CRC &&
        kui_armada_ce_excerpt_le32(header+20u)==KUI_ARMADA_CE_EXCERPT_RAM_START &&
        kui_armada_ce_excerpt_le32(header+24u)==KUI_ARMADA_CE_EXCERPT_RAM_FREE &&
        kui_armada_ce_excerpt_le32(header+28u)==KUI_ARMADA_CE_EXCERPT_POOL_END &&
        kui_armada_ce_excerpt_le32(header+60u)==KUI_ARMADA_CE_EXCERPT_POOL_END &&
        kui_armada_ce_excerpt_le32(header+64u)==KUI_ARMADA_CE_EXCERPT_OEM_BYTES;
}
/* Every rejection leaves out unchanged. No caller memory, PCM, original
 * header or hardware state is changed. Cancellation precedes optional reads. */
static inline enum kui_armada_ce_excerpt_pool_status kui_armada_ce_excerpt_pool_prepare(
    const struct kui_armada_ce_excerpt_pool_inputs *in,struct kui_armada_ce_excerpt_pool_plan *out) {
    if(!in || !out) return KUI_ARMADA_CE_EXCERPT_POOL_ARGUMENT;
    if(in->cancelled) return KUI_ARMADA_CE_EXCERPT_POOL_CANCELLED;
    enum kui_armada_ce_excerpt_pool_status status=kui_armada_ce_excerpt_pool_limits_valid(&in->limits);
    if(status!=KUI_ARMADA_CE_EXCERPT_POOL_OK) return status;
    if(!kui_armada_ce_excerpt_pool_header_valid(in->original_header,in->header_bytes))
        return KUI_ARMADA_CE_EXCERPT_POOL_HEADER;
    if(in->seed_bytes!=KUI_ARMADA_CE_EXCERPT_SEED_BYTES || !kui_armada_ce_excerpt_seed_valid(in->seed))
        return KUI_ARMADA_CE_EXCERPT_POOL_SEED;
    if(in->pcm_bytes!=KUI_ARMADA_CE_EXCERPT_PCM_BYTES) return KUI_ARMADA_CE_EXCERPT_POOL_PCM_BYTES;
    if(in->seed[KUI_ARMADA_CE_EXCERPT_SEED_PCM_CRC32]!=in->pcm_crc32)
        return KUI_ARMADA_CE_EXCERPT_POOL_PCM_CRC;
    const struct kui_armada_ce_excerpt_pool_plan plan={
        KUI_ARMADA_CE_EXCERPT_RAM_END_WORD,KUI_ARMADA_CE_EXCERPT_POOL_END,
        KUI_ARMADA_CE_EXCERPT_POOL_BEGIN,KUI_ARMADA_CE_EXCERPT_POOL_BEGIN,
        KUI_ARMADA_CE_EXCERPT_POOL_END,KUI_ARMADA_CE_EXCERPT_PCM_BEGIN,
        KUI_ARMADA_CE_EXCERPT_PCM_BYTES,in->pcm_crc32,KUI_ARMADA_CE_EXCERPT_SECTORS,
        KUI_ARMADA_CE_EXCERPT_START_LBA
    };
    *out=plan;
    return KUI_ARMADA_CE_EXCERPT_POOL_OK;
}

/* PRE-BOOT ONLY: never run this scan from a GD hook/masked runtime callback.
 * Read-only callback variant. Bounds are fixed:21 header words,16 seed words,
 * then exactly88,200 PCM words. Invalid headers/seeds fail before PCM reads;
 * cancellation or I/O failure at any point leaves out unchanged. */
static inline enum kui_armada_ce_excerpt_pool_status kui_armada_ce_excerpt_pool_capture(
    kui_armada_ce_excerpt_pool_read32 read32,kui_armada_ce_excerpt_pool_cancel cancel,
    void *context,const struct kui_armada_ce_excerpt_pool_limits *limits,
    struct kui_armada_ce_excerpt_pool_plan *out) {
    if(!read32 || !limits || !out) return KUI_ARMADA_CE_EXCERPT_POOL_ARGUMENT;
    if(cancel && cancel(context)) return KUI_ARMADA_CE_EXCERPT_POOL_CANCELLED;
    enum kui_armada_ce_excerpt_pool_status status=kui_armada_ce_excerpt_pool_limits_valid(limits);
    if(status!=KUI_ARMADA_CE_EXCERPT_POOL_OK) return status;
    uint8_t header[KUI_ARMADA_CE_EXCERPT_ROMHDR_BYTES];
    uint32_t seed[KUI_ARMADA_CE_EXCERPT_SEED_WORDS],word,crc=0;
    for(unsigned i=0;i<KUI_ARMADA_CE_EXCERPT_ROMHDR_BYTES/4u;i++) {
        if(cancel && cancel(context)) return KUI_ARMADA_CE_EXCERPT_POOL_CANCELLED;
        if(!read32(context,KUI_ARMADA_CE_EXCERPT_ROMHDR+i*4u,&word))
            return KUI_ARMADA_CE_EXCERPT_POOL_READ;
        for(unsigned byte=0;byte<4u;byte++) header[i*4u+byte]=(uint8_t)(word>>(8u*byte));
    }
    if(!kui_armada_ce_excerpt_pool_header_valid(header,sizeof(header)))
        return KUI_ARMADA_CE_EXCERPT_POOL_HEADER;
    for(unsigned i=0;i<KUI_ARMADA_CE_EXCERPT_SEED_WORDS;i++) {
        if(cancel && cancel(context)) return KUI_ARMADA_CE_EXCERPT_POOL_CANCELLED;
        if(!read32(context,KUI_ARMADA_CE_EXCERPT_POOL_BEGIN+i*4u,&seed[i]))
            return KUI_ARMADA_CE_EXCERPT_POOL_READ;
    }
    if(!kui_armada_ce_excerpt_seed_valid(seed)) return KUI_ARMADA_CE_EXCERPT_POOL_SEED;
    for(unsigned i=0;i<KUI_ARMADA_CE_EXCERPT_PCM_BYTES/4u;i++) {
        if(cancel && cancel(context)) return KUI_ARMADA_CE_EXCERPT_POOL_CANCELLED;
        if(!read32(context,KUI_ARMADA_CE_EXCERPT_PCM_BEGIN+i*4u,&word))
            return KUI_ARMADA_CE_EXCERPT_POOL_READ;
        crc=kui_armada_ce_excerpt_crc32_word(crc,word);
    }
    if(cancel && cancel(context)) return KUI_ARMADA_CE_EXCERPT_POOL_CANCELLED;
    const struct kui_armada_ce_excerpt_pool_inputs in={
        header,sizeof(header),seed,sizeof(seed),KUI_ARMADA_CE_EXCERPT_PCM_BYTES,crc,*limits,false
    };
    return kui_armada_ce_excerpt_pool_prepare(&in,out);
}

#endif
