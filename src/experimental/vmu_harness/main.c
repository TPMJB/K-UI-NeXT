/* SPDX-License-Identifier: GPL-3.0-only */
/* Owned KOS proof program. This never installs a retail-game hook or sends a
 * VMU write onto Maple. The virtual DMA schedule is completed in software. */
#include "platform.h"
#include "kui/shell_font.h"
#include "kui/vmu_emulation.h"
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
#define PROBE_BLOCK 42u
#define LOG_ROWS 15u
#define LOG_COLS 96u

static char lines[LOG_ROWS][LOG_COLS];
static unsigned line_count;
static unsigned transport=KUI_STORAGE_SCI;
static const char *result="Idle. A creates a new isolated probe; X checks the latest.";
static struct kui_vmu_store store;
static kui_vmu_emulation_t device;
static FATFS volume;
struct file_pair { FIL image,journal; bool image_open,journal_open; };
static struct file_pair pair;
static uint8_t request[140],expected[512],actual[512];
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
static void pattern(unsigned number,uint8_t out[512]) {
    for(unsigned i=0;i<512;i++) out[i]=(uint8_t)((i*37u)^(i>>2)^number^(number>>8));
    memcpy(out,"KUI virtual VMU persistent block proof v1",40);put32(out+40,number);
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
static void command(unsigned cmd,unsigned words,unsigned phase) {
    memset(request,0,sizeof(request));request[0]=(uint8_t)cmd;request[1]=1;
    request[3]=(uint8_t)words;request[7]=2;request[9]=(uint8_t)phase;
    request[11]=PROBE_BLOCK;
}
static bool virtual_read(uint8_t out[512]) {
    command(0x0b,2,0);
    if(!exchange(12,0x08,524)) return false;
    memcpy(out,reply+12,512);return true;
}
static bool protocol_setup(void) {
    const struct kui_vmu_emulation_io io={&store,block_read,block_commit};
    if(kui_vmu_emulation_init(&device,0x01,&io)) return fail("virtual device initialization");
    command(0x01,0,0);
    if(!exchange(4,0x05,116)) return false;
    kui_log("PASS virtual device discovery (software chain)");
    command(0x0a,2,0);request[11]=0;
    if(!exchange(12,0x08,32)) return false;
    kui_log("PASS virtual memory geometry");return true;
}
static bool prepare_store(unsigned number) {
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
    return protocol_setup();
}
static void format_sector(unsigned block,uint8_t out[512]) {
    memset(out,0,512);
    if(block==254) {
        for(unsigned i=0;i<256;i++) put16(out+2*i,i<200?0xfffc:0xfffa);
        for(unsigned i=242;i<=253;i++) put16(out+2*i,i-1);
    } else if(block==255) {
        memset(out,0x55,16);
        /* Standard empty 200-user-block VMU geometry. Block 42 remains free:
         * the proof changes raw bytes, not a valid game save or directory. */
        put16(out+64,255);put16(out+66,0);put16(out+68,255);
        put16(out+70,254);put16(out+72,1);put16(out+74,253);
        put16(out+76,13);put16(out+78,0);put16(out+80,200);put16(out+82,31);
    }
}
static bool make_file(unsigned number,const char *leaf,unsigned sectors,bool formatted) {
    char name[96];FIL file;
    if(!path(name,number,leaf) || f_open(&file,name,FA_WRITE|FA_CREATE_NEW)!=FR_OK)
        return fail("create-new isolated test file");
    bool ok=true;uint8_t bytes[512];
    for(unsigned block=0;block<sectors;block++) {
        UINT done=0;format_sector(formatted?block:0,bytes);
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
    return make_file(*number,"card.vmd",256,true) && make_file(*number,"card.journal",2,false);
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
static bool run_test(bool create) {
    unsigned number=0;
    if(!connect_volume()) return false;
    if(create?!new_probe(&number):!latest_probe(&number)) return false;
    kui_log("Probe /KUI/vmu-test/p%04u",number);
    if(!prepare_store(number)) return false;
    pattern(number,expected);
    if(create) {
        memset(actual,0xa5,sizeof(actual));
        if(!virtual_read(actual)) return false;
        for(unsigned i=0;i<512;i++) if(actual[i]) return fail("new block is not empty");
        for(unsigned phase=0;phase<4;phase++) {
            command(0x0c,34,phase);memcpy(request+12,expected+phase*128,128);
            if(!exchange(140,0x07,4)) return false;
        }
        if(!virtual_read(actual)) return false;
        for(unsigned i=0;i<512;i++) if(actual[i]) return fail("staged bytes became visible before sync");
        kui_log("PASS four 128-byte writes remain staged until sync");
        command(0x0d,2,4);
        if(!exchange(12,0x07,4)) return false;
        if(!virtual_read(actual) || memcmp(actual,expected,512)) return fail("durable block readback");
        if(kui_vmu_store_sync(&store)!=KUI_VMU_STORE_OK) return fail("final store barrier");
        kui_log("PASS journaled commit and virtual readback");
        if(!disconnect_volume()) return fail("close / unmount after commit");
        memset(&store,0,sizeof(store));memset(&device,0,sizeof(device));
        if(!connect_volume() || !prepare_store(number)) return false;
    }
    if(!virtual_read(actual) || memcmp(actual,expected,512)) return fail("persisted block differs");
    kui_log(create?"PASS fresh reopen after remount / reconnect":"PASS latest persisted block matches expected bytes");
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
