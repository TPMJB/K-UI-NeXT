/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_maple_dma.h"

#include <string.h>

#define PLAN_PREPARED 1u
#define PLAN_COMPLETING 2u
#define PLAN_COMPLETE 3u
#define NORMAL_CONTROL_MASK UINT32_C(0x800300ff)

static uint32_t read_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
        | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void write_le32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static int valid_span(const void *p, size_t n) {
    return (!n || p) && n <= UINTPTR_MAX - (uintptr_t)p;
}

static int overlaps(const void *a, size_t an, const void *b, size_t bn) {
    uintptr_t ap = (uintptr_t)a, bp = (uintptr_t)b;
    return an && bn && ap < bp + bn && bp < ap + an;
}

static int one_bit(unsigned value) {
    return value && !(value & (value - 1u));
}

static int valid_unit(uint8_t address) {
    unsigned unit = address & 0x3fu;
    return unit == 0x20u || (unit < 0x20u && one_bit(unit));
}

static uint8_t *find_reply(uint32_t address,
    const struct kui_vmu_maple_dma_region *regions, size_t count) {
    size_t i;
    for(i = 0; i < count; ++i) {
        const struct kui_vmu_maple_dma_region *r = &regions[i];
        if(address >= r->physical_base) {
            size_t offset = address - r->physical_base;
            if(offset <= r->size && KUI_VMU_MAPLE_DMA_REPLY_BYTES <= r->size - offset)
                return r->bytes + offset;
        }
    }
    return NULL;
}

int kui_vmu_maple_dma_prepare(struct kui_vmu_maple_dma_plan *plan,
    uint8_t virtual_address, const uint8_t *source_chain, size_t source_capacity,
    uint8_t *hardware_chain, size_t hardware_capacity,
    const struct kui_vmu_maple_dma_region *regions, size_t region_count) {
    size_t i, j, offset = 0, output = 0, last_output = 0;
    unsigned subunit = virtual_address & 0x3fu;

    if(!plan)
        return KUI_VMU_MAPLE_DMA_INVALID;
    plan->state = 0;
    if(!one_bit(subunit) || subunit >= 0x20u
        || !source_chain || source_capacity < 12u || (source_capacity & 3u)
        || !regions || !region_count || region_count > KUI_VMU_MAPLE_DMA_MAX_FRAMES
        || !valid_span(source_chain, source_capacity)
        || !valid_span(hardware_chain, hardware_capacity)
        || !valid_span(plan, sizeof(*plan))
        || !valid_span(regions, region_count * sizeof(*regions))
        || ((uintptr_t)hardware_chain & 31u))
        return KUI_VMU_MAPLE_DMA_INVALID;

    if(overlaps(plan, sizeof(*plan), source_chain, source_capacity)
        || overlaps(plan, sizeof(*plan), hardware_chain, hardware_capacity)
        || overlaps(plan, sizeof(*plan), regions, region_count * sizeof(*regions))
        || overlaps(hardware_chain, hardware_capacity, source_chain, source_capacity)
        || overlaps(hardware_chain, hardware_capacity, regions, region_count * sizeof(*regions)))
        return KUI_VMU_MAPLE_DMA_INVALID;

    for(i = 0; i < region_count; ++i) {
        const struct kui_vmu_maple_dma_region *r = &regions[i];
        if(!r->bytes || r->size < KUI_VMU_MAPLE_DMA_REPLY_BYTES
            || r->physical_base < KUI_VMU_MAPLE_DMA_RAM_BASE
            || r->physical_base >= KUI_VMU_MAPLE_DMA_RAM_END
            || r->size > KUI_VMU_MAPLE_DMA_RAM_END - r->physical_base
            || (r->physical_base & 31u) || !valid_span(r->bytes, r->size)
            || overlaps(r->bytes, r->size, plan, sizeof(*plan))
            || overlaps(r->bytes, r->size, source_chain, source_capacity)
            || overlaps(r->bytes, r->size, hardware_chain, hardware_capacity)
            || overlaps(r->bytes, r->size, regions, region_count * sizeof(*regions)))
            return KUI_VMU_MAPLE_DMA_INVALID;
        for(j = 0; j < i; ++j) {
            const struct kui_vmu_maple_dma_region *other = &regions[j];
            if(overlaps(r->bytes, r->size, other->bytes, other->size)
                || (r->physical_base < other->physical_base + other->size
                    && other->physical_base < r->physical_base + r->size))
                return KUI_VMU_MAPLE_DMA_INVALID;
        }
    }

    memset(plan, 0, sizeof(*plan));
    plan->address = virtual_address;

    for(;;) {
        struct kui_vmu_maple_dma_frame *frame;
        const uint8_t *request;
        uint32_t control, receive;
        size_t descriptor_bytes;
        unsigned port;
        if(plan->frame_count == KUI_VMU_MAPLE_DMA_MAX_FRAMES)
            return KUI_VMU_MAPLE_DMA_CAPACITY;
        if(offset > source_capacity || source_capacity - offset < 12u)
            return KUI_VMU_MAPLE_DMA_INVALID;
        control = read_le32(source_chain + offset);
        if(control & ~NORMAL_CONTROL_MASK)
            return KUI_VMU_MAPLE_DMA_UNSUPPORTED;
        descriptor_bytes = 12u + 4u * (control & 0xffu);
        if(descriptor_bytes > source_capacity - offset)
            return KUI_VMU_MAPLE_DMA_INVALID;
        request = source_chain + offset + 8u;
        port = (control >> 16) & 3u;
        if(request[3] != (control & 0xffu) || (request[1] >> 6) != port
            || request[2] != port << 6)
            return KUI_VMU_MAPLE_DMA_INVALID;
        if(!valid_unit(request[1]))
            return KUI_VMU_MAPLE_DMA_UNSUPPORTED;
        receive = read_le32(source_chain + offset + 4u);
        if(receive & 31u)
            return KUI_VMU_MAPLE_DMA_INVALID;
        frame = &plan->frames[plan->frame_count];
        frame->response = find_reply(receive, regions, region_count);
        if(!frame->response)
            return KUI_VMU_MAPLE_DMA_INVALID;
        for(i = 0; i < plan->frame_count; ++i) {
            if(overlaps(frame->response, KUI_VMU_MAPLE_DMA_REPLY_BYTES,
                    plan->frames[i].response, KUI_VMU_MAPLE_DMA_REPLY_BYTES))
                return KUI_VMU_MAPLE_DMA_INVALID;
        }
        frame->request = request;
        frame->request_bytes = descriptor_bytes - 8u;
        frame->is_virtual = request[1] == virtual_address;
        frame->is_parent = (request[1] == ((virtual_address & 0xc0u) | 0x20u));
        ++plan->frame_count;
        if(frame->is_virtual)
            ++plan->virtual_count;
        else {
            ++plan->hardware_count;
            if(output > hardware_capacity || descriptor_bytes > hardware_capacity - output)
                return KUI_VMU_MAPLE_DMA_CAPACITY;
            output += descriptor_bytes;
        }
        offset += descriptor_bytes;
        if(control & KUI_VMU_MAPLE_DMA_LAST)
            break;
    }

    /* No copy or externally visible response change until all validation passes. */
    plan->source_bytes = offset;
    plan->hardware_bytes = output;
    output = 0;
    for(i = 0; i < plan->frame_count; ++i) {
        const struct kui_vmu_maple_dma_frame *frame = &plan->frames[i];
        size_t n = frame->request_bytes + 8u;
        if(frame->is_virtual)
            continue;
        memcpy(hardware_chain + output, frame->request - 8u, n);
        write_le32(hardware_chain + output,
            read_le32(hardware_chain + output) & ~KUI_VMU_MAPLE_DMA_LAST);
        last_output = output;
        output += n;
    }
    if(plan->hardware_count)
        write_le32(hardware_chain + last_output,
            read_le32(hardware_chain + last_output) | KUI_VMU_MAPLE_DMA_LAST);
    plan->state = PLAN_PREPARED;
    return KUI_VMU_MAPLE_DMA_OK;
}

