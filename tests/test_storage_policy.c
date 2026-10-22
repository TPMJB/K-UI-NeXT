/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/storage_policy.h"
#include <assert.h>
#include <string.h>

static unsigned available,calls[3];
static bool probe(void *ctx,unsigned transport) {
    (void)ctx;assert(transport<3);++calls[transport];return (available&(1u<<transport))!=0;
}
int main(void) {
    struct kui_storage_probe_ops ops={NULL,probe};unsigned chosen=KUI_STORAGE_AUTO;
    available=1u<<KUI_STORAGE_SCI;
    assert(kui_storage_discover(&chosen,&ops) && chosen==KUI_STORAGE_SCI);
    assert(calls[0]==1 && calls[1]==1 && calls[2]==0);
    available=1u<<KUI_STORAGE_IDE;
    assert(!kui_storage_discover(&chosen,&ops));
    assert(chosen==KUI_STORAGE_SCI && calls[0]==1 && calls[1]==2 && calls[2]==0);
    chosen=KUI_STORAGE_AUTO;
    assert(kui_storage_discover(&chosen,&ops) && chosen==KUI_STORAGE_IDE);
    chosen=KUI_STORAGE_AUTO;available=0;
    assert(!kui_storage_discover(&chosen,&ops) && chosen==KUI_STORAGE_AUTO);
    struct kui_storage_boot_marker marker=KUI_STORAGE_BOOT_INITIALIZER;
    assert(kui_storage_boot_transport(&marker)==KUI_STORAGE_AUTO);
    assert(kui_storage_patch_boot(&marker,sizeof(marker),KUI_STORAGE_SCI));
    assert(kui_storage_boot_transport(&marker)==KUI_STORAGE_SCI);
    marker.inverse^=1;assert(kui_storage_boot_transport(&marker)==KUI_STORAGE_AUTO);
    struct kui_storage_boot_marker pair[2]={KUI_STORAGE_BOOT_INITIALIZER,KUI_STORAGE_BOOT_INITIALIZER};
    struct kui_storage_boot_marker before[2];memcpy(before,pair,sizeof(pair));
    assert(!kui_storage_patch_boot(pair,sizeof(pair),KUI_STORAGE_IDE));
    assert(!memcmp(before,pair,sizeof(pair)));
    pair[1].version=2;
    assert(kui_storage_patch_boot(pair,sizeof(pair),KUI_STORAGE_IDE));
    assert(kui_storage_boot_transport(&pair[0])==KUI_STORAGE_IDE);
    assert(pair[1].version==2 && pair[1].transport==KUI_STORAGE_AUTO);
    assert(!kui_storage_patch_boot(&pair[1],sizeof(pair[1]),KUI_STORAGE_IDE));
    memset(pair,0,sizeof(pair));assert(!kui_storage_patch_boot(pair,sizeof(pair),KUI_STORAGE_IDE));
    assert(!kui_storage_patch_boot(pair,19,KUI_STORAGE_IDE));
    return 0;
}
