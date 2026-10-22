/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/armada_ce_cache_word.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Independently authored read-only cache-array model. Array address and
 * uncached RAM admission are asserted before simulated data are consulted. */
struct cache_fixture {
    uint32_t va,p1,page,ccr,tags[4],cached,ram;
    uint32_t addresses[10],change_mask;
    unsigned reads,fail_at,change_at;
};
static bool sample(void *context,uint32_t address,uint32_t *value) {
    struct cache_fixture *f=context;
    const uint32_t index=f->p1&0xfe0u;
    bool admitted=false;
    assert(!(address&3u) && f->reads<10u);
    for(unsigned color=0;color<4u;color++) {
        if(address==(0xf4000000u|index|(color<<12))) {
            *value=f->tags[color];admitted=true;
        }
    }
    if(address==(0xf5000000u|index|(((f->va>>12)&3u)<<12)|(f->p1&0x1cu))) {
        *value=f->cached;admitted=true;
    }
    if(address==(f->p1|0x20000000u)) {
        uint32_t canonical;
        assert(kui_armada_ce_ram_p1(address,&canonical) && canonical==f->p1);
        *value=f->ram;admitted=true;
    }
    assert(admitted);
    f->addresses[f->reads++]=address;
    if(f->reads==f->fail_at) return false;
    if(f->reads==f->change_at) *value^=f->change_mask;
    return true;
}
static struct cache_fixture fixture(unsigned color,unsigned offset) {
    assert(color<4u && offset<8u);
    struct cache_fixture f={0};
    const uint32_t low=0x460u+offset*4u;
    f.va=0x03cd8000u|(color<<12)|low;
    f.p1=0x8c320000u|(((color+1u)&3u)<<12)|low;
    f.page=(f.p1&0x1ffff000u)|0x79u;f.ccr=0x105u;
    f.cached=0x76543210u^color^offset;f.ram=0x01234567u^color^offset;
    /* Reserved bits have no semantic meaning, and must not make the
     * physical tag compare or stability witness spuriously fail. */
    for(unsigned i=0;i<4u;i++) f.tags[i]=0x600003fcu;
    return f;
}
static enum kui_armada_ce_ram_bind_status run(struct cache_fixture *f,uint32_t *out) {
    return kui_armada_ce_cache_word(f->va,f->p1,f->page,f->ccr,sample,f,out);
}
static void expect(struct cache_fixture *f,enum kui_armada_ce_ram_bind_status status,
                   uint32_t expected,unsigned reads) {
    uint32_t out=0xa5a5a5a5u;
    assert(run(f,&out)==status && out==expected && f->reads==reads);
}
int main(void) {
    unsigned cases=0;
    /* All four VA colors and all eight word offsets in a 32-byte line
     * recover both clean and dirty target lines, ignoring stale main RAM. */
    for(unsigned color=0;color<4u;color++) for(unsigned offset=0;offset<8u;offset++) {
        for(unsigned dirty=0;dirty<2u;dirty++) {
            struct cache_fixture f=fixture(color,offset),before;
            f.tags[color]|=(f.p1&0x1ffffc00u)|1u|(dirty?2u:0u);before=f;
            expect(&f,KUI_ARMADA_CE_RAM_BIND_OK,f.cached,10u);
            assert(!memcmp(f.tags,before.tags,sizeof(f.tags)) && f.cached==before.cached && f.ram==before.ram);
            assert(f.addresses[4]==(0xf5000000u|(f.p1&0xfe0u)|(color<<12)|(f.p1&0x1cu)));
            ++cases;
        }
        /* Invalid dirty bits do not create a valid alias; a clean matching
         * alternate alias cannot override a target miss's main RAM value. */
        for(unsigned alias=0;alias<4u;alias++) {
            struct cache_fixture f=fixture(color,offset);
            f.tags[alias]|=(f.p1&0x1ffffc00u)|(alias==color?2u:1u);
            expect(&f,KUI_ARMADA_CE_RAM_BIND_OK,f.ram,10u);
            assert(f.addresses[4]==(f.p1|0x20000000u));++cases;
        }
        /* A dirty alternate alias cannot prove what the native VA sees. */
        for(unsigned alias=0;alias<4u;alias++) if(alias!=color) {
            struct cache_fixture f=fixture(color,offset);
            f.tags[color]|=(f.p1&0x1ffffc00u)|3u;
            f.tags[alias]|=(f.p1&0x1ffffc00u)|3u;
            expect(&f,KUI_ARMADA_CE_RAM_BIND_CACHE,0,alias+1u);++cases;
        }
        /* Uncached pages always use P2 when matching valid lines are clean,
         * and refuse every dirty matching alias, including the VA color. */
        for(unsigned alias=0;alias<4u;alias++) {
            struct cache_fixture f=fixture(color,offset);f.page&=~8u;
            f.tags[alias]|=(f.p1&0x1ffffc00u)|1u;
            expect(&f,KUI_ARMADA_CE_RAM_BIND_OK,f.ram,10u);
            assert(f.addresses[4]==(f.p1|0x20000000u));++cases;
            f=fixture(color,offset);f.page&=~8u;
            f.tags[alias]|=(f.p1&0x1ffffc00u)|3u;
            expect(&f,KUI_ARMADA_CE_RAM_BIND_CACHE,0,alias+1u);++cases;
        }
        /* A dirty line for a different physical tag is harmless. */
        for(unsigned alias=0;alias<4u;alias++) {
            struct cache_fixture f=fixture(color,offset);
            f.tags[alias]|=((f.p1&0x1ffffc00u)^0x4000u)|3u;
            expect(&f,KUI_ARMADA_CE_RAM_BIND_OK,f.ram,10u);++cases;
        }
        /* Every read in both successful paths can fail independently. */
        for(unsigned hit=0;hit<2u;hit++) for(unsigned at=1;at<=10u;at++) {
            struct cache_fixture f=fixture(color,offset);
            if(hit) f.tags[color]|=(f.p1&0x1ffffc00u)|3u;
            f.fail_at=at;expect(&f,KUI_ARMADA_CE_RAM_BIND_READ,0,at);++cases;
        }
        /* Every tag's significant V/U/address bits are rechecked, including
         * tags which never matched; reserved-bit changes are ignored. */
        const uint32_t mutations[]={1u,2u,0x400u,0x1000u,0x10000000u};
        for(unsigned hit=0;hit<2u;hit++) for(unsigned alias=0;alias<4u;alias++) {
            for(unsigned bit=0;bit<sizeof(mutations)/sizeof(*mutations);bit++) {
                struct cache_fixture f=fixture(color,offset);
                if(hit) f.tags[color]|=(f.p1&0x1ffffc00u)|3u;
                f.change_at=6u+alias;f.change_mask=mutations[bit];
                expect(&f,KUI_ARMADA_CE_RAM_BIND_CHANGED,0,6u+alias);++cases;
            }
            struct cache_fixture f=fixture(color,offset);
            if(hit) f.tags[color]|=(f.p1&0x1ffffc00u)|3u;
            f.change_at=6u+alias;f.change_mask=~0x1ffffc03u;
            expect(&f,KUI_ARMADA_CE_RAM_BIND_OK,hit?f.cached:f.ram,10u);++cases;
        }
        for(unsigned hit=0;hit<2u;hit++) {
            struct cache_fixture f=fixture(color,offset);
            if(hit) f.tags[color]|=(f.p1&0x1ffffc00u)|3u;
            f.change_at=10u;f.change_mask=1u;
            expect(&f,KUI_ARMADA_CE_RAM_BIND_CHANGED,0,10u);++cases;
        }
    }
    const uint32_t invalid_va[]={1u,0x80000000u,0xa3cd846cu,0xffffffffu};
    for(unsigned i=0;i<sizeof(invalid_va)/sizeof(*invalid_va);i++) {
        struct cache_fixture f=fixture(0,3);f.va=invalid_va[i];
        expect(&f,KUI_ARMADA_CE_RAM_BIND_ARGUMENT,0,0);++cases;
    }
    const uint32_t invalid_p1[]={0u,0x8c00046cu,0x8d00046cu,0xac32146cu,0xa080046cu,
        0xf400046cu,0xf500046cu,0x8c32146du,0x8c321468u};
    for(unsigned i=0;i<sizeof(invalid_p1)/sizeof(*invalid_p1);i++) {
        struct cache_fixture f=fixture(0,3);f.p1=invalid_p1[i];
        expect(&f,KUI_ARMADA_CE_RAM_BIND_ARGUMENT,0,0);++cases;
    }
    const uint32_t invalid_page[]={0x0c321078u,0x0c322079u,0x00801079u,0x0c001079u};
    for(unsigned i=0;i<sizeof(invalid_page)/sizeof(*invalid_page);i++) {
        struct cache_fixture f=fixture(0,3);f.page=invalid_page[i];
        expect(&f,KUI_ARMADA_CE_RAM_BIND_ARGUMENT,0,0);++cases;
    }
    const uint32_t unsupported_ccr[]={0u,0x104u,0x125u,0x185u,0x1a5u};
    for(unsigned i=0;i<sizeof(unsupported_ccr)/sizeof(*unsupported_ccr);i++) {
        struct cache_fixture f=fixture(0,3);f.ccr=unsupported_ccr[i];
        expect(&f,KUI_ARMADA_CE_RAM_BIND_CACHE,0,0);++cases;
    }
    struct cache_fixture f=fixture(0,3);uint32_t out=1;
    assert(kui_armada_ce_cache_word(f.va,f.p1,f.page,f.ccr,NULL,&f,&out)==KUI_ARMADA_CE_RAM_BIND_ARGUMENT && !out && !f.reads);++cases;
    assert(kui_armada_ce_cache_word(f.va,f.p1,f.page,f.ccr,sample,&f,NULL)==KUI_ARMADA_CE_RAM_BIND_ARGUMENT && !f.reads);++cases;
    printf("Armada CE cache word: %u cases passed; all 4 colors / 8 offsets; max 10 read-only callbacks, no invalid access.\n",cases);
    return 0;
}
