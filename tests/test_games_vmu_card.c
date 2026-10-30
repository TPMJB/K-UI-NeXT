/* SPDX-License-Identifier: GPL-3.0-only */
/* The launcher's game-card glue (src/apps/games_vmu_card.inc) with a mocked
 * selection service and platform. argv[1] is the public build template. */
#include "kui/runtime.h"
#include "kui/retail_image.h"
#include "kui/media.h"
#include "kui/vmu_persist_profile.h"
#include "ff.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)) {fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x);exit(1);}} while(0)

struct kui_vmu_sd_identity;
bool kui_sd_raw_read_ops(struct kui_media_ops *out);
bool kui_sd_vmu_profile_identity(struct kui_vmu_sd_identity *out);
#include "../src/apps/games_vmu_card.inc"

static struct {
    int result,mounts,unmounts,raw_reads,assignment_result,assignment_reads,candidate_reads,identity_reads,exact_reads;
    int candidate_result;bool mounted,identity_fails,volume_moves,salts_differ;
    char problem[96];uint8_t fill;
    const struct kui_retail_manifest *map;
} mock={.candidate_result=KUI_VMU_GAME_CANDIDATES_ABSENT};
static struct kui_volume volume={2048u,1000000u,true};
static FATFS mounted_fs;
FRESULT f_mount(FATFS *fs,const TCHAR *path,BYTE opt) {
    CHECK(!strcmp(path,"0:"));
    if(!fs) {CHECK(!opt);++mock.unmounts;mock.mounted=false;return FR_OK;}
    CHECK(fs==&mounted_fs && opt==1u);++mock.mounts;mock.mounted=true;
    if(mock.volume_moves) volume.start+=8u;
    return FR_OK;
}
const struct kui_volume *kui_media_volume(void) {return &volume;}
static int raw_read(void *context,uint32_t block,size_t count,uint8_t *data) {
    CHECK(context==&mock && count==1u && !mock.mounted);
    ++mock.raw_reads;memset(data,(int)(block&255u),512u);return 0;
}
bool kui_sd_raw_read_ops(struct kui_media_ops *out) {
    *out=(struct kui_media_ops){.ctx=&mock,.read=raw_read};return true;
}
bool kui_sd_vmu_profile_identity(struct kui_vmu_sd_identity *out) {
    ++mock.identity_reads;
    if(mock.identity_fails) return false;
    memset(out,0,sizeof(*out));memset(out->cid,0x42,16u);out->sectors=4000000u;out->high_capacity=true;
    return true;
}
bool kui_clock_now(struct kui_datetime *out) {memset(out,0,sizeof(*out));return false;}
bool kui_clock_to_seconds(const struct kui_datetime *value,int64_t *out) {(void)value;*out=0;return false;}
static void log_line(const char *format,...) {(void)format;}
static bool never(void) {return false;}
int kui_vmu_game_candidates_list(const char *product,const struct kui_vmu_game_candidates_storage *storage,
    struct kui_vmu_game_candidates_view *out,kui_log_fn log,kui_cancel_fn cancel) {
    (void)log;CHECK(!strcmp(product,mock.map->product) && cancel==never && storage);
    CHECK(storage->partition_first==mock.map->partition_start && storage->partition_end==mock.map->partition_end);
    ++mock.candidate_reads;memset(out,0,sizeof(*out));return mock.candidate_result;
}

int kui_vmu_game_card_assignment(const char *product,struct kui_vmu_game_card_work *work,
    struct kui_vmu_game_card_choice *choice,kui_cancel_fn cancel) {
    CHECK(work && choice && cancel==never && !strcmp(product,mock.map->product));
    ++mock.assignment_reads;memset(choice,0,sizeof(*choice));choice->pair=23u;
    snprintf(choice->problem,sizeof(choice->problem),"%s",mock.problem);
    return mock.assignment_result;
}

