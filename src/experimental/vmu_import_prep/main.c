/* SPDX-License-Identifier: GPL-3.0-only */
/* Isolated read-only physical VMU save snapshot and new-pair preparation. */
#include "platform.h"
#include "kui/shell_font.h"
#include "kui/vmu_image_clone.h"
#include "kui/vmu_save_snapshot.h"
#include "kui/vmu_read_bounded.h"
#include "kui/vmu_persist_profile.h"
#include "kui/vmu_fs_admission.h"
#include "kui/vmu_backing.h"
#include "kui/media.h"
#include "kui/hash.h"
#include <kos.h>
#include <dc/maple/controller.h>
#include <dc/vmufs.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

KOS_INIT_FLAGS(INIT_IRQ | INIT_CONTROLLER | INIT_VMU | INIT_NO_DCLOAD | INIT_QUIET);
#if !KUI_VMU_PERSIST_IMPORT || KUI_VMU_PERSIST_PREP || KUI_VMU_PERSIST_PROOF
#error Import preparation requires its own isolated build
#endif
#ifndef KUI_BUILD_ID
#define KUI_BUILD_ID "local"
#endif
const uint32_t kui_vmu_import_prep_build_mode=1u;
#define IMPORT_ROOT "0:/KUI/vmu-test"
#define ACTIVE_PROFILE IMPORT_ROOT "/p0001/retail-persistent-profile-0001.bin"
#define LOG_ROWS 14u
#define LOG_COLS 88u
static char lines[LOG_ROWS][LOG_COLS];
static unsigned line_count,selected_slot,import_number;
static bool capture_attempted,preview_ready,import_attempted,sd_connected,mounted,destination_open;
static maple_device_t *source_device;
static uint32_t source_functions;
static char source_name[31];
static unsigned source_failure_block,source_failure_pass,source_failure_reason;
static bool source_failure_has_maple;
static struct kui_vmu_read_bounded_diagnostic source_failure_maple;
static bool source_failure_has_root;
static uint8_t source_failure_root[0x58];
struct source_compare_diagnostic {
    bool valid;
    int comparison;
    unsigned first_index,different,first_byte,second_byte;
    uint32_t first_crc,second_crc;
    struct kui_vmu_read_bounded_diagnostic reads[2];
};
static struct source_compare_diagnostic source_failure_compare;
enum source_guard_bit {
    SOURCE_GUARD_CONTEXT=1u, SOURCE_GUARD_OUTPUT=2u, SOURCE_GUARD_BLOCK=4u,
    SOURCE_GUARD_MISSING=8u, SOURCE_GUARD_REPLACED=16u, SOURCE_GUARD_INVALID=32u,
    SOURCE_GUARD_FUNCTIONS=64u, SOURCE_GUARD_MEMCARD=128u, SOURCE_GUARD_NAME=256u
};
struct source_guard_diagnostic {
    unsigned mask,slot,valid,name_index;
    uintptr_t context,out,expected_device,actual_device;
    uint32_t expected_functions,actual_functions;
    unsigned expected_name,actual_name;
};
static struct source_guard_diagnostic source_failure_guard;
static struct kui_vmu_image_clone clone;
static struct kui_vmu_save_snapshot save_snapshot;
static _Alignas(32) uint8_t source_first[1024],source_second[1024];
static struct kui_vmu_image_workspace image_workspace;
static FATFS volume;
static FIL destination;
static uint8_t image_id[16],persist_wire[4096],persist_check[4096];
static _Alignas(32) uint8_t sector[512];
static struct kui_vmu_persist_profile persist_profile,persist_verify,active_anchor;
static struct kui_vmu_fs_workspace persist_workspace;
static struct kui_vmu_sd_identity import_card;
static uint64_t import_partition_first,import_partition_end;
static bool import_card_pinned;
static char exported_profile_path[96];
static const char *result="Choose source slot, then press X for a read-only preview.";
struct import_raw_reader {const struct kui_vmu_persist_profile *profile;struct kui_media_ops operations;};
static struct import_raw_reader persist_raw;

