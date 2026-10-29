/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_game_assign.h"
#include "kui/retail_image.h"
#include <string.h>

static const uint8_t assign_magic[8]={'K','U','I','V','M','U','A','1'};
static void assign_put32(uint8_t *p,uint32_t value) {
    for(unsigned i=0;i<4u;i++) p[i]=(uint8_t)(value>>(8u*i));
}
static uint32_t assign_get32(const uint8_t *p) {
    uint32_t value=0;for(unsigned i=0;i<4u;i++) value|=(uint32_t)p[i]<<(8u*i);return value;
}
static bool assign_zero(const void *data,size_t bytes) {
    const uint8_t *p=data;unsigned any=0;
    for(size_t i=0;i<bytes;i++) any|=p[i];
    return !any;
}
static bool assign_overlap(const void *a,size_t ab,const void *b,size_t bb) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return x<=y?y-x<ab:x-y<bb;
}
static bool assign_key(const char key[KUI_VMU_GAME_CARD_KEY_BYTES]) {
    char canonical[KUI_VMU_GAME_CARD_KEY_BYTES];
    return key && memchr(key,0,KUI_VMU_GAME_CARD_KEY_BYTES) &&
        kui_vmu_game_card_key(key,canonical) && !memcmp(key,canonical,sizeof(canonical));
}
static bool assign_entry(const struct kui_vmu_game_assignment *entry) {
    return assign_key(entry->key) && entry->pair>=1u && entry->pair<=9999u &&
        !assign_zero(entry->identity,sizeof(entry->identity));
}
static bool assign_table(const struct kui_vmu_game_assign_table *table) {
    if(!table || table->count>KUI_VMU_GAME_ASSIGN_MAX) return false;
    for(uint32_t i=0;i<table->count;i++) {
        if(!assign_entry(&table->entries[i])) return false;
        for(uint32_t j=0;j<i;j++) if(!memcmp(table->entries[i].key,table->entries[j].key,
            KUI_VMU_GAME_CARD_KEY_BYTES)) return false;
    }
    return true;
}
const struct kui_vmu_game_assignment *kui_vmu_game_assign_find(
    const struct kui_vmu_game_assign_table *table,const char key[KUI_VMU_GAME_CARD_KEY_BYTES]) {
    if(!table || table->count>KUI_VMU_GAME_ASSIGN_MAX || !assign_key(key)) return NULL;
    for(uint32_t i=0;i<table->count;i++)
        if(!memcmp(table->entries[i].key,key,KUI_VMU_GAME_CARD_KEY_BYTES)) return &table->entries[i];
    return NULL;
}
bool kui_vmu_game_assign_encode(const struct kui_vmu_game_assign_table *table,
    uint8_t out[KUI_VMU_GAME_ASSIGN_WIRE_BYTES]) {
    if(!out) return false;
    if(table && assign_overlap(table,sizeof(*table),out,KUI_VMU_GAME_ASSIGN_WIRE_BYTES)) return false;
    memset(out,0,KUI_VMU_GAME_ASSIGN_WIRE_BYTES);
    if(!assign_table(table) || !table->sequence) return false;
    memcpy(out,assign_magic,sizeof(assign_magic));
    assign_put32(out+8u,1u);assign_put32(out+12u,KUI_VMU_GAME_ASSIGN_WIRE_BYTES);
    assign_put32(out+16u,(uint32_t)table->sequence);assign_put32(out+20u,(uint32_t)(table->sequence>>32u));
    assign_put32(out+24u,table->count);assign_put32(out+28u,KUI_VMU_GAME_ASSIGN_ENTRY_BYTES);
    assign_put32(out+32u,KUI_VMU_GAME_ASSIGN_HEADER_BYTES);assign_put32(out+36u,KUI_VMU_GAME_ASSIGN_MAX);
    for(uint32_t i=0;i<table->count;i++) {
        uint8_t *p=out+KUI_VMU_GAME_ASSIGN_HEADER_BYTES+(size_t)i*KUI_VMU_GAME_ASSIGN_ENTRY_BYTES;
        memcpy(p,table->entries[i].key,KUI_VMU_GAME_CARD_KEY_BYTES);
        assign_put32(p+12u,table->entries[i].pair);memcpy(p+16u,table->entries[i].identity,16u);
    }
    uint32_t crc=kui_retail_crc32(0,out,508u);
    crc=kui_retail_crc32(crc,out+512u,KUI_VMU_GAME_ASSIGN_WIRE_BYTES-512u);
    assign_put32(out+508u,crc);return true;
}
bool kui_vmu_game_assign_decode(const void *wire,size_t bytes,struct kui_vmu_game_assign_table *table) {
    const uint8_t *p=wire;
    if(!table) return false;
    if(wire && assign_overlap(wire,bytes,table,sizeof(*table))) return false;
    memset(table,0,sizeof(*table));
    if(!p || bytes!=KUI_VMU_GAME_ASSIGN_WIRE_BYTES || memcmp(p,assign_magic,sizeof(assign_magic)) ||
       assign_get32(p+8u)!=1u || assign_get32(p+12u)!=KUI_VMU_GAME_ASSIGN_WIRE_BYTES ||
       assign_get32(p+28u)!=KUI_VMU_GAME_ASSIGN_ENTRY_BYTES ||
       assign_get32(p+32u)!=KUI_VMU_GAME_ASSIGN_HEADER_BYTES || assign_get32(p+36u)!=KUI_VMU_GAME_ASSIGN_MAX ||
       !assign_zero(p+40u,468u)) return false;
    uint32_t count=assign_get32(p+24u);
    uint64_t sequence=assign_get32(p+16u)|((uint64_t)assign_get32(p+20u)<<32u);
    if(!sequence || count>KUI_VMU_GAME_ASSIGN_MAX ||
       !assign_zero(p+512u+(size_t)count*32u,(KUI_VMU_GAME_ASSIGN_MAX-count)*32u)) return false;
    uint32_t crc=kui_retail_crc32(0,p,508u);
    crc=kui_retail_crc32(crc,p+512u,KUI_VMU_GAME_ASSIGN_WIRE_BYTES-512u);
    if(crc!=assign_get32(p+508u)) return false;
    /* Validate records without populating the output; a refusal leaves it empty. */
    for(uint32_t i=0;i<count;i++) {
        const uint8_t *at=p+512u+(size_t)i*32u;
        struct kui_vmu_game_assignment entry;
        memcpy(entry.key,at,12u);entry.pair=assign_get32(at+12u);memcpy(entry.identity,at+16u,16u);
        if(!assign_entry(&entry)) return false;
        for(uint32_t j=0;j<i;j++) if(!memcmp(at,p+512u+(size_t)j*32u,12u)) return false;
    }
    table->sequence=sequence;table->count=count;
    for(uint32_t i=0;i<count;i++) {
        const uint8_t *at=p+512u+(size_t)i*32u;
        memcpy(table->entries[i].key,at,12u);table->entries[i].pair=assign_get32(at+12u);
        memcpy(table->entries[i].identity,at+16u,16u);
    }
    return true;
}
bool kui_vmu_game_assign_update(struct kui_vmu_game_assign_table *table,
    const struct kui_vmu_game_assignment *entries,size_t count) {
    if(!assign_table(table) || count>KUI_VMU_GAME_ASSIGN_MAX || (count && !entries) ||
       (count && assign_overlap(table,sizeof(*table),entries,count*sizeof(*entries)))) return false;
    uint32_t added=0;
    for(size_t i=0;i<count;i++) {
        if(!assign_entry(&entries[i])) return false;
        for(size_t j=0;j<i;j++) if(!memcmp(entries[i].key,entries[j].key,KUI_VMU_GAME_CARD_KEY_BYTES)) return false;
        if(!kui_vmu_game_assign_find(table,entries[i].key)) ++added;
    }
    if(added>KUI_VMU_GAME_ASSIGN_MAX-table->count) return false;
    for(size_t i=0;i<count;i++) {
        const struct kui_vmu_game_assignment *found=kui_vmu_game_assign_find(table,entries[i].key);
        uint32_t at=found?(uint32_t)(found-table->entries):table->count++;
        table->entries[at]=entries[i];
    }
    return true;
}
