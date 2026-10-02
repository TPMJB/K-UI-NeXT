/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/sci_async_probe.h"
#include "sd_reader.h"
#include "sci_async_probe_test_support.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define SMR 0xffe00000u
#define BRR 0xffe00004u
#define SCR 0xffe00008u
#define SSR 0xffe00010u
#define RDR 0xffe00014u
#define SCMR 0xffe00018u
#define SPTR 0xffe0001cu
#define PDTR 0xff800030u
#define SAR 0xffa00010u
#define DAR 0xffa00014u
#define TCR 0xffa00018u
#define CHCR 0xffa0001cu
#define DMAOR 0xffa00040u
#define TDRE 0x80u
#define RDRF 0x40u
#define ORER 0x20u
#define TEND 0x04u
#define DE 1u
#define END 2u
#define IE 4u
#define DMA_BASE 0x0c100000u

enum fault { NO_FAULT, EARLY_ERROR, BAD_CRC, WRONG_DATA, STALLED,
             BAD_GUARD, DMA_FAULT, TRAILING_ERROR, BAD_RESPONSE, BAD_TOKEN,
             TRAILING_FRAME_ERROR, TRAILING_PARITY_ERROR, TRAILING_ERI,
             EARLY_RX_IRQ, TRAILING_WITHOUT_END, BAD_PADDING,
             FOREIGN_DURING_FRAMING, FOREIGN_AFTER_DMA, FOREIGN_DURING_DMA,
             TOKEN_NOT_ENDED, BAD_GPIO_CONTROL };
static struct {
    uint8_t smr, brr, scr, ssr, rdr, scmr, sptr;
    uint16_t pdtr;
    uint32_t sar, dar, tcr, chcr, dmaor;
    irq_cb_t handlers[3], probe_handlers[3];
    unsigned priorities[2], irq_mask;
    unsigned writes, handler_writes, priority_writes, dma_starts;
    unsigned cache_purges, cache_invalidates, work_ticks, transferred, total_bytes;
    unsigned command_position, command_count, selects, deselects;
    unsigned original_calls, dispatches;
    unsigned slow_starts, fast_starts;
    unsigned install_attempts, fail_install_at;
    unsigned monitored_gpio_reads, failed_handler_restores;
    uint32_t command_argument;
    uint64_t now;
    uint8_t baseline[512], command_bytes[5], *dma_buffer;
    size_t dma_size;
    bool selected, token_sent, response_sent, inside_irq, pending_dma;
    bool fault_fired, advancing, fail_restore_register, foreign_active;
    bool dynamic_sptr_inputs, no_overlap, fail_handler_restore;
    bool rxd_pin, sck_pin, token_rx_high;
    enum fault fault;
} hw;
static int handler_data[3];
static int foreign_data;

