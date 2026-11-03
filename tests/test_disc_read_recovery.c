/* SPDX-License-Identifier: GPL-3.0-only */
/* The actual launcher and its private bounded reader execute in this TU.
 * The existing authored optical fixture supplies released raw/cooked API
 * outcomes; firmware status, PIO and timing are tested by test_disc.c. */
#define main launcher_fixture_main
#include "test_disc_launcher.c"
#undef main
#include "../src/apps/disc_launcher.c"

static void run_cut(unsigned mode) {
    setup();
    struct kui_app_status status={0};
    struct optical_source source={.log=log_line,.cancel=cancelled,
        .progress=progress,.status=&status,.phase="direct bounded reader"};
    uint8_t guarded[32u+8u*2048u+32u];memset(guarded,0xa5,sizeof(guarded));
    enum kui_disc_boot_io_result expected=KUI_DISC_BOOT_IO_ERROR;
    if(mode==0u)fixture.read_fatal=3u;
    if(mode==1u)fixture.cancel_at=3u;
    if(mode==2u) {
        fixture.persistent_edc_fad=FIRST+1u;
        fixture.persistent_retry_fad=FIRST+2u;
        fixture.cooked_fatal_at=2u;
    }
    if(mode==3u) {
        fixture.persistent_edc_fad=FIRST+1u;
        fixture.persistent_address_fad=FIRST+2u;
    }
    if(mode==4u) {
        fixture.unexpected_at=3u;fixture.unexpected_result=KUI_READ_PARTIAL;
    }
    if(mode==5u) {
        fixture.persistent_edc_fad=FIRST+1u;
        fixture.persistent_retry_fad=FIRST+2u;
        fixture.cooked_cancel_at=2u;
    }
    if(mode==6u) {
        fixture.persistent_edc_fad=FIRST+1u;
        fixture.persistent_retry_fad=FIRST+2u;
        fixture.cooked_retry_always=true;
    }
    if(mode==1u || mode==5u)expected=KUI_DISC_BOOT_IO_CANCELLED;
    CHECK(read_run(&source,FIRST,8u,guarded+32u)==expected);
    for(size_t i=0u;i<sizeof(guarded);i++)CHECK(guarded[i]==0xa5u);
    CHECK(!source.reads && !status.done);
    if(mode==2u)CHECK(fixture.cooked_reads==2u);
    if(mode==3u)CHECK(fixture.cooked_reads==1u && fixture.last_cooked_fad==FIRST+1u);
    if(mode==6u)CHECK(fixture.cooked_reads==2u);
    free(source.raw);
}
int main(void) {
    for(unsigned mode=0u;mode<7u;mode++)run_cut(mode);
    setup();
    struct kui_app_status status={0};
    struct optical_source source={.log=log_line,.cancel=cancelled,
        .progress=progress,.status=&status,.phase="direct bounded reader"};
    uint8_t guarded[32u+8u*2048u+32u];memset(guarded,0x5a,sizeof(guarded));
    fixture.persistent_edc_fad=FIRST+1u;fixture.persistent_retry_fad=FIRST+2u;
    CHECK(read_run(&source,FIRST,8u,guarded+32u)==KUI_DISC_BOOT_IO_OK);
    for(unsigned i=0u;i<32u;i++)CHECK(guarded[i]==0x5au && guarded[sizeof(guarded)-1u-i]==0x5au);
    CHECK(!memcmp(guarded+32u,fixture.data,8u*2048u));
    CHECK(source.cooked_reads && source.reads==8u && status.done==8u*2048u);
    CHECK(fixture.cooked_reads==7u);free(source.raw);
    printf("Physical disc bounded read publication: %u checks; actual read_run preserves complete caller canaries on fatal/Stop/partial/wrong-address/cooked failure and publishes only complete recovered runs\n",checks);
    return 0;
}
