/* SPDX-License-Identifier: GPL-3.0-only */
/* Independent public recovery-API oracle. Reuse the existing device boundary
 * and authored two-block save, not the new sixteen-block recovery fixture. */
#define main inherited_vmu_app_main
#include "test_vmu_app.c"
#undef main

static void recovered_image_exact(const uint8_t *captured,
    const struct kui_vmu_image_summary *summary) {
    assert(summary->files==1u && summary->used_blocks==2u && summary->free_blocks==198u);
    const uint8_t *entry=captured+253u*512u;
    assert(!memcmp(entry+4u,card[253]+4u,12u));
    assert(entry[0]==card[253][0] && entry[1]==card[253][1]);
    assert(!memcmp(entry+24u,card[253]+24u,8u));
    unsigned at=entry[2]|(unsigned)entry[3]<<8;
    bool used[200]={false};
    for(unsigned i=0;i<2u;i++) {
        assert(at<200u && !used[at]);used[at]=true;
        assert(!memcmp(captured+at*512u,card[128u+i],512u));
        at=captured[254u*512u+2u*at]|(unsigned)captured[254u*512u+2u*at+1u]<<8;
    }
    assert(at==0xfffau);
    for(unsigned b=0;b<200u;b++) if(!used[b])
        for(unsigned i=0;i<512u;i++) assert(!captured[b*512u+i]);
    for(unsigned i=1u;i<208u;i++) {
        const uint8_t *unused=captured+(253u-i/16u)*512u+(i%16u)*32u;
        for(unsigned j=0;j<32u;j++) assert(!unused[j]);
    }
}
int main(int argc,char **argv) {
    assert(argc==2);const char *mode=argv[1];
    bool strict=!strcmp(mode,"strict-between-passes");
    bool expected=!strcmp(mode,"read-recovery-healthy") || !strcmp(mode,"read-recovery-once") ||
        !strcmp(mode,"read-recovery-last") || !strcmp(mode,"read-recovery-between-passes") ||
        !strcmp(mode,"read-recovery-recovered-then-changed");
    collection_setup(1u);move_first_save(128u);collection_read_guard=true;
    if(!strcmp(mode,"dirty-metadata")) card[253][28]=1u;
    if(!strcmp(mode,"orphan-allocation")) put16(card[254]+100u*2u,0xfffau);
    if(!strcmp(mode,"missing-source")) missing=true;
    memcpy(original_card,card,sizeof(card));
    fault=strict?"read-recovery-between-passes":!strcmp(mode,"metadata-change")?"contents-change":mode;
    uint8_t *captured=malloc(KUI_VMU_IMAGE_BYTES);assert(captured);
    memset(captured,0x6du,KUI_VMU_IMAGE_BYTES);
    struct kui_vmu_image_summary summary={99u,99u,99u,99u};struct kui_vmu_view out;
    unsigned slot=!strcmp(mode,"invalid-slot")?8u:0u;
    size_t bytes=!strcmp(mode,"invalid-size")?KUI_VMU_IMAGE_BYTES-1u:KUI_VMU_IMAGE_BYTES;
    bool ok=strict?kui_vmu_capture_run(slot,captured,bytes,&summary,&out,log_line,cancelled,progress):
        kui_vmu_capture_recover_run(slot,captured,bytes,&summary,&out,log_line,cancelled,progress);
    assert(ok==expected && out.status.passed==expected && out.status.complete==expected);
    assert(!connects && !disconnects && !media_reads && !writes && !vmu_writes && !source_vmu_writes && !locks);
    assert(!free_user_reads && !collection_unowned_reads);
    bool changed=!strcmp(mode,"read-recovery-between-passes") ||
        !strcmp(mode,"read-recovery-recovered-then-changed") || strict || !strcmp(mode,"metadata-change");
    if(!changed) assert(!memcmp(original_card,card,sizeof(card)));
    if(expected) {
        assert(!out.status.errors && !out.status.stopped);
        recovered_image_exact(captured,&summary);
        if(!strcmp(mode,"read-recovery-between-passes")) {
            assert(injected && recovery_reads==10u);
            assert(card[128][11]==(uint8_t)(original_card[128][11]^1u));
            for(unsigned i=0;i<512u;i++) if(i!=11u) assert(card[128][i]==original_card[128][i]);
            assert(!memcmp(card[129],original_card[129],512u));
        }
    } else {
        assert(out.status.errors || out.status.stopped);
        assert(!summary.files && !summary.game_files && !summary.used_blocks && !summary.free_blocks);
        for(unsigned i=0;i<KUI_VMU_IMAGE_BYTES;i++) assert(captured[i]==0x6du);
        if(strict) assert(injected && recovery_reads==4u && strstr(out.status.message,"changed at block 128"));
        if(!strcmp(mode,"read-recovery-retained-change"))
            assert(injected && strstr(out.status.message,"retained") && strstr(out.status.message,"RAM"));
        if(!strcmp(mode,"read-recovery-timeout") || !strcmp(mode,"read-recovery-malformed") ||
           !strcmp(mode,"read-recovery-cancel") || !strcmp(mode,"read-recovery-identity") ||
           !strcmp(mode,"read-recovery-oversize") || !strcmp(mode,"read-recovery-prior-oversize"))
            assert(injected && recovery_reads==3u);
    }
    free(captured);printf("PASS independent public whole-save capture: %s\n",mode);
    return 0;
}
