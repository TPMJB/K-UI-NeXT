/* SPDX-License-Identifier: GPL-3.0-only */
/* Run the production preparation helpers against generated PCM and exact
 * descriptor geometry. No Armada executable, kernel or audio is in this
 * fixture, and no storage writes or loader handoff occur. */
#include "../src/apps/games_retail.c"
#include <assert.h>

#define SEED_AT (KUI_RETAIL_STAGE_BLOB_OFFSET+128u)
#define SEED_TOTAL (KUI_ARMADA_CE_EXCERPT_SEED_BYTES+KUI_ARMADA_CE_EXCERPT_PCM_BYTES)
#define GUARD_BYTES 32u
static unsigned checks,reads,logs;
static enum kui_game_result read_result;
static struct kui_game_image image;
static struct kui_game_metadata metadata;
static struct kui_runtime_image package;
static uint8_t *allocation;
#define CHECK(value) do {++checks;assert(value);} while(0)

static uint8_t generated_byte(uint64_t at) {
    return (uint8_t)(at*17u+(at>>8)*5u+73u);
}
static enum kui_game_result generated_stat(void *ctx,const char *name,uint64_t *bytes) {
    CHECK(ctx==&image);
    for(unsigned i=0;i<image.count;i++) if(!strcmp(name,image.tracks[i].name)) {
        *bytes=image.tracks[i].file_bytes;return KUI_GAME_OK;
    }
    return KUI_GAME_NOT_FOUND;
}
static enum kui_game_result generated_read(void *ctx,const char *name,uint64_t offset,
        void *out,size_t bytes) {
    CHECK(ctx==&image && !strcmp(name,"track04.raw"));
    CHECK(offset==150u*2352u && bytes==150u*2352u);
    ++reads;
    uint8_t *data=out;
    size_t take=read_result==KUI_GAME_OK?bytes:16u;
    for(size_t i=0;i<take;i++) data[i]=generated_byte(offset+i);
    return read_result;
}
static void record_log(const char *format,...) {
    CHECK(format!=NULL);++logs;
}
static void exact_profile(void) {
    memset(&image,0,sizeof(image));memset(&metadata,0,sizeof(metadata));
    static const uint32_t starts[]={0u,756u,45000u,149842u,434062u};
    image.count=5u;image.data_lba=45000u;image.format=KUI_GAME_IMAGE_GDI;
    image.files=(struct kui_game_file_ops){&image,generated_stat,generated_read};
    for(unsigned i=0;i<image.count;i++) {
        struct kui_game_image_track *t=&image.tracks[i];
        t->number=i+1u;t->start_lba=starts[i];
        t->end_lba=i+1u<image.count?starts[i+1u]:starts[i]+1024u;
        t->sector_bytes=2352u;t->control=(i==1u || i==3u)?0u:4u;
        t->sector_mode=t->control?1u:0u;t->data_offset=t->control?16u:0u;
        t->file_bytes=(uint64_t)(t->end_lba-t->start_lba)*t->sector_bytes;
        snprintf(t->name,sizeof(t->name),"track%02u.raw",i+1u);
    }
    metadata.ip_valid=metadata.boot_valid=metadata.windows_ce=true;
    metadata.boot_bytes=1253376u;metadata.boot_lba=45166u;
    strcpy(metadata.product,"T40301N 00");strcpy(metadata.bootfile,"0WINCEOS.BIN");
    reads=logs=0u;read_result=KUI_GAME_OK;
}
static void profile_admission(void) {
    exact_profile();CHECK(armada_excerpt_profile(&image,&metadata));
    const struct kui_game_image original=image;
    const struct kui_game_metadata original_metadata=metadata;
    for(unsigned fault=0;fault<9u;fault++) {
        image=original;metadata=original_metadata;
        if(fault==0u) image.format=KUI_GAME_IMAGE_ISO;
        if(fault==1u) image.count=4u;
        if(fault==2u) image.cd_image=true;
        if(fault==3u) image.scrambled=true;
        if(fault==4u) metadata.windows_ce=false;
        if(fault==5u) metadata.boot_bytes--;
        if(fault==6u) strcpy(metadata.product,"T00000N 00");
        if(fault==7u) strcpy(metadata.bootfile,"OTHER.BIN");
        if(fault==8u) image.tracks[3].end_lba=KUI_ARMADA_CE_EXCERPT_START_LBA+149u;
        CHECK(!armada_excerpt_profile(&image,&metadata) && !reads);
    }
    for(unsigned track=0;track<5u;track++) for(unsigned fault=0;fault<4u;fault++) {
        image=original;metadata=original_metadata;
        if(fault==0u) image.tracks[track].start_lba++;
        if(fault==1u) image.tracks[track].file_offset=512u;
        if(fault==2u) image.tracks[track].sector_bytes=2048u;
        if(fault==3u) image.tracks[track].control=image.tracks[track].control?0u:4u;
        CHECK(!armada_excerpt_profile(&image,&metadata) && !reads);
    }
    image=original;metadata=original_metadata;
    /* A user-selected cooked version can share metadata but must fail before
     * any PCM read, regardless of where it is stored or how it is named. */
    image.tracks[2].sector_bytes=2048u;image.tracks[2].data_offset=0u;
    CHECK(!armada_excerpt_profile(&image,&metadata));
    CHECK(!reads);
}
static void new_package(size_t payload_bytes,bool include_seed) {
    free(allocation);
    allocation=calloc(1,payload_bytes+2u*GUARD_BYTES);CHECK(allocation!=NULL);
    memset(allocation,0xa5,GUARD_BYTES);
    memset(allocation+GUARD_BYTES+payload_bytes,0xa5,GUARD_BYTES);
    package=(struct kui_runtime_image){allocation+GUARD_BYTES,
        {.payload_bytes=(uint32_t)payload_bytes,.memory_bytes=(uint32_t)payload_bytes}};
    if(include_seed) {
        CHECK(payload_bytes>=SEED_AT+SEED_TOTAL);
        CHECK(kui_armada_ce_excerpt_seed_make((uint32_t *)(void *)((uint8_t *)package.data+SEED_AT),0u));
    }
    reads=logs=0u;read_result=KUI_GAME_OK;
}
static uint32_t *seed(void) {return (uint32_t *)(void *)((uint8_t *)package.data+SEED_AT);}
static uint8_t *pcm(void) {return (uint8_t *)package.data+SEED_AT+64u;}
static void guard_check(void) {
    for(unsigned i=0;i<GUARD_BYTES;i++) {
        CHECK(allocation[i]==0xa5);
        CHECK(allocation[GUARD_BYTES+package.info.payload_bytes+i]==0xa5);
    }
}
static void seed_success(void) {
    exact_profile();new_package(SEED_AT+SEED_TOTAL+64u,true);
    CHECK(armada_excerpt_seed(&package,&image,record_log));
    CHECK(reads==1u && logs==1u);
    /* Independent Python zlib CRC over generated file bytes at [352800,705600). */
    CHECK(seed()[KUI_ARMADA_CE_EXCERPT_SEED_PCM_CRC32]==UINT32_C(0xac6048a8));
    for(size_t i=0;i<KUI_ARMADA_CE_EXCERPT_PCM_BYTES;i++)
        CHECK(pcm()[i]==generated_byte(150u*2352u+i));
    for(size_t i=0;i<SEED_AT;i++) CHECK(!((uint8_t *)package.data)[i]);
    for(size_t i=SEED_AT+SEED_TOTAL;i<package.info.payload_bytes;i++)
        CHECK(!((uint8_t *)package.data)[i]);
    guard_check();
    CHECK(!armada_excerpt_seed(&package,&image,record_log));
    CHECK(reads==1u && logs==1u); /* Never preload an already populated template. */
}
static void seed_rejections(void) {
    exact_profile();new_package(SEED_AT+SEED_TOTAL,true);
    for(unsigned word=0;word<16u;word++) {
        new_package(SEED_AT+SEED_TOTAL,true);seed()[word]^=1u;
        CHECK(!armada_excerpt_seed(&package,&image,record_log));
        CHECK(!reads && !logs);guard_check();
    }
    static const size_t dirty[]={0u,KUI_ARMADA_CE_EXCERPT_PCM_BYTES/2u,KUI_ARMADA_CE_EXCERPT_PCM_BYTES-1u};
    for(unsigned i=0;i<sizeof(dirty)/sizeof(*dirty);i++) {
        new_package(SEED_AT+SEED_TOTAL,true);pcm()[dirty[i]]=1u;
        CHECK(!armada_excerpt_seed(&package,&image,record_log));
        CHECK(!reads && !logs && !seed()[KUI_ARMADA_CE_EXCERPT_SEED_PCM_CRC32]);guard_check();
    }
    new_package(SEED_AT+2u*SEED_TOTAL,true);
    CHECK(kui_armada_ce_excerpt_seed_make((uint32_t *)(void *)((uint8_t *)package.data+SEED_AT+SEED_TOTAL),0u));
    CHECK(!armada_excerpt_seed(&package,&image,record_log) && !reads && !logs);guard_check();
    /* Missing, truncated, unaligned and pre-stage templates are all refused
     * before file I/O. Guard bytes expose any overread/overwrite under ASan. */
    static const size_t lengths[]={0u,4096u,SEED_AT+63u,SEED_AT+SEED_TOTAL-1u};
    for(unsigned i=0;i<sizeof(lengths)/sizeof(*lengths);i++) {
        new_package(lengths[i],false);
        CHECK(!armada_excerpt_seed(&package,&image,record_log) && !reads && !logs);guard_check();
    }
    new_package(SEED_AT+SEED_TOTAL,false);
    CHECK(kui_armada_ce_excerpt_seed_make((uint32_t *)(void *)((uint8_t *)package.data+KUI_RETAIL_STAGE_BLOB_OFFSET-64u),0u));
    CHECK(!armada_excerpt_seed(&package,&image,record_log) && !reads && !logs);guard_check();
    new_package(SEED_AT+SEED_TOTAL+4u,false);
    uint32_t words[16];CHECK(kui_armada_ce_excerpt_seed_make(words,0u));
    memcpy((uint8_t *)package.data+SEED_AT+1u,words,sizeof(words));
    CHECK(!armada_excerpt_seed(&package,&image,record_log) && !reads && !logs);guard_check();
    for(unsigned fault=0;fault<2u;fault++) {
        new_package(SEED_AT+SEED_TOTAL,true);
        read_result=fault?KUI_GAME_CANCELLED:KUI_GAME_IO;
        CHECK(!armada_excerpt_seed(&package,&image,record_log));
        CHECK(reads==1u && logs==1u && !seed()[KUI_ARMADA_CE_EXCERPT_SEED_PCM_CRC32]);guard_check();
    }
    /* Even a typed package cannot make a truncated source range readable. */
    exact_profile();new_package(SEED_AT+SEED_TOTAL,true);
    image.tracks[3].end_lba=KUI_ARMADA_CE_EXCERPT_START_LBA+149u;
    CHECK(!armada_excerpt_seed(&package,&image,record_log));
    CHECK(!reads && logs==1u && !seed()[KUI_ARMADA_CE_EXCERPT_SEED_PCM_CRC32]);guard_check();
}
/* A read-only filesystem model drives the entire public prepare function.
 * Its sectors and runtime package are generated; the original parser,
 * range checker, extent mapper, manifest encoder and seed writer still run. */
