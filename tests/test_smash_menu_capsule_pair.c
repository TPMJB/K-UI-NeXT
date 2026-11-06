/* SPDX-License-Identifier: GPL-3.0-only */
/* Reuse the bounded two-file FatFs fixture, then exercise the production
 * pair publisher rather than reconstructing its plan/CRC ordering here. */
#define main capsule_file_fixture_regressions
#include "test_smash_menu_capsule_prepare.c"
#undef main
#include "../src/apps/smash_menu_capsule_pair_prepare.inc"

static uint8_t native_payload[8688],outer_payload[4480],outer_template[4480];
static struct kui_runtime_image native_module,outer_module;
static const size_t native_seed_at=256u,child_return_at=1024u,loader_seed_at=256u,outer_return_at=1024u;

static void pair_fixture(void) {
    fixture();capsules[CHILD].present=capsules[OUTER].present=false;
    memset(native_payload,0x57,sizeof(native_payload));memset(outer_payload,0x39,sizeof(outer_payload));
    const struct kui_smash_native_menu_seed native={.magic0=KUI_SMASH_NATIVE_MENU_SEED_MAGIC0,
        .magic1=KUI_SMASH_NATIVE_MENU_SEED_MAGIC1,.version=1u,.bytes=KUI_SMASH_NATIVE_MENU_SEED_BYTES};
    const struct kui_smash_menu_loader_seed loader={KUI_SMASH_MENU_LOADER_SEED_BLANK_WORDS};
    const struct kui_smash_menu_return_seed returned={.magic0=KUI_SMASH_MENU_RETURN_SEED_MAGIC0,
        .magic1=KUI_SMASH_MENU_RETURN_SEED_MAGIC1,.version=1u,.bytes=KUI_SMASH_MENU_RETURN_SEED_BYTES};
    memcpy(native_payload+native_seed_at,&native,sizeof(native));
    memcpy(native_payload+child_return_at,&returned,sizeof(returned));
    memcpy(outer_payload+loader_seed_at,&loader,sizeof(loader));
    memcpy(outer_payload+outer_return_at,&returned,sizeof(returned));
    memcpy(outer_template,outer_payload,sizeof(outer_template));
    native_module=(struct kui_runtime_image){.data=native_payload,
        .info={.payload_bytes=sizeof(native_payload),.memory_bytes=sizeof(native_payload),.build="123456abcdef"}};
    outer_module=(struct kui_runtime_image){.data=outer_payload,
        .info={.payload_bytes=sizeof(outer_payload),.memory_bytes=sizeof(outer_payload),.build="123456abcdef"}};
}
static bool publish_pair(void) {
    bool ok=smash_menu_capsule_pair_publish(&fs,&volume,&game,&profile,&native_module,&outer_module,page,
        &problem,log_message,cancel);
    assert(!active);return ok;
}
static bool initial_return_blank(void) {
    struct kui_smash_menu_return_seed returned;memcpy(&returned,outer_payload+outer_return_at,sizeof(returned));
    return kui_smash_menu_return_seed_blank(&returned);
}
static void verify_published_pair(void) {
    struct kui_smash_native_menu_seed native;
    struct kui_smash_menu_loader_seed child_plan,initial_child_plan;
    struct kui_smash_menu_return_seed stored_return,initial_return,child_return;
    memcpy(&native,capsules[CHILD].disk+native_seed_at,sizeof(native));
    memcpy(&child_return,capsules[CHILD].disk+child_return_at,sizeof(child_return));
    memcpy(&child_plan,capsules[OUTER].disk+loader_seed_at,sizeof(child_plan));
    memcpy(&initial_child_plan,outer_payload+loader_seed_at,sizeof(initial_child_plan));
    memcpy(&stored_return,capsules[OUTER].disk+outer_return_at,sizeof(stored_return));
    memcpy(&initial_return,outer_payload+outer_return_at,sizeof(initial_return));
    assert(kui_smash_native_menu_seed_blank(&native) && kui_smash_menu_return_seed_blank(&child_return));
    assert(kui_smash_menu_loader_seed_valid(&child_plan) && !memcmp(&child_plan,&initial_child_plan,sizeof(child_plan)));
    assert(child_plan.payload_bytes==sizeof(native_payload) && child_plan.padded_bytes==8704u &&
        child_plan.menu_seed_offset==native_seed_at && child_plan.first_lba==2292u && child_plan.first_blocks==17u);
    assert(kui_retail_crc32(0u,capsules[CHILD].disk,8704u)==child_plan.payload_crc32);
    assert(!memcmp(capsules[CHILD].disk,native_payload,sizeof(native_payload)));
    for(size_t at=sizeof(native_payload);at<8704u;at++)assert(!capsules[CHILD].disk[at]);
    assert(kui_smash_menu_return_seed_blank(&stored_return));
    assert(kui_smash_menu_return_seed_valid(&initial_return) && initial_return.flags==KUI_SMASH_MENU_RETURN_CONFIGURED);
    assert(initial_return.outer.payload_bytes==sizeof(outer_payload) && initial_return.outer.padded_bytes==4608u &&
        initial_return.outer.menu_seed_offset==outer_return_at && initial_return.outer.first_lba==2932u &&
        initial_return.outer.first_blocks==9u && !initial_return.outer.handoff_crc32 && !initial_return.outer.raw_crc32);
    assert(kui_retail_crc32(0u,capsules[OUTER].disk,4608u)==initial_return.outer.payload_crc32);
    for(size_t at=sizeof(outer_payload);at<4608u;at++)assert(!capsules[OUTER].disk[at]);
    for(size_t at=0u;at<sizeof(outer_payload);at++) {
        bool returned=at>=outer_return_at && at<outer_return_at+KUI_SMASH_MENU_RETURN_SEED_BYTES;
        if(!returned)assert(outer_payload[at]==capsules[OUTER].disk[at]);
    }
    /* The configured return plan changes only the launch allocation. The
     * disk CRC therefore never needs to include a checksum of itself. */
    assert(memcmp(outer_payload+outer_return_at,capsules[OUTER].disk+outer_return_at,KUI_SMASH_MENU_RETURN_SEED_BYTES));
}
int main(void) {
    pair_fixture();assert(publish_pair());verify_published_pair();
    assert(capsules[CHILD].writes==3u && capsules[OUTER].writes==2u);
    unsigned child_writes=capsules[CHILD].writes,outer_writes=capsules[OUTER].writes;
    memcpy(outer_payload,outer_template,sizeof(outer_payload));
    assert(publish_pair());verify_published_pair();
    assert(capsules[CHILD].writes==child_writes && capsules[OUTER].writes==outer_writes);
    /* A changed prepared child (for example, fresh card snapshot metadata)
     * updates both disk CRCs before publishing the new launch-only plan. */
    native_payload[4096u]^=1u;memcpy(outer_payload,outer_template,sizeof(outer_payload));
    assert(publish_pair());verify_published_pair();
    assert(capsules[CHILD].writes==child_writes+3u && capsules[OUTER].writes==outer_writes+2u);
    pair_fixture();native_module.info.build[11]='0';assert(!publish_pair());
    assert(!capsules[CHILD].creates && !capsules[OUTER].creates && initial_return_blank());
    pair_fixture();native_module.info.payload_bytes--;native_module.info.memory_bytes--;
    assert(!publish_pair() && !capsules[CHILD].creates && !capsules[OUTER].creates);
    pair_fixture();native_payload[child_return_at]^=1u;assert(!publish_pair());
    assert(!capsules[CHILD].creates && !capsules[OUTER].creates && initial_return_blank());
    pair_fixture();outer_payload[outer_return_at]^=1u;assert(!publish_pair());
    assert(!capsules[CHILD].creates && !capsules[OUTER].creates);
    pair_fixture();memcpy(native_payload+2048u,native_payload+child_return_at,KUI_SMASH_MENU_RETURN_SEED_BYTES);
    assert(!publish_pair() && !capsules[CHILD].creates && !capsules[OUTER].creates);
    /* Child-first preparation still inspects the OLD outer allocation. An
     * alias is refused before truncating either file, even if both differ. */
    pair_fixture();capsules[CHILD].present=capsules[OUTER].present=true;capsules[OUTER].old_table[2]=20u;
    assert(!publish_pair());assert(!capsules[CHILD].creates && !capsules[OUTER].creates && initial_return_blank());
    pair_fixture();capsules[OUTER].new_table[2]=20u;
    assert(!publish_pair());assert(capsules[CHILD].writes==3u && !capsules[OUTER].writes && initial_return_blank());
    pair_fixture();capsules[CHILD].fail_sync=true;
    assert(!publish_pair());assert(!capsules[OUTER].creates && initial_return_blank());
    assert(kui_smash_menu_loader_seed_find(outer_payload,sizeof(outer_payload))==loader_seed_at);
    pair_fixture();capsules[OUTER].fail_sync=true;
    assert(!publish_pair());assert(capsules[CHILD].writes==3u && capsules[OUTER].writes==2u && initial_return_blank());
    pair_fixture();capsules[OUTER].corrupt_readback=true;
    assert(!publish_pair());assert(capsules[CHILD].writes==3u && capsules[OUTER].writes==2u && initial_return_blank());
    pair_fixture();cancelled_now=true;assert(!publish_pair());
    assert(!capsules[CHILD].creates && !capsules[OUTER].creates && initial_return_blank());
    puts("Smash menu capsule pair publication: OK");return 0;
}