bool kui_disc_read_pending(void *context) {(void)context;return false;}
bool kui_cancelled(void) {return false;}
static void render(void) {
    for(unsigned i=0;i<640u*480u;i++) vram_s[i]=0x0841;
    kui_shell_font_draw(vram_s,28,24,0xfe2d,"Virtual VMU - import preparation",true);
    kui_shell_font_draw(vram_s,28,56,0xffff,"PHYSICAL VMU READ ONLY / NEW SD PAIR ONLY",false);
    kui_shell_font_draw(vram_s,28,80,0xc618,"Active p0001 and every earlier folder stay unchanged.",false);
    char heading[88];snprintf(heading,sizeof(heading),"Build %.12s   Source %c%u   SCI SD only",KUI_BUILD_ID,
        'A'+selected_slot/2u,selected_slot%2u+1u);
    kui_shell_font_draw(vram_s,28,106,0xc618,heading,false);
    for(unsigned row=0;row<line_count;row++)
        kui_shell_font_draw(vram_s,28,139+(int)row*18,0xffff,lines[row],false);
    kui_shell_font_draw(vram_s,28,418,0xfe2d,result,false);
    kui_shell_font_draw(vram_s,28,444,0xc618,capture_attempted?
        "Source locked for this launch. A imports once after preview.":
        "Left/Right Slot   X Preview   A Import after preview",false);
    vid_waitvbl();vid_flip(-1);
}
void kui_log(const char *format,...) {
    if(line_count==LOG_ROWS) {memmove(lines,lines+1,(LOG_ROWS-1u)*LOG_COLS);--line_count;}
    va_list args;va_start(args,format);
    vsnprintf(lines[line_count++],LOG_COLS,format,args);va_end(args);render();
}
static bool fail(const char *message) {kui_log("FAIL: %s",message);return false;}
static bool source_guard_check(void *context,uint16_t block,void *out,bool check_io) {
    struct source_guard_diagnostic d={0};
    d.slot=selected_slot;d.context=(uintptr_t)context;d.out=(uintptr_t)out;
    d.expected_device=(uintptr_t)source_device;d.expected_functions=source_functions;
    d.name_index=30u;
    if(check_io) {
        if(context!=&source_device) d.mask|=SOURCE_GUARD_CONTEXT;
        if(!out) d.mask|=SOURCE_GUARD_OUTPUT;
        if(block>=256u) d.mask|=SOURCE_GUARD_BLOCK;
    }
    maple_device_t *now=maple_enum_dev((int)(selected_slot/2u),(int)(selected_slot%2u+1u));
    d.actual_device=(uintptr_t)now;
    if(!now) d.mask|=SOURCE_GUARD_MISSING;
    else {
        if(now!=source_device) d.mask|=SOURCE_GUARD_REPLACED;
        d.valid=now->valid;d.actual_functions=now->info.functions;
        if(!d.valid) d.mask|=SOURCE_GUARD_INVALID;
        if(d.actual_functions!=d.expected_functions) d.mask|=SOURCE_GUARD_FUNCTIONS;
        if(!(d.actual_functions&MAPLE_FUNC_MEMCARD)) d.mask|=SOURCE_GUARD_MEMCARD;
        for(unsigned i=0;i<30u;i++) {
            if((uint8_t)now->info.product_name[i]!=(uint8_t)source_name[i]) {
                d.mask|=SOURCE_GUARD_NAME;d.name_index=i;
                d.expected_name=(uint8_t)source_name[i];
                d.actual_name=(uint8_t)now->info.product_name[i];break;
            }
        }
    }
    /* Freeze the first refusal before UI rendering or a later Maple refresh.
     * No source pointer is dereferenced after enumeration has refused it. */
    if(d.mask && !source_failure_guard.mask) source_failure_guard=d;
    return d.mask==0u;
}
static bool source_same(void) {
    return source_guard_check(&source_device,0u,NULL,false);
}
static int source_read_refused(unsigned block,unsigned pass,unsigned reason,bool has_maple) {
    source_failure_block=block;source_failure_pass=pass;source_failure_reason=reason;
    source_failure_has_maple=has_maple;
    if(has_maple) source_failure_maple=*kui_vmu_read_bounded_last();
    return -1;
}
static unsigned source_root16(unsigned offset) {
    return (unsigned)source_failure_root[offset]|(unsigned)source_failure_root[offset+1u]<<8;
}
static void source_failure_report(void) {
    if(source_failure_reason)
        kui_log("READ block %lu pass %lu reason %lu",(unsigned long)source_failure_block,
            (unsigned long)source_failure_pass,(unsigned long)source_failure_reason);
    if(save_snapshot.failed)
        kui_log("SNAP result %d last %lu reads %lu",save_snapshot.result,
            (unsigned long)save_snapshot.last_block,(unsigned long)save_snapshot.source_reads);
    else kui_log("CLONE phase %lu last %lu reads %lu",(unsigned long)clone.phase,
        (unsigned long)clone.last_block,(unsigned long)clone.source_reads);
    if(source_failure_has_root) {
        char magic[33],opaque40[13],opaque52[13];
        kui_hex(source_failure_root,16u,magic);
        kui_hex(source_failure_root+0x40,6u,opaque40);
        kui_hex(source_failure_root+0x52,6u,opaque52);
        kui_log("ROOT magic %s custom %u",magic,(unsigned)source_failure_root[16]);
        kui_log("ROOT FAT %u/%u DIR %u/%u USER %u ICON %u",source_root16(0x46),
            source_root16(0x48),source_root16(0x4a),source_root16(0x4c),
            source_root16(0x50),source_root16(0x4e));
        kui_log("ROOT opaque 40-45 %s",opaque40);
        kui_log("ROOT opaque 52-57 %s",opaque52);
        return;
    }
    if(source_failure_reason==5u && source_failure_compare.valid) {
        const struct source_compare_diagnostic *c=&source_failure_compare;
        kui_log("COMPARE %smemcmp %d",c->different && c->comparison?"":"anomaly ",c->comparison);
        kui_log("DIFF first %lu count %lu bytes %02lx/%02lx",(unsigned long)c->first_index,
            (unsigned long)c->different,(unsigned long)c->first_byte,(unsigned long)c->second_byte);
        kui_log("CRC32 first %08lx second %08lx",(unsigned long)c->first_crc,(unsigned long)c->second_crc);
        for(unsigned pass=0;pass<2u;pass++) {
            const struct kui_vmu_read_bounded_diagnostic *d=&c->reads[pass];
            kui_log("PASS%u stage %lu result %d type %d words %lu",pass+1u,(unsigned long)d->stage,
                d->result,d->response,(unsigned long)d->data_len);
            kui_log("PASS%u func %08lx/%08lx block %08lx/%08lx",pass+1u,
                (unsigned long)d->expected_function,(unsigned long)d->actual_function,
                (unsigned long)d->expected_location,(unsigned long)d->actual_location);
            kui_log("PASS%u RX %08lx/%08lx COPY %08lx",pass+1u,
                (unsigned long)d->raw_crc_before,(unsigned long)d->raw_crc_after,
                (unsigned long)d->copy_crc);
        }
        return;
    }
    if(source_failure_guard.mask) {
        const struct source_guard_diagnostic *g=&source_failure_guard;
        kui_log("GUARD bits %03lx slot %c%u",(unsigned long)g->mask,
            'A'+g->slot/2u,g->slot%2u+1u);
        kui_log("CTX expected %08lx got %08lx",(unsigned long)(uintptr_t)&source_device,
            (unsigned long)g->context);
        kui_log("OUT %08lx",(unsigned long)g->out);
        kui_log("DEVICE pinned %08lx current %08lx",(unsigned long)g->expected_device,
            (unsigned long)g->actual_device);
        kui_log("VALID %lu FUNC pin %08lx now %08lx",(unsigned long)g->valid,
            (unsigned long)g->expected_functions,(unsigned long)g->actual_functions);
        if(g->mask&SOURCE_GUARD_NAME)
            kui_log("NAME byte %lu expected %02lx got %02lx",(unsigned long)g->name_index,
                (unsigned long)g->expected_name,(unsigned long)g->actual_name);
        return;
    }
    if(!source_failure_has_maple) return;
    const struct kui_vmu_read_bounded_diagnostic *d=&source_failure_maple;
    kui_log("MAPLE stage %lu result %d locks %lu",(unsigned long)d->stage,d->result,
        (unsigned long)d->lock_attempts);
    kui_log("WAIT %d state %lu queued %lu",d->wait_result,(unsigned long)d->frame_state,
        (unsigned long)d->queued);
    kui_log("REPLY type %d words %lu",d->response,(unsigned long)d->data_len);
    kui_log("FUNC expected %08lx got %08lx",(unsigned long)d->expected_function,
        (unsigned long)d->actual_function);
    kui_log("BLOCK expected %08lx got %08lx",(unsigned long)d->expected_location,
        (unsigned long)d->actual_location);
}
/* This is the only physical VMU operation in the application. */
int __attribute__((noipa)) kui_vmu_import_source_read(void *context,uint16_t block,void *out) {
    if(!source_guard_check(context,block,out,true))
        return source_read_refused(block,0u,1u,false);
    /* The scoped BREAD helper validates exact framing before copying. These
     * independent guards also reject short/changed/oversize adapter results. */
    memset(source_first,0xa5,sizeof(source_first));memset(source_second,0x5a,sizeof(source_second));
    if(kui_vmu_read_bounded(source_device,block,source_first)!=0)
        return source_read_refused(block,1u,2u,true);
    const struct kui_vmu_read_bounded_diagnostic first_read=*kui_vmu_read_bounded_last();
    if(!source_same()) return source_read_refused(block,1u,3u,true);
    if(kui_vmu_read_bounded(source_device,block,source_second)!=0)
        return source_read_refused(block,2u,2u,true);
    if(!source_same()) return source_read_refused(block,2u,3u,true);
    for(unsigned i=512u;i<1024u;i++) if(source_first[i]!=0xa5u || source_second[i]!=0x5au)
        return source_read_refused(block,0u,4u,false);
    struct source_compare_diagnostic comparison={.first_index=512u,
        .comparison=memcmp(source_first,source_second,512u)};
    /* Independent volatile loads distinguish changed bytes from a compare
     * anomaly. Either disagreement still refuses; no source bytes reach out. */
    const volatile uint8_t *first=source_first,*second=source_second;
    for(unsigned i=0;i<512u;i++) {
        unsigned a=first[i],b=second[i];
        if(a==b) continue;
        if(!comparison.different) {
            comparison.first_index=i;comparison.first_byte=a;comparison.second_byte=b;
        }
        ++comparison.different;
    }
    if(comparison.comparison || comparison.different) {
        /* Freeze both data summaries and bounded reply headers before any
         * logging can yield to a Maple refresh or reuse these scratch bytes. */
        comparison.valid=true;
        comparison.first_crc=kui_crc32(0,source_first,512u);
        comparison.second_crc=kui_crc32(0,source_second,512u);
        comparison.reads[0]=first_read;
        comparison.reads[1]=*kui_vmu_read_bounded_last();
        if(!source_failure_compare.valid) source_failure_compare=comparison;
        return source_read_refused(block,0u,5u,false);
    }
    memcpy(out,source_first,512u);
    return 0;
}
static bool capture_preview(void) {
    capture_attempted=true;preview_ready=false;line_count=0;
    source_failure_reason=0u;source_failure_has_maple=false;source_failure_has_root=false;
    memset(&source_failure_guard,0,sizeof(source_failure_guard));
    memset(&source_failure_compare,0,sizeof(source_failure_compare));
    source_device=maple_enum_dev((int)(selected_slot/2u),(int)(selected_slot%2u+1u));
    if(!source_device || !source_device->valid || !(source_device->info.functions&MAPLE_FUNC_MEMCARD))
        return fail("No memory card in the selected slot; restart to retry.");
    source_functions=source_device->info.functions;
    memcpy(source_name,source_device->info.product_name,30u);source_name[30]=0;
    kui_log("Reading saves and metadata; keep the VMU connected.");
    if(vmufs_mutex_lock()!=0) return fail("Cannot lock source VMU for read-only capture.");
    const struct kui_vmu_image_io io={&source_device,kui_vmu_import_source_read,NULL,NULL};
    int status=kui_vmu_save_snapshot_capture(&save_snapshot,&io,KUI_VMU_IMAGE_BYTES);
    const struct kui_vmu_image_io canonical={&save_snapshot,kui_vmu_save_snapshot_read_block,NULL,NULL};
    if(!status) status=kui_vmu_image_clone_capture(&clone,&canonical,KUI_VMU_IMAGE_BYTES);
    /* GEOMETRY exits validation with the already-read root in this workspace.
     * Freeze only its header before unlock/render; no diagnostic card read. */
    if(save_snapshot.image_result==KUI_VMU_IMAGE_GEOMETRY) {
        memcpy(source_failure_root,save_snapshot.image.block,sizeof(source_failure_root));
        source_failure_has_root=true;
    }
    int unlocked=vmufs_mutex_unlock();
    if(status || unlocked || !source_same()) {
        kui_log("Capture refused: status %d, image %d",status,
            save_snapshot.image_result?save_snapshot.image_result:clone.image_result);
        source_failure_report();
        return fail("No SD files created. Photograph and restart.");
    }
    char hash[17];kui_hex(clone.sha256,8u,hash);line_count=0;
    kui_log("Source %c%u: %.30s",'A'+selected_slot/2u,selected_slot%2u+1u,source_name);
    kui_log("Preview: %u saves, %u used / %u free blocks",clone.summary.files,
        clone.summary.used_blocks,clone.summary.free_blocks);
    kui_log("Saves pinned; unused blocks zeroed. SHA %.16s",hash);
    for(unsigned i=0;i<clone.summary.files && i<4u;i++) {
        char name[13];memcpy(name,clone.image.names[i],12u);name[12]=0;
        kui_log("Save %u: %s",i+1u,name);
    }
    kui_log("A creates a separate new p0002-or-later pair.");
    kui_log("Physical VMU and active p0001 are never written.");
    preview_ready=true;return true;
}
static bool import_path(char out[96],const char *leaf) {
    if(import_number<2u || import_number>9999u) return false;
    int n=snprintf(out,96,IMPORT_ROOT "/p%04u/%s",import_number,leaf);
    return n>0 && n<96;
}
static bool disconnect_checked(void) {
    if(!sd_connected) return true;
    bool ok=kui_sd_vmu_import_disconnect();sd_connected=false;
    return ok;
}
static bool disconnect_volume(void) {
    bool ok=true;
    if(destination_open) {if(f_close(&destination)!=FR_OK) ok=false;destination_open=false;}
    if(mounted) {if(f_mount(NULL,"0:",0)!=FR_OK) ok=false;mounted=false;}
    if(!disconnect_checked()) ok=false;
    return ok;
}
static bool connect_volume(void) {
    if(sd_connected || mounted) return fail("Unexpected active storage connection.");
    kui_sd_set_params(KUI_STORAGE_SCI,true);
    if(!kui_sd_connect()) return fail("SCI SD connection.");
    sd_connected=true;
    mounted=true;
    if(!kui_mount(&volume,kui_log)) return fail("SCI filesystem mount.");
    return true;
}
static bool target_card_matches(void) {
    struct kui_vmu_sd_identity current;
    return import_card_pinned && kui_sd_vmu_profile_identity(&current) &&
        current.sectors==import_card.sectors && current.high_capacity==import_card.high_capacity &&
        !memcmp(current.cid,import_card.cid,16u);
}
static bool pin_target_card(void) {
    const struct kui_volume *view=kui_media_volume();
    if(import_card_pinned || !view || !view->count || !kui_sd_vmu_profile_identity(&import_card))
        return fail("Initial SCI card identity/partition.");
    import_partition_first=view->start;import_partition_end=(uint64_t)view->start+view->count;
    import_card_pinned=true;return true;
}
static bool new_directory(void) {
    const char *parents[]={"0:/KUI",IMPORT_ROOT};
    for(unsigned i=0;i<2u;i++) {
        FRESULT r=f_mkdir(parents[i]);
        if(r!=FR_OK && r!=FR_EXIST) return fail("Cannot create import parent directory.");
    }
    for(unsigned n=2u;n<=9999u;n++) {
        char path[64];snprintf(path,sizeof(path),IMPORT_ROOT "/p%04u",n);
        FRESULT r=f_mkdir(path);
        if(r==FR_OK) {import_number=n;return true;}
        if(r!=FR_EXIST) return fail("Cannot create a new import directory.");
    }
    return fail("No unused p0002-or-later directory remains.");
}
static bool make_zero_file(const char *leaf,unsigned blocks) {
    char name[96];FIL file;bool ok=true;
    if(!import_path(name,leaf) || f_open(&file,name,FA_CREATE_NEW|FA_WRITE)!=FR_OK)
        return fail("Cannot create a fresh import file.");
    memset(sector,0,512u);
    for(unsigned i=0;i<blocks;i++) {
        UINT done=0;
        if(f_write(&file,sector,512u,&done)!=FR_OK || done!=512u) {ok=false;break;}
    }
    FRESULT synced=f_sync(&file),closed=f_close(&file);
    if(!ok || synced!=FR_OK || closed!=FR_OK) return fail("New import file write/sync/close.");
    if(f_open(&file,name,FA_READ)!=FR_OK) return fail("New import file verification open.");
    ok=f_size(&file)==(FSIZE_t)blocks*512u;
    for(unsigned i=0;ok && i<blocks;i++) {
        UINT done=0;
        if(f_read(&file,sector,512u,&done)!=FR_OK || done!=512u) {ok=false;break;}
        for(unsigned j=0;j<512u;j++) if(sector[j]) {ok=false;break;}
    }
    closed=f_close(&file);
    return ok && closed==FR_OK?true:fail("New import file is not exactly zero.");
}
static int destination_read(void *context,uint16_t block,void *out) {
    if(context!=&destination || !destination_open || block>=256u || f_size(&destination)!=131072u) return -1;
    UINT done=0;return f_lseek(&destination,(FSIZE_t)block*512u)==FR_OK &&
        f_read(&destination,out,512u,&done)==FR_OK && done==512u?0:-1;
}
static int destination_write(void *context,uint16_t block,const void *in) {
    if(context!=&destination || !destination_open || block>=256u || f_size(&destination)!=131072u) return -1;
    UINT done=0;return f_lseek(&destination,(FSIZE_t)block*512u)==FR_OK &&
        f_write(&destination,in,512u,&done)==FR_OK && done==512u?0:-1;
}
static int destination_sync(void *context) {
    return context==&destination && destination_open && f_sync(&destination)==FR_OK?0:-1;
}
static bool publish_identity_last(void) {
    struct kui_sha256 hash;uint8_t digest[32],binding[8]={0};kui_sha256_init(&hash);
    static const char label[]="K-UI read-only physical VMU clone identity v1";
    binding[0]=(uint8_t)import_number;binding[1]=(uint8_t)(import_number>>8);binding[2]=(uint8_t)selected_slot;
    kui_sha256_update(&hash,label,sizeof(label)-1u);kui_sha256_update(&hash,clone.sha256,32u);
    kui_sha256_update(&hash,binding,sizeof(binding));kui_sha256_digest(&hash,digest);
    memcpy(image_id,digest,16u);
    char part[96],final[96];FIL file;UINT done=0;
    if(!import_path(part,"identity.part") || !import_path(final,"identity.bin") ||
       f_open(&file,part,FA_CREATE_NEW|FA_WRITE)!=FR_OK) return fail("New identity staging.");
    FRESULT written=f_write(&file,image_id,16u,&done),synced=f_sync(&file),closed=f_close(&file);
    if(written!=FR_OK || synced!=FR_OK || closed!=FR_OK || done!=16u) return fail("Identity staging write.");
    if(f_open(&file,part,FA_READ)!=FR_OK) return fail("Identity staging readback open.");
    uint8_t check[17];done=0;
    FRESULT read=f_read(&file,check,sizeof(check),&done);bool exact=f_size(&file)==16u;
    closed=f_close(&file);
    if(read!=FR_OK || closed!=FR_OK || !exact || done!=16u || memcmp(check,image_id,16u))
        return fail("Identity staging exact readback.");
    return f_rename(part,final)==FR_OK?true:fail("Identity final publication.");
}
static bool persist_card_matches(const struct kui_vmu_fs_input *input) {
    struct kui_vmu_sd_identity actual;
    return kui_sd_vmu_profile_identity(&actual) && actual.sectors==input->card_sectors &&
        (unsigned)actual.high_capacity==input->high_capacity && !memcmp(actual.cid,input->cid,16u);
}
static int persist_read_sector(void *context,uint32_t lba,uint8_t out[512]) {
    struct import_raw_reader *reader=context;
    return reader->operations.read(reader->operations.ctx,lba,1u,out);
}
static int persist_read_role(void *context,unsigned role,uint32_t block,uint8_t out[512]) {
    struct import_raw_reader *reader=context;uint32_t lba;
    if(kui_vmu_persist_profile_map_lba(reader->profile,role,block,&lba)) return -1;
    return persist_read_sector(context,lba,out);
}
static int persist_image_read(void *context,uint16_t block,void *out) {
    return persist_read_role(context,KUI_VMU_PERSIST_BASE,block,out);
}
/* A newly imported pair has never held a transaction. The generic P2 snapshot
 * permits an old payload beneath a zero marker, but this tool promises a new
 * all-zero journal, including its first sector, at both raw checkpoints. */