/* Independent bit-by-bit oracles, deliberately unlike the driver operations. */
static uint8_t reverse_byte(uint8_t byte) {
    uint8_t out=0;
    for(unsigned i=0;i<8;++i) { out=(uint8_t)((out<<1)|(byte&1u)); byte>>=1; }
    return out;
}
static uint16_t reference_crc(const uint8_t *bytes,size_t count) {
    uint16_t crc=0;
    for(size_t i=0;i<count;++i) {
        crc^=(uint16_t)bytes[i]<<8;
        for(unsigned bit=0;bit<8;++bit)
            crc=(uint16_t)((crc<<1)^((crc&0x8000u)?0x1021u:0));
    }
    return crc;
}
static uint8_t reference_crc7(const uint8_t bytes[5]) {
    unsigned remainder=0;
    for(unsigned i=0;i<5;++i) {
        for(unsigned bit=0;bit<8;++bit) {
            bool feedback=((remainder>>6)^((bytes[i]>>(7-bit))&1u))&1u;
            remainder=(remainder<<1)&0x7fu;
            if(feedback) remainder^=0x09u;
        }
    }
    return (uint8_t)((remainder<<1)|1u);
}
static unsigned handler_index(irq_t irq) {
    if(irq==EXC_DMAC_DMTE1) return 0;
    if(irq==EXC_SCI_ERI) return 1;
    assert(irq==EXC_SCI_RXI); return 2;
}
static void original_handler(irq_t irq,irq_context_t *context,void *data) {
    (void)context;
    assert(data==&handler_data[handler_index(irq)]);
    ++hw.original_calls;
}
static void foreign_handler(irq_t irq,irq_context_t *context,void *data) {
    (void)context; assert(irq==EXC_DMAC_DMTE1 && data==&foreign_data);
    ++hw.original_calls;
}
static void install_foreign_dma(void) {
    hw.sar=0x0c080000; hw.dar=0x0c090000; hw.tcr=64; hw.chcr=0x1025;
    hw.handlers[0]=(irq_cb_t){foreign_handler,&foreign_data};
    hw.foreign_active=true;
}
static void deliver(irq_t irq) {
    unsigned index=handler_index(irq);
    unsigned source=index==0?IRQ_SRC_DMAC:IRQ_SRC_SCI1;
    if((hw.irq_mask&0x10000000u) ||
       hw.priorities[source]<=((hw.irq_mask>>4)&15u) || hw.inside_irq) return;
    irq_cb_t callback=hw.handlers[index];
    if(!callback.hdl) return;
    irq_context_t context={0};
    hw.inside_irq=true; ++hw.dispatches;
    callback.hdl(irq,&context,callback.data);
    hw.inside_irq=false;
}
static bool receiving(void) {
    return (hw.scr&0x70u)==0x50u && (hw.chcr&(DE|END))==DE &&
        ((hw.chcr>>8)&15u)==9u;
}
static uint8_t logical_byte(unsigned position) {
    if(position<512) {
        uint8_t value=hw.baseline[position];
        if(hw.fault==WRONG_DATA && position==73) value^=0x40u;
        return value;
    }
    uint8_t payload[512];
    memcpy(payload,hw.baseline,sizeof(payload));
    if(hw.fault==WRONG_DATA) payload[73]^=0x40u;
    uint16_t crc=reference_crc(payload,sizeof(payload));
    if(hw.fault==BAD_CRC) crc^=1u;
    return position==512?(uint8_t)(crc>>8):(uint8_t)crc;
}
static void advance(void) {
    if(hw.advancing) return;
    hw.advancing=true;
    if(receiving() && hw.fault!=STALLED) {
        if(!hw.fault_fired && hw.transferred>=64 &&
           (hw.fault==EARLY_ERROR || hw.fault==DMA_FAULT || hw.fault==EARLY_RX_IRQ ||
            hw.fault==FOREIGN_DURING_DMA)) {
            hw.fault_fired=true;
            if(hw.fault==EARLY_ERROR) { hw.ssr|=ORER; deliver(EXC_SCI_ERI); }
            else if(hw.fault==EARLY_RX_IRQ) { hw.ssr|=RDRF; deliver(EXC_SCI_RXI); }
            else if(hw.fault==FOREIGN_DURING_DMA) {
                install_foreign_dma(); hw.ssr|=ORER; deliver(EXC_SCI_ERI);
            }
            else hw.dmaor|=4u;
        }
        if(receiving() && !(hw.ssr&ORER) && !(hw.dmaor&6u)) {
            unsigned budget=hw.no_overlap?514u:32u;
            for(unsigned bytes=0;bytes<budget && hw.tcr;++bytes) {
                assert(hw.dma_buffer && hw.dar>=DMA_BASE);
                size_t offset=hw.dar-DMA_BASE;
                assert(offset<hw.dma_size && hw.transferred<514);
                hw.dma_buffer[offset]=reverse_byte(logical_byte(hw.transferred));
                ++hw.dar; ++hw.transferred; ++hw.total_bytes; --hw.tcr;
            }
            if(!hw.tcr) {
                hw.chcr|=END; hw.pending_dma=true;
                if(hw.fault==TRAILING_ERROR) hw.ssr|=ORER|RDRF;
                if(hw.fault==TRAILING_FRAME_ERROR) hw.ssr|=0x10u;
                if(hw.fault==TRAILING_PARITY_ERROR) hw.ssr|=0x08u;
                if(hw.fault==TRAILING_ERI || hw.fault==TRAILING_WITHOUT_END) {
                    hw.ssr|=ORER|RDRF;
                    if(hw.fault==TRAILING_WITHOUT_END) hw.chcr&=~END;
                    deliver(EXC_SCI_ERI);
                }
                if(hw.fault==BAD_PADDING) hw.dma_buffer[514]^=0x80u;
                if(hw.fault==BAD_GUARD) {
                    /* Corrupt the byte immediately beyond the complete DMA
                     * destination allocation, not a useful payload byte. */
                    hw.dma_buffer[hw.dma_size]^=0x80u;
                }
            }
        }
    }
    if(hw.pending_dma && (hw.chcr&(IE|END))==(IE|END)) {
        deliver(EXC_DMAC_DMTE1);
        if(!(hw.chcr&IE)) hw.pending_dma=false;
    }
    hw.advancing=false;
}

