#include "kui/native_readahead.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static struct kui_native_ra_public pub;static struct kui_native_ra_exports exports;static kui_retail_async_region_type region;
static unsigned rows,pages,pio_rows,pair_rows,cache_rows;
static kui_retail_async_region_type *reader_capture_region(void){return &region;}
static const struct kui_native_ra_exports *reader_readahead_exports(const kui_retail_async_region_type*r){assert(r==&region);return &exports;}
void retail_display_restore(const struct retail_display_state*d){(void)d;if(pages==1)cache_rows=rows;assert(rows<=16);rows=0;pages++;}
void retail_display_line(const char*s){assert(strlen(s)<=49);rows++;assert(rows<=16);}
void retail_display_values(const char*s,const uint32_t*v,unsigned n){assert(n<=5);retail_display_line(s);rows++;assert(rows<=16);if(!strcmp(s,"PIO WORK MAX BAD")){assert(n==3&&v[0]==7&&v[1]==1500&&v[2]==2);pio_rows++;}if(!strcmp(s,"SECOND SKIP ONE MAX OVER")){assert(n==4&&v[0]==3&&v[1]==4&&v[2]==900&&v[3]==1);pair_rows++;}if(!strcmp(s,"FAIL POLL TAKE APPEND FETCH"))for(unsigned i=0;i<5;i++)assert(!v[i]);}
void retail_display_hex(const char*s,uint32_t v){assert(!strcmp(s,"AUX MAX TICKS")&&v==9999);retail_display_line(s);}
void retail_display_pause(uint32_t n){assert(n==240&&rows<=16);}
/* Eight label/value pairs fit the sixteen body rows. The immutable field
 * map keeps every raw word while sharing one formatter and argument buffer. */
