/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/toy_pilot_driver_load.h"
#include <assert.h>
#include <stdio.h>

/* These numerical SH addresses are deliberately not mapped on the host.
 * Preparation must not read the empty allocation. The successful loader
 * boundary creates a one-use ticket for the worker's subsequent SHA check. */
static const uint32_t image = UINT32_C(0x8c200000);
static void prepare(struct kui_toy_pilot_driver_load *pending) {
    assert(kui_toy_pilot_driver_load_prepare(pending,image,20740u,3u,7u));
}

int main(void) {
    struct kui_toy_pilot_driver_load pending={0},ticket={0};
    prepare(&pending);
    assert(pending.image==image && pending.bytes==20740u);
    assert(kui_toy_pilot_driver_load_take(&pending,1u,image,3u,7u,&ticket));
    assert(!pending.image && ticket.bytes==20740u);
    assert(kui_toy_pilot_driver_load_current(&ticket,3u,7u));

    /* A valid file load cannot be replayed to admit another installation. */
    assert(!kui_toy_pilot_driver_load_take(&pending,1u,image,3u,7u,&ticket));
    assert(!ticket.image);

    /* Failure and a different destination consume, rather than retain, the
     * pending initialization. Neither permits hashing or driver admission. */
    const uint32_t failures[]={0u,2u,UINT32_MAX};
    for(unsigned i=0;i<sizeof(failures)/sizeof(*failures);i++) {
        prepare(&pending);
        assert(!kui_toy_pilot_driver_load_take(&pending,failures[i],image,3u,7u,&ticket));
        assert(!pending.image && !ticket.image);
    }
    prepare(&pending);
    assert(!kui_toy_pilot_driver_load_take(&pending,1u,image+32u,3u,7u,&ticket));
    assert(!pending.image && !ticket.image);

    /* A new amInit or lifecycle revocation invalidates an older completion. */
    prepare(&pending);
    assert(!kui_toy_pilot_driver_load_take(&pending,1u,image,4u,7u,&ticket));
    prepare(&pending);
    assert(!kui_toy_pilot_driver_load_take(&pending,1u,image,3u,8u,&ticket));
    prepare(&pending);
    kui_toy_pilot_driver_load_clear(&pending);
    assert(!kui_toy_pilot_driver_load_take(&pending,1u,image,3u,7u,&ticket));

    /* The worker must recheck this ticket after SHA, so interruption while
     * hashing cannot re-enable a revoked or replaced driver generation. */
    prepare(&pending);
    assert(kui_toy_pilot_driver_load_take(&pending,1u,image,3u,7u,&ticket));
    assert(!kui_toy_pilot_driver_load_current(&ticket,4u,7u));
    assert(!kui_toy_pilot_driver_load_current(&ticket,3u,8u));
    assert(!kui_toy_pilot_driver_load_current(&ticket,3u,UINT32_C(0x7fffffff)));

    /* File identity excludes the SDK's rounded 20768-byte install span. */
    assert(!kui_toy_pilot_driver_load_prepare(&pending,image,20768u,3u,7u));
    assert(!pending.image);
    assert(!kui_toy_pilot_driver_load_prepare(&pending,UINT32_C(0x8c00ffff),20740u,3u,7u));
    assert(!kui_toy_pilot_driver_load_prepare(&pending,UINT32_C(0x8cfd0000)-20739u,20740u,3u,7u));
    assert(kui_toy_pilot_driver_load_prepare(&pending,UINT32_C(0x8cfd0000)-20740u,20740u,3u,7u));
    assert(!kui_toy_pilot_driver_load_prepare(&pending,image,20740u,0u,7u));
    assert(!kui_toy_pilot_driver_load_prepare(&pending,image,20740u,3u,0u));
    assert(!kui_toy_pilot_driver_load_prepare(&pending,image,20740u,3u,UINT32_C(0x7fffffff)));
    puts("Toy driver load admission: empty buffer, success/failure, one-use, range and revocation checks passed");
    return 0;
}
