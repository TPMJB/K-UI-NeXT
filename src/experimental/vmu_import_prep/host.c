/* SPDX-License-Identifier: GPL-3.0-only */
/* Actual console main through real FatFs. Only Maple, display and raw media
 * are substituted; no physical VMU write implementation is supplied. */
#define _POSIX_C_SOURCE 200809L
#include <string.h>
static int host_source_memcmp(const void *,const void *,size_t);
#define memcmp host_source_memcmp
#define main kui_vmu_import_console_main
#include "main.c"
#undef main
#undef memcmp
#include <assert.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

static FILE *disk_image;
static uint64_t image_blocks;
static bool host_connected,raw_phase,source_present=true,source_locked;
static unsigned physical_slot,source_reads,fail_source_at,fail_source_body;
static unsigned fail_source_block=256u,physical_block_reads[256];
static unsigned source_short,source_oversize,source_mismatch;
static unsigned source_mismatch_index=7u,source_second_fill,source_compare_override;
static bool source_copy_corruption,source_first_mutation,source_raw_mutation;
static unsigned source_busy,source_bad_length,drop_source_at;
static struct kui_vmu_read_bounded_diagnostic host_read_diagnostic;
static unsigned raw_connections,phase_reads,identity_reads,disconnects;
static unsigned host_connections,host_reads,host_writes,source_enumerations;
static unsigned fail_raw_connection,fail_identity,fail_disconnect;
static unsigned fault_journal_start,journal_fault_reads;
static uint8_t physical_image[256][512],physical_before[256][512];
static maple_device_t physical_device;
static bool disturb_guard_report,disturb_compare_report,disturb_root_report,drop_source_on_lock;
static const uint8_t host_cid[16]={0x12,0x34,0x56,0x78,0x9a,0xbc,0xde,0xf0,
    0x13,0x57,0x9b,0xdf,0x24,0x68,0xac,0xe0};
