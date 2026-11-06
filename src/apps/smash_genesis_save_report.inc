/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/smash_genesis_save_header.h"
#include <inttypes.h>
#include <stdarg.h>

#define SMASH_GENESIS_REPORT_PATH "0:/KUI/apps/games/smash-genesis-saves.txt"
#define SMASH_GENESIS_REPORT_READ_LIMIT 64u
struct smash_genesis_report_work {
    const struct kui_game_metadata_ops *source;
    kui_cancel_fn cancel;
    unsigned reads,next;
    bool exhausted;
    struct {bool valid;uint32_t lba;uint8_t data[2048];} cache[2];
    uint8_t sector[2048];
    char text[4096];
    size_t used;
    bool formatted;
};
static enum kui_game_metadata_io_result smash_genesis_report_source(
    struct smash_genesis_report_work *w,uint32_t lba,uint8_t out[2048]) {
    if(w->cancel())return KUI_GAME_METADATA_IO_CANCELLED;
    if(w->reads==SMASH_GENESIS_REPORT_READ_LIMIT) {
        w->exhausted=true;return KUI_GAME_METADATA_IO_ERROR;
    }
    ++w->reads;
    return w->source->read_sector(w->source->ctx,lba,out);
}
/* Cache only directory sectors. A ROM header must not evict the root used by
 * every subsequent title. All source reads, including ROMs, share one budget. */
static enum kui_game_metadata_io_result smash_genesis_report_read(void *context,
    uint32_t lba,uint8_t out[2048]) {
    struct smash_genesis_report_work *w=context;
    if(w->cancel())return KUI_GAME_METADATA_IO_CANCELLED;
    for(unsigned i=0u;i<2u;i++)if(w->cache[i].valid && w->cache[i].lba==lba) {
        w->next=i^1u;memcpy(out,w->cache[i].data,2048u);return KUI_GAME_METADATA_IO_OK;
    }
    enum kui_game_metadata_io_result result=smash_genesis_report_source(w,lba,out);
    if(result==KUI_GAME_METADATA_IO_OK) {
        unsigned slot=w->next;w->next=slot^1u;w->cache[slot].valid=true;w->cache[slot].lba=lba;
        memcpy(w->cache[slot].data,out,2048u);
    }
    return result;
}
static bool smash_genesis_report_range(void *context,uint32_t lba,uint32_t count) {
    struct smash_genesis_report_work *w=context;
    return w->source->data_range(w->source->ctx,lba,count);
}
static void smash_genesis_report_append(struct smash_genesis_report_work *w,const char *format,...) {
    if(!w->formatted)return;
    va_list args;va_start(args,format);
    int n=vsnprintf(w->text+w->used,sizeof(w->text)-w->used,format,args);va_end(args);
    if(n<0 || (size_t)n>=sizeof(w->text)-w->used)w->formatted=false;
    else w->used+=(size_t)n;
}
static const char *smash_genesis_report_header(struct smash_genesis_report_work *w,
    const struct kui_game_metadata_ops *ops,const struct kui_game_metadata *metadata,
    unsigned selection,uint32_t *sram) {
    struct kui_game_metadata directory=*metadata;uint32_t lba=0u,bytes=0u;
    if(w->exhausted)return "scan read budget exhausted";
    if(kui_game_metadata_find_directory(ops,&directory,kui_smash_native_menu_folder(selection),
        &lba,&bytes)!=KUI_GAME_METADATA_OK)
        return w->exhausted?"scan read budget exhausted":"original ROM directory unavailable";
    directory.root_lba=lba;directory.root_bytes=bytes;
    if(kui_game_metadata_find(ops,&directory,kui_smash_native_menu_rom(selection),
        KUI_GAME_METADATA_MAX_BOOT_BYTES,&lba,&bytes)!=KUI_GAME_METADATA_OK)
        return w->exhausted?"scan read budget exhausted":"original ROM file unavailable";
    if(bytes<KUI_SMASH_GENESIS_HEADER_BYTES)return "ROM shorter than 512-byte header";
    if(!ops->data_range(ops->ctx,lba,1u) ||
       smash_genesis_report_source(w,lba,w->sector)!=KUI_GAME_METADATA_IO_OK)
        return w->exhausted?"scan read budget exhausted":"ROM header read unavailable";
    *sram=kui_smash_genesis_sram_bytes(w->sector,KUI_SMASH_GENESIS_HEADER_BYTES);
    return NULL;
}
/* Best effort after successful owned collection preparation. A report failure
 * cannot change the prepared launch. No ROM body or full game image is read. */
