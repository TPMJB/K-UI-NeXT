/* SPDX-License-Identifier: GPL-3.0-only */
/* Real worker/protocol/SD readers against an independently authored,
 * read-only SPI card and a scoped ownership gateway. No physical MMIO. */
#include "kui/vmu_sd_profile.h"
#include "kui/vmu_backing.h"
#include "sd_reader.h"

#define main readonly_base_main
#include "test_vmu_silver_readonly_worker.c"
#undef main

#if KUI_VMU_READONLY_PROOF != 2
#error This test exercises only the explicit SD-backed proof mode
#endif

#define PROFILE_TOKEN UINT32_C(0x8ce01000)
#define LEASE_TOKEN UINT32_C(0x8c006000)

struct readonly_sd_model {
    uint8_t media[1024][512],cid[16],frame[6],reply[520];
    uint32_t ticks,wire_bytes,last_lba;
    unsigned frame_n,reply_n,reply_at,cids,reads,stops,payloads;
    unsigned lease_calls,acquires,releases,callbacks,callback_faults;
    bool selected,owned,streaming,deny_lease,release_fault,healthy;
    bool cid_crc_fault,data_crc_fault,start_fault,stop_fault,payload_fault;
    bool wrong_context,skip_callback,repeat_callback;
    unsigned bad_loan;
};
static struct readonly_sd_model sd;
static struct kui_loader_sd card;
static struct kui_vmu_sd_profile profile,profile_template;
static bool profile_template_ready;
static bool profile_pointer_missing;
static unsigned transaction_mask=6u,preparation_reads;
static void *transaction_context;

static uint16_t model_crc16(const uint8_t *p,size_t n) {
    uint16_t crc=0;
    for(size_t i=0;i<n;i++) {
        crc^=(uint16_t)p[i]<<8;
        for(unsigned bit=0;bit<8u;bit++)
            crc=(uint16_t)((crc<<1)^((crc&0x8000u)?0x1021u:0u));
    }
    return crc;
}
static uint8_t model_crc7(const uint8_t *p) {
    unsigned crc=0;
    for(unsigned i=0;i<5u;i++) for(unsigned bit=0;bit<8u;bit++) {
        unsigned feedback=((crc>>6)^((p[i]>>(7u-bit))&1u))&1u;
        crc=(crc<<1)&127u;if(feedback) crc^=9u;
    }
    return (uint8_t)((crc<<1)|1u);
}
static uint32_t media_lba(unsigned block) {
    assert(block<256u);return block<128u?100u+block:400u+block-128u;
}
static bool media_allowed(uint32_t lba) {
    return (lba>=100u && lba<228u) || (lba>=400u && lba<528u);
}
static void queue_payload(const uint8_t *p,unsigned bytes,bool corrupt) {
    assert(bytes<=512u && sd.reply_n==sd.reply_at);
    sd.reply_at=0;sd.reply[0]=0u;sd.reply[1]=0xfeu;
    memcpy(sd.reply+2u,p,bytes);
    uint16_t crc=model_crc16(p,bytes)^(unsigned)corrupt;
    sd.reply[bytes+2u]=(uint8_t)(crc>>8);sd.reply[bytes+3u]=(uint8_t)crc;
    sd.reply_n=bytes+4u;
}
static void model_command(void) {
    assert(sd.frame[5]==model_crc7(sd.frame));
    unsigned cmd=sd.frame[0]&63u;
    uint32_t arg=(uint32_t)sd.frame[1]<<24|(uint32_t)sd.frame[2]<<16|
        (uint32_t)sd.frame[3]<<8|sd.frame[4];
    sd.frame_n=0;sd.reply_at=sd.reply_n=0;
    if(cmd==10u) {
        assert(!arg && !sd.streaming);++sd.cids;
        queue_payload(sd.cid,16u,sd.cid_crc_fault);
    } else if(cmd==18u) {
        assert(!sd.streaming && arg<1024u && media_allowed(arg));
        ++sd.reads;sd.last_lba=arg;sd.streaming=true;
        if(sd.start_fault) {sd.reply[0]=4u;sd.reply_n=1u;}
        else queue_payload(sd.media[arg],512u,sd.data_crc_fault);
    } else if(cmd==12u) {
        assert(!arg && sd.streaming);++sd.stops;sd.streaming=false;
        sd.reply[0]=0xffu;sd.reply[1]=sd.stop_fault?4u:0u;sd.reply_n=2u;
    } else assert(!"Unexpected SD command: writes, initialization and metadata are forbidden");
}
static void model_select(void *context,bool selected) {
    assert(context==&sd && sd.owned);
    sd.selected=selected;
    if(!selected) {sd.reply_at=sd.reply_n=sd.frame_n=0u;}
}
static uint8_t model_transfer(void *context,uint8_t value,bool slow) {
    assert(context==&sd && sd.owned && !slow);
    ++sd.wire_bytes;++sd.ticks;
    if(!sd.selected) return 0xffu;
    if(sd.reply_at<sd.reply_n) {
        assert(value==0xffu);return sd.reply[sd.reply_at++];
    }
    if(sd.frame_n || (value&0xc0u)==0x40u) {
        sd.frame[sd.frame_n++]=value;if(sd.frame_n==6u) model_command();
    } else assert(value==0xffu);
    return 0xffu;
}
static uint32_t model_ticks(void *context) {assert(context==&sd);return sd.ticks;}
static bool model_transfer_block(void *context,const uint8_t *input,uint8_t *out,
    size_t bytes,bool slow,uint16_t *crc_out) {
    assert(context==&sd && sd.owned && sd.selected && !slow && !input);
    assert(out && crc_out && bytes==512u && !((uintptr_t)out&31u));
    assert(sd.streaming && sd.reply_n-sd.reply_at==514u);++sd.payloads;
    if(sd.payload_fault) {
        /* A bounded uncertain partial transfer must poison the loan. */
        for(unsigned i=0;i<73u;i++) out[i]=model_transfer(context,0xffu,false);
        sd.reply_at=sd.reply_n;return false;
    }
    for(size_t i=0;i<bytes;i++) out[i]=model_transfer(context,0xffu,false);
    *crc_out=model_crc16(out,bytes);return true;
}
static int preparation_read(void *context,uint16_t block,uint8_t out[512]) {
    assert(context==&sd && block<256u);++preparation_reads;
    memcpy(out,sd.media[media_lba(block)],512u);return 0;
}

