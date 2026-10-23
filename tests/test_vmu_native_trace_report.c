/* SPDX-License-Identifier: GPL-3.0-only */
/* Execute the actual terminal page renderer with a display-row bus. No live
 * sampler/MMIO operation is called. The row model matches retail_display.c. */
#define KUI_VMU_NATIVE_PERSISTENT_PROBE 1
#define KUI_VMU_NATIVE_READONLY_PROBE 1
#include "../src/loader/vmu_native_trace.c"
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>

static struct {unsigned rows;char text[16][64];} pages[8];
static unsigned page_count,expected_pages,pauses,row,checks;
static unsigned packet_hits[KUI_VMU_VOLATILE_CHAIN_FRAMES],history_rows;
static bool packet_page,guard_values_seen;
static uint32_t expected_guard[4];
static struct kui_vmu_volatile_chain_frame frames[KUI_VMU_VOLATILE_CHAIN_FRAMES];
static jmp_buf cycle_complete;
#define CHECK(x) do {++checks;assert(x);} while(0)

void retail_display_restore(const struct retail_display_state *display) {
    CHECK(display==&trace.display && page_count<expected_pages && page_count<8u);
    ++page_count;row=108u;packet_page=false;
}
void retail_display_line(const char *text) {
    CHECK(page_count && row<=444u && strlen(text)<sizeof(pages[0].text[0]));
    unsigned n=pages[page_count-1u].rows++;
    CHECK(n<16u);strcpy(pages[page_count-1u].text[n],text);row+=22u;
    if(!strcmp(text,"INDEX CONTROL RX HEADER PAYLOAD0")) {packet_page=true;return;}
    if(!strcmp(text,"CODE PR MDST TICK WAIT FLAG / OLDEST FIRST")) {packet_page=false;return;}
    /* The actual row helpers format five eight-digit words with a trailing
     * space per word. Titles and legends do not have this exact shape. */
    if(strlen(text)==45u && text[8]==' ' && text[17]==' ' && text[26]==' ' &&
       text[35]==' ' && text[44]==' ') {
        for(unsigned i=0u;i<45u;i++) CHECK(i%9u==8u?text[i]==' ':
            (text[i]>='0' && text[i]<='9') || (text[i]>='A' && text[i]<='F'));
        if(packet_page) {
            char *end;unsigned index=(unsigned)strtoul(text,&end,16);
            CHECK(end==text+8u && index<KUI_VMU_VOLATILE_CHAIN_FRAMES);
            uint32_t values[5]={index,frames[index].control,frames[index].rx,frames[index].header,frames[index].payload0};
            for(unsigned i=0u;i<5u;i++) CHECK(strtoul(text+i*9u,NULL,16)==values[i]);
            ++packet_hits[index];
        } else if(page_count==2u || page_count==3u) ++history_rows;
    }
}
void retail_display_values(const char *legend,const uint32_t *values,unsigned count) {
    CHECK(count<=5u);retail_display_line(legend);
    if(!strcmp(legend,"CHECK AT VALUE EXPECT")) {
        CHECK(count==4u && !memcmp(values,expected_guard,sizeof(expected_guard)));
        CHECK(page_count==5u);guard_values_seen=true;
    }
    char out[46];unsigned n=0u;
    for(unsigned v=0u;v<count;v++) {
        for(unsigned digit=0u;digit<8u;digit++) out[n++]="0123456789ABCDEF"[(values[v]>>(28u-digit*4u))&15u];
        out[n++]=' ';
    }
    out[n]=0;retail_display_line(out);
}
void retail_display_hex(const char *label,uint32_t value) {
    char out[64];int n=snprintf(out,sizeof(out),"%s %08X",label,value);
    CHECK(n>0 && (size_t)n<sizeof(out));retail_display_line(out);
}
void retail_display_pause(uint32_t count) {
    CHECK(count==300u && page_count==pauses+1u);++pauses;
    if(pauses==expected_pages) longjmp(cycle_complete,1);
}
static void render_cycle(bool failure,unsigned count) {
    memset(pages,0,sizeof(pages));memset(packet_hits,0,sizeof(packet_hits));
    page_count=pauses=history_rows=0u;guard_values_seen=false;
    expected_pages=4u+(failure?1u+(count>6u?(count-6u+11u)/12u:0u):0u);
    struct kui_vmu_native_passive_config exact={0};
    exact.driver.image_bytes=2143652u;exact.driver.load_address=0x8c010000u;
    memcpy(exact.driver.image_sha256,evolution_digest,sizeof(evolution_digest));
    kui_vmu_native_trace_init(&exact);CHECK(trace.enabled);
    trace.sequence=32u;
    for(unsigned i=0u;i<16u;i++) {
        trace.ring[i].code=0x10u+i;trace.ring[i].pr=0x8c010000u+2u*i;
        trace.ring[i].mdst=i&1u;trace.ring[i].tick=UINT32_MAX-i;trace.ring[i].latch=i;
    }
    expected_guard[0]=0x30002u;expected_guard[1]=0x0c248420u;
    expected_guard[2]=(uint32_t)-7;expected_guard[3]=0u;
    for(unsigned i=0u;i<KUI_VMU_VOLATILE_CHAIN_FRAMES;i++) frames[i]=
        (struct kui_vmu_volatile_chain_frame){0x80000000u+i,0x0c500000u+1024u*i,0x01002009u+i,0x01000000u+i};
    if(failure) {
        struct kui_vmu_native_trace_failure snapshot={
            .check=expected_guard[0],.address=expected_guard[1],.value=expected_guard[2],.expected=expected_guard[3],
            .source=0x0c248420u,.source_bytes=40u,.target=0x0c38a880u,.capacity=512u,
            .frame_count=count,.nops=3u,.frames=frames};
        kui_vmu_native_trace_failure(&snapshot);
    }
    if(!setjmp(cycle_complete)) kui_vmu_native_trace_report(0x40u,expected_guard[0],expected_guard[1],expected_guard[2]);
    CHECK(page_count==expected_pages && pauses==expected_pages && history_rows==16u);
    CHECK(guard_values_seen==failure);
    for(unsigned i=0u;i<KUI_VMU_VOLATILE_CHAIN_FRAMES;i++) CHECK(packet_hits[i]==(failure && i<count?1u:0u));
    for(unsigned i=0u;i<page_count;i++) CHECK(pages[i].rows>0u && pages[i].rows<=16u);
}
int main(void) {
    render_cycle(false,0u);
    const unsigned counts[]={0u,6u,7u,18u,32u};
    for(unsigned i=0u;i<sizeof(counts)/sizeof(counts[0]);i++) render_cycle(true,counts[i]);
    printf("Native trace report: %u checks; actual page renderer, 16-row limit, complete packet pagination and four-word guard witnesses passed\n",checks);
    return 0;
}
