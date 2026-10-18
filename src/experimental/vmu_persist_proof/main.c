/* SPDX-License-Identifier: GPL-3.0-only */
/* Owned KOS physical-journal proof. No retail hook or physical VMU writes. */
#include "platform.h"
#include "kui/shell_font.h"
#include "kui/media.h"
#include "kui/vmu_backing.h"
#include "kui/vmu_persist_session.h"
#include "kui/vmu_import.h"
#include "kui/crc16.h"
#include "kui/hash.h"
#include <kos.h>
#include <dc/maple/controller.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

KOS_INIT_FLAGS(INIT_IRQ | INIT_CONTROLLER | INIT_NO_DCLOAD | INIT_QUIET);
#if !KUI_VMU_PERSIST_PROOF || KUI_VMU_PERSIST_PREP
#error Physical proof requires its own explicit isolated build
#endif
#ifndef KUI_BUILD_ID
#define KUI_BUILD_ID "local"
#endif
const uint32_t kui_vmu_persist_proof_build_mode=1u;
#define PROFILE_PATH "0:/KUI/vmu-test/p0001/retail-persistent-profile-0001.bin"
#define PROOF_BLOCK 199u
#define LOG_ROWS 15u
#define LOG_COLS 96u
static FATFS volume;
static struct kui_media_ops raw;
static struct kui_vmu_persist_profile profile;
static struct kui_vmu_persist_session session;
static struct kui_vmu_fs_workspace fs_workspace;
static struct kui_vmu_image_workspace image_workspace;
static uint8_t profile_wire[4096],fixture[1024],observed[1024];
static _Alignas(32) uint8_t sector[512];
static uint32_t before_crc[256];
static char lines[LOG_ROWS][LOG_COLS];
static unsigned line_count;
static const char *result="A commits one proof block; X verifies after restart.";

bool kui_disc_read_pending(void *context) {(void)context;return false;}
bool kui_cancelled(void) {return false;}
static void render(void) {
    for(unsigned i=0;i<640u*480u;i++) vram_s[i]=0x0841;
    kui_shell_font_draw(vram_s,28,24,0xfe2d,"Virtual VMU - physical journal proof",true);
    kui_shell_font_draw(vram_s,28,56,0xffff,"OWNED KUIPROOF SAVE / REAL IMAGE AND JOURNAL WRITES",false);
    kui_shell_font_draw(vram_s,28,80,0xc618,"Retail saving is not enabled by this tool.",false);
    char title[96];snprintf(title,sizeof(title),"Build %.12s   SCI   /KUI/vmu-test/p0001",KUI_BUILD_ID);
    kui_shell_font_draw(vram_s,28,106,0xc618,title,false);
    for(unsigned row=0;row<line_count;row++)
        kui_shell_font_draw(vram_s,28,139+(int)row*18,0xffff,lines[row],false);
    kui_shell_font_draw(vram_s,28,418,0xfe2d,result,false);
    kui_shell_font_draw(vram_s,28,444,0xc618,"A Commit generation   X Reopen and verify   SCI only",false);
    vid_waitvbl();vid_flip(-1);
}
void kui_log(const char *format,...) {
    if(line_count==LOG_ROWS) {memmove(lines,lines+1,(LOG_ROWS-1u)*LOG_COLS);--line_count;}
    va_list args;va_start(args,format);
    vsnprintf(lines[line_count++],LOG_COLS,format,args);va_end(args);render();
}
static bool fail(const char *message) {kui_log("FAIL: %s",message);return false;}
static void put16(uint8_t *p,unsigned value) {p[0]=(uint8_t)value;p[1]=(uint8_t)(value>>8);}
static void put32(uint8_t *p,uint32_t value) {for(unsigned i=0;i<4u;i++) p[i]=(uint8_t)(value>>(i*8u));}
/* Exact pre-existing nonproprietary fixture made by the original owned tool.
 * Only short-description bytes14/15 and CRC16 bytes70/71 may change. All four
 * bytes are in the same512-byte VMU block; the package remains CRC-valid. */
