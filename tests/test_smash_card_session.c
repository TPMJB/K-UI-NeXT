/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/smash_card_session.h"
#include "kui/retail_image.h"
#include "kui/hash.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static struct kui_smash_card_config blank(void) {
    return (struct kui_smash_card_config){.magic0=KUI_SMASH_CARD_CONFIG_MAGIC0,
        .magic1=KUI_SMASH_CARD_CONFIG_MAGIC1,.version=1u,.bytes=KUI_SMASH_CARD_CONFIG_BYTES};
}
static void reseal(void *object,size_t bytes,size_t offset) {
    uint8_t *p=object;uint32_t zero=0u,crc=kui_retail_crc32(0u,p,offset);
    crc=kui_retail_crc32(crc,&zero,4u);crc=kui_retail_crc32(crc,p+offset+4u,bytes-offset-4u);
    memcpy(p+offset,&crc,4u);
}
static bool zero(const void *object,size_t bytes) {
    const uint8_t *p=object;for(size_t i=0u;i<bytes;i++)if(p[i])return false;return true;
}
static void binding_domain(const struct kui_smash_card_config *c,const char *domain,uint8_t out[32]) {
    struct kui_sha256 h;kui_sha256_init(&h);kui_sha256_update(&h,domain,strlen(domain)+1u);
    kui_sha256_update(&h,c->build,12u);
    uint32_t words[]={c->generation,c->witness_bytes,c->data.first_lba,c->data.first_blocks,
        c->data.second_lba,c->data.blocks,c->allocated.first_lba,c->allocated.first_blocks,
        c->allocated.second_lba,c->allocated.blocks};
    for(unsigned i=0u;i<sizeof(words)/sizeof(words[0]);i++) {
        uint8_t b[4];for(unsigned j=0u;j<4u;j++)b[j]=(uint8_t)(words[i]>>(8u*j));
        kui_sha256_update(&h,b,sizeof(b));
    }
    kui_sha256_update(&h,c->ownership_sha256,32u);kui_sha256_digest(&h,out);
}
int main(int argc,char **argv) {
    assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);uint8_t wire[4096];
    assert(fread(wire,1u,sizeof(wire),f)==sizeof(wire) && fgetc(f)==EOF && !fclose(f));
    struct kui_vmu_persist_profile profile;assert(!kui_vmu_persist_profile_decode(&profile,wire,sizeof(wire)));
    uint32_t spc=profile.geometry.sectors_per_cluster;
    uint32_t lba=(uint32_t)(profile.geometry.data_first_lba+(uint64_t)(profile.geometry.cluster_count-2048u)*spc);
    const uint32_t blocks=KUI_VMU_FS_WITNESS_FILE_BYTES/512u;
    struct kui_smash_menu_capsule_runs data={lba,blocks,0u,blocks};
    uint32_t allocated_blocks=((blocks+spc-1u)/spc+1u)*spc;
    struct kui_smash_menu_capsule_runs allocated={lba,allocated_blocks,0u,allocated_blocks};
    struct kui_smash_card_config configs[4];struct kui_vmu_fs_witness_binding bindings[4];
    for(unsigned i=0u;i<4u;i++) {
        configs[i]=blank();assert(kui_smash_card_config_blank(&configs[i]));
        assert(kui_smash_card_configure(&configs[i],i+1u,19u,"123456abcdef",&data,&allocated,&profile));
        assert(kui_smash_card_config_valid(&configs[i]) && !kui_smash_card_config_blank(&configs[i]));
        assert(kui_smash_card_binding(&configs[i],&profile,&bindings[i]));
        if(i)assert(!memcmp(&bindings[0],&bindings[i],sizeof(bindings[i])));
        assert(i?!memcmp(configs[i].profile_wire,(uint8_t[4096]){0},4096u):!memcmp(configs[i].profile_wire,wire,4096u));
        struct kui_smash_card_config before=configs[i];
        assert(!kui_smash_card_configure(&configs[i],i+1u,20u,"123456abcdef",&data,&allocated,&profile));
        assert(!memcmp(&before,&configs[i],sizeof(before)));
    }
    uint8_t digest[32];binding_domain(&configs[0],"KUI/SMASH/CARD-SESSION/BINDING/V2",digest);
    assert(!memcmp(digest,bindings[0].binding_sha256,32u));
    binding_domain(&configs[0],"KUI/SMASH/CARD-SESSION/BINDING/V1",digest);
    assert(memcmp(digest,bindings[0].binding_sha256,32u));
    uint8_t payload[3u*KUI_SMASH_CARD_CONFIG_BYTES+4u];memset(payload,0x55,sizeof(payload));
    struct kui_smash_card_config initial=blank();memcpy(payload+4u,&initial,sizeof(initial));
    assert(kui_smash_card_config_find(payload,sizeof(payload))==4u);
    memcpy(payload+4u+sizeof(initial),&initial,sizeof(initial));assert(kui_smash_card_config_find(payload,sizeof(payload))==SIZE_MAX);
    memset(payload,0x55,sizeof(payload));memcpy(payload+1u,&initial,sizeof(initial));assert(kui_smash_card_config_find(payload,sizeof(payload))==SIZE_MAX);
    memset(payload,0x55,sizeof(payload));memcpy(payload,&initial,16u);assert(kui_smash_card_config_find(payload,sizeof(payload))==SIZE_MAX);
    assert(kui_smash_card_config_find(NULL,sizeof(payload))==SIZE_MAX);
    struct kui_smash_card_config changed;
    for(size_t at=0u;at<sizeof(changed);at++) {
        changed=configs[0];((uint8_t *)&changed)[at]^=1u;assert(!kui_smash_card_config_valid(&changed));
    }
    for(unsigned failure=0u;failure<8u;failure++) {
        changed=configs[1];
        if(failure==0u)changed.role=0u;
        if(failure==1u)changed.generation=0u;
        if(failure==2u)changed.reserved0=1u;
        if(failure==3u)changed.profile_wire[0]=1u;
        if(failure==4u)changed.build[0]='G';
        if(failure==5u)changed.data.first_lba++;
        if(failure==6u)changed.allocated.blocks--;
        if(failure==7u)memset(changed.ownership_sha256,0,32u);
        reseal(&changed,sizeof(changed),offsetof(struct kui_smash_card_config,crc32));
        assert(!kui_smash_card_config_valid(&changed));
    }
    struct kui_vmu_persist_profile fresh=profile;fresh.base_sha256[0]^=1u;fresh.block_crc[0]^=1u;fresh.journal_sequence++;
    struct kui_vmu_fs_witness_binding binding;
    assert(kui_smash_card_binding(&configs[1],&fresh,&binding));assert(!memcmp(&binding,&bindings[0],sizeof(binding)));
    fresh=profile;fresh.geometry.volume_serial^=1u;memset(&binding,0xa5,sizeof(binding));
    assert(!kui_smash_card_binding(&configs[1],&fresh,&binding) && zero(&binding,sizeof(binding)));
    changed=configs[1];changed.generation++;reseal(&changed,sizeof(changed),offsetof(struct kui_smash_card_config,crc32));
    assert(kui_smash_card_binding(&changed,&profile,&binding));assert(memcmp(binding.binding_sha256,bindings[0].binding_sha256,32u));
    struct kui_smash_card_config refused=blank(),before=refused;
    assert(!kui_smash_card_configure(&refused,0u,19u,"123456abcdef",&data,&allocated,&profile));
    assert(!kui_smash_card_configure(&refused,1u,0u,"123456abcdef",&data,&allocated,&profile));
    struct kui_smash_menu_capsule_runs overlap=data;overlap.first_lba=profile.files[0].allocated[0].lba;
    struct kui_smash_menu_capsule_runs overlap_allocated=overlap;
    assert(!kui_smash_card_configure(&refused,1u,19u,"123456abcdef",&overlap,&overlap_allocated,&profile));
    assert(!memcmp(&before,&refused,sizeof(before)));
    uint8_t admission[32];memset(admission,0x87,sizeof(admission));
    struct kui_smash_card_receipt receipt={.magic0=KUI_SMASH_CARD_RECEIPT_MAGIC0,
        .magic1=KUI_SMASH_CARD_RECEIPT_MAGIC1,.version=KUI_SMASH_CARD_RECEIPT_VERSION,.bytes=KUI_SMASH_CARD_RECEIPT_BYTES};
    assert(kui_smash_card_receipt_blank(&receipt) && !kui_smash_card_receipt_valid(&receipt));
    assert(!kui_smash_card_receipt_owner(&receipt) && !kui_smash_card_receipt_transition_valid(&receipt,2u));
    struct kui_smash_card_receipt old=receipt;old.magic1=UINT32_C(0x31305243);old.version=1u;
    assert(!kui_smash_card_receipt_blank(&old) && !kui_smash_card_receipt_valid(&old));
    assert(kui_smash_card_receipt_mint(&receipt,&bindings[0],admission));
    assert(receipt.sequence==1u && receipt.version==2u && kui_smash_card_receipt_owner(&receipt)==1u &&
        receipt.flags==(1u|(1u<<8u)) && kui_smash_card_receipt_matches(&receipt,&bindings[0]));
    for(unsigned i=0u;i<4u;i++)assert(kui_smash_card_receipt_matches(&receipt,&bindings[i]));
    for(size_t at=0u;at<sizeof(receipt);at++)for(unsigned bit=0u;bit<8u;bit++) {
        struct kui_smash_card_receipt bad=receipt;((uint8_t *)&bad)[at]^=(uint8_t)(1u<<bit);
        assert(!kui_smash_card_receipt_valid(&bad) && !kui_smash_card_receipt_owner(&bad) &&
            !kui_smash_card_receipt_matches(&bad,&bindings[0]));
    }
    for(unsigned owner=1u;owner<=4u;owner++)for(unsigned next=0u;next<=5u;next++) {
        struct kui_smash_card_receipt trial=receipt;
        trial.flags=1u|(owner<<8u);reseal(&trial,sizeof(trial),offsetof(struct kui_smash_card_receipt,crc32));
        assert(kui_smash_card_receipt_owner(&trial)==owner);
        bool allowed=(owner==1u && next>=2u && next<=4u) || (owner==2u && next==1u);
        struct kui_smash_card_receipt before=trial;
        assert(kui_smash_card_receipt_transition_valid(&trial,next)==allowed);
        assert(kui_smash_card_receipt_transfer(&trial,next)==allowed);
        if(allowed)assert(trial.sequence==2u && kui_smash_card_receipt_owner(&trial)==next &&
            !memcmp(trial.admission_sha256,before.admission_sha256,32u) &&
            !memcmp(trial.binding_sha256,before.binding_sha256,32u));
        else assert(!memcmp(&before,&trial,sizeof(trial)));
    }
    for(unsigned failure=0u;failure<7u;failure++) {
        struct kui_smash_card_receipt bad=receipt;
        if(failure==0u)bad.flags=1u;
        if(failure==1u)bad.flags=1u|(5u<<8u);
        if(failure==2u)bad.flags|=2u;
        if(failure==3u)bad.generation=0u;
        if(failure==4u)bad.sequence=0u;
        if(failure==5u) {bad.magic1=UINT32_C(0x31305243);bad.version=1u;}
        if(failure==6u)memset(bad.admission_sha256,0,32u);
        reseal(&bad,sizeof(bad),offsetof(struct kui_smash_card_receipt,crc32));
        struct kui_smash_card_receipt before=bad;
        assert(!kui_smash_card_receipt_valid(&bad) && !kui_smash_card_receipt_owner(&bad) &&
            !kui_smash_card_receipt_transfer(&bad,2u) && !memcmp(&before,&bad,sizeof(bad)));
    }
    /* CE terminal return is an explicit edge, never permission to use the
     * ordinary transfer API after a still-running CE application. */
    for(unsigned owner=1u;owner<=4u;owner++) {
        struct kui_smash_card_receipt trial=receipt;
        trial.flags=1u|(owner<<8u);reseal(&trial,sizeof(trial),offsetof(struct kui_smash_card_receipt,crc32));
        struct kui_smash_card_receipt before=trial;
        bool allowed=owner==3u || owner==4u;
        assert(kui_smash_card_receipt_ce_return_valid(&trial)==allowed);
        assert(kui_smash_card_receipt_ce_return(&trial)==allowed);
        if(allowed) {
            assert(trial.sequence==before.sequence+1u && kui_smash_card_receipt_owner(&trial)==1u &&
                kui_smash_card_receipt_matches(&trial,&bindings[0]) &&
                !memcmp(trial.binding_sha256,before.binding_sha256,64u));
            trial=before;trial.sequence=UINT32_MAX;
            reseal(&trial,sizeof(trial),offsetof(struct kui_smash_card_receipt,crc32));before=trial;
            assert(!kui_smash_card_receipt_ce_return_valid(&trial) &&
                !kui_smash_card_receipt_ce_return(&trial) && !memcmp(&trial,&before,sizeof(trial)));
        } else assert(!memcmp(&trial,&before,sizeof(trial)));
    }
    assert(!kui_smash_card_receipt_ce_return_valid(NULL) && !kui_smash_card_receipt_ce_return(NULL));
    struct kui_smash_card_receipt prior=receipt;assert(kui_smash_card_receipt_transfer(&receipt,2u));
    assert(receipt.sequence==2u && kui_smash_card_receipt_owner(&receipt)==2u &&
        !memcmp(receipt.admission_sha256,prior.admission_sha256,32u));
    assert(kui_smash_card_receipt_matches(&receipt,&bindings[0]));
    assert(kui_smash_card_receipt_transfer(&receipt,1u) && receipt.sequence==3u &&
        kui_smash_card_receipt_owner(&receipt)==1u);
    receipt.sequence=UINT32_MAX;reseal(&receipt,sizeof(receipt),offsetof(struct kui_smash_card_receipt,crc32));prior=receipt;
    assert(!kui_smash_card_receipt_transfer(&receipt,2u) && !memcmp(&receipt,&prior,sizeof(receipt)));
    binding=bindings[0];binding.generation++;assert(!kui_smash_card_receipt_matches(&receipt,&binding));
    changed=configs[1];changed.build[0]='9';reseal(&changed,sizeof(changed),offsetof(struct kui_smash_card_config,crc32));
    assert(kui_smash_card_binding(&changed,&profile,&binding) && !kui_smash_card_receipt_matches(&receipt,&binding));
    changed=configs[1];changed.generation++;reseal(&changed,sizeof(changed),offsetof(struct kui_smash_card_config,crc32));
    assert(kui_smash_card_binding(&changed,&profile,&binding) && !kui_smash_card_receipt_matches(&receipt,&binding));
    memset(admission,0,sizeof(admission));assert(!kui_smash_card_receipt_mint(&receipt,&bindings[0],admission));
    assert(!memcmp(&receipt,&prior,sizeof(receipt)));
    assert(!kui_smash_card_receipt_owner(NULL) && !kui_smash_card_receipt_transition_valid(NULL,2u));
    puts("PASS shared Smash card codec v2: four roles/one V2 binding, v1 receipt refusal, all 768 receipt-bit mutations, exact owner transition matrix and generation/build/sequence guards");
    return 0;
}