static bool persist_journal_is_zero(void) {
    for(unsigned block=0;block<2u;block++) {
        if(persist_read_role(&persist_raw,KUI_VMU_PERSIST_JOURNAL,block,sector))
            return fail("Imported journal raw verification read.");
        for(unsigned i=0;i<512u;i++) if(sector[i])
            return fail("Imported journal must be exactly 1024 zero bytes.");
    }
    return true;
}
static bool persist_open_raw(const struct kui_vmu_fs_input *input) {
    if(sd_connected || mounted || !kui_sd_connect()) return fail("Fresh raw SCI connection.");
    sd_connected=true;
    if(!persist_card_matches(input) || !kui_sd_raw_read_ops(&persist_raw.operations) ||
       !persist_raw.operations.blocks || !persist_raw.operations.read || persist_raw.operations.write ||
       persist_raw.operations.sync || persist_raw.operations.blocks(persist_raw.operations.ctx)!=input->card_sectors)
        return fail("Raw SCI identity/read-only ownership.");
    return true;
}
static bool persist_scan_result(int status,const char *phase) {
    if(!status) return true;
    const struct kui_vmu_fs_stats *s=&persist_workspace.stats;
    kui_log("FS %s status %d detail %lu",phase,status,(unsigned long)s->detail);
    kui_log("LBA %08lx reads %lu chain %lu",(unsigned long)s->last_lba,
        (unsigned long)s->reads,(unsigned long)s->chain_steps);
    if(s->boot.valid) kui_log("BOOT flags %04lx reject %02lx",(unsigned long)s->boot.flags,
        (unsigned long)s->boot.reject_mask);
    if(s->link.valid) kui_log("LINK %08lx -> %08lx decision %lu",(unsigned long)s->link.source_cluster,
        (unsigned long)s->link.target_cluster,(unsigned long)s->link.decision);
    return false;
}
bool __attribute__((noipa)) kui_vmu_import_prepare_export(void) {
    bool ok=false,open=false;FIL file;UINT done=0;
    struct kui_vmu_fs_input input={.read_sector=persist_read_sector,.context=&persist_raw};
    char pair_path[96];exported_profile_path[0]=0;
    if(import_number<2u || !clone.completed || !connect_volume()) goto finished;
    const struct kui_volume *view=kui_media_volume();struct kui_vmu_sd_identity actual;
    if(!view || !view->count || !kui_sd_vmu_profile_identity(&actual) || !import_card_pinned ||
       actual.sectors!=import_card.sectors || actual.high_capacity!=import_card.high_capacity ||
       memcmp(actual.cid,import_card.cid,16u) || view->start!=import_partition_first ||
       (uint64_t)view->start+view->count!=import_partition_end) {
        fail("Import card identity/partition unavailable.");goto finished;
    }
    input.card_sectors=actual.sectors;input.partition_first=view->start;
    input.partition_end=(uint64_t)view->start+view->count;input.high_capacity=actual.high_capacity?1u:0u;
    memcpy(input.cid,actual.cid,16u);memcpy(input.image_id,image_id,16u);
    int n=snprintf(pair_path,sizeof(pair_path),"/KUI/vmu-test/p%04u",import_number);
    if(n<=0 || (size_t)n>=sizeof(pair_path)) {fail("Import pair path.");goto finished;}
    input.pair_path=pair_path;
    if(!disconnect_volume() || !persist_open_raw(&input)) goto finished;
    kui_log("Checking new allocation; keep power on.");
    if(!persist_scan_result(kui_vmu_fs_collect(&input,&persist_profile,&persist_workspace),"SOURCE")) goto finished;
    persist_raw.profile=&persist_profile;
    if(!persist_journal_is_zero() ||
       kui_vmu_persist_profile_collect_snapshot(&persist_profile,persist_read_role,&persist_raw) ||
       memcmp(persist_profile.base_sha256,clone.sha256,32u) ||
       memcmp(persist_profile.block_crc,clone.block_crc,sizeof(clone.block_crc))) {
        fail("Raw clone bytes or clean journal verification.");goto finished;
    }
    const struct kui_vmu_image_io io={&persist_raw,persist_image_read,NULL,NULL};
    struct kui_vmu_image_summary summary;
    if(kui_vmu_image_validate(&io,131072u,&image_workspace,&summary) ||
       summary.files!=clone.summary.files || summary.used_blocks!=clone.summary.used_blocks ||
       !persist_card_matches(&input)) {fail("Raw imported VMU validation/card identity.");goto finished;}
    if(!disconnect_checked()) {fail("Initial raw SCI release/restoration.");goto finished;}
    if(kui_vmu_persist_profile_encode(&persist_profile,persist_wire) ||
       !connect_volume() || !persist_card_matches(&input)) {fail("Profile output admission.");goto finished;}
    view=kui_media_volume();
    if(!view || view->start!=input.partition_first || (uint64_t)view->start+view->count!=input.partition_end) {
        fail("Profile output partition changed.");goto finished;
    }
    char leaf[48];snprintf(leaf,sizeof(leaf),"retail-persistent-profile-%04u.bin",import_number);
    if(!import_path(exported_profile_path,leaf) || f_open(&file,exported_profile_path,FA_CREATE_NEW|FA_WRITE)!=FR_OK) {
        fail("Create-new P2 output.");goto finished;
    }
    open=true;FRESULT written=f_write(&file,persist_wire,sizeof(persist_wire),&done);
    FRESULT synced=f_sync(&file),closed=f_close(&file);open=false;
    if(written!=FR_OK || synced!=FR_OK || closed!=FR_OK || done!=sizeof(persist_wire)) {
        fail("P2 write/sync/close.");goto finished;
    }
    if(f_open(&file,exported_profile_path,FA_READ)!=FR_OK) {fail("P2 readback open.");goto finished;}
    open=true;done=0;FRESULT read=f_read(&file,persist_check,sizeof(persist_check),&done);
    bool exact=f_size(&file)==sizeof(persist_check) && read==FR_OK && done==sizeof(persist_check) &&
        !memcmp(persist_wire,persist_check,sizeof(persist_wire));
    closed=f_close(&file);open=false;
    if(!exact || closed!=FR_OK || !disconnect_volume()) {fail("P2 readback/unmount/release.");goto finished;}
    kui_log("Rechecking ownership and clone bytes after profile allocation.");
    if(!persist_open_raw(&input) ||
       !persist_scan_result(kui_vmu_fs_collect(&input,&persist_verify,&persist_workspace),"FINAL") ||
       !kui_vmu_persist_profile_same_ownership(&persist_profile,&persist_verify) ||
       !persist_journal_is_zero() ||
       kui_vmu_persist_profile_verify_snapshot(&persist_profile,persist_read_role,&persist_raw) ||
       !persist_card_matches(&input)) {fail("Final ownership/content/card check.");goto finished;}
    ok=true;
finished:
    if(open && f_close(&file)!=FR_OK) ok=false;
    if(!disconnect_volume()) {fail("Final SCI release/restoration.");ok=false;}
    if(!ok) exported_profile_path[0]=0;
    if(ok) {
        char prefix[17];kui_hex(persist_profile.base_sha256,8u,prefix);line_count=0;
        kui_log("NEW PAIR /KUI/vmu-test/p%04u",import_number);
        kui_log("Source %c%u: %u saves, %u used blocks",'A'+selected_slot/2u,selected_slot%2u+1u,
            clone.summary.files,clone.summary.used_blocks);
        kui_log("Canonical save image SHA %.16s",prefix);
        kui_log("P2 PROFILE: %s",exported_profile_path+2u);
        kui_log("Physical VMU read only; p0001 unchanged.");
        kui_log("READY: photograph and upload this new .bin.");
        kui_log("Retail loader is unchanged; this card is not installed yet.");
    }
    return ok;
}
/* This first proof requires the already-working card's P2 as a read-only
 * ownership anchor BEFORE allowing FatFs to allocate any new cluster. Its
 * preparation hashes and retained journal are deliberately not replayed or
 * compared with saved contents. A cleared live allocation bit must refuse
 * before a filesystem allocator could reuse the active card's data. */
