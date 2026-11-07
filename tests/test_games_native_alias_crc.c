/* SPDX-License-Identifier: GPL-3.0-only */
/* Actual preparation/FatFs publication; only immutable SDK discovery is an
 * explicit pure adversary. It grants no console/game compatibility claim. */
#define main retail_fixture_main
#define __wrap_f_open fixture_open
#define __wrap_f_close fixture_close
#define __wrap_f_write fixture_write
#define __wrap_f_mkdir fixture_mkdir
#define __wrap_f_unlink fixture_unlink
#define __wrap_f_rename fixture_rename
#include "games_retail.c"
#undef main
#undef __wrap_f_open
#undef __wrap_f_close
#undef __wrap_f_write
#undef __wrap_f_mkdir
#undef __wrap_f_unlink
#undef __wrap_f_rename
#define main codec_fixture_main
#include "test_vmu_native_resolution_cache.c"
#undef main
#include "kui/retail_loader_layout.h"
#include "kui/vmu_native_cache_seed.h"
static FIL *cache_files[4];
static unsigned cache_opens,alias_publications;
static bool fail_alias,physical_off;
static bool cache_path(const char *p) {return !strncmp(p,"0:/KUI/VMU/native-cache",sizeof("0:/KUI/VMU/native-cache")-1u);}
static bool cache_file(FIL *f) {for(unsigned i=0;i<4u;i++)if(cache_files[i]==f)return true;return false;}
FRESULT __wrap_f_open(FIL *f,const TCHAR *p,BYTE flags) {
    if(!test.active || !cache_path(p))return fixture_open(f,p,flags);
    FRESULT result=__real_f_open(f,p,flags);++cache_opens;
    if(result==FR_OK){unsigned i;for(i=0;i<4u && cache_files[i];i++);assert(i<4u);cache_files[i]=f;++test.files;}
    return result;
}
FRESULT __wrap_f_close(FIL *f) {
    for(unsigned i=0;i<4u;i++)if(cache_files[i]==f){cache_files[i]=NULL;assert(test.files);--test.files;return __real_f_close(f);}
    return fixture_close(f);
}
FRESULT __wrap_f_write(FIL *f,const void *p,UINT n,UINT *put) {return cache_file(f)?__real_f_write(f,p,n,put):fixture_write(f,p,n,put);}
FRESULT __wrap_f_mkdir(const TCHAR *p) {
    if(test.active && (!strcmp(p,"0:/KUI") || !strcmp(p,"0:/KUI/VMU") || cache_path(p)))return __real_f_mkdir(p);
    return fixture_mkdir(p);
}
FRESULT __wrap_f_unlink(const TCHAR *p) {return test.active && cache_path(p)?__real_f_unlink(p):fixture_unlink(p);}
FRESULT __wrap_f_rename(const TCHAR *p,const TCHAR *to) {
    if(test.active && cache_path(p) && cache_path(to)) {
        if(strstr(to,"/hints/")){++alias_publications;if(fail_alias)return FR_DISK_ERR;}
        return __real_f_rename(p,to);
    }
    return fixture_rename(p,to);
}
bool __real_kui_vmu_native_resolution_cache_build(const void *,size_t,uint32_t,void *,size_t,struct kui_vmu_native_resolution *);
bool __wrap_kui_vmu_native_resolution_cache_build(const void *source,size_t bytes,uint32_t address,
    void *wire,size_t wire_bytes,struct kui_vmu_native_resolution *out) {
    assert(test.active && bytes==3001u && address==load && wire_bytes==1024u);
    assert(!__real_kui_vmu_native_resolution_cache_build(source,bytes,address,wire,wire_bytes,out));
    struct kui_vmu_native_resolution record;mock(source,bytes,&record);record.heap.initializer_slot=0x8c010100u;
    assert(kui_vmu_native_resolution_cache_encode(&record,wire,wire_bytes));*out=record;return true;
}
int main(int argc,char **argv) {
    assert(argc==6);fail_alias=!strcmp(argv[5],"failed-alias");physical_off=!strcmp(argv[5],"off");
    struct stat st;assert(!lstat(argv[1],&st));test.image=fopen(argv[1],"r+b");assert(test.image);
    test.blocks=(uint64_t)st.st_size/512u;test.fault=argv[4];
    if(!strcmp(argv[3],"seed")) {
        kui_media_set(&media);assert(kui_mount(&fs,log_line));seed(argv[2]);
        assert(kui_vmu_emulation_settings_save(!physical_off,log_line));
        assert(f_mount(NULL,"0:",0)==FR_OK);kui_media_set(NULL);
    } else {
        test.active=true;test.hash_begin[0]=0u;test.hash_end[0]=16u*track_stride(2);
        test.hash_begin[1]=21u*track_stride(2);test.hash_end[1]=23u*track_stride(2);test.hash_bounded=true;
        struct kui_runtime_image staged={0};struct kui_game_ram_patches patches={0};
        assert(kui_games_retail_prepare_launch(selected,KUI_RETAIL_READER_STANDARD,&patches,&staged,log_line,cancel));
        bool optional_deferred=!OLD_POLICY && !fail_alias && !physical_off && !cooked_track(2);
        unsigned expected_crc=(!cooked_track(2) && !physical_off && (OLD_POLICY || fail_alias))?1u:0u;
        assert(test.native_source_passes==1u && test.direct_crc_passes==expected_crc && test.hash_reads[1]==1u+expected_crc);
        assert(!test.files && !test.connected && test.connects==test.disconnects);
        struct kui_retail_manifest map;assert(!kui_retail_manifest_decode((uint8_t *)staged.data+KUI_RETAIL_MAP_OFFSET,&map));
        assert((bool)(map.flags&KUI_RETAIL_IMAGE_BOOT_CRC)==!optional_deferred);
        if(physical_off)assert(!cache_opens && !alias_publications);
        else assert(alias_publications==(!cooked_track(2)?1u:0u));
        /* Execute the same fresh length/SHA replay used after stage lifetime
         * proof. A changed loaded source must miss even with optional CRC off. */
        size_t n;uint8_t *track=host_file(argv[2],"track03.bin",&n);uint8_t loaded[3001];
        unsigned stride=track_stride(2),header=cooked_track(2)?0u:16u;
        for(unsigned i=0;i<sizeof(loaded);i++)loaded[i]=track[(21u+i/2048u)*stride+header+i%2048u];
        free(track);
        size_t program=0u;assert(kui_vmu_native_program_extent(loaded,sizeof(loaded),load,&program) && program==sizeof(loaded));
        size_t slot=kui_vmu_native_cache_seed_find(staged.data,staged.info.payload_bytes);assert(slot!=SIZE_MAX);
        const uint8_t *wire,*pin;assert(kui_vmu_native_cache_seed_read((uint8_t *)staged.data+slot,1120u,&wire,&pin));
        struct kui_vmu_native_resolution replayed;assert(kui_vmu_native_resolution_cache_replay(loaded,program,load,wire,1024u,pin,&replayed));
        loaded[100]^=1u;assert(!kui_vmu_native_resolution_cache_replay(loaded,program,load,wire,1024u,pin,&replayed));
        printf("ALIAS_CRC source=%u crc=%u reads=%u flags=%08x publishes=%u off=%u failed=%u\n",test.native_source_passes,test.direct_crc_passes,test.hash_reads[1],map.flags,alias_publications,physical_off,fail_alias);
        kui_runtime_free(&staged);
    }
    assert(!fclose(test.image));return 0;
}