static bool smash_genesis_save_report(struct files *files,
    const struct kui_game_metadata_ops *ops,const struct kui_game_metadata *metadata,
    const char *build,kui_log_fn log,kui_cancel_fn cancel) {
    if(!files || !ops || !ops->read_sector || !ops->data_range || !metadata ||
       !build || !log || !cancel || cancel())return false;
    char root[KUI_GAME_LAUNCH_SETTINGS_PATH_CAP];
    /* Source tracks are sibling filenames. This also excludes short-name
     * aliases of the fixed report destination from being overwritten. */
    if(strcmp(files->root,"/") && (!kui_game_launch_settings_normalize(root,files->root) ||
       !strcmp(root,"/kui/apps/games")))return false;
    struct smash_genesis_report_work *w=calloc(1u,sizeof(*w));
    if(!w)return false;
    w->source=ops;w->cancel=cancel;w->formatted=true;
    struct kui_game_metadata_ops reader={w,smash_genesis_report_read,smash_genesis_report_range};
    smash_genesis_report_append(w,"K-UI Sega Smash Pack Genesis save capabilities\nbuild: %.12s\n"
        "source directory: %s\n"
        "All ten original Genesis selections use the shared engine and virtual VMU A1.\n"
        "SRAM bytes describe the original engine's RA-header eligibility only.\n"
        "Eligibility does not prove that the game saves high scores or that a save persisted.\n"
        "SRAM 0 means the original engine admits no cartridge SRAM; controller settings are separate.\n"
        "Only each ROM's first 512 bytes are inspected; the image reader transfers one 2048-byte sector.\n\n",
        build,files->root);
    for(unsigned i=1u;i<=KUI_SMASH_NATIVE_MENU_SELECTIONS;i++) {
        uint32_t sram=0u;
        const char *reason=smash_genesis_report_header(w,&reader,metadata,i,&sram);
        smash_genesis_report_append(w,"%u. %s\nROM: %s/%s\n",i,kui_smash_native_menu_name(i),
            kui_smash_native_menu_folder(i),kui_smash_native_menu_rom(i));
        if(reason)smash_genesis_report_append(w,"SRAM bytes: unavailable (%s)\n\n",reason);
        else smash_genesis_report_append(w,"SRAM bytes: %" PRIu32 "\n\n",sram);
        if(cancel())break;
    }
    smash_genesis_report_append(w,"Source sectors read: %u (maximum %u, including directory lookups)\n",
        w->reads,SMASH_GENESIS_REPORT_READ_LIMIT);
    bool ok=close_reader(files) && w->formatted && !cancel();
    FIL report;
    if(ok) {
        ok=f_open(&report,SMASH_GENESIS_REPORT_PATH,FA_WRITE|FA_CREATE_ALWAYS)==FR_OK;
        if(ok) {
            UINT written=0u;
            ok=!cancel() && f_write(&report,w->text,(UINT)w->used,&written)==FR_OK && written==w->used;
            if(ok)ok=f_sync(&report)==FR_OK;
            if(f_close(&report)!=FR_OK)ok=false;
        }
    }
    free(w);
    if(ok)log("Smash Pack: Genesis save-capability report saved to %s",SMASH_GENESIS_REPORT_PATH);
    return ok;
}