static uint16_t frame[640u*480u];
uint16_t *vram_s=frame;
static int host_source_memcmp(const void *first,const void *second,size_t bytes) {
    if(first==source_first && second==source_second && bytes==512u && source_compare_override)
        return source_compare_override==1u?73:0;
    return memcmp(first,second,bytes);
}
void vid_waitvbl(void) {
    if(disturb_root_report && source_failure_has_root) {
        disturb_root_report=false;memset(clone.image.block,0xe7,sizeof(clone.image.block));
        memset(save_snapshot.image.block,0xe7,sizeof(save_snapshot.image.block));
    }
    if(disturb_compare_report) {
        disturb_compare_report=false;
        memset(source_first,0x3c,sizeof(source_first));memset(source_second,0xc3,sizeof(source_second));
        memset(&host_read_diagnostic,0xea,sizeof(host_read_diagnostic));
    }
    if(!disturb_guard_report) return;
    disturb_guard_report=false;
    /* Rendering can yield while Maple hotplug changes live source metadata.
     * The photographed report must keep the identity from the failed guard. */
    selected_slot=7u;source_device=NULL;source_functions=0u;
    memset(source_name,0x69,sizeof(source_name));source_present=false;
    physical_device.valid=0;physical_device.info.functions=0u;
    memset(physical_device.info.product_name,0x79,sizeof(physical_device.info.product_name));
    memset(&host_read_diagnostic,0xea,sizeof(host_read_diagnostic));
}
void vid_flip(int page) {(void)page;}
void vid_set_mode(int mode,int format) {(void)mode;(void)format;}
void thd_sleep(unsigned milliseconds) {(void)milliseconds;}
maple_device_t *maple_enum_type(int index,uint32_t functions) {(void)index;(void)functions;return NULL;}
void *maple_dev_status(maple_device_t *device) {(void)device;return NULL;}
maple_device_t *maple_enum_dev(int port,int unit) {
    ++source_enumerations;return source_present && port==(int)(physical_slot/2u) && unit==(int)(physical_slot%2u+1u)?&physical_device:NULL;
}
int vmufs_mutex_lock(void) {assert(!source_locked);source_locked=true;if(drop_source_on_lock) source_present=false;return 0;}
int vmufs_mutex_unlock(void) {assert(source_locked);source_locked=false;return 0;}
const struct kui_vmu_read_bounded_diagnostic *kui_vmu_read_bounded_last(void) {
    return &host_read_diagnostic;
}
int kui_vmu_read_bounded(maple_device_t *device,uint16_t block,uint8_t out[512]) {
    assert(source_locked && device==&physical_device && block<256u);++source_reads;
    ++physical_block_reads[block];
    host_read_diagnostic=(struct kui_vmu_read_bounded_diagnostic){.block=block,.stage=10u,
        .frame_state=3u,.lock_attempts=(source_reads&1u)?1u:2u,.response=8,.data_len=130u,
        .expected_function=MAPLE_FUNC_MEMCARD,.actual_function=MAPLE_FUNC_MEMCARD,
        .expected_location=(uint32_t)block<<24,.actual_location=(uint32_t)block<<24};
    if(source_busy) {
        host_read_diagnostic.stage=2u;host_read_diagnostic.result=-5;
        host_read_diagnostic.frame_state=2u;host_read_diagnostic.lock_attempts=7u;
        host_read_diagnostic.response=-127;host_read_diagnostic.data_len=0u;
        return -5;
    }
    if(source_bad_length) {
        host_read_diagnostic.stage=7u;host_read_diagnostic.result=-1;
        host_read_diagnostic.data_len=129u;return -1;
    }
    if(fail_source_at==source_reads || block==fail_source_block || (fail_source_body && destination_open && block==198u)) {
        host_read_diagnostic.stage=5u;host_read_diagnostic.result=-5;host_read_diagnostic.wait_result=-1;
        host_read_diagnostic.frame_state=2u;host_read_diagnostic.queued=1u;
        host_read_diagnostic.response=-127;host_read_diagnostic.data_len=0u;
        host_read_diagnostic.actual_function=host_read_diagnostic.actual_location=0u;
        return -5;
    }
    if(mounted && import_number>=2u && !clone.completed) {
        char name[96];FILINFO info;assert(import_path(name,"identity.bin"));
        FRESULT r=f_stat(name,&info);assert(r==FR_NO_FILE || r==FR_NO_PATH);
    }
    uint8_t raw[512];memcpy(raw,physical_image[block],512u);
    if(source_mismatch && !(source_reads&1u)) {
        if(source_mismatch==2u) memset(raw,(int)source_second_fill,512u);
        else raw[source_mismatch_index]^=1u;
    }
    host_read_diagnostic.raw_crc_before=kui_crc32(0,raw,512u);
    memcpy(out,raw,source_short?511u:512u);
    if(source_oversize) out[512]=0x72u;
    if(source_copy_corruption && !(source_reads&1u)) out[source_mismatch_index]^=1u;
    if(source_raw_mutation && !(source_reads&1u)) raw[source_mismatch_index]^=1u;
    host_read_diagnostic.raw_crc_after=kui_crc32(0,raw,512u);
    host_read_diagnostic.copy_crc=kui_crc32(0,out,512u);
    /* A second transaction can disturb the first caller buffer after its
     * helper's completed-copy fingerprint has already been frozen. */
    if(source_first_mutation && !(source_reads&1u)) source_first[source_mismatch_index]^=1u;
    if(source_reads==drop_source_at) source_present=false;
    return 0;
}
void kui_shell_font_draw(uint16_t *pixels,int x,int y,uint16_t color,const char *text,bool large) {
    (void)pixels;(void)x;(void)y;(void)color;(void)text;(void)large;
}
static uint64_t host_blocks(void *context) {(void)context;return image_blocks;}
static int host_read(void *context,uint32_t lba,size_t count,uint8_t *out) {
    (void)context;assert(host_connected && (uint64_t)lba+count<=image_blocks);++host_reads;
    if(raw_phase) {
        assert(count==1u);++phase_reads;
        if(fail_raw_connection==raw_connections && phase_reads==7u) return -1;
    }
    if(fseeko(disk_image,(off_t)lba*512u,SEEK_SET) || fread(out,512u,count,disk_image)!=count) return -1;
    if(raw_phase && fault_journal_start && raw_connections>=fault_journal_start && persist_raw.profile) {
        uint32_t payload_lba=0;
        assert(!kui_vmu_persist_profile_map_lba(persist_raw.profile,KUI_VMU_PERSIST_JOURNAL,0u,&payload_lba));
        if(lba==payload_lba) {out[0]^=0x72u;++journal_fault_reads;}
    }
    return 0;
}
static int host_write(void *context,uint32_t lba,size_t count,const uint8_t *in) {
    (void)context;assert(host_connected && !raw_phase && (uint64_t)lba+count<=image_blocks);++host_writes;
    return fseeko(disk_image,(off_t)lba*512u,SEEK_SET) || fwrite(in,512u,count,disk_image)!=count?-1:0;
}
static int host_sync(void *context) {(void)context;return fflush(disk_image)||fsync(fileno(disk_image))?-1:0;}
void kui_sd_set_params(unsigned transport,bool crc) {assert(transport==KUI_STORAGE_SCI && crc);}
bool kui_sd_connect(void) {
    assert(!host_connected);++host_connections;host_connected=true;raw_phase=false;
    const struct kui_media_ops ops={NULL,host_blocks,host_read,host_write,host_sync};
    kui_media_set(&ops);return true;
}
bool kui_sd_vmu_import_disconnect(void) {
    assert(host_connected);++disconnects;kui_media_set(NULL);raw_phase=false;host_connected=false;
    assert(!host_sync(NULL));return fail_disconnect!=disconnects;
}
void kui_sd_disconnect(void) {if(host_connected) (void)kui_sd_vmu_import_disconnect();}
bool kui_sd_vmu_profile_identity(struct kui_vmu_sd_identity *out) {
    if(!out || !host_connected) return false;
    memset(out,0,sizeof(*out));memcpy(out->cid,host_cid,16u);out->sectors=image_blocks;out->high_capacity=true;
    ++identity_reads;if(fail_identity && identity_reads>=fail_identity) out->cid[0]^=1u;return true;
}
bool kui_sd_raw_read_ops(struct kui_media_ops *out) {
    if(!out || !host_connected) return false;
    assert(!mounted && !volume.fs_type);raw_phase=true;++raw_connections;phase_reads=0u;
    *out=(struct kui_media_ops){.blocks=host_blocks,.read=host_read};return true;
}
void kui_storage_boot_begin(void) {}
unsigned kui_storage_selected(void) {return KUI_STORAGE_SCI;}
static void host_put16(uint8_t *p,unsigned value) {p[0]=(uint8_t)value;p[1]=(uint8_t)(value>>8);}
static void physical_fixture(void) {
    for(unsigned i=0;i<256u;i++) assert(!kui_vmu_image_blank_block((uint16_t)i,physical_image[i]));
    host_put16(physical_image[254]+199u*2u,198u);host_put16(physical_image[254]+198u*2u,0xfffau);
    uint8_t *entry=physical_image[253];entry[0]=0x33u;entry[1]=0xffu;host_put16(entry+2u,199u);
    memcpy(entry+4u,"SOURCE_SAVE",11u);host_put16(entry+24u,2u);
    for(unsigned i=0;i<512u;i++) {physical_image[199][i]=(uint8_t)(i*17u+31u);physical_image[198][i]=(uint8_t)(i*29u+7u);}
    /* The canonical snapshot must skip these stale free-sector bytes while
     * preserving all allocated payload/protection bytes and the source. */
    memset(physical_image[57],0x9b,512u);
    memset(physical_block_reads,0,sizeof(physical_block_reads));
    memset(&physical_device,0,sizeof(physical_device));physical_device.valid=1;
    physical_device.info.functions=MAPLE_FUNC_MEMCARD;
    memcpy(physical_device.info.product_name,"Owned physical source",21u);
}
static void opaque_root_fixture(unsigned pattern) {
    static const uint8_t varied[12]={0x12,0x34,0xab,0xcd,0x56,0x78,0xfe,0xdc,0xba,0x98,0x76,0x54};
    for(unsigned i=0;i<6u;i++) {
        physical_image[255][0x40u+i]=pattern==2u?varied[i]:(pattern?0xffu:0u);
        physical_image[255][0x52u+i]=pattern==2u?varied[i+6u]:(pattern?0xffu:0u);
    }
}
static void source_geometry_refusals(void) {
    static const struct {const char *name;unsigned offset,value;} cases[]={
        {"magic",0u,0u},{"custom",16u,2u},{"fat-first",0x46u,253u},
        {"fat-count",0x48u,2u},{"dir-first",0x4au,252u},{"dir-count",0x4cu,12u},
        {"user-count",0x50u,199u},{"icon",0x4eu,124u}
    };
    for(unsigned test=0;test<sizeof(cases)/sizeof(cases[0]);test++) {
        physical_fixture();opaque_root_fixture(2u);
        selected_slot=physical_slot=0u;source_present=true;source_reads=0u;
        memset(&clone,0,sizeof(clone));memset(&save_snapshot,0,sizeof(save_snapshot));
        if(cases[test].offset<0x40u) physical_image[255][cases[test].offset]=(uint8_t)cases[test].value;
        else host_put16(physical_image[255]+cases[test].offset,cases[test].value);
        memcpy(physical_before,physical_image,sizeof(physical_before));
        unsigned connects=host_connections,reads=host_reads,writes=host_writes;
        disturb_root_report=true;assert(!capture_preview());
        assert(!disturb_root_report && !preview_ready && !source_failure_reason && source_failure_has_root);
        assert(save_snapshot.result==KUI_VMU_SAVE_SNAPSHOT_IMAGE && save_snapshot.image_result==KUI_VMU_IMAGE_GEOMETRY);
        assert(!clone.started && !clone.source_reads && save_snapshot.source_reads==118u && source_reads==236u);
        assert(!memcmp(source_failure_root,physical_image[255],sizeof(source_failure_root)));
        assert(!memcmp(physical_before,physical_image,sizeof(physical_before)));
        assert(host_connections==connects && host_reads==reads && host_writes==writes);
        assert(!host_connected && !sd_connected && !mounted && !source_locked && !import_number);
        assert(line_count==8u && !strcmp(lines[2],"SNAP result -5 last 255 reads 118"));
        char first_report[LOG_ROWS][LOG_COLS];memcpy(first_report,lines,sizeof(first_report));
        line_count=2u;source_failure_report();assert(!fail("No SD files created. Photograph and restart."));
        assert(line_count==8u && !memcmp(first_report,lines,line_count*LOG_COLS));
        assert(host_connections==connects && host_reads==reads && host_writes==writes && source_reads==236u);
        for(unsigned row=0;row<line_count;row++) printf("%s\n",lines[row]);
        printf("PASS source geometry %s reads 236 SD 0 frozen root report\n",cases[test].name);
    }
}
static void source_guard_refusals(void) {
    static const struct {const char *name;unsigned mask;} cases[]={
        {"context-null",1u},{"output-null",2u},{"block-256",4u},
        {"missing",8u},{"replacement",16u},{"invalid",32u},
        {"functions",64u},{"memcard",128u},{"name-first",256u},
        {"name-last",256u},{"context-device",1u},{"block-65535",4u},
        {"combined-inputs",7u},{"combined-identity",480u},{"combined-missing",15u}
    };
    for(unsigned test=0;test<sizeof(cases)/sizeof(cases[0]);test++) {
        physical_fixture();selected_slot=physical_slot=0u;source_present=true;
        source_device=&physical_device;source_functions=physical_device.info.functions;
        memcpy(source_name,physical_device.info.product_name,30u);source_name[30]=0;
        source_failure_reason=0u;source_failure_has_maple=false;
        memset(&source_failure_guard,0,sizeof(source_failure_guard));
        memset(&clone,0,sizeof(clone));
        uint8_t output[514];memset(output,0x97,sizeof(output));
        void *context=&source_device,*out=output+1;uint16_t block=17u;
        maple_device_t replacement=physical_device;
        if(test==0u) context=NULL;
        if(test==1u) out=NULL;
        if(test==2u) block=256u;
        if(test==3u) source_present=false;
        if(test==4u) source_device=&replacement;
        if(test==5u) physical_device.valid=0;
        if(test==6u) physical_device.info.functions|=MAPLE_FUNC_CONTROLLER;
        if(test==7u) source_functions=physical_device.info.functions=0u;
        unsigned name_index=test==9u?29u:0u;
        if(test==8u || test==9u) physical_device.info.product_name[name_index]^=0x20;
        if(test==10u) context=&physical_device;
        if(test==11u) block=UINT16_MAX;
        if(test==12u || test==14u) {context=NULL;out=NULL;block=UINT16_MAX;}
        if(test==13u) {
            physical_device.valid=0;physical_device.info.functions=0u;
            physical_device.info.product_name[0]^=0x20;
        }
        if(test==14u) source_present=false;
        /* This is the original compound guard, independent of the new helper.
         * Every refused combination must preserve its admission decision. */
        maple_device_t *now=source_present?&physical_device:NULL;
        bool original=context==&source_device && out && block<256u && now && now==source_device &&
            now->valid && now->info.functions==source_functions && (now->info.functions&MAPLE_FUNC_MEMCARD) &&
            !memcmp(now->info.product_name,source_name,30u);
        assert(!original);
        uintptr_t pinned=(uintptr_t)source_device;
        unsigned enumeration_before=source_enumerations,reads_before=source_reads;
        unsigned connects_before=host_connections,sd_reads_before=host_reads,writes_before=host_writes;
        assert(kui_vmu_import_source_read(context,block,out)==-1);
        assert(source_reads==reads_before && host_connections==connects_before && host_reads==sd_reads_before && host_writes==writes_before);
        assert(!host_connected && !sd_connected && !mounted && !source_locked);
        assert(source_enumerations==enumeration_before+1u);
        for(unsigned i=0;i<sizeof(output);i++) assert(output[i]==0x97);
        assert(source_failure_reason==1u && source_failure_pass==0u && source_failure_block==block);
        assert(!source_failure_has_maple && source_failure_guard.mask==cases[test].mask);
        assert(source_failure_guard.slot==0u && source_failure_guard.context==(uintptr_t)context);
        assert(source_failure_guard.out==(uintptr_t)out && source_failure_guard.expected_device==pinned);
        assert(source_failure_guard.actual_device==(uintptr_t)now);
        assert(source_failure_guard.expected_functions==source_functions);
        assert(source_failure_guard.actual_functions==(now?now->info.functions:0u));
        assert(source_failure_guard.valid==(unsigned)(now?now->valid:0));
        if(test==8u || test==9u || test==13u) {
            assert(source_failure_guard.name_index==name_index);
            assert(source_failure_guard.expected_name==(unsigned char)source_name[name_index]);
            assert(source_failure_guard.actual_name==(unsigned char)physical_device.info.product_name[name_index]);
        }
        unsigned char snapshot[sizeof(source_failure_guard)];
        memcpy(snapshot,&source_failure_guard,sizeof(snapshot));
        unsigned failure_block=source_failure_block,failure_reason=source_failure_reason,failure_pass=source_failure_pass;
        /* Hotplug and another Maple diagnostic occur during the first log row. */
        line_count=0u;disturb_guard_report=true;unsigned report_enumerations=source_enumerations;
        source_failure_report();
        assert(!disturb_guard_report && source_enumerations==report_enumerations);
        assert(!memcmp(snapshot,&source_failure_guard,sizeof(snapshot)));
        assert(source_failure_reason==failure_reason && source_failure_pass==failure_pass && source_failure_block==failure_block);
        assert(source_reads==reads_before && host_connections==connects_before && host_reads==sd_reads_before && host_writes==writes_before);
        char first_report[LOG_ROWS][LOG_COLS];memcpy(first_report,lines,sizeof(first_report));
        unsigned first_rows=line_count;bool guard_reported=false;
        for(unsigned row=0;row<line_count;row++) {
            assert(!strstr(lines[row],"MAPLE stage "));
            if(strstr(lines[row],"GUARD bits ")) guard_reported=true;
        }
        assert(guard_reported);
        char expected_guard[LOG_COLS];
        snprintf(expected_guard,sizeof(expected_guard),"GUARD bits %03x slot A1",cases[test].mask);
        assert(!strcmp(lines[2],expected_guard));
        line_count=0u;source_failure_report();
        assert(line_count==first_rows && !memcmp(first_report,lines,line_count*LOG_COLS));
        assert(source_enumerations==report_enumerations && !memcmp(snapshot,&source_failure_guard,sizeof(snapshot)));
        /* A later refused callback must not replace the first guard identity. */
        assert(kui_vmu_import_source_read(NULL,UINT16_MAX,NULL)==-1);
        assert(!memcmp(snapshot,&source_failure_guard,sizeof(snapshot)));
        assert(source_reads==reads_before && host_connections==connects_before && host_reads==sd_reads_before && host_writes==writes_before);
        printf("PASS source guard %s mask %03x enumerations %u reads 0 writes 0 frozen report\n",
            cases[test].name,cases[test].mask,report_enumerations-enumeration_before);
    }
    physical_fixture();selected_slot=physical_slot=0u;source_present=true;
    source_device=&physical_device;source_functions=physical_device.info.functions;
    memcpy(source_name,physical_device.info.product_name,30u);source_name[30]=0;
    memset(&source_failure_guard,0,sizeof(source_failure_guard));
    uint8_t out[512];
    assert(source_guard_check(&source_device,255u,out,true));
    assert(!source_failure_guard.mask && source_same());
}
static void source_compare_refusals(void) {
    static const struct {
        const char *name;unsigned block,index,mismatch,override;
        int fill,second_fill;unsigned path;
    } cases[]={
        {"first-byte",0u,0u,1u,0u,-1,-1,0u},
        {"last-byte",255u,511u,1u,0u,-1,-1,0u},
        {"zero-to-ff",17u,0u,2u,0u,0,255,0u},
        {"ff-to-zero",200u,0u,2u,0u,255,0,0u},
        {"compare-anomaly",57u,512u,0u,1u,-1,-1,0u},
        {"compare-false-equal",57u,7u,1u,2u,-1,-1,0u},
        {"raw-reply-difference",57u,18u,1u,0u,-1,-1,0u},
        {"copy-output-corruption",57u,18u,0u,0u,-1,-1,1u},
        {"first-output-mutated",57u,12u,0u,0u,-1,-1,2u},
        {"raw-change-observation",57u,18u,0u,0u,-1,-1,3u},
        {"same-zero",31u,512u,0u,0u,0,-1,0u},
        {"same-ff",230u,512u,0u,0u,255,-1,0u}
    };
    for(unsigned test=0;test<sizeof(cases)/sizeof(cases[0]);test++) {
        physical_fixture();selected_slot=physical_slot=0u;source_present=true;source_reads=0u;
        source_device=&physical_device;source_functions=physical_device.info.functions;
        memcpy(source_name,physical_device.info.product_name,30u);source_name[30]=0;
        source_failure_reason=0u;source_failure_has_maple=false;
        memset(&source_failure_guard,0,sizeof(source_failure_guard));
        memset(&source_failure_compare,0,sizeof(source_failure_compare));
        unsigned block=cases[test].block;
        if(cases[test].fill>=0) memset(physical_image[block],cases[test].fill,512u);
        uint8_t expected_first[512],expected_second[512],output[514];
        memcpy(expected_first,physical_image[block],512u);memcpy(expected_second,expected_first,512u);
        source_mismatch=cases[test].mismatch;source_mismatch_index=cases[test].index;
        source_compare_override=cases[test].override;
        source_copy_corruption=cases[test].path==1u;source_first_mutation=cases[test].path==2u;
        source_raw_mutation=cases[test].path==3u;
        if(cases[test].second_fill>=0) {
            source_second_fill=(unsigned)cases[test].second_fill;
            memset(expected_second,cases[test].second_fill,512u);
        } else if(source_mismatch) expected_second[source_mismatch_index]^=1u;
        if(source_copy_corruption) expected_second[source_mismatch_index]^=1u;
        if(source_first_mutation) expected_first[source_mismatch_index]^=1u;
        unsigned differences=source_mismatch==2u?512u:(source_mismatch || source_copy_corruption || source_first_mutation);
        bool refused=differences || source_compare_override;
        unsigned connects_before=host_connections,reads_before=host_reads,writes_before=host_writes;
        memset(output,0x97,sizeof(output));assert(!vmufs_mutex_lock());
        assert(kui_vmu_import_source_read(&source_device,(uint16_t)block,output+1)==(refused?-1:0));
        assert(!vmufs_mutex_unlock() && source_reads==2u);
        assert(host_connections==connects_before && host_reads==reads_before && host_writes==writes_before);
        assert(!host_connected && !sd_connected && !mounted && !source_locked);
        assert(output[0]==0x97 && output[513]==0x97);
        if(!refused) {
            assert(!memcmp(output+1,expected_first,512u) && !source_failure_compare.valid && !source_failure_reason);
            uint8_t raw_after[512];memcpy(raw_after,expected_second,512u);
            if(source_raw_mutation) raw_after[source_mismatch_index]^=1u;
            assert(host_read_diagnostic.raw_crc_before==kui_crc32(0,expected_second,512u));
            assert(host_read_diagnostic.raw_crc_after==kui_crc32(0,raw_after,512u));
            assert(host_read_diagnostic.copy_crc==kui_crc32(0,output+1,512u));
            printf("PASS source compare %s reads 2 SD 0 output exact\n",cases[test].name);
            continue;
        }
        for(unsigned i=0;i<sizeof(output);i++) assert(output[i]==0x97);
        assert(source_failure_reason==5u && source_failure_pass==0u && source_failure_block==block);
        assert(!source_failure_has_maple && !source_failure_guard.mask && source_failure_compare.valid);
        const struct source_compare_diagnostic *c=&source_failure_compare;
        assert(c->different==differences && c->first_index==cases[test].index);
        if(differences) assert(c->first_byte==expected_first[c->first_index] && c->second_byte==expected_second[c->first_index]);
        assert(c->comparison==(source_compare_override==1u?73:source_compare_override==2u?0:memcmp(expected_first,expected_second,512u)));
        assert(c->first_crc==kui_crc32(0,expected_first,512u) && c->second_crc==kui_crc32(0,expected_second,512u));
        for(unsigned pass=0;pass<2u;pass++) {
            assert(c->reads[pass].block==block && c->reads[pass].stage==10u && !c->reads[pass].result);
            assert(c->reads[pass].response==8 && c->reads[pass].data_len==130u && c->reads[pass].lock_attempts==pass+1u);
            assert(c->reads[pass].expected_function==MAPLE_FUNC_MEMCARD && c->reads[pass].actual_function==MAPLE_FUNC_MEMCARD);
            assert(c->reads[pass].expected_location==(uint32_t)block<<24 && c->reads[pass].actual_location==(uint32_t)block<<24);
            uint32_t raw_crc=kui_crc32(0,pass && source_mismatch?expected_second:physical_image[block],512u);
            assert(c->reads[pass].raw_crc_before==raw_crc && c->reads[pass].raw_crc_after==raw_crc);
            assert(c->reads[pass].copy_crc==(pass?c->second_crc:kui_crc32(0,physical_image[block],512u)));
        }
        struct source_compare_diagnostic snapshot=*c;
        memset(&clone,0,sizeof(clone));clone.phase=1u;clone.last_block=(uint16_t)block;clone.source_reads=1u;
        line_count=0u;kui_log("Reading all 256 VMU blocks; keep the VMU connected.");
        disturb_compare_report=true;kui_log("Capture refused: status -1, image 0");
        source_failure_report();assert(!fail("No SD files created. Photograph and restart."));
        assert(!disturb_compare_report && !memcmp(&snapshot,c,sizeof(snapshot)) && line_count==14u);
        assert(strstr(lines[0],"Reading all 256") && strstr(lines[1],"Capture refused") && strstr(lines[13],"FAIL:"));
        assert(strstr(lines[4],source_compare_override?"COMPARE anomaly":"COMPARE memcmp"));
        char first_report[LOG_ROWS][LOG_COLS];memcpy(first_report,lines,sizeof(first_report));
        line_count=2u;source_failure_report();assert(!fail("No SD files created. Photograph and restart."));
        assert(line_count==14u && !memcmp(first_report,lines,line_count*LOG_COLS));
        assert(!memcmp(&snapshot,c,sizeof(snapshot)) && source_reads==2u);
        assert(host_connections==connects_before && host_reads==reads_before && host_writes==writes_before);
        for(unsigned row=0;row<line_count;row++) printf("%s\n",lines[row]);
        printf("PASS source compare %s reads 2 SD 0 output unchanged frozen report\n",cases[test].name);
    }
    source_mismatch=source_compare_override=0u;source_mismatch_index=7u;
    source_copy_corruption=source_first_mutation=source_raw_mutation=false;
}
static void file_hash(unsigned number,const char *leaf,uint8_t out[32]) {
    char name[96];int n=snprintf(name,sizeof(name),IMPORT_ROOT "/p%04u/%s",number,leaf);assert(n>0 && n<96);
    FIL file;assert(f_open(&file,name,FA_READ)==FR_OK);struct kui_sha256 hash;kui_sha256_init(&hash);
    for(;;) {uint8_t bytes[512];UINT done=0;assert(f_read(&file,bytes,sizeof(bytes),&done)==FR_OK);if(!done) break;kui_sha256_update(&hash,bytes,done);}
    assert(f_close(&file)==FR_OK);kui_sha256_digest(&hash,out);
}
static void hash_existing_pair(uint8_t out[3][32]) {
    assert(connect_volume());file_hash(1u,"card.vmd",out[0]);file_hash(1u,"card.journal",out[1]);file_hash(1u,"identity.bin",out[2]);
    assert(disconnect_volume());
    /* Test instrumentation is outside the actual tool's connection counts. */
    disconnects=identity_reads=raw_connections=phase_reads=0u;
}
static void new_pair_exact(void) {
    assert(import_number>=2u && clone.completed && !clone.failed && exported_profile_path[0]);
    unsigned before_disconnects=disconnects;fail_disconnect=fail_identity=fail_raw_connection=0u;
    assert(connect_volume());char name[96];FIL file;assert(import_path(name,"card.vmd"));
    assert(f_open(&file,name,FA_READ)==FR_OK && f_size(&file)==131072u);
    for(unsigned i=0;i<256u;i++) {
        uint8_t bytes[512];UINT done=0;
        assert(f_read(&file,bytes,512u,&done)==FR_OK && done==512u);
        const bool free_user=i<200u && physical_image[254][i*2u]==0xfcu && physical_image[254][i*2u+1u]==0xffu;
        if(free_user) {
            assert(!physical_block_reads[i]); /* Physical free data was never requested. */
            for(unsigned byte=0;byte<512u;byte++) assert(!bytes[byte]);
        } else {
            assert(physical_block_reads[i]>0u && !memcmp(bytes,physical_image[i],512u));
        }
    }
    assert(f_close(&file)==FR_OK);assert(import_path(name,"card.journal"));
    assert(f_open(&file,name,FA_READ)==FR_OK && f_size(&file)==1024u);
    for(unsigned i=0;i<2u;i++) {uint8_t bytes[512];UINT done=0;assert(f_read(&file,bytes,512u,&done)==FR_OK && done==512u);for(unsigned j=0;j<512u;j++) assert(!bytes[j]);}
    assert(f_close(&file)==FR_OK);assert(import_path(name,"identity.bin"));
    assert(f_open(&file,name,FA_READ)==FR_OK && f_size(&file)==16u);uint8_t check[16];UINT done=0;
    assert(f_read(&file,check,16u,&done)==FR_OK && done==16u && !memcmp(check,image_id,16u));assert(f_close(&file)==FR_OK);
    assert(f_open(&file,exported_profile_path,FA_READ)==FR_OK && f_size(&file)==4096u);
    assert(f_read(&file,persist_check,4096u,&done)==FR_OK && done==4096u && !memcmp(persist_check,persist_wire,4096u));
    assert(f_close(&file)==FR_OK && disconnect_volume());
    assert(!kui_vmu_persist_profile_decode(&persist_verify,persist_check,sizeof(persist_check)));
    assert(kui_vmu_persist_profile_same_ownership(&persist_verify,&persist_profile));
    char sha[65];kui_hex(clone.sha256,32u,sha);
    printf("IMPORT-SOURCE-SHA %s\nIMPORT-PATH %s\nIMPORT-WIRE ",sha,exported_profile_path);
    for(unsigned i=0;i<4096u;i++) printf("%02x",persist_check[i]);
    putchar('\n');
    printf("IMPORT-COUNTS READS %u CID %u RELEASES %u\n",source_reads,identity_reads,before_disconnects);
}
static void partial_directory(void) {
    assert(connect_volume());FRESULT r=f_mkdir("0:/KUI/vmu-test/p0002");assert(r==FR_OK || r==FR_EXIST);
    FIL file;assert(f_open(&file,"0:/KUI/vmu-test/p0002/aborted.part",FA_CREATE_NEW|FA_WRITE)==FR_OK);
    UINT done=0;uint8_t byte=0x5a;assert(f_write(&file,&byte,1u,&done)==FR_OK && done==1u);
    assert(f_sync(&file)==FR_OK && f_close(&file)==FR_OK && disconnect_volume());
    disconnects=identity_reads=raw_connections=phase_reads=0u;
}
int main(int argc,char **argv) {
    if(argc!=3) return 2;
    struct stat info;assert(!stat(argv[1],&info) && info.st_size%512==0);
    image_blocks=(uint64_t)info.st_size/512u;disk_image=fopen(argv[1],"r+b");assert(disk_image);physical_fixture();
    const char *mode=argv[2];
    if(!strcmp(mode,"refuse-source-guards")) {
        source_guard_refusals();assert(!fclose(disk_image));
        puts("PASS physical VMU import: refuse-source-guards");return 0;
    }
    if(!strcmp(mode,"refuse-source-mismatches")) {
        source_compare_refusals();assert(!fclose(disk_image));
        puts("PASS physical VMU import: refuse-source-mismatches");return 0;
    }
    if(!strcmp(mode,"refuse-root-geometries")) {
        source_geometry_refusals();assert(!fclose(disk_image));
        puts("PASS physical VMU import: refuse-root-geometries");return 0;
    }
    bool expected_failure=!strncmp(mode,"refuse-",7u),ok=false;
    if(!strcmp(mode,"import-slot7")) selected_slot=physical_slot=7u;
    bool raw_anchor_refusal=!strcmp(mode,"refuse-anchor");
    uint8_t before[3][32],after[3][32];
    if(!raw_anchor_refusal) hash_existing_pair(before);
    if(!strcmp(mode,"hide-anchor")) {
        assert(connect_volume());
        assert(f_rename(ACTIVE_PROFILE,IMPORT_ROOT "/p0001/anchor-saved.bin")==FR_OK);
        assert(disconnect_volume());hash_existing_pair(after);assert(!memcmp(before,after,sizeof(before)));
        assert(!fclose(disk_image));puts("PASS fixture anchor renamed read-only for import test");return 0;
    }
    if(!strcmp(mode,"skip-partial")) partial_directory();
    unsigned preview_connections=host_connections,preview_reads=host_reads,preview_writes=host_writes;
    if(!strcmp(mode,"import-root-opaque-zero")) opaque_root_fixture(0u);
    if(!strcmp(mode,"import-root-opaque-ff")) opaque_root_fixture(1u);
    if(!strcmp(mode,"import-root-opaque-varied")) opaque_root_fixture(2u);
    if(!strcmp(mode,"refuse-guard-before-read")) drop_source_on_lock=true;
    if(!strcmp(mode,"refuse-guard-after-first-read")) drop_source_at=1u;
    if(!strcmp(mode,"refuse-guard-after-second-read")) drop_source_at=2u;
    if(!strcmp(mode,"refuse-no-vmu")) source_present=false;
    if(!strcmp(mode,"refuse-malformed")) physical_image[255][0]=0u;
    if(!strcmp(mode,"refuse-read")) fail_source_block=255u;
    if(!strcmp(mode,"refuse-busy")) source_busy=1u;
    if(!strcmp(mode,"refuse-response-length")) source_bad_length=1u;
    if(!strcmp(mode,"refuse-short")) source_short=1u;
    if(!strcmp(mode,"refuse-oversize")) source_oversize=1u;
    if(!strcmp(mode,"refuse-mismatch")) source_mismatch=1u;
    memcpy(physical_before,physical_image,sizeof(physical_before));
    if(!strcmp(mode,"refuse-before-preview")) ok=import_after_preview();
    else if(capture_preview()) {
        assert(host_connections==preview_connections && host_reads==preview_reads && host_writes==preview_writes);
        if(!strcmp(mode,"refuse-source-change")) {physical_image[199][17]^=1u;memcpy(physical_before,physical_image,sizeof(physical_before));}
        if(!strcmp(mode,"refuse-slot-change")) source_present=false;
        if(!strcmp(mode,"refuse-copy-read")) fail_source_body=1u;
        if(!strcmp(mode,"refuse-anchor-raw")) fail_raw_connection=1u;
        if(!strcmp(mode,"refuse-raw")) fail_raw_connection=2u;
        if(!strcmp(mode,"refuse-final-raw")) fail_raw_connection=3u;
        if(!strcmp(mode,"refuse-journal-payload")) fault_journal_start=2u;
        if(!strcmp(mode,"refuse-final-journal-payload")) fault_journal_start=3u;
        if(!strcmp(mode,"refuse-cid")) fail_identity=2u;
        if(!strcmp(mode,"refuse-release")) fail_disconnect=1u;
        if(!strcmp(mode,"refuse-final-release")) fail_disconnect=7u;
        ok=import_after_preview();
    }
    assert(!host_connected && !sd_connected && !mounted && !source_locked);
    assert(!memcmp(physical_before,physical_image,sizeof(physical_before)));
    if(ok) new_pair_exact();
    else {
        assert(!exported_profile_path[0]);
        for(unsigned i=0;i<line_count;i++) assert(!strstr(lines[i],"READY:"));
        if(!strcmp(mode,"refuse-malformed")) {
            assert(!source_failure_reason && source_failure_has_root && save_snapshot.image_result==KUI_VMU_IMAGE_GEOMETRY);
            assert(!clone.started && !clone.source_reads && save_snapshot.source_reads==118u && source_reads==236u);
            assert(host_connections==preview_connections && host_reads==preview_reads && host_writes==preview_writes);
            assert(!import_number && !raw_connections && !identity_reads && !disconnects && line_count==8u);
        }
        if(!strcmp(mode,"refuse-read") || source_busy || source_bad_length) {
            assert(!import_number && !raw_connections && !identity_reads && !disconnects);
            assert(source_failure_reason==2u && source_failure_pass==1u && source_failure_has_maple);
            assert(source_failure_block==(!strcmp(mode,"refuse-read")?255u:254u));
            bool reported=false;
            for(unsigned i=0;i<line_count;i++) if(strstr(lines[i],"MAPLE stage ")) reported=true;
            assert(reported);
            if(source_busy) assert(source_failure_maple.stage==2u && source_failure_maple.lock_attempts==7u);
            else if(source_bad_length) assert(source_failure_maple.stage==7u && source_failure_maple.data_len==129u);
            else assert(source_failure_maple.stage==5u && source_failure_maple.result==-5);
        }
        if(!strcmp(mode,"refuse-guard-before-read") || drop_source_at) {
            assert(source_reads==drop_source_at && !import_number && !raw_connections && !identity_reads && !disconnects);
            assert(host_connections==preview_connections && host_reads==preview_reads && host_writes==preview_writes);
            assert(source_failure_reason==(drop_source_at?3u:1u) && source_failure_pass==drop_source_at && source_failure_block==254u);
            assert(source_failure_has_maple==(drop_source_at!=0u));
            assert(source_failure_guard.mask==8u && !clone.started && !clone.source_reads && save_snapshot.last_block==254u);
            bool reported=false;
            for(unsigned i=0;i<line_count;i++) {
                if(!strcmp(lines[i],"GUARD bits 008 slot A1")) reported=true;
                assert(!strstr(lines[i],"MAPLE stage "));
            }
            assert(reported);
        }
        if(!strcmp(mode,"refuse-copy-read") && import_number) {
            fail_disconnect=fail_identity=fail_raw_connection=0u;assert(connect_volume());
            char name[96];FILINFO value;assert(import_path(name,"identity.bin"));
            assert(f_stat(name,&value)==FR_NO_FILE);assert(disconnect_volume());
        }
        if(fault_journal_start) {
            assert(journal_fault_reads>0u);
            if(fault_journal_start==2u) {
                /* A stable nonzero payload would pass generic snapshots. The
                 * stronger initial check must refuse before publishing P2. */
                assert(raw_connections==2u);
                fault_journal_start=0u;assert(connect_volume());
                char name[96];FILINFO value;
                assert(import_path(name,"retail-persistent-profile-0002.bin"));
                assert(f_stat(name,&value)==FR_NO_FILE);assert(disconnect_volume());
            }
        }
    }
    fail_disconnect=fail_identity=fail_raw_connection=0u;
    if(!raw_anchor_refusal) {hash_existing_pair(after);assert(!memcmp(before,after,sizeof(before)));}
    for(unsigned i=0;i<line_count;i++) printf("%s\n",lines[i]);
    assert(fclose(disk_image)==0);
    assert(ok!=expected_failure);printf("PASS physical VMU import: %s\n",mode);return 0;
}
