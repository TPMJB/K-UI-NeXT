/* SPDX-License-Identifier: GPL-3.0-only */
/* Owned KOS proof program. This never installs a retail-game hook or sends a
 * VMU write onto Maple. The virtual DMA schedule is completed in software. */
#include "platform.h"
#include "kui/shell_font.h"
#include "kui/vmu_emulation.h"
#include "kui/vmu_image.h"
#include "kui/vmu_import.h"
#include "kui/crc16.h"
#include "kui/vmu_maple_dma.h"
#include "kui/vmu_store.h"
#include <kos.h>
#include <dc/maple/controller.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

KOS_INIT_FLAGS(INIT_IRQ | INIT_CONTROLLER | INIT_NO_DCLOAD | INIT_QUIET);
#ifndef KUI_BUILD_ID
#define KUI_BUILD_ID "local"
#endif

#define PROBE_ROOT "0:/KUI/vmu-test"
#define PROBE_FIRST_BLOCK 199u
#define PROBE_SECOND_BLOCK 198u
#define PROBE_SAVE_BYTES 1024u
#define PROBE_PACKAGE_BYTES 704u
#define LOG_ROWS 15u
#define LOG_COLS 96u

static char lines[LOG_ROWS][LOG_COLS];
static unsigned line_count;
static unsigned transport=KUI_STORAGE_SCI;
static const char *result="Idle. A creates a new isolated probe; X checks the latest.";
static struct kui_vmu_store store;
static kui_vmu_emulation_t device;
static struct kui_vmu_image_workspace image_workspace;
static FATFS volume;
struct file_pair { FIL image,journal; bool image_open,journal_open; };
static struct file_pair pair;
static uint8_t request[140],expected[PROBE_SAVE_BYTES],actual[PROBE_SAVE_BYTES];
static uint8_t original[512],metadata[512];
static _Alignas(32) uint8_t reply[1024];

/* Existing SD adapter's optical arbitration seam. This standalone program
 * never starts an optical request. No drive initialization is performed. */
bool kui_disc_read_pending(void *context) {(void)context;return false;}
bool kui_cancelled(void) {return false;}

static void render(void) {
    for(unsigned i=0;i<640u*480u;i++) vram_s[i]=0x0841;
    kui_shell_font_draw(vram_s,28,24,0xfe2d,"Virtual VMU - native proof harness",true);
    kui_shell_font_draw(vram_s,28,56,0xffff,"OWNED TEST PROGRAM / SOFTWARE MAPLE CHAIN",false);
    kui_shell_font_draw(vram_s,28,80,0xc618,"Retail games cannot use the virtual card in this build.",false);
    char heading[100];
    snprintf(heading,sizeof(heading),"Build %.12s   Storage: %s   Card: /KUI/vmu-test",KUI_BUILD_ID,
             transport==KUI_STORAGE_SCI?"SCI":"SCIF");
    kui_shell_font_draw(vram_s,28,106,0xc618,heading,false);
    for(unsigned row=0;row<line_count;row++)
        kui_shell_font_draw(vram_s,28,139+(int)row*18,0xffff,lines[row],false);
    kui_shell_font_draw(vram_s,28,418,0xfe2d,result,false);
    kui_shell_font_draw(vram_s,28,444,0xc618,"A New write test   X Verify latest   Left/Right SCI/SCIF",false);
    vid_waitvbl();vid_flip(-1);
}
void kui_log(const char *format,...) {
    if(line_count==LOG_ROWS) {
        memmove(lines,lines+1,(LOG_ROWS-1u)*LOG_COLS);--line_count;
    }
    va_list args;va_start(args,format);
    vsnprintf(lines[line_count++],LOG_COLS,format,args);va_end(args);
    render();
}
static bool fail(const char *message) {kui_log("FAIL: %s",message);return false;}
static void put16(uint8_t *p,unsigned value) {p[0]=(uint8_t)value;p[1]=(uint8_t)(value>>8);}
static void put32(uint8_t *p,uint32_t value) {for(unsigned i=0;i<4;i++) p[i]=(uint8_t)(value>>(i*8));}
static void identity(unsigned number,uint8_t out[16]) {
    memcpy(out,"KUI-VMU-PB01",12);put32(out+12,number);
}
/* A constructed, nonproprietary VMS package: 128-byte header, one 512-byte
 * icon, 64 application bytes and zero padding to two VMU blocks. The ordinary
 * import parser checks the actual CRC/lengths after every persisted reopen. */