bool __attribute__((noipa)) kui_vmu_import_active_admit(void) {
    FIL file;UINT done=0;
    if(!mounted || !import_card_pinned || f_open(&file,ACTIVE_PROFILE,FA_READ)!=FR_OK)
        return fail("Active p0001 P2 anchor is required before import.");
    bool exact=f_size(&file)==sizeof(persist_check) &&
        f_read(&file,persist_check,sizeof(persist_check),&done)==FR_OK && done==sizeof(persist_check);
    FRESULT closed=f_close(&file);
    if(!exact || closed!=FR_OK || kui_vmu_persist_profile_decode(&active_anchor,persist_check,sizeof(persist_check)) ||
       strcmp(active_anchor.pair_path,"/KUI/vmu-test/p0001") ||
       active_anchor.card_sectors!=import_card.sectors ||
       active_anchor.high_capacity!=(unsigned)import_card.high_capacity ||
       memcmp(active_anchor.cid,import_card.cid,16u) ||
       active_anchor.partition_first!=import_partition_first || active_anchor.partition_end!=import_partition_end)
        return fail("Active P2 identity/partition/format mismatch.");
    struct kui_vmu_fs_input input={.read_sector=persist_read_sector,.context=&persist_raw,
        .card_sectors=import_card.sectors,.partition_first=import_partition_first,
        .partition_end=import_partition_end,.high_capacity=import_card.high_capacity?1u:0u,
        .pair_path=active_anchor.pair_path};
    memcpy(input.cid,import_card.cid,16u);memcpy(input.image_id,active_anchor.image_id,16u);
    if(!disconnect_volume() || !persist_open_raw(&input)) return false;
    kui_log("Checking active p0001 ownership before any new allocation.");
    if(!persist_scan_result(kui_vmu_fs_admit(&input,&active_anchor,&persist_workspace),"ACTIVE") ||
       !persist_card_matches(&input)) return fail("Active allocation/identity refused; no files created.");
    if(!disconnect_checked()) return fail("Active admission SCI release/restoration.");
    if(!connect_volume() || !target_card_matches()) return fail("SCI card changed after active admission.");
    const struct kui_volume *view=kui_media_volume();
    if(!view || view->start!=import_partition_first || (uint64_t)view->start+view->count!=import_partition_end)
        return fail("SCI partition changed after active admission.");
    return true;
}
static bool import_after_preview(void) {
    bool ok=false,locked=false;import_attempted=true;
    const uint32_t mode=*(const volatile uint32_t *)&kui_vmu_import_prep_build_mode;
    if(mode!=1u) return fail("Import build identity refused.");
    if(!preview_ready || clone.failed || !source_same()) return fail("A requires the unchanged previewed VMU.");
    kui_log("Rechecking source before creating any SD file.");
    if(vmufs_mutex_lock()!=0) return fail("Source VMU lock.");
    locked=true;
    if(kui_vmu_image_clone_verify_source(&clone)) {
        fail("Source recheck failed; no SD files created.");source_failure_report();goto finished;
    }
    if(!connect_volume() || !pin_target_card() || !kui_vmu_import_active_admit() || !new_directory()) goto finished;
    kui_log("Creating NEW /KUI/vmu-test/p%04u only.",import_number);
    if(!make_zero_file("card.vmd",256u)) goto finished;
    char path[96];
    if(!import_path(path,"card.vmd") || f_open(&destination,path,FA_READ|FA_WRITE)!=FR_OK) {
        fail("Open newly created clone destination.");goto finished;
    }
    destination_open=true;
    const struct kui_vmu_image_io io={&destination,destination_read,destination_write,destination_sync};
    int copied=kui_vmu_image_clone_copy(&clone,&io,131072u);
    FRESULT closed=f_close(&destination);destination_open=false;
    if(copied || closed!=FR_OK) {fail("Exact clone copy/VMU verification.");source_failure_report();goto finished;}
    if(vmufs_mutex_unlock()!=0) {locked=false;fail("Source VMU unlock.");goto finished;}
    locked=false;
    if(!make_zero_file("card.journal",2u)) goto finished;
    if(!target_card_matches()) {fail("SCI card changed before identity publication.");goto finished;}
    if(!publish_identity_last()) goto finished;
    if(!disconnect_volume()) {fail("New pair unmount/SCI release.");goto finished;}
    ok=kui_vmu_import_prepare_export();
finished:
    if(locked && vmufs_mutex_unlock()!=0) ok=false;
    if(!disconnect_volume()) ok=false;
    if(!ok) exported_profile_path[0]=0;
    return ok;
}
static uint32_t buttons(void) {
    maple_device_t *controller=maple_enum_type(0,MAPLE_FUNC_CONTROLLER);
    cont_state_t *state=controller?maple_dev_status(controller):NULL;
    return state?state->buttons:0u;
}
int main(void) {
    vid_set_mode(DM_640x480|DM_MULTIBUFFER,PM_RGB565);kui_storage_boot_begin();
    kui_log("Select the physical VMU to copy with Left/Right.");
    kui_log("X previews it without any SD writes. Then A creates a new pair.");
    kui_log("Only SCI SD is supported. No physical VMU write exists.");
    const uint32_t mode=*(const volatile uint32_t *)&kui_vmu_import_prep_build_mode;
    if(mode!=1u) {
        capture_attempted=import_attempted=true;
        result="BUILD REFUSED. Photograph and restart.";
        fail("Import build identity must be one.");
    }
    uint32_t previous=buttons();
    for(;;) {
        uint32_t now=buttons(),pressed=now&~previous;previous=now;
        if(!capture_attempted && (pressed&(CONT_DPAD_LEFT|CONT_DPAD_RIGHT)))
            selected_slot=(selected_slot+((pressed&CONT_DPAD_RIGHT)?1u:7u))%8u;
        if(!capture_attempted && (pressed&CONT_X)) {
            result="Reading source; keep the physical VMU connected.";render();
            bool ok=capture_preview();
            result=ok?"PREVIEW READY. A creates the separate SD clone.":
                "FAILED. No import. Photograph and restart.";
            previous=buttons();
        } else if(preview_ready && !import_attempted && (pressed&CONT_A)) {
            result="Copying and checking ownership; keep power on.";render();
            bool ok=import_after_preview();
            result=ok?"IMPORT / READY. Photograph; upload the new .bin.":
                "FAILED. Preserve files; photograph and restart.";
            previous=buttons();
        }
        render();thd_sleep(16);
    }
    return 0;
}