static void pattern(unsigned generation,uint8_t out[1024]) {
    static const char digits[]="0123456789ABCDEF";
    memset(out,0,1024u);memcpy(out,"K-UI VMU proof",14u);
    memcpy(out+16u,"Owned emulation persistence test",31u);
    memcpy(out+48u,"KUI-VMU-PROOF",13u);put16(out+64u,1u);put32(out+72u,64u);
    for(unsigned i=96u;i<640u;i++) out[i]=(uint8_t)(i*37u+(i>>2));
    memcpy(out+640u,"KUI virtual VMU allocated save proof v2",38u);put32(out+680u,1u);
    for(unsigned i=684u;i<704u;i++) out[i]=(uint8_t)(i^1u);
    if(generation) {out[14]=digits[generation>>4];out[15]=digits[generation&15u];}
    put16(out+70u,kui_crc16_nibble(0,out,704u));
}
static int hex_digit(unsigned value) {
    if(value>='0' && value<='9') return (int)(value-'0');
    if(value>='A' && value<='F') return (int)(value-'A'+10u);
    return -1;
}
static bool fixture_generation(unsigned *generation) {
    if(kui_vmu_persist_session_read(&session,199u,observed) ||
       kui_vmu_persist_session_read(&session,198u,observed+512u)) return fail("proof package read");
    unsigned current=0;
    if(observed[14] || observed[15]) {
        int hi=hex_digit(observed[14]),lo=hex_digit(observed[15]);
        if(hi<0 || lo<0 || !(current=(unsigned)(hi*16+lo))) return fail("unrecognized proof generation");
    }
    pattern(current,fixture);
    if(memcmp(observed,fixture,sizeof(fixture))) return fail("save differs from the owned KUIPROOF fixture");
    char error[128];struct kui_vmu_save_info parsed;
    if(!kui_vmu_save_parse(KUI_VMU_SAVE_VMS,observed,sizeof(observed),NULL,"KUIPROOF",&parsed,error,sizeof(error)) ||
       parsed.padded_bytes!=1024u) return fail("proof package CRC16 / size");
    if(kui_vmu_persist_session_read(&session,254u,sector) ||
       sector[398]!=198u || sector[399] || sector[396]!=0xfau || sector[397]!=0xffu)
        return fail("proof VMU FAT chain");
    if(kui_vmu_persist_session_read(&session,253u,sector)) return fail("proof VMU directory");
    uint8_t entry[32]={0};entry[0]=0x33u;put16(entry+2u,199u);
    memcpy(entry+4u,"KUIPROOF",8u);memcpy(entry+16u,(uint8_t[8]){0x20,0,1,1,0,0,0,5},8u);put16(entry+24u,2u);
    if(memcmp(entry,sector,32u)) return fail("proof directory entry differs");
    for(unsigned i=32u;i<512u;i++) if(sector[i]) return fail("unexpected proof directory entry");
    *generation=current;return true;
}
static bool load_profile(void) {
    FIL file;UINT done=0;
    if(!kui_sd_connect() || !kui_mount(&volume,kui_log)) return fail("SCI profile mount");
    if(f_open(&file,PROFILE_PATH,FA_READ)!=FR_OK) return fail("p0001 persistent profile file missing");
    bool exact=f_size(&file)==sizeof(profile_wire) &&
        f_read(&file,profile_wire,sizeof(profile_wire),&done)==FR_OK && done==sizeof(profile_wire);
    FRESULT closed=f_close(&file);
    if(!exact || closed!=FR_OK || kui_vmu_persist_profile_decode(&profile,profile_wire,sizeof(profile_wire)) ||
       strcmp(profile.pair_path,"/KUI/vmu-test/p0001")) return fail("exact canonical p0001 profile required");
    if(f_mount(NULL,"0:",0)!=FR_OK) return fail("profile unmount");
    return kui_sd_vmu_proof_disconnect()?true:fail("profile disconnect / SCI restoration");
}
static int physical_identity(void *context,struct kui_vmu_persist_session_identity *out) {
    (void)context;struct kui_vmu_sd_identity captured;
    if(!kui_sd_vmu_profile_identity(&captured)) return -1;
    memcpy(out->cid,captured.cid,16u);out->card_sectors=captured.sectors;
    out->high_capacity=captured.high_capacity?1u:0u;return 0;
}
static bool physical_healthy(void *context) {(void)context;return kui_sd_vmu_proof_healthy();}
static int physical_read(void *context,uint64_t lba,void *bytes) {
    (void)context;if(lba>UINT32_MAX) return -1;
    return raw.read(raw.ctx,(uint32_t)lba,1u,bytes);
}
static int physical_write(void *context,uint64_t lba,const void *bytes) {
    (void)context;if(lba>UINT32_MAX) return -1;
    return raw.write(raw.ctx,(uint32_t)lba,1u,bytes);
}
static int physical_sync(void *context) {(void)context;return raw.sync(raw.ctx);}
static bool proof_summary(void) {
    return session.summary.files==1u && session.summary.used_blocks==2u &&
        session.summary.free_blocks==198u;
}
/* One operation per program invocation. A second session requires a fresh
 * launch/connection; no failed session is reset or reused. Every exit unmounts
 * and disconnects before the UI publishes READY. A pending physical journal
 * remains bound to this exact pair for the next fresh-admission recovery. */