int kui_vmu_game_card_select(const char *product,uint64_t partition_first,uint64_t partition_end,
    const struct kui_vmu_fs_range *forbidden,size_t forbidden_count,
    const struct kui_vmu_game_card_platform *platform,struct kui_vmu_game_card_work *work,
    uint8_t wire[KUI_VMU_PERSIST_PROFILE_WIRE_BYTES],struct kui_vmu_game_card_choice *choice,
    kui_log_fn log,kui_cancel_fn cancel) {
    CHECK(work && wire && choice && log==log_line && cancel==never);
    CHECK(!strcmp(product,mock.map->product) && partition_first==mock.map->partition_start &&
        partition_end==mock.map->partition_end && forbidden_count==mock.map->extent_count);
    for(size_t i=0;i<forbidden_count;i++) {
        const struct kui_retail_extent *e=&mock.map->slots[mock.map->track_count+i].extent;
        CHECK(forbidden[i].lba==e->card_lba && forbidden[i].blocks==e->blocks);
    }
    /* The platform is exercised exactly as the service uses it: identity
     * while mounted, then raw reads between unmount and remount. */
    struct kui_vmu_game_card_raw raw={0};
    CHECK(platform->identity(platform->context,&raw)==!mock.identity_fails && mock.mounted && !mock.unmounts);
    if(!mock.identity_fails) {
        CHECK(!memcmp(raw.cid,(uint8_t[16]){0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x42},16u));
        CHECK(raw.card_sectors==4000000u && raw.high_capacity);
    }
    uint8_t sector[512];
    CHECK(platform->raw_begin(platform->context,&raw) && mock.unmounts==1 && !mock.mounted);
    CHECK(!raw.read_sector(raw.context,0x1234u,sector) && sector[0]==0x34 && mock.raw_reads==1);
    CHECK(platform->raw_end(platform->context)==!mock.volume_moves && mock.mounted);
    uint8_t one[16],two[16];platform->salt(platform->context,one);platform->salt(platform->context,two);
    mock.salts_differ=memcmp(one,two,16u)!=0;
    memset(choice,0,sizeof(*choice));
    if(mock.result==KUI_VMU_GAME_CARD_READY) memset(wire,mock.fill,KUI_VMU_PERSIST_PROFILE_WIRE_BYTES);
    else memset(wire,0,KUI_VMU_PERSIST_PROFILE_WIRE_BYTES);
    snprintf(choice->problem,sizeof(choice->problem),"%s",mock.problem);
    return mock.result;
}
int kui_vmu_game_card_select_exact(const char *product,const struct kui_vmu_game_candidate_target *target,
    const struct kui_vmu_game_candidates_storage *storage,uint64_t first,uint64_t end,
    const struct kui_vmu_fs_range *forbidden,size_t count,const struct kui_vmu_game_card_platform *platform,
    struct kui_vmu_game_card_work *work,uint8_t wire[KUI_VMU_PERSIST_PROFILE_WIRE_BYTES],
    struct kui_vmu_game_card_choice *choice,kui_log_fn log,kui_cancel_fn cancel) {
    CHECK(target && target->pair==23 && target->identity[0]==0x23 && storage && storage->cid[0]==0x42);
    ++mock.exact_reads;
    return kui_vmu_game_card_select(product,first,end,forbidden,count,platform,work,wire,choice,log,cancel);
}

