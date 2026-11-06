/* SPDX-License-Identifier: GPL-3.0-only */
/* Actual FatFs allocation and capsule guards, with the pre-existing card
 * service's validated-profile output supplied at an explicit fixture seam. */
#define main card_session_original_capsule_io_suite
#include "test_smash_capsule_launch_io.c"
#undef main
#include "kui/game_launch_settings.h"
#include "kui/vc2_ce_vmu_persist.h"
#include "../src/apps/smash_menu_collection_files.inc"
struct files {char root[512];bool reader_open;};
struct games_card_platform {FATFS *fs;struct kui_volume volume;char history_problem[128];};
static unsigned salt_calls,source_closes;
static bool close_reader(struct files *files) {++source_closes;files->reader_open=false;return true;}
static void games_card_salt(void *context,uint8_t out[16]) {
    struct games_card_platform *p=context;CHECK(p->fs==&fs && p->volume.start==volume.start);
    memset(out,0x4d,16u);put32(out,++salt_calls);
}
#include "../src/apps/smash_card_session_prepare.inc"
enum {MODULE_BYTES=12288,CONFIG_AT=1024};
static uint8_t module_data[4][MODULE_BYTES],card_page[4096],witness[295424],saved[295424];
static struct kui_runtime_image outer,children[3];
static struct files source;
static struct kui_vmu_persist_profile card_profile;
static struct kui_retail_manifest card_map;
static struct smash_collection_files collection;
static struct smash_card_session_gui session;
static void blank_modules(void) {
    memset(module_data,0x77,sizeof(module_data));
    struct kui_smash_card_config config={.magic0=KUI_SMASH_CARD_CONFIG_MAGIC0,
        .magic1=KUI_SMASH_CARD_CONFIG_MAGIC1,.version=1u,.bytes=KUI_SMASH_CARD_CONFIG_BYTES};
    struct kui_smash_card_receipt receipt={.magic0=KUI_SMASH_CARD_RECEIPT_MAGIC0,
        .magic1=KUI_SMASH_CARD_RECEIPT_MAGIC1,.version=KUI_SMASH_CARD_RECEIPT_VERSION,.bytes=KUI_SMASH_CARD_RECEIPT_BYTES};
    for(unsigned i=0u;i<4u;i++) {
        memcpy(module_data[i]+CONFIG_AT,&config,sizeof(config));
        memcpy(module_data[i]+KUI_SMASH_CARD_RECEIPT_OFFSET,&receipt,sizeof(receipt));
        struct kui_runtime_image *module=i?&children[i-1u]:&outer;
        *module=(struct kui_runtime_image){.data=module_data[i],
            .info={.payload_bytes=MODULE_BYTES,.memory_bytes=MODULE_BYTES,.build="123456abcdef"}};
    }
    source=(struct files){.root="/games/smash",.reader_open=true};source_closes=0u;
}
static void prepare_context(void) {
    blank_modules();CHECK(smash_card_session_modules(&session,&outer,children,&problem) && session.enabled);
    card_map=(struct kui_retail_manifest){.partition_start=volume.start,.partition_end=(uint64_t)volume.start+volume.count};
    collection=(struct smash_collection_files){.fs=&fs,.volume=&volume,.map=&card_map,
        .profile=&card_profile,.page=card_page,.card_session=true};
    CHECK(smash_collection_files_inspect(&collection,&problem,cancel));
}
static bool prepare_session(void) {
    return smash_card_session_prepare(&source,&collection,&outer,children,&session,&problem,log_line,cancel);
}
static void read_witness(void) {
    FIL file;UINT got=0u;CHECK(f_open(&file,KUI_SMASH_CARD_WITNESS_PATH,FA_READ)==FR_OK && f_size(&file)==sizeof(witness));
    CHECK(f_read(&file,witness,sizeof(witness),&got)==FR_OK && got==sizeof(witness) && f_close(&file)==FR_OK);
}
static void configs_valid(void) {
    struct kui_vmu_fs_witness_binding common={0};
    for(unsigned i=0u;i<4u;i++) {
        struct kui_smash_card_config config;memcpy(&config,module_data[i]+CONFIG_AT,sizeof(config));
        CHECK(kui_smash_card_config_valid(&config) && config.role==i+1u && config.generation==session.generation);
        CHECK(!memcmp(&config.data,&session.witness.runs,sizeof(config.data)));
        CHECK(!memcmp(&config.allocated,&session.witness.allocated_runs,sizeof(config.allocated)));
        struct kui_vmu_fs_witness_binding binding;CHECK(kui_smash_card_binding(&config,&card_profile,&binding));
        if(i)CHECK(!memcmp(&binding,&common,sizeof(binding)));else common=binding;
    }
}
static void configs_blank(void) {
    for(unsigned i=0u;i<4u;i++) {
        struct kui_smash_card_config config;memcpy(&config,module_data[i]+CONFIG_AT,sizeof(config));
        CHECK(kui_smash_card_config_blank(&config));
    }
}
static void module_refusals(void) {
    blank_modules();memset(module_data,0x77,sizeof(module_data));
    CHECK(smash_card_session_modules(&session,&outer,children,&problem) && !session.enabled);
    for(unsigned missing=0u;missing<4u;missing++) {
        blank_modules();memset(module_data[missing]+CONFIG_AT,0,sizeof(struct kui_smash_card_config));
        CHECK(!smash_card_session_modules(&session,&outer,children,&problem));
    }
    blank_modules();module_data[1][CONFIG_AT+16u]=1u;CHECK(!smash_card_session_modules(&session,&outer,children,&problem));
    blank_modules();memcpy(module_data[1]+6144u,module_data[1]+CONFIG_AT,sizeof(struct kui_smash_card_config));
    CHECK(!smash_card_session_modules(&session,&outer,children,&problem));
    blank_modules();module_data[2][KUI_SMASH_CARD_RECEIPT_OFFSET+16u]=1u;
    CHECK(!smash_card_session_modules(&session,&outer,children,&problem));
    for(unsigned old=0u;old<4u;old++) {
        blank_modules();put32(module_data[old]+KUI_SMASH_CARD_RECEIPT_OFFSET+4u,UINT32_C(0x31305243));
        put32(module_data[old]+KUI_SMASH_CARD_RECEIPT_OFFSET+8u,1u);
        CHECK(!smash_card_session_modules(&session,&outer,children,&problem));
    }
    blank_modules();
    for(unsigned i=0u;i<4u;i++) {
        put32(module_data[i]+KUI_SMASH_CARD_RECEIPT_OFFSET+4u,UINT32_C(0x31305243));
        put32(module_data[i]+KUI_SMASH_CARD_RECEIPT_OFFSET+8u,1u);
    }
    CHECK(!smash_card_session_modules(&session,&outer,children,&problem));
}
static void trial_card(BYTE kind,DWORD cluster) {
    wide=true;memset(payload_runs,0,sizeof(payload_runs));format_volume(kind,cluster);
    /* Reserve an unrelated file so every test allocation lies beyond the
     * fixture profile's protected file ranges. It is never payload-written. */
    FIL filler;CHECK(f_open(&filler,"0:/filler.bin",FA_READ|FA_WRITE|FA_CREATE_ALWAYS)==FR_OK);
    CHECK(f_expand(&filler,8u*1024u*1024u,1u)==FR_OK && f_sync(&filler)==FR_OK && f_close(&filler)==FR_OK);
    prepare_files();prepare_context();CHECK(!collection.allocated[SMASH_CAP_CARD_SESSION].blocks);
    reset_counts();observing=true;CHECK(prepare_session());observing=false;
    CHECK(source_closes==1u && session.generation && collection.allocated[SMASH_CAP_CARD_SESSION].blocks);
    CHECK(session.witness.runs.blocks==577u);configs_valid();read_witness();verify_payloads();
    CHECK(!memcmp(witness,"KUIFSW01\1\0\0\0\0\2\0\0",16u));
    uint32_t first_generation=session.generation;
    FIL file;UINT put=0u;uint8_t runtime=0x9au;
    CHECK(f_open(&file,KUI_SMASH_CARD_WITNESS_PATH,FA_WRITE)==FR_OK && f_lseek(&file,1000u)==FR_OK);
    CHECK(f_write(&file,&runtime,1u,&put)==FR_OK && put==1u && f_sync(&file)==FR_OK && f_close(&file)==FR_OK);
    read_witness();memcpy(saved,witness,sizeof(saved));
    prepare_context();reset_counts();observing=true;CHECK(prepare_session());observing=false;
    CHECK(!writes && session.generation!=first_generation);configs_valid();read_witness();
    CHECK(!memcmp(saved,witness,sizeof(saved)));verify_payloads();
    struct kui_smash_menu_capsule_runs witness_allocated=session.witness.allocated_runs;
    /* The reverse guard applies to every later child/outer/helper publisher:
     * its old allocation cannot be truncated if the witness aliases it. */
    for(unsigned i=0u;i<SMASH_CAP_CARD_SESSION;i++) {
        prepare_context();collection.allocated[SMASH_CAP_CARD_SESSION]=collection.allocated[i];
        struct kui_smash_menu_capsule_prepared refused={0};
        reset_counts();observing=true;
        CHECK(!smash_collection_file_publish(&collection,i,payload[i],sizes[i],sizes[i],&refused,
            &problem,log_line,cancel));
        observing=false;CHECK(!writes);read_witness();CHECK(!memcmp(saved,witness,sizeof(saved)));verify_payloads();
    }
    for(unsigned i=0u;i<SMASH_CAP_CARD_SESSION;i++) {
        prepare_context();collection.allocated[i]=witness_allocated;
        reset_counts();observing=true;CHECK(!prepare_session());observing=false;CHECK(!writes);
        configs_blank();read_witness();CHECK(!memcmp(saved,witness,sizeof(saved)));verify_payloads();
    }
    prepare_context();strcpy(source.root,"/KUI/apps/games");reset_counts();observing=true;
    CHECK(!prepare_session());observing=false;CHECK(!reads && !writes);configs_blank();
    /* Reallocate the same extent into every forbidden slot. The real FatFs
     * allocator may write metadata, but the post-allocation guard must stop
     * before any witness payload sector or existing capsule is changed. */
    DWORD first_cluster=(DWORD)((witness_allocated.first_lba-volume.start-fs.database)/fs.csize+2u);
    for(unsigned i=0u;i<SMASH_CAP_CARD_SESSION;i++) {
        CHECK(f_unlink(KUI_SMASH_CARD_WITNESS_PATH)==FR_OK);prepare_context();
        collection.allocated[i]=witness_allocated;fs.last_clst=first_cluster;
        memset(payload_runs,0,sizeof(payload_runs));payload_runs[0]=witness_allocated;
        reset_counts();observing=true;CHECK(!prepare_session());observing=false;
        CHECK(!payload_write_sectors);configs_blank();verify_payloads();
    }
    module_refusals();
    unmount_volume();free(disk);disk=NULL;
    printf("PASS real %s/%u shared-card GUI: seventh file FIRST, four configs/common binding, fresh generation, runtime reuse, six old/new allocation guards and mixed-capability refusals\n",
        kind==FM_FAT32?"FAT32":"exFAT",(unsigned)cluster);
}
static void read_actual_module(const char *path,struct kui_runtime_image *module) {
    FILE *input=fopen(path,"rb");CHECK(input && !fseek(input,0,SEEK_END));
    long length=ftell(input);CHECK(length>64 && !fseek(input,0,SEEK_SET));
    uint8_t header[64];CHECK(fread(header,1u,sizeof(header),input)==sizeof(header));
    CHECK(kui_runtime_header(header,(uint64_t)length,&module->info)==KUI_RUNTIME_OK);
    module->data=malloc(module->info.payload_bytes);CHECK(module->data);
    CHECK(fread(module->data,1u,module->info.payload_bytes,input)==module->info.payload_bytes &&
        fgetc(input)==EOF && !fclose(input));
    CHECK(kui_retail_crc32(0u,module->data,module->info.payload_bytes)==module->info.crc32);
}
static void actual_modules(const char *const paths[4]) {
    uint8_t *original[4];size_t offsets[4];
    static const uint8_t typed[16]={'K','U','I','S','C','F','0','1',1u,0u,0u,0u,0x80u,0x10u,0u,0u};
    memset(&outer,0,sizeof(outer));memset(children,0,sizeof(children));
    /* Read and authenticate every source envelope before discovery or patching.
     * Only the private payload copies may subsequently change. */
    for(unsigned i=0u;i<4u;i++) {
        struct kui_runtime_image *module=smash_card_session_module(i,&outer,children);
        read_actual_module(paths[i],module);original[i]=malloc(module->info.payload_bytes);CHECK(original[i]);
        memcpy(original[i],module->data,module->info.payload_bytes);
        unsigned count=0u;
        for(size_t at=0u;at+sizeof(typed)<=module->info.payload_bytes;at++)
            if(!memcmp((const uint8_t *)module->data+at,typed,sizeof(typed))) {CHECK(!(at&3u));++count;}
        CHECK(count==1u);
        offsets[i]=kui_smash_card_config_find(module->data,module->info.payload_bytes);CHECK(offsets[i]!=SIZE_MAX);
        struct kui_smash_card_receipt receipt;
        memcpy(&receipt,(const uint8_t *)module->data+KUI_SMASH_CARD_RECEIPT_OFFSET,sizeof(receipt));
        CHECK(kui_smash_card_receipt_blank(&receipt));
    }
    CHECK(smash_card_session_modules(&session,&outer,children,&problem) && session.enabled);
    for(unsigned i=0u;i<4u;i++) {
        struct kui_runtime_image *module=smash_card_session_module(i,&outer,children);
        CHECK(session.offset[i]==offsets[i] && !memcmp(original[i],module->data,module->info.payload_bytes));
    }
    wide=true;memset(payload_runs,0,sizeof(payload_runs));format_volume(FM_EXFAT,131072u);
    FIL filler;CHECK(f_open(&filler,"0:/filler.bin",FA_READ|FA_WRITE|FA_CREATE_ALWAYS)==FR_OK);
    CHECK(f_expand(&filler,8u*1024u*1024u,1u)==FR_OK && f_sync(&filler)==FR_OK && f_close(&filler)==FR_OK);
    prepare_files();
    source=(struct files){.root="/games/smash",.reader_open=true};source_closes=0u;
    card_map=(struct kui_retail_manifest){.partition_start=volume.start,.partition_end=(uint64_t)volume.start+volume.count};
    collection=(struct smash_collection_files){.fs=&fs,.volume=&volume,.map=&card_map,
        .profile=&card_profile,.page=card_page,.card_session=true};
    CHECK(smash_collection_files_inspect(&collection,&problem,cancel));
    CHECK(prepare_session() && source_closes==1u && session.generation);
    struct kui_vmu_fs_witness_binding common={0};
    for(unsigned i=0u;i<4u;i++) {
        struct kui_runtime_image *module=smash_card_session_module(i,&outer,children);
        struct kui_smash_card_config config;struct kui_vmu_fs_witness_binding binding;
        memcpy(&config,(const uint8_t *)module->data+offsets[i],sizeof(config));
        CHECK(kui_smash_card_config_valid(&config) && config.role==i+1u &&
            config.generation==session.generation && !memcmp(config.build,module->info.build,12u));
        CHECK(kui_smash_card_binding(&config,&card_profile,&binding));
        if(i)CHECK(!memcmp(&binding,&common,sizeof(binding)));else common=binding;
        CHECK(!memcmp(original[i],module->data,offsets[i]));
        size_t after=offsets[i]+sizeof(config);
        CHECK(!memcmp(original[i]+after,(const uint8_t *)module->data+after,module->info.payload_bytes-after));
        /* Reopen the same source read-only after patching to prove no package
         * or its envelope was modified while exercising the production GUI. */
        struct kui_runtime_image reread={0};read_actual_module(paths[i],&reread);
        CHECK(!memcmp(&reread.info,&module->info,sizeof(reread.info)) &&
            !memcmp(reread.data,original[i],module->info.payload_bytes));
        printf("PASS actual role %u build %.12s payload %lu CRC %08lx: unique aligned config @%lx, blank receipt, production GUI binding, source unchanged\n",
            i+1u,module->info.build,(unsigned long)module->info.payload_bytes,
            (unsigned long)module->info.crc32,(unsigned long)offsets[i]);
        free(reread.data);free(original[i]);free(module->data);memset(module,0,sizeof(*module));
    }
    read_witness();CHECK(session.witness.runs.blocks==577u);
    CHECK(!memcmp(witness,"KUIFSW01\1\0\0\0\0\2\0\0",16u));verify_payloads();
    unmount_volume();free(disk);disk=NULL;
    puts("PASS actual four-module shared-card quorum: production discovery before mutation and all four configured roles share one binding");
}
int main(int argc,char **argv) {
    CHECK(argc==2 || argc==6);FILE *input=fopen(argv[1],"rb");uint8_t wire[4096];CHECK(input);
    CHECK(fread(wire,1u,sizeof(wire),input)==sizeof(wire) && fgetc(input)==EOF && !fclose(input));
    CHECK(!kui_vmu_persist_profile_decode(&card_profile,wire,sizeof(wire)));
    for(unsigned i=0u;i<FILES;i++) {
        payload[i]=malloc(sizes[i]);CHECK(payload[i]);
        for(uint32_t at=0u;at<sizes[i];at++)payload[i][at]=(uint8_t)((at*37u)^(at>>7)^(i*0x5bu));
    }
    if(argc==6)actual_modules((const char *const *)(argv+2));
    else {trial_card(FM_FAT32,512u);trial_card(FM_EXFAT,4096u);trial_card(FM_EXFAT,131072u);}
    for(unsigned i=0u;i<FILES;i++)free(payload[i]);
    return 0;
}
