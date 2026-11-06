/* SPDX-License-Identifier: GPL-3.0-only */
/* Execute the production report against authored raw native ABI storage.
 * Display mocks record rows; this does not model hardware video or caches. */
#define KUI_RETAIL_MANIFEST_SLOTS 48
#include "kui/retail_image.h"
#include "kui/native_ocram_diagnostic.h"
#include "../src/loader/retail_display.h"
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>

/* Native pointers have32-bit width. Only the raw engine/shared512-byte span
 * is read by this report, so represent that published ABI without host
 * pointer fields changing its1600-byte geometry. */
typedef struct {
    uint8_t receive[0x440];
    uint32_t engine[128];
} kui_retail_async_region_type;
static kui_retail_async_region_type kui_retail_async_region;
static struct {
    uint32_t before[8];
    union {
        struct kui_retail_manifest map;
        struct {
            uint8_t prefix[68];
            volatile struct kui_native_ocram_diagnostic trace;
            uint8_t retired[608];
            uint32_t diagnostic_row;
        } report;
    } state;
    uint32_t after[8];
} storage;
_Static_assert(offsetof(__typeof__(storage.state),report.diagnostic_row)==824u,
    "Actual LATE resident display cursor ABI");
#define RETAIL_MANIFEST (&storage.state.map)
#define kui_native_ocram_trace storage.state.report.trace
#define display_cursor storage.state.report.diagnostic_row
static struct retail_display_state display;
static uint32_t expected[168];
static uint8_t old_map[sizeof(storage.state)];
static unsigned restores,pauses,limit,numeric_rows,printed_lines,seen[168];
static jmp_buf report_complete;

void retail_display_line(const char *text) {
    assert(text && strlen(text)<=49u);
    assert(display_cursor>=52u && display_cursor+14u<=480u);
    ++printed_lines;display_cursor+=18u;
    if(printed_lines==1u)assert(!strcmp(text,"VC2 STOP"));
    if(printed_lines==18u)assert(!strcmp(text,"PHOTO 3 POWER OFF"));
    assert(printed_lines<=18u);
}
void retail_display_restore(const struct retail_display_state *state) {
    assert(state==&display);
    ++restores;numeric_rows=printed_lines=0u;
    /* Production restore prints BUILD at20, then sets the OCRAM row to52. */
    display_cursor=52u;
}
void retail_display_values(const char *legend,const uint32_t *values,unsigned count) {
    assert(restores && count<=5u);
    const unsigned page=(restores-1u)%3u;
    if(*legend) {
        assert(!strcmp(legend,"PG WHY ADDR VAL") && printed_lines==1u && count==4u);
        assert(values[0]==page+1u && values[1]==expected[15] &&
            values[2]==expected[3] && values[3]==expected[14]);
        retail_display_line(legend);
    } else {
        assert(count==5u && numeric_rows<14u);
        const unsigned at=page*56u+numeric_rows*4u;
        assert(values[0]==at);
        for(unsigned j=0u;j<4u;++j) {
            assert(values[j+1u]==expected[at+j]);++seen[at+j];
        }
        ++numeric_rows;
    }
    char text[50];
    for(unsigned i=0u;i<count;++i)(void)snprintf(text+i*9u,10u,"%08X ",values[i]);
    text[count*9u]=0;retail_display_line(text);
}
void retail_display_pause(uint32_t frames) {
    assert(frames==480u && numeric_rows==14u && printed_lines==18u && display_cursor==376u);
    assert(!memcmp(&storage.state,expected,sizeof(expected)));
    ++pauses;
    if(pauses==1u) {
        /* The original trace and boot fields intentionally alias the new
         * manifest snapshot, so they cannot be poisoned independently.
         * Poison the separate live engine/shared source after capture. */
        memset(&kui_retail_async_region.engine,0xcc,512u);
    }
    if(pauses==limit)longjmp(report_complete,1);
}
#include "../src/loader/vc2_ce_vmu_late_report.inc"

static void fixture(unsigned pages) {
    memset(&storage,0xa5,sizeof(storage));
    memset(&kui_retail_async_region,0x5a,sizeof(kui_retail_async_region));
    RETAIL_MANIFEST->boot_lba=0x12345678u;
    RETAIL_MANIFEST->boot_bytes=0x87654321u;
    RETAIL_MANIFEST->gdi_crc32=48u;
    for(unsigned i=0u;i<37u;++i)expected[i]=0x70000000u+i*0x10101u;
    memcpy((void *)&kui_native_ocram_trace,expected,37u*4u);
    for(unsigned i=0u;i<128u;++i)
        expected[37u+i]=kui_retail_async_region.engine[i]=0x90000000u+i*0x20202u;
    expected[165]=RETAIL_MANIFEST->boot_lba;
    expected[166]=RETAIL_MANIFEST->boot_bytes;
    expected[167]=RETAIL_MANIFEST->gdi_crc32;
    memcpy(old_map,&storage.state,sizeof(old_map));
    restores=pauses=numeric_rows=printed_lines=0u;limit=pages;
    memset(seen,0,sizeof(seen));
}
int main(void) {
    for(volatile unsigned pages=3u;pages<=6u;pages+=3u) {
        fixture(pages);
        if(!setjmp(report_complete))native_ocram_diagnostic_report("IGNORED",0xdeadbeefu,0xffffffffu);
        assert(pauses==pages && restores==pages);
        for(unsigned i=0u;i<168u;++i)assert(seen[i]==pages/3u);
        for(unsigned i=0u;i<8u;++i)assert(storage.before[i]==0xa5a5a5a5u && storage.after[i]==0xa5a5a5a5u);
        assert(!memcmp((uint8_t *)&storage.state+672u,old_map+672u,824u-672u));
        if(sizeof(old_map)>828u)
            assert(!memcmp((uint8_t *)&storage.state+828u,old_map+828u,sizeof(old_map)-828u));
    }
    puts("PASS production late report:168 exact snapshot words,3 distinct numbered pages,14 numeric rows/page,480-frame pauses, full-cycle wrap, nonoverlapping cursor824, captured state survives poisoned live source;18 lines finish at row376");
    return 0;
}
