/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_image_merge.h"
#include "kui/vmu_image_pack.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

enum { BYTES=131072, BLOCK=512 };
static _Alignas(max_align_t) uint8_t base[BYTES],incoming[BYTES],out[BYTES],before[BYTES];
static uint8_t base_before[BYTES],incoming_before[BYTES],payload[200*BLOCK];
static struct kui_vmu_image_merge_workspace work;
static struct kui_vmu_image_merge_report report;
static struct kui_vmu_image_workspace pack_work;
static struct kui_vmu_image_summary pack_summary;
static struct kui_vmu_image_file files[200];
static unsigned get16(const uint8_t *p) {return p[0]|((unsigned)p[1]<<8);}
static void put16(uint8_t *p,unsigned n) {p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8);}
static uint8_t *entry(uint8_t *image,unsigned n) {return image+(253u-n/16u)*BLOCK+n%16u*32u;}
static void blank(uint8_t *image) {
    for(unsigned i=0;i<256u;i++) assert(!kui_vmu_image_blank_block((uint16_t)i,image+i*BLOCK));
}
static void reset(void) {
    blank(base);blank(incoming);memset(out,0xa5,BYTES);memset(files,0,sizeof(files));
    for(unsigned i=0;i<sizeof(payload);i++) payload[i]=(uint8_t)(i*31u+i/BLOCK*19u);
}
static void file(unsigned n,const char *name,unsigned blocks,unsigned game,unsigned offset) {
    assert(n<200u && strlen(name)<=12u && offset+blocks<=200u);
    memset(files+n,0,sizeof(files[n]));files[n].entry[0]=game?0xcc:0x33;
    files[n].entry[1]=0x10; /* Preserve opaque protection, including CT2. */
    memcpy(files[n].entry+4,name,strlen(name));put16(files[n].entry+24,blocks);
    for(unsigned i=16;i<24u;i++) files[n].entry[i]=(uint8_t)(i+7u*n);
    files[n].entry[29]=0xe7;files[n].entry[31]=0x91;
    files[n].payload=payload+offset*BLOCK;files[n].bytes=blocks*BLOCK;
}
static void pack(uint8_t *image,unsigned n) {
    assert(!kui_vmu_image_pack(files,n,image,BYTES,&pack_work,&pack_summary));
}
static int merge(void) {
    memcpy(base_before,base,BYTES);memcpy(incoming_before,incoming,BYTES);
    int result=kui_vmu_image_merge(base,incoming,out,BYTES,&work,&report);
    assert(!memcmp(base_before,base,BYTES));assert(!memcmp(incoming_before,incoming,BYTES));
    return result;
}
static void reject(int wanted) {
    memcpy(before,out,BYTES);assert(merge()==wanted);assert(!memcmp(before,out,BYTES));
}
static void payload_equal(uint8_t *image,unsigned n,const void *expected,unsigned blocks) {
    unsigned at=get16(entry(image,n)+2);
    for(unsigned b=0;b<blocks;b++) {
        assert(at<200u);
        assert(!memcmp(image+at*BLOCK,(const uint8_t *)expected+b*BLOCK,BLOCK));
        at=get16(image+254u*BLOCK+at*2u);
    }
    assert(at==0xfffau);
}
static void preservation(void) {
    reset();file(0,"A.SAVE",3,0,0);file(1,"B.SAVE",2,0,3);pack(base,2);
    /* Fragment the first source chain independently of the packer. */
    memcpy(base+17u*BLOCK,base+198u*BLOCK,BLOCK);
    put16(base+254u*BLOCK+199u*2u,17u);put16(base+254u*BLOCK+17u*2u,197u);
    put16(base+254u*BLOCK+198u*2u,0xfffcu);
    file(0,"C.SAVE",2,0,5);pack(incoming,1);
    assert(merge()==0);assert(report.summary.files==3 && report.summary.used_blocks==7u);
    assert(report.added==1 && !report.duplicates);
    payload_equal(out,0,payload,3);payload_equal(out,1,payload+3u*BLOCK,2);
    payload_equal(out,2,payload+5u*BLOCK,2);
    for(unsigned n=0;n<3u;n++) {
        const uint8_t *original=n<2u?entry(base,n):entry(incoming,0);
        assert(!memcmp(entry(out,n),original,2u));assert(!memcmp(entry(out,n)+4,original+4,28u));
    }
    /* Stale free flash and inactive directory bytes are not transplanted. */
    for(unsigned j=0;j<BLOCK;j++) assert(out[100u*BLOCK+j]==0u);
}
static void duplicates(void) {
    reset();file(0,"A",2,0,0);file(1,"DUP",1,0,2);pack(base,2);
    file(0,"NEW",3,0,3);file(1,"DUP",1,0,2);pack(incoming,2);
    assert(get16(entry(base,1)+2)!=get16(entry(incoming,1)+2));
    assert(!merge());assert(report.duplicates==1 && report.added==1 && report.summary.files==3);
    incoming[get16(entry(incoming,1)+2)*BLOCK+81u]^=0x80u;
    reject(KUI_VMU_MERGE_NAME_CONFLICT);assert(!strcmp(report.conflict_name,"DUP"));
    incoming[get16(entry(incoming,1)+2)*BLOCK+81u]^=0x80u;
    entry(incoming,1)[20]^=1u;reject(KUI_VMU_MERGE_NAME_CONFLICT);
    entry(incoming,1)[20]^=1u;memset(entry(incoming,1)+7,' ',9u);
    reject(KUI_VMU_MERGE_NAME_CONFLICT); /* Same trimmed key, distinct metadata. */
    reset();file(0,"SAME",1,0,0);pack(base,1);file(1,"SAME",1,0,1);pack(incoming,2);
    reject(KUI_VMU_MERGE_INVALID);
    reset();file(0,"",1,0,0);pack(base,1);reject(KUI_VMU_MERGE_INVALID);
}
static void capacity_and_games(void) {
    reset();file(0,"BIG",199,0,0);pack(base,1);file(0,"LAST",1,0,199);pack(incoming,1);
    assert(!merge());assert(report.summary.used_blocks==200 && report.summary.free_blocks==0);
    file(0,"LAST",2,0,0);pack(incoming,1);reject(KUI_VMU_MERGE_FULL);
    reset();file(0,"GAME.A",2,1,0);pack(base,1);file(0,"GAME.B",3,1,2);pack(incoming,1);
    reject(KUI_VMU_MERGE_MINIGAME_CONFLICT);
    file(0,"GAME.A",2,1,0);pack(incoming,1);assert(!merge());assert(report.duplicates==1);
    reset();file(0,"DATA",2,0,0);pack(base,1);file(0,"GAME",3,1,2);pack(incoming,1);
    assert(!merge());assert(get16(entry(out,1)+2)==0u);
    payload_equal(out,0,payload,2);payload_equal(out,1,payload+2u*BLOCK,3);
    reset();assert(!merge());assert(!report.summary.files && report.summary.free_blocks==200);
}
static void invalid_and_aliases(void) {
    reset();file(0,"SAVE",1,0,0);pack(base,1);memcpy(incoming,base,BYTES);
    put16(base+254u*BLOCK+199u*2u,199u);reject(KUI_VMU_MERGE_INVALID);
    reset();file(0,"SAVE",1,0,0);pack(base,1);
    memcpy(before,out,BYTES);struct kui_vmu_image_merge_report prior;
    memset(&report,0x91,sizeof(report));prior=report;
    assert(kui_vmu_image_merge(base,incoming,out,BYTES-1u,&work,&report)==KUI_VMU_MERGE_ARGUMENT);
    assert(kui_vmu_image_merge(NULL,incoming,out,BYTES,&work,&report)==KUI_VMU_MERGE_ARGUMENT);
    assert(kui_vmu_image_merge(base,incoming,out,BYTES,NULL,&report)==KUI_VMU_MERGE_ARGUMENT);
    assert(kui_vmu_image_merge(base,incoming,out,BYTES,&work,NULL)==KUI_VMU_MERGE_ARGUMENT);
    assert(kui_vmu_image_merge(base,incoming,base,BYTES,&work,&report)==KUI_VMU_MERGE_ARGUMENT);
    assert(kui_vmu_image_merge(base,base,out,BYTES,&work,&report)==KUI_VMU_MERGE_ARGUMENT);
    assert(kui_vmu_image_merge(base,incoming,out,BYTES,(void *)out,&report)==KUI_VMU_MERGE_ARGUMENT);
    assert(kui_vmu_image_merge(base,incoming,out,BYTES,&work,(void *)(out+BYTES-1))==KUI_VMU_MERGE_ARGUMENT);
    assert(!memcmp(before,out,BYTES));assert(!memcmp(&prior,&report,sizeof(report)));
}
int main(void) {
    preservation();duplicates();capacity_and_games();invalid_and_aliases();
    puts("PASS lossless VMU merge, duplicate/conflict/capacity/geometry and alias guards");return 0;
}
