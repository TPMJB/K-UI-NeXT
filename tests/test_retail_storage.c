/* SPDX-License-Identifier: GPL-3.0-only */
#include "retail_storage.h"
#include "sci_sd_bus.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const struct kui_loader_sd_bus resident_sci={0};
static const struct kui_ata_bus resident_ata={0};
static unsigned scif_claims,sci_claims,ata_reads,ata_stops,sd_reads,stops,releases;
static uint64_t ata_lba;
static uint32_t ata_available;
static void *ata_output;
static bool ata_read_ok=true,ata_stop_ok=true;
static bool healthy=true;
const struct kui_loader_sd_bus *kui_sci_sd_bus(void) { return &resident_sci; }
const struct kui_ata_bus *kui_ata_native_bus(void) { return &resident_ata; }
bool kui_sci_sd_healthy(void) { return healthy; }
enum kui_loader_sd_result kui_sci_sd_acquire(void) { ++sci_claims; return KUI_LOADER_SD_OK; }
void kui_sci_sd_release(void) { ++releases; }
enum kui_loader_sd_result kui_retail_sd_acquire(void) { ++scif_claims; return KUI_LOADER_SD_OK; }
void kui_retail_sd_release(void) { ++releases; }
enum kui_loader_sd_result kui_retail_sd_adopt(struct kui_loader_sd *s,const struct kui_loader_sd *p) {
    *s=*p; memset(&s->bus,0,sizeof(s->bus)); return KUI_LOADER_SD_OK;
}
enum kui_loader_sd_result kui_retail_sd_init(struct kui_loader_sd *s) {
    s->ready=true;s->blocks=2000;return KUI_LOADER_SD_OK;
}
enum kui_loader_sd_result kui_loader_sd_init_bus(struct kui_loader_sd *s,const struct kui_loader_sd_bus *b) {
    s->ready=true;s->blocks=3000;s->bus=*b;return KUI_LOADER_SD_OK;
}
bool kui_ata_init(struct kui_ata *a,const struct kui_ata_bus *b) { a->ready=true;a->sectors=4000;a->bus=b;return true; }
bool kui_ata_read_run(struct kui_ata *a,uint64_t lba,uint32_t available,void *out) {
    assert(a->bus==&resident_ata && a->ready && available && out);
    ata_lba=lba;ata_available=available;ata_output=out;++ata_reads;
    if(ata_read_ok) a->read_active=true;
    return ata_read_ok;
}
bool kui_ata_read_stop(struct kui_ata *a) {
    assert(a->bus==&resident_ata);++ata_stops;
    a->read_active=false;
    if(!ata_stop_ok) a->ready=false;
    return ata_stop_ok;
}
enum kui_loader_sd_result kui_retail_sd_read_run(struct kui_loader_sd *s,
    struct kui_loader_sd_stream *stream,uint32_t lba,uint32_t available,uint8_t out[512]) {
    assert(s->ready && lba==100 && available==7 && out);++sd_reads;
    stream->active=true;return KUI_LOADER_SD_OK;
}
enum kui_loader_sd_result kui_loader_sd_stream_stop(struct kui_loader_sd *s,struct kui_loader_sd_stream *stream) {
    assert(s->ready);++stops;stream->active=false;return KUI_LOADER_SD_OK;
}
int main(void) {
    struct kui_retail_storage stage,resident;
    uint8_t out[512];
    for(unsigned transport=KUI_STORAGE_SCIF;transport<=KUI_STORAGE_IDE;++transport) {
        assert(kui_retail_storage_init(&stage,transport)==KUI_LOADER_SD_OK);
        /* Poison all source bus pointers: adoption must not retain one. */
        if(transport==KUI_STORAGE_IDE) stage.device.ata.bus=(void *)(uintptr_t)1;
        else memset(&stage.device.sd.bus,0xa5,sizeof(stage.device.sd.bus));
        assert(kui_retail_storage_adopt(&resident,&stage)==KUI_LOADER_SD_OK);
        assert(resident.transport==transport && kui_retail_storage_blocks(&resident)==2000u+transport*1000u);
        if(transport==KUI_STORAGE_SCI) assert(!memcmp(&resident.device.sd.bus,&resident_sci,sizeof(resident_sci)));
        assert(kui_retail_storage_acquire(&resident)==KUI_LOADER_SD_OK);
        assert(kui_retail_storage_read_run(&resident,100,7,out)==KUI_LOADER_SD_OK);
        assert(kui_retail_storage_stop(&resident)==KUI_LOADER_SD_OK);
        kui_retail_storage_release(&resident);
    }
    assert(scif_claims==1 && sci_claims==2 && sd_reads==2 && ata_reads==1 && ata_stops==1 && stops==2 && releases==3);
    assert(ata_lba==100 && ata_available==7 && ata_output==out);
    /* The IDE adapter forwards the validated run boundary, not a hard-coded
     * single-sector request, and owns no SCI/SCIF traffic or release hook. */
    assert(kui_retail_storage_init(&stage,KUI_STORAGE_IDE)==KUI_LOADER_SD_OK);
    assert(kui_retail_storage_read_run(&stage,0x12345678u,256,out)==KUI_LOADER_SD_OK);
    assert(ata_reads==2 && ata_lba==0x12345678u && ata_available==256 && ata_output==out);
    assert(stage.device.ata.read_active);
    assert(kui_retail_storage_adopt(&resident,&stage)==KUI_LOADER_SD_NOT_READY);
    assert(stage.device.ata.read_active && ata_reads==2 && ata_stops==1);
    assert(kui_retail_storage_stop(&stage)==KUI_LOADER_SD_OK);
    assert(!stage.device.ata.read_active && ata_stops==2);
    assert(kui_retail_storage_adopt(&resident,&stage)==KUI_LOADER_SD_OK);
    ata_read_ok=false;
    assert(kui_retail_storage_read_run(&resident,101,6,out)==KUI_LOADER_SD_COMMAND);
    assert(ata_reads==3 && ata_lba==101 && ata_available==6);
    ata_read_ok=true;
    assert(kui_retail_storage_read_run(&resident,102,5,out)==KUI_LOADER_SD_OK);
    ata_stop_ok=false;
    assert(kui_retail_storage_stop(&resident)==KUI_LOADER_SD_COMMAND);
    assert(!resident.device.ata.ready && !resident.device.ata.read_active && ata_stops==3);
    assert(kui_retail_storage_acquire(&resident)==KUI_LOADER_SD_NOT_READY);
    ata_stop_ok=true;
    kui_retail_storage_release(&resident);
    assert(scif_claims==1 && sci_claims==2 && sd_reads==2 && stops==2 && releases==3);
    assert(kui_retail_storage_read_run(&stage,100,0,out)==KUI_LOADER_SD_ARGUMENT);
    assert(kui_retail_storage_read_run(&stage,100,1,NULL)==KUI_LOADER_SD_ARGUMENT);
    assert(ata_reads==4 && ata_stops==3);
    assert(kui_retail_storage_init(&stage,KUI_STORAGE_AUTO)==KUI_LOADER_SD_ARGUMENT);
    stage.transport=KUI_STORAGE_AUTO;
    assert(kui_retail_storage_adopt(&resident,&stage)==KUI_LOADER_SD_ARGUMENT);
    assert(kui_retail_storage_acquire(&stage)==KUI_LOADER_SD_ARGUMENT);
    stage.transport=KUI_STORAGE_SCI;stage.stream.active=true;
    assert(kui_retail_storage_adopt(&resident,&stage)==KUI_LOADER_SD_NOT_READY);
    assert(kui_retail_storage_init(&stage,KUI_STORAGE_SCI)==KUI_LOADER_SD_OK);
    healthy=false;
    assert(kui_retail_storage_stop(&stage)==KUI_LOADER_SD_TIMEOUT && !stage.device.sd.ready);
    puts("Retail storage: fixed transport, bounded ATA stream dispatch and independent handoff passed");
}