static void pattern(unsigned number,uint8_t out[PROBE_SAVE_BYTES]) {
    memset(out,0,PROBE_SAVE_BYTES);
    memcpy(out,"K-UI VMU proof",14);
    memcpy(out+16,"Owned emulation persistence test",31);
    memcpy(out+48,"KUI-VMU-PROOF",13);
    put16(out+64,1);put32(out+72,64);
    for(unsigned i=96;i<640;i++) out[i]=(uint8_t)(i*37u+(i>>2));
    memcpy(out+640,"KUI virtual VMU allocated save proof v2",38);
    put32(out+680,number);
    for(unsigned i=684;i<PROBE_PACKAGE_BYTES;i++) out[i]=(uint8_t)(i^number^(number>>8));
    put16(out+70,kui_crc16_nibble(0,out,PROBE_PACKAGE_BYTES));
}
static void directory_entry(uint8_t out[32]) {
    static const uint8_t timestamp[8]={0x20,0,1,1,0,0,0,5};
    memset(out,0,32);out[0]=0x33;put16(out+2,PROBE_FIRST_BLOCK);
    memcpy(out+4,"KUIPROOF",8);memcpy(out+16,timestamp,8);put16(out+24,2);
}
static bool path(char out[96],unsigned number,const char *leaf) {
    int n=snprintf(out,96,PROBE_ROOT "/p%04u/%s",number,leaf);
    return n>0 && n<96;
}
static void close_pair(void) {
    if(pair.journal_open) { (void)f_close(&pair.journal);pair.journal_open=false; }
    if(pair.image_open) { (void)f_close(&pair.image);pair.image_open=false; }
}
static bool close_pair_checked(void) {
    bool ok=true;
    if(pair.journal_open) {if(f_close(&pair.journal)!=FR_OK) ok=false;pair.journal_open=false;}
    if(pair.image_open) {if(f_close(&pair.image)!=FR_OK) ok=false;pair.image_open=false;}
    return ok;
}
static int file_read(void *context,uint64_t lba,void *sector) {
    struct file_pair *files=context;
    if(lba>=258u || !files->image_open || !files->journal_open) return -1;
    FIL *file=lba<256u?&files->image:&files->journal;
    FSIZE_t at=(FSIZE_t)((lba<256u?lba:lba-256u)*512u);UINT done=0;
    return f_lseek(file,at)==FR_OK && f_read(file,sector,512,&done)==FR_OK && done==512?0:-1;
}
static int file_write(void *context,uint64_t lba,const void *sector) {
    struct file_pair *files=context;
    if(lba>=258u || !files->image_open || !files->journal_open) return -1;
    FIL *file=lba<256u?&files->image:&files->journal;
    FSIZE_t at=(FSIZE_t)((lba<256u?lba:lba-256u)*512u);UINT done=0;
    /* Exact lengths were checked before opening the store; never extend. */
    if(f_size(file)!=(lba<256u?131072u:1024u)) return -1;
    return f_lseek(file,at)==FR_OK && f_write(file,sector,512,&done)==FR_OK && done==512?0:-1;
}
static int file_sync(void *context) {
    struct file_pair *files=context;
    if(!files->image_open || !files->journal_open) return -1;
    FRESULT a=f_sync(&files->journal),b=f_sync(&files->image);
    return a==FR_OK && b==FR_OK?0:-1;
}
static int image_read(void *context,uint16_t block,void *data) {
    return block<256u?file_read(context,block,data):-1;
}
static int image_write(void *context,uint16_t block,const void *data) {
    return block<256u?file_write(context,block,data):-1;
}
static bool validate_image(bool populated) {
    const struct kui_vmu_image_io io={&pair,image_read,NULL,NULL};
    struct kui_vmu_image_summary summary;
    int status=kui_vmu_image_validate(&io,131072,&image_workspace,&summary);
    if(status!=KUI_VMU_IMAGE_OK || summary.files!=(populated?1u:0u) ||
       summary.used_blocks!=(populated?2u:0u) || summary.free_blocks!=(populated?198u:200u)) {
        kui_log("FAIL image filesystem validation: status=%d",status);return false;
    }
    kui_log(populated?"PASS VMU allocation: one save, two used / 198 free blocks":
                      "PASS empty VMU allocation: 200 free blocks");return true;
}
static int block_read(void *context,uint16_t block,uint8_t data[512]) {
    return kui_vmu_store_read(context,block,data)==KUI_VMU_STORE_OK?0:-1;
}
static int block_commit(void *context,uint16_t block,const uint8_t data[512]) {
    return kui_vmu_store_write(context,block,data)==KUI_VMU_STORE_OK?0:-1;
}
static int dispatch(void *context,const uint8_t *input,size_t input_bytes,
                    uint8_t *output,size_t output_capacity,size_t *output_bytes) {
    return kui_vmu_emulation_handle(context,input,input_bytes,output,output_capacity,output_bytes);
}
/* These fake physical addresses describe owned software buffers only. The
 * chain is NEVER submitted to the hardware Maple engine. KOS independently
 * handles the real controller; no frame or device list is patched. */
