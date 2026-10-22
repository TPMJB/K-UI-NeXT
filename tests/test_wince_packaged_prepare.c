/* SPDX-License-Identifier: GPL-3.0-only */
/* Actual shipped package boundary, generated disc sectors and optional
 * owner-supplied kernel bytes. No proprietary fixture belongs in this tree. */
#define main synthetic_prepare_fixture_main
#include "test_armada_ce_excerpt_prepare.c"
#undef main

static uint8_t *owner_kernel,*shipped_payload;
static size_t owner_bytes,shipped_bytes;
static const char *shipped_path;
static enum kui_wince_cdda_profile packaged_profile=KUI_WINCE_CDDA_WORMS_ARMAGEDDON;
static uint8_t *read_whole(const char *path,size_t *bytes) {
    FILE *file=fopen(path,"rb");CHECK(file);
    CHECK(!fseek(file,0,SEEK_END));long size=ftell(file);CHECK(size>0);
    CHECK(!fseek(file,0,SEEK_SET));uint8_t *data=malloc((size_t)size);CHECK(data);
    CHECK(fread(data,1,(size_t)size,file)==(size_t)size && !fclose(file));
    *bytes=(size_t)size;return data;
}
static enum kui_runtime_result shipped_runtime(const char *path,struct kui_runtime_image *out) {
    CHECK(!strcmp(path,profile_package_path(packaged_profile)));
    size_t bytes;uint8_t *data=read_whole(shipped_path,&bytes);
    CHECK(bytes>=64u && !memcmp(data,"KUIRUN1\0",8u) && le32(data+12u)==64u);
    CHECK(kui_crc32(0,data,60u)==le32(data+60u));
    shipped_bytes=le32(data+16u);CHECK(shipped_bytes==bytes-64u);
    CHECK(le32(data+28u)==shipped_bytes && kui_crc32(0,data+64u,shipped_bytes)==le32(data+32u));
    free(shipped_payload);shipped_payload=malloc(shipped_bytes);CHECK(shipped_payload);
    memcpy(shipped_payload,data+64u,shipped_bytes);
    out->data=malloc(shipped_bytes);CHECK(out->data);
    memcpy(out->data,shipped_payload,shipped_bytes);
    out->info=(struct kui_runtime_info){.payload_bytes=(uint32_t)shipped_bytes,
        .memory_bytes=(uint32_t)shipped_bytes,.crc32=le32(data+32u)};
    memcpy(out->info.build,data+40u,12u);free(data);
    size_t seed_at=armada_stream_seed_find(out,packaged_profile);CHECK(seed_at!=SIZE_MAX);
    const struct kui_armada_ce_stream_seed *blank=(const void *)((const uint8_t *)out->data+seed_at);
    CHECK(!blank->words[KUI_ARMADA_CE_STREAM_MAP_CRC_WORD]);
    for(size_t i=0;i<sizeof(blank->wire);i++) CHECK(!blank->wire[i]);
    return KUI_RUNTIME_OK;
}
static void owner_sector(const char *name,uint64_t offset,void *out,size_t bytes) {
    if(!owner_kernel || strcmp(name,image.tracks[image.count-1u].name)) return;
    const struct kui_game_image_track *track=&image.tracks[image.count-1u];
    CHECK(bytes==track->sector_bytes && !(offset%track->sector_bytes));
    uint64_t lba=track->start_lba+offset/track->sector_bytes;
    if(lba<metadata.boot_lba) return;
    uint64_t at=(lba-metadata.boot_lba)*2048u;
    if(at>=owner_bytes) return;
    size_t take=owner_bytes-at;if(take>2048u) take=2048u;
    memcpy((uint8_t *)out+track->data_offset,owner_kernel+at,take);
}
static void packaged_disc(bool cooked) {
    launch_setup();launch.stream=launch.variant=true;launch.kernel_profile=packaged_profile;
    launch.admit_kernel=owner_kernel==NULL;
    const struct kui_wince_cdda_disc *disc=kui_wince_cdda_disc(launch.kernel_profile);
    image.count=disc->track_count;metadata.boot_lba=disc->boot_lba;metadata.boot_bytes=disc->kernel_bytes;
    strcpy(metadata.product,disc->product);
    size_t at=(size_t)snprintf(launch.descriptor,sizeof(launch.descriptor),"%u\n",disc->track_count);
    for(unsigned i=0;i<image.count;i++) {
        uint32_t control=kui_wince_cdda_track_control(launch.kernel_profile,i);
        uint32_t stride=cooked && control?2048u:2352u;
        uint32_t end=i+1u<image.count?disc->starts[i+1u]:549150u;
        struct kui_game_image_track *t=&image.tracks[i];
        *t=(struct kui_game_image_track){.number=i+1u,.start_lba=disc->starts[i],.end_lba=end,
            .control=control,.sector_bytes=stride,.sector_mode=control?1u:0u,
            .data_offset=control && stride==2352u?16u:0u,
            .file_bytes=(uint64_t)(end-disc->starts[i])*stride};
        snprintf(t->name,sizeof(t->name),"track%02u.raw",i+1u);
        at+=(size_t)snprintf(launch.descriptor+at,sizeof(launch.descriptor)-at,
            "%u %u %u %u %s 0\n",t->number,t->start_lba,t->control,t->sector_bytes,t->name);
        CHECK(at<sizeof(launch.descriptor));
    }
}
/* Execute only the actual SH package entry and stage's cache/BSS preamble.
 * The bounded integer subset stops at the first C call, before hardware I/O.
 * Both P1/P2 aliases address the same simulated 16MiB physical RAM. */
