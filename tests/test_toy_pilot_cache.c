/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/toy_pilot_cache.h"
#include "kui/toy_pilot_scratch.h"
#include "toy_pilot_admission.h"
#include <assert.h>
#include <stdio.h>

#ifndef KUI_TOY_TEST_EXPECT_PRIVATE
#error The test runner must specify the expected private-state profile
#endif
#ifndef KUI_TOY_TEST_EXPECT_NATIVE
#error The test runner must specify the expected native-cache profile
#endif
_Static_assert(KUI_TOY_PILOT_PRIVATE_P2 == KUI_TOY_TEST_EXPECT_PRIVATE,
    "private-state profile must match the requested test variant");
_Static_assert(KUI_TOY_PILOT_NATIVE_CACHE == KUI_TOY_TEST_EXPECT_NATIVE,
    "native-cache profile must match the requested test variant");

static void canonical_addresses(void) {
    const struct { uint32_t address, cached; } cases[] = {
        {0xac000000u,0x8c000000u}, {0xac005100u,0x8c005100u},
        {0xacfd0000u,0x8cfd0000u}, {0xacffffffu,0x8cffffffu},
        {0x8c000000u,0x8c000000u}, {0x8cffffffu,0x8cffffffu},
        {0x8d000000u,0x8d000000u}, {0xabffffffu,0xabffffffu},
        {0xad000000u,0xad000000u}, {0x9cfd0000u,0x9cfd0000u},
        {0xbcfd0000u,0xbcfd0000u}, {0u,0u}, {UINT32_MAX,UINT32_MAX}
    };
    for(unsigned i=0;i<sizeof(cases)/sizeof(*cases);i++) {
        uint32_t cached=kui_toy_pilot_cached_address(cases[i].address);
        assert(cached==cases[i].cached);
        assert(kui_toy_pilot_cached_address(cached)==cached);
    }
}

static void typed_state_domains(void) {
    const uint32_t begin=0x8c005100u, end=0x8c007100u;
    const uint32_t alias=KUI_TOY_TEST_EXPECT_PRIVATE?0x20000000u:0u;
    const uint32_t other=KUI_TOY_TEST_EXPECT_PRIVATE?0u:0x20000000u;
    assert(KUI_TOY_PILOT_DATA_ALIAS==alias);
    assert(kui_toy_pilot_state_address(begin+alias,begin,end));
    assert(kui_toy_pilot_state_address(end-1u+alias,begin,end));
    assert(!kui_toy_pilot_state_address(begin-1u+alias,begin,end));
    assert(!kui_toy_pilot_state_address(end+alias,begin,end));
    assert(!kui_toy_pilot_state_address(begin+other,begin,end));
    assert(!kui_toy_pilot_state_address(0x9c005100u,begin,end));
    assert(!kui_toy_pilot_state_address(0xbc005100u,begin,end));
    assert(!kui_toy_pilot_state_address(UINT32_MAX,begin,end));
    assert(!kui_toy_pilot_state_address(0u,begin,end));

    /* This helper proves an address domain, not a typed object's alignment
     * or size. Its users retain those separate checks. */
    assert(kui_toy_pilot_state_address(begin+1u+alias,begin,end));
    assert(kui_toy_pilot_state_address(0x8cffffffu+alias,
        0x8cffffffu,0x8d000000u));
    assert(!kui_toy_pilot_state_address(0x8d000000u+alias,
        0x8cffffffu,0x8d000000u));

    /* Reservation arguments remain canonical P1; wrapping, reversed,
     * empty and other-region reservations must never widen admission. */
    const struct { uint32_t begin, end; } denied[] = {
        {begin,begin}, {end,begin}, {0x8bffffffu,end},
        {begin,0x8d000001u}, {0xac005100u,0xac007100u},
        {0x9c005100u,0x9c007100u}, {0xbc005100u,0xbc007100u},
        {0u,UINT32_MAX}, {0xffffff00u,0x100u},
        {begin,UINT32_MAX}, {UINT32_MAX,0u}
    };
    for(unsigned i=0;i<sizeof(denied)/sizeof(*denied);i++)
        assert(!kui_toy_pilot_state_address(begin+alias,
            denied[i].begin,denied[i].end));
}

static void exact_cache_admission(void) {
    const uint32_t expected=KUI_TOY_TEST_EXPECT_NATIVE?0x105u:0x101u;
    assert(KUI_TOY_PILOT_CACHE_POLICY_SELECTED==expected);
    assert(kui_toy_pilot_cache_policy(0x105u)==expected);
    const uint32_t denied[]={0u,0x101u,0x109u,1u,5u,0x100105u,
        0x80000105u,UINT32_MAX};
    for(unsigned i=0;i<sizeof(denied)/sizeof(*denied);i++)
        assert(!kui_toy_pilot_cache_policy(denied[i]));
    assert((0x105u^expected)==(KUI_TOY_TEST_EXPECT_NATIVE?0u:4u));
}

static uint32_t scratch(uint32_t function,uint32_t command,uint32_t param,
    uint32_t pr,uint32_t sp,uint32_t top) {
    return kui_toy_pilot_scratch_capability(function,command,param,pr,sp,top);
}

