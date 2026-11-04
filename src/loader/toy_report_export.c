/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/toy_report_export.h"
#if !KUI_TOY_TEXT_REPORT
#error Terminal text writer belongs only to the isolated text profile
#endif
#include "kui/toy_report_text.h"
#include "kui/report_sci_pio.h"
#include "kui/report_sd_storage.h"
#ifndef KUI_TOY_REPORT_EXPORT_HOST_TEST
#include "toy_pilot_resident_symbols.h"
#endif
#include <string.h>
#ifndef KUI_BUILD_ID
#define KUI_BUILD_ID "LOCAL"
#endif
/* Initialized data survives BSS initialization. A copied descriptor never
 * carries a stage address, filesystem state or controller callback. */
volatile struct kui_toy_report_binding kui_toy_report_binding
    __attribute__((aligned(32),section(".data.toy_report_binding")))={
    KUI_TOY_REPORT_BIND_MAGIC0,KUI_TOY_REPORT_BIND_MAGIC1,1u,KUI_TOY_DECISION_PROFILE,{0}};
static struct {
    struct kui_report_sci_pio port;
    struct kui_loader_sd card;
    bool stopped,initialized;
} terminal;
#ifdef KUI_TOY_REPORT_EXPORT_HOST_TEST
extern uint32_t kui_toy_report_host_sr(void);
extern void kui_toy_report_host_set_sr(uint32_t);
extern uint32_t kui_toy_report_host_active(void);
extern const struct kui_loader_sd *kui_toy_report_host_card(void);
#define read_sr kui_toy_report_host_sr
#define write_sr kui_toy_report_host_set_sr
#define low_active kui_toy_report_host_active
#define accepted_card kui_toy_report_host_card
#else
static uint32_t read_sr(void) {uint32_t sr;__asm__ __volatile__("stc sr,%0":"=r"(sr));return sr;}
static void write_sr(uint32_t sr) {__asm__ __volatile__("ldc %0,sr"::"r"(sr):"memory","t");}
static uint32_t low_active(void) {return *(volatile const uint32_t *)(uintptr_t)KUI_TOY_PILOT_LOW_ACTIVE;}
static const struct kui_loader_sd *accepted_card(void) {
    return (const struct kui_loader_sd *)(uintptr_t)KUI_TOY_PILOT_LOW_SCI_CARD;
}
#endif
static int begin(void *unused) {
    (void)unused;
    if(!terminal.stopped || terminal.initialized || low_active())return -1;
    terminal.initialized=true;
    const struct kui_loader_sd *accepted=accepted_card();
    if(!accepted->ready || accepted->slow || !accepted->blocks)return -1;
    terminal.card=*accepted;
    if(kui_report_sci_pio_init(&terminal.port) ||
       kui_report_sci_pio_bus(&terminal.port,&terminal.card.bus))return -1;
    int result=kui_report_sci_pio_acquire(&terminal.port);
    if(result) {
        if(terminal.port.acquired)(void)kui_report_sci_pio_release(&terminal.port);
        return -1;
    }
    return 0;
}
static int identity(void *unused,uint8_t cid[16],uint64_t *sectors,uint32_t *hc) {
    (void)unused;return kui_report_sci_pio_identity(&terminal.port,&terminal.card,cid,sectors,hc);
}
static int read_sector(void *unused,uint32_t lba,uint8_t out[512]) {
    (void)unused;
    return kui_loader_sd_read(&terminal.card,lba,1u,out)==KUI_LOADER_SD_OK &&
        kui_report_sci_pio_healthy(&terminal.port)?0:-1;
}
static int write_sector(void *unused,uint32_t lba,const uint8_t in[512]) {
    (void)unused;
    return kui_report_sd_write(&terminal.card,lba,in)==KUI_LOADER_SD_OK &&
        kui_report_sci_pio_healthy(&terminal.port)?0:-1;
}
static int sync_card(void *unused) {
    (void)unused;
    return kui_report_sd_sync(&terminal.card)==KUI_LOADER_SD_OK &&
        kui_report_sci_pio_healthy(&terminal.port)?0:-1;
}
static int end(void *unused,bool healthy) {
    (void)unused;
    bool before=kui_report_sci_pio_healthy(&terminal.port);
    if(!healthy)terminal.card.ready=false;
    int released=kui_report_sci_pio_release(&terminal.port);
    return before && !released && !terminal.port.acquired &&
        !terminal.port.fault?0:-1;
}
int kui_toy_report_export_save(const struct kui_toy_decision_report *report,
    const uint32_t stopped_words[16],bool quiesced) {
    if(!report || !stopped_words || !report->frozen || !quiesced ||
       kui_toy_report_binding.magic0!=KUI_TOY_REPORT_BIND_MAGIC0 ||
       kui_toy_report_binding.magic1!=KUI_TOY_REPORT_BIND_MAGIC1 ||
       kui_toy_report_binding.version!=1u ||
       kui_toy_report_binding.profile!=1u+report->enabled)
        return KUI_REPORT_FILE_NOT_FROZEN;
    terminal.stopped=true;
    const struct kui_report_file_descriptor *file=(const struct kui_report_file_descriptor *)(const void *)&kui_toy_report_binding.file;
    if(!kui_report_file_descriptor_valid(file) || file->profile!=kui_toy_report_binding.profile)
        return KUI_REPORT_FILE_ARGUMENT;
    const struct kui_toy_report_text text={report,stopped_words,KUI_BUILD_ID};
    const struct kui_report_file_io io={NULL,begin,identity,read_sector,write_sector,sync_card,end};
    /* The permanently stopped terminal already owns the guarded4KiB GD
     * stack. Its audited call chain leaves room for these two sector buffers;
     * no gameplay/IRQ memory or suspended worker frame is reused. */
    struct kui_report_file_work work;
    uint32_t sr=read_sr();
    if(!(sr&UINT32_C(0x40000000)))return KUI_REPORT_FILE_CLAIM;
    /* This is a permanently stopped game. Retain MD and all other bits,
     * mask interrupts, and clear only BL for SCI byte I/O; restore exactly. */
    write_sr((sr|UINT32_C(0x000000f0))&~UINT32_C(0x10000000));
    int result=kui_report_file_save_source(file,true,kui_toy_report_text_length(&text),
        kui_toy_report_text_read,(void *)&text,&io,&work);
    write_sr(sr);return result;
}
