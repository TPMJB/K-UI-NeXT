/* SPDX-License-Identifier: GPL-3.0-only */
/* Real wizard/merge/pack/key code, fake read-only devices and card publisher.
 * Tests control ownership and interrupted multi-card publication, without
 * mirroring the separately tested FatFs card-service implementation. */
#include "kui/vmu_wizard.h"
#include "kui/vmu_game_card_service.h"
#include "kui/vmu_image_pack.h"
#include "kui/games_storage.h"
#include "kui/clock.h"
#include "kui/vmu_backing.h"
#include "kui/vmu_library.h"
#include "kui/media.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,captures,inspects,imports,assigns,visiting;
static unsigned fail_import,fail_assign,scan_mode,cancel_after,cancel_calls;
static bool mounted,capture_error;
static unsigned cid=1;
static struct kui_volume volume={2048,65536,true};
static struct kui_vmu_game_assignment saved[KUI_VMU_GAME_ASSIGN_MAX];
static unsigned saved_count;
static unsigned library_reads,card_kind,assignment_mode;
static bool target_changed,swap_storage_on_read,allocation_error,cancel_on_assignment,cancel_latched;
static uint8_t opened_bytes[KUI_VMU_GAME_ASSIGN_WIRE_BYTES];
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"%s:%u: %s\n",__FILE__,__LINE__,#x);exit(1);}} while(0)
static bool cancelled(void) {return cancel_latched || (cancel_after && ++cancel_calls>=cancel_after);}
static void log_line(const char *format,...) {(void)format;}
bool kui_clock_now(struct kui_datetime *out) {memset(out,0,sizeof(*out));return false;}
bool kui_clock_to_seconds(const struct kui_datetime *in,int64_t *out) {(void)in;*out=0;return false;}
enum kui_games_storage_result kui_games_storage_open(kui_log_fn log) {(void)log;mounted=true;return KUI_GAMES_STORAGE_OK;}
bool kui_games_storage_release(void) {CHECK(!visiting);mounted=false;return true;}
void kui_games_storage_hold(bool hold) {(void)hold;}
const struct kui_volume *kui_media_volume(void) {return &volume;}
bool kui_sd_vmu_profile_identity(struct kui_vmu_sd_identity *out) {
    CHECK(mounted);memset(out,0,sizeof(*out));out->cid[0]=(uint8_t)cid;
    out->sectors=131072;out->high_capacity=true;return true;
}
bool kui_vmu_library_path(const char *path) {
    return !strcmp(path,"0:/KUI/vmu-test/p0042/card.vmd") ||
        !strcmp(path,"0:/KUI/vmu/cards/v0042/card.vmd");
}
int kui_vmu_library_read(const char *path,uint8_t image[KUI_VMU_IMAGE_BYTES],kui_log_fn log,kui_cancel_fn cancel) {
    (void)log;(void)cancel;CHECK(mounted && kui_vmu_library_path(path));++library_reads;
    if(card_kind==5) return KUI_VMU_LIBRARY_IO;
    for(unsigned i=0;i<KUI_VMU_IMAGE_BLOCKS;i++) CHECK(!kui_vmu_image_blank_block((uint16_t)i,image+512u*i));
    if(swap_storage_on_read) ++cid;
    return KUI_VMU_LIBRARY_OK;
}
int kui_vmu_library_check_allocation(kui_cancel_fn cancel) {
    (void)cancel;CHECK(mounted);return allocation_error?KUI_VMU_LIBRARY_INVALID:KUI_VMU_LIBRARY_OK;
}
FRESULT f_stat(const TCHAR *path,FILINFO *info) {
    CHECK(mounted);memset(info,0,sizeof(*info));
    if(strstr(path,"/assign-")) {
        if(cancel_on_assignment) cancel_latched=true;
        if(!assignment_mode || (strstr(path,"assign-b") && assignment_mode!=2 && assignment_mode!=3 && assignment_mode!=4)) return FR_NO_FILE;
        info->fsize=KUI_VMU_GAME_ASSIGN_WIRE_BYTES;return FR_OK;
    }
    if(strstr(path,"/game.bin")) {
        if(card_kind!=1) return FR_NO_FILE;
        info->fsize=64;return FR_OK;
    }
    CHECK(strstr(path,"/import.bin"));
    if(card_kind==6) return FR_DISK_ERR;
    if(card_kind!=0 && card_kind!=3) return FR_NO_FILE;
    info->fsize=64;return FR_OK;
}
FRESULT f_open(FIL *file,const TCHAR *path,BYTE mode) {
    CHECK(mounted && mode==FA_READ);memset(file,0,sizeof(*file));memset(opened_bytes,0,sizeof(opened_bytes));
    if(strstr(path,"/identity.bin")) {
        if(card_kind==4) return FR_DISK_ERR;
        memset(opened_bytes,0x42,16);file->obj.objsize=16;return FR_OK;
    }
    if(strstr(path,"/game.bin")) {
        char key[12];CHECK(kui_vmu_game_card_key("ALPHA-1",key));
        CHECK(kui_vmu_game_card_binding_encode(key,opened_bytes));file->obj.objsize=64;return FR_OK;
    }
    if(strstr(path,"/assign-")) {
        struct kui_vmu_game_assign_table table={.sequence=1,.count=assignment_mode==1?3u:1u};
        strcpy(table.entries[0].key,"ALPHA-1");table.entries[0].pair=assignment_mode==6?99:42;
        memset(table.entries[0].identity,0x42,16);
        if(assignment_mode==1) {
            strcpy(table.entries[1].key,"MISSING-1");table.entries[1].pair=42;memset(table.entries[1].identity,0x42,16);
            strcpy(table.entries[2].key,"BETA-1");table.entries[2].pair=99;memset(table.entries[2].identity,0x99,16);
        }
        if(assignment_mode==7) strcpy(table.entries[0].key,"GAMMA-1");
        if(strstr(path,"assign-b")) {table.sequence=assignment_mode==3?1:2;if(assignment_mode==3) table.entries[0].pair=99;}
        CHECK(kui_vmu_game_assign_encode(&table,opened_bytes));
        if(assignment_mode==4 || (assignment_mode==2 && strstr(path,"assign-a"))) opened_bytes[101]^=1;
        file->obj.objsize=sizeof(opened_bytes);file->obj.id=1;return FR_OK;
    }
    CHECK(strstr(path,"/import.bin"));memcpy(opened_bytes,"KUIVMUI1",8);memset(opened_bytes+16,0x42,16);
    uint32_t crc=kui_crc32(0,opened_bytes,60);
    for(unsigned i=0;i<4;i++) opened_bytes[60+i]=(uint8_t)(crc>>(8u*i));
    if(card_kind==3) opened_bytes[20]^=1;
    file->obj.objsize=64;return FR_OK;
}
FRESULT f_read(FIL *file,void *data,UINT bytes,UINT *got) {
    CHECK(mounted && bytes==file->obj.objsize && bytes<=sizeof(opened_bytes));
    if(file->obj.id && assignment_mode==5) return FR_DISK_ERR;
    memcpy(data,opened_bytes,bytes);*got=bytes;return FR_OK;
}
FRESULT f_close(FIL *file) {(void)file;CHECK(mounted);return FR_OK;}
bool kui_destination_normalize(char out[KUI_DEST_ROOT_CAP],const char *path) {
    if(!path || path[0]!='/' || strlen(path)>=KUI_DEST_ROOT_CAP) return false;
    strcpy(out,path);return true;
}
bool kui_destination_join(char out[KUI_DEST_ROOT_CAP],const char *root,const char *name) {
    int n=snprintf(out,KUI_DEST_ROOT_CAP,"%s/%s",root,name);return n>0 && n<(int)KUI_DEST_ROOT_CAP;
}
bool kui_games_visit_images(const char *root,kui_games_image_visit_fn visit,void *context,kui_cancel_fn cancel) {
    (void)cancel;CHECK(mounted);++visiting;bool ok=true;
    if(scan_mode==1) {
        for(unsigned i=0;i<2001 && ok;i++) ok=visit(context,"many.gdi",false);
    } else if(!strcmp(root,"/Games")) {
        const char *names[]={"alpha-disc1.gdi","alpha-disc2.gdi","beta-ce.gdi","bad.gdi","nested"};
        for(unsigned i=0;i<5 && ok;i++) ok=visit(context,names[i],i==4);
        for(unsigned i=0;i<10 && ok;i++) {
            char name[32];snprintf(name,sizeof(name),"extra%u.gdi",i);ok=visit(context,name,false);
        }
    } else if(!strcmp(root,"/Games/nested")) {
        ok=visit(context,"gamma.gdi",false);
        if(ok) ok=visit(context,"deep",true);
    } else if(!strcmp(root,"/Games/nested/deep")) ok=visit(context,"omitted",true);
    --visiting;return ok;
}
bool kui_games_inspect(const char *path,struct kui_games_detail *detail,kui_log_fn log,kui_cancel_fn cancel) {
    (void)log;(void)cancel;CHECK(!visiting);++inspects;memset(detail,0,sizeof(*detail));
    detail->valid=true;detail->native_gd=true;
    if(strstr(path,"alpha")) {strcpy(detail->product," alpha-1 ");strcpy(detail->title,"Alpha");}
    else if(strstr(path,"beta")) {strcpy(detail->product,"BETA-1");strcpy(detail->title,"Beta");detail->windows_ce=true;}
    else if(strstr(path,"gamma")) {strcpy(detail->product,"GAMMA-1");strcpy(detail->title,"Gamma");}
    else if(strstr(path,"extra")) {
        unsigned n=0;CHECK(sscanf(strstr(path,"extra"),"extra%u",&n)==1);
        snprintf(detail->product,sizeof(detail->product),"EXTRA-%u",n);
        snprintf(detail->title,sizeof(detail->title),"Extra %u",n);
    } else {detail->valid=false;return false;}
    return true;
}
bool kui_vmu_capture_run(unsigned slot,void *image,size_t bytes,struct kui_vmu_image_summary *summary,
    struct kui_vmu_view *view,kui_log_fn log,kui_cancel_fn cancel,kui_app_progress_fn progress) {
    (void)log;(void)cancel;(void)progress;++captures;memset(view,0,sizeof(*view));
    if(capture_error) {strcpy(view->status.message,"Source removed");view->status.errors=1;return false;}
    struct kui_vmu_image_file file={.bytes=512};
    uint8_t payload[512];memset(payload,slot==2?0x99:(slot==1?0x22:0x11),sizeof(payload));
    file.entry[0]=0x33;memcpy(file.entry+4,slot==1?"FILE-B":"FILE-A",6);file.entry[24]=1;file.payload=payload;
    struct kui_vmu_image_workspace work;
    CHECK(kui_vmu_image_pack(&file,1,image,bytes,&work,summary)==KUI_VMU_IMAGE_OK);return true;
}
int kui_vmu_game_card_import_publish(const void *image,size_t bytes,const uint8_t salt[16],
    struct kui_vmu_game_card_work *work,struct kui_vmu_game_card_choice *choice,uint8_t identity[16],
    kui_log_fn log,kui_cancel_fn cancel) {
    (void)image;(void)salt;(void)work;(void)log;(void)cancel;CHECK(mounted && bytes==131072);
    ++imports;memset(choice,0,sizeof(*choice));
    if(fail_import && imports==fail_import) {strcpy(choice->problem,"Injected publication failure");return KUI_VMU_GAME_CARD_FAILED;}
    choice->pair=imports+1;choice->created=true;memset(identity,(int)choice->pair,16);return KUI_VMU_GAME_CARD_READY;
}
int kui_vmu_game_card_assignments_apply(const struct kui_vmu_game_assignment *entries,size_t count,
    struct kui_vmu_game_card_work *work,struct kui_vmu_game_card_choice *choice,kui_log_fn log,kui_cancel_fn cancel) {
    (void)work;(void)log;(void)cancel;CHECK(mounted);++assigns;
    if(target_changed) {strcpy(choice->problem,"assigned p0042 is missing or its identity changed");return KUI_VMU_GAME_CARD_FAILED;}
    if(fail_assign) {--fail_assign;strcpy(choice->problem,"Injected assignment failure");return KUI_VMU_GAME_CARD_FAILED;}
    CHECK(count<=KUI_VMU_GAME_ASSIGN_MAX);saved_count=(unsigned)count;
    memcpy(saved,entries,count*sizeof(*entries));return KUI_VMU_GAME_CARD_READY;
}
static struct kui_vmu_wizard_request request;
static struct kui_vmu_wizard_view result;
static void run(enum kui_vmu_wizard_op op) {
    request.op=op;++request.generation;
    kui_vmu_wizard_run(&request,&result,log_line,cancelled,NULL);
    CHECK(result.generation==request.generation);CHECK(result.status.complete);CHECK(!mounted);
}
static void init(void) {
    memset(&request,0,sizeof(request));strcpy(request.root,"/Games");run(KUI_VMU_WIZARD_RESET);
}
int main(void) {
    init();CHECK(!result.cards);
    run(KUI_VMU_WIZARD_LIST_GAMES);CHECK(result.status.errors && !inspects);
    run(KUI_VMU_WIZARD_COMMIT);CHECK(result.status.errors && !imports);
    run(KUI_VMU_WIZARD_CAPTURE);CHECK(result.cards==1 && result.summary[0].files==1);
    request.slot=1;run(KUI_VMU_WIZARD_CAPTURE);CHECK(result.cards==1 && result.summary[0].files==2);
    request.slot=0;run(KUI_VMU_WIZARD_CAPTURE);CHECK(result.cards==1 && result.sources==3 && result.summary[0].files==2);
    request.slot=2;run(KUI_VMU_WIZARD_CAPTURE);CHECK(result.cards==2 && result.card==1 && result.summary[1].files==1);
    capture_error=true;run(KUI_VMU_WIZARD_CAPTURE);CHECK(result.status.errors && result.cards==2 && result.sources==4);capture_error=false;
    request.card=0;run(KUI_VMU_WIZARD_LIST_GAMES);CHECK(result.stage==KUI_VMU_WIZARD_GAMES_STAGE && result.total==13);
    CHECK(strstr(result.status.message,"1 deeper folders not scanned"));
    CHECK(inspects==15 && !strcmp(result.games[0].title,"Alpha") && !result.games[1].supported);
    unsigned inspected=inspects;request.index=1;run(KUI_VMU_WIZARD_TOGGLE);CHECK(result.status.errors && !result.selected_games);
    request.index=0;run(KUI_VMU_WIZARD_TOGGLE);CHECK(result.selected_games==1 && result.summary[0].games==1);
    request.card=1;run(KUI_VMU_WIZARD_LIST_GAMES);CHECK(inspects==inspected);
    CHECK(strstr(result.status.message,"1 deeper folders not scanned"));
    run(KUI_VMU_WIZARD_TOGGLE);CHECK(result.selected_games==1 && result.summary[0].games==0 && result.summary[1].games==1);
    request.card=0;request.page=1;run(KUI_VMU_WIZARD_LIST_GAMES);CHECK(result.page==1 && result.count==5);
    request.index=0;run(KUI_VMU_WIZARD_TOGGLE);CHECK(result.status.errors && result.selected_games==1);
    request.index=12;run(KUI_VMU_WIZARD_TOGGLE);CHECK(result.selected_games==2 && result.summary[0].games==1);
    run(KUI_VMU_WIZARD_SHOW_REVIEW);CHECK(result.stage==KUI_VMU_WIZARD_REVIEW);
    fail_import=2;run(KUI_VMU_WIZARD_COMMIT);CHECK(result.status.errors && imports==2 && assigns==0 && result.summary[0].pair==2);
    CHECK(result.publication_started);cid=2;run(KUI_VMU_WIZARD_COMMIT);
    CHECK(result.status.errors && imports==2 && assigns==0 && strstr(result.status.message,"SD card or partition changed"));cid=1;
    run(KUI_VMU_WIZARD_SHOW_SOURCES);request.slot=1;run(KUI_VMU_WIZARD_CAPTURE);CHECK(result.status.errors && result.sources==4);
    run(KUI_VMU_WIZARD_LIST_GAMES);run(KUI_VMU_WIZARD_SHOW_REVIEW);
    fail_assign=1;run(KUI_VMU_WIZARD_COMMIT);CHECK(result.status.errors && imports==3 && assigns==1 && result.summary[1].pair==4);
    run(KUI_VMU_WIZARD_LIST_GAMES);request.index=12;run(KUI_VMU_WIZARD_TOGGLE);
    CHECK(result.status.errors && result.selected_games==2);run(KUI_VMU_WIZARD_SHOW_REVIEW);
    run(KUI_VMU_WIZARD_COMMIT);CHECK(!result.status.errors && result.stage==KUI_VMU_WIZARD_DONE && imports==3 && assigns==2);
    CHECK(saved_count==2 && !strcmp(saved[0].key,"ALPHA-1") && saved[0].pair==4);
    CHECK(!strcmp(saved[1].key,"GAMMA-1") && saved[1].pair==2);
    init();run(KUI_VMU_WIZARD_CAPTURE);request.slot=2;run(KUI_VMU_WIZARD_CAPTURE);CHECK(result.cards==2);
    request.card=0;run(KUI_VMU_WIZARD_LIST_GAMES);request.index=0;run(KUI_VMU_WIZARD_TOGGLE);
    run(KUI_VMU_WIZARD_SHOW_REVIEW);unsigned previous=imports;cancel_after=4;cancel_calls=0;
    run(KUI_VMU_WIZARD_COMMIT);CHECK(result.status.stopped && imports==previous+1 && result.stage==KUI_VMU_WIZARD_REVIEW);
    cancel_after=0;run(KUI_VMU_WIZARD_COMMIT);
    CHECK(result.stage==KUI_VMU_WIZARD_DONE && imports==previous+2 && result.summary[1].pair && !result.summary[1].games);
    init();run(KUI_VMU_WIZARD_CAPTURE);cancel_after=4;cancel_calls=0;run(KUI_VMU_WIZARD_LIST_GAMES);
    CHECK(result.status.stopped && !result.total);cancel_after=0;run(KUI_VMU_WIZARD_LIST_GAMES);CHECK(result.total==13);
    init();run(KUI_VMU_WIZARD_CAPTURE);scan_mode=1;inspected=inspects;run(KUI_VMU_WIZARD_LIST_GAMES);
    CHECK(result.status.errors && !result.total && inspects==inspected);
    scan_mode=0;run(KUI_VMU_WIZARD_LIST_GAMES);CHECK(result.total==13);
    init();strcpy(request.card_path,"0:/KUI/vmu-test/p0042/card.vmd");
    previous=imports;unsigned previous_reads=library_reads;
    run(KUI_VMU_WIZARD_USE_CARD);
    CHECK(!result.status.errors && result.existing_card && !result.copy_on_commit);
    CHECK(result.stage==KUI_VMU_WIZARD_GAMES_STAGE && result.cards==1 && result.summary[0].pair==42);
    CHECK(result.summary[0].files==0 && result.summary[0].blocks==0 && library_reads==previous_reads+1 && imports==previous);
    run(KUI_VMU_WIZARD_SHOW_SOURCES);run(KUI_VMU_WIZARD_CAPTURE);
    CHECK(result.status.errors && !result.sources);run(KUI_VMU_WIZARD_LIST_GAMES);
    request.index=0;run(KUI_VMU_WIZARD_TOGGLE);run(KUI_VMU_WIZARD_SHOW_REVIEW);
    CHECK(strstr(result.status.message,"existing virtual VMU"));
    allocation_error=true;unsigned previous_assigns=assigns;run(KUI_VMU_WIZARD_COMMIT);allocation_error=false;
    CHECK(result.status.errors && imports==previous && assigns==previous_assigns && !result.publication_started);
    target_changed=true;run(KUI_VMU_WIZARD_COMMIT);
    CHECK(result.status.errors && imports==previous && result.stage==KUI_VMU_WIZARD_REVIEW);
    target_changed=false;run(KUI_VMU_WIZARD_COMMIT);
    CHECK(!result.status.errors && result.stage==KUI_VMU_WIZARD_DONE && imports==previous);
    CHECK(saved_count==1 && saved[0].pair==42 && saved[0].identity[0]==0x42);
    for(unsigned kind=1;kind<=2;kind++) {
        init();card_kind=kind;strcpy(request.card_path,kind==1?
            "0:/KUI/vmu-test/p0042/card.vmd":"0:/KUI/vmu/cards/v0042/card.vmd");
        previous=imports;run(KUI_VMU_WIZARD_USE_CARD);
        CHECK(!result.status.errors && result.copy_on_commit && result.existing_card && !result.summary[0].pair);
        CHECK(imports==previous && strstr(result.status.message,"new shared copy"));
        request.index=0;run(KUI_VMU_WIZARD_TOGGLE);run(KUI_VMU_WIZARD_SHOW_REVIEW);
        CHECK(strstr(result.status.message,"shared copy"));run(KUI_VMU_WIZARD_COMMIT);
        CHECK(!result.status.errors && imports==previous+1 && saved_count==1 && saved[0].pair!=42);
    }
    for(unsigned kind=3;kind<=6;kind++) {
        init();run(KUI_VMU_WIZARD_CAPTURE);card_kind=kind;
        strcpy(request.card_path,"0:/KUI/vmu-test/p0042/card.vmd");previous=imports;
        run(KUI_VMU_WIZARD_USE_CARD);
        CHECK(result.status.errors && result.cards==1 && result.sources==1 && !result.existing_card && imports==previous);
    }
    init();card_kind=0;strcpy(request.card_path,"0:/KUI/vmu-test/p0042/../card.vmd");
    previous_reads=library_reads;run(KUI_VMU_WIZARD_USE_CARD);
    CHECK(result.status.errors && !result.cards && library_reads==previous_reads);
    init();run(KUI_VMU_WIZARD_CAPTURE);strcpy(request.card_path,"0:/KUI/vmu-test/p0042/card.vmd");
    swap_storage_on_read=true;run(KUI_VMU_WIZARD_USE_CARD);swap_storage_on_read=false;
    CHECK(result.status.errors && result.cards==1 && result.sources==1 && !result.existing_card);
    CHECK(strstr(result.status.message,"SD card or partition changed"));cid=1;
    cancel_after=2;cancel_calls=0;run(KUI_VMU_WIZARD_USE_CARD);cancel_after=0;
    CHECK(result.status.stopped && result.cards==1 && result.sources==1 && !result.existing_card);
    init();assignment_mode=1;request.view_only=true;strcpy(request.card_path,"0:/KUI/vmu-test/p0042/card.vmd");
    previous=imports;previous_assigns=assigns;unsigned previous_captures=captures;
    run(KUI_VMU_WIZARD_USE_CARD);
    CHECK(!result.status.errors && result.view_only && result.total==2 && !result.selected_games);
    CHECK(!strcmp(result.games[0].title,"Alpha") && !strcmp(result.games[1].title,"Unknown title: MISSING-1"));
    CHECK(imports==previous && assigns==previous_assigns && captures==previous_captures);
    request.view_only=false;request.index=0;run(KUI_VMU_WIZARD_TOGGLE);CHECK(result.status.errors && result.view_only);
    run(KUI_VMU_WIZARD_SHOW_REVIEW);CHECK(result.status.errors && result.view_only);
    run(KUI_VMU_WIZARD_COMMIT);CHECK(result.status.errors && result.view_only);
    run(KUI_VMU_WIZARD_CAPTURE);CHECK(result.status.errors && result.view_only);
    CHECK(imports==previous && assigns==previous_assigns && captures==previous_captures);
    run(KUI_VMU_WIZARD_LIST_GAMES);CHECK(!result.status.errors && result.total==2 && strstr(result.status.message,"read only"));
    run(KUI_VMU_WIZARD_OPEN);CHECK(!result.view_only && !result.cards && result.stage==KUI_VMU_WIZARD_SOURCES);
    init();assignment_mode=1;card_kind=0;run(KUI_VMU_WIZARD_CAPTURE);run(KUI_VMU_WIZARD_LIST_GAMES);
    request.index=0;run(KUI_VMU_WIZARD_TOGGLE);CHECK(result.cards==1 && result.selected_games==1);
    request.view_only=true;strcpy(request.card_path,"0:/KUI/vmu-test/p0042/card.vmd");run(KUI_VMU_WIZARD_USE_CARD);
    CHECK(result.view_only && result.total==2 && !result.selected_games);
    run(KUI_VMU_WIZARD_USE_CARD);CHECK(result.view_only && result.total==2);
    run(KUI_VMU_WIZARD_OPEN);
    CHECK(!result.view_only && result.cards==1 && result.sources==1 && result.selected_games==1 && result.total==13);
    CHECK(result.stage==KUI_VMU_WIZARD_GAMES_STAGE && !strcmp(result.games[0].title,"Alpha"));
    for(unsigned kind=0;kind<=1;kind++) {
        init();assignment_mode=0;card_kind=kind;request.view_only=true;
        strcpy(request.card_path,"0:/KUI/vmu-test/p0042/card.vmd");run(KUI_VMU_WIZARD_USE_CARD);
        CHECK(!result.status.errors && result.view_only && result.total==kind);
        if(kind) CHECK(!strcmp(result.games[0].title,"Alpha"));
    }
    init();assignment_mode=6;card_kind=1;request.view_only=true;
    strcpy(request.card_path,"0:/KUI/vmu-test/p0042/card.vmd");run(KUI_VMU_WIZARD_USE_CARD);
    CHECK(!result.status.errors && result.total==0 && strstr(result.status.message,"No explicit"));
    init();assignment_mode=7;card_kind=1;request.view_only=true;
    strcpy(request.card_path,"0:/KUI/vmu-test/p0042/card.vmd");run(KUI_VMU_WIZARD_USE_CARD);
    CHECK(!result.status.errors && result.total==2 && !strcmp(result.games[0].title,"Alpha") && !strcmp(result.games[1].title,"Gamma"));
    init();assignment_mode=2;card_kind=0;request.view_only=true;
    strcpy(request.card_path,"0:/KUI/vmu-test/p0042/card.vmd");run(KUI_VMU_WIZARD_USE_CARD);
    CHECK(!result.status.errors && result.total==1 && !strcmp(result.games[0].title,"Alpha"));
    for(unsigned mode=3;mode<=5;mode++) {
        init();assignment_mode=mode;card_kind=0;request.view_only=true;
        strcpy(request.card_path,"0:/KUI/vmu-test/p0042/card.vmd");run(KUI_VMU_WIZARD_USE_CARD);
        CHECK(result.status.errors && result.view_only && result.total==0);
    }
    init();assignment_mode=1;request.view_only=true;
    strcpy(request.card_path,"0:/KUI/vmu/cards/v0042/card.vmd");run(KUI_VMU_WIZARD_USE_CARD);
    CHECK(!result.status.errors && result.view_only && !result.total);
    init();card_kind=0;assignment_mode=1;request.view_only=true;cancel_on_assignment=true;
    strcpy(request.card_path,"0:/KUI/vmu-test/p0042/card.vmd");previous=imports;previous_assigns=assigns;
    run(KUI_VMU_WIZARD_USE_CARD);
    CHECK(result.status.stopped && result.view_only && !result.total && imports==previous && assigns==previous_assigns);
    cancel_on_assignment=cancel_latched=false;
    assignment_mode=0;
    init();printf("VMU wizard: %u checks passed\n",checks);return 0;
}