int kui_vmu_maple_dma_complete(struct kui_vmu_maple_dma_plan *plan,
    kui_vmu_maple_dma_handler_fn handler, void *context) {
    size_t i;
    if(!plan || plan->state != PLAN_PREPARED)
        return KUI_VMU_MAPLE_DMA_STATE;
    if(plan->virtual_count && !handler)
        return KUI_VMU_MAPLE_DMA_INVALID;
    /* Mark terminal before calling external code: callbacks must not reenter. */
    plan->state = PLAN_COMPLETING;
    for(i = 0; i < plan->frame_count; ++i) {
        const struct kui_vmu_maple_dma_frame *frame = &plan->frames[i];
        if(frame->is_virtual) {
            size_t response_bytes = 0;
            int timeout_marker;
            int handled = handler(context, frame->request, frame->request_bytes,
                frame->response, KUI_VMU_MAPLE_DMA_REPLY_BYTES, &response_bytes);
            timeout_marker = response_bytes == 4u
                && read_le32(frame->response) == UINT32_MAX;
            if(handled != 1 || response_bytes < 4u
                || response_bytes > KUI_VMU_MAPLE_DMA_REPLY_BYTES
                || (!timeout_marker && (response_bytes != 4u + 4u * frame->response[3]
                    || frame->response[1] != frame->request[2]
                    || frame->response[2] != plan->address))) {
                plan->state = 0;
                return KUI_VMU_MAPLE_DMA_HANDLER;
            }
        }
    }
    for(i = 0; i < plan->frame_count; ++i) {
        const struct kui_vmu_maple_dma_frame *frame = &plan->frames[i];
        uint8_t *response = frame->response;
        uint8_t port = plan->address & 0xc0u;
        if(frame->is_parent && response[0] >= 5u && response[0] <= 8u
            && response[1] == port && (response[2] & 0xe0u) == (port | 0x20u))
            response[2] |= plan->address & 0x1fu;
    }
    plan->state = PLAN_COMPLETE;
    return KUI_VMU_MAPLE_DMA_OK;
}
