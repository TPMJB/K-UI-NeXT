/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_volatile_chain.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define SOURCE UINT32_C(0x0c100000)
#define OUTPUT UINT32_C(0x0c200000)
#define RECEIVE UINT32_C(0x0c400000)
#define LAST KUI_VMU_VOLATILE_CHAIN_LAST

static struct {
    struct kui_vmu_volatile_chain_frame frames[32];
    struct kui_vmu_volatile_chain_plan plan;
    _Alignas(32) uint8_t output[512];
    uint8_t raw[0x3000];
    size_t count, raw_bytes;
} fixture;

static uint32_t word(const uint8_t *p) {
    uint32_t value=0;
    for(unsigned i=0; i<4u; ++i) value|=(uint32_t)p[i]<<(8u*i);
    return value;
}

static void store(uint8_t *p, uint32_t value) {
    for(unsigned i=0; i<4u; ++i) p[i]=(uint8_t)(value>>(8u*i));
}

/* An independent MSB-first implementation of the reflected IEEE CRC. */
static uint32_t reverse(uint32_t value) {
    uint32_t result=0;
    for(unsigned i=0; i<32u; ++i) {result=(result<<1)|(value&1u);value>>=1;}
    return result;
}

static uint32_t expected_crc(const uint8_t *p, size_t bytes) {
    uint32_t value=UINT32_MAX;
    for(size_t i=0; i<bytes; ++i) {
        value^=reverse(p[i])&UINT32_C(0xff000000);
        for(unsigned bit=0; bit<8u; ++bit)
            value=(value<<1)^((value>>31)?UINT32_C(0x04c11db7):0u);
    }
    return reverse(value)^UINT32_MAX;
}

static void reset(void) {
    memset(&fixture,0,sizeof(fixture));
    memset(fixture.output,0xa5,sizeof(fixture.output));
    memset(&fixture.plan,0x3c,sizeof(fixture.plan));
}

static size_t append(unsigned command, unsigned destination, unsigned words,
    uint32_t payload0, int last) {
    assert(fixture.count<32u && words<256u);
    size_t start=fixture.raw_bytes;
    size_t bytes=12u+4u*words;
    assert(start+bytes<=sizeof(fixture.raw));
    struct kui_vmu_volatile_chain_frame *f=&fixture.frames[fixture.count];
    f->control=((uint32_t)(destination>>6)<<16)|words|(last?LAST:0u);
    f->rx=RECEIVE+(uint32_t)fixture.count*1024u;
    f->header=command|destination<<8|(destination&0xc0u)<<16|words<<24;
    f->payload0=payload0;
    store(fixture.raw+start,f->control);store(fixture.raw+start+4u,f->rx);
    store(fixture.raw+start+8u,f->header);
    for(unsigned i=0; i<words; ++i)
        store(fixture.raw+start+12u+4u*i,i?UINT32_C(0xdecafbad)^i:payload0);
    fixture.raw_bytes+=bytes;++fixture.count;
    return start;
}

static void padding(unsigned count) {
    assert(fixture.raw_bytes+4u*count<=sizeof(fixture.raw));
    for(unsigned i=0; i<count; ++i) {
        store(fixture.raw+fixture.raw_bytes,UINT32_C(0x00000700));
        fixture.raw_bytes+=4u;
    }
}

static int build(void) {
    return kui_vmu_volatile_chain_build(&fixture.plan,fixture.frames,fixture.count,
        SOURCE,(uint32_t)fixture.raw_bytes,fixture.output,sizeof(fixture.output),OUTPUT);
}

static void untouched_failure(void) {
    uint8_t output[sizeof(fixture.output)];
    struct kui_vmu_volatile_chain_plan plan=fixture.plan;
    struct kui_vmu_volatile_chain_frame frames[32];
    uint8_t raw[sizeof(fixture.raw)];
    memcpy(output,fixture.output,sizeof(output));
    memcpy(frames,fixture.frames,sizeof(frames));memcpy(raw,fixture.raw,sizeof(raw));
    assert(build()!=KUI_VMU_VOLATILE_CHAIN_OK);
    assert(!memcmp(output,fixture.output,sizeof(output)));
    assert(!memcmp(&plan,&fixture.plan,sizeof(plan)));
    assert(!memcmp(frames,fixture.frames,sizeof(frames)));
    assert(!memcmp(raw,fixture.raw,sizeof(raw)));
}