static uint8_t template_wire[4096];
static uint8_t payload[65536];
static struct kui_retail_manifest map;
static bool prepare(const char **problem) {
    struct kui_runtime_image package={0};
    package.data=payload;package.info.payload_bytes=sizeof(payload);
    mock.unmounts=mock.mounts=mock.raw_reads=0;mock.mounted=true;volume.start=2048u;
    *problem=NULL;
    return games_card_prepare(&map,&package,&mounted_fs,problem,log_line,never);
}
static void mode_put32(uint8_t *p,uint32_t value) {
    for(unsigned i=0;i<4u;i++) p[i]=(uint8_t)(value>>(8u*i));
}
static void toggle_capability(uint8_t *wire) {
    static const uint32_t words[8]={KUI_VMU_NATIVE_TOGGLE_MAGIC0,KUI_VMU_NATIVE_TOGGLE_MAGIC1,
        1u,32u,7u,0x51000u,0x30000u,1u};
    for(unsigned i=0;i<8u;i++) mode_put32(wire+i*4u,words[i]);
}
static void toggle_mode(uint8_t *wire,unsigned mode) {
    memset(wire,0,32u);memcpy(wire,"KUINVM01",8u);
    mode_put32(wire+8u,1u);mode_put32(wire+12u,32u);mode_put32(wire+16u,mode);
}
static void emulation_mode_controls(uint32_t lease_bytes) {
    const char *problem=NULL;
    struct kui_runtime_image package={.data=payload,.info={.payload_bytes=sizeof(payload)}};
    static uint8_t before[sizeof(payload)];
    memset(payload,0xa5,sizeof(payload));memcpy(before,payload,sizeof(before));
    CHECK(games_card_emulation_mode(&package,true,&problem));
    CHECK(!memcmp(payload,before,sizeof(payload)));
    CHECK(!games_card_emulation_mode(&package,false,&problem) && strstr(problem,"matching toggle-capable"));
    CHECK(!memcmp(payload,before,sizeof(payload)));
    toggle_capability(payload+1024u);mode_put32(payload+1044u,lease_bytes);
    memcpy(before,payload,sizeof(before));
    CHECK(!games_card_emulation_mode(&package,false,&problem) && strstr(problem,"mode is missing"));
    CHECK(!memcmp(payload,before,sizeof(payload)));
    toggle_mode(payload+2048u,KUI_VMU_NATIVE_MODE_NORMAL);
    /* A raw magic constant is not a second canonical launch-mode record. */
    memcpy(payload+512u,"KUINVM01",8u);
    memcpy(payload+8192u,template_wire,sizeof(template_wire));
    memcpy(before,payload,sizeof(before));problem=NULL;
    int lookups=mock.assignment_reads,reads=mock.raw_reads;
    CHECK(games_card_emulation_mode(&package,false,&problem) && !problem);
    for(size_t i=0;i<sizeof(payload);i++)
        CHECK(payload[i]==(i==2064u?7u:before[i]));
    CHECK(mock.assignment_reads==lookups && mock.raw_reads==reads);
    CHECK(!memcmp(payload+8192u,template_wire,sizeof(template_wire)));
    CHECK(games_card_emulation_mode(&package,false,&problem));
    CHECK(games_card_emulation_mode(&package,true,&problem));
    CHECK(!memcmp(payload,before,sizeof(payload)));
    /* Every refusal leaves the complete module and stored-card profile alone. */
    toggle_mode(payload+4096u,KUI_VMU_NATIVE_MODE_NORMAL);memcpy(before,payload,sizeof(before));
    CHECK(!games_card_emulation_mode(&package,false,&problem) && strstr(problem,"ambiguous"));
    CHECK(!memcmp(payload,before,sizeof(payload)));
    /* Duplicate and mixed-layout toggle capabilities both refuse. */
    memset(payload+4096u,0xa5,32u);toggle_capability(payload+4096u);memcpy(before,payload,sizeof(before));
    CHECK(!games_card_emulation_mode(&package,false,&problem));
    CHECK(!games_card_emulation_mode(&package,true,&problem));
    CHECK(!memcmp(payload,before,sizeof(payload)));
    memset(payload+4096u,0xa5,32u);toggle_mode(payload+2048u,KUI_VMU_NATIVE_MODE_PROFILE);
    memcpy(before,payload,sizeof(before));
    CHECK(!games_card_emulation_mode(&package,false,&problem) && strstr(problem,"damaged or ambiguous"));
    CHECK(!memcmp(payload,before,sizeof(payload)));
    toggle_mode(payload+2048u,0u);payload[2068u]=1u;memcpy(before,payload,sizeof(before));
    CHECK(!games_card_emulation_mode(&package,false,&problem));CHECK(!memcmp(payload,before,sizeof(payload)));
    puts("PASS native VMU launch toggle: exact capability/mode, old-module refusal, preserved reader/cache/profile bytes and zero card I/O");
}
int main(int argc,char **argv) {
    if(argc!=2) {fprintf(stderr,"Usage: %s TEMPLATE\n",argv[0]);return 2;}
    FILE *file=fopen(argv[1],"rb");CHECK(file && fread(template_wire,1,4096u,file)==4096u && !fclose(file));
    snprintf(map.product,sizeof(map.product),"MK-51136");
    map.partition_start=2048u;map.partition_end=1002048u;map.track_count=3u;map.extent_count=2u;
    map.slots[3].extent=(struct kui_retail_extent){.card_lba=500000u,.blocks=4096u};
    map.slots[4].extent=(struct kui_retail_extent){.card_lba=600000u,.blocks=128u};
    mock.map=&map;mounted_fs.fs_type=FS_EXFAT;mounted_fs.last_clst=77u;
    const char *problem;
    emulation_mode_controls(UINT32_C(0x51000));
    emulation_mode_controls(UINT32_C(0x47000));

    /* Unassigned games retain standalone/Legacy behavior. An explicit
     * import can only run through combined virtual A2, never silently fall
     * back to physical saving. Combined-ready continues into preparation. */
    mock.assignment_result=KUI_VMU_GAME_CARD_UNBOUND;
    CHECK(games_card_route(&map,true,false,true,&problem,never));
    CHECK(games_card_route(&map,true,false,false,&problem,never));
    mock.assignment_result=KUI_VMU_GAME_CARD_READY;
    CHECK(!games_card_route(&map,true,false,true,&problem,never) && strstr(problem,"standalone New"));
    CHECK(!games_card_route(&map,true,false,false,&problem,never) && strstr(problem,"combined New reader"));
    int lookups=mock.assignment_reads,catalogues=mock.candidate_reads,identities=mock.identity_reads;
    mock.candidate_result=KUI_VMU_GAME_CANDIDATES_INVALID;mock.identity_fails=true;
    CHECK(games_card_route(&map,false,false,false,&problem,never) && mock.assignment_reads==lookups);
    CHECK(games_card_route(&map,false,false,true,&problem,never) && mock.assignment_reads==lookups);
    CHECK(games_card_route(&map,false,true,false,&problem,never) && mock.assignment_reads==lookups);
    CHECK(games_card_route(&map,true,true,false,&problem,never) && mock.assignment_reads==lookups);
    CHECK(mock.candidate_reads==catalogues && mock.identity_reads==identities);
    mock.candidate_result=KUI_VMU_GAME_CANDIDATES_ABSENT;mock.identity_fails=false;
    snprintf(mock.problem,sizeof(mock.problem),"VMU game assignments are damaged");
    mock.assignment_result=KUI_VMU_GAME_CARD_FAILED;
    CHECK(!games_card_route(&map,true,false,false,&problem,never) && strstr(problem,"assignments are damaged"));
    mock.assignment_result=KUI_VMU_GAME_CARD_CANCELLED;
    CHECK(!games_card_route(&map,true,false,true,&problem,never) && strstr(problem,"cancelled"));
    memset(mock.problem,0,sizeof(mock.problem));

    /* A module without a profile slot cannot carry a card. */
    memset(payload,0xa5,sizeof(payload));
    CHECK(!prepare(&problem) && strstr(problem,"no card profile slot"));

    /* READY replaces only the slot. */
    memcpy(payload+8192,template_wire,4096u);
    static uint8_t before[65536];memcpy(before,payload,sizeof(payload));
    mock.result=KUI_VMU_GAME_CARD_READY;mock.fill=0x5c;
    CHECK(prepare(&problem) && !problem && mock.salts_differ);
    for(size_t i=0;i<sizeof(payload);i++)
        CHECK(payload[i]==(i>=8192u && i<12288u?0x5c:before[i]));
    /* The launch snapshot carries product/pair/identity/storage by value;
     * it cannot silently become a different game's default or row index. */
    memcpy(payload,before,sizeof(payload));
    struct kui_runtime_image selected_package={.data=payload,.info={.payload_bytes=sizeof(payload)}};
    struct kui_vmu_game_selection selected={.pair=23,.selected=true};
    strcpy(selected.product,map.product);memset(selected.identity,0x23,16u);
    selected.storage.cid[0]=0x42;selected.storage.card_sectors=4000000u;
    selected.storage.partition_first=map.partition_start;selected.storage.partition_end=map.partition_end;
    mock.unmounts=mock.mounts=mock.raw_reads=0;mock.mounted=true;
    CHECK(games_card_prepare_selected(&map,&selected_package,&mounted_fs,&selected,&problem,log_line,never));
    CHECK(mock.exact_reads==1);
    memcpy(payload,before,sizeof(payload));strcpy(selected.product,"ANOTHER");
    int exact=mock.exact_reads,raw=mock.raw_reads;
    CHECK(!games_card_prepare_selected(&map,&selected_package,&mounted_fs,&selected,&problem,log_line,never));
    CHECK(mock.exact_reads==exact && mock.raw_reads==raw && !memcmp(payload,before,sizeof(payload)));

    /* No product key: an installed card stays; the build template refuses. */
    memcpy(payload,before,sizeof(payload));
    mock.result=KUI_VMU_GAME_CARD_UNBOUND;
    CHECK(!prepare(&problem) && strstr(problem,"no product number"));
    CHECK(!memcmp(payload,before,sizeof(payload)));
    static struct kui_vmu_persist_profile installed;
    CHECK(!kui_vmu_persist_profile_decode(&installed,template_wire,4096u));
    installed.cid[0]^=1u;CHECK(!kui_vmu_persist_profile_encode(&installed,payload+8192));
    memcpy(before,payload,sizeof(payload));
    CHECK(prepare(&problem) && !problem && !memcmp(payload,before,sizeof(payload)));

    /* Refusals keep the slot and name the service's reason. */
    mock.result=KUI_VMU_GAME_CARD_CHOICE_REQUIRED;
    CHECK(!prepare(&problem) && strstr(problem,"choose a virtual VMU") && !memcmp(payload,before,sizeof(payload)));
    mock.result=KUI_VMU_GAME_CARD_FAILED;snprintf(mock.problem,sizeof(mock.problem),"p0002 and p0010 both claim MK-51136");
    CHECK(!prepare(&problem) && !strcmp(problem,"virtual card: p0002 and p0010 both claim MK-51136"));
    CHECK(!memcmp(payload,before,sizeof(payload)));
    mock.result=KUI_VMU_GAME_CARD_CANCELLED;
    CHECK(!prepare(&problem) && strstr(problem,"cancelled"));

    /* Platform failures are reported to the service, which refuses. */
    mock.result=KUI_VMU_GAME_CARD_FAILED;mock.identity_fails=true;
    CHECK(!prepare(&problem));mock.identity_fails=false;
    mock.volume_moves=true;CHECK(!prepare(&problem));mock.volume_moves=false;
    printf("games vmu card glue: %u checks\n",checks);
    return 0;
}
