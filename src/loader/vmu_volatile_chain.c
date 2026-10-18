/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_volatile_chain.h"

#define RAM_BEGIN UINT32_C(0x0c000000)
#define RAM_END UINT32_C(0x0d000000)
#define CONTROL_MASK UINT32_C(0x800300ff)
#define CONTROLLER UINT32_C(0x01000000)

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
        (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void put32(uint8_t *p, uint32_t value) {
    for(unsigned i=0; i<4u; ++i) p[i]=(uint8_t)(value >> (i*8u));
}

static uint32_t crc32(const uint8_t *p, size_t bytes) {
    uint32_t value=UINT32_MAX;
    for(size_t i=0; i<bytes; ++i) {
        value^=p[i];
        for(unsigned bit=0; bit<8u; ++bit)
            value=(value>>1) ^ (UINT32_C(0xedb88320) & (0u-(value&1u)));
    }
    return value ^ UINT32_MAX;
}

/* Return 1 for a recognized main-RAM physical/P1/P2 alias, 0 otherwise. */
static int pointer_ram(const void *pointer, uint32_t *physical) {
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

static int pointer_span(const void *pointer, size_t bytes) {
    uint32_t physical;
    return pointer && bytes<=UINTPTR_MAX-(uintptr_t)pointer &&
        (!pointer_ram(pointer,&physical) || bytes<=RAM_END-physical);
}

static int pointer_overlap(const void *a, size_t an, const void *b, size_t bn) {
    uint32_t ap, bp;
    if(pointer_ram(a,&ap) && pointer_ram(b,&bp))
        return ap<bp+bn && bp<ap+an;
    return (uintptr_t)a<(uintptr_t)b+bn && (uintptr_t)b<(uintptr_t)a+an;
}

static int ram_span(uint32_t address, uint32_t bytes) {
    return bytes && !(address&3u) && !(bytes&3u) && address>=RAM_BEGIN &&
        address<RAM_END && bytes<=RAM_END-address;
}

static int physical_overlap(uint32_t a, uint32_t an, uint32_t b, uint32_t bn) {
    return a<b+bn && b<a+an;
}

static int pointer_physical_overlap(const void *pointer, size_t bytes,
    uint32_t physical, uint32_t physical_bytes) {
    uint32_t address;
    return pointer_ram(pointer,&address) &&
        address<physical+physical_bytes && physical<address+bytes;
}

static int geometry(const struct kui_vmu_volatile_chain_plan *plan,
    const void *output, size_t capacity) {
    uint32_t address;
    if(!pointer_span(plan,sizeof(*plan)) ||
       ((uintptr_t)plan % _Alignof(struct kui_vmu_volatile_chain_plan)) ||
       !pointer_span(output,capacity) || ((uintptr_t)output&31u))
        return KUI_VMU_VOLATILE_CHAIN_ARGUMENT;
    if(capacity<12u || capacity>KUI_VMU_VOLATILE_CHAIN_BYTES || (capacity&3u) ||
       capacity!=plan->output_capacity)
        return KUI_VMU_VOLATILE_CHAIN_CAPACITY;
    if((plan->source_physical&31u) || (plan->output_physical&31u) ||
       plan->source_bytes>KUI_VMU_VOLATILE_CHAIN_SOURCE_BYTES ||
       !ram_span(plan->source_physical,plan->source_bytes) ||
       !ram_span(plan->output_physical,(uint32_t)capacity) ||
       (pointer_ram(output,&address) && address!=plan->output_physical))
        return KUI_VMU_VOLATILE_CHAIN_SPAN;
    if(pointer_overlap(plan,sizeof(*plan),output,capacity) ||
       physical_overlap(plan->source_physical,plan->source_bytes,
           plan->output_physical,(uint32_t)capacity) ||
       pointer_physical_overlap(plan,sizeof(*plan),plan->source_physical,plan->source_bytes))
        return KUI_VMU_VOLATILE_CHAIN_OVERLAP;
    return KUI_VMU_VOLATILE_CHAIN_OK;
}

/* 0 is a filtered A1 request; 1 is an admitted physical MAIN request. */
static int classify(const struct kui_vmu_volatile_chain_frame *frame) {
    uint32_t control=frame->control, header=frame->header;
    unsigned words=header>>24, port=(control>>16)&3u;
    unsigned destination=(header>>8)&255u, command=header&255u;
    if((control & ~CONTROL_MASK) || words!=(control&255u) ||
       destination>>6!=port || ((header>>16)&255u)!=(port<<6))
        return KUI_VMU_VOLATILE_CHAIN_DESCRIPTOR;
    if(destination==1u) return 0;
    if((destination&63u)!=32u)
        return KUI_VMU_VOLATILE_CHAIN_UNSUPPORTED;
    if(((command==1u || command==2u) && !words) ||
       (command==9u && words==1u && frame->payload0==CONTROLLER)) return 1;
    return KUI_VMU_VOLATILE_CHAIN_UNSUPPORTED;
}

static int receive_geometry(uint32_t rx,
    const struct kui_vmu_volatile_chain_plan *plan, size_t capacity) {
    if((rx&31u) || !ram_span(rx,KUI_VMU_VOLATILE_CHAIN_RX_BYTES))
        return KUI_VMU_VOLATILE_CHAIN_SPAN;
    if(physical_overlap(rx,KUI_VMU_VOLATILE_CHAIN_RX_BYTES,
           plan->source_physical,plan->source_bytes) ||
       physical_overlap(rx,KUI_VMU_VOLATILE_CHAIN_RX_BYTES,
           plan->output_physical,(uint32_t)capacity) ||
       pointer_physical_overlap(plan,sizeof(*plan),rx,KUI_VMU_VOLATILE_CHAIN_RX_BYTES))
        return KUI_VMU_VOLATILE_CHAIN_OVERLAP;
    return KUI_VMU_VOLATILE_CHAIN_OK;
}

int kui_vmu_volatile_chain_build(struct kui_vmu_volatile_chain_plan *result,
    const struct kui_vmu_volatile_chain_frame *frames, size_t count,
    uint32_t source, uint32_t source_bytes,
    uint8_t *output, size_t capacity, uint32_t output_physical) {
    struct kui_vmu_volatile_chain_plan plan={source,source_bytes,output_physical,0,
        (uint32_t)capacity,(uint32_t)count,0,0,0,0};
    uint32_t input_bytes=0;
    int status;
    if(!pointer_span(result,sizeof(*result)) ||
       ((uintptr_t)result % _Alignof(struct kui_vmu_volatile_chain_plan)) ||
       !count || count>KUI_VMU_VOLATILE_CHAIN_FRAMES ||
       !pointer_span(frames,count*sizeof(*frames)) ||
       ((uintptr_t)frames % _Alignof(struct kui_vmu_volatile_chain_frame)))
        return KUI_VMU_VOLATILE_CHAIN_ARGUMENT;
    status=geometry(&plan,output,capacity);
    if(status) return status;
    /* geometry() used the local plan for its scalar validation. Check the
     * caller's result separately before either object can be written. */
    if(pointer_overlap(result,sizeof(*result),frames,count*sizeof(*frames)) ||
       pointer_overlap(result,sizeof(*result),output,capacity) ||
       pointer_overlap(frames,count*sizeof(*frames),output,capacity) ||
       pointer_physical_overlap(result,sizeof(*result),source,source_bytes) ||
       pointer_physical_overlap(frames,count*sizeof(*frames),source,source_bytes))
        return KUI_VMU_VOLATILE_CHAIN_OVERLAP;
    for(size_t i=0; i<count; ++i) {
        const struct kui_vmu_volatile_chain_frame *frame=&frames[i];
        int physical=classify(frame);
        if(physical<0) return physical;
        if(!!(frame->control&KUI_VMU_VOLATILE_CHAIN_LAST)!=(i+1u==count))
            return KUI_VMU_VOLATILE_CHAIN_DESCRIPTOR;
        status=receive_geometry(frame->rx,&plan,capacity);
        if(status) return status;
        if(pointer_physical_overlap(result,sizeof(*result),frame->rx,
               KUI_VMU_VOLATILE_CHAIN_RX_BYTES) ||
           pointer_physical_overlap(frames,count*sizeof(*frames),frame->rx,
               KUI_VMU_VOLATILE_CHAIN_RX_BYTES))
            return KUI_VMU_VOLATILE_CHAIN_OVERLAP;
        for(size_t j=0; j<i; ++j)
            if(physical_overlap(frame->rx,KUI_VMU_VOLATILE_CHAIN_RX_BYTES,
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
        if(((frame->header>>8)&255u)==1u) continue;
        uint32_t bytes=12u+4u*(frame->control&255u);
        uint32_t control=frame->control & ~KUI_VMU_VOLATILE_CHAIN_LAST;
        if(offset+bytes==plan.output_bytes) control|=KUI_VMU_VOLATILE_CHAIN_LAST;
        put32(output+offset,control);put32(output+offset+4u,frame->rx);
        put32(output+offset+8u,frame->header);
        if(bytes==16u) put32(output+offset+12u,frame->payload0);
        offset+=bytes;
    }
    plan.crc32=crc32(output,plan.output_bytes);
    *result=plan;
    return KUI_VMU_VOLATILE_CHAIN_OK;
}

int kui_vmu_volatile_chain_verify(const struct kui_vmu_volatile_chain_plan *plan,
    const uint8_t *output, size_t capacity) {
    int status=geometry(plan,output,capacity);
    if(status) return status;
    if(!plan->frame_count || plan->frame_count>KUI_VMU_VOLATILE_CHAIN_FRAMES ||
       !plan->physical_count || plan->physical_count>plan->frame_count ||
       plan->virtual_count!=plan->frame_count-plan->physical_count ||
       plan->padding_count>KUI_VMU_VOLATILE_CHAIN_ENTRIES-plan->frame_count ||
       plan->output_bytes>capacity || (plan->output_bytes&3u) ||
       plan->output_bytes<12u*plan->physical_count ||
       plan->output_bytes>16u*plan->physical_count ||
       plan->source_bytes<plan->output_bytes+12u*plan->virtual_count+4u*plan->padding_count)
        return KUI_VMU_VOLATILE_CHAIN_DESCRIPTOR;
    if(crc32(output,plan->output_bytes)!=plan->crc32)
        return KUI_VMU_VOLATILE_CHAIN_CHANGED;
    size_t offset=0;
    for(uint32_t i=0; i<plan->physical_count; ++i) {
        struct kui_vmu_volatile_chain_frame frame;
        if(plan->output_bytes-offset<12u)
            return KUI_VMU_VOLATILE_CHAIN_DESCRIPTOR;
        frame.control=le32(output+offset);frame.rx=le32(output+offset+4u);
        frame.header=le32(output+offset+8u);frame.payload0=0;
        uint32_t bytes=12u+4u*(frame.control&255u);
        if(bytes>plan->output_bytes-offset)
            return KUI_VMU_VOLATILE_CHAIN_DESCRIPTOR;
        if(bytes==16u) frame.payload0=le32(output+offset+12u);
        if(classify(&frame)!=1 ||
           !!(frame.control&KUI_VMU_VOLATILE_CHAIN_LAST)!=(i+1u==plan->physical_count))
            return KUI_VMU_VOLATILE_CHAIN_DESCRIPTOR;
        status=receive_geometry(frame.rx,plan,capacity);
        if(status) return status;
        size_t previous=0;
        for(uint32_t j=0; j<i; ++j) {
            uint32_t rx=le32(output+previous+4u);
            if(physical_overlap(frame.rx,KUI_VMU_VOLATILE_CHAIN_RX_BYTES,
                   rx,KUI_VMU_VOLATILE_CHAIN_RX_BYTES))
                return KUI_VMU_VOLATILE_CHAIN_OVERLAP;
            previous+=12u+4u*(le32(output+previous)&255u);
        }
        offset+=bytes;
    }
    return offset==plan->output_bytes ? KUI_VMU_VOLATILE_CHAIN_OK :
        KUI_VMU_VOLATILE_CHAIN_DESCRIPTOR;
}