static bool run_physical_proof(bool commit) {
    bool ok=false;unsigned generation=0;
    const uint32_t proof_mode=*(const volatile uint32_t *)&kui_vmu_persist_proof_build_mode;
    if(proof_mode!=1u) return fail("physical proof build identity");
    kui_sd_set_params(KUI_STORAGE_SCI,true);
    if(!load_profile()) goto finished;
    if(!kui_sd_connect()) {fail("fresh SCI connection");goto finished;}
    if(!kui_sd_vmu_proof_raw_ops(&raw) || !raw.blocks || !raw.read || !raw.write || !raw.sync) {
        fail("fresh unmounted SCI proof ownership");goto finished;
    }
    struct kui_vmu_persist_session_identity actual_card;
    if(physical_identity(NULL,&actual_card) || raw.blocks(raw.ctx)!=actual_card.card_sectors) {
        fail("fresh physical card identity");goto finished;
    }
    struct kui_vmu_fs_input input={.card_sectors=actual_card.card_sectors,
        .partition_first=profile.partition_first,.partition_end=profile.partition_end,
        .high_capacity=actual_card.high_capacity,.pair_path=profile.pair_path};
    memcpy(input.cid,actual_card.cid,16u);memcpy(input.image_id,profile.image_id,16u);
    const struct kui_vmu_persist_session_ops ops={.identity=physical_identity,
        .read_sector=physical_read,.write_sector=physical_write,
        .sync=physical_sync,.healthy=physical_healthy};
    kui_log("Checking live allocation before physical journal recovery.");
    kui_log("Keep power on until READY.");
    int status=kui_vmu_persist_session_init(&session,&profile,&input,&ops,&fs_workspace,&image_workspace);
    if(status) {
        kui_log("FAIL session %d phase %lu",status,(unsigned long)session.phase);
        kui_log("FS %d detail %lu LBA %08lx",session.fs_result,
            (unsigned long)fs_workspace.stats.detail,(unsigned long)fs_workspace.stats.last_lba);
        kui_log("STORE %d IMAGE %d",session.store_result,session.image_result);goto finished;
    }
    if(!proof_summary() || !fixture_generation(&generation)) {
        fail("exact one-save KUIPROOF fixture required");goto finished;
    }
    kui_log("PASS live ownership, real journal binding and VMU filesystem.");
    kui_log("Stored generation %02X; VMS package CRC16 valid.",generation);
    if(commit) {
        if(generation==255u) {fail("generation FF reached; preserve the pair");goto finished;}
        /* No new write before the complete fixture admission above. */
        memcpy(before_crc,session.block_crc,sizeof(before_crc));
        pattern(generation+1u,fixture);
        kui_log("Committing generation %02X through physical journal.",generation+1u);
        status=kui_vmu_persist_session_commit(&session,PROOF_BLOCK,fixture);
        if(status) {kui_log("FAIL physical commit %d; preserve both files",status);goto finished;}
        status=kui_vmu_persist_session_refresh(&session,&image_workspace);
        if(status || !proof_summary()) {fail("post-commit VMU filesystem / hash validation");goto finished;}
        for(unsigned i=0;i<256u;i++) if(i!=PROOF_BLOCK && before_crc[i]!=session.block_crc[i]) {
            fail("another VMU block changed");goto finished;
        }
        unsigned saved=0;
        if(!fixture_generation(&saved) || saved!=generation+1u || before_crc[PROOF_BLOCK]==session.block_crc[PROOF_BLOCK]) {
            fail("committed generation / exact block readback");goto finished;
        }
        generation=saved;kui_log("PASS one-block durable commit and exact readback.");
    } else if(!generation) {fail("no committed proof generation; use A on a fresh launch");goto finished;}
    char prefix[17];kui_hex(session.base_sha256,8u,prefix);
    kui_log("PASS generation %02X; current image SHA %.16s",generation,prefix);
    kui_log("Physical redo record retained for the next restart.");
    if(!commit) kui_log("After a power cycle, this confirms restart persistence.");
    ok=true;
finished:
    if(f_mount(NULL,"0:",0)!=FR_OK) {fail("final unmount");ok=false;}
    if(!kui_sd_vmu_proof_disconnect()) {
        kui_vmu_persist_session_poison(&session);fail("final SCI release / restoration");ok=false;
    }
    if(ok) kui_log(commit?"READY: power off, relaunch this tool, then press X.":
        "READY: photograph this screen; power may be switched off.");
    return ok;
}
static uint32_t buttons(void) {
    maple_device_t *controller=maple_enum_type(0,MAPLE_FUNC_CONTROLLER);
    cont_state_t *state=controller?maple_dev_status(controller):NULL;
    return state?state->buttons:0;
}
int main(void) {
    vid_set_mode(DM_640x480|DM_MULTIBUFFER,PM_RGB565);kui_storage_boot_begin();
    kui_log("A changes only the existing KUIPROOF test-save generation.");
    kui_log("X reopens the physical journal and verifies the saved value.");
    kui_log("Keep card.vmd, card.journal and identity.bin together.");
    kui_log("Do not run the earlier synthetic A/X test on this pair.");
    uint32_t previous=buttons();bool attempted=false;
    for(;;) {
        uint32_t now=buttons(),pressed=now&~previous;previous=now;
        if(!attempted && (pressed&(CONT_A|CONT_X))) {
            attempted=true;line_count=0;
            result="Working: keep power on until READY.";render();
            bool ok=run_physical_proof((pressed&CONT_A)!=0);
            result=ok?"PASS / READY. Photograph, then power off.":
                "FAILED. Preserve the pair; photograph and restart.";
            previous=buttons();
        }
        render();thd_sleep(16);
    }
    return 0;
}
