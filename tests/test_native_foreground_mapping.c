/* SPDX-License-Identifier: GPL-3.0-only */
/* Execute production map_tracks/map_track with explicit FatFs extent fixtures.
 * This does not mount a filesystem or replay a console storage device. */
#include "../src/apps/games_retail.c"
#include <assert.h>
#include <stdio.h>
static FATFS fixture_fs;
static struct kui_game_image fixture_image;
static unsigned opens,closes,fragments=1;
static unsigned track_fragments[40];
static int fail_open=-1;
static unsigned current_track;
static unsigned pieces_for(unsigned track) {
    return track_fragments[track]?track_fragments[track]:fragments;
}
bool kui_destination_name_valid(const char *name) { return name && name[0]; }
FRESULT f_open(FIL *file,const TCHAR *name,BYTE mode) {
    unsigned number=0;const char *leaf=strrchr(name,'/');
    assert(leaf && sscanf(leaf+1,"track%u.bin",&number)==1 && number>=1 && number<=40);
    assert(mode==FA_READ);current_track=number-1;
    if((int)current_track==fail_open)return FR_DISK_ERR;
    memset(file,0,sizeof(*file));file->obj.fs=&fixture_fs;
    file->obj.objsize=fixture_image.tracks[current_track].file_bytes;
    ++opens;return FR_OK;
}
FRESULT f_lseek(FIL *file,FSIZE_t offset) {
    unsigned pieces=pieces_for(current_track);
    assert(offset==CREATE_LINKMAP && file->cltbl && file->cltbl[0]>=2u+pieces*2u);
    DWORD *map=file->cltbl;unsigned blocks=(unsigned)((file->obj.objsize+511u)/512u);
    assert(blocks>=pieces);map[0]=2u+pieces*2u;
    for(unsigned i=0;i<pieces;i++) {
        map[1u+2u*i]=i+1u==pieces?blocks-i:1u;
        map[2u+2u*i]=100u+current_track*100u+i*3u;
    }
    map[1u+2u*pieces]=0u;return FR_OK;
}
FRESULT f_close(FIL *file) {assert(file->obj.fs==&fixture_fs);++closes;return FR_OK;}
static void fixture(void) {
    memset(&fixture_fs,0,sizeof(fixture_fs));fixture_fs.csize=1;
    fixture_fs.database=32;fixture_fs.n_fatent=100000;
    memset(&fixture_image,0,sizeof(fixture_image));fixture_image.count=40;
    for(unsigned i=0;i<40;i++) {
        struct kui_game_image_track *t=&fixture_image.tracks[i];
        t->number=i+1;t->start_lba=i*8;t->end_lba=i*8+4;
        t->control=(i==0 || i==2 || i==39)?4:0;
        t->sector_bytes=2352;t->file_bytes=4u*2352u;
        snprintf(t->name,sizeof(t->name),"track%02u.bin",i+1);
    }
}
static enum map_result exercise_capacity(unsigned capacity,unsigned expected,bool audio_expected) {
    struct kui_retail_manifest map={0};map.track_count=40;
    struct files files={0};strcpy(files.root,"/Games/Map");
    const struct kui_volume volume={.start=2048,.count=200000};bool audio=false;
    enum map_result result=map_tracks(&files,&fixture_fs,&volume,&fixture_image,&map,
        capacity,&audio);
    assert(opens==closes);
    /* The actual mapper must refuse excess extents before storing any slot
     * beyond this module's capacity, even though the UI's struct is larger. */
    const union kui_retail_slot empty={0};
    for(unsigned i=capacity;i<KUI_RETAIL_MANIFEST_SLOTS;i++)
        assert(!memcmp(&map.slots[i],&empty,sizeof(empty)));
    if(result==MAP_OK) {
        assert(map.track_count==40 && map.extent_count==expected && audio==audio_expected);
        for(unsigned i=0;i<40;i++) {
            const struct kui_retail_track *t=&map.slots[i].track;
            assert(t->start_lba==fixture_image.tracks[i].start_lba &&
                t->end_lba==fixture_image.tracks[i].end_lba &&
                kui_retail_track_control(t)==fixture_image.tracks[i].control);
            assert(t->extent_count==((audio || fixture_image.tracks[i].control)?pieces_for(i):0));
        }
        map.reader=KUI_RETAIL_READER_STANDARD;map.storage_transport=KUI_STORAGE_SCI;
        assert(kui_native_foreground_manifest_valid(&map)==
               (map.track_count+map.extent_count<=KUI_RETAIL_ASYNC_SLOTS));
    }
    return result;
}
static enum map_result exercise(bool foreground,unsigned expected,bool audio_expected) {
    return exercise_capacity(kui_native_standard_map_capacity(foreground),expected,audio_expected);
}
int main(void) {
    uint32_t cap[8]={KUI_NATIVE_FOREGROUND_CAP_MAGIC0,KUI_NATIVE_FOREGROUND_CAP_MAGIC1,
        1u,KUI_NATIVE_FOREGROUND_CAP_BYTES,KUI_RETAIL_READER_STANDARD,64u,0u,0u};
    assert(kui_native_foreground_descriptor_valid(cap,sizeof(cap)));
    assert(KUI_NATIVE_FOREGROUND_VC2_SLOTS==48u);
    cap[5]=48u;assert(!kui_native_foreground_descriptor_valid(cap,sizeof(cap)));
    cap[7]=KUI_NATIVE_FOREGROUND_CAP_VC2_READONLY;
    assert(kui_native_foreground_descriptor_valid(cap,sizeof(cap)));
    cap[5]=44u;assert(!kui_native_foreground_descriptor_valid(cap,sizeof(cap)));
    cap[5]=64u;assert(!kui_native_foreground_descriptor_valid(cap,sizeof(cap)));
    cap[5]=48u;cap[7]=KUI_NATIVE_FOREGROUND_CAP_VC2_LATE;
    assert(kui_native_foreground_descriptor_valid(cap,sizeof(cap)));
    cap[5]=64u;assert(!kui_native_foreground_descriptor_valid(cap,sizeof(cap)));
    cap[5]=48u;cap[7]=3u;assert(!kui_native_foreground_descriptor_valid(cap,sizeof(cap)));
    cap[7]=1u;cap[6]=1u;assert(!kui_native_foreground_descriptor_valid(cap,sizeof(cap)));
    fixture();assert(kui_native_standard_map_capacity(false)==160 &&
        kui_native_standard_map_capacity(true)==64);
    assert(exercise(false,40,true)==MAP_OK); /* Normal Standard retains all80 slots. */
    assert(exercise(true,3,false)==MAP_OK); /*40 tracks+3 data extents=43 slots. */
    fragments=2;
    assert(exercise_capacity(44u,0u,false)==MAP_FULL); /* Reproduce the old launch refusal. */
    assert(exercise_capacity(48u,6u,false)==MAP_OK); /* User's46-slot shape, unchanged40-track TOC. */
    track_fragments[0]=4u;
    assert(exercise_capacity(48u,8u,false)==MAP_OK); /* Exact48-slot capacity boundary. */
    track_fragments[0]=0u;fragments=3u;
    assert(exercise_capacity(48u,0u,false)==MAP_FULL); /*49 slots need one beyond the limit. */
    fragments=10;assert(exercise(true,0,false)==MAP_FULL); /*40+30 cannot fit. */
    fail_open=2;fragments=1;assert(exercise(true,0,false)==MAP_FAILED);
    puts("PASS production40-track mapping: normal80 slots; foreground43,46 and exact48 slots retain full TOC; old44 and excess49 refuse without excess-slot stores; all files close");
    return 0;
}