void kui_vmu_test_sd_reset(void) {
    memset(&sd,0,sizeof(sd));sd.healthy=true;profile_pointer_missing=false;
    transaction_context=NULL;
    memset(sd.media,0x69,sizeof(sd.media));
    for(unsigned i=0;i<256u;i++) assert(kui_vmu_image_blank_block((uint16_t)i,sd.media[media_lba(i)])==0);
    for(unsigned i=0;i<512u;i++) sd.media[media_lba(42u)][i]=(uint8_t)(i*7u+(i>>3)+0x31u);
    for(unsigned i=0;i<16u;i++) sd.cid[i]=(uint8_t)(0x20u+i);
    if(!profile_template_ready) {
        memset(&profile_template,0,sizeof(profile_template));
        profile_template.transport=KUI_VMU_SD_PROFILE_TRANSPORT;
        profile_template.image_bytes=KUI_VMU_SD_PROFILE_IMAGE_BYTES;
        profile_template.high_capacity=1u;profile_template.card_sectors=1024u;
        profile_template.partition_first=32u;profile_template.partition_end=1024u;
        profile_template.extent_count=2u;
        profile_template.extents[0]=(struct kui_vmu_sd_profile_extent){0u,100u,128u};
        profile_template.extents[1]=(struct kui_vmu_sd_profile_extent){128u,400u,128u};
        memcpy(profile_template.cid,sd.cid,16u);profile_template.image_id[0]=0x41u;
        assert(kui_vmu_sd_profile_collect_hashes(&profile_template,preparation_read,&sd)==0);
        profile_template_ready=true;
    }
    profile=profile_template;
    preparation_reads=0u;
    card=(struct kui_loader_sd){.bus={.ctx=&sd,.select=model_select,
        .transfer=model_transfer,.ticks=model_ticks,.transfer_block=model_transfer_block},
        .blocks=1024u,.high_capacity=true,.ready=true,.slow=false};
}
void kui_vmu_test_sd_configure(struct kui_vmu_maple_observe_config *config) {
    assert(config);config->sd_lease=LEASE_TOKEN;config->sd_profile=PROFILE_TOKEN;
    config->sd_profile_bytes=sizeof(profile);
}
const struct kui_vmu_sd_profile *kui_vmu_maple_observer_test_sd_profile(uint32_t token) {
    return token==PROFILE_TOKEN && !profile_pointer_missing?&profile:NULL;
}
int kui_vmu_maple_observer_test_sd_lease(kui_vmu_sd_transaction transaction,void *context) {
    assert(transaction==kui_vmu_maple_observer_sd_transaction && context && !sd.owned);
    ++sd.lease_calls;
    if(sd.deny_lease) return KUI_VMU_SD_LEASE_BUSY;
    assert(card.ready && !sd.selected && !sd.streaming);
    sd.owned=true;++sd.acquires;
    struct kui_loader_sd loan=card;
    if(sd.bad_loan==1u) loan.blocks--;
    else if(sd.bad_loan==2u) loan.high_capacity=false;
    else if(sd.bad_loan==3u) loan.slow=true;
    else if(sd.bad_loan==4u) loan.ready=false;
    transaction_context=context;
    int result=0;
    if(!sd.skip_callback) {
        ++sd.callbacks;result=transaction(sd.wrong_context?NULL:context,&loan);
        if(!result && sd.repeat_callback) {
            ++sd.callbacks;result=transaction(context,&loan);
        }
    }
    if(result) {assert(!loan.ready);++sd.callback_faults;}
    assert(!sd.selected && !sd.streaming);
    ++sd.releases;sd.owned=false;
    if(sd.release_fault) sd.healthy=false;
    if(result || !loan.ready || !sd.healthy) {card.ready=false;return KUI_VMU_SD_LEASE_FAULT;}
    return KUI_VMU_SD_LEASE_OK;
}