static void scoped_stack_capabilities(void) {
    const uint32_t top=0x8cfdc000u;
    const uint32_t physical=0x8cfdb000u;
    const uint32_t alias=KUI_TOY_TEST_EXPECT_PRIVATE?0x20000000u:0u;
    const uint32_t sp=physical+alias;
    const uint32_t check_pr=0x8c0bd374u, request_pr=0x8c0bd57eu;
    uint32_t write=scratch(KUI_GD_CHECK,129u,sp,check_pr,sp,top);
    uint32_t read=scratch(KUI_GD_REQUEST,KUI_RETAIL_GD_REQ_STAT,
        sp,request_pr,sp,top);
    assert(write==(physical|1u));
    assert(read==physical);
    assert(kui_toy_pilot_scratch_maps(write,physical,16u,1));
    assert(kui_toy_pilot_scratch_maps(read,physical,16u,0));
    assert(!kui_toy_pilot_scratch_maps(write,physical,16u,0));
    assert(!kui_toy_pilot_scratch_maps(read,physical,16u,1));
    assert(!kui_toy_pilot_scratch_maps(write,physical,16u,
        KUI_RETAIL_MAP_VALIDATE));
    assert(!kui_toy_pilot_scratch_maps(write,physical+4u,16u,1));
    assert(!kui_toy_pilot_scratch_maps(write,physical+0x20000000u,16u,1));
    assert(!kui_toy_pilot_scratch_maps(0u,physical,16u,1));
    const uint32_t wrong_sizes[]={0u,1u,4u,8u,12u,15u,17u,20u,UINT32_MAX};
    for(unsigned i=0;i<sizeof(wrong_sizes)/sizeof(*wrong_sizes);i++) {
        assert(!kui_toy_pilot_scratch_maps(write,physical,wrong_sizes[i],1));
        assert(!kui_toy_pilot_scratch_maps(read,physical,wrong_sizes[i],0));
    }

    /* Incoming param and SP must be equal before physical canonicalization.
     * Neither the opposite alias nor another in-range stack word qualifies. */
    assert(!scratch(KUI_GD_CHECK,129u,sp+4u,check_pr,sp,top));
    assert(!scratch(KUI_GD_CHECK,129u,sp,check_pr,sp+4u,top));
    assert(!scratch(KUI_GD_CHECK,129u,
        physical+(alias?0u:0x20000000u),check_pr,sp,top));
    assert(!scratch(KUI_GD_CHECK,129u,sp,check_pr,
        physical+(alias?0u:0x20000000u),top));
    uint32_t other_sp=physical+(alias?0u:0x20000000u);
    assert(!scratch(KUI_GD_CHECK,129u,other_sp,check_pr,other_sp,top));
    assert(!scratch(KUI_GD_CHECK,129u,sp+1u,check_pr,sp+1u,top));
    assert(!scratch(KUI_GD_CHECK,129u,sp+2u,check_pr,sp+2u,top));
    assert(!scratch(KUI_GD_CHECK,129u,sp,check_pr+2u,sp,top));
    assert(!scratch(KUI_GD_CHECK,129u,sp,check_pr+0x20000000u,sp,top));
    assert(!scratch(KUI_GD_REQUEST,KUI_RETAIL_GD_REQ_STAT,
        sp,request_pr+2u,sp,top));
    assert(!scratch(KUI_GD_REQUEST,KUI_RETAIL_GD_REQ_STAT,
        sp,request_pr+0x20000000u,sp,top));
    assert(!scratch(KUI_GD_CHECK,129u,sp,check_pr,sp,top+0x20000000u));

    const uint32_t wrong_commands[]={KUI_RETAIL_GD_PLAY,KUI_RETAIL_GD_PAUSE,
        KUI_RETAIL_GD_GETSCD,KUI_GD_COMMAND_INIT,0u,UINT32_MAX};
    for(unsigned i=0;i<sizeof(wrong_commands)/sizeof(*wrong_commands);i++)
        assert(!scratch(KUI_GD_REQUEST,wrong_commands[i],sp,request_pr,sp,top));
    for(uint32_t function=0u;function<=KUI_GD_DATATYPE+1u;function++) {
        if(function!=KUI_GD_CHECK)
            assert(!scratch(function,129u,sp,check_pr,sp,top));
        if(function!=KUI_GD_REQUEST)
            assert(!scratch(function,KUI_RETAIL_GD_REQ_STAT,sp,request_pr,sp,top));
    }

    /* An 8192-byte private stack excludes 64 guard bytes, 36 caller-save
     * bytes and the top 16-byte bridge anchor. The full scratch must fit. */
    const uint32_t valid_edges[]={0x8cfda064u,0x8cfdbfe0u};
    for(unsigned i=0;i<sizeof(valid_edges)/sizeof(*valid_edges);i++) {
        uint32_t edge=valid_edges[i]+alias;
        assert(scratch(KUI_GD_CHECK,129u,edge,check_pr,edge,top)==
            (valid_edges[i]|1u));
    }
    const uint32_t outside[]={0x8cfda000u,0x8cfda040u,0x8cfda060u,
        0x8cfdbfe4u,0x8cfdbff0u,0x8cfdc000u,0x8cfcfffcu,
        0x8cfe0000u,0u,0xfffffffcu};
    for(unsigned i=0;i<sizeof(outside)/sizeof(*outside);i++) {
        uint32_t address=outside[i];
        if(address>=0x8c000000u && address<0x8d000000u) address+=alias;
        assert(!scratch(KUI_GD_CHECK,129u,address,check_pr,address,top));
    }
}

int main(void) {
    canonical_addresses();
    typed_state_domains();
    exact_cache_admission();
    scoped_stack_capabilities();
    printf("Toy cache profile private-P2=%u native-cache=%u: exact aliases, policy and scoped scratch passed\n",
        (unsigned)KUI_TOY_PILOT_PRIVATE_P2,(unsigned)KUI_TOY_PILOT_NATIVE_CACHE);
    return 0;
}