static void mixed_byte_exact(void) {
    reset();
    uint8_t expected[512]={0}, raw[sizeof(fixture.raw)];
    size_t offsets[4], sizes[4]={12,16,12,16}, used=0;
    offsets[0]=append(1,0x20,0,0,0);padding(5);
    append(12,1,34,UINT32_C(0x02000000),0);padding(5);
    offsets[1]=append(9,0x60,1,UINT32_C(0x01000000),0);
    offsets[2]=append(2,0xa0,0,0,0);padding(2);
    append(13,1,2,UINT32_C(0x02000000),0);
    offsets[3]=append(9,0xe0,1,UINT32_C(0x01000000),0);
    append(11,1,2,UINT32_C(0x02000000),1);
    for(unsigned i=0; i<4u; ++i) {
        memcpy(expected+used,fixture.raw+offsets[i],sizes[i]);
        store(expected+used,word(expected+used)&~LAST);
        if(i==3u) store(expected+used,word(expected+used)|LAST);
        used+=sizes[i];
    }
    memcpy(raw,fixture.raw,sizeof(raw));
    assert(build()==0);
    assert(fixture.plan.source_physical==SOURCE && fixture.plan.source_bytes==fixture.raw_bytes);
    assert(fixture.plan.output_physical==OUTPUT && fixture.plan.output_bytes==used);
    assert(fixture.plan.output_capacity==512 && fixture.plan.frame_count==7);
    assert(fixture.plan.physical_count==4 && fixture.plan.virtual_count==3);
    assert(fixture.plan.padding_count==12);
    assert(fixture.plan.crc32==expected_crc(expected,used));
    assert(!memcmp(expected,fixture.output,used));
    for(size_t i=used; i<sizeof(fixture.output); ++i) assert(fixture.output[i]==0xa5u);
    assert(!memcmp(raw,fixture.raw,sizeof(raw)));
    assert(kui_vmu_volatile_chain_verify(&fixture.plan,fixture.output,sizeof(fixture.output))==0);
}

static void every_virtual_command_is_filtered(void) {
    for(unsigned command=0; command<256u; ++command) {
        reset();
        append(1,0x20,0,0,0);padding(5);
        append(command,1,command,UINT32_C(0x02000000),1);
        assert(build()==0);
        assert(fixture.plan.physical_count==1 && fixture.plan.virtual_count==1);
        assert(fixture.plan.output_bytes==12 && word(fixture.output)==LAST);
        assert(word(fixture.output+8u)==UINT32_C(0x00002001));
        assert(kui_vmu_volatile_chain_verify(&fixture.plan,fixture.output,512)==0);
    }
    reset();append(12,1,34,UINT32_C(0x02000000),1);untouched_failure();
}

static void strict_physical_whitelist(void) {
    for(unsigned port=0; port<4u; ++port) {
        for(unsigned command=0; command<256u; ++command) {
            reset();
            append(command,(port<<6)|32u,command==9u?1u:0u,UINT32_C(0x01000000),1);
            if(command==1u || command==2u || command==9u) {
                assert(build()==0);
                assert(kui_vmu_volatile_chain_verify(&fixture.plan,fixture.output,512)==0);
            } else untouched_failure();
        }
        for(unsigned unit=0; unit<64u; ++unit) if(unit!=32u && (port || unit!=1u)) {
            reset();append(1,(port<<6)|unit,0,0,1);untouched_failure();
        }
    }
    for(unsigned words=0; words<3u; ++words) {
        reset();append(9,0x20,words,UINT32_C(0x01000000),1);
        if(words==1u) assert(build()==0);else untouched_failure();
    }
    reset();append(9,0x20,1,UINT32_C(0x02000000),1);untouched_failure();
    reset();append(1,0x20,1,0,1);untouched_failure();
    reset();append(2,0x20,1,0,1);untouched_failure();
}

static void invalid_descriptors_are_atomic(void) {
    reset();append(1,0x20,0,0,0);untouched_failure();
    reset();append(1,0x20,0,0,1);append(2,0x60,0,0,1);untouched_failure();
    reset();append(1,0x20,0,0,0);append(9,0x60,1,UINT32_C(0x01000000),1);
    fixture.frames[1].header^=UINT32_C(0x01000000);untouched_failure();
    for(unsigned bit=8u; bit<31u; ++bit) if(bit!=16u && bit!=17u) {
        reset();append(1,0x20,0,0,1);fixture.frames[0].control|=UINT32_C(1)<<bit;
        untouched_failure();
    }
    reset();append(1,0x20,0,0,1);fixture.frames[0].header|=UINT32_C(0x00400000);untouched_failure();
    reset();append(1,0x20,0,0,1);fixture.frames[0].control|=UINT32_C(0x10000);untouched_failure();
    reset();append(1,0x20,0,0,1);fixture.frames[0].control=UINT32_C(0x00000700);untouched_failure();
    reset();append(1,0x20,0,0,1);fixture.raw_bytes-=4u;untouched_failure();
    reset();append(1,0x20,0,0,1);padding(256);untouched_failure();
}