static bool exchange(size_t request_bytes,unsigned response_command,size_t response_bytes) {
    _Alignas(32) uint8_t source[160],hardware[160];
    struct kui_vmu_maple_dma_plan plan;
    const struct kui_vmu_maple_dma_region region={0x0c080000u,reply,sizeof(reply)};
    memset(source,0,sizeof(source));memset(reply,0xa5,sizeof(reply));
    put32(source,KUI_VMU_MAPLE_DMA_LAST|request[3]);put32(source+4,region.physical_base);
    memcpy(source+8,request,request_bytes);
    int status=kui_vmu_maple_dma_prepare(&plan,0x01,source,8+request_bytes,
                                       hardware,sizeof(hardware),&region,1);
    if(status!=KUI_VMU_MAPLE_DMA_OK || plan.virtual_count!=1 || plan.hardware_count)
        return fail("software DMA schedule preparation");
    status=kui_vmu_maple_dma_complete(&plan,dispatch,&device);
    if(status!=KUI_VMU_MAPLE_DMA_OK || reply[0]!=response_command ||
       (size_t)reply[3]*4u+4u!=response_bytes)
        return fail("virtual protocol reply / length");
    return true;
}
static void command(unsigned cmd,unsigned words,unsigned phase,unsigned block) {
    memset(request,0,sizeof(request));request[0]=(uint8_t)cmd;request[1]=1;
    request[3]=(uint8_t)words;request[7]=2;request[9]=(uint8_t)phase;
    request[10]=(uint8_t)(block>>8);request[11]=(uint8_t)block;
}
static bool virtual_read(unsigned block,uint8_t out[512]) {
    command(0x0b,2,0,block);
    if(!exchange(12,0x08,524)) return false;
    memcpy(out,reply+12,512);return true;
}
static bool protocol_setup(void) {
    const struct kui_vmu_emulation_io io={&store,block_read,block_commit};
    if(kui_vmu_emulation_init(&device,0x01,&io)) return fail("virtual device initialization");
    command(0x01,0,0,0);
    if(!exchange(4,0x05,116)) return false;
    kui_log("PASS virtual device discovery (software chain)");
    command(0x0a,2,0,0);
    if(!exchange(12,0x08,32)) return false;
    kui_log("PASS virtual memory geometry");return true;
}
static bool prepare_store(unsigned number,bool fresh) {
    char name[96];uint8_t id[16],check[17];FIL file;UINT done=0;
    if(!path(name,number,"identity.bin") || f_open(&file,name,FA_READ)!=FR_OK)
        return fail("probe identity is missing");
    FRESULT read=f_read(&file,check,sizeof(check),&done),closed=f_close(&file);
    identity(number,id);
    if(read!=FR_OK || closed!=FR_OK || done!=16 || memcmp(check,id,16))
        return fail("probe identity mismatch");
    if(!path(name,number,"card.vmd") || f_open(&pair.image,name,FA_READ|FA_WRITE)!=FR_OK)
        return fail("open probe card.vmd");
    pair.image_open=true;
    if(f_size(&pair.image)!=131072u) return fail("probe card length");
    if(!path(name,number,"card.journal") || f_open(&pair.journal,name,FA_READ|FA_WRITE)!=FR_OK)
        return fail("open probe journal");
    pair.journal_open=true;
    if(f_size(&pair.journal)!=1024u) return fail("probe journal length");
    const struct kui_vmu_store_extent image={0,256},journal={256,2};
    const struct kui_vmu_store_io io={&pair,file_read,file_write,file_sync};
    struct kui_vmu_store_config config={.card_sectors=258,.image_extents=&image,
        .image_extent_count=1,.journal_extents=&journal,.journal_extent_count=1};
    memcpy(config.image_id,id,sizeof(id));
    int status=kui_vmu_store_open(&store,&io,&config);
    if(status!=KUI_VMU_STORE_OK) {
        kui_log("Store refused pair: %d; keep both files for recovery",status);return false;
    }
    if(fresh) {
        const struct kui_vmu_image_io image_io={&pair,image_read,image_write,file_sync};
        if(kui_vmu_image_format_blank(&image_io,131072,NULL,&image_workspace)!=KUI_VMU_IMAGE_OK)
            return fail("format only the newly created zero image");
        kui_log("PASS shared formatter created and verified empty VMU");
    }
    return validate_image(!fresh) && protocol_setup();
}
static bool make_file(unsigned number,const char *leaf,unsigned sectors) {
    char name[96];FIL file;
    if(!path(name,number,leaf) || f_open(&file,name,FA_WRITE|FA_CREATE_NEW)!=FR_OK)
        return fail("create-new isolated test file");
    bool ok=true;uint8_t bytes[512]={0};
    for(unsigned block=0;block<sectors;block++) {
        UINT done=0;
        if(f_write(&file,bytes,512,&done)!=FR_OK || done!=512) {ok=false;break;}
    }
    FRESULT synced=f_sync(&file),closed=f_close(&file);
    return ok && synced==FR_OK && closed==FR_OK;
}
static bool new_probe(unsigned *number) {
    const char *const parents[]={"0:/KUI",PROBE_ROOT};
    for(unsigned i=0;i<2;i++) {
        FRESULT status=f_mkdir(parents[i]);
        if(status!=FR_OK && status!=FR_EXIST) return fail("create probe directory");
    }
    char directory[96];bool made=false;
    for(unsigned n=1;n<=9999;n++) {
        snprintf(directory,sizeof(directory),PROBE_ROOT "/p%04u",n);
        FRESULT status=f_mkdir(directory);
        if(status==FR_OK) {*number=n;made=true;break;}
        if(status!=FR_EXIST) return fail("create-new probe folder");
    }
    if(!made) return fail("all 9999 probe folders already exist");
    char name[96];uint8_t id[16];identity(*number,id);FIL file;UINT done=0;
    if(!path(name,*number,"identity.bin") || f_open(&file,name,FA_WRITE|FA_CREATE_NEW)!=FR_OK)
        return fail("create-new pair identity");
    FRESULT written=f_write(&file,id,sizeof(id),&done),synced=f_sync(&file),closed=f_close(&file);
    if(written!=FR_OK || synced!=FR_OK || closed!=FR_OK || done!=sizeof(id))
        return fail("persist pair identity");
    return make_file(*number,"card.vmd",256) && make_file(*number,"card.journal",2);
}
static bool latest_probe(unsigned *number) {
    DIR directory;FILINFO info;
    if(f_opendir(&directory,PROBE_ROOT)!=FR_OK) return fail("no earlier probe directory");
    unsigned latest=0;bool ok=true;
    for(;;) {
        if(f_readdir(&directory,&info)!=FR_OK) {ok=false;break;}
        if(!info.fname[0]) break;
        const char *p=info.fname;
        if(!(info.fattrib&AM_DIR) || strlen(p)!=5 || p[0]!='p') continue;
        unsigned n=0;bool digits=true;
        for(unsigned i=1;i<5;i++) {
            if(p[i]<'0' || p[i]>'9') {digits=false;break;}
            n=n*10u+(unsigned)(p[i]-'0');
        }
        if(digits && n>latest) latest=n;
    }
    if(f_closedir(&directory)!=FR_OK) ok=false;
    if(!ok || !latest) return fail("cannot locate latest isolated probe");
    *number=latest;return true;
}
static bool connect_volume(void) {
    kui_sd_set_params(transport,true);
    if(!kui_sd_connect()) return fail("storage connection; check SCI/SCIF selection");
    return kui_mount(&volume,kui_log);
}
static bool disconnect_volume(void) {
    bool ok=close_pair_checked();
    if(f_mount(NULL,"0:",0)!=FR_OK) ok=false;
    kui_sd_disconnect();return ok;
}
/* The same four-phase transaction is used for payload, FAT and directory
 * blocks. No byte becomes visible until the durable block-sync succeeds. */
