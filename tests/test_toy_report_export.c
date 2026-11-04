/* SPDX-License-Identifier: GPL-3.0-only */
#define KUI_TOY_REPORT_EXPORT_HOST_TEST 1
#include "../src/loader/toy_report_export.c"
#include <assert.h>
#include <stdio.h>
extern uint32_t kui_report_file_descriptor_checksum(const struct kui_report_file_descriptor *);
static uint32_t sr,active;
static struct kui_loader_sd original;
static uint8_t disk[64][512];
static unsigned claims,releases,writes,reads,identities;
static bool fail_claim,mismatch;
uint32_t kui_toy_report_host_sr(void) {return sr;}
void kui_toy_report_host_set_sr(uint32_t value) {sr=value;}
uint32_t kui_toy_report_host_active(void) {return active;}
const struct kui_loader_sd *kui_toy_report_host_card(void) {return &original;}
static void context(void) {assert((sr&0x500000f0u)==0x400000f0u);}
int kui_report_sci_pio_init(struct kui_report_sci_pio *p) {context();memset(p,0,sizeof(*p));return 0;}
int kui_report_sci_pio_acquire(struct kui_report_sci_pio *p) {context();claims++;p->acquired=1;return fail_claim?1:0;}
int kui_report_sci_pio_release(struct kui_report_sci_pio *p) {context();releases++;p->acquired=0;return 0;}
bool kui_report_sci_pio_healthy(const struct kui_report_sci_pio *p) {context();return p->acquired && !p->fault;}
static void select_card(void *p,bool selected) {(void)p;(void)selected;}
static uint8_t byte(void *p,uint8_t v,bool slow) {(void)p;(void)v;(void)slow;return 0xff;}
static uint32_t tick(void *p) {(void)p;return 0;}
int kui_report_sci_pio_bus(struct kui_report_sci_pio *p,struct kui_loader_sd_bus *b) {
    context();*b=(struct kui_loader_sd_bus){.ctx=p,.select=select_card,.transfer=byte,.ticks=tick};return 0;
}
int kui_report_sci_pio_identity(struct kui_report_sci_pio *p,struct kui_loader_sd *c,
    uint8_t cid[16],uint64_t *sectors,uint32_t *hc) {
    context();assert(p->acquired && c->bus.ctx==p && !c->bus.transfer_block && !c->bus.begin && !c->bus.end);
    identities++;memset(cid,mismatch?4:3,16u);*sectors=c->blocks;*hc=c->high_capacity;return 0;
}
enum kui_loader_sd_result kui_loader_sd_read(struct kui_loader_sd *c,uint32_t lba,uint32_t n,void *out) {
    context();assert(c!=&original && c->bus.ctx==&terminal.port && !c->bus.transfer_block);
    assert(lba>=100u && lba<164u && n==1u);reads++;memcpy(out,disk[lba-100u],512u);return KUI_LOADER_SD_OK;
}
enum kui_loader_sd_result kui_report_sd_write(struct kui_loader_sd *c,uint32_t lba,const uint8_t in[512]) {
    context();assert(c!=&original && c->bus.ctx==&terminal.port && !c->bus.transfer_block);
    assert(lba>=100u && lba<164u);writes++;memcpy(disk[lba-100u],in,512u);return KUI_LOADER_SD_OK;
}
enum kui_loader_sd_result kui_report_sd_sync(struct kui_loader_sd *c) {
    context();assert(c!=&original);return KUI_LOADER_SD_OK;
}
static void reset(void) {
    memset(&terminal,0,sizeof(terminal));memset(disk,0xa5,sizeof(disk));
    claims=releases=writes=reads=identities=0u;fail_claim=mismatch=false;active=0u;sr=0x700000f2u;
    original=(struct kui_loader_sd){.blocks=1000u,.high_capacity=true,.ready=true};
    struct kui_report_file_descriptor d={.magic=KUI_REPORT_FILE_MAGIC,.version=1u,
        .bytes=256u,.profile=KUI_TOY_DECISION_PROFILE,.card_sectors=1000u,.high_capacity=1u,.extent_count=1u,
        .extents={{100u,64u}}};memset(d.cid,3,16u);d.checksum=kui_report_file_descriptor_checksum(&d);
    memcpy((void *)&kui_toy_report_binding.file,&d,sizeof(d));
}
static void binding(void) {
    _Alignas(32) uint8_t blob[1024]={0};size_t at=999u;
    struct kui_toy_report_binding b={KUI_TOY_REPORT_BIND_MAGIC0,KUI_TOY_REPORT_BIND_MAGIC1,1u,KUI_TOY_DECISION_PROFILE,{0}};
    memcpy(blob+96u,&b,sizeof(b));assert(kui_toy_report_binding_offset(blob,sizeof(blob),KUI_TOY_DECISION_PROFILE,&at) && at==96u);
    memcpy(blob+416u,&b,sizeof(b));assert(!kui_toy_report_binding_offset(blob,sizeof(blob),KUI_TOY_DECISION_PROFILE,&at));
    memset(blob+416u,0,sizeof(b));blob[96u+16u]=1u;assert(!kui_toy_report_binding_offset(blob,sizeof(blob),KUI_TOY_DECISION_PROFILE,&at));
    memset(blob,0,sizeof(blob));memcpy(blob+100u,&b,sizeof(b));assert(!kui_toy_report_binding_offset(blob,sizeof(blob),KUI_TOY_DECISION_PROFILE,&at));
    assert(!kui_toy_report_binding_offset(blob,sizeof(blob),0u,&at));
}
int main(void) {
    binding();uint32_t words[16]={0};
    struct kui_toy_decision_report r={.magic=0x54444f31u,.version=1u,.enabled=KUI_TOY_DECISION_PROFILE-1u,.frozen=1u};
    reset();struct kui_loader_sd retained=original;
    assert(kui_toy_report_export_save(&r,words,true)==KUI_REPORT_FILE_OK);
    assert(sr==0x700000f2u && claims==1u && releases==1u && writes==65u && reads>=64u && identities>=65u);
    assert(!memcmp(&original,&retained,sizeof(original)));
    assert(!memcmp(disk[63],"KUI REPORT COMPLETE V1",22u));
    reset();assert(kui_toy_report_export_save(&r,words,false)==KUI_REPORT_FILE_NOT_FROZEN);assert(!claims && !writes);
    reset();r.frozen=0u;assert(kui_toy_report_export_save(&r,words,true)==KUI_REPORT_FILE_NOT_FROZEN);assert(!claims);r.frozen=1u;
    reset();active=1u;assert(kui_toy_report_export_save(&r,words,true)==KUI_REPORT_FILE_CLAIM);assert(!claims && !writes && sr==0x700000f2u);
    reset();fail_claim=true;assert(kui_toy_report_export_save(&r,words,true)==KUI_REPORT_FILE_CLAIM);assert(claims==1u && releases==1u && !writes && sr==0x700000f2u);
    reset();mismatch=true;assert(kui_toy_report_export_save(&r,words,true)==KUI_REPORT_FILE_IDENTITY);assert(claims==1u && releases==1u && !writes && sr==0x700000f2u);
    reset();sr=0x300000f2u;assert(kui_toy_report_export_save(&r,words,true)==KUI_REPORT_FILE_CLAIM);assert(!claims && sr==0x300000f2u);
    puts("PASS Toy terminal export: unique initialized binding, frozen/quiescent gates, owned byte PIO, partial-claim release, identity refusal and exact SR/card preservation");
}