static void full_rx_and_output_geometry(void) {
    uint32_t bad[]={RECEIVE+4u,UINT32_C(0x0bffffe0),UINT32_C(0x0cffffe0),
        SOURCE,SOURCE-512u,OUTPUT,OUTPUT+480u};
    for(size_t i=0; i<sizeof(bad)/sizeof(bad[0]); ++i) {
        reset();append(1,0x20,0,0,1);fixture.frames[0].rx=bad[i];untouched_failure();
    }
    reset();append(1,0x20,0,0,0);append(12,1,34,0,1);
    fixture.frames[1].rx=fixture.frames[0].rx+32u;untouched_failure();
    reset();append(1,0x20,0,0,0);append(12,1,34,0,1);
    fixture.frames[1].rx=OUTPUT+480u;untouched_failure();
    reset();append(1,0x20,0,0,1);
    uint8_t before[512];memcpy(before,fixture.output,512);
    assert(kui_vmu_volatile_chain_build(&fixture.plan,fixture.frames,1,SOURCE,12,
        fixture.output,512,SOURCE+32u)==KUI_VMU_VOLATILE_CHAIN_OK);
    /* A full output allocation at SOURCE-32 overlaps the source despite its
     * twelve emitted bytes ending before the SDK source begins. */
    memset(fixture.output,0xa5,512);
    assert(kui_vmu_volatile_chain_build(&fixture.plan,fixture.frames,1,SOURCE,12,
        fixture.output,512,SOURCE-32u)==KUI_VMU_VOLATILE_CHAIN_OVERLAP);
    assert(!memcmp(before,fixture.output,512));
    assert(kui_vmu_volatile_chain_build(&fixture.plan,fixture.frames,1,SOURCE+4u,12,
        fixture.output,512,OUTPUT)==KUI_VMU_VOLATILE_CHAIN_SPAN);
    assert(kui_vmu_volatile_chain_build(&fixture.plan,fixture.frames,1,SOURCE,12,
        fixture.output+1u,508,OUTPUT)==KUI_VMU_VOLATILE_CHAIN_ARGUMENT);
}

static void aliases_and_arguments(void) {
    reset();append(1,0x20,0,0,1);
    assert(kui_vmu_volatile_chain_build(NULL,fixture.frames,1,SOURCE,12,
        fixture.output,512,OUTPUT)==KUI_VMU_VOLATILE_CHAIN_ARGUMENT);
    assert(kui_vmu_volatile_chain_build(&fixture.plan,NULL,1,SOURCE,12,
        fixture.output,512,OUTPUT)==KUI_VMU_VOLATILE_CHAIN_ARGUMENT);
    assert(kui_vmu_volatile_chain_build(&fixture.plan,fixture.frames,0,SOURCE,12,
        fixture.output,512,OUTPUT)==KUI_VMU_VOLATILE_CHAIN_ARGUMENT);
    assert(kui_vmu_volatile_chain_build(&fixture.plan,fixture.frames,33,SOURCE,12,
        fixture.output,512,OUTPUT)==KUI_VMU_VOLATILE_CHAIN_ARGUMENT);
    assert(kui_vmu_volatile_chain_build(&fixture.plan,fixture.frames,1,SOURCE,12,
        fixture.output,8,OUTPUT)==KUI_VMU_VOLATILE_CHAIN_CAPACITY);
    assert(kui_vmu_volatile_chain_build(&fixture.plan,fixture.frames,1,SOURCE,12,
        fixture.output,516,OUTPUT)==KUI_VMU_VOLATILE_CHAIN_CAPACITY);
    assert(kui_vmu_volatile_chain_build(&fixture.plan,fixture.frames,1,SOURCE,12,
        (uint8_t *)(uintptr_t)UINT32_C(0xac200000),512,OUTPUT+32u)==KUI_VMU_VOLATILE_CHAIN_SPAN);
    assert(kui_vmu_volatile_chain_build(
        (struct kui_vmu_volatile_chain_plan *)(uintptr_t)UINT32_C(0x8cfd0000),
        (const struct kui_vmu_volatile_chain_frame *)(uintptr_t)UINT32_C(0xacfd0000),
        1,SOURCE,12,fixture.output,512,OUTPUT)==KUI_VMU_VOLATILE_CHAIN_OVERLAP);
    assert(kui_vmu_volatile_chain_build(&fixture.plan,
        (const struct kui_vmu_volatile_chain_frame *)(uintptr_t)UINT32_C(0x8cfd0000),
        1,SOURCE,12,(uint8_t *)(uintptr_t)UINT32_C(0xacfd0000),512,
        UINT32_C(0x0cfd0000))==KUI_VMU_VOLATILE_CHAIN_OVERLAP);
    assert(kui_vmu_volatile_chain_build(
        (struct kui_vmu_volatile_chain_plan *)(uintptr_t)UINT32_C(0x8c100000),
        fixture.frames,1,SOURCE,12,fixture.output,512,OUTPUT)==KUI_VMU_VOLATILE_CHAIN_OVERLAP);
    assert(kui_vmu_volatile_chain_build(&fixture.plan,
        (const struct kui_vmu_volatile_chain_frame *)(uintptr_t)(UINTPTR_MAX-3u),
        1,SOURCE,12,fixture.output,512,OUTPUT)==KUI_VMU_VOLATILE_CHAIN_ARGUMENT);
}