irq_mask_t irq_disable(void) {
    unsigned old=hw.irq_mask; hw.irq_mask|=0xf0u; return old;
}
void irq_restore(irq_mask_t mask) { hw.irq_mask=mask; }
bool irq_inside_int(void) { return hw.inside_irq; }
irq_cb_t irq_get_handler(irq_t source) { return hw.handlers[handler_index(source)]; }
int irq_set_handler(irq_t source,irq_hdl_t handler,void *data) {
    assert((hw.irq_mask&0xf0u)==0xf0u || hw.inside_irq);
    if(hw.fail_handler_restore && handler==original_handler) {
        ++hw.failed_handler_restores; return -1;
    }
    if(handler && handler!=original_handler &&
       ++hw.install_attempts==hw.fail_install_at) return -1;
    hw.handlers[handler_index(source)]=(irq_cb_t){handler,data};
    if(handler && handler!=original_handler)
        hw.probe_handlers[handler_index(source)]=(irq_cb_t){handler,data};
    ++hw.handler_writes; return 0;
}
unsigned irq_get_priority(irq_src_t source) {
    assert(source<2); return hw.priorities[source];
}
void irq_set_priority(irq_src_t source,unsigned priority) {
    assert(source<2 && priority<16);
    /* This experiment may borrow SCI priority, never the shared DMAC level. */
    assert(source!=IRQ_SRC_DMAC);
    hw.priorities[source]=priority; ++hw.priority_writes;
}
uint64_t timer_us_gettime64(void) { hw.now+=37; advance(); return hw.now; }
void kui_sci_async_test_work_tick(void) { ++hw.work_ticks; hw.now+=11; advance(); }
void kui_sci_async_test_delay(uint32_t count) { assert(count); }
uint32_t kui_sci_async_test_dma_address(const void *buffer,size_t count) {
    assert(buffer && count>=514 && count<=1024);
    assert(!((uintptr_t)buffer&31u));
    hw.dma_buffer=(uint8_t *)buffer; hw.dma_size=count; return DMA_BASE;
}
void kui_sci_async_test_cache_purge(void *buffer,size_t count) {
    assert(buffer && !((uintptr_t)buffer&31u)); assert(count && !(count&31u));
    ++hw.cache_purges;
}
void kui_sci_async_test_cache_invalidate(void *buffer,size_t count) {
    assert(buffer && !((uintptr_t)buffer&31u)); assert(count && !(count&31u));
    assert(!(hw.scr&0x50u) && !(hw.chcr&DE)); ++hw.cache_invalidates;
    if(hw.fault==FOREIGN_AFTER_DMA) install_foreign_dma();
}
uint32_t kui_sci_async_test_read(uint32_t address,unsigned width) {
    switch(address) {
        case SMR: assert(width==1); return hw.smr;
        case BRR: assert(width==1); return hw.brr;
        case SCR: assert(width==1); return hw.scr;
        case SSR: assert(width==1); return hw.ssr;
        case RDR: assert(width==1); return hw.rdr;
        case SCMR: assert(width==1); return hw.scmr;
        case SPTR: {
            assert(width==1);
            /* Reads expose RxD/SCK pins, never the output data latches,
             * regardless of output directions or the transmitter state. */
            if(hw.sptr&2u) ++hw.monitored_gpio_reads;
            return (hw.sptr&~5u)|(hw.rxd_pin?1u:0u)|(hw.sck_pin?4u:0u);
        }
        case PDTR: assert(width==2); return hw.pdtr;
        case SAR: assert(width==4); return hw.sar;
        case DAR: assert(width==4); return hw.dar;
        case TCR: assert(width==4); return hw.tcr;
        case CHCR: assert(width==4); return hw.chcr;
        case DMAOR: assert(width==4); return hw.dmaor;
        default: assert(!"Unexpected MMIO read"); return 0;
    }
}
void kui_sci_async_test_write(uint32_t address,uint32_t value,unsigned width) {
    ++hw.writes;
    if(hw.foreign_active)
        assert(address!=SAR && address!=DAR && address!=TCR && address!=CHCR);
    switch(address) {
        case SMR: assert(width==1); hw.smr=value; break;
        case BRR: assert(width==1); hw.brr=value; break;
        case SCR:
            assert(width==1);
            if((value&0x70u)==0x50u && !(hw.scr&0x10u)) {
                assert(hw.selected && hw.token_sent && hw.smr==0x80);
                assert(hw.tcr==514 && hw.sar==0x1fe00014u);
                assert((hw.chcr&0xffffu)==0x4915u && hw.cache_purges);
                assert((hw.sptr&3u)==3u && (hw.sptr&0x80u));
                assert(hw.brr==0 || hw.brr==31);
                if(hw.brr) ++hw.slow_starts; else ++hw.fast_starts;
                hw.transferred=0; ++hw.dma_starts;
            }
            hw.scr=value;
            if(!(value&0x20u)) hw.ssr|=TEND;
            break;
        case SSR: assert(width==1); hw.ssr&=value; break;
        case SCMR: assert(width==1); hw.scmr=value; break;
        case SPTR:
            assert(width==1);
            if(!(hw.fail_restore_register && value==0x04)) {
                hw.sptr=value;
                if(hw.fault==BAD_GPIO_CONTROL && value==0x83) hw.sptr&=(uint8_t)~0x80u;
            }
            break;
        case SAR: assert(width==4); hw.sar=value; break;
        case DAR: assert(width==4); hw.dar=value; break;
        case TCR: assert(width==4); hw.tcr=value; break;
        case CHCR:
            assert(width==4); hw.chcr=value;
            if(!(value&IE)) hw.pending_dma=false;
            break;
        default: assert(!"Unexpected MMIO write (global DMAOR is never owned)");
    }
}
static void bus_select(void *ctx,bool selected) {
    assert(ctx==&hw);
    if(selected) {
        assert(!hw.selected); ++hw.selects;
        hw.command_position=0; hw.command_argument=0;
        hw.token_sent=false; hw.response_sent=false;
    } else {
        assert((hw.scr&0x70u)!=0x50u && (!(hw.chcr&DE) || hw.foreign_active));
        ++hw.deselects;
    }
    hw.selected=selected;
    if(selected) hw.pdtr&=(uint16_t)~0x80u;
    else {
        hw.pdtr|=0x80u;
        if(hw.dynamic_sptr_inputs && hw.command_count) {
            hw.rxd_pin=true; hw.sck_pin=false;
        }
    }
}
static uint8_t bus_transfer(void *ctx,uint8_t byte,bool slow) {
    assert(ctx==&hw && (hw.scr&0x70u)!=0x50u);
    /* The existing synchronous bus owns framing and its clock-state cache. */
    hw.brr=slow?31:0; hw.scr=0x30;
    if(!hw.selected) { assert(byte==0xff); return 0xff; }
    if(!hw.command_position && byte==0xff) return 0xff;
    if(hw.command_position<6) {
        unsigned pos=hw.command_position++;
        if(pos<5) hw.command_bytes[pos]=byte;
        if(!pos) { assert(byte==0x51); ++hw.command_count; }
        else if(pos<5) hw.command_argument=(hw.command_argument<<8)|byte;
        else assert(byte==reference_crc7(hw.command_bytes));
        return 0xff;
    }
    assert(byte==0xff);
    if(!hw.response_sent) {
        hw.response_sent=true; return hw.fault==BAD_RESPONSE?0x04:0x00;
    }
    if(!hw.token_sent) {
        hw.token_sent=true;
        /* The last bit of a successful 0xfe token is zero. Outputting a
         * high TxD latch must not turn this independent RxD input high. */
        hw.rxd_pin=hw.token_rx_high; hw.sck_pin=true;
        if(hw.fault==TOKEN_NOT_ENDED) hw.ssr&=(uint8_t)~TEND;
        if(hw.fault==FOREIGN_DURING_FRAMING) install_foreign_dma();
        return hw.fault==BAD_TOKEN?0x0b:0xfe;
    }
    return 0xff;
}
static uint32_t bus_ticks(void *ctx) { assert(ctx==&hw); return (uint32_t)(hw.now++); }
static struct kui_loader_sd reset(void) {
    memset(&hw,0,sizeof(hw));
    hw.smr=0x80; hw.brr=0; hw.scr=0x30; hw.ssr=TDRE|TEND; hw.sptr=0x04;
    hw.sck_pin=true;
    hw.pdtr=0x1280;
    hw.sar=0x0c002000; hw.dar=0x0c004000; hw.tcr=19;
    hw.chcr=0x4000; hw.dmaor=0x0301;
    hw.priorities[IRQ_SRC_SCI1]=2; hw.priorities[IRQ_SRC_DMAC]=5;
    for(unsigned i=0;i<3;++i) hw.handlers[i]=(irq_cb_t){original_handler,&handler_data[i]};
    for(unsigned i=0;i<512;++i) hw.baseline[i]=(uint8_t)(i*71u+19u);
    return (struct kui_loader_sd){
        .bus={.ctx=&hw,.select=bus_select,.transfer=bus_transfer,.ticks=bus_ticks},
        .blocks=10000,.high_capacity=true,.ready=true,.slow=false
    };
}
static void restored(const struct kui_sci_async_probe_result *result) {
    assert(result->safe_restored && result->handlers_restored && result->registers_restored);
    assert(hw.smr==0x80 && hw.brr==0 && hw.scr==0x30 && hw.scmr==0 && hw.sptr==0x04);
    assert(!(hw.ssr&(RDRF|0x38u)) && !hw.selected && !hw.pending_dma);
    assert(hw.sar==0x0c002000 && hw.dar==0x0c004000 && hw.tcr==19 && hw.chcr==0x4000);
    assert(hw.priorities[IRQ_SRC_SCI1]==2 && hw.priorities[IRQ_SRC_DMAC]==5);
    assert(hw.irq_mask==0 && !hw.inside_irq && !hw.original_calls);
    for(unsigned i=0;i<3;++i) {
        assert(hw.handlers[i].hdl==original_handler);
        assert(hw.handlers[i].data==&handler_data[i]);
    }
}
static void test_success(bool high_miso) {
    struct kui_loader_sd card=reset(); hw.token_rx_high=high_miso;
    struct kui_sci_async_probe_result result;
    assert(kui_sci_async_probe_run(&card,123,hw.baseline,NULL,NULL,&result)==KUI_SCI_ASYNC_OK);
    assert(result.status==KUI_SCI_ASYNC_OK && result.started && result.lba==123);
    assert(result.slow.attempted==16 && result.slow.passed==16);
    assert(result.fast.attempted==64 && result.fast.passed==64);
    assert(result.slow.dma_irqs==16 && result.fast.dma_irqs==64);
    assert(result.slow.dma_started==16 && result.fast.dma_started==64);
    assert(result.slow.last_phase==KUI_SCI_ASYNC_PHASE_COMPLETE);
    assert(result.fast.last_phase==KUI_SCI_ASYNC_PHASE_COMPLETE);
    assert(result.slow.command_response==0 && result.fast.command_response==0);
    assert(result.slow.last_token==0xfe && result.fast.last_token==0xfe);
    /* Both pin states complete all 80 transfers with the same high TxD latch.
     * The former gate would incorrectly reject the low-input case. */
    uint32_t sampled=0x86u|(high_miso?1u:0u);
    assert(result.slow.snapshot_sptr==sampled && result.fast.snapshot_sptr==sampled);
    assert(((sampled&0x8bu)==0x83u)==high_miso);
    assert((sampled&0x8au)==0x82u);
    assert(result.slow.overlap_iterations && result.fast.overlap_iterations && hw.work_ticks);
    assert(result.guards_ok && result.crc_ok && result.baseline_ok);
    assert(hw.command_count==80 && hw.dma_starts==80 && hw.command_argument==123);
    assert(hw.total_bytes==80u*514u);
    assert(hw.slow_starts==16 && hw.fast_starts==64);
    assert(hw.dmaor==0x0301); restored(&result);
}
static void test_fault(enum fault fault,enum kui_sci_async_status expected) {
    struct kui_loader_sd card=reset(); hw.fault=fault;
    struct kui_sci_async_probe_result result;
    assert(kui_sci_async_probe_run(&card,123,hw.baseline,NULL,NULL,&result)==expected);
    assert(result.status==expected && result.started && result.slow.passed==0);
    assert(result.fast.attempted==0);
    if(fault==BAD_RESPONSE || fault==BAD_TOKEN || fault==TOKEN_NOT_ENDED || fault==BAD_GPIO_CONTROL) {
        enum kui_sci_async_phase phase=fault==BAD_RESPONSE?KUI_SCI_ASYNC_PHASE_COMMAND:
            fault==BAD_TOKEN?KUI_SCI_ASYNC_PHASE_TOKEN:
            fault==TOKEN_NOT_ENDED?KUI_SCI_ASYNC_PHASE_TOKEN_END:KUI_SCI_ASYNC_PHASE_GPIO;
        assert(result.slow.last_phase==phase && !result.slow.dma_started);
        assert(!hw.dma_starts && !result.slow.dma_irqs && !result.slow.overlap_batches);
        assert(!result.guards_ok && !result.crc_ok && !result.baseline_ok);
        assert(result.slow.command_response==(fault==BAD_RESPONSE?0x04u:0u));
        assert(result.slow.last_token==(fault==BAD_RESPONSE?0xffu:fault==BAD_TOKEN?0x0bu:0xfeu));
        assert(result.slow.snapshot_ssr==(fault==TOKEN_NOT_ENDED?TDRE:TDRE|TEND));
        assert(result.slow.snapshot_sptr==(fault==BAD_GPIO_CONTROL?0x06u:0x04u));
        assert(result.operation_status==expected && !result.dma_quarantined);
    }
    restored(&result);
}
static void quarantined_fault(enum fault fault,enum kui_sci_async_status cause) {
    struct kui_loader_sd card=reset(); hw.fault=fault;
    struct kui_sci_async_probe_result result;
    assert(kui_sci_async_probe_run(&card,123,hw.baseline,NULL,NULL,&result)==KUI_SCI_ASYNC_RESTORE);
    assert(result.status==KUI_SCI_ASYNC_RESTORE && result.operation_status==cause);
    assert(result.started && result.dma_quarantined && !result.foreign_dma);
    assert(!result.safe_restored && !result.registers_restored && result.handlers_restored);
    assert(!result.guards_ok && !result.crc_ok && !result.baseline_ok);
    assert(result.slow.attempted==1 && !result.slow.passed && !result.fast.attempted);
    assert(!hw.cache_invalidates && !hw.selected && !hw.pending_dma);
    assert(hw.chcr==0x4910 && !(hw.chcr&(DE|IE)) && (hw.scr&0x70u)!=0x50u);
    assert(hw.sar==0x1fe00014 && hw.dar==DMA_BASE+hw.total_bytes);
    assert(hw.tcr==514u-hw.total_bytes);
    assert(hw.sar!=0x0c002000 && hw.dar!=0x0c004000 && hw.tcr!=19);
    assert(hw.dmaor==(fault==DMA_FAULT?0x0305u:0x0301u));
    if(fault==STALLED) assert(result.slow.timeouts==1);
    for(unsigned i=0;i<3;++i) {
        assert(hw.handlers[i].hdl==original_handler);
        assert(hw.handlers[i].data==&handler_data[i]);
    }
    unsigned writes=hw.writes, delivered=hw.dispatches;
    for(unsigned i=0;i<100;++i) kui_sci_async_test_work_tick();
    assert(hw.dispatches==delivered && !hw.original_calls);
    irq_context_t context={0};
    for(unsigned i=0;i<3;++i)
        hw.probe_handlers[i].hdl(i==0?EXC_DMAC_DMTE1:(i==1?EXC_SCI_ERI:EXC_SCI_RXI),
                                &context,hw.probe_handlers[i].data);
    assert(hw.writes==writes && hw.chcr==0x4910);
    assert(kui_sci_async_probe_run(&card,123,hw.baseline,NULL,NULL,&result)==KUI_SCI_ASYNC_BUSY);
    assert(!result.started && hw.writes==writes && !hw.cache_invalidates);
}
static void test_early_error_quarantined(void) { quarantined_fault(EARLY_ERROR,KUI_SCI_ASYNC_RECEIVE_ERROR); }
static void test_early_rx_irq_quarantined(void) { quarantined_fault(EARLY_RX_IRQ,KUI_SCI_ASYNC_RECEIVE_ERROR); }
static void test_timeout_quarantined(void) { quarantined_fault(STALLED,KUI_SCI_ASYNC_TIMEOUT); }
static void test_missing_end_quarantined(void) { quarantined_fault(TRAILING_WITHOUT_END,KUI_SCI_ASYNC_RECEIVE_ERROR); }
static void test_busy_no_writes(void) {
    for(unsigned flag=1;flag<=4;flag<<=1) {
        struct kui_loader_sd card=reset(); hw.chcr|=flag;
        struct kui_sci_async_probe_result result;
        assert(kui_sci_async_probe_run(&card,123,hw.baseline,NULL,NULL,&result)==KUI_SCI_ASYNC_BUSY);
        assert(!result.started && !hw.writes && !hw.handler_writes && !hw.priority_writes);
        assert(!hw.selects && !hw.command_count && hw.chcr==(0x4000u|flag));
    }
}
static bool cancel_before_first(void *ctx) { assert(ctx==&hw); return true; }
static void test_cancellation(void) {
    struct kui_loader_sd card=reset();
    struct kui_sci_async_probe_result result;
    assert(kui_sci_async_probe_run(&card,123,hw.baseline,cancel_before_first,&hw,&result)==KUI_SCI_ASYNC_CANCELLED);
    assert(!hw.command_count && !hw.dma_starts);
    if(result.started) restored(&result);
}
static void test_standard_capacity_address(void) {
    struct kui_loader_sd card=reset(); card.high_capacity=false;
    struct kui_sci_async_probe_result result;
    assert(kui_sci_async_probe_run(&card,123,hw.baseline,NULL,NULL,&result)==KUI_SCI_ASYNC_OK);
    assert(hw.command_argument==123u*512u && hw.total_bytes==80u*514u);
    restored(&result);
}
static void test_sptr_input_monitors(void) {
    struct kui_loader_sd card=reset(); hw.dynamic_sptr_inputs=true;
    struct kui_sci_async_probe_result result;
    assert(kui_sci_async_test_read(SPTR,1)==0x04);
    assert(kui_sci_async_probe_run(&card,123,hw.baseline,NULL,NULL,&result)==KUI_SCI_ASYNC_OK);
    assert(hw.monitored_gpio_reads>=80);
    assert(kui_sci_async_test_read(SPTR,1)==0x01);
    assert(result.slow.passed==16 && result.fast.passed==64);
    restored(&result);
}
static void test_sptr_reads_pins_with_transmitter_on_or_off(void) {
    (void)reset();
    hw.sptr=0x83; /* High TxD output latch, EIO and SPB0IO enabled. */
    assert(hw.scr&0x20u);
    assert(kui_sci_async_test_read(SPTR,1)==0x86); /* RxD remains low. */
    hw.scr=0;
    assert(kui_sci_async_test_read(SPTR,1)==0x86);
    hw.rxd_pin=true; hw.sck_pin=false;
    assert(kui_sci_async_test_read(SPTR,1)==0x83);
    hw.sptr=0; /* Changing direction still does not change which pins read. */
    assert(kui_sci_async_test_read(SPTR,1)==0x01);
}
static void test_preexisting_gpio_output_no_touch(void) {
    const uint8_t directions[]={2,8,10};
    for(unsigned i=0;i<sizeof(directions);++i) {
        struct kui_loader_sd card=reset(); hw.sptr|=directions[i];
        uint8_t before=hw.sptr;
        struct kui_sci_async_probe_result result;
        assert(kui_sci_async_probe_run(&card,123,hw.baseline,NULL,NULL,&result)==KUI_SCI_ASYNC_UNSUPPORTED);
        assert(!result.started && !hw.writes && !hw.handler_writes && !hw.priority_writes);
        assert(!hw.command_count && hw.sptr==before);
        assert(result.slow.last_phase==KUI_SCI_ASYNC_PHASE_LEASE);
        assert(!result.slow.attempted && !result.slow.dma_started);
        assert((result.slow.snapshot_sptr&0x0au)==directions[i]);
    }
}
static void test_no_foreground_overlap(void) {
    struct kui_loader_sd card=reset(); hw.no_overlap=true;
    struct kui_sci_async_probe_result result;
    assert(kui_sci_async_probe_run(&card,123,hw.baseline,NULL,NULL,&result)==KUI_SCI_ASYNC_NO_OVERLAP);
    assert(result.status==KUI_SCI_ASYNC_NO_OVERLAP);
    assert(result.slow.passed==16 && result.fast.passed==64);
    assert(result.slow.dma_irqs==16 && result.fast.dma_irqs==64);
    assert(!result.slow.overlap_batches && !result.fast.overlap_batches);
    assert(!result.slow.overlap_iterations && !result.fast.overlap_iterations);
    assert(result.guards_ok && result.crc_ok && result.baseline_ok);
    assert(hw.total_bytes==80u*514u);
    restored(&result);
}
static void test_trailing_overrun(enum fault fault) {
    struct kui_loader_sd card=reset(); hw.fault=fault;
    struct kui_sci_async_probe_result result;
    assert(kui_sci_async_probe_run(&card,123,hw.baseline,NULL,NULL,&result)==KUI_SCI_ASYNC_OK);
    assert(result.slow.passed==16 && result.fast.passed==64);
    assert(result.slow.trailing_overruns==16 && result.fast.trailing_overruns==64);
    assert(result.guards_ok && result.crc_ok && result.baseline_ok);
    if(fault==TRAILING_ERI) {
        assert(result.slow.sci_error_irqs==16 && result.fast.sci_error_irqs==64);
        assert(!result.slow.dma_irqs && !result.fast.dma_irqs);
    }
    restored(&result);
}
static void test_unsupported_no_writes(void) {
    for(unsigned which=0;which<8;++which) {
        struct kui_loader_sd card=reset();
        if(which==0) hw.irq_mask=0xf0u;
        else if(which==1) hw.irq_mask=0x10000000u;
        else if(which==2) hw.inside_irq=true;
        else if(which==3) hw.priorities[IRQ_SRC_DMAC]=0;
        else if(which==4) hw.ssr|=RDRF;
        else if(which==5) hw.ssr|=ORER;
        else if(which==6) hw.dmaor&=~1u;
        else hw.irq_mask=0x50u; /* DMAC priority is also five. */
        unsigned old_mask=hw.irq_mask;
        struct kui_sci_async_probe_result result;
        assert(kui_sci_async_probe_run(&card,123,hw.baseline,NULL,NULL,&result)==KUI_SCI_ASYNC_UNSUPPORTED);
        assert(!result.started && !hw.writes && !hw.handler_writes && !hw.priority_writes);
        assert(!hw.command_count && hw.irq_mask==old_mask);
    }
}
static void test_partial_install_restore(void) {
    for(unsigned at=1;at<=3;++at) {
        struct kui_loader_sd card=reset(); hw.fail_install_at=at;
        struct kui_sci_async_probe_result result;
        assert(kui_sci_async_probe_run(&card,123,hw.baseline,NULL,NULL,&result)==KUI_SCI_ASYNC_UNSUPPORTED);
        assert(!result.started && !hw.writes && !hw.priority_writes && !hw.command_count);
        restored(&result);
    }
}
static void test_failed_partial_install_rollback(void) {
    struct kui_loader_sd card=reset();
    hw.fail_install_at=2; hw.fail_handler_restore=true;
    struct kui_sci_async_probe_result result;
    assert(kui_sci_async_probe_run(&card,123,hw.baseline,NULL,NULL,&result)==KUI_SCI_ASYNC_RESTORE);
    assert(result.status==KUI_SCI_ASYNC_RESTORE && !result.started);
    assert(!result.safe_restored && !result.handlers_restored && result.registers_restored);
    assert(hw.failed_handler_restores==1 && hw.install_attempts==2);
    assert(!hw.writes && !hw.priority_writes && !hw.command_count && !hw.dma_starts);
    assert(hw.handlers[0].hdl==hw.probe_handlers[0].hdl);
    assert(hw.handlers[0].data==hw.probe_handlers[0].data);
    unsigned handlers=hw.handler_writes;
    hw.fail_handler_restore=false;
    assert(kui_sci_async_probe_run(&card,123,hw.baseline,NULL,NULL,&result)==KUI_SCI_ASYNC_BUSY);
    assert(!result.started && !hw.writes && hw.handler_writes==handlers);
}
static void test_report_failed_restore(void) {
    struct kui_loader_sd card=reset(); hw.fault=BAD_CRC; hw.fail_restore_register=true;
    struct kui_sci_async_probe_result result;
    assert(kui_sci_async_probe_run(&card,123,hw.baseline,NULL,NULL,&result)==KUI_SCI_ASYNC_RESTORE);
    assert(!result.safe_restored && !result.registers_restored && result.handlers_restored);
    assert(!(hw.chcr&7u) && (hw.scr&0x70u)!=0x50u && !hw.selected);
}
static void test_dma_global_error_is_not_cleared(void) {
    quarantined_fault(DMA_FAULT,KUI_SCI_ASYNC_DMA_ERROR);
}
static bool cancel_if_foreign(void *ctx) { assert(ctx==&hw); return hw.foreign_active; }
static void foreign_preserved(enum fault fault) {
    struct kui_loader_sd card=reset(); hw.fault=fault;
    struct kui_sci_async_probe_result result;
    assert(kui_sci_async_probe_run(&card,123,hw.baseline,cancel_if_foreign,&hw,&result)==KUI_SCI_ASYNC_RESTORE);
    assert(!result.safe_restored && !result.registers_restored && !result.handlers_restored);
    assert(hw.sar==0x0c080000 && hw.dar==0x0c090000 && hw.tcr==64 && hw.chcr==0x1025);
    assert(hw.handlers[0].hdl==foreign_handler && hw.handlers[0].data==&foreign_data);
    assert(!hw.selected && !hw.original_calls);
    assert(hw.dma_starts==(fault==FOREIGN_DURING_FRAMING?0u:1u));
    if(fault==FOREIGN_DURING_DMA) {
        assert(hw.total_bytes==64 && !hw.cache_invalidates);
        assert(!result.guards_ok && !result.crc_ok && !result.baseline_ok);
    }
    unsigned writes=hw.writes;
    irq_context_t context={0};
    hw.probe_handlers[0].hdl(EXC_DMAC_DMTE1,&context,hw.probe_handlers[0].data);
    assert(hw.writes==writes && hw.chcr==0x1025);
    /* Failed restoration poisons this session, even if hardware is later idle. */
    hw.foreign_active=false; hw.chcr=0;
    assert(kui_sci_async_probe_run(&card,123,hw.baseline,NULL,NULL,&result)==KUI_SCI_ASYNC_BUSY);
    assert(!result.started && hw.writes==writes);
}
static void test_foreign_during_framing(void) { foreign_preserved(FOREIGN_DURING_FRAMING); }
static void test_foreign_after_dma(void) { foreign_preserved(FOREIGN_AFTER_DMA); }
static void test_foreign_during_dma(void) { foreign_preserved(FOREIGN_DURING_DMA); }
static void isolated(void (*test)(void)) {
    /* The production API deliberately has no reset for a poisoned session. */
    pid_t child=fork(); assert(child>=0);
    if(!child) { test(); _exit(0); }
    int status=0; assert(waitpid(child,&status,0)==child);
    assert(WIFEXITED(status) && WEXITSTATUS(status)==0);
}
int main(void) {
    test_success(false);
    test_success(true);
    test_standard_capacity_address();
    test_sptr_input_monitors();
    test_sptr_reads_pins_with_transmitter_on_or_off();
    test_preexisting_gpio_output_no_touch();
    test_no_foreground_overlap();
    isolated(test_early_error_quarantined);
    test_fault(BAD_CRC,KUI_SCI_ASYNC_CRC);
    test_fault(WRONG_DATA,KUI_SCI_ASYNC_MISMATCH);
    isolated(test_timeout_quarantined);
    test_fault(BAD_RESPONSE,KUI_SCI_ASYNC_COMMAND);
    test_fault(BAD_TOKEN,KUI_SCI_ASYNC_TOKEN);
    test_fault(TOKEN_NOT_ENDED,KUI_SCI_ASYNC_TIMEOUT);
    test_fault(BAD_GPIO_CONTROL,KUI_SCI_ASYNC_UNSUPPORTED);
    test_fault(BAD_GUARD,KUI_SCI_ASYNC_GUARD);
    test_fault(BAD_PADDING,KUI_SCI_ASYNC_GUARD);
    test_fault(TRAILING_FRAME_ERROR,KUI_SCI_ASYNC_RECEIVE_ERROR);
    test_fault(TRAILING_PARITY_ERROR,KUI_SCI_ASYNC_RECEIVE_ERROR);
    isolated(test_early_rx_irq_quarantined);
    isolated(test_missing_end_quarantined);
    test_trailing_overrun(TRAILING_ERROR);
    test_trailing_overrun(TRAILING_ERI);
    test_busy_no_writes();
    test_unsupported_no_writes();
    test_partial_install_restore();
    isolated(test_report_failed_restore);
    isolated(test_failed_partial_install_rollback);
    isolated(test_dma_global_error_is_not_cleared);
    isolated(test_foreign_during_framing);
    isolated(test_foreign_after_dma);
    isolated(test_foreign_during_dma);
    test_cancellation();
    puts("SCI async probe host tests passed");
    return 0;
}
