/* SPDX-License-Identifier: GPL-3.0-only */
/* Synthetic header/entry CRC fixtures and public metadata scalars only.
 * No owner executable bytes are required or compiled into this test. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "kui/armada_ce_maple_reservation.h"
#if (defined(KUI_WINCE_CDDA_WORMS) && KUI_WINCE_CDDA_WORMS) || \
    (defined(KUI_WINCE_CDDA_WWP) && KUI_WINCE_CDDA_WWP) || \
    (defined(KUI_WINCE_CDDA_BAM4) && KUI_WINCE_CDDA_BAM4)
#error "Maple reservation fixture is exact Armada only"
#endif
#if KUI_ARMADA_CE_MAPLE_A2_STAGE
#if KUI_ARMADA_CE_MAPLE_WRITE_LAYOUT
#define EXPECTED_RESERVE_BEGIN UINT32_C(0x8cfc4000)
#else
#define EXPECTED_RESERVE_BEGIN UINT32_C(0x8cfca000)
#endif
#elif KUI_ARMADA_CE_MAPLE_FENCE_STAGE
#define EXPECTED_RESERVE_BEGIN UINT32_C(0x8cfcd000)
#elif KUI_ARMADA_CE_MAPLE_TRAFFIC_STAGE
#define EXPECTED_RESERVE_BEGIN UINT32_C(0x8cfce000)
#else
#define EXPECTED_RESERVE_BEGIN UINT32_C(0x8cfcf000)
#endif
static unsigned checks;
#define CHECK(x) do { ++checks;assert(x); } while(0)
static uint8_t header[84],record[24],entry[32],padding[92];
static const uint32_t synthetic_header[21]={
    0x7a55300bu,0x0ee9c49fu,0xa27d5833u,0x3611ecc7u,0xcaa5805bu,
    0x8c142000u,0x8c150000u,0x8cfd0000u,0x1af5d0abu,0xae89643fu,
    0x421df8d3u,0xd6b18c67u,0x6a4520fbu,0xfed9b48fu,0x926d4823u,
    0x8cfd0000u,0x00030000u,0x4e2904dfu,0xe2bd9873u,0x76512c07u,0xbd142631u
};
static const uint32_t text_metadata[6]={
    0x57a2u,0x1000u,0x5800u,0x8c0af000u,0x03dd1000u,0x60000020u
};
static const uint32_t synthetic_entry[8]={
    0x7a55300bu,0x0ee9c49fu,0xa27d5833u,0x3611ecc7u,
    0xcaa5805bu,0x5e3914efu,0xf2cda883u,0x63397ec1u
};
static void encode(uint8_t *out,const uint32_t *words,unsigned count) {
    for(unsigned n=0;n<count;n++) for(unsigned b=0;b<4u;b++)
        out[n*4u+b]=(uint8_t)(words[n]>>(b*8u));
}
static struct kui_maple_reservation_inputs input(void) {
    encode(header,synthetic_header,21u);encode(record,text_metadata,6u);
    encode(entry,synthetic_entry,8u);memset(padding,0,sizeof(padding));
    return (struct kui_maple_reservation_inputs){header,record,entry,padding,
        sizeof(header),sizeof(record),sizeof(entry),sizeof(padding),
        0x8ce31624u,0x8ce35000u,true,true};
}
static void rejected(const struct kui_maple_reservation_inputs *in) {
    struct kui_maple_reservation_plan out,before;
    memset(&out,0xa5,sizeof(out));before=out;
    CHECK(!kui_maple_reservation_prepare(in,&out));
    CHECK(!memcmp(&out,&before,sizeof(out)));
}
static void expected(const struct kui_maple_reservation_plan *p) {
    CHECK(p->ram_end_word==0x8c14143cu && p->ram_end==EXPECTED_RESERVE_BEGIN);
    CHECK(p->vsize_word==0x8c099bd4u && p->vsize==0x5800u);
    CHECK(p->bridge==0x8c0b47a4u);
#if KUI_ARMADA_CE_MAPLE_FENCE_STAGE
    CHECK(p->entry==0x8c0b3db0u && p->entry_word==0xb4f70009u);
#else
    CHECK(p->entry==0x8c0b3b0cu && p->entry_word==0x0009a64au);
#endif
}
static void success_and_immutable(void) {
    struct kui_maple_reservation_inputs in=input(),before_in=in;
    uint8_t before_header[84],before_record[24],before_entry[32],before_padding[92];
    memcpy(before_header,header,sizeof(header));memcpy(before_record,record,sizeof(record));
    memcpy(before_entry,entry,sizeof(entry));memcpy(before_padding,padding,sizeof(padding));
    CHECK(kui_armada_ce_excerpt_crc32(header,sizeof(header))==0x7ddd77a1u);
    CHECK(kui_armada_ce_excerpt_crc32(record,sizeof(record))==0x94183a25u);
    CHECK(kui_armada_ce_excerpt_crc32(entry,sizeof(entry))==0x7bd7ff55u);
    struct kui_maple_reservation_plan out;
    CHECK(kui_maple_reservation_prepare(&in,&out));expected(&out);
    CHECK(!memcmp(&in,&before_in,sizeof(in)));
    CHECK(!memcmp(header,before_header,sizeof(header)));
    CHECK(!memcmp(record,before_record,sizeof(record)));
    CHECK(!memcmp(entry,before_entry,sizeof(entry)));
    CHECK(!memcmp(padding,before_padding,sizeof(padding)));
    in.stage_end=0x8ce10000u;in.bss_end=in.stage_end;
    CHECK(kui_maple_reservation_prepare(&in,&out));expected(&out);
    in.stage_end=EXPECTED_RESERVE_BEGIN-1u;in.bss_end=in.stage_end;
    CHECK(kui_maple_reservation_prepare(&in,&out));expected(&out);
}
static void changed_bytes(void) {
    for(unsigned group=0;group<4u;group++) {
        struct kui_maple_reservation_inputs in=input();
        uint8_t *bytes[]={header,record,entry,padding};
        const size_t sizes[]={sizeof(header),sizeof(record),sizeof(entry),sizeof(padding)};
        for(size_t i=0;i<sizes[group];i++) for(unsigned bit=0;bit<8u;bit++) {
            bytes[group][i]^=(uint8_t)(1u<<bit);rejected(&in);
            bytes[group][i]^=(uint8_t)(1u<<bit);
        }
    }
}
static void invalid_arguments(void) {
    struct kui_maple_reservation_inputs in=input();
    rejected(NULL);CHECK(!kui_maple_reservation_prepare(&in,NULL));
    in.source_admitted=false;rejected(&in);in=input();in.gd_alias_guard=false;rejected(&in);
    for(unsigned i=0;i<4u;i++) {
        in=input();
        switch(i) {case 0:in.rom_header=NULL;break;case 1:in.text_record=NULL;break;
            case 2:in.entry=NULL;break;default:in.padding=NULL;break;}
        rejected(&in);
    }
    for(unsigned i=0;i<4u;i++) {
        static const size_t valid[]={84u,24u,32u,92u};
        const size_t invalid[]={0u,valid[i]-1u,valid[i]+1u,SIZE_MAX};
        for(unsigned n=0;n<4u;n++) {
            in=input();
            switch(i) {case 0:in.header_bytes=invalid[n];break;case 1:in.record_bytes=invalid[n];break;
                case 2:in.entry_bytes=invalid[n];break;default:in.padding_bytes=invalid[n];break;}
            rejected(&in);
        }
    }
    static const uint32_t stages[][2]={
        {0,0},{0x8ce0ffffu,0x8ce10000u},{0x8ce10000u,0x8ce0ffffu},
        {0x8ce31624u,0x8ce31623u},{EXPECTED_RESERVE_BEGIN,EXPECTED_RESERVE_BEGIN},
        {0x8ce10000u,EXPECTED_RESERVE_BEGIN},{0x8ce10000u,0x8cfd0000u},
        {0xace10000u,0xace20000u},{0x0ce10000u,0x0ce20000u},
        {UINT32_MAX,UINT32_MAX},{0x8ce10000u,UINT32_MAX}
    };
    for(unsigned i=0;i<sizeof(stages)/sizeof(*stages);i++) {
        in=input();in.stage_end=stages[i][0];in.bss_end=stages[i][1];rejected(&in);
    }
}
static bool guard_reference(uint32_t address,uint32_t bytes) {
    uint32_t segment=address&0xe0000000u;
    uint64_t start=address&0x1fffffffu,end=start+(uint64_t)bytes;
    return (segment==0 || segment==0x80000000u || segment==0xa0000000u) &&
        bytes && start>=0x0c000000u && end<=0x0d000000u &&
        (end<=(EXPECTED_RESERVE_BEGIN&0x1fffffffu) || start>=0x0cfd0000u);
}
static void destination_guards(void) {
    static const uint32_t segments[]={0,0x20000000u,0x40000000u,0x60000000u,
        0x80000000u,0xa0000000u,0xc0000000u,0xe0000000u};
    static const uint32_t centers[]={0x0c000000u,(EXPECTED_RESERVE_BEGIN&0x1fffffffu),0x0cfd0000u,0x0d000000u};
    for(unsigned s=0;s<8u;s++) for(unsigned c=0;c<4u;c++)
        for(int delta=-16;delta<=16;delta++) for(uint32_t bytes=0;bytes<=34u;bytes++) {
            uint32_t address=(centers[c]+(uint32_t)delta)|segments[s];
            CHECK(kui_maple_reservation_destination_safe(address,bytes)==guard_reference(address,bytes));
        }
    static const uint32_t huge[]={0x00ffffffu,0x01000000u,0x10000000u,0x7fffffffu,0xffffffffu};
    for(unsigned s=0;s<8u;s++) for(unsigned c=0;c<4u;c++) for(unsigned n=0;n<5u;n++) {
        uint32_t address=centers[c]|segments[s];
        CHECK(kui_maple_reservation_destination_safe(address,huge[n])==guard_reference(address,huge[n]));
    }
    CHECK(kui_maple_reservation_destination_safe(EXPECTED_RESERVE_BEGIN-4u,4u));
    CHECK(!kui_maple_reservation_destination_safe(EXPECTED_RESERVE_BEGIN-4u,5u));
    CHECK(!kui_maple_reservation_destination_safe(0xacfcffffu,1u));
    CHECK(kui_maple_reservation_destination_safe(0x0cfd0000u,1u));
    CHECK(kui_maple_reservation_destination_safe(0x8cffffffu,1u));
    CHECK(!kui_maple_reservation_destination_safe(0x8cffffffu,2u));
}
int main(void) {
    success_and_immutable();changed_bytes();invalid_arguments();destination_guards();
    printf("CE Maple reservation: %u checks; synthetic CRC fixtures, immutable refusals, all byte mutations and physical/P1/P2/unknown destination boundaries passed\n",checks);
    return 0;
}