static uint32_t ram_offset(uint32_t address,size_t bytes) {
    uint32_t physical=address&0x1fffffffu;
    CHECK(physical>=0x0c000000u && physical-0x0c000000u<=0x1000000u-bytes);
    return physical-0x0c000000u;
}
static uint32_t ram_word(const uint8_t *ram,uint32_t address) {
    return le32(ram+ram_offset(address,4u));
}
static uint32_t execute_entry(uint8_t *ram,const uint8_t *payload,size_t bytes) {
    memcpy(ram+0x10000u,payload,bytes);
    uint32_t registers[16]={0},pc=0xac010000u;bool t=false;
    uint32_t bss_begin=0,bss_end=0;unsigned cache_writes=0;
    for(unsigned step=0;step<1000000u;step++) {
        uint32_t at=ram_offset(pc,2u);uint16_t instruction=(uint16_t)(ram[at]|(uint32_t)ram[at+1u]<<8);
        unsigned n=(instruction>>8)&15u,m=(instruction>>4)&15u;uint32_t next=pc+2u;
        if((instruction&0xf000u)==0xd000u) {
            registers[n]=ram_word(ram,((pc+4u)&~3u)+4u*(instruction&255u));
        } else if((instruction&0xf00fu)==0x6006u) {
            registers[n]=ram_word(ram,registers[m]);registers[m]+=4u;
        } else if((instruction&0xf00fu)==0x2002u) {
            if(registers[n]==0xff00001cu) ++cache_writes;
            else {
                uint32_t offset=ram_offset(registers[n],4u);put32(ram+offset,registers[m]);
                if(m==2u && registers[m]==0u && registers[n]>=0x8ce10000u) {
                    if(!bss_begin) bss_begin=registers[n];
                    bss_end=registers[n]+4u;
                }
            }
        } else if((instruction&0xf000u)==0x7000u) registers[n]+=(uint32_t)(int32_t)(int8_t)instruction;
        else if((instruction&0xf000u)==0xe000u) registers[n]=(uint32_t)(int32_t)(int8_t)instruction;
        else if((instruction&0xf0ffu)==0x4010u) t=--registers[n]==0u;
        else if((instruction&0xf0ffu)==0x4009u) registers[n]>>=2u;
        else if((instruction&0xf00fu)==0x3002u) t=registers[n]>=registers[m];
        else if((instruction&0xff00u)==0x8b00u) {if(!t) next=pc+4u+2u*(int32_t)(int8_t)instruction;}
        else if((instruction&0xff00u)==0x8900u) {if(t) next=pc+4u+2u*(int32_t)(int8_t)instruction;}
        else if((instruction&0xf0ffu)==0x402bu || (instruction&0xf000u)==0xa000u) {
            CHECK((ram_word(ram,pc+2u)&0xffffu)==0x0009u); /* Jump delay slot is NOP. */
            int32_t displacement=instruction&4095u;
            if(displacement>=2048) displacement-=4096;
            next=(instruction&0xf000u)==0xa000u?pc+4u+2u*displacement:registers[n];
        } else if((instruction&0xf0ffu)==0x400bu) {
            CHECK(cache_writes==1u && bss_begin && bss_end>bss_begin);
            CHECK(registers[15]==KUI_RETAIL_STAGE_STACK);
            CHECK(registers[4]==0x8c011000u);
            CHECK(registers[n]>=KUI_RETAIL_CE_STAGE_ADDRESS && registers[n]<KUI_RETAIL_CE_STAGE_ADDRESS+bytes);
            CHECK(bss_begin>=KUI_RETAIL_CE_STAGE_ADDRESS && bss_end<KUI_RETAIL_STAGE_MEMORY_END);
            return registers[4];
        } else CHECK(instruction==0x0009u || (instruction&0xf0ffu)==0x400eu);
        pc=next;
    }
    CHECK(false);return 0;
}
static void packaged_case(bool cooked) {
    packaged_disc(cooked);
    const struct kui_wince_cdda_disc *disc=kui_wince_cdda_disc(packaged_profile);
    const uint32_t request=KUI_GAMES_RETAIL_CE_PROBE|KUI_GAMES_RETAIL_CE_EXCERPT|KUI_RETAIL_READER_ASYNC;
    struct kui_runtime_image prepared;
    CHECK(kui_games_retail_prepare_reader("/Games/ARMADA/ARMADA.gdi",request,&prepared,record_log,launch_cancel));
    CHECK(!launch.connected && !launch.mounted && launch.opens==launch.closes);
    CHECK(launch.kernel_hashes==(owner_kernel?0u:1u));
    CHECK(launch.package_reads==1u && !strcmp(launch.package_path,profile_package_path(packaged_profile)));
    uint8_t *payload=prepared.data;CHECK(prepared.info.payload_bytes==shipped_bytes);
    size_t seed_at=armada_stream_seed_find(&prepared,packaged_profile);CHECK(seed_at!=SIZE_MAX);
    for(size_t i=0;i<shipped_bytes;i++) {
        bool map=i>=KUI_RETAIL_MAP_OFFSET && i<KUI_RETAIL_MAP_OFFSET+KUI_RETAIL_MAP_BYTES;
        bool seed_crc=i>=seed_at+24u && i<seed_at+28u;
        bool seed_map=i>=seed_at+64u && i<seed_at+sizeof(struct kui_armada_ce_stream_seed);
        if(!map && !seed_crc && !seed_map) CHECK(payload[i]==shipped_payload[i]);
    }
    CHECK(le32(payload+36u)==0x8c012000u && le32(payload+40u)==KUI_RETAIL_CE_STAGE_ADDRESS);
    CHECK(le32(payload+44u)==shipped_bytes-KUI_RETAIL_STAGE_BLOB_OFFSET);
    CHECK(le32(payload+52u)==0x8c011000u);
    uint8_t *ram=malloc(0x1000000u);CHECK(ram);memset(ram,0xa5,0x1000000u);
    uint32_t map_address=execute_entry(ram,payload,shipped_bytes);
    const uint8_t *wire=ram+ram_offset(map_address,4096u);
    CHECK(!memcmp(wire,payload+KUI_RETAIL_MAP_OFFSET,4096u));
    struct kui_retail_manifest decoded,source;
    CHECK(kui_retail_manifest_decode(wire,&decoded)==KUI_GAME_OK);
    CHECK(decoded.reader==KUI_RETAIL_READER_ASYNC && decoded.track_count==disc->track_count);
    CHECK(decoded.flags==(cooked?KUI_RETAIL_IMAGE_BOOT_CRC:0u));
    for(unsigned i=0;i<disc->track_count;i++) CHECK(decoded.slots[i].track.control==
        (kui_wince_cdda_track_control(packaged_profile,i)|(cooked && image.tracks[i].control?KUI_RETAIL_TRACK_COOKED:0u)));
    CHECK(kui_wince_cdda_runtime_data_manifest_valid(&decoded,packaged_profile));
    struct kui_armada_ce_stream_seed *seed=(void *)(payload+seed_at);
    CHECK(kui_retail_manifest_decode(seed->wire,&source)==KUI_GAME_OK);
    CHECK(source.track_count==disc->track_count && source.extent_count==disc->track_count);
    CHECK(kui_wince_cdda_runtime_manifest_valid(&source,packaged_profile));
    CHECK(kui_armada_ce_stream_maps_match(&source,&decoded));
    uint8_t broken[4096]={0};CHECK(kui_retail_manifest_decode(broken,&decoded)==KUI_GAME_INVALID);
    memcpy(broken,wire,sizeof(broken));broken[16]^=1u;
    CHECK(kui_retail_manifest_decode(broken,&decoded)==KUI_GAME_INVALID);
    printf("PASS real package %s %s %s: blank CEX2 accepted and only map/seed changed; actual SH entry copy/cache/BSS preamble preserves map; production decode/raw/cooked flags pass; %s\n",
        prepared.info.build,disc->name,cooked?"2048 DATA":"raw DATA",owner_kernel?"owner kernel SHA computed without substitution":"explicit synthetic fingerprint orchestration fixture");
    free(ram);kui_runtime_free(&prepared);
}
static void packaged_kernel_rejections(bool cooked) {
    if(!owner_kernel) return;
    packaged_disc(cooked);
    const uint32_t request=KUI_GAMES_RETAIL_CE_PROBE|KUI_GAMES_RETAIL_CE_EXCERPT|KUI_RETAIL_READER_ASYNC;
    struct kui_runtime_image prepared;
    owner_kernel[owner_bytes-1u]^=1u;
    CHECK(!kui_games_retail_prepare_reader("/Games/ARMADA/ARMADA.gdi",request,&prepared,record_log,launch_cancel));
    owner_kernel[owner_bytes-1u]^=1u;
    CHECK(!prepared.data && !prepared.info.payload_bytes && !launch.package_reads && !launch.kernel_hashes);
    CHECK(!launch.connected && !launch.mounted && !launch.active && launch.opens==launch.closes);
}
int main(int argc,char **argv) {
    int first=1;
    if(argc>1 && !strcmp(argv[1],"--wwp")) {packaged_profile=KUI_WINCE_CDDA_WORMS_WORLD_PARTY;++first;}
    CHECK(argc>=first+2);
    if(strcmp(argv[first],"-")) {
        owner_kernel=read_whole(argv[first],&owner_bytes);
        CHECK(owner_bytes==kui_wince_cdda_disc(packaged_profile)->kernel_bytes);
    }
    fixture_runtime_source=shipped_runtime;fixture_sector_source=owner_sector;
    for(int i=first+1;i<argc;i++) {shipped_path=argv[i];packaged_case(false);packaged_case(true);}
    packaged_kernel_rejections(false);packaged_kernel_rejections(true);
    free(owner_kernel);free(shipped_payload);free(allocation);
    printf("PASS packaged preparation boundary: %u checks\n",checks);return 0;
}
