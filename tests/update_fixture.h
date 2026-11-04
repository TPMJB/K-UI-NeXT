/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_TEST_UPDATE_FIXTURE_H
#define KUI_TEST_UPDATE_FIXTURE_H
#include "kui/update.h"
#include "kui/update_handoff.h"
#include "kui/runtime.h"
#include "kui/hash.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#define UF_PAYLOAD_BYTES 512u
#define UF_FILE_BYTES (64u+UF_PAYLOAD_BYTES)
struct uf_package {uint8_t *data;size_t bytes;unsigned count;};
static inline void uf_put32(uint8_t *p,uint32_t n) {for(unsigned i=0;i<4u;i++)p[i]=(uint8_t)(n>>(8u*i));}
static inline void uf_put64(uint8_t *p,uint64_t n) {for(unsigned i=0;i<8u;i++)p[i]=(uint8_t)(n>>(8u*i));}
static inline void uf_hash(const void *p,size_t n,uint8_t out[32]) {
    struct kui_sha256 h;kui_sha256_init(&h);kui_sha256_update(&h,p,n);kui_sha256_digest(&h,out);
}
static inline uint8_t *uf_entry(struct uf_package *p,unsigned i) {return p->data+128u+128u*i;}
static inline uint8_t *uf_file(struct uf_package *p,unsigned i) {return p->data+128u+128u*p->count+UF_FILE_BYTES*i;}
static inline void uf_header_crc(struct uf_package *p) {uf_put32(p->data+124u,kui_crc32(0,p->data,124u));}
static inline void uf_manifest_hash(struct uf_package *p) {
    uf_hash(p->data+128u,128u*p->count,p->data+64u);uf_header_crc(p);
}
static inline void uf_file_hash(struct uf_package *p,unsigned i) {
    uf_hash(uf_file(p,i),UF_FILE_BYTES,uf_entry(p,i)+88u);uf_manifest_hash(p);
}
static inline struct uf_package uf_build(const char build[13],unsigned channel) {
    struct uf_package p={0};p.count=(unsigned)kui_update_catalog_count();assert(p.count && p.count<=64u);
    p.bytes=128u+p.count*(128u+UF_FILE_BYTES);p.data=calloc(1u,p.bytes);assert(p.data);
    memcpy(p.data,"KUIUPD1\0",8u);uf_put32(p.data+8u,1u);uf_put32(p.data+12u,128u);
    uf_put32(p.data+16u,p.count);uf_put32(p.data+20u,p.count*128u);
    uf_put64(p.data+24u,(uint64_t)p.count*UF_FILE_BYTES);memcpy(p.data+32u,build,12u);p.data[44u]=(uint8_t)channel;
    for(unsigned i=0;i<p.count;i++) {
        const char *path=kui_update_catalog_path(i);assert(path && strlen(path)<80u);
        uint8_t *entry=uf_entry(&p,i),*file=uf_file(&p,i),*body=file+64u;
        memcpy(entry,path,strlen(path));uf_put64(entry+80u,UF_FILE_BYTES);
        for(unsigned j=0;j<UF_PAYLOAD_BYTES;j++)body[j]=(uint8_t)(j*37u+i*19u+11u);
        if(!strcmp(path,"runtime.kui")) {
            static const uint8_t marker[16]=KUI_UPDATE_CAPABILITY_INITIALIZER;
            static const uint8_t handoff[KUI_UPDATE_HANDOFF_BYTES]=KUI_UPDATE_HANDOFF_INITIALIZER;
            memcpy(body+32u,marker,sizeof(marker));
            memcpy(body+128u,handoff,sizeof(handoff));
        }
        memcpy(file,"KUIRUN1\0",8u);uf_put32(file+8u,1u);uf_put32(file+12u,64u);uf_put32(file+16u,UF_PAYLOAD_BYTES);
        uf_put32(file+20u,KUI_RUNTIME_ADDRESS);uf_put32(file+24u,KUI_RUNTIME_ADDRESS);uf_put32(file+28u,512u);
        uf_put32(file+32u,kui_crc32(0,body,UF_PAYLOAD_BYTES));memcpy(file+40u,build,12u);
        uf_put32(file+60u,kui_crc32(0,file,60u));uf_hash(file,UF_FILE_BYTES,entry+88u);
    }
    uf_manifest_hash(&p);return p;
}
static inline enum kui_update_result uf_decode(struct uf_package *p,struct kui_update_package *out) {
    enum kui_update_result r=kui_update_header_decode(p->data,p->bytes,out);
    return r==KUI_UPDATE_OK?kui_update_manifest_decode(out,p->data+128u,p->count*128u):r;
}
static inline void uf_free(struct uf_package *p) {free(p->data);*p=(struct uf_package){0};}
#endif