static void reader_readahead_failure(const struct retail_display_state *display,
    const struct kui_native_ra_failure_snapshot *s,const char *heading) {
#define RA_FIELD(name) (offsetof(struct kui_native_ra_failure_snapshot,name)/4u)
    static const uint8_t field[37]={RA_FIELD(stage),RA_FIELD(result),RA_FIELD(event),
        RA_FIELD(flags),RA_FIELD(token),RA_FIELD(cursor_block),RA_FIELD(cache_base),
        RA_FIELD(cache_count),RA_FIELD(cache_end),RA_FIELD(operation_lba),RA_FIELD(loan_state),
        RA_FIELD(rx_flags),RA_FIELD(ready_lba[0]),RA_FIELD(ready_lba[1]),RA_FIELD(kept_lba),
        RA_FIELD(arrived),RA_FIELD(lost),RA_FIELD(stream_state),RA_FIELD(stream_position),
        RA_FIELD(pre.sar),RA_FIELD(pre.dar),RA_FIELD(pre.tcr),RA_FIELD(pre.chcr),RA_FIELD(pre.dmaor),
        RA_FIELD(post.sar),RA_FIELD(post.dar),RA_FIELD(post.tcr),RA_FIELD(post.chcr),RA_FIELD(post.dmaor),
        RA_FIELD(pre.sci),RA_FIELD(pre.pins),RA_FIELD(pre.tmu),RA_FIELD(post_stream_state),
        RA_FIELD(post_stream_position),RA_FIELD(post.sci),RA_FIELD(post.pins),RA_FIELD(post.tmu)};
#undef RA_FIELD
    static const uint8_t count[8]={5,5,5,4,5,5,5,3};
    static const char *const label[7]={"BLK BASE COUNT END OP","LOAN RX R0 R1 KEPT",
        "ARRIVED LOST PRE STATE POS","PRE SAR DAR TCR CHCR DMAOR",
        "POST SAR DAR TCR CHCR DMAOR","PRE SCI PINS TMU POST STATE POS","POST SCI PINS TMU"};
    uint32_t values[5];unsigned at=0u;
    retail_display_restore(display);
    for(unsigned row=0u;row<8u;row++) {
        for(unsigned col=0u;col<count[row];col++,at++)
            memcpy(&values[col],(const uint8_t *)s+4u*field[at],4u);
        retail_display_values(row?label[row-1u]:heading,values,count[row]);
    }
    retail_display_pause(240u);
}
static void reader_readahead_diagnostics(const struct retail_display_state *display,
    const struct kui_native_ra_diagnostics *d) {
    retail_display_restore(display);
    retail_display_line("READ AHEAD EXIT REASONS");
    uint32_t stages[7]={d->failures,0,0,0,0,0,0};
    for(unsigned stage=1u;stage<KUI_NATIVE_RA_STAGE_COUNT;stage++)
        for(unsigned result=0u;result<KUI_NATIVE_RA_RESULT_COUNT;result++)
            stages[stage]+=d->stage_result[stage][result];
    retail_display_values("FAIL POLL TAKE APPEND FETCH",stages,5u);
    retail_display_values("RESUME STOP FIRST ST LAST ST",(uint32_t[4]){
        stages[5],stages[6],d->first.stage,d->last.stage},4u);
    const uint32_t *n=d->skipped;
    retail_display_values("SKIP QUIET ABORT SEED LOAN ACTIVE",n,5u);
    retail_display_values("SKIP PEND FAIL OPEN PORT BUSY",n+5u,5u);
    retail_display_values("SKIP DMA POS DEFER OWNED",n+10u,4u);
    retail_display_pause(240u);
    if(!d->failures)return;
    bool same=!memcmp(&d->first,&d->last,sizeof(d->first));
    reader_readahead_failure(display,&d->first,same?
        "BOTH ST RES EVT FL TOK":
        "FIRST ST RES EVT FL TOK");
    if(!same)reader_readahead_failure(display,&d->last,
        "LAST ST RES EVT FL TOK");
}
static void reader_readahead_report(const struct retail_display_state *display) {
    const struct kui_native_ra_exports *a=reader_readahead_exports(reader_capture_region());
    if(!a)return;
    const struct kui_native_ra_stats *s=&((const struct kui_native_ra_public *)a->context)->stats;
    retail_display_restore(display);
    retail_display_line("CRC VALIDATED PHYSICAL READ AHEAD");
    retail_display_values("CAP HITS MISS HIT REQS",(uint32_t[4]){
        KUI_NATIVE_RA_BLOCKS,s->hits,s->misses,s->hit_requests},4u);
    retail_display_values("MADE VISITS HEARTBEAT IRQ",(uint32_t[4]){
        s->producer_blocks,s->producer_visits,s->heartbeat_visits,s->irq_visits},4u);
    retail_display_values("SEEK STOP RESUME FULL EXTENT",(uint32_t[5]){
        s->seeks,s->quiesces,s->resumes,s->full,s->extent_ends},5u);
    retail_display_values("CRC TOKEN ABORT GUARD CLOCK",(uint32_t[5]){
        s->crc_errors,s->token_errors,s->abandoned,s->guard_faults,s->clock_bad},5u);
    retail_display_values("PIO WORK MAX BAD",
        ((const struct kui_native_ra_public *)a->context)->diagnostics.stage_result[KUI_NATIVE_RA_STAGE_NONE],3u);
    retail_display_values("SECOND SKIP ONE MAX OVER",
        ((const struct kui_native_ra_public *)a->context)->diagnostics.stage_result[KUI_NATIVE_RA_STAGE_NONE]+
            KUI_NATIVE_RA_PIO_SECOND_ATTEMPTS,4u);
    retail_display_hex("AUX MAX TICKS",s->max_visit_ticks);
    retail_display_line("HEX - 781250 HZ");
    retail_display_pause(240u);
    reader_readahead_diagnostics(display,
        &((const struct kui_native_ra_public *)a->context)->diagnostics);
}
int main(void){struct retail_display_state display={0};exports.context=&pub;pub.stats.max_visit_ticks=9999;const uint32_t v[7]={7,1500,2,3,4,900,1};memcpy(pub.diagnostics.stage_result[KUI_NATIVE_RA_STAGE_NONE],v,sizeof(v));reader_readahead_report(&display);assert(pio_rows==1&&pair_rows==1&&cache_rows==15&&pages==2);puts("Actual PIO-pair report: all7metricwords in order; separate whole/single/aux maxima; reserved row excluded from failures; 15-row cache viewport passed");return 0;}