static void prepare_read(unsigned selected_bank,unsigned block) {
    build(selected_bank,9u,11u,2u,0x02000000u,block<<24);
    pre(transaction_mask);assert(!stops);physical_complete(9u);preserve();
}
static void assert_success_read(unsigned block) {
    const struct kui_vmu_maple_observe_snapshot *s=&kui_vmu_maple_observer_snapshot;
    unsigned completed=s->proof_completed,read_count=sd.reads;
    complete(transaction_mask);success_bytes(524u);
    uint8_t *reply=kui_vmu_maple_observer_test_response(target_rx);
    assert(le32(reply)==0x82010008u && le32(reply+4u)==0x02000000u);
    assert(le32(reply+8u)==block<<24);
    assert(!memcmp(reply+12u,sd.media[media_lba(block)],512u));
    assert(!stops && s->proof_completed==completed+1u && sd.reads==read_count+1u);
    assert(sd.cids==sd.reads && sd.stops==sd.reads && sd.payloads==sd.reads);
    assert(s->sd_reads==sd.reads && s->sd_cid_checks==sd.cids && !s->sd_faults && !s->sd_busy);
    assert(!s->proof_commit_attempts && !s->proof_flags && card.ready && sd.healthy);
    assert(sd.acquires==sd.releases && sd.callbacks==sd.acquires && !sd.callback_faults);
    assert(s->completion_sr_last==sr(transaction_mask));
}
static void actual_image_and_profile_copy(void) {
    reset(100u);
    uint8_t blank[512];assert(kui_vmu_image_blank_block(42u,blank)==0);
    assert(memcmp(blank,sd.media[media_lba(42u)],512u)); /* Imported bytes, not blank emulation. */
    /* The source profile belongs to the departed shell and may disappear. */
    memset(&profile,0,sizeof(profile));profile_pointer_missing=true;
    prepare_read(0u,42u);assert_success_read(42u);
    assert(kui_vmu_maple_observer_snapshot.sd_last_lba==142u);
    prepare_read(1u,128u);assert_success_read(128u);
    assert(kui_vmu_maple_observer_snapshot.sd_last_lba==400u);
    prepare_read(0u,255u);assert_success_read(255u);
    assert(kui_vmu_maple_observer_snapshot.sd_last_lba==527u);
    prepare_read(1u,42u);assert_success_read(42u);
    assert(kui_vmu_maple_observer_snapshot.sd_last_lba==142u);
}
static void assert_terminal_no_publication(unsigned expected_result) {
    const struct kui_vmu_maple_observe_snapshot *s=&kui_vmu_maple_observer_snapshot;
    complete(transaction_mask);unchanged();
    assert(stops==1u && s->stop_reason==KUI_VMU_OBSERVE_PROOF_REFUSED);
    assert(!s->proof_completed && !s->proof_replies && !s->proof_commit_attempts);
    assert(!s->sd_reads && s->sd_last_result==expected_result && !sd.owned && !sd.selected);
    unsigned bytes=sd.wire_bytes,leases=sd.lease_calls;
    complete(transaction_mask);unchanged();
    assert(sd.wire_bytes==bytes && sd.lease_calls==leases && !s->proof_commit_attempts);
}
static void identity_data_and_cleanup_failures(void) {
    for(unsigned fault=0;fault<8u;fault++) {
        reset(100u);prepare_read(fault&1u,42u);
        unsigned expected=KUI_VMU_OBSERVE_SD_READ;
        switch(fault) {
        case 0:sd.cid[7]^=1u;expected=KUI_VMU_OBSERVE_SD_IDENTITY;break;
        case 1:sd.cid_crc_fault=true;expected=KUI_VMU_OBSERVE_SD_IDENTITY;break;
        case 2:sd.media[142u][123]^=1u;expected=KUI_VMU_OBSERVE_SD_CRC;break;
        case 3:sd.data_crc_fault=true;break;
        case 4:sd.start_fault=true;break;
        case 5:sd.stop_fault=true;break;
        case 6:sd.payload_fault=true;break;
        default:sd.release_fault=true;expected=KUI_VMU_OBSERVE_SD_LEASE_FAULT;break;
        }
        assert_terminal_no_publication(expected);
        assert(sd.lease_calls==1u && sd.acquires==1u && sd.callbacks==1u && sd.releases==1u);
        assert(!card.ready && kui_vmu_maple_observer_snapshot.sd_faults==1u);
        if(fault<2u) assert(sd.cids==1u && !sd.reads && !sd.stops);
        else assert(sd.cids==1u && sd.reads==1u && sd.stops==1u);
        assert(sd.callback_faults==(fault==7u?0u:1u));
    }
    /* Identity and stored block CRC are checked on every loan, including
     * a second read of exactly the same block after one successful reply. */
    for(unsigned stale=0;stale<2u;stale++) {
        reset(100u);prepare_read(0u,42u);assert_success_read(42u);
        prepare_read(1u,42u);
        if(stale) sd.media[142u][13]^=1u;else sd.cid[0]^=1u;
        complete(transaction_mask);unchanged();
        assert(stops==1u && sd.cids==2u && sd.reads==1u+stale && sd.stops==1u+stale);
        assert(kui_vmu_maple_observer_snapshot.sd_reads==1u && !card.ready);
        assert(kui_vmu_maple_observer_snapshot.sd_last_result==
            (stale?KUI_VMU_OBSERVE_SD_CRC:KUI_VMU_OBSERVE_SD_IDENTITY));
        assert(!kui_vmu_maple_observer_snapshot.proof_commit_attempts);
    }
}
static void busy_bad_loans_and_transaction_context(void) {
    reset(100u);prepare_read(0u,42u);sd.deny_lease=true;
    assert_terminal_no_publication(KUI_VMU_OBSERVE_SD_BUSY);
    assert(sd.lease_calls==1u && !sd.wire_bytes && !sd.acquires && !sd.callbacks && !sd.releases);
    assert(kui_vmu_maple_observer_snapshot.sd_busy==1u && !kui_vmu_maple_observer_snapshot.sd_faults);
    for(unsigned bad=1u;bad<=4u;bad++) {
        reset(100u);prepare_read(bad&1u,42u);sd.bad_loan=bad;
        assert_terminal_no_publication(KUI_VMU_OBSERVE_SD_IDENTITY);
        assert(!sd.wire_bytes && !sd.cids && !sd.reads && !card.ready);
        assert(sd.acquires==1u && sd.callbacks==1u && sd.releases==1u && sd.callback_faults==1u);
    }
    reset(100u);struct kui_loader_sd loan=card;
    assert(kui_vmu_maple_observer_sd_transaction(NULL,&loan)!=0 && !loan.ready);
    assert(!sd.wire_bytes && !sd.lease_calls && !sd.acquires && !sd.releases);
    assert(kui_vmu_maple_observer_sd_transaction(NULL,NULL)!=0 && !sd.wire_bytes);
    for(unsigned fault=0;fault<3u;fault++) {
        reset(100u);prepare_read(fault&1u,42u);
        sd.wrong_context=fault==0u;sd.skip_callback=fault==1u;sd.repeat_callback=fault==2u;
        assert_terminal_no_publication(fault==1u?KUI_VMU_OBSERVE_SD_LEASE_FAULT:KUI_VMU_OBSERVE_SD_CONTEXT);
        assert(sd.acquires==1u && sd.releases==1u && !sd.owned);
        if(fault<2u) assert(!sd.wire_bytes);
        else assert(sd.reads==1u && sd.cids==1u && sd.callbacks==2u);
    }
    reset(100u);prepare_read(0u,42u);assert_success_read(42u);
    const unsigned prior_wire=sd.wire_bytes,prior_leases=sd.lease_calls;
    loan=card;preserve();
    assert(transaction_context && kui_vmu_maple_observer_sd_transaction(transaction_context,&loan)!=0);
    unchanged();assert(!loan.ready && sd.wire_bytes==prior_wire && sd.lease_calls==prior_leases);
}
static void invalid_configuration_and_profile_have_no_wire(void) {
    for(unsigned fault=0;fault<12u;fault++) {
        reset(100u);
        struct kui_vmu_maple_observe_config config={
            KUI_VMU_MAPLE_OBSERVE_MAGIC,KUI_VMU_MAPLE_OBSERVE_VERSION,
            sizeof(config),100u,20u,{0},0,0,0};
        kui_vmu_test_sd_configure(&config);
        switch(fault) {
        case 0:config.sd_lease|=1u;break;
        case 1:config.sd_lease=0x8c003ffeu;break;
        case 2:config.sd_lease=0x8c007800u;break;
        case 3:config.sd_profile|=1u;break;
        case 4:config.sd_profile=0xace01000u;break;
        case 5:config.sd_profile=0x8cfcffe0u;break;
        case 6:config.sd_profile_bytes--;break;
        case 7:profile_pointer_missing=true;break;
        case 8:profile.extents[1].logical_block++;break;
        case 9:profile.transport=0u;break;
        case 10:memset(profile.cid,0,sizeof(profile.cid));break;
        default:memset(profile.sha256,0,sizeof(profile.sha256));break;
        }
        assert(kui_vmu_maple_observer_init(&config)!=0);
        assert(!kui_vmu_maple_observer_snapshot.initialized && !sd.wire_bytes && !sd.lease_calls);
        preserve();kui_vmu_maple_observer_visit(sr(transaction_mask));complete(transaction_mask);unchanged();
        assert(!sd.wire_bytes && !sd.lease_calls && !sd.callbacks && !sd.releases);
    }
}

