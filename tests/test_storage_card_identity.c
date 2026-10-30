/* SPDX-License-Identifier: GPL-3.0-only */
/* Reuse the real adapter's hardware stubs without running its separate suite. */
#define main storage_error_baseline_main
#include "test_storage_errors.c"
#undef main
#include "kui/vmu_backing.h"

static unsigned identity_queries;
static int identity_result;
static bool identity_bus_failure;
static struct kui_vmu_sd_identity expected_identity={
    .cid={0x31,0x53,0x44,0x53,0x44,0x31,0x32,0x38,0x81,0x19,0x73,0x42,0x67,0x06,0x91,0x8b},
    .sectors=10000,.high_capacity=true
};
int kui_vmu_sd_identity_read(struct kui_loader_sd *card,struct kui_vmu_sd_identity *out) {
    assert(card && card->ready && !card->slow && out);
    ++identity_queries;card->last_command=10;card->last_response=0;
    *out=expected_identity;
    if(identity_bus_failure)healthy=false;
    return identity_result;
}
static void seed_output(struct kui_vmu_sd_identity *out) {memset(out,0xa5,sizeof(*out));}
static void assert_empty(const struct kui_vmu_sd_identity *out) {
    for(unsigned i=0;i<sizeof(out->cid);++i)assert(!out->cid[i]);
    assert(!out->sectors && !out->high_capacity);
}
static void assert_identity(const struct kui_vmu_sd_identity *out) {
    assert(!memcmp(out->cid,expected_identity.cid,sizeof(out->cid)));
    assert(out->sectors==expected_identity.sectors && out->high_capacity==expected_identity.high_capacity);
}
static void connect_sci(void) {
    kui_sd_disconnect();healthy=true;disc_busy=false;
    init_result=KUI_LOADER_SD_OK;identity_result=0;identity_bus_failure=false;
    kui_sd_set_params(KUI_STORAGE_SCI,true);assert(kui_sd_connect());
    kui_storage_errors_reset();clear_log();
}
int main(void) {
    struct kui_vmu_sd_identity out;
    seed_output(&out);
    assert(!kui_sd_card_identity(&out));assert_empty(&out);
    assert(!identity_queries && !snapshot().total);

    connect_sci();
    assert(!kui_sd_card_identity(NULL) && !identity_queries);
    assert(kui_sd_card_identity(&out));assert_identity(&out);
    assert(identity_queries==1 && snapshot().total==0);
    assert(kui_sd_vmu_profile_identity(&out));assert_identity(&out);
    assert(identity_queries==2 && snapshot().total==0);

    disc_busy=true;seed_output(&out);
    assert(!kui_sd_card_identity(&out));assert_empty(&out);
    assert(identity_queries==2 && snapshot().total==0);
    disc_busy=false;healthy=false;seed_output(&out);
    assert(!kui_sd_card_identity(&out));assert_empty(&out);
    assert(identity_queries==2 && snapshot().total==0);

    connect_sci();identity_result=KUI_VMU_BACKING_IO;seed_output(&out);
    assert(!kui_sd_card_identity(&out));assert_empty(&out);
    assert(identity_queries==3);
    struct kui_storage_errors errors=snapshot();
    assert(errors.total==1 && errors.read_errors==1 && errors.rejected_errors==1);
    assert(errors.last_transport==KUI_STORAGE_SCI && errors.sd_detail_valid && errors.sd_command==10);
    /* The failed query poisons readiness: no retry occurs in either entry point. */
    seed_output(&out);assert(!kui_sd_card_identity(&out));assert_empty(&out);
    seed_output(&out);assert(!kui_sd_vmu_profile_identity(&out));assert_empty(&out);
    assert(identity_queries==3 && snapshot().total==1);

    connect_sci();identity_bus_failure=true;seed_output(&out);
    assert(!kui_sd_card_identity(&out));assert_empty(&out);
    assert(identity_queries==4 && snapshot().total==1);
    healthy=true;seed_output(&out);
    assert(!kui_sd_card_identity(&out));assert_empty(&out);
    assert(identity_queries==4 && snapshot().total==1);

    kui_sd_disconnect();healthy=true;scif_result=0;
    kui_sd_set_params(KUI_STORAGE_SCIF,true);assert(kui_sd_connect());
    kui_storage_errors_reset();seed_output(&out);
    assert(!kui_sd_card_identity(&out));assert_empty(&out);
    assert(identity_queries==4 && snapshot().total==0);
    kui_sd_disconnect();seed_output(&out);
    assert(!kui_sd_card_identity(&out));assert_empty(&out);
    assert(identity_queries==4 && snapshot().total==0);
    puts("PASS storage card identity: SCI/VMU alias, unavailable guards, failed-query poison, no retries");
    return 0;
}
