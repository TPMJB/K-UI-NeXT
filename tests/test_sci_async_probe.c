/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/sci_async_probe.h"
#include "sd_reader.h"
#include "sci_sd_bus.h"
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
#define STBCR 0xffc00004u
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
             TOKEN_NOT_ENDED, BAD_GPIO_CONTROL, HANDOFF_REASSERTED_ERROR,
             HANDOFF_STUCK_ERROR, HANDOFF_NO_TEND, BUS_FAULT_ON_DESELECT,
             BUS_FAULT_ON_COMMAND, BUS_FAULT_ON_READY_DESELECT,
             BUS_FAULT_ON_IDLE_CLOCK, BUS_FAULT_ON_READY_POLL,
             PERSISTENT_OVERRUN, LATE_DMA_AFTER_STOP, LATE_BYTE_AFTER_STOP,
             STREAM_OVERRUN, STREAM_NO_TAIL, STREAM_BAD_CRC };
/* CMD18 card model: token wait, block, fill gap, token, block... until CMD12,
 * then a stuff byte, R1 and the busy interval. */
enum stream_state { STREAM_OFF, STREAM_TOKEN, STREAM_DATA, STREAM_STUFF,
                    STREAM_R1, STREAM_BUSY };
static struct {
    uint8_t smr, brr, scr, ssr, rdr, scmr, sptr, stbcr;
    uint16_t pdtr;
    uint32_t sar, dar, tcr, chcr, dmaor;
    irq_cb_t handlers[3], probe_handlers[3];
    unsigned priorities[2], irq_mask;
    unsigned writes, handler_writes, priority_writes, dma_starts;
    unsigned cache_purges, cache_invalidates, work_ticks, transferred, total_bytes;
    unsigned command_position, command_count, selects, deselects;
    unsigned bus_bytes, ready_delay, token_delay;
    unsigned original_calls, dispatches;
    unsigned slow_starts, fast_starts;
    unsigned install_attempts, fail_install_at;
    unsigned monitored_gpio_reads, failed_handler_restores;
    unsigned flag_clears, faulty_framing_calls;
    unsigned module_asserts, module_resumes, module_polls;
    unsigned module_signature_bad, reinit_stage;
    uint32_t command_argument;
    uint32_t interrupted_pc, interrupted_sr;
    uint64_t now;
    uint8_t baseline[512], command_bytes[5], *dma_buffer;
    size_t dma_size;
    /* Registered DMA destinations, each at its own physical base (the first
     * at DMA_BASE); a transfer uses the one its start address falls in. */
    struct {uint8_t *host; size_t size; uint32_t base;} regions[8];
    unsigned region_count;
    uint32_t dma_base;
    bool selected, token_sent, response_sent, inside_irq, pending_dma;
    bool fault_fired, advancing, fail_restore_register, foreign_active;
    bool dynamic_sptr_inputs, no_overlap, fail_handler_restore;
    bool rxd_pin, sck_pin, token_rx_high;
    bool bus_fault, reassert_once;
    bool force_tail_overrun, tail_fast_only, receiver_stalled;
    bool module_assert_failure, module_resume_failure, module_reinitializing;
    bool bus_fault_on_tail_deselect, foreign_on_tail_deselect;
    bool timer_stopped;
    /* A payload overrun stops reception part way through the block. */
    bool aborted_dma, overrun_this_dma;
    unsigned fences, late_writes;
    bool bus_slow, speed_resync_failure;
    struct kui_sci_sd_fault first_bus_fault;
    enum fault fault;
    enum stream_state stream;
    bool multi, stream_drop_on_deselect;
    unsigned multi_count, stream_block, stream_pos, stream_wait, stream_gap;
    unsigned stream_gap_every, stream_gap_extra, stop_position, stop_count;
    unsigned stop_busy, busy_left, dma_total, stream_tail_lost;
    uint8_t stop_r1, stream_payload[514];
    uint32_t stream_bad_lba;
    unsigned stream_budget; /* streamed bytes per model step; 0 means 256 */
    /* Uptime counter reads, and one injected 2.5 ms CPU stall at stall_at. */
    uint64_t tick_reads, stall_at;
    bool stall_done;
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
    irq_context_t context={.pc=hw.interrupted_pc,.sr=hw.interrupted_sr};
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
static const uint8_t stop_command[6]={0x4c,0,0,0,0,0x61};
static void stream_block_bytes(unsigned block,uint8_t out[514]) {
    for(unsigned i=0;i<512;++i) out[i]=(uint8_t)(hw.baseline[i]+block*3u+1u);
    uint16_t crc=reference_crc(out,512);
    out[512]=(uint8_t)(crc>>8);out[513]=(uint8_t)crc;
}
static unsigned stream_gap_for(unsigned block) {
    return hw.stream_gap+(hw.stream_gap_every && !(block%hw.stream_gap_every)?hw.stream_gap_extra:0u);
}
/* One byte of the card's CMD18 output; the card sends whenever clocked. */
static uint8_t stream_byte(void) {
    if(hw.stream==STREAM_TOKEN) {
        if(hw.stream_wait) {--hw.stream_wait;return 0xff;}
        hw.stream=STREAM_DATA;hw.stream_pos=0;
        stream_block_bytes(hw.command_argument+hw.stream_block,hw.stream_payload);
        if(hw.fault==STREAM_BAD_CRC && !hw.fault_fired &&
           hw.command_argument+hw.stream_block==hw.stream_bad_lba) {
            hw.stream_payload[100]^=0x10u;hw.fault_fired=true; /* once, after its CRC */
        }
        return 0xfe;
    }
    if(hw.stream==STREAM_DATA) {
        uint8_t value=hw.stream_payload[hw.stream_pos++];
        if(hw.stream_pos==514) {
            hw.stream=STREAM_TOKEN;++hw.stream_block;
            hw.stream_wait=stream_gap_for(hw.command_argument+hw.stream_block);
        }
        return value;
    }
    return 0xff;
}
static uint8_t card_stream_clock(uint8_t in) {
    /* CMD12 arrives while the card keeps sending; a card that rejected CMD18
     * is idle but still answers it. */
    if(hw.stream==STREAM_OFF || hw.stream==STREAM_TOKEN || hw.stream==STREAM_DATA) {
        if(hw.stop_position || in!=0xff) {
            assert(hw.stop_position<6 && in==stop_command[hw.stop_position]);
            if(++hw.stop_position==6) {
                uint8_t out=stream_byte();
                hw.stop_position=0;++hw.stop_count;hw.stream=STREAM_STUFF;
                return out;
            }
        }
        return stream_byte();
    }
    assert(in==0xff);
    if(hw.stream==STREAM_STUFF) {hw.stream=STREAM_R1;return 0x3c;}
    if(hw.stream==STREAM_R1) {
        hw.busy_left=hw.stop_busy;hw.stream=hw.busy_left?STREAM_BUSY:STREAM_OFF;
        return hw.stop_r1;
    }
    if(hw.stream==STREAM_BUSY) {if(!--hw.busy_left) hw.stream=STREAM_OFF;return 0x00;}
    return 0xff;
}
/* Reception after the DMA stopped taking bytes: RDR fills with the first,
 * the second overruns and the receiver stops. */
static void stream_tail(void) {
    hw.rdr=reverse_byte(stream_byte());(void)stream_byte();
    hw.stream_tail_lost+=2;hw.ssr|=ORER|RDRF;hw.receiver_stalled=true;
}
static void advance(void) {
    if(hw.advancing) return;
    hw.advancing=true;
    if(receiving() && hw.fault!=STALLED) {
        bool once=hw.fault==EARLY_ERROR || hw.fault==LATE_DMA_AFTER_STOP ||
            hw.fault==LATE_BYTE_AFTER_STOP;
        bool overrun=once?!hw.fault_fired:hw.fault==PERSISTENT_OVERRUN && !hw.overrun_this_dma;
        if(overrun && hw.transferred>=64) {
            /* RDR was not read in time: reception stops mid-block and the
             * receiver needs the module reset before ordinary transfers. */
            hw.fault_fired=hw.overrun_this_dma=true;
            hw.aborted_dma=hw.receiver_stalled=true;
            if(hw.stream!=STREAM_OFF) stream_tail();
            hw.ssr|=ORER; deliver(EXC_SCI_ERI);
        }
        if(!hw.fault_fired && hw.transferred>=64 &&
           (hw.fault==DMA_FAULT || hw.fault==EARLY_RX_IRQ || hw.fault==FOREIGN_DURING_DMA)) {
            hw.fault_fired=true;
            if(hw.fault==EARLY_RX_IRQ) { hw.ssr|=RDRF; deliver(EXC_SCI_RXI); }
            else if(hw.fault==FOREIGN_DURING_DMA) {
                install_foreign_dma(); hw.ssr|=ORER; deliver(EXC_SCI_ERI);
            }
            else hw.dmaor|=4u;
        }
        if(receiving() && !(hw.ssr&ORER) && !(hw.dmaor&6u)) {
            bool stream=hw.stream!=STREAM_OFF;
            unsigned budget=stream?(hw.stream_budget?hw.stream_budget:256u):hw.no_overlap?514u:32u;
            for(unsigned bytes=0;bytes<budget && hw.tcr;++bytes) {
                assert(hw.dma_buffer && hw.dar>=hw.dma_base);
                size_t offset=hw.dar-hw.dma_base;
                assert(offset<hw.dma_size && hw.transferred<hw.dma_total);
                if(stream && hw.fault==STREAM_OVERRUN && hw.transferred==1000 && !hw.fault_fired) {
                    /* The DMA fell behind: one byte waits in RDR, the next
                     * overruns and reception stops. */
                    stream_tail();hw.fault_fired=hw.aborted_dma=true;break;
                }
                uint8_t value=stream?stream_byte():logical_byte(hw.transferred);
                hw.dma_buffer[offset]=reverse_byte(value);
                ++hw.dar; ++hw.transferred; ++hw.total_bytes; --hw.tcr;
            }
            if(!hw.tcr) {
                hw.chcr|=END; hw.pending_dma=true;
                /* Clocking continues past the count (unless a fault stops it). */
                if(stream && hw.fault!=STREAM_NO_TAIL) stream_tail();
                if(hw.fault==TRAILING_ERROR || hw.fault==HANDOFF_REASSERTED_ERROR ||
                   hw.fault==HANDOFF_STUCK_ERROR) hw.ssr|=ORER|RDRF;
                if(hw.force_tail_overrun && (!hw.tail_fast_only || hw.brr==0)) {
                    hw.ssr|=ORER|RDRF;
                    hw.receiver_stalled=true;
                }
                if(hw.fault==HANDOFF_REASSERTED_ERROR) hw.reassert_once=true;
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
void irq_restore(irq_mask_t mask) {
    if(hw.module_signature_bad && hw.module_resumes) assert(!(hw.scr&0xc4u));
    hw.irq_mask=mask;
}
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
/* The model's clock: each reading lets 37 us pass. */
static uint64_t model_now(void) { if(!hw.timer_stopped) hw.now+=37; advance(); return hw.now; }
/* KOS's timer_us_gettime64() as on the console: it counts TMU2's 80.2 ns ticks
 * as 80 ns, so a second reads only 997,498 us and the next one starts 2.5 ms
 * later. The probe must not time with it. */
uint64_t timer_us_gettime64(void) {
    uint64_t now=model_now();
    return now/1000000u*1000000u+now%1000000u*997498u/1000000u;
}
void kui_sci_async_test_work_tick(void) { ++hw.work_ticks; hw.now+=11; advance(); }
/* A transfer accepted before DE was cleared may still complete late: once for
 * LATE_BYTE_AFTER_STOP, and on every fence for LATE_DMA_AFTER_STOP. */
void kui_sci_async_test_bus_fence(void) {
    ++hw.fences;
    bool late=hw.fault==LATE_DMA_AFTER_STOP ||
        (hw.fault==LATE_BYTE_AFTER_STOP && !hw.late_writes);
    if(late && hw.aborted_dma && hw.tcr && hw.dma_buffer && !(hw.chcr&DE)) {
        size_t offset=hw.dar-hw.dma_base;
        assert(offset<hw.dma_size);
        hw.dma_buffer[offset]=reverse_byte(logical_byte(hw.transferred));
        ++hw.dar; ++hw.transferred; ++hw.total_bytes; --hw.tcr; ++hw.late_writes;
    }
}
timer_val_t kui_sci_async_test_ticks(void) {
    uint64_t now=model_now();
    return (timer_val_t){(uint32_t)(now/1000000u),(uint32_t)(now%1000000u*2u)};
}
void kui_sci_async_test_delay(uint32_t count) {
    assert(count);
    if(hw.module_reinitializing && hw.reinit_stage==3 && count>=(hw.brr?1024u:64u)) hw.reinit_stage=4;
}
uint32_t kui_sci_async_test_dma_address(const void *buffer,size_t count) {
    assert(buffer && count>=514 && count<=32768);
    assert(!((uintptr_t)buffer&31u));
    for(unsigned i=0;i<hw.region_count;++i) if(hw.regions[i].host==buffer) {
        if(count>hw.regions[i].size) hw.regions[i].size=count;
        return hw.regions[i].base;
    }
    assert(hw.region_count<8u);
    unsigned i=hw.region_count++;
    hw.regions[i].host=(uint8_t *)buffer;hw.regions[i].size=count;
    hw.regions[i].base=DMA_BASE+0x10000u*i;
    return hw.regions[i].base;
}
void kui_sci_async_test_cache_purge(void *buffer,size_t count) {
    assert(buffer && !((uintptr_t)buffer&31u)); assert(count && !(count&31u));
    ++hw.cache_purges;
}
void kui_sci_async_test_cache_invalidate(void *buffer,size_t count) {
    assert(buffer && !((uintptr_t)buffer&31u)); assert(count && !(count&31u));
    /* Never under a transfer that may still write: a streamed block is
     * checked while the next one goes to the other receive area. */
    if((hw.chcr&(DE|END))==DE)
        assert(!hw.dma_buffer || (uint8_t *)buffer+count<=hw.dma_buffer ||
               (uint8_t *)buffer>=hw.dma_buffer+hw.dma_size);
    else assert(!(hw.scr&0x40u)); /* no receive request enabled without a channel */
    ++hw.cache_invalidates;
    if(hw.fault==FOREIGN_AFTER_DMA) install_foreign_dma();
}
uint32_t kui_sci_async_test_read(uint32_t address,unsigned width) {
    /* SCI has no usable register interface while its module clock is gated. */
    if(address>=SMR && address<=SPTR) assert(!(hw.stbcr&1u));
    switch(address) {
        case STBCR: assert(width==1); ++hw.module_polls; return hw.stbcr;
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
        /* TMU2 uptime counter: a 2 MHz down-counter reloading each second.
         * Each read lets one tick pass. */
        case 0xffd80020u: assert(width==4); return 1999999u;
        case 0xffd80024u: {
            assert(width==4);
            uint64_t ticks=hw.now*2u+hw.tick_reads++;
            if(hw.stall_at && hw.now>=hw.stall_at && !hw.stall_done) {hw.now+=2500;hw.stall_done=true;}
            return 1999999u-(uint32_t)(ticks%2000000u);
        }
        default: assert(!"Unexpected MMIO read"); return 0;
    }
}
void kui_sci_async_test_write(uint32_t address,uint32_t value,unsigned width) {
    ++hw.writes;
    if(address>=SMR && address<=SPTR) assert(!(hw.stbcr&1u));
    if(hw.foreign_active)
        assert(address!=SAR && address!=DAR && address!=TCR && address!=CHCR);
    switch(address) {
        case STBCR: {
            assert(width==1 && (hw.irq_mask&0xf0u)==0xf0u);
            assert(!hw.selected && !hw.scr && !hw.chcr && hw.tcr==0);
            assert((value&~1u)==(hw.stbcr&~1u));
            if(value&1u) {
                ++hw.module_asserts;
                assert((hw.transferred==hw.dma_total || hw.aborted_dma) && (hw.sptr&0x8bu)==0x83u);
                if(!hw.module_assert_failure) hw.stbcr=value;
            } else {
                ++hw.module_resumes;
                if(hw.module_resume_failure && (hw.stbcr&1u)) break;
                bool stopped=(hw.stbcr&1u)!=0;hw.stbcr=value;
                if(stopped) {
                    /* Documented SCI register defaults, not a claim that
                     * the console's hidden fault is reproduced by a mock. */
                    hw.smr=hw.scr=hw.scmr=hw.rdr=0;hw.brr=0xff;hw.ssr=TDRE|TEND;
                    hw.receiver_stalled=false;
                    if(hw.module_signature_bad&KUI_SCI_ASYNC_MODULE_RESET_BAD_SCR) hw.scr=0x50;
                    if(hw.module_signature_bad&KUI_SCI_ASYNC_MODULE_RESET_BAD_SMR) hw.smr=0x80;
                    if(hw.module_signature_bad&KUI_SCI_ASYNC_MODULE_RESET_BAD_BRR) hw.brr=0;
                    if(hw.module_signature_bad&KUI_SCI_ASYNC_MODULE_RESET_BAD_SCMR) hw.scmr=1;
                    if(hw.module_signature_bad&KUI_SCI_ASYNC_MODULE_RESET_BAD_SSR) hw.ssr|=ORER;
                    if(hw.module_signature_bad&KUI_SCI_ASYNC_MODULE_RESET_BAD_SPTR) hw.sptr&=(uint8_t)~0x80u;
                    hw.module_reinitializing=!hw.module_signature_bad;
                    hw.reinit_stage=0;
                }
            }
            break;
        }
        case SMR:
            assert(width==1 && !hw.selected);
            if(hw.module_reinitializing) {assert(hw.reinit_stage==1 && !hw.scr);hw.reinit_stage=2;}
            hw.smr=value;break;
        case BRR:
            assert(width==1 && !hw.selected);
            if(hw.module_reinitializing) {assert(hw.reinit_stage==2 && !hw.scr);hw.reinit_stage=3;}
            hw.brr=value;break;
        case SCR:
            assert(width==1);
            if(hw.module_reinitializing && value==0x30) {
                assert(hw.reinit_stage==5 && hw.smr==0x80 && !hw.scmr);
                assert(hw.brr==0 || hw.brr==31);
                hw.module_reinitializing=false;
            }
            if((value&0x70u)==0x50u && !(hw.scr&0x10u)) {
                if(hw.stream!=STREAM_OFF) {
                    /* CMD18: a masked measurement without completion IRQ, or
                     * a streamed block of 513 bytes with it; both from the
                     * first payload byte of a block. */
                    assert(hw.selected && hw.stream==STREAM_DATA && !hw.stream_pos);
                    assert(hw.sar==0x1fe00014u && hw.cache_purges && hw.smr==0x80 && !hw.brr);
                    if((hw.chcr&0xffffu)==0x4915u) assert(hw.tcr==513);
                    else assert((hw.chcr&0xffffu)==0x4911u && hw.tcr>=513 && (hw.irq_mask&0xf0u)==0xf0u);
                } else {
                    assert(hw.selected && hw.token_sent && hw.smr==0x80);
                    assert(hw.tcr==514 && hw.sar==0x1fe00014u);
                    assert((hw.chcr&0xffffu)==0x4915u && hw.cache_purges);
                }
                assert((hw.sptr&3u)==3u && (hw.sptr&0x80u));
                assert(hw.brr==0 || hw.brr==31);
                if(hw.brr) ++hw.slow_starts; else ++hw.fast_starts;
                hw.transferred=0; ++hw.dma_starts; hw.dma_total=hw.tcr;
                hw.dma_buffer=NULL;
                for(unsigned i=0;i<hw.region_count;++i)
                    if(hw.dar>=hw.regions[i].base && hw.dar+hw.tcr<=hw.regions[i].base+hw.regions[i].size) {
                        hw.dma_buffer=hw.regions[i].host;hw.dma_size=hw.regions[i].size;
                        hw.dma_base=hw.regions[i].base;
                    }
                assert(hw.dma_buffer);
                hw.aborted_dma=hw.overrun_this_dma=false;
            }
            hw.scr=value;
            if(!(value&0x20u)) hw.ssr|=TEND;
            if(hw.fault==HANDOFF_NO_TEND && hw.dma_starts) hw.ssr&=(uint8_t)~TEND;
            break;
        case SSR:
            assert(width==1);
            if((hw.ssr&(ORER|RDRF)) && !(value&(ORER|RDRF))) ++hw.flag_clears;
            hw.ssr&=value;
            /* A receive flag still visible after the first cleanup write
             * must be checked again before the sticky-fault bus is called.
             * This fault injection tests the handoff contract, not a claim
             * that the console report proved this exact peripheral timing. */
            if(hw.reassert_once || (hw.fault==HANDOFF_STUCK_ERROR && hw.dma_starts)) {
                hw.ssr|=ORER|RDRF;hw.reassert_once=false;
            }
            break;
        case SCMR:
            assert(width==1);
            if(hw.module_reinitializing) {assert(hw.reinit_stage==0 && !hw.scr);hw.reinit_stage=1;}
            hw.scmr=value;break;
        case SPTR:
            assert(width==1);
            if(hw.module_reinitializing) {assert(hw.reinit_stage==4);hw.reinit_stage=5;}
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
static void latch_bus_fault(uint32_t flag) {
    if(!hw.bus_fault) {
        hw.first_bus_fault=(struct kui_sci_sd_fault){.valid=1,.wait_flag=flag,
            .ssr=hw.ssr,.scr=hw.scr,.smr=hw.smr,.brr=hw.brr,.scmr=hw.scmr,
            .sptr=hw.sptr,.pdtr=hw.pdtr,.polls=10000};
        hw.bus_fault=true;
    }
    hw.scr=0;
}
static void bus_select(void *ctx,bool selected) {
    assert(ctx==&hw && !(hw.stbcr&1u));
    if(selected) {
        assert(!hw.selected); ++hw.selects;
        /* A card in a CMD18 stream keeps its place across deselection. */
        if(hw.stream==STREAM_OFF) {
            hw.command_position=0; hw.command_argument=0;
            hw.token_sent=false; hw.response_sent=false; hw.multi=false;
        }
    } else {
        assert((hw.scr&0x70u)!=0x50u && (!(hw.chcr&DE) || hw.foreign_active));
        ++hw.deselects;
        /* Production select(false) invokes wait_flag(TEND), which latches
         * an error permanently even though this callback cannot report it. */
        if((hw.fault==BUS_FAULT_ON_DESELECT && hw.dma_starts) ||
           (hw.fault==BUS_FAULT_ON_READY_DESELECT && hw.dma_starts==17 && hw.scr==0x30))
            latch_bus_fault(TEND);
        if(!hw.bus_fault && ((hw.ssr&0x38u) || !(hw.ssr&TEND))) {
            latch_bus_fault(TEND);
        }
        if(hw.receiver_stalled && hw.bus_fault_on_tail_deselect) latch_bus_fault(TEND);
        if(hw.receiver_stalled && hw.foreign_on_tail_deselect) install_foreign_dma();
        if(hw.stream_drop_on_deselect && (hw.stream==STREAM_TOKEN || hw.stream==STREAM_DATA)) {
            /* A card that abandons the read: no further data token. */
            hw.stream=STREAM_TOKEN;hw.stream_wait=~0u;
        }
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
    assert(ctx==&hw && (hw.scr&0x70u)!=0x50u && !(hw.stbcr&1u));
    ++hw.bus_bytes;
    if(hw.bus_fault) {++hw.faulty_framing_calls;return 0xff;}
    if(hw.receiver_stalled) {latch_bus_fault(RDRF);return 0xff;}
    if(hw.fault==BUS_FAULT_ON_COMMAND && hw.dma_starts==17 && byte==0x51) {
        latch_bus_fault(RDRF);return 0xff;
    }
    if(hw.dma_starts==17 && byte==0xff &&
       ((hw.fault==BUS_FAULT_ON_IDLE_CLOCK && !hw.selected) ||
        (hw.fault==BUS_FAULT_ON_READY_POLL && hw.selected && !hw.command_position))) {
        /* A modeled wait timeout is sampled before stopping SCR. This tests
         * report propagation, not a claim about the console's failing flag. */
        latch_bus_fault(RDRF);return 0xff;
    }
    /* The existing synchronous bus owns framing and its clock-state cache. */
    if(slow!=hw.bus_slow) {hw.brr=slow?31:0;hw.bus_slow=slow;}
    assert(hw.brr==(slow?31u:0u));hw.scr=0x30;
    if(!hw.selected) { assert(byte==0xff); return 0xff; }
    if(!hw.command_position && byte==0xff) {
        if(hw.ready_delay) {--hw.ready_delay;return 0;}
        return 0xff;
    }
    if(hw.command_position<6) {
        unsigned pos=hw.command_position++;
        if(pos<5) hw.command_bytes[pos]=byte;
        if(!pos) {
            assert(byte==0x51 || byte==0x52);
            hw.multi=byte==0x52;
            if(hw.multi) ++hw.multi_count; else ++hw.command_count;
        }
        else if(pos<5) hw.command_argument=(hw.command_argument<<8)|byte;
        else assert(byte==reference_crc7(hw.command_bytes));
        return 0xff;
    }
    if(hw.multi && hw.response_sent) return card_stream_clock(byte);
    assert(byte==0xff);
    if(!hw.response_sent) {
        hw.response_sent=true;
        if(hw.multi && hw.fault!=BAD_RESPONSE) {
            hw.stream=STREAM_TOKEN;hw.stream_block=0;hw.stream_wait=hw.token_delay;
            hw.stop_position=0;
        }
        return hw.fault==BAD_RESPONSE?0x04:0x00;
    }
    if(!hw.token_sent) {
        if(hw.token_delay) {--hw.token_delay;return 0xff;}
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
bool kui_sci_sd_healthy(void) { return !hw.bus_fault; }
bool kui_sci_sd_resync_speed(void) {
    assert((hw.irq_mask&0xf0u)==0xf0u && !hw.bus_fault && !(hw.stbcr&1u));
    assert(!hw.selected && hw.smr==0x80 && !hw.scmr && !(hw.scr&0xc4u));
    if(hw.speed_resync_failure) return false;
    hw.bus_slow=hw.brr!=0;return true;
}
void kui_sci_sd_fault_get(struct kui_sci_sd_fault *out) {
    if(out) *out=hw.first_bus_fault;
}
static uint32_t bus_ticks(void *ctx) { assert(ctx==&hw); return (uint32_t)(hw.now++); }
static struct kui_loader_sd reset(void) {
    memset(&hw,0,sizeof(hw));
    hw.smr=0x80; hw.brr=0; hw.scr=0x30; hw.ssr=TDRE|TEND; hw.sptr=0x04;
    hw.sck_pin=true;
    hw.pdtr=0x1280;
    hw.interrupted_pc=0x8c123456;hw.interrupted_sr=0x40000000;
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
    if(!hw.bus_fault) assert(!hw.bus_slow);
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
    assert(result.slow.handoff_checks==16 && result.fast.handoff_checks==64);
    assert(!result.slow.handoff_retries && !result.fast.handoff_retries);
    assert(!result.slow.handoff_failures && !result.fast.handoff_failures);
    assert(result.slow.handoff_scr==0x30 && result.fast.handoff_scr==0x30);
    assert(!result.slow.bus_faults && !result.fast.bus_faults);
    assert(!result.slow.module_reset_attempts && !result.fast.module_reset_attempts);
    assert(!hw.module_asserts && !hw.module_resumes);
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
    assert(!result.slow.module_reset_attempts && !hw.module_asserts && !hw.module_resumes);
    assert(hw.chcr==0x4910 && !(hw.chcr&(DE|IE)) && (hw.scr&0x70u)!=0x50u);
    assert(hw.sar==0x1fe00014 && hw.dar==DMA_BASE+hw.total_bytes);
    assert(hw.tcr==514u-hw.total_bytes);
    assert(hw.sar!=0x0c002000 && hw.dar!=0x0c004000 && hw.tcr!=19);
    assert(hw.dmaor==(fault==DMA_FAULT?0x0305u:0x0301u));
    if(fault==STALLED) assert(result.slow.timeouts==1);
    if(fault==LATE_DMA_AFTER_STOP) {
        /* Still moving after the stop: never proven idle, never retried. */
        assert(result.slow.undrained_overruns==1 && !result.slow.payload_overruns);
        assert(!result.slow.overrun_retries && hw.late_writes==3 && hw.command_count==1);
    }
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
static enum kui_sci_async_status poll_complete(struct kui_sci_async_reader *reader);
static void begin_active(struct kui_sci_async_reader *reader);
static void test_late_dma_quarantined(void) { quarantined_fault(LATE_DMA_AFTER_STOP,KUI_SCI_ASYNC_RECEIVE_ERROR); }
/* One overrun mid-payload: proven idle, receiver reset, rest of the block
 * clocked out, the same CMD17 re-issued, and every later read unaffected. */
static void test_payload_overrun_retried(enum fault fault) {
    struct kui_loader_sd card=reset(); hw.fault=fault;
    struct kui_sci_async_probe_result result;
    assert(kui_sci_async_probe_run(&card,123,hw.baseline,NULL,NULL,&result)==KUI_SCI_ASYNC_OK);
    assert(result.status==KUI_SCI_ASYNC_OK && !result.dma_quarantined && !result.foreign_dma);
    assert(result.slow.attempted==16 && result.slow.passed==16);
    assert(result.fast.attempted==64 && result.fast.passed==64);
    assert(result.slow.payload_overruns==1 && result.slow.overrun_retries==1);
    assert(!result.slow.undrained_overruns && !result.fast.payload_overruns);
    assert(result.slow.sci_error_irqs==1 && result.slow.dma_irqs==16 && result.slow.dma_started==17);
    assert(result.slow.module_reset_attempts==1 && result.slow.module_resets==1);
    assert(result.slow.handoff_checks==17 && !result.slow.handoff_failures);
    assert(hw.command_count==81 && hw.dma_starts==81 && hw.module_asserts==1);
    unsigned late=fault==LATE_BYTE_AFTER_STOP?1u:0u;
    assert(hw.total_bytes==80u*514u+64u+late && hw.late_writes==late && hw.fences>=4);
    const struct kui_sci_async_fault *f=&result.first_overrun;
    assert(f->valid && f->event==EXC_SCI_ERI && f->lba==123 && f->tcr==450);
    assert(f->dar==DMA_BASE+64 && f->chcr==0x4915 && f->ssr==(TDRE|TEND|ORER));
    assert(f->scr==0x50 && f->context_valid && f->pc==hw.interrupted_pc);
    assert(!result.fault.valid && result.guards_ok && result.crc_ok && result.baseline_ok);
    assert(result.slow.framing_us && result.slow.finish_us && result.fast.framing_us);
    restored(&result);
}
static void test_payload_overrun_once(void) { test_payload_overrun_retried(EARLY_ERROR); }
static void test_payload_overrun_late_byte(void) { test_payload_overrun_retried(LATE_BYTE_AFTER_STOP); }
static void test_payload_overrun_cancel(void) {
    struct kui_loader_sd card=reset();hw.fault=PERSISTENT_OVERRUN;
    struct kui_sci_async_reader reader={0};struct kui_sci_async_probe_result result;
    assert(kui_sci_async_open(&reader,&card,&result)==KUI_SCI_ASYNC_OK);
    begin_active(&reader);
    assert(kui_sci_async_cancel(&reader)==KUI_SCI_ASYNC_PENDING);
    assert(poll_complete(&reader)==KUI_SCI_ASYNC_CANCELLED);
    /* The receiver is still reset before the request ends; no re-issue. */
    assert(result.fast.payload_overruns==1 && !result.fast.overrun_retries);
    assert(result.fast.module_resets==1 && hw.command_count==1 && hw.dma_starts==1);
    assert(kui_sci_async_close(&reader)==KUI_SCI_ASYNC_CANCELLED);
    assert(!result.dma_quarantined && !result.fast.passed);
    restored(&result);
}
static void test_token_bytes_and_quantum(void) {
    struct kui_loader_sd card=reset();hw.token_delay=5;
    struct kui_sci_async_reader reader={0};struct kui_sci_async_probe_result result;
    uint8_t payload[512];
    assert(kui_sci_async_open(&reader,&card,&result)==KUI_SCI_ASYNC_OK);
    kui_sci_async_set_framing_quantum(&reader,4096);
    assert(kui_sci_async_begin(&reader,123,false)==KUI_SCI_ASYNC_OK);
    /* One poll sends the command, waits for the token and starts the DMA. */
    assert(kui_sci_async_poll(&reader)==KUI_SCI_ASYNC_PENDING && hw.dma_starts==1 && receiving());
    assert(poll_complete(&reader)==KUI_SCI_ASYNC_OK);
    assert(kui_sci_async_finish(&reader,payload,hw.baseline)==KUI_SCI_ASYNC_OK);
    assert(!memcmp(payload,hw.baseline,512));
    assert(result.fast.token_bytes==5 && result.fast.max_token_bytes==5);
    hw.token_delay=2;
    assert(kui_sci_async_begin(&reader,124,false)==KUI_SCI_ASYNC_OK);
    assert(poll_complete(&reader)==KUI_SCI_ASYNC_OK);
    assert(kui_sci_async_finish(&reader,payload,hw.baseline)==KUI_SCI_ASYNC_OK);
    assert(result.fast.token_bytes==7 && result.fast.max_token_bytes==5);
    assert(result.fast.token_us>=result.fast.max_token_us && result.fast.max_token_us);
    assert(result.fast.framing_us && result.fast.finish_us && result.fast.passed==2);
    /* Out-of-range quanta are clamped, never zero or unbounded. */
    kui_sci_async_set_framing_quantum(&reader,0);
    assert(kui_sci_async_begin(&reader,125,false)==KUI_SCI_ASYNC_OK);
    unsigned bytes=hw.bus_bytes;
    assert(kui_sci_async_poll(&reader)==KUI_SCI_ASYNC_PENDING);
    assert(hw.bus_bytes-bytes<=8 && hw.dma_starts==2 && !receiving());
    assert(poll_complete(&reader)==KUI_SCI_ASYNC_OK);
    assert(kui_sci_async_finish(&reader,payload,hw.baseline)==KUI_SCI_ASYNC_OK);
    assert(kui_sci_async_close(&reader)==KUI_SCI_ASYNC_OK);restored(&result);
}
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
    assert(result.slow.module_reset_attempts==16 && result.fast.module_reset_attempts==64);
    assert(result.slow.module_resets==16 && result.fast.module_resets==64);
    assert(!result.slow.module_reset_failures && !result.fast.module_reset_failures);
    assert(result.guards_ok && result.crc_ok && result.baseline_ok);
    if(fault==TRAILING_ERI) {
        assert(result.slow.sci_error_irqs==16 && result.fast.sci_error_irqs==64);
        assert(!result.slow.dma_irqs && !result.fast.dma_irqs);
    }
    restored(&result);
}
static void test_fast_tail_module_reset(void) {
    struct kui_loader_sd card=reset();
    hw.force_tail_overrun=hw.tail_fast_only=true;
    hw.stbcr=0x6eu;
    struct kui_sci_async_probe_result result;
    assert(kui_sci_async_probe_run(&card,123,hw.baseline,NULL,NULL,&result)==KUI_SCI_ASYNC_OK);
    assert(result.slow.passed==16 && result.fast.passed==64);
    assert(!result.slow.trailing_overruns && !result.slow.module_reset_attempts);
    assert(result.fast.trailing_overruns==64 && result.fast.module_reset_attempts==64);
    assert(result.fast.module_resets==64 && !result.fast.module_reset_failures);
    assert(result.fast.module_reset_state==KUI_SCI_ASYNC_MODULE_RESET_OK);
    assert(result.fast.module_stb_before==0x6e && result.fast.module_stb_stopped==0x6f);
    assert(result.fast.module_stb_after==0x6e && hw.stbcr==0x6e);
    assert(hw.module_asserts==64 && hw.module_resumes==64 && hw.module_polls==64u*4u);
    assert(!hw.receiver_stalled && !hw.module_reinitializing && !hw.bus_fault);
    assert(hw.command_count==80 && hw.dma_starts==80);
    restored(&result);
}
static void test_module_assert_failure(void) {
    struct kui_loader_sd card=reset();hw.force_tail_overrun=true;hw.module_assert_failure=true;
    struct kui_sci_async_probe_result result;
    assert(kui_sci_async_probe_run(&card,123,hw.baseline,NULL,NULL,&result)==KUI_SCI_ASYNC_HANDOFF);
    assert(result.slow.module_reset_attempts==1 && !result.slow.module_resets);
    assert(result.slow.module_reset_failures==1 && result.slow.handoff_failures==1);
    assert(result.slow.module_reset_state==KUI_SCI_ASYNC_MODULE_RESET_ASSERT_FAILED);
    assert(hw.module_asserts==1 && hw.module_resumes==1 && hw.module_polls==11);
    assert(hw.stbcr==0 && hw.command_count==1 && hw.dma_starts==1);
    assert(result.guards_ok && result.crc_ok && result.baseline_ok);
    restored(&result);
}
static void test_module_resume_failure(void) {
    struct kui_loader_sd card=reset();hw.force_tail_overrun=true;hw.module_resume_failure=true;
    hw.stbcr=0x2eu;
    struct kui_sci_async_probe_result result;
    assert(kui_sci_async_probe_run(&card,123,hw.baseline,NULL,NULL,&result)==KUI_SCI_ASYNC_RESTORE);
    assert(result.operation_status==KUI_SCI_ASYNC_HANDOFF && !result.safe_restored);
    assert(!result.registers_restored && result.handlers_restored && !result.dma_quarantined);
    assert(!result.foreign_dma && result.guards_ok && result.crc_ok && result.baseline_ok);
    assert(result.slow.module_reset_attempts==1 && !result.slow.module_resets);
    assert(result.slow.module_reset_failures==1 && result.slow.handoff_failures==1);
    assert(result.slow.module_reset_state==KUI_SCI_ASYNC_MODULE_RESET_RESUME_FAILED);
    assert(result.slow.module_stb_before==0x2e && result.slow.module_stb_stopped==0x2f);
    assert(result.slow.module_stb_after==0x2f && hw.stbcr==0x2f);
    assert(hw.module_asserts==1 && hw.module_resumes==1 && hw.module_polls==11);
    assert(hw.command_count==1 && hw.dma_starts==1 && !hw.selected && !hw.pending_dma);
    /* Source is gated; the owned DMA registers and callbacks are still
     * restored, with no attempted SCI MMIO or bus callback after the stop. */
    assert(hw.sar==0x0c002000 && hw.dar==0x0c004000 && hw.tcr==19 && hw.chcr==0x4000);
    assert(hw.priorities[IRQ_SRC_SCI1]==2 && hw.priorities[IRQ_SRC_DMAC]==5 && !hw.irq_mask);
    for(unsigned i=0;i<3;++i) {
        assert(hw.handlers[i].hdl==original_handler && hw.handlers[i].data==&handler_data[i]);
    }
    unsigned writes=hw.writes, polls=hw.module_polls;
    assert(kui_sci_async_probe_run(&card,123,hw.baseline,NULL,NULL,&result)==KUI_SCI_ASYNC_BUSY);
    assert(!result.started && hw.writes==writes && hw.module_polls==polls);
}
static void test_module_reset_signature(void) {
    for(unsigned bad=1;bad<=32;bad<<=1) {
        struct kui_loader_sd card=reset();hw.force_tail_overrun=true;hw.module_signature_bad=bad;
        struct kui_sci_async_probe_result result;
        assert(kui_sci_async_probe_run(&card,123,hw.baseline,NULL,NULL,&result)==KUI_SCI_ASYNC_HANDOFF);
        assert(result.slow.module_reset_attempts==1 && !result.slow.module_resets);
        assert(result.slow.module_reset_failures==1 && result.slow.handoff_failures==1);
        assert(result.slow.module_reset_state==(KUI_SCI_ASYNC_MODULE_RESET_SIGNATURE|bad));
        assert(hw.module_asserts==1 && hw.module_resumes==1 && hw.module_polls==4);
        assert(!hw.stbcr && hw.command_count==1 && hw.dma_starts==1);
        restored(&result);
    }
}
static void test_module_reset_validation_gates(void) {
    const enum fault faults[]={BAD_CRC,WRONG_DATA,BAD_GUARD,BAD_PADDING};
    const enum kui_sci_async_status statuses[]={KUI_SCI_ASYNC_CRC,KUI_SCI_ASYNC_MISMATCH,
        KUI_SCI_ASYNC_GUARD,KUI_SCI_ASYNC_GUARD};
    for(unsigned i=0;i<sizeof(faults)/sizeof(faults[0]);++i) {
        struct kui_loader_sd card=reset();hw.force_tail_overrun=true;hw.fault=faults[i];
        struct kui_sci_async_probe_result result;
        assert(kui_sci_async_probe_run(&card,123,hw.baseline,NULL,NULL,&result)==statuses[i]);
        assert(!result.slow.module_reset_attempts && !hw.module_asserts && !hw.module_resumes);
        assert(hw.command_count==1 && hw.dma_starts==1);
        restored(&result);
    }
    struct kui_loader_sd card=reset();hw.force_tail_overrun=true;hw.bus_fault_on_tail_deselect=true;
    struct kui_sci_async_probe_result result;
    assert(kui_sci_async_probe_run(&card,123,hw.baseline,NULL,NULL,&result)==KUI_SCI_ASYNC_BUS_FAULT);
    assert(result.slow.bus_faults==1 && !result.slow.module_reset_attempts);
    assert(!hw.module_asserts && !hw.module_resumes && hw.command_count==1);
    restored(&result);
}
static void test_module_reset_rechecks_dma_owner(void) {
    struct kui_loader_sd card=reset();hw.force_tail_overrun=true;hw.foreign_on_tail_deselect=true;
    struct kui_sci_async_probe_result result;
    assert(kui_sci_async_probe_run(&card,123,hw.baseline,NULL,NULL,&result)==KUI_SCI_ASYNC_RESTORE);
    assert(result.operation_status==KUI_SCI_ASYNC_DMA_ERROR && result.foreign_dma);
    assert(!result.safe_restored && result.slow.module_reset_attempts==1);
    assert(result.slow.module_reset_state==KUI_SCI_ASYNC_MODULE_RESET_PRECONDITION);
    assert(result.slow.module_reset_failures==1 && !hw.module_asserts && !hw.module_resumes);
    assert(hw.sar==0x0c080000 && hw.dar==0x0c090000 && hw.tcr==64 && hw.chcr==0x1025);
    assert(hw.handlers[0].hdl==foreign_handler && hw.handlers[0].data==&foreign_data);
}
static void test_handoff_rechecks_receive_flags(void) {
    struct kui_loader_sd card=reset();hw.fault=HANDOFF_REASSERTED_ERROR;
    struct kui_sci_async_probe_result result;
    assert(kui_sci_async_probe_run(&card,123,hw.baseline,NULL,NULL,&result)==KUI_SCI_ASYNC_OK);
    assert(result.slow.passed==16 && result.fast.passed==64);
    assert(result.slow.trailing_overruns==16 && result.fast.trailing_overruns==64);
    assert(result.slow.handoff_retries==16 && result.fast.handoff_retries==64);
    assert(!result.slow.handoff_failures && !result.fast.handoff_failures);
    assert(!hw.bus_fault && !hw.faulty_framing_calls && hw.flag_clears==160);
    restored(&result);
}
static void test_handoff_stuck_receive_flags(void) {
    struct kui_loader_sd card=reset();hw.fault=HANDOFF_STUCK_ERROR;
    struct kui_sci_async_probe_result result;
    assert(kui_sci_async_probe_run(&card,123,hw.baseline,NULL,NULL,&result)==KUI_SCI_ASYNC_RESTORE);
    assert(result.operation_status==KUI_SCI_ASYNC_HANDOFF && !result.safe_restored);
    assert(!result.dma_quarantined && result.handlers_restored && result.registers_restored);
    assert(result.slow.handoff_checks==1 && result.slow.handoff_failures==1);
    assert(result.slow.handoff_retries==8 && result.slow.last_phase==KUI_SCI_ASYNC_PHASE_HANDOFF);
    assert(result.slow.handoff_ssr&(ORER|RDRF));
    assert(result.guards_ok && result.crc_ok && result.baseline_ok);
    assert(hw.command_count==1 && hw.dma_starts==1 && !hw.faulty_framing_calls);
}
static void test_handoff_requires_idle(void) {
    struct kui_loader_sd card=reset();hw.fault=HANDOFF_NO_TEND;
    struct kui_sci_async_probe_result result;
    assert(kui_sci_async_probe_run(&card,123,hw.baseline,NULL,NULL,&result)==KUI_SCI_ASYNC_HANDOFF);
    assert(result.slow.handoff_checks==1 && result.slow.handoff_failures==1);
    assert(result.slow.handoff_retries==8 && !(result.slow.handoff_ssr&TEND));
    assert(result.slow.last_phase==KUI_SCI_ASYNC_PHASE_HANDOFF);
    assert(hw.command_count==1 && hw.dma_starts==1 && !hw.faulty_framing_calls);
    restored(&result);
}
static void test_bus_fault_has_distinct_status(void) {
    const enum fault faults[]={BUS_FAULT_ON_DESELECT,BUS_FAULT_ON_COMMAND,
        BUS_FAULT_ON_READY_DESELECT,BUS_FAULT_ON_IDLE_CLOCK,BUS_FAULT_ON_READY_POLL};
    const enum kui_sci_async_framing_step steps[]={KUI_SCI_ASYNC_FRAMING_HANDOFF,
        KUI_SCI_ASYNC_FRAMING_COMMAND,KUI_SCI_ASYNC_FRAMING_DESELECT,
        KUI_SCI_ASYNC_FRAMING_IDLE_CLOCK,KUI_SCI_ASYNC_FRAMING_READY};
    for(unsigned i=0;i<sizeof(faults)/sizeof(faults[0]);++i) {
        struct kui_loader_sd card=reset();hw.fault=faults[i];
        struct kui_sci_async_probe_result result;
        assert(kui_sci_async_probe_run(&card,123,hw.baseline,NULL,NULL,&result)==KUI_SCI_ASYNC_BUS_FAULT);
        assert(result.operation_status==KUI_SCI_ASYNC_BUS_FAULT);
        assert(hw.bus_fault && !hw.faulty_framing_calls);
        const struct kui_sci_async_stage *s=faults[i]==BUS_FAULT_ON_DESELECT?&result.slow:&result.fast;
        assert(s->framing_step==steps[i] && s->framing_index==0);
        assert(s->bus_fault_valid==1 && s->bus_fault_polls==10000);
        assert(s->bus_wait_flag==hw.first_bus_fault.wait_flag);
        assert(s->bus_fault_ssr==hw.first_bus_fault.ssr && s->bus_fault_scr==hw.first_bus_fault.scr);
        assert(s->bus_fault_smr==hw.first_bus_fault.smr && s->bus_fault_brr==hw.first_bus_fault.brr);
        assert(s->bus_fault_scmr==hw.first_bus_fault.scmr && s->bus_fault_sptr==hw.first_bus_fault.sptr);
        assert(s->bus_fault_pdtr==hw.first_bus_fault.pdtr);
        if(faults[i]==BUS_FAULT_ON_DESELECT) {
            assert(result.slow.bus_faults==1 && !result.fast.attempted);
            assert(result.slow.last_phase==KUI_SCI_ASYNC_PHASE_HANDOFF);
            assert(hw.command_count==1 && hw.dma_starts==1);
        } else {
            assert(result.slow.passed==16 && result.fast.passed==1 && result.fast.attempted==2);
            assert(result.fast.bus_faults==1);
            assert(result.fast.last_phase==(faults[i]==BUS_FAULT_ON_COMMAND?
                KUI_SCI_ASYNC_PHASE_COMMAND:KUI_SCI_ASYNC_PHASE_READY));
            assert(result.fast.bus_fault_scr==0x30 && result.fast.bus_fault_brr==0);
            assert(result.fast.dma_started==1 && result.fast.dma_irqs==1);
            assert(result.fast.command_response==0xff && result.fast.snapshot_ssr==(TDRE|TEND));
        }
        restored(&result);
    }
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
    assert(!result.slow.module_reset_attempts && !hw.module_asserts && !hw.module_resumes);
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
static enum kui_sci_async_status poll_complete(struct kui_sci_async_reader *reader) {
    enum kui_sci_async_status status;
    unsigned calls=0;
    do {status=kui_sci_async_poll(reader);assert(++calls<210000u);}
    while(status==KUI_SCI_ASYNC_PENDING);
    return status;
}
static void begin_active(struct kui_sci_async_reader *reader) {
    assert(kui_sci_async_begin(reader,123,false)==KUI_SCI_ASYNC_OK);
    for(unsigned i=0;i<100 && !hw.dma_starts;++i)
        assert(kui_sci_async_poll(reader)==KUI_SCI_ASYNC_PENDING);
    assert(hw.dma_starts==1 && receiving());
}
static void test_reader_bounded_framing_and_publish(void) {
    struct kui_loader_sd card=reset();hw.ready_delay=29;hw.token_delay=37;
    struct kui_sci_async_reader reader={0};struct kui_sci_async_probe_result result;
    uint8_t payload[512];memset(payload,0x39,sizeof(payload));
    assert(kui_sci_async_open(&reader,&card,&result)==KUI_SCI_ASYNC_OK);
    assert(kui_sci_async_begin(&reader,123,false)==KUI_SCI_ASYNC_OK);
    assert(!hw.bus_bytes && !hw.dma_starts);
    assert(kui_sci_async_finish(&reader,payload,hw.baseline)==KUI_SCI_ASYNC_PENDING);
    unsigned calls=0;enum kui_sci_async_status status;
    do {
        unsigned before=hw.bus_bytes,work=hw.work_ticks;
        status=kui_sci_async_poll(&reader);
        assert(hw.bus_bytes-before<=8 && hw.work_ticks==work);
        assert(!hw.cache_invalidates && !result.fast.passed);
        for(unsigned i=0;i<512;++i) assert(payload[i]==0x39);
        assert(++calls<100);
    } while(status==KUI_SCI_ASYNC_PENDING);
    assert(status==KUI_SCI_ASYNC_OK && calls>10 && hw.total_bytes==514);
    assert(kui_sci_async_finish(&reader,payload,hw.baseline)==KUI_SCI_ASYNC_OK);
    assert(!memcmp(payload,hw.baseline,512) && result.baseline_checked && result.baseline_ok);
    assert(result.fast.passed==1 && hw.cache_invalidates==1);
    assert(kui_sci_async_finish(&reader,payload,hw.baseline)==KUI_SCI_ASYNC_ARGUMENT);
    assert(kui_sci_async_close(&reader)==KUI_SCI_ASYNC_OK);
    assert(!reader.generation && result.max_open_us && result.max_begin_us && result.max_poll_us);
    assert(result.max_finish_us && result.max_close_us && result.max_call_us>=result.max_finish_us);
    restored(&result);
}
static void test_reader_pending_cancel_drains(void) {
    struct kui_loader_sd card=reset();hw.fault=STALLED;hw.force_tail_overrun=true;
    struct kui_sci_async_reader reader={0};struct kui_sci_async_probe_result result;
    uint8_t payload[512];memset(payload,0x5e,sizeof(payload));
    assert(kui_sci_async_open(&reader,&card,&result)==KUI_SCI_ASYNC_OK);
    begin_active(&reader);
    unsigned writes=hw.writes;
    for(unsigned i=0;i<5;++i) {
        assert(kui_sci_async_poll(&reader)==KUI_SCI_ASYNC_PENDING);
        assert(kui_sci_async_finish(&reader,payload,hw.baseline)==KUI_SCI_ASYNC_PENDING);
    }
    assert(kui_sci_async_close(&reader)==KUI_SCI_ASYNC_PENDING);
    assert(kui_sci_async_cancel(&reader)==KUI_SCI_ASYNC_PENDING);
    assert(hw.writes==writes && receiving() && !result.dma_quarantined);
    hw.fault=NO_FAULT;
    assert(poll_complete(&reader)==KUI_SCI_ASYNC_OK);
    assert(kui_sci_async_finish(&reader,payload,hw.baseline)==KUI_SCI_ASYNC_CANCELLED);
    for(unsigned i=0;i<512;++i) assert(payload[i]==0x5e);
    assert(result.fast.module_resets==1 && !result.fast.passed && result.baseline_ok);
    assert(kui_sci_async_close(&reader)==KUI_SCI_ASYNC_CANCELLED);
    assert(result.max_cancel_us && !result.dma_quarantined);restored(&result);
}
static void test_reader_generic_crc_and_reuse(void) {
    struct kui_loader_sd card=reset();hw.force_tail_overrun=true;
    struct kui_sci_async_reader reader={0};struct kui_sci_async_probe_result result;
    uint8_t payload[512];
    assert(kui_sci_async_open(&reader,&card,&result)==KUI_SCI_ASYNC_OK);
    for(unsigned n=0;n<4;++n) {
        for(unsigned i=0;i<512;++i) hw.baseline[i]=(uint8_t)(i*23u+n*47u);
        assert(kui_sci_async_begin(&reader,120+n,false)==KUI_SCI_ASYNC_OK);
        assert(poll_complete(&reader)==KUI_SCI_ASYNC_OK);
        assert(kui_sci_async_finish(&reader,payload,NULL)==KUI_SCI_ASYNC_OK);
        assert(!memcmp(payload,hw.baseline,512) && !result.baseline_checked && !result.baseline_ok);
        assert(result.crc_ok && result.guards_ok && hw.command_argument==120+n);
    }
    assert(result.fast.passed==4 && result.fast.module_resets==4);
    assert(kui_sci_async_close(&reader)==KUI_SCI_ASYNC_OK);restored(&result);
}
static void test_reader_failed_crc_never_publishes(void) {
    struct kui_loader_sd card=reset();hw.fault=BAD_CRC;hw.force_tail_overrun=true;
    struct kui_sci_async_reader reader={0};struct kui_sci_async_probe_result result;
    uint8_t payload[512];memset(payload,0x9b,sizeof(payload));
    assert(kui_sci_async_open(&reader,&card,&result)==KUI_SCI_ASYNC_OK);
    assert(kui_sci_async_begin(&reader,123,false)==KUI_SCI_ASYNC_OK);
    assert(poll_complete(&reader)==KUI_SCI_ASYNC_OK);
    assert(kui_sci_async_finish(&reader,payload,hw.baseline)==KUI_SCI_ASYNC_CRC);
    for(unsigned i=0;i<512;++i) assert(payload[i]==0x9b);
    assert(!result.fast.module_reset_attempts && !result.fast.passed);
    assert(kui_sci_async_close(&reader)==KUI_SCI_ASYNC_CRC);restored(&result);
}
static void test_reader_handles_and_framing_cancel(void) {
    struct kui_loader_sd card=reset();struct kui_sci_async_reader reader={0};
    struct kui_sci_async_probe_result result,other_result;
    assert(kui_sci_async_open(&reader,&card,&result)==KUI_SCI_ASYNC_OK);
    struct kui_sci_async_reader copy=reader,other={0};
    unsigned writes=hw.writes;
    assert(kui_sci_async_begin(&copy,123,false)==KUI_SCI_ASYNC_ARGUMENT);
    assert(kui_sci_async_cancel(&copy)==KUI_SCI_ASYNC_ARGUMENT);
    assert(kui_sci_async_close(&copy)==KUI_SCI_ASYNC_ARGUMENT);
    assert(kui_sci_async_open(&reader,&card,&other_result)==KUI_SCI_ASYNC_BUSY);
    assert(kui_sci_async_open(&other,&card,&result)==KUI_SCI_ASYNC_BUSY);
    assert(hw.writes==writes && reader.generation && result.started);
    assert(kui_sci_async_begin(&reader,123,true)==KUI_SCI_ASYNC_OK);
    assert(kui_sci_async_poll(&reader)==KUI_SCI_ASYNC_PENDING && !hw.dma_starts);
    assert(kui_sci_async_cancel(&reader)==KUI_SCI_ASYNC_CANCELLED);
    assert(kui_sci_async_close(&reader)==KUI_SCI_ASYNC_CANCELLED);restored(&result);
    assert(kui_sci_async_poll(&copy)==KUI_SCI_ASYNC_ARGUMENT);
    assert(kui_sci_async_open(&other,&card,&other_result)==KUI_SCI_ASYNC_OK);
    assert(kui_sci_async_poll(&reader)==KUI_SCI_ASYNC_ARGUMENT);
    assert(kui_sci_async_close(&other)==KUI_SCI_ASYNC_OK);restored(&other_result);
}
static void test_reader_cancel_stalled_deadline(void) {
    struct kui_loader_sd card=reset();hw.fault=STALLED;hw.timer_stopped=true;
    struct kui_sci_async_reader reader={0};struct kui_sci_async_probe_result result;
    assert(kui_sci_async_open(&reader,&card,&result)==KUI_SCI_ASYNC_OK);
    begin_active(&reader);
    assert(kui_sci_async_cancel(&reader)==KUI_SCI_ASYNC_PENDING);
    assert(poll_complete(&reader)==KUI_SCI_ASYNC_TIMEOUT);
    assert(!hw.cache_invalidates);
    assert(kui_sci_async_close(&reader)==KUI_SCI_ASYNC_RESTORE);
    assert(result.dma_quarantined && !result.safe_restored && result.operation_status==KUI_SCI_ASYNC_TIMEOUT);
    assert(kui_sci_async_open(&reader,&card,&result)==KUI_SCI_ASYNC_BUSY);
}
static void test_reader_foreign_between_calls(void) {
    struct kui_loader_sd card=reset();struct kui_sci_async_reader reader={0};
    struct kui_sci_async_probe_result result;
    assert(kui_sci_async_open(&reader,&card,&result)==KUI_SCI_ASYNC_OK);
    assert(kui_sci_async_begin(&reader,123,false)==KUI_SCI_ASYNC_OK);
    assert(kui_sci_async_poll(&reader)==KUI_SCI_ASYNC_PENDING);
    unsigned bytes=hw.bus_bytes;install_foreign_dma();
    assert(kui_sci_async_poll(&reader)==KUI_SCI_ASYNC_DMA_ERROR && hw.bus_bytes==bytes);
    assert(kui_sci_async_work_sample(&reader)==0);
    assert(kui_sci_async_close(&reader)==KUI_SCI_ASYNC_RESTORE);
    assert(result.foreign_dma && !result.safe_restored);
}
static void test_reader_active_observer_rejects_foreign(void) {
    struct kui_loader_sd card=reset();struct kui_sci_async_reader reader={0};
    struct kui_sci_async_probe_result result;
    assert(kui_sci_async_open(&reader,&card,&result)==KUI_SCI_ASYNC_OK);
    begin_active(&reader);
    uint32_t before=kui_sci_async_work_sample(&reader);
    assert(before>0 && before<514);
    unsigned writes=hw.writes;uint64_t time=hw.now;
    hw.inside_irq=true;
    assert(kui_sci_async_work_sample(&reader)==before);
    hw.inside_irq=false;assert(hw.writes==writes && hw.now==time);
    install_foreign_dma();
    assert(kui_sci_async_work_sample(&reader)==0);
    kui_sci_async_work_record(&reader,before,16,123);
    assert(!result.fast.overlap_batches);
    assert(kui_sci_async_poll(&reader)==KUI_SCI_ASYNC_DMA_ERROR);
    assert(kui_sci_async_close(&reader)==KUI_SCI_ASYNC_RESTORE);
}
static void test_reader_ready_close_discards(void) {
    struct kui_loader_sd card=reset();hw.force_tail_overrun=true;
    struct kui_sci_async_reader reader={0};struct kui_sci_async_probe_result result;
    assert(kui_sci_async_open(&reader,&card,&result)==KUI_SCI_ASYNC_OK);
    assert(kui_sci_async_begin(&reader,123,false)==KUI_SCI_ASYNC_OK);
    assert(poll_complete(&reader)==KUI_SCI_ASYNC_OK);
    assert(!hw.cache_invalidates && !result.crc_ok);
    assert(kui_sci_async_close(&reader)==KUI_SCI_ASYNC_OK);
    assert(result.crc_ok && result.guards_ok && !result.baseline_checked && !result.baseline_ok);
    assert(result.fast.module_resets==1);restored(&result);
}
static void test_reader_argument_preserves_session(void) {
    struct kui_loader_sd card=reset();struct kui_sci_async_reader reader={0};
    struct kui_sci_async_probe_result result;uint8_t payload[512];
    assert(kui_sci_async_open(&reader,&card,&result)==KUI_SCI_ASYNC_OK);
    unsigned purges=hw.cache_purges,writes=hw.writes;
    assert(kui_sci_async_begin(&reader,10000,false)==KUI_SCI_ASYNC_ARGUMENT);
    assert(hw.cache_purges==purges && hw.writes==writes && !result.fast.attempted);
    assert(kui_sci_async_begin(&reader,456,false)==KUI_SCI_ASYNC_OK);
    assert(kui_sci_async_begin(&reader,789,true)==KUI_SCI_ASYNC_BUSY);
    assert(result.lba==456 && result.fast.attempted==1 && !result.slow.attempted);
    assert(poll_complete(&reader)==KUI_SCI_ASYNC_OK);
    assert(kui_sci_async_finish(&reader,payload,hw.baseline)==KUI_SCI_ASYNC_OK);
    assert(hw.command_argument==456 && result.baseline_ok);
    assert(kui_sci_async_close(&reader)==KUI_SCI_ASYNC_OK);restored(&result);
}
static void test_reader_framing_fixed_budget(void) {
    struct kui_loader_sd card=reset();hw.timer_stopped=true;hw.token_delay=9000;
    struct kui_sci_async_reader reader={0};struct kui_sci_async_probe_result result;
    assert(kui_sci_async_open(&reader,&card,&result)==KUI_SCI_ASYNC_OK);
    assert(kui_sci_async_begin(&reader,123,false)==KUI_SCI_ASYNC_OK);
    unsigned calls=0;enum kui_sci_async_status status;
    do {
        unsigned bytes=hw.bus_bytes;status=kui_sci_async_poll(&reader);
        assert(hw.bus_bytes-bytes<=8);assert(++calls<1100);
    } while(status==KUI_SCI_ASYNC_PENDING);
    assert(status==KUI_SCI_ASYNC_TIMEOUT && !hw.dma_starts && hw.token_delay==808);
    assert(kui_sci_async_close(&reader)==KUI_SCI_ASYNC_TIMEOUT);restored(&result);
}
static void test_reader_foreign_before_begin(void) {
    struct kui_loader_sd card=reset();struct kui_sci_async_reader reader={0};
    struct kui_sci_async_probe_result result;
    assert(kui_sci_async_open(&reader,&card,&result)==KUI_SCI_ASYNC_OK);
    install_foreign_dma();unsigned purges=hw.cache_purges;
    assert(kui_sci_async_begin(&reader,123,false)==KUI_SCI_ASYNC_DMA_ERROR);
    assert(hw.cache_purges==purges && !result.fast.attempted && !hw.bus_bytes);
    assert(kui_sci_async_close(&reader)==KUI_SCI_ASYNC_RESTORE);
}
static void test_reader_foreign_before_finish(void) {
    struct kui_loader_sd card=reset();struct kui_sci_async_reader reader={0};
    struct kui_sci_async_probe_result result;uint8_t payload[512];memset(payload,0xf4,sizeof(payload));
    assert(kui_sci_async_open(&reader,&card,&result)==KUI_SCI_ASYNC_OK);
    assert(kui_sci_async_begin(&reader,123,false)==KUI_SCI_ASYNC_OK);
    assert(poll_complete(&reader)==KUI_SCI_ASYNC_OK);
    install_foreign_dma();
    assert(kui_sci_async_finish(&reader,payload,hw.baseline)==KUI_SCI_ASYNC_DMA_ERROR);
    for(unsigned i=0;i<512;++i) assert(payload[i]==0xf4);
    assert(!hw.cache_invalidates);
    assert(kui_sci_async_close(&reader)==KUI_SCI_ASYNC_RESTORE);
}
static void test_reader_slow_close_resyncs_bus(void) {
    for(unsigned failure=0;failure<2;++failure) {
        struct kui_loader_sd card=reset();if(failure) hw.fault=BAD_CRC;
        struct kui_sci_async_reader reader={0};struct kui_sci_async_probe_result result;
        uint8_t payload[512];
        assert(kui_sci_async_open(&reader,&card,&result)==KUI_SCI_ASYNC_OK);
        assert(kui_sci_async_begin(&reader,123,true)==KUI_SCI_ASYNC_OK);
        assert(poll_complete(&reader)==KUI_SCI_ASYNC_OK);
        enum kui_sci_async_status expected=failure?KUI_SCI_ASYNC_CRC:KUI_SCI_ASYNC_OK;
        assert(kui_sci_async_finish(&reader,payload,hw.baseline)==expected);
        assert(hw.bus_slow && hw.brr==31);
        unsigned bytes=hw.bus_bytes;
        assert(kui_sci_async_close(&reader)==expected);
        assert(hw.bus_bytes==bytes && !hw.bus_slow && hw.brr==0);restored(&result);
        /* The ordinary callback models cached speed: unchanged fast requests
         * skip BRR writes, so a stale cache would fail its mode assertion. */
        assert(card.bus.transfer(card.bus.ctx,0xff,false)==0xff && hw.brr==0);
    }
}
static void test_reader_speed_resync_failure(void) {
    struct kui_loader_sd card=reset();hw.speed_resync_failure=true;
    struct kui_sci_async_reader reader={0};struct kui_sci_async_probe_result result;
    assert(kui_sci_async_open(&reader,&card,&result)==KUI_SCI_ASYNC_OK);
    assert(kui_sci_async_close(&reader)==KUI_SCI_ASYNC_RESTORE);
    assert(result.registers_restored && result.handlers_restored && !result.safe_restored);
    assert(kui_sci_async_open(&reader,&card,&result)==KUI_SCI_ASYNC_BUSY);
}
static void test_fault_snapshot_first_rejected_request(void) {
    struct kui_loader_sd card=reset();hw.force_tail_overrun=true;
    struct kui_sci_async_reader reader={0};struct kui_sci_async_probe_result result;
    uint8_t payload[512];
    assert(kui_sci_async_open(&reader,&card,&result)==KUI_SCI_ASYNC_OK);
    /* Prior good completion and even a valid trailing ERI are not faults. */
    for(unsigned i=0;i<4;++i) {
        hw.fault=i==3?TRAILING_ERI:NO_FAULT;
        assert(kui_sci_async_begin(&reader,120+i,false)==KUI_SCI_ASYNC_OK);
        assert(poll_complete(&reader)==KUI_SCI_ASYNC_OK);
        assert(kui_sci_async_finish(&reader,payload,hw.baseline)==KUI_SCI_ASYNC_OK);
        assert(!result.fault.valid);
    }
    hw.fault=PERSISTENT_OVERRUN;
    assert(kui_sci_async_begin(&reader,987,false)==KUI_SCI_ASYNC_OK);
    assert(poll_complete(&reader)==KUI_SCI_ASYNC_RECEIVE_ERROR);
    /* Every attempt overran; the last one's evidence describes the failure. */
    assert(result.fast.payload_overruns==1u+KUI_SCI_ASYNC_OVERRUN_RETRIES);
    assert(result.fast.overrun_retries==KUI_SCI_ASYNC_OVERRUN_RETRIES);
    assert(!result.fast.undrained_overruns && hw.command_count==8);
    assert(result.first_overrun.valid && result.first_overrun.lba==987);
    assert(result.first_overrun.tcr==450 && result.first_overrun.dar==DMA_BASE+64);
    const struct kui_sci_async_fault saved=result.fault;
    assert(saved.valid && saved.event==EXC_SCI_ERI && saved.lba==987);
    assert(saved.ssr==(TDRE|TEND|ORER) && saved.scr==0x50);
    assert(saved.chcr==0x4915 && saved.chcr!=result.fast.last_chcr);
    assert(result.fast.last_chcr==0x4910 && saved.tcr==450);
    assert(saved.sar==0x1fe00014 && saved.dar==DMA_BASE+64 && saved.start_address==DMA_BASE);
    assert(saved.dmaor==0x0301 && saved.request_elapsed_us>0);
    assert(saved.context_valid && saved.pc==hw.interrupted_pc && saved.sr==hw.interrupted_sr);
    assert(hw.cache_invalidates==4 && result.fast.passed==4);
    assert(kui_sci_async_finish(&reader,payload,hw.baseline)==KUI_SCI_ASYNC_RECEIVE_ERROR);
    assert(kui_sci_async_cancel(&reader)==KUI_SCI_ASYNC_RECEIVE_ERROR);
    /* Proven idle: local cleanup succeeds; no quarantine or restart. */
    assert(kui_sci_async_close(&reader)==KUI_SCI_ASYNC_RECEIVE_ERROR);
    assert(!result.dma_quarantined && result.safe_restored);
    assert(!memcmp(&saved,&result.fault,sizeof(saved)));
    irq_context_t stale={.pc=0x8cabcdef,.sr=0};
    hw.probe_handlers[1].hdl(EXC_SCI_ERI,&stale,hw.probe_handlers[1].data);
    assert(!memcmp(&saved,&result.fault,sizeof(saved)));
}
static void test_fault_snapshot_never_reuses_successful_eri(void) {
    struct kui_loader_sd card=reset();hw.fault=TRAILING_ERI;
    struct kui_sci_async_reader reader={0};struct kui_sci_async_probe_result result;
    uint8_t payload[512];
    assert(kui_sci_async_open(&reader,&card,&result)==KUI_SCI_ASYNC_OK);
    begin_active(&reader);
    for(unsigned i=0;i<100 && receiving();++i) kui_sci_async_test_work_tick();
    assert(!receiving());
    assert(poll_complete(&reader)==KUI_SCI_ASYNC_OK);
    assert(kui_sci_async_finish(&reader,payload,hw.baseline)==KUI_SCI_ASYNC_OK);
    assert(result.fast.sci_error_irqs==1 && result.fast.passed==1 && !result.fault.valid);
    install_foreign_dma();
    assert(kui_sci_async_begin(&reader,987,false)==KUI_SCI_ASYNC_DMA_ERROR);
    assert(!result.fault.valid);
    assert(kui_sci_async_close(&reader)==KUI_SCI_ASYNC_RESTORE);
    assert(!result.fault.valid && hw.chcr==0x1025);
}
static void test_fault_snapshot_forced_stop_context_unavailable(void) {
    struct kui_loader_sd card=reset();hw.fault=DMA_FAULT;
    struct kui_sci_async_reader reader={0};struct kui_sci_async_probe_result result;
    assert(kui_sci_async_open(&reader,&card,&result)==KUI_SCI_ASYNC_OK);
    assert(kui_sci_async_begin(&reader,321,false)==KUI_SCI_ASYNC_OK);
    assert(poll_complete(&reader)==KUI_SCI_ASYNC_DMA_ERROR);
    assert(result.fault.valid && !result.fault.event && result.fault.lba==321);
    assert(result.fault.scr==0x50 && result.fault.chcr==0x4915 && result.fault.tcr==450);
    assert(result.fault.dmaor==0x0305);
    assert(!result.fault.context_valid && !result.fault.pc && !result.fault.sr);
    assert(!hw.cache_invalidates);
    assert(kui_sci_async_close(&reader)==KUI_SCI_ASYNC_RESTORE && result.dma_quarantined);
    assert(result.fault.dmaor==0x0305 && hw.dmaor==0x0305);
}
static void test_fault_snapshot_foreign_preserves_owner(void) {
    struct kui_loader_sd card=reset();struct kui_sci_async_reader reader={0};
    struct kui_sci_async_probe_result result;
    assert(kui_sci_async_open(&reader,&card,&result)==KUI_SCI_ASYNC_OK);
    begin_active(&reader);install_foreign_dma();
    assert(kui_sci_async_poll(&reader)==KUI_SCI_ASYNC_DMA_ERROR);
    assert(result.fault.valid && !result.fault.event && !result.fault.context_valid);
    assert(result.fault.sar==0x0c080000 && result.fault.dar==0x0c090000);
    assert(result.fault.chcr==0x1025 && result.fault.tcr==64);
    assert(result.fault.start_address==DMA_BASE);
    assert(!hw.cache_invalidates && hw.chcr==0x1025 && hw.tcr==64);
    assert(kui_sci_async_close(&reader)==KUI_SCI_ASYNC_RESTORE && result.foreign_dma);
    assert(hw.handlers[0].hdl==foreign_handler && hw.chcr==0x1025);
}
/* Oracle for a continuous capture that starts at block lba's payload. */
static unsigned capture_layout(uint32_t lba,uint32_t bytes,unsigned gaps[KUI_SCI_ASYNC_STREAM_GAPS],
        unsigned *gap_count) {
    uint32_t pos=0;unsigned blocks=0;*gap_count=0;
    while(bytes-pos>=514u) {
        pos+=514u;++blocks;
        unsigned gap=stream_gap_for(lba+blocks);
        if(pos+gap>=bytes) break;
        if(*gap_count<KUI_SCI_ASYNC_STREAM_GAPS) gaps[*gap_count]=gap;
        ++*gap_count;pos+=gap+1u;
    }
    return blocks;
}
static void read_after_stream(struct kui_sci_async_reader *reader) {
    uint8_t payload[512];
    assert(kui_sci_async_begin(reader,77,false)==KUI_SCI_ASYNC_OK);
    assert(poll_complete(reader)==KUI_SCI_ASYNC_OK);
    assert(kui_sci_async_finish(reader,payload,hw.baseline)==KUI_SCI_ASYNC_OK);
    assert(!memcmp(payload,hw.baseline,512) && hw.command_argument==77);
}
static void test_stream_capture(void) {
    struct kui_loader_sd card=reset();
    hw.token_delay=5;hw.stream_gap=6;hw.stream_gap_every=4;hw.stream_gap_extra=40;hw.stop_busy=3;
    static uint8_t buffer[16384] __attribute__((aligned(32)));
    struct kui_sci_async_reader reader={0};struct kui_sci_async_probe_result result;
    struct kui_sci_async_stream stream;
    assert(kui_sci_async_open(&reader,&card,&result)==KUI_SCI_ASYNC_OK);
    assert(kui_sci_async_stream_capture(&reader,300,buffer,sizeof(buffer),&stream)==KUI_SCI_ASYNC_OK);
    unsigned gaps[KUI_SCI_ASYNC_STREAM_GAPS],gap_count=0;
    unsigned blocks=capture_layout(300,sizeof(buffer),gaps,&gap_count);
    assert(blocks==30 && gap_count==30);
    assert(stream.status==KUI_SCI_ASYNC_OK && stream.complete && stream.received==sizeof(buffer));
    assert(stream.command_response==0 && stream.first_token_bytes==5 && stream.last_token==0xfe);
    assert(stream.blocks==blocks && stream.gaps==gap_count && !stream.crc_errors && !stream.token_errors);
    unsigned total=0,low=~0u,high=0;
    for(unsigned i=0;i<gap_count;++i) {
        assert(stream.gap[i]==gaps[i]);total+=gaps[i];
        if(gaps[i]<low) low=gaps[i];
        if(gaps[i]>high) high=gaps[i];
    }
    assert(stream.gap_min==low && stream.gap_max==high && stream.gap_total==total);
    assert(low==6 && high==46);
    for(unsigned k=0;k<blocks;++k) {
        uint8_t expected[514];stream_block_bytes(300+k,expected);
        assert(!memcmp(buffer+512u*k,expected,512));
    }
    /* Stopped after the trailing overrun, SCI reset with the card deselected,
     * then CMD12 into the stream: stuff byte skipped, R1, busy, deselect. */
    assert(stream.end_ssr&ORER && !stream.end_count && stream.reset_state==KUI_SCI_ASYNC_MODULE_RESET_OK);
    assert(stream.stop_response==0 && stream.stop_busy_bytes==3 && hw.stop_count==1);
    assert(hw.multi_count==1 && hw.command_argument==300 && hw.stream==STREAM_OFF);
    assert(hw.module_asserts==1 && hw.stream_tail_lost==2 && !hw.selected);
    assert(stream.masked_us && stream.capture_us && stream.elapsed_us>=stream.capture_us);
    assert(result.cmd18.attempted==1 && result.cmd18.passed==1 && result.cmd18.dma_started==1);
    assert(result.cmd18.trailing_overruns==1 && result.cmd18.module_resets==1);
    assert(!result.fast.attempted && !result.fast.dma_started && !result.fast.module_resets);
    /* The masked capture is reported separately from the reader's windows. */
    assert(result.max_irq_masked_us<stream.masked_us);
    read_after_stream(&reader);
    assert(kui_sci_async_close(&reader)==KUI_SCI_ASYNC_OK);restored(&result);
}
static void test_stream_capture_overrun(void) {
    struct kui_loader_sd card=reset();hw.fault=STREAM_OVERRUN;
    hw.token_delay=2;hw.stream_gap=6;
    static uint8_t buffer[4096] __attribute__((aligned(32)));
    struct kui_sci_async_reader reader={0};struct kui_sci_async_probe_result result;
    struct kui_sci_async_stream stream;
    assert(kui_sci_async_open(&reader,&card,&result)==KUI_SCI_ASYNC_OK);
    assert(kui_sci_async_stream_capture(&reader,40,buffer,sizeof(buffer),&stream)==KUI_SCI_ASYNC_OK);
    /* An early stop is a finding: what arrived is still parsed. */
    assert(stream.status==KUI_SCI_ASYNC_RECEIVE_ERROR && !stream.complete);
    assert(stream.received==1000 && stream.end_count==sizeof(buffer)-1000u && stream.end_ssr&ORER);
    assert(stream.blocks==1 && stream.gaps==1 && stream.gap[0]==stream_gap_for(41) && !stream.crc_errors);
    assert(stream.stop_response==0 && hw.stop_count==1 && hw.module_asserts==1);
    assert(result.cmd18.premature_errors==1 && !result.cmd18.passed && !result.dma_quarantined);
    read_after_stream(&reader);
    assert(kui_sci_async_close(&reader)==KUI_SCI_ASYNC_OK);restored(&result);
}
static void test_stream_resume(void) {
    struct kui_loader_sd card=reset();
    hw.token_delay=9;hw.stream_gap=6;hw.stream_gap_every=4;hw.stream_gap_extra=40;hw.stop_busy=2;
    static uint8_t dst[12*512];
    struct kui_sci_async_reader reader={0};struct kui_sci_async_probe_result result;
    struct kui_sci_async_resume resume;
    assert(kui_sci_async_open(&reader,&card,&result)==KUI_SCI_ASYNC_OK);
    assert(kui_sci_async_stream_resume(&reader,500,12,dst,&resume)==KUI_SCI_ASYNC_OK);
    assert(resume.status==KUI_SCI_ASYNC_OK && resume.blocks==12 && resume.requested==12);
    assert(!resume.crc_errors && !resume.token_errors && !resume.guard_errors);
    assert(resume.command_response==0 && resume.first_token_bytes==9 && resume.last_token==0xfe);
    /* Each 513-byte stop leaves the second CRC byte in RDR and loses one
     * fill byte; the search finds the rest of the gap, then 0xfe. */
    unsigned expected=0,highest=0;
    for(unsigned k=1;k<12;++k) {
        unsigned seen=stream_gap_for(500+k)-1u;expected+=seen;
        if(seen>highest) highest=seen;
    }
    assert(resume.token_bytes==expected && resume.max_token_bytes==highest && highest==45);
    assert(!resume.missing_tail_bytes);
    for(unsigned k=0;k<12;++k) {
        uint8_t block[514];stream_block_bytes(500+k,block);
        assert(!memcmp(dst+512u*k,block,512));
    }
    assert(resume.receive_us && resume.reset_us && resume.check_us && resume.max_masked_us);
    assert(resume.reset_state==KUI_SCI_ASYNC_MODULE_RESET_OK && resume.stop_response==0);
    assert(resume.stop_busy_bytes==2 && hw.stop_count==1 && hw.multi_count==1);
    assert(hw.module_asserts==12 && hw.stream_tail_lost==24 && hw.dma_starts==12);
    assert(result.cmd18.passed==1 && result.cmd18.dma_started==12 && result.cmd18.module_resets==12);
    read_after_stream(&reader);
    assert(kui_sci_async_close(&reader)==KUI_SCI_ASYNC_OK);restored(&result);
}
static void resume_finding(unsigned gap,bool dropped,enum kui_sci_async_status expected,unsigned blocks) {
    struct kui_loader_sd card=reset();
    hw.token_delay=3;hw.stream_gap=gap;hw.stream_drop_on_deselect=dropped;
    static uint8_t dst[4*512];
    struct kui_sci_async_reader reader={0};struct kui_sci_async_probe_result result;
    struct kui_sci_async_resume resume;
    assert(kui_sci_async_open(&reader,&card,&result)==KUI_SCI_ASYNC_OK);
    assert(kui_sci_async_stream_resume(&reader,600,4,dst,&resume)==KUI_SCI_ASYNC_OK);
    assert(resume.status==expected && resume.blocks==blocks);
    assert(resume.token_errors==(expected==KUI_SCI_ASYNC_TOKEN?1u:0u));
    /* CMD12 still stops the card and the reader stays usable. */
    assert(resume.stop_response==0 && hw.stop_count==1 && hw.stream==STREAM_OFF);
    assert(result.cmd18.passed==(expected==KUI_SCI_ASYNC_OK?1u:0u) && result.status==KUI_SCI_ASYNC_OK);
    read_after_stream(&reader);
    assert(kui_sci_async_close(&reader)==KUI_SCI_ASYNC_OK);restored(&result);
}
static void test_stream_resume_findings(void) {
    /* One fill byte is enough (the console card's gap); with none, the stop
     * loses the token itself. */
    resume_finding(1,false,KUI_SCI_ASYNC_OK,4);
    resume_finding(0,false,KUI_SCI_ASYNC_TOKEN,1);
    resume_finding(8,true,KUI_SCI_ASYNC_TIMEOUT,1);
}
static void test_stream_stop_rejected(void) {
    struct kui_loader_sd card=reset();hw.token_delay=1;hw.stream_gap=4;hw.stop_r1=0x04;
    static uint8_t buffer[2048] __attribute__((aligned(32)));
    struct kui_sci_async_reader reader={0};struct kui_sci_async_probe_result result;
    struct kui_sci_async_stream stream;
    assert(kui_sci_async_open(&reader,&card,&result)==KUI_SCI_ASYNC_OK);
    assert(kui_sci_async_stream_capture(&reader,10,buffer,sizeof(buffer),&stream)==KUI_SCI_ASYNC_COMMAND);
    assert(stream.status==KUI_SCI_ASYNC_COMMAND && stream.stop_response==0x04 && stream.blocks==3);
    assert(result.operation_status==KUI_SCI_ASYNC_COMMAND && !hw.selected);
    assert(kui_sci_async_begin(&reader,77,false)==KUI_SCI_ASYNC_BUSY);
    assert(kui_sci_async_close(&reader)==KUI_SCI_ASYNC_COMMAND);
    assert(!result.dma_quarantined && result.safe_restored);
}
/* Problems before the card accepted CMD18, or a stop without a response,
 * fail the reader; they are never reported as findings about the data. */
static void stream_failure(enum fault fault,unsigned ready_delay,uint8_t stop_r1,
        enum kui_sci_async_status expected,unsigned stops) {
    struct kui_loader_sd card=reset();hw.fault=fault;hw.ready_delay=ready_delay;
    hw.token_delay=1;hw.stream_gap=4;hw.stop_r1=stop_r1;
    static uint8_t dst[2*512];
    struct kui_sci_async_reader reader={0};struct kui_sci_async_probe_result result;
    struct kui_sci_async_resume resume;
    assert(kui_sci_async_open(&reader,&card,&result)==KUI_SCI_ASYNC_OK);
    assert(kui_sci_async_stream_resume(&reader,10,2,dst,&resume)==expected);
    assert(resume.status==expected && result.operation_status==expected);
    assert(hw.stop_count==stops && !result.cmd18.passed);
    /* Any card still selected is released by close, as for a request. */
    assert(kui_sci_async_close(&reader)==expected && result.safe_restored && !hw.selected);
}
static void test_stream_ready_timeout(void) {stream_failure(NO_FAULT,70000,0,KUI_SCI_ASYNC_TIMEOUT,0);}
/* An issued CMD18 is always followed by CMD12, even after a rejection. */
static void test_stream_command_rejected(void) {stream_failure(BAD_RESPONSE,0,0,KUI_SCI_ASYNC_COMMAND,1);}
static void test_stream_stop_unanswered(void) {stream_failure(NO_FAULT,0,0xff,KUI_SCI_ASYNC_TIMEOUT,1);}
static void test_stream_arguments(void) {
    struct kui_loader_sd card=reset();
    static uint8_t buffer[32800] __attribute__((aligned(32)));
    struct kui_sci_async_reader reader={0};struct kui_sci_async_probe_result result;
    struct kui_sci_async_stream stream;struct kui_sci_async_resume resume;
    assert(kui_sci_async_open(&reader,&card,&result)==KUI_SCI_ASYNC_OK);
    unsigned bytes=hw.bus_bytes,writes=hw.writes;
    assert(kui_sci_async_stream_capture(&reader,0,buffer,1024,&stream)==KUI_SCI_ASYNC_ARGUMENT);
    assert(kui_sci_async_stream_capture(&reader,0,buffer,2050,&stream)==KUI_SCI_ASYNC_ARGUMENT);
    assert(kui_sci_async_stream_capture(&reader,0,buffer,32800,&stream)==KUI_SCI_ASYNC_ARGUMENT);
    assert(kui_sci_async_stream_capture(&reader,0,buffer+1,2048,&stream)==KUI_SCI_ASYNC_ARGUMENT);
    assert(kui_sci_async_stream_capture(&reader,9997,buffer,2048,&stream)==KUI_SCI_ASYNC_ARGUMENT);
    assert(kui_sci_async_stream_capture(&reader,0,NULL,2048,&stream)==KUI_SCI_ASYNC_ARGUMENT);
    assert(stream.status==KUI_SCI_ASYNC_ARGUMENT && stream.lba==0 && stream.bytes==2048);
    assert(kui_sci_async_stream_resume(&reader,0,0,buffer,&resume)==KUI_SCI_ASYNC_ARGUMENT);
    assert(kui_sci_async_stream_resume(&reader,0,KUI_SCI_ASYNC_RESUME_BLOCKS+1u,buffer,&resume)==KUI_SCI_ASYNC_ARGUMENT);
    assert(kui_sci_async_stream_resume(&reader,9999,2,buffer,&resume)==KUI_SCI_ASYNC_ARGUMENT);
    assert(kui_sci_async_stream_resume(&reader,0,2,NULL,&resume)==KUI_SCI_ASYNC_ARGUMENT);
    struct kui_sci_async_reader stale={reader.generation+1u};
    assert(kui_sci_async_stream_capture(&stale,0,buffer,2048,&stream)==KUI_SCI_ASYNC_ARGUMENT);
    assert(hw.bus_bytes==bytes && hw.writes==writes && !hw.multi_count);
    /* Only between requests. */
    begin_active(&reader);
    assert(kui_sci_async_stream_capture(&reader,0,buffer,2048,&stream)==KUI_SCI_ASYNC_BUSY);
    assert(kui_sci_async_stream_resume(&reader,0,2,buffer,&resume)==KUI_SCI_ASYNC_BUSY);
    assert(stream.status==KUI_SCI_ASYNC_BUSY && resume.status==KUI_SCI_ASYNC_BUSY && !hw.multi_count);
    uint8_t payload[512];
    assert(poll_complete(&reader)==KUI_SCI_ASYNC_OK);
    assert(kui_sci_async_finish(&reader,payload,hw.baseline)==KUI_SCI_ASYNC_OK);
    assert(result.cmd18.attempted==0);
    assert(kui_sci_async_close(&reader)==KUI_SCI_ASYNC_OK);restored(&result);
}
/* One stream run through the public reader API. */
static enum kui_sci_async_status stream_read(struct kui_sci_async_reader *reader,
        uint32_t lba,uint32_t count,uint8_t *dst) {
    enum kui_sci_async_status status=kui_sci_async_begin_stream(reader,lba,count,dst);
    if(status==KUI_SCI_ASYNC_OK) status=poll_complete(reader);
    return status;
}
static void stream_blocks_match(const uint8_t *dst,uint32_t lba,uint32_t count) {
    for(uint32_t k=0;k<count;++k) {
        uint8_t block[514];stream_block_bytes(lba+k,block);
        assert(!memcmp(dst+512u*k,block,512));
    }
}
static void test_stream_reader(void) {
    struct kui_loader_sd card=reset();
    hw.token_delay=7;hw.stream_gap=1;hw.stream_gap_every=4;hw.stream_gap_extra=30;hw.stop_busy=2;
    static uint8_t dst[10*512];
    struct kui_sci_async_reader reader={0};struct kui_sci_async_probe_result result;
    assert(kui_sci_async_open(&reader,&card,&result)==KUI_SCI_ASYNC_OK);
    assert(stream_read(&reader,700,10,dst)==KUI_SCI_ASYNC_OK);
    stream_blocks_match(dst,700,10);
    const struct kui_sci_async_stage *s=&result.streaming;
    assert(s->attempted==10 && s->passed==10 && s->dma_started==10 && s->dma_irqs==10);
    assert(s->module_resets==10 && s->trailing_overruns==10 && !s->stream_restarts);
    assert(!s->missing_tail_bytes && !s->payload_overruns && s->last_phase==KUI_SCI_ASYNC_PHASE_COMPLETE);
    /* One CMD18 and one CMD12; the first token after the command, then the
     * rest of each gap (one fill byte is lost at every stop). */
    assert(hw.multi_count==1 && hw.stop_count==1 && !hw.command_count && hw.stream==STREAM_OFF);
    unsigned waited=7;
    for(unsigned k=1;k<10;++k) waited+=stream_gap_for(700+k)-1u;
    assert(s->token_bytes==waited && s->max_token_bytes==30 && s->token_us);
    assert(s->framing_us && s->finish_us && s->receive_us);
    assert(hw.dma_starts==10 && hw.total_bytes==10u*513u && hw.stream_tail_lost==20 && !hw.selected);
    assert(!result.fast.attempted && !result.slow.attempted && result.lba==709);
    /* The run is over: the reader is idle and takes ordinary requests. */
    assert(kui_sci_async_poll(&reader)==KUI_SCI_ASYNC_ARGUMENT);
    read_after_stream(&reader);
    assert(kui_sci_async_close(&reader)==KUI_SCI_ASYNC_OK);restored(&result);
}
static void test_stream_reader_overlap(void) {
    /* While block k+1 is received, block k has already been checked: after
     * the poll that starts a DMA, the previous block is in dst. */
    struct kui_loader_sd card=reset();hw.token_delay=2;hw.stream_gap=1;
    static uint8_t dst[4*512];memset(dst,0x5a,sizeof(dst));
    struct kui_sci_async_reader reader={0};struct kui_sci_async_probe_result result;
    assert(kui_sci_async_open(&reader,&card,&result)==KUI_SCI_ASYNC_OK);
    kui_sci_async_set_framing_quantum(&reader,4096);
    assert(kui_sci_async_begin_stream(&reader,30,4,dst)==KUI_SCI_ASYNC_OK);
    unsigned seen=0;enum kui_sci_async_status status;
    do {
        unsigned starts=hw.dma_starts;
        status=kui_sci_async_poll(&reader);
        if(status==KUI_SCI_ASYNC_PENDING && hw.dma_starts>starts && starts) {
            /* This poll started block `starts`; block starts-1 is checked. */
            assert(result.streaming.passed==starts);
            uint8_t block[514];stream_block_bytes(30+starts-1u,block);
            assert(!memcmp(dst+512u*(starts-1u),block,512));
            ++seen;
        }
    } while(status==KUI_SCI_ASYNC_PENDING);
    assert(status==KUI_SCI_ASYNC_OK && seen==3);
    stream_blocks_match(dst,30,4);
    assert(kui_sci_async_close(&reader)==KUI_SCI_ASYNC_OK);restored(&result);
}
static void test_stream_reader_lost_token(void) {
    /* A card without a fill byte: every stop loses the next token, so each
     * later block is fetched with CMD12 and a fresh CMD18. */
    struct kui_loader_sd card=reset();hw.token_delay=3;hw.stream_gap=0;
    static uint8_t dst[6*512];
    struct kui_sci_async_reader reader={0};struct kui_sci_async_probe_result result;
    assert(kui_sci_async_open(&reader,&card,&result)==KUI_SCI_ASYNC_OK);
    assert(stream_read(&reader,900,6,dst)==KUI_SCI_ASYNC_OK);
    stream_blocks_match(dst,900,6);
    assert(result.streaming.passed==6 && result.streaming.stream_restarts==5);
    assert(hw.multi_count==6 && hw.stop_count==6 && hw.stream==STREAM_OFF);
    assert(kui_sci_async_close(&reader)==KUI_SCI_ASYNC_OK);restored(&result);
}
static void test_stream_reader_overrun(void) {
    /* A mid-block overrun: proven idle, SCI reset, CMD12 and CMD18 again at
     * the same block; the stream then continues normally. */
    struct kui_loader_sd card=reset();hw.fault=EARLY_ERROR;hw.token_delay=2;hw.stream_gap=1;
    static uint8_t dst[5*512];
    struct kui_sci_async_reader reader={0};struct kui_sci_async_probe_result result;
    assert(kui_sci_async_open(&reader,&card,&result)==KUI_SCI_ASYNC_OK);
    assert(stream_read(&reader,40,5,dst)==KUI_SCI_ASYNC_OK);
    stream_blocks_match(dst,40,5);
    const struct kui_sci_async_stage *s=&result.streaming;
    assert(s->payload_overruns==1 && s->overrun_retries==1 && s->stream_restarts==1);
    assert(s->passed==5 && s->dma_started==6 && s->module_resets==6 && !s->undrained_overruns);
    /* The model moves streamed bytes 256 at a time: the overrun hits at 256. */
    assert(result.first_overrun.valid && result.first_overrun.lba==40 && result.first_overrun.tcr==257);
    assert(hw.multi_count==2 && hw.stop_count==2 && !result.dma_quarantined);
    assert(kui_sci_async_close(&reader)==KUI_SCI_ASYNC_OK);restored(&result);
}
static void stream_bad_block(uint32_t bad,unsigned dma_starts,unsigned stops) {
    /* A block that fails its CRC check is read again: the card is stopped
     * (once the in-flight block, which is dropped, has arrived) and CMD18 is
     * re-issued at the bad block. */
    struct kui_loader_sd card=reset();hw.fault=STREAM_BAD_CRC;hw.stream_bad_lba=bad;
    hw.token_delay=2;hw.stream_gap=1;
    static uint8_t dst[6*512];
    struct kui_sci_async_reader reader={0};struct kui_sci_async_probe_result result;
    assert(kui_sci_async_open(&reader,&card,&result)==KUI_SCI_ASYNC_OK);
    assert(stream_read(&reader,50,6,dst)==KUI_SCI_ASYNC_OK);
    stream_blocks_match(dst,50,6);
    const struct kui_sci_async_stage *s=&result.streaming;
    assert(hw.fault_fired && s->passed==6 && s->stream_restarts==1 && s->dma_started==dma_starts);
    assert(hw.multi_count==2 && hw.stop_count==stops && hw.stream==STREAM_OFF);
    assert(kui_sci_async_close(&reader)==KUI_SCI_ASYNC_OK);restored(&result);
}
static void test_stream_reader_bad_block(void) {
    stream_bad_block(53,8,2); /* block 54 in flight is dropped; 53..55 read again */
    stream_bad_block(55,7,2); /* last block: the card was already stopped */
}
static void stream_cancel(bool during_dma) {
    /* A long gap keeps the token search going past one poll's 8 bytes. */
    struct kui_loader_sd card=reset();hw.token_delay=2;hw.stream_gap=during_dma?1u:20u;
    hw.stream_budget=16; /* a block spans many model steps */
    static uint8_t dst[8*512];
    struct kui_sci_async_reader reader={0};struct kui_sci_async_probe_result result;
    assert(kui_sci_async_open(&reader,&card,&result)==KUI_SCI_ASYNC_OK);
    assert(kui_sci_async_begin_stream(&reader,60,8,dst)==KUI_SCI_ASYNC_OK);
    unsigned calls=0;
    while(result.streaming.passed<2) {assert(kui_sci_async_poll(&reader)==KUI_SCI_ASYNC_PENDING);assert(++calls<100000u);}
    /* With the long gap the next block is still being framed; otherwise its
     * DMA was started by the same poll (the model may already have finished it). */
    if(!during_dma) assert(!receiving() && !(hw.chcr&DE));
    else assert(hw.chcr&DE);
    /* A running stream must be cancelled (or finished) before close. */
    assert(kui_sci_async_close(&reader)==KUI_SCI_ASYNC_PENDING);
    if(during_dma) {
        assert(kui_sci_async_cancel(&reader)==KUI_SCI_ASYNC_PENDING);
        assert(poll_complete(&reader)==KUI_SCI_ASYNC_CANCELLED);
    } else assert(kui_sci_async_cancel(&reader)==KUI_SCI_ASYNC_CANCELLED);
    /* CMD12 still stops the card before the session ends. */
    assert(hw.stop_count==1 && hw.stream==STREAM_OFF && result.streaming.passed==2);
    stream_blocks_match(dst,60,2);
    assert(kui_sci_async_close(&reader)==KUI_SCI_ASYNC_CANCELLED && result.safe_restored);
    assert(!hw.selected && !result.dma_quarantined);
}
static void test_stream_reader_cancel(void) {stream_cancel(false);stream_cancel(true);}
static void test_stream_reader_missing_tail(void) {
    /* Reception stops exactly at the count: nothing is left in RDR and the
     * card is not clocked past the second CRC byte, so the next token is
     * missing too. The token and the failed check both restart the run, and
     * with no block checked in between the budget of three ends it. */
    struct kui_loader_sd card=reset();hw.fault=STREAM_NO_TAIL;hw.token_delay=1;hw.stream_gap=1;
    static uint8_t dst[4*512];memset(dst,0x5a,sizeof(dst));
    struct kui_sci_async_reader reader={0};struct kui_sci_async_probe_result result;
    assert(kui_sci_async_open(&reader,&card,&result)==KUI_SCI_ASYNC_OK);
    assert(stream_read(&reader,80,4,dst)==KUI_SCI_ASYNC_RECEIVE_ERROR);
    assert(result.streaming.missing_tail_bytes==2 && result.streaming.stream_restarts==3);
    assert(!result.streaming.passed && hw.multi_count==2 && hw.stop_count==2);
    assert(dst[0]==0x5a && dst[511]==0x5a);
    assert(kui_sci_async_close(&reader)==KUI_SCI_ASYNC_RECEIVE_ERROR && result.safe_restored);
    assert(!hw.selected && !result.dma_quarantined && hw.stream==STREAM_OFF);
}
static void test_stream_reader_arguments(void) {
    struct kui_loader_sd card=reset();
    static uint8_t dst[3*512];
    struct kui_sci_async_reader reader={0};struct kui_sci_async_probe_result result;
    assert(kui_sci_async_open(&reader,&card,&result)==KUI_SCI_ASYNC_OK);
    unsigned writes=hw.writes;
    assert(kui_sci_async_begin_stream(&reader,0,0,dst)==KUI_SCI_ASYNC_ARGUMENT);
    assert(kui_sci_async_begin_stream(&reader,0,2,NULL)==KUI_SCI_ASYNC_ARGUMENT);
    assert(kui_sci_async_begin_stream(&reader,9990,11,dst)==KUI_SCI_ASYNC_ARGUMENT);
    struct kui_sci_async_reader stale={reader.generation+1u};
    assert(kui_sci_async_begin_stream(&stale,0,2,dst)==KUI_SCI_ASYNC_ARGUMENT);
    assert(hw.writes==writes && !hw.bus_bytes && !result.streaming.attempted);
    begin_active(&reader);
    assert(kui_sci_async_begin_stream(&reader,0,2,dst)==KUI_SCI_ASYNC_BUSY);
    uint8_t payload[512];
    assert(poll_complete(&reader)==KUI_SCI_ASYNC_OK);
    assert(kui_sci_async_finish(&reader,payload,hw.baseline)==KUI_SCI_ASYNC_OK);
    /* A stream right up to the card's last block is accepted. */
    hw.token_delay=1;hw.stream_gap=1;
    assert(stream_read(&reader,9997,3,dst)==KUI_SCI_ASYNC_OK);
    stream_blocks_match(dst,9997,3);
    /* finish is not part of a stream. */
    assert(kui_sci_async_finish(&reader,payload,NULL)==KUI_SCI_ASYNC_ARGUMENT);
    assert(kui_sci_async_close(&reader)==KUI_SCI_ASYNC_OK);restored(&result);
}
/* A 2.5 ms stall injected inside one SCI module reset: logged as a masked
 * pause at the reset, with the step that took the time. */
static void test_pause_in_module_reset(void) {
    struct kui_loader_sd card=reset();hw.force_tail_overrun=true;
    struct kui_sci_async_reader reader={0};struct kui_sci_async_probe_result result;
    uint8_t payload[512];
    assert(kui_sci_async_open(&reader,&card,&result)==KUI_SCI_ASYNC_OK);
    for(unsigned i=0;i<3;++i) {
        assert(kui_sci_async_begin(&reader,123,false)==KUI_SCI_ASYNC_OK);
        assert(poll_complete(&reader)==KUI_SCI_ASYNC_OK);
        assert(kui_sci_async_finish(&reader,payload,hw.baseline)==KUI_SCI_ASYNC_OK);
    }
    assert(result.fast.module_resets==3 && !result.pause_count);
    const struct kui_sci_async_reset_time *worst=&result.reset_worst;
    assert(worst->stage==1 && worst->ns && worst->ns<1000000u);
    hw.stall_at=hw.now; /* the next uptime-counter read stalls */
    assert(kui_sci_async_begin(&reader,123,false)==KUI_SCI_ASYNC_OK);
    assert(poll_complete(&reader)==KUI_SCI_ASYNC_OK);
    assert(kui_sci_async_finish(&reader,payload,hw.baseline)==KUI_SCI_ASYNC_OK);
    assert(hw.stall_done && result.pause_count>=1);
    const struct kui_sci_async_pause *pause=&result.pauses[0];
    assert(pause->site==3 && pause->stage==1 && pause->us>=2500 && pause->at_us);
    assert(result.max_irq_masked_site==3 && result.max_irq_masked_stage==1);
    assert(result.max_irq_masked_us>=2500 && worst->ns>=2500000u && worst->ns<2600000u);
    /* The stall shows in the first step (between the first two reads). */
    assert(worst->steps_ns[0]>=2500000u);
    for(unsigned i=1;i<6;++i) assert(worst->steps_ns[i]<1000000u);
    assert(kui_sci_async_close(&reader)==KUI_SCI_ASYNC_OK);
}
/* Reads across a whole second, where KOS's clock jumps 2.5 ms: the probe's
 * own clock does not, so nothing is logged as a pause. */
static void test_second_boundary(void) {
    struct kui_loader_sd card=reset();hw.force_tail_overrun=true;
    hw.now=999900u;
    uint64_t before=timer_us_gettime64();
    hw.now=999963u;
    assert(timer_us_gettime64()-before>2500u); /* 63 us later on the model */
    hw.now=998000u;
    struct kui_sci_async_reader reader={0};struct kui_sci_async_probe_result result;
    uint8_t payload[512];
    assert(kui_sci_async_open(&reader,&card,&result)==KUI_SCI_ASYNC_OK);
    for(unsigned i=0;i<100 && hw.now<1003000u;++i) {
        assert(kui_sci_async_begin(&reader,123,false)==KUI_SCI_ASYNC_OK);
        assert(poll_complete(&reader)==KUI_SCI_ASYNC_OK);
        assert(kui_sci_async_finish(&reader,payload,hw.baseline)==KUI_SCI_ASYNC_OK);
    }
    assert(hw.now>=1003000u && result.fast.passed>=2);
    assert(!result.pause_count && result.max_irq_masked_us<500 && result.max_call_us<1500);
    assert(result.fast.max_receive_us<1500 && result.reset_worst.ns<1000000u);
    assert(kui_sci_async_close(&reader)==KUI_SCI_ASYNC_OK);restored(&result);
}
static void isolated(void (*test)(void)) {
    /* The production API deliberately has no reset for a poisoned session. */
    pid_t child=fork(); assert(child>=0);
    if(!child) { test(); _exit(0); }
    int status=0; assert(waitpid(child,&status,0)==child);
    assert(WIFEXITED(status) && WEXITSTATUS(status)==0);
}
int main(void) {
    isolated(test_fault_snapshot_first_rejected_request);
    isolated(test_fault_snapshot_never_reuses_successful_eri);
    isolated(test_fault_snapshot_forced_stop_context_unavailable);
    isolated(test_fault_snapshot_foreign_preserves_owner);
    test_reader_slow_close_resyncs_bus();
    isolated(test_reader_speed_resync_failure);
    test_reader_ready_close_discards();
    test_reader_argument_preserves_session();
    test_reader_framing_fixed_budget();
    isolated(test_reader_foreign_before_begin);
    isolated(test_reader_foreign_before_finish);
    test_reader_bounded_framing_and_publish();
    test_reader_pending_cancel_drains();
    test_reader_generic_crc_and_reuse();
    test_reader_failed_crc_never_publishes();
    test_reader_handles_and_framing_cancel();
    isolated(test_reader_cancel_stalled_deadline);
    isolated(test_reader_foreign_between_calls);
    isolated(test_reader_active_observer_rejects_foreign);
    test_success(false);
    test_success(true);
    test_standard_capacity_address();
    test_sptr_input_monitors();
    test_sptr_reads_pins_with_transmitter_on_or_off();
    test_preexisting_gpio_output_no_touch();
    test_no_foreground_overlap();
    isolated(test_late_dma_quarantined);
    isolated(test_payload_overrun_once);
    isolated(test_payload_overrun_late_byte);
    isolated(test_payload_overrun_cancel);
    test_token_bytes_and_quantum();
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
    test_fast_tail_module_reset();
    test_module_assert_failure();
    isolated(test_module_resume_failure);
    test_module_reset_signature();
    test_module_reset_validation_gates();
    isolated(test_module_reset_rechecks_dma_owner);
    test_handoff_rechecks_receive_flags();
    isolated(test_handoff_stuck_receive_flags);
    test_handoff_requires_idle();
    test_bus_fault_has_distinct_status();
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
    test_stream_capture();
    test_stream_capture_overrun();
    test_stream_resume();
    test_stream_resume_findings();
    isolated(test_stream_stop_rejected);
    isolated(test_stream_ready_timeout);
    isolated(test_stream_command_rejected);
    isolated(test_stream_stop_unanswered);
    test_stream_arguments();
    test_stream_reader();
    test_stream_reader_overlap();
    test_stream_reader_lost_token();
    isolated(test_stream_reader_overrun);
    test_stream_reader_bad_block();
    test_stream_reader_cancel();
    isolated(test_stream_reader_missing_tail);
    test_stream_reader_arguments();
    test_pause_in_module_reset();
    test_second_boundary();
    puts("SCI pause tracing: masked pause, module reset steps and the whole-second jump passed");
    puts("SCI CMD18 streaming reader: overlapped checks, RDR byte, lost token, overrun, bad block, cancel passed");
    puts("SCI CMD18 capture and per-block resume: gaps, findings, CMD12 stop and reuse passed");
    puts("SCI module reset: bounded gates, restoration and modeled RX recovery passed; console proof still required");
    puts("SCI reader lifecycle: bounded polling, cancel/drain, no early publish and ownership passed");
    puts("SCI first fault: pre-stop control/context preserved; good completions excluded");
    puts("SCI payload overrun: proven-idle retry, late byte, cancel, retry limit and undrained quarantine passed");
    puts("SCI async probe host tests passed");
    return 0;
}
