/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_volatile_chain.h"

#define RAM_BEGIN UINT32_C(0x0c000000)
#define RAM_END UINT32_C(0x0d000000)
#define CONTROL_MASK UINT32_C(0x800300ff)
#define CONTROLLER UINT32_C(0x01000000)
#define MEMCARD UINT32_C(0x02000000)
#define LCD UINT32_C(0x04000000)
#define CLOCK UINT32_C(0x08000000)

static uint32_t volatile_chain_le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
        (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void volatile_chain_put32(uint8_t *p, uint32_t value) {
    for(unsigned i=0; i<4u; ++i) p[i]=(uint8_t)(value >> (i*8u));
}

static uint32_t volatile_chain_crc32(const uint8_t *p, size_t bytes) {
    uint32_t value=UINT32_MAX;
    for(size_t i=0; i<bytes; ++i) {
        value^=p[i];
        for(unsigned bit=0; bit<8u; ++bit)
            value=(value>>1) ^ (UINT32_C(0xedb88320) & (0u-(value&1u)));
    }
    return value ^ UINT32_MAX;
}

/* Return 1 for a recognized main-RAM physical/P1/P2 alias, 0 otherwise. */
static int volatile_chain_pointer_ram(const void *pointer, uint32_t *physical) {
    uintptr_t p=(uintptr_t)pointer;
#if UINTPTR_MAX > UINT32_MAX
    if(p>UINT32_MAX) return 0;
#endif
    uint32_t value=(uint32_t)p;
    if((value>=RAM_BEGIN && value<RAM_END) ||
       (value>=UINT32_C(0x8c000000) && value<UINT32_C(0x8d000000)) ||
       (value>=UINT32_C(0xac000000) && value<UINT32_C(0xad000000))) {
        *physical=value & UINT32_C(0x1fffffff);
        return 1;
    }
    return 0;
}

static int volatile_chain_pointer_span(const void *pointer, size_t bytes) {
    uint32_t physical;
    return pointer && bytes<=UINTPTR_MAX-(uintptr_t)pointer &&
        (!volatile_chain_pointer_ram(pointer,&physical) || bytes<=RAM_END-physical);
}

static int volatile_chain_pointer_overlap(const void *a, size_t an, const void *b, size_t bn) {
    uint32_t ap, bp;
    if(volatile_chain_pointer_ram(a,&ap) && volatile_chain_pointer_ram(b,&bp))
        return ap<bp+bn && bp<ap+an;
    return (uintptr_t)a<(uintptr_t)b+bn && (uintptr_t)b<(uintptr_t)a+an;
}

static int volatile_chain_ram_span(uint32_t address, uint32_t bytes) {
    return bytes && !(address&3u) && !(bytes&3u) && address>=RAM_BEGIN &&
        address<RAM_END && bytes<=RAM_END-address;
}

static int volatile_chain_physical_overlap(uint32_t a, uint32_t an, uint32_t b, uint32_t bn) {
    return a<b+bn && b<a+an;
}

static int volatile_chain_pointer_physical_overlap(const void *pointer, size_t bytes,
    uint32_t physical, uint32_t physical_bytes) {
    uint32_t address;
    return volatile_chain_pointer_ram(pointer,&address) &&
        address<physical+physical_bytes && physical<address+bytes;
}

static int volatile_chain_geometry(const struct kui_vmu_volatile_chain_plan *plan,
    const void *output, size_t capacity) {
    uint32_t address;
    if(!volatile_chain_pointer_span(plan,sizeof(*plan)) ||
       ((uintptr_t)plan % _Alignof(struct kui_vmu_volatile_chain_plan)) ||
       !volatile_chain_pointer_span(output,capacity) || ((uintptr_t)output&31u))
        return KUI_VMU_VOLATILE_CHAIN_ARGUMENT;
    if(plan->virtual_address!=1u && plan->virtual_address!=2u)
        return KUI_VMU_VOLATILE_CHAIN_ARGUMENT;
    size_t maximum=plan->virtual_address==2u?KUI_VMU_VOLATILE_CHAIN_PASSTHROUGH_BYTES:
        KUI_VMU_VOLATILE_CHAIN_BYTES;
    if(capacity<12u || capacity>maximum || (capacity&3u) ||
       capacity!=plan->output_capacity)
        return KUI_VMU_VOLATILE_CHAIN_CAPACITY;
    if((plan->source_physical&31u) || (plan->output_physical&31u) ||
       plan->source_bytes>KUI_VMU_VOLATILE_CHAIN_SOURCE_BYTES ||
       !volatile_chain_ram_span(plan->source_physical,plan->source_bytes) ||
       !volatile_chain_ram_span(plan->output_physical,(uint32_t)capacity) ||
       (volatile_chain_pointer_ram(output,&address) && address!=plan->output_physical))
        return KUI_VMU_VOLATILE_CHAIN_SPAN;
    if(volatile_chain_pointer_overlap(plan,sizeof(*plan),output,capacity) ||
       volatile_chain_physical_overlap(plan->source_physical,plan->source_bytes,
           plan->output_physical,(uint32_t)capacity) ||
       volatile_chain_pointer_physical_overlap(plan,sizeof(*plan),plan->source_physical,plan->source_bytes))
        return KUI_VMU_VOLATILE_CHAIN_OVERLAP;
    return KUI_VMU_VOLATILE_CHAIN_OK;
}

/* 0 is the filtered virtual endpoint. The explicit A2 path retains A1 VMU
 * memory and LCD requests with their complete immutable payloads. */
static int volatile_chain_classify(const struct kui_vmu_volatile_chain_frame *frame,
    unsigned virtual_address) {
    uint32_t control=frame->control, header=frame->header;
    unsigned words=header>>24, port=(control>>16)&3u;
    unsigned destination=(header>>8)&255u, command=header&255u;
    if((control & ~CONTROL_MASK) || words!=(control&255u) ||
       destination>>6!=port || ((header>>16)&255u)!=(port<<6))
        return KUI_VMU_VOLATILE_CHAIN_DESCRIPTOR;
    if(destination==virtual_address) return 0;
    unsigned unit=destination&63u;
    if(unit!=32u && (!unit || unit>=32u || (unit&(unit-1u))))
        return KUI_VMU_VOLATILE_CHAIN_UNSUPPORTED;
    if(((command==1u || command==2u) && !words) ||
       (command==9u && words==1u &&
        (unit==32u?frame->payload0==CONTROLLER:
         frame->payload0==CONTROLLER || frame->payload0==UINT32_C(0x02000000) ||
         frame->payload0==UINT32_C(0x04000000) || frame->payload0==UINT32_C(0x08000000) ||
         frame->payload0==UINT32_C(0x00010000)))) return 1;
    if(virtual_address==2u && destination==1u && command==3u && !words) return 1;
    if(virtual_address==2u && destination==1u && words>1u) {
        if(!volatile_chain_pointer_span(frame->payload,4u*words) ||
           volatile_chain_le32(frame->payload)!=frame->payload0)
            return KUI_VMU_VOLATILE_CHAIN_ARGUMENT;
        uint32_t selector=volatile_chain_le32(frame->payload+4u);
        if(frame->payload0==MEMCARD) {
            if(command==10u && words==2u && !selector) return 1;
            if(command==11u && words==2u && !(selector&UINT32_C(0x00ffffff))) return 1;
            if(!(selector&UINT32_C(0x00ff00ff))) {
                unsigned phase=(selector>>8)&255u;
                if(command==12u && words==34u && phase<4u) return 1;
                if(command==13u && words==2u && (phase==1u || phase==4u)) return 1;
            }
        }
        if(frame->payload0==CLOCK && command==14u && words==2u) return 1;
        if(frame->payload0==LCD && ((command==14u && words==49u) ||
           (command==12u && words==50u && !selector))) return 1;
    }
    return KUI_VMU_VOLATILE_CHAIN_UNSUPPORTED;
}

static int volatile_chain_receive_geometry(uint32_t rx,
    const struct kui_vmu_volatile_chain_plan *plan, size_t capacity) {
    if((rx&31u) || !volatile_chain_ram_span(rx,KUI_VMU_VOLATILE_CHAIN_RX_BYTES))
        return KUI_VMU_VOLATILE_CHAIN_SPAN;
    if(volatile_chain_physical_overlap(rx,KUI_VMU_VOLATILE_CHAIN_RX_BYTES,
           plan->source_physical,plan->source_bytes) ||
       volatile_chain_physical_overlap(rx,KUI_VMU_VOLATILE_CHAIN_RX_BYTES,
           plan->output_physical,(uint32_t)capacity) ||
       volatile_chain_pointer_physical_overlap(plan,sizeof(*plan),rx,KUI_VMU_VOLATILE_CHAIN_RX_BYTES))
        return KUI_VMU_VOLATILE_CHAIN_OVERLAP;
    return KUI_VMU_VOLATILE_CHAIN_OK;
}

int kui_vmu_volatile_chain_build_address(struct kui_vmu_volatile_chain_plan *result,
    const struct kui_vmu_volatile_chain_frame *frames, size_t count,
    uint32_t source, uint32_t source_bytes,
    uint8_t *output, size_t capacity, uint32_t output_physical, uint8_t virtual_address) {
    struct kui_vmu_volatile_chain_plan plan={source,source_bytes,output_physical,0,
        (uint32_t)capacity,(uint32_t)count,0,0,0,0,virtual_address};
    uint32_t input_bytes=0;
    int status;
    if(!volatile_chain_pointer_span(result,sizeof(*result)) ||
       ((uintptr_t)result % _Alignof(struct kui_vmu_volatile_chain_plan)) ||
       !count || count>KUI_VMU_VOLATILE_CHAIN_FRAMES ||
       !volatile_chain_pointer_span(frames,count*sizeof(*frames)) ||
       ((uintptr_t)frames % _Alignof(struct kui_vmu_volatile_chain_frame)))
        return KUI_VMU_VOLATILE_CHAIN_ARGUMENT;
    status=volatile_chain_geometry(&plan,output,capacity);
    if(status) return status;
    /* volatile_chain_geometry() used the local plan for its scalar validation. Check the
     * caller's result separately before either object can be written. */
    if(volatile_chain_pointer_overlap(result,sizeof(*result),frames,count*sizeof(*frames)) ||
       volatile_chain_pointer_overlap(result,sizeof(*result),output,capacity) ||
       volatile_chain_pointer_overlap(frames,count*sizeof(*frames),output,capacity) ||
       volatile_chain_pointer_physical_overlap(result,sizeof(*result),source,source_bytes) ||
       volatile_chain_pointer_physical_overlap(frames,count*sizeof(*frames),source,source_bytes))
        return KUI_VMU_VOLATILE_CHAIN_OVERLAP;
    for(size_t i=0; i<count; ++i) {
        const struct kui_vmu_volatile_chain_frame *frame=&frames[i];
        unsigned words=frame->control&255u;
        if(((frame->header>>8)&255u)!=virtual_address && words>1u) {
            size_t payload_bytes=4u*words;
            if(!volatile_chain_pointer_span(frame->payload,payload_bytes))
                return KUI_VMU_VOLATILE_CHAIN_ARGUMENT;
            if(volatile_chain_pointer_overlap(frame->payload,payload_bytes,result,sizeof(*result)) ||
               volatile_chain_pointer_overlap(frame->payload,payload_bytes,frames,count*sizeof(*frames)) ||
               volatile_chain_pointer_overlap(frame->payload,payload_bytes,output,capacity))
                return KUI_VMU_VOLATILE_CHAIN_OVERLAP;
            for(size_t j=0;j<count;++j)
                if(volatile_chain_pointer_physical_overlap(frame->payload,payload_bytes,
                       frames[j].rx,KUI_VMU_VOLATILE_CHAIN_RX_BYTES))
                    return KUI_VMU_VOLATILE_CHAIN_OVERLAP;
        }
        int physical=volatile_chain_classify(frame,virtual_address);
        if(physical<0) return physical;
        if(!!(frame->control&KUI_VMU_VOLATILE_CHAIN_LAST)!=(i+1u==count))
            return KUI_VMU_VOLATILE_CHAIN_DESCRIPTOR;
        status=volatile_chain_receive_geometry(frame->rx,&plan,capacity);
        if(status) return status;
        if(volatile_chain_pointer_physical_overlap(result,sizeof(*result),frame->rx,
               KUI_VMU_VOLATILE_CHAIN_RX_BYTES) ||
           volatile_chain_pointer_physical_overlap(frames,count*sizeof(*frames),frame->rx,
               KUI_VMU_VOLATILE_CHAIN_RX_BYTES))
            return KUI_VMU_VOLATILE_CHAIN_OVERLAP;
        for(size_t j=0; j<i; ++j)
            if(volatile_chain_physical_overlap(frame->rx,KUI_VMU_VOLATILE_CHAIN_RX_BYTES,
                   frames[j].rx,KUI_VMU_VOLATILE_CHAIN_RX_BYTES))
                return KUI_VMU_VOLATILE_CHAIN_OVERLAP;
        uint32_t bytes=12u+4u*(frame->control&255u);
        input_bytes+=bytes;
        if(physical) {++plan.physical_count;plan.output_bytes+=bytes;}
        else ++plan.virtual_count;
    }
    if(input_bytes>source_bytes)
        return KUI_VMU_VOLATILE_CHAIN_SPAN;
    plan.padding_count=(source_bytes-input_bytes)/4u;
    if(plan.padding_count>KUI_VMU_VOLATILE_CHAIN_ENTRIES-count)
        return KUI_VMU_VOLATILE_CHAIN_SPAN;
    if(!plan.physical_count) return KUI_VMU_VOLATILE_CHAIN_EMPTY;
    if(plan.output_bytes>capacity) return KUI_VMU_VOLATILE_CHAIN_CAPACITY;
    size_t offset=0;
    for(size_t i=0; i<count; ++i) {
        const struct kui_vmu_volatile_chain_frame *frame=&frames[i];
        if(((frame->header>>8)&255u)==virtual_address) continue;
        uint32_t bytes=12u+4u*(frame->control&255u);
        uint32_t control=frame->control & ~KUI_VMU_VOLATILE_CHAIN_LAST;
        if(offset+bytes==plan.output_bytes) control|=KUI_VMU_VOLATILE_CHAIN_LAST;
        volatile_chain_put32(output+offset,control);volatile_chain_put32(output+offset+4u,frame->rx);
        volatile_chain_put32(output+offset+8u,frame->header);
        if(bytes==16u) volatile_chain_put32(output+offset+12u,frame->payload0);
        else for(uint32_t j=12u;j<bytes;++j) output[offset+j]=frame->payload[j-12u];
        offset+=bytes;
    }
    plan.crc32=volatile_chain_crc32(output,plan.output_bytes);
    *result=plan;
    return KUI_VMU_VOLATILE_CHAIN_OK;
}

int kui_vmu_volatile_chain_build(struct kui_vmu_volatile_chain_plan *result,
    const struct kui_vmu_volatile_chain_frame *frames, size_t count,
    uint32_t source, uint32_t source_bytes,
    uint8_t *output, size_t capacity, uint32_t output_physical) {
    return kui_vmu_volatile_chain_build_address(result,frames,count,source,source_bytes,
        output,capacity,output_physical,1u);
}

int kui_vmu_volatile_chain_verify(const struct kui_vmu_volatile_chain_plan *plan,
    const uint8_t *output, size_t capacity) {
    int status=volatile_chain_geometry(plan,output,capacity);
    if(status) return status;
    if(!plan->frame_count || plan->frame_count>KUI_VMU_VOLATILE_CHAIN_FRAMES ||
       !plan->physical_count || plan->physical_count>plan->frame_count ||
       plan->virtual_count!=plan->frame_count-plan->physical_count ||
       plan->padding_count>KUI_VMU_VOLATILE_CHAIN_ENTRIES-plan->frame_count ||
       plan->output_bytes>capacity || (plan->output_bytes&3u) ||
       plan->output_bytes<12u*plan->physical_count ||
       plan->output_bytes>(plan->virtual_address==2u?1032u:16u)*plan->physical_count ||
       plan->source_bytes<plan->output_bytes+12u*plan->virtual_count+4u*plan->padding_count)
        return KUI_VMU_VOLATILE_CHAIN_DESCRIPTOR;
    if(volatile_chain_crc32(output,plan->output_bytes)!=plan->crc32)
        return KUI_VMU_VOLATILE_CHAIN_CHANGED;
    size_t offset=0;
    for(uint32_t i=0; i<plan->physical_count; ++i) {
        struct kui_vmu_volatile_chain_frame frame;
        if(plan->output_bytes-offset<12u)
            return KUI_VMU_VOLATILE_CHAIN_DESCRIPTOR;
        frame.control=volatile_chain_le32(output+offset);frame.rx=volatile_chain_le32(output+offset+4u);
        frame.header=volatile_chain_le32(output+offset+8u);frame.payload0=0;frame.payload=NULL;
        uint32_t bytes=12u+4u*(frame.control&255u);
        if(bytes>plan->output_bytes-offset)
            return KUI_VMU_VOLATILE_CHAIN_DESCRIPTOR;
        if(bytes>12u) frame.payload0=volatile_chain_le32(output+offset+12u);
        if(bytes>16u) frame.payload=output+offset+12u;
        if(volatile_chain_classify(&frame,plan->virtual_address)!=1 ||
           !!(frame.control&KUI_VMU_VOLATILE_CHAIN_LAST)!=(i+1u==plan->physical_count))
            return KUI_VMU_VOLATILE_CHAIN_DESCRIPTOR;
        status=volatile_chain_receive_geometry(frame.rx,plan,capacity);
        if(status) return status;
        size_t previous=0;
        for(uint32_t j=0; j<i; ++j) {
            uint32_t rx=volatile_chain_le32(output+previous+4u);
            if(volatile_chain_physical_overlap(frame.rx,KUI_VMU_VOLATILE_CHAIN_RX_BYTES,
                   rx,KUI_VMU_VOLATILE_CHAIN_RX_BYTES))
                return KUI_VMU_VOLATILE_CHAIN_OVERLAP;
            previous+=12u+4u*(volatile_chain_le32(output+previous)&255u);
        }
        offset+=bytes;
    }
    return offset==plan->output_bytes ? KUI_VMU_VOLATILE_CHAIN_OK :
        KUI_VMU_VOLATILE_CHAIN_DESCRIPTOR;
}