static void full_imported_profile_preflight_checks_sha(void) {
    reset(100u);preserve();
    assert(profile.image_bytes==131072u && profile.extent_count==2u);
    assert(kui_vmu_sd_profile_verify_image(&profile,preparation_read,&sd)==0);
    assert(preparation_reads==256u && !sd.wire_bytes && !sd.lease_calls);
    profile.sha256[7]^=1u;preparation_reads=0u;
    assert(kui_vmu_sd_profile_verify_image(&profile,preparation_read,&sd)==KUI_VMU_SD_PROFILE_VERIFY);
    assert(preparation_reads==256u && !sd.wire_bytes && !sd.lease_calls);unchanged();
    profile=profile_template;profile.block_crc[42u]^=1u;preparation_reads=0u;
    assert(kui_vmu_sd_profile_verify_image(&profile,preparation_read,&sd)==KUI_VMU_SD_PROFILE_VERIFY);
    assert(preparation_reads==43u && !sd.wire_bytes && !sd.lease_calls);unchanged();
}

static void rejected_silver_ownership_never_borrows_sd(void) {
    const struct kui_vmu_maple_observe_snapshot *s=&kui_vmu_maple_observer_snapshot;
    for(unsigned post=0;post<2u;post++) for(unsigned fault=0;fault<68u;fault++) {
        reset(100u);build(1u,9u,11u,2u,0x02000000u,42u<<24);
        if(post) {pre(transaction_mask);assert(!stops);physical_complete(9u);}
        if(fault<20u) corrupt(fault,post);
        else *field(0xfe8u+(fault-20u)*4u)^=0x400u;
        preserve();
        if(post) complete(transaction_mask);else kui_vmu_maple_observer_visit(sr(transaction_mask));
        unchanged();assert(stops==1u && !s->proof_completed && !s->proof_replies);
        assert(!sd.lease_calls && !sd.wire_bytes && !sd.cids && !sd.reads && !sd.acquires);
        assert(!s->proof_commit_attempts && !rx_reads);
    }
}

#ifndef KUI_VMU_SD_WORKER_ENTRY
#define KUI_VMU_SD_WORKER_ENTRY main
#endif
int KUI_VMU_SD_WORKER_ENTRY(void) {
    assert(readonly_base_main()==0);
    full_imported_profile_preflight_checks_sha();
    for(transaction_mask=6u;transaction_mask<=15u;transaction_mask+=9u) {
        actual_image_and_profile_copy();identity_data_and_cleanup_failures();
        busy_bad_loans_and_transaction_context();invalid_configuration_and_profile_have_no_wire();
        rejected_silver_ownership_never_borrows_sd();
    }
    puts("Silver SD worker: imported image reads, scoped read-only SPI, identity/CRC/cleanup faults and zero-publication refusals passed");
    return 0;
}