static void maximum_batch_and_padding(void) {
    reset();
    for(unsigned i=0; i<32u; ++i) append(9,((i%4u)<<6)|32u,1,UINT32_C(0x01000000),i==31u);
    assert(build()==0 && fixture.plan.output_bytes==512 && fixture.plan.physical_count==32);
    assert(kui_vmu_volatile_chain_verify(&fixture.plan,fixture.output,512)==0);
    reset();padding(255);append(1,0x20,0,0,1);
    assert(build()==0 && fixture.plan.padding_count==255);
    assert(kui_vmu_volatile_chain_verify(&fixture.plan,fixture.output,512)==0);
}

static void verify_rejects_mutation_even_with_recomputed_crc(void) {
    reset();append(1,0x20,0,0,0);append(9,0x60,1,UINT32_C(0x01000000),1);
    assert(build()==0);
    uint8_t original[512];memcpy(original,fixture.output,512);
    struct kui_vmu_volatile_chain_plan plan=fixture.plan;
    for(unsigned byte=0; byte<fixture.plan.output_bytes; ++byte) {
        fixture.output[byte]^=1u;
        assert(kui_vmu_volatile_chain_verify(&fixture.plan,fixture.output,512)==KUI_VMU_VOLATILE_CHAIN_CHANGED);
        fixture.output[byte]^=1u;
    }
    uint32_t changes[][2]={{0,LAST},{8,UINT32_C(0x0000010c)},
        {12,UINT32_C(0x80010002)},{24,UINT32_C(0x02000000)},{16,RECEIVE}};
    for(size_t i=0; i<sizeof(changes)/sizeof(changes[0]); ++i) {
        memcpy(fixture.output,original,512);fixture.plan=plan;
        store(fixture.output+changes[i][0],changes[i][1]);
        fixture.plan.crc32=expected_crc(fixture.output,fixture.plan.output_bytes);
        assert(kui_vmu_volatile_chain_verify(&fixture.plan,fixture.output,512)!=0);
    }
    memcpy(fixture.output,original,512);fixture.plan=plan;
    assert(kui_vmu_volatile_chain_verify(&fixture.plan,fixture.output,508)==KUI_VMU_VOLATILE_CHAIN_CAPACITY);
    fixture.plan.output_bytes=UINT32_MAX;
    assert(kui_vmu_volatile_chain_verify(&fixture.plan,fixture.output,512)!=0);
    fixture.plan=plan;++fixture.plan.physical_count;
    assert(kui_vmu_volatile_chain_verify(&fixture.plan,fixture.output,512)!=0);
    fixture.plan=plan;fixture.plan.source_bytes=12;
    assert(kui_vmu_volatile_chain_verify(&fixture.plan,fixture.output,512)!=0);
}

int main(void) {
    assert(expected_crc((const uint8_t *)"123456789",9)==UINT32_C(0xcbf43926));
    mixed_byte_exact();every_virtual_command_is_filtered();strict_physical_whitelist();
    invalid_descriptors_are_atomic();full_rx_and_output_geometry();aliases_and_arguments();
    maximum_batch_and_padding();verify_rejects_mutation_even_with_recomputed_crc();
    puts("volatile Maple chain tests passed");
    return 0;
}