static const char descriptor[]=
    "5\n1 0 4 2352 track01.raw 0\n2 756 0 2352 track02.raw 0\n"
    "3 45000 4 2352 track03.raw 0\n4 149842 0 2352 track04.raw 0\n"
    "5 434062 4 2352 track05.raw 0\n";
static struct {
    bool connected,mounted,cancelled,missing_seed,read_fail,cancel_read;
    bool stream;
    unsigned fragments;
    unsigned connects,disconnects,opens,closes,preloads,package_reads;
    unsigned transport;
    FIL *active;
    FATFS *fs;
    char name[KUI_GAME_NAME_CAP],package_path[96];
} launch;
static const struct kui_volume launch_volume={.start=2048u,.count=4000000u};
static uint64_t fixture_size(const char *name) {
    if(!strcmp(name,"ARMADA.gdi")) return sizeof(descriptor)-1u;
    for(unsigned i=0;i<image.count;i++)
        if(!strcmp(name,image.tracks[i].name)) return image.tracks[i].file_bytes;
    return 0u;
}
bool kui_sd_connect(void) {
    CHECK(!launch.connected);launch.connected=true;++launch.connects;return true;
}
void kui_sd_disconnect(void) {
    CHECK(launch.connected && !launch.mounted && !launch.active);
    launch.connected=false;++launch.disconnects;
}
unsigned kui_storage_active(void) {return launch.transport;}
bool kui_mount(FATFS *fs,kui_log_fn log) {
    CHECK(launch.connected && !launch.mounted && log!=NULL);
    memset(fs,0,sizeof(*fs));fs->csize=1u;fs->database=50u;fs->n_fatent=4000000u;
    launch.fs=fs;launch.mounted=true;return true;
}
const struct kui_volume *kui_media_volume(void) {return &launch_volume;}
FRESULT f_mount(FATFS *fs,const TCHAR *path,BYTE option) {
    CHECK(!fs && !strcmp(path,"0:") && !option && !launch.active && launch.mounted);
    launch.mounted=false;return FR_OK;
}
FRESULT f_stat(const TCHAR *path,FILINFO *info) {
    CHECK(launch.connected && launch.mounted);
    const char *name=strrchr(path,'/');CHECK(name!=NULL);
    uint64_t bytes=fixture_size(name+1u);
    if(!bytes) return FR_NO_FILE;
    memset(info,0,sizeof(*info));info->fsize=bytes;return FR_OK;
}
FRESULT f_open(FIL *file,const TCHAR *path,BYTE mode) {
    CHECK(launch.connected && launch.mounted && !launch.active && mode==FA_READ);
    const char *name=strrchr(path,'/');CHECK(name!=NULL);
    uint64_t bytes=fixture_size(name+1u);
    if(!bytes) return FR_NO_FILE;
    memset(file,0,sizeof(*file));file->obj.fs=launch.fs;file->obj.objsize=bytes;
    launch.active=file;snprintf(launch.name,sizeof(launch.name),"%s",name+1u);
    ++launch.opens;return FR_OK;
}
FRESULT f_close(FIL *file) {
    CHECK(file==launch.active && !file->cltbl);
    launch.active=NULL;++launch.closes;return FR_OK;
}
FRESULT f_lseek(FIL *file,FSIZE_t offset) {
    CHECK(file==launch.active);
    if(offset==CREATE_LINKMAP) {
        CHECK(file->cltbl && file->cltbl[0]>=4u);
        unsigned track=0;CHECK(sscanf(launch.name,"track%u.raw",&track)==1);
        uint32_t runs=track==4u && launch.fragments?launch.fragments:1u;
        uint32_t required=2u+2u*runs,capacity=file->cltbl[0];
        file->cltbl[0]=required;
        if(capacity<required) return FR_NOT_ENOUGH_CORE;
        uint32_t blocks=(uint32_t)((file->obj.objsize+511u)/512u),done=0;
        CHECK(runs<=blocks);
        for(uint32_t i=0;i<runs;i++) {
            uint32_t count=blocks/runs+(i<blocks%runs?1u:0u);
            file->cltbl[1u+2u*i]=count;
            file->cltbl[2u+2u*i]=1000u+track*100000u+done+i;
            done+=count;
        }
        file->cltbl[1u+2u*runs]=0u;
    } else {
        CHECK(offset<=file->obj.objsize);file->fptr=offset;
    }
    return FR_OK;
}
FRESULT f_read(FIL *file,void *out,UINT bytes,UINT *got) {
    CHECK(file==launch.active && bytes<=file->obj.objsize-file->fptr);
    uint8_t *data=out;uint64_t offset=file->fptr;
    if(!strcmp(launch.name,"ARMADA.gdi")) {
        memcpy(out,descriptor+offset,bytes);
    } else {
        unsigned track=0;CHECK(sscanf(launch.name,"track%u.raw",&track)==1);
        if(track==4u) {
            CHECK(offset==150u*2352u && bytes==150u*2352u);++launch.preloads;
            if(launch.read_fail) {*got=0u;return FR_DISK_ERR;}
            if(launch.cancel_read) launch.cancelled=true;
        }
        for(size_t i=0;i<bytes;i++) data[i]=generated_byte(offset+i);
        if(track==1u || track==3u || track==5u) {
            CHECK(bytes==2352u && !(offset%2352u));
            data[0]=data[11]=0u;memset(data+1u,255,10u);data[15]=1u;
        }
    }
    file->fptr+=bytes;*got=bytes;return FR_OK;
}
static void put32(uint8_t *out,uint32_t value) {
    for(unsigned i=0;i<4u;i++) out[i]=(uint8_t)(value>>(8u*i));
}
enum kui_runtime_result kui_runtime_read(const char *path,struct kui_runtime_image *out,
        kui_log_fn log,kui_cancel_fn cancel) {
    CHECK(launch.connected && launch.mounted && !out->data && log && cancel);
    ++launch.package_reads;snprintf(launch.package_path,sizeof(launch.package_path),"%s",path);
    bool ce=!strcmp(path,KUI_GAMES_RETAIL_CE_PACKAGE) || !strcmp(path,KUI_GAMES_RETAIL_CE_EXCERPT_PACKAGE);
    bool excerpt=!strcmp(path,KUI_GAMES_RETAIL_CE_EXCERPT_PACKAGE);
    CHECK(ce || !strcmp(path,KUI_GAMES_RETAIL_PACKAGE));
    uint32_t bytes=excerpt?SEED_AT+(launch.stream?sizeof(struct kui_armada_ce_stream_seed):SEED_TOTAL)+64u:KUI_RETAIL_STAGE_BLOB_OFFSET+16u;
    uint8_t *data=calloc(1,bytes);CHECK(data!=NULL);
    *out=(struct kui_runtime_image){data,{.payload_bytes=bytes,.memory_bytes=bytes}};
    uint8_t *h=data+KUI_RETAIL_HEADER_OFFSET;
    memcpy(h,ce?KUI_RETAIL_CE_PACKAGE_MAGIC:KUI_RETAIL_PACKAGE_MAGIC,8u);
    uint32_t stage=ce?KUI_RETAIL_CE_STAGE_ADDRESS:KUI_RETAIL_STAGE_ADDRESS;
    const uint32_t words[]={KUI_RETAIL_PACKAGE_VERSION,KUI_RETAIL_HEADER_BYTES,
        KUI_RETAIL_MAP_OFFSET,KUI_RETAIL_IMAGE_WIRE_BYTES,stage,bytes-KUI_RETAIL_STAGE_BLOB_OFFSET,
        stage,KUI_RETAIL_EXEC_ADDRESS,KUI_RETAIL_EXEC_MAX_BYTES,KUI_RETAIL_STAGE_STACK,
        KUI_RETAIL_STAGE_BLOB_OFFSET,KUI_RETAIL_LEGACY_RESIDENT_ADDRESS,KUI_RETAIL_LEGACY_STANDARD_LIMIT,0u};
    for(unsigned i=0;i<sizeof(words)/sizeof(*words);i++) put32(h+8u+4u*i,words[i]);
    if(excerpt && !launch.missing_seed) {
        if(launch.stream) {
            const uint32_t words[16]=KUI_ARMADA_CE_STREAM_BLANK_WORDS;
            memcpy(data+SEED_AT,words,sizeof(words));
        } else CHECK(kui_armada_ce_excerpt_seed_make((uint32_t *)(void *)(data+SEED_AT),0u));
    }
    return KUI_RUNTIME_OK;
}
void kui_runtime_free(struct kui_runtime_image *out) {
    free(out->data);*out=(struct kui_runtime_image){0};
}
const char *kui_runtime_result_name(enum kui_runtime_result result) {
    (void)result;return "fixture runtime result";
}
enum kui_game_metadata_status kui_game_metadata_read(const struct kui_game_metadata_ops *ops,
        uint32_t session_lba,struct kui_game_metadata *out) {
    CHECK(ops && ops->ctx && ops->read_sector && ops->data_range && session_lba==45000u);
    CHECK(ops->data_range(ops->ctx,session_lba,16u));
    *out=metadata;return KUI_GAME_METADATA_OK;
}
const char *kui_game_metadata_status_text(enum kui_game_metadata_status status) {
    (void)status;return "fixture metadata result";
}
static bool launch_cancel(void) {return launch.cancelled;}
static void launch_setup(void) {
    CHECK(!launch.connected && !launch.mounted && !launch.active);
    memset(&launch,0,sizeof(launch));launch.transport=KUI_STORAGE_SCI;
    exact_profile();
    /* Sparse disc coordinates require no enormous copyrighted fixture files. */
    static const uint32_t sectors[]={756u,75u,1024u,450u,16u};
    for(unsigned i=0;i<5u;i++) {
        image.tracks[i].end_lba=image.tracks[i].start_lba+sectors[i];
        image.tracks[i].file_bytes=(uint64_t)sectors[i]*2352u;
    }
}
static void launcher_paths(void) {
    const uint32_t excerpt=KUI_GAMES_RETAIL_CE_PROBE|KUI_GAMES_RETAIL_CE_EXCERPT|KUI_RETAIL_READER_ASYNC;
    const uint32_t invalid[]={KUI_GAMES_RETAIL_CE_EXCERPT|KUI_RETAIL_READER_ASYNC,
        KUI_GAMES_RETAIL_CE_PROBE|KUI_GAMES_RETAIL_CE_EXCERPT,
        excerpt|KUI_RETAIL_READER_ASYNC_EAGER,excerpt|KUI_GAMES_RETAIL_CDDA,
        excerpt|KUI_GAMES_RETAIL_DESCRAMBLE,excerpt|KUI_GAMES_RETAIL_BOOT_PLAIN,excerpt|0x80000000u};
    struct kui_runtime_image prepared;
    for(unsigned i=0;i<sizeof(invalid)/sizeof(*invalid);i++) {
        launch_setup();memset(&prepared,0x5a,sizeof(prepared));
        CHECK(!kui_games_retail_prepare_reader("/Games/ARMADA/ARMADA.gdi",invalid[i],&prepared,record_log,launch_cancel));
        CHECK(!prepared.data && !prepared.info.payload_bytes && !launch.connects && !launch.package_reads);
    }
    for(unsigned route=0;route<3u;route++) {
        launch_setup();uint32_t request=route==0u?excerpt:route==1u?
            KUI_GAMES_RETAIL_CE_PROBE|KUI_RETAIL_READER_ASYNC:KUI_RETAIL_READER_STANDARD;
        if(route==2u) {
            metadata.windows_ce=false;metadata.native_gd=true;metadata.boot_bytes=123456u;
            strcpy(metadata.bootfile,"1ST_READ.BIN");
        }
        CHECK(kui_games_retail_prepare_reader("/Games/ARMADA/ARMADA.gdi",request,&prepared,record_log,launch_cancel));
        const char *expected=route==0u?KUI_GAMES_RETAIL_CE_EXCERPT_PACKAGE:route==1u?
            KUI_GAMES_RETAIL_CE_PACKAGE:KUI_GAMES_RETAIL_PACKAGE;
        CHECK(!strcmp(launch.package_path,expected) && launch.package_reads==1u);
        CHECK(launch.preloads==(route==0u?1u:0u));
        CHECK(launch.connects==1u && launch.disconnects==1u && launch.opens==launch.closes);
        struct kui_retail_manifest map;
        CHECK(kui_retail_manifest_decode((uint8_t *)prepared.data+KUI_RETAIL_MAP_OFFSET,&map)==KUI_GAME_OK);
        CHECK(map.reader==(route==2u?KUI_RETAIL_READER_STANDARD:KUI_RETAIL_READER_ASYNC));
        CHECK(map.track_count==5u && map.extent_count==5u && map.storage_transport==KUI_STORAGE_SCI);
        if(!route) {
            const uint32_t *words=(const uint32_t *)(const void *)((uint8_t *)prepared.data+SEED_AT);
            CHECK(words[KUI_ARMADA_CE_EXCERPT_SEED_PCM_CRC32]==UINT32_C(0xac6048a8));
        }
        kui_runtime_free(&prepared);
    }
    for(unsigned fault=0;fault<6u;fault++) {
        launch_setup();
        if(fault==0u) strcpy(metadata.product,"T00000N 00");
        if(fault==1u) launch.transport=KUI_STORAGE_SCIF;
        if(fault==2u) image.tracks[3].end_lba=image.tracks[3].start_lba+299u,
            image.tracks[3].file_bytes=299u*2352u;
        if(fault==3u) launch.missing_seed=true;
        if(fault==4u) launch.read_fail=true;
        if(fault==5u) launch.cancel_read=true;
        CHECK(!kui_games_retail_prepare_reader("/Games/ARMADA/ARMADA.gdi",excerpt,&prepared,record_log,launch_cancel));
        CHECK(!prepared.data && !prepared.info.payload_bytes && !prepared.info.memory_bytes);
        CHECK(launch.package_reads==1u && !strcmp(launch.package_path,KUI_GAMES_RETAIL_CE_EXCERPT_PACKAGE));
        CHECK(launch.preloads==(fault>=4u?1u:0u));
        CHECK(launch.connects==1u && launch.disconnects==1u && launch.opens==launch.closes);
        CHECK(!launch.connected && !launch.mounted && !launch.active);
    }
}
static void stream_launcher_paths(void) {
    const uint32_t request=KUI_GAMES_RETAIL_CE_PROBE|KUI_GAMES_RETAIL_CE_EXCERPT|KUI_RETAIL_READER_ASYNC;
    struct kui_runtime_image prepared;
    for(unsigned fragmented=0;fragmented<2u;fragmented++) {
        launch_setup();launch.stream=true;launch.fragments=fragmented?70u:0u;
        CHECK(kui_games_retail_prepare_reader("/Games/ARMADA/ARMADA.gdi",request,&prepared,record_log,launch_cancel));
        CHECK(!launch.preloads && !launch.connected && !launch.mounted && launch.opens==launch.closes);
        CHECK(armada_stream_seed_find(&prepared)==SEED_AT);
        struct kui_armada_ce_stream_seed *seed=(void *)((uint8_t *)prepared.data+SEED_AT);
        struct kui_retail_manifest full,data;
        CHECK(kui_armada_ce_stream_seed_header_valid(seed->words));
        CHECK(seed->words[KUI_ARMADA_CE_STREAM_MAP_CRC_WORD]==le32(seed->wire+16u));
        CHECK(kui_retail_manifest_decode(seed->wire,&full)==KUI_GAME_OK);
        CHECK(kui_armada_ce_stream_map_valid(&full));
        CHECK(kui_retail_manifest_decode((uint8_t *)prepared.data+KUI_RETAIL_MAP_OFFSET,&data)==KUI_GAME_OK);
        CHECK(kui_armada_ce_stream_maps_match(&full,&data));
        CHECK(full.reader==KUI_RETAIL_READER_STANDARD && data.reader==KUI_RETAIL_READER_ASYNC);
        CHECK(full.slots[3].track.extent_count==(fragmented?70u:1u));
        CHECK(data.slots[3].track.extent_count==(fragmented?0u:1u));
        CHECK(full.slots[3].track.end_lba==150292u); /* Actual450-sector file, not next track434062. */
        struct kui_retail_manifest original=full;
        for(unsigned fault=0;fault<8u;fault++) {
            full=original;
            if(fault==0u) full.storage_transport=KUI_STORAGE_SCIF;
            if(fault==1u) full.reader=KUI_RETAIL_READER_ASYNC;
            if(fault==2u) full.slots[3].track.extent_count=0u;
            if(fault==3u) full.slots[1].track.control=4u;
            if(fault==4u) full.product[0]^=1;
            if(fault==5u) full.bootfile[0]^=1;
            if(fault==6u) full.slots[3].track.start_lba++;
            if(fault==7u) full.boot_bytes--;
            CHECK(!kui_armada_ce_stream_map_valid(&full));
        }
        full=original;full.slots[3].track.end_lba++;
        CHECK(!kui_armada_ce_stream_maps_match(&full,&data));
        kui_runtime_free(&prepared);
    }
    for(unsigned fault=0;fault<3u;fault++) {
        launch_setup();launch.stream=true;
        if(fault==0u) launch.fragments=252u;
        if(fault==1u) launch.missing_seed=true;
        if(fault==2u) launch.transport=KUI_STORAGE_SCIF;
        CHECK(!kui_games_retail_prepare_reader("/Games/ARMADA/ARMADA.gdi",request,&prepared,record_log,launch_cancel));
        CHECK(!prepared.data && !launch.preloads && !launch.connected && launch.opens==launch.closes);
    }
    const uint32_t blank[16]=KUI_ARMADA_CE_STREAM_BLANK_WORDS;
    for(unsigned word=0;word<16u;word++) {
        uint32_t words[16];memcpy(words,blank,sizeof(words));words[word]^=1u;
        CHECK(kui_armada_ce_stream_seed_header_valid(words)==(word==KUI_ARMADA_CE_STREAM_MAP_CRC_WORD));
    }
}
int main(void) {
    profile_admission();seed_success();seed_rejections();free(allocation);allocation=NULL;
    launcher_paths();
    stream_launcher_paths();
    printf("Armada CE excerpt preparation: %u checks; exact raw profile, generated 150-sector preload, fixed CRC, unique empty seed, bounds, package isolation and failed/cancelled reads passed\n",checks);
    return 0;
}