static bool virtual_write(unsigned block,const uint8_t bytes[512]) {
    if(!virtual_read(block,original)) return false;
    for(unsigned phase=0;phase<4;phase++) {
        command(0x0c,34,phase,block);memcpy(request+12,bytes+phase*128,128);
        if(!exchange(140,0x07,4)) return false;
    }
    if(!virtual_read(block,actual) || memcmp(actual,original,512))
        return fail("staged bytes became visible before block sync");
    command(0x0d,2,4,block);
    if(!exchange(12,0x07,4) || !virtual_read(block,actual) || memcmp(actual,bytes,512))
        return fail("durable virtual block readback");
    return true;
}
static bool verify_save(unsigned number) {
    char error[128];struct kui_vmu_save_info parsed;
    pattern(number,expected);
    if(!virtual_read(PROBE_FIRST_BLOCK,actual) || !virtual_read(PROBE_SECOND_BLOCK,actual+512) ||
       memcmp(actual,expected,PROBE_SAVE_BYTES)) return fail("persisted VMS bytes differ");
    if(!kui_vmu_save_parse(KUI_VMU_SAVE_VMS,actual,sizeof(actual),NULL,"KUIPROOF",
                           &parsed,error,sizeof(error)) || parsed.padded_bytes!=PROBE_SAVE_BYTES)
        return fail("existing VMS parser rejected package / CRC16");
    if(!virtual_read(253,metadata)) return false;
    directory_entry(original);
    if(memcmp(metadata,original,32)) return fail("persisted VMU directory entry differs");
    for(unsigned i=32;i<512;i++) if(metadata[i]) return fail("unexpected second directory entry");
    if(!virtual_read(254,metadata) || metadata[PROBE_FIRST_BLOCK*2]!=PROBE_SECOND_BLOCK ||
       metadata[PROBE_FIRST_BLOCK*2+1] || metadata[PROBE_SECOND_BLOCK*2]!=0xfa ||
       metadata[PROBE_SECOND_BLOCK*2+1]!=0xff) return fail("persisted VMU save chain differs");
    kui_log("PASS allocated KUIPROOF VMS: exact bytes and package CRC16");
    return validate_image(true);
}
static bool run_test(bool create) {
    unsigned number=0;
    if(!connect_volume()) return false;
    if(create?!new_probe(&number):!latest_probe(&number)) return false;
    kui_log("Probe /KUI/vmu-test/p%04u",number);
    if(!prepare_store(number,create)) return false;
    pattern(number,expected);
    if(create) {
        /* Reset must discard an unfinished block without changing storage. */
        if(!virtual_read(PROBE_FIRST_BLOCK,original)) return false;
        command(0x0c,34,0,PROBE_FIRST_BLOCK);memcpy(request+12,expected,128);
        if(!exchange(140,0x07,4)) return false;
        command(0x03,0,0,0);
        if(!exchange(4,0x07,4) || !virtual_read(PROBE_FIRST_BLOCK,actual) ||
           memcmp(actual,original,512)) return fail("reset published an unfinished transaction");
        kui_log("PASS reset discards unfinished write without publishing it");
        /* Publish allocated metadata only after both package blocks exist.
         * Per-block journaling does not make this four-block save atomic. */
        if(!virtual_write(PROBE_FIRST_BLOCK,expected) || !virtual_write(PROBE_SECOND_BLOCK,expected+512))
            return false;
        if(!virtual_read(254,metadata)) return false;
        put16(metadata+PROBE_FIRST_BLOCK*2,PROBE_SECOND_BLOCK);
        put16(metadata+PROBE_SECOND_BLOCK*2,0xfffa);
        if(!virtual_write(254,metadata)) return false;
        memset(metadata,0,sizeof(metadata));directory_entry(metadata);
        if(!virtual_write(253,metadata)) return false;
        kui_log("PASS four-phase durable writes: VMS data, FAT and directory");
        command(0x03,0,0,0);
        if(!exchange(4,0x07,4) || !verify_save(number)) return false;
        if(kui_vmu_store_sync(&store)!=KUI_VMU_STORE_OK) return fail("final store barrier");
        kui_log("PASS committed save survives virtual device reset");
        if(!disconnect_volume()) return fail("close / unmount after commit");
        memset(&store,0,sizeof(store));memset(&device,0,sizeof(device));
        if(!connect_volume() || !prepare_store(number,false)) return false;
    }
    if(!verify_save(number)) return false;
    kui_log(create?"PASS fresh reopen after remount / reconnect":"PASS latest persisted save matches expected bytes");
    if(kui_vmu_store_checkpoint(&store)!=KUI_VMU_STORE_OK) return fail("clean image / journal checkpoint");
    kui_log("PASS clean checkpoint; base image is synced");
    if(!disconnect_volume()) return fail("final close / unmount");
    kui_log("READY: power may now be switched off.");
    if(create) kui_log("After a power cycle, relaunch this tool and press X.");
    else kui_log("If you power-cycled first, this proves restart persistence.");
    return true;
}
static uint32_t buttons(void) {
    maple_device_t *controller=maple_enum_type(0,MAPLE_FUNC_CONTROLLER);
    cont_state_t *state=controller?maple_dev_status(controller):NULL;
    return state?state->buttons:0;
}
int main(void) {
    vid_set_mode(DM_640x480|DM_MULTIBUFFER,PM_RGB565);
    kui_storage_boot_begin();
    unsigned inherited=kui_storage_selected();
    if(inherited==KUI_STORAGE_SCI || inherited==KUI_STORAGE_SCIF) transport=inherited;
    kui_log("Virtual card commands use our bounded software DMA adapter.");
    kui_log("The real controller is polled normally by KOS.");
    kui_log("Only new probe folders or their own card/journal are written.");
    kui_log("No physical VMU read or write operation is called.");
    uint32_t previous=buttons();
    for(;;) {
        uint32_t now=buttons(),pressed=now&~previous;previous=now;
        if(pressed&(CONT_DPAD_LEFT|CONT_DPAD_RIGHT)) {
            transport=transport==KUI_STORAGE_SCI?KUI_STORAGE_SCIF:KUI_STORAGE_SCI;
            result="Idle. A creates a new isolated probe; X checks the latest.";
        }
        if(pressed&(CONT_A|CONT_X)) {
            bool create=(pressed&CONT_A)!=0;line_count=0;
            result=create?"Running isolated persistence write test...":"Checking latest probe (may replay its journal)...";
            render();bool ok=run_test(create);
            /* A failed callback blocks this session; never acknowledge a save
             * or silently retry. Retain files, close, and show the failure. */
            close_pair();(void)f_mount(NULL,"0:",0);kui_sd_disconnect();
            result=ok?"PASS / READY. Photograph this screen, then power off.":
                      "FAILED. Keep the probe files and photograph this screen.";
            previous=buttons();
        }
        render();thd_sleep(16);
    }
    return 0;
}
