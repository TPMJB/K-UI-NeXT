/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/cdda_disc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks,stats,reads;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"FAIL disc line%d: %s\n",__LINE__,#x);exit(1);}}while(0)
static const char gdi[]="6\n1 0 4 2048 disc01.bin 0\n2 20 0 2352 disc02.raw 0\n3 45000 4 2048 disc03.bin 0\n4 45150 0 2352 disc04.raw 512\n5 45300 0 2352 disc05.raw 1024\n6 45675 4 2352 disc06.bin 0\n";
static const uint32_t bytes[]={16u*2048u,75u*2352u,64u*2048u,512u+150u*2352u,1024u+225u*2352u,32u*2352u};
static enum kui_game_result stat_file(void *ctx,const char *name,uint64_t *out) {
    CHECK(ctx==bytes);stats++;
    for(unsigned i=0;i<6u;i++){char want[32];snprintf(want,sizeof(want),"disc%02u.%s",i+1u,i==1u || i==3u || i==4u?"raw":"bin");
        if(!strcmp(name,want)){*out=bytes[i];return KUI_GAME_OK;}}
    return KUI_GAME_NOT_FOUND;
}
static enum kui_game_result read_file(void *ctx,const char *name,uint64_t offset,void *out,size_t n) {
    (void)ctx;(void)name;(void)offset;(void)out;(void)n;reads++;return KUI_GAME_IO;
}
static struct kui_game_image image;
static struct kui_cdda_disc_map map;
static void fixture(void) {
    struct kui_game_file_ops ops={(void*)bytes,stat_file,read_file};
    CHECK(kui_game_image_open(gdi,sizeof(gdi)-1u,&ops,&image)==KUI_GAME_OK);
    unsigned before=stats;
    CHECK(kui_cdda_disc_from_image(&image,&map)==KUI_CDDA_DISC_OK && stats==before && !reads);
    CHECK(map.count==6 && map.complete && !map.cd_image && kui_cdda_disc_validate(&map)==KUI_CDDA_DISC_OK);
}
static uint32_t get(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void known_geometry(void) {
    fixture();const uint32_t first[]={150,170,45150,45300,45450,45825},end[]={166,245,45214,45450,45675,45857};
    for(unsigned i=0;i<6u;i++) {
        const struct kui_cdda_disc_track *t=NULL;
        CHECK(kui_cdda_disc_track(&map,i+1u,&t)==KUI_CDDA_DISC_OK && t==map.tracks+i);
        CHECK(t->start_fad==first[i] && t->end_fad==end[i] && t->file_bytes==bytes[i]);
        CHECK(t->file_offset==(i==3u?512u:i==4u?1024u:0u));
    }
    struct kui_cdda_disc_audio a;
    for(unsigned index=1;index<5u;index++) if(index!=2u) {
        const struct kui_cdda_disc_track *t=map.tracks+index;
        CHECK(kui_cdda_disc_audio_range(&map,t->start_fad,t->end_fad,&a)==KUI_CDDA_DISC_OK);
        CHECK(a.track==index+1u && a.first_frame==0 && a.end_frame==(end[index]-first[index])*588u);
        CHECK(a.source.file_bytes==bytes[index] && a.source.backing_offset==t->file_offset &&
            a.source.sector_stride==2352u && !a.source.big_endian && a.source.sectors==end[index]-first[index]);
        CHECK(kui_cdda_disc_audio_range(&map,t->start_fad+1u,t->end_fad-1u,&a)==KUI_CDDA_DISC_OK && a.first_frame==588u);
    }
    struct kui_cdda_disc_data d;
    CHECK(kui_cdda_disc_data_range(&map,45826u,16u,&d)==KUI_CDDA_DISC_OK);
    CHECK(d.track==6u && d.offset==2048u && d.bytes==32768u && d.file_offset==2368u && d.stride==2352u && d.data_offset==16u);
    CHECK(kui_cdda_disc_data_range(&map,45198u,16u,&d)==KUI_CDDA_DISC_OK && d.offset==48u*2048u && d.file_offset==d.offset);
    uint8_t toc[408];
    CHECK(kui_cdda_disc_toc(&map,0,toc)==KUI_CDDA_DISC_OK);
    CHECK(get(toc)==0x41000096u && get(toc+4u)==0x010000aau && get(toc+8u)==UINT32_MAX);
    CHECK(get(toc+396u)==0x41010000u && get(toc+400u)==0x01020000u && get(toc+404u)==0x010000f5u);
    CHECK(kui_cdda_disc_toc(&map,1,toc)==KUI_CDDA_DISC_OK);
    CHECK(get(toc)==UINT32_MAX && get(toc+4u)==UINT32_MAX && get(toc+8u)==(0x41000000u|45150u));
    CHECK(get(toc+12u)==(0x01000000u|45300u) && get(toc+16u)==(0x01000000u|45450u));
    CHECK(get(toc+20u)==(0x41000000u|45825u) && get(toc+24u)==UINT32_MAX);
    CHECK(get(toc+396u)==0x41030000u && get(toc+400u)==0x41060000u && get(toc+404u)==(0x41000000u|45857u));
    map.cd_image=true;CHECK(kui_cdda_disc_toc(&map,0,toc)==KUI_CDDA_DISC_OK && get(toc+20u)!=(uint32_t)-1);
    memset(toc,0xa5,sizeof(toc));CHECK(kui_cdda_disc_toc(&map,1,toc)==KUI_CDDA_DISC_UNSUPPORTED);
    for(unsigned i=0;i<408u;i++) CHECK(toc[i]==0xa5u);
}
static void ranges_and_metadata(void) {
    fixture();struct kui_cdda_disc_audio a={0},saved=a;struct kui_cdda_disc_data d={0},sd=d;
    const uint32_t bad_audio[][2]={{0,1},{169,170},{170,170},{170,246},{45449,45451},{45150,45151},{45857,45858},{UINT32_MAX,0}};
    for(unsigned i=0;i<sizeof(bad_audio)/sizeof(bad_audio[0]);i++) {
        CHECK(kui_cdda_disc_audio_range(&map,bad_audio[i][0],bad_audio[i][1],&a)!=KUI_CDDA_DISC_OK);
        CHECK(!memcmp(&a,&saved,sizeof(a)));
    }
    const uint32_t bad_data[][2]={{150,0},{165,2},{166,1},{170,1},{45214,1},{45856,2},{UINT32_MAX,2},{0,UINT32_MAX}};
    for(unsigned i=0;i<sizeof(bad_data)/sizeof(bad_data[0]);i++) {
        CHECK(kui_cdda_disc_data_range(&map,bad_data[i][0],bad_data[i][1],&d)!=KUI_CDDA_DISC_OK);
        CHECK(!memcmp(&d,&sd,sizeof(d)));
    }
    struct kui_game_image original=image;struct kui_cdda_disc_map untouched=map;
    for(unsigned issue=0;issue<17u;issue++) {
        image=original;
        switch(issue) {
        case 0:image.count=0;break;case 1:image.count=100;break;case 2:image.tracks[0].number=2;break;
        case 3:image.tracks[0].end_lba=0;break;case 4:image.tracks[0].end_lba=21;break;
        case 5:image.tracks[0].file_bytes=32767;break;case 6:image.tracks[0].file_offset=32768;break;
        case 7:image.tracks[0].file_bytes=UINT64_MAX;break;case 8:image.tracks[0].sector_mode=2;break;
        case 9:image.tracks[0].sector_bytes=2336;break;case 10:image.tracks[0].data_offset=16;break;
        case 11:strcpy(image.tracks[0].name,"../file");break;case 12:memset(image.tracks[0].name,'x',128);break;
        case 13:image.tracks[1].control=4;break;case 14:image.tracks[0].end_lba=UINT32_MAX;break;
        case 15:image.files.read=NULL;break;default:strcpy(image.tracks[1].name,"DISC01.BIN");break;
        }
        CHECK(kui_cdda_disc_from_image(&image,&map)!=KUI_CDDA_DISC_OK && !memcmp(&map,&untouched,sizeof(map)));
    }
    image=original;
    /* Shared backing files are supported only when exact physical extents do
     * not overlap; case aliases cannot hide an overlap. */
    strcpy(image.tracks[1].name,image.tracks[0].name);image.tracks[0].file_bytes=209168;
    image.tracks[1].file_bytes=209168;image.tracks[1].file_offset=32768;
    CHECK(kui_cdda_disc_from_image(&image,&map)==KUI_CDDA_DISC_OK);
    image.tracks[1].file_offset--;
    CHECK(kui_cdda_disc_from_image(&image,&map)==KUI_CDDA_DISC_OVERLAP);
    CHECK(!reads);
}
static void selected_and_properties(void) {
    fixture();char descriptor[4096];size_t n=(size_t)snprintf(descriptor,sizeof(descriptor),"15\n");
    for(unsigned i=1;i<=15u;i++) n+=(size_t)snprintf(descriptor+n,sizeof(descriptor)-n,"%u %u %u %u track%02u.raw 0\n",i,
        i==14u?374201u:i==15u?377422u:i*1000u,i==3u?4u:0u,2352u,i);
    unsigned oldstats=stats;
    CHECK(kui_cdda_disc_selected_audio(descriptor,n,14,7222992,&map)==KUI_CDDA_DISC_OK && stats==oldstats && !reads);
    CHECK(map.count==1 && !map.complete && map.tracks[0].number==14u && map.tracks[0].start_fad==374351u && map.tracks[0].end_fad==377422u);
    struct kui_cdda_disc_audio a;
    CHECK(kui_cdda_disc_audio_range(&map,374351,377422,&a)==KUI_CDDA_DISC_OK && a.end_frame==1805748u && a.source.sectors==3071u);
    uint8_t toc[408];memset(toc,0xa5,sizeof(toc));
    CHECK(kui_cdda_disc_toc(&map,0,toc)==KUI_CDDA_DISC_UNSUPPORTED && kui_cdda_disc_toc(&map,1,toc)==KUI_CDDA_DISC_UNSUPPORTED);
    for(unsigned i=0;i<sizeof(toc);i++) CHECK(toc[i]==0xa5u);
    const struct kui_cdda_disc_track *track=NULL;
    CHECK(kui_cdda_disc_track(&map,13,&track)==KUI_CDDA_DISC_UNSUPPORTED && !track);
    CHECK(kui_cdda_disc_audio_range(&map,377422,377423,&a)==KUI_CDDA_DISC_UNSUPPORTED);
    struct kui_cdda_disc_map original=map;
    uint64_t badbytes[]={0,7222991,7222993,UINT32_MAX+UINT64_C(1),2352u*3222u};
    for(unsigned i=0;i<sizeof(badbytes)/sizeof(badbytes[0]);i++)
        CHECK(kui_cdda_disc_selected_audio(descriptor,n,14,badbytes[i],&map)!=KUI_CDDA_DISC_OK && !memcmp(&map,&original,sizeof(map)));
    CHECK(kui_cdda_disc_selected_audio(descriptor,n,3,7222992,&map)==KUI_CDDA_DISC_UNSUPPORTED);
    CHECK(kui_cdda_disc_selected_audio(descriptor,n,16,7222992,&map)==KUI_CDDA_DISC_UNSUPPORTED);
    CHECK(kui_cdda_disc_selected_audio("1\n1 0 0 2352 ../escape 0\n",28,1,2352,&map)==KUI_CDDA_DISC_INVALID);
    fixture();uint32_t rng=0x68322fu;
    for(unsigned i=0;i<25000u;i++) {
        rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;
        uint32_t fad=i%3u?45140u+rng%100u:rng,count=i%5u?1u+rng%30u:rng;
        bool expected=fad>=45150u && fad<45214u && count && (uint64_t)fad+count<=45214u;
        struct kui_cdda_disc_data data;
        CHECK((kui_cdda_disc_data_range(&map,fad,count,&data)==KUI_CDDA_DISC_OK)==expected);
        if(expected) CHECK(data.track==3u && data.offset==(fad-45150u)*2048u && data.bytes==count*2048u);
    }
}
int main(void) {known_geometry();ranges_and_metadata();selected_and_properties();
    printf("PASS CDDA disc map: %u checks; backed multitrack geometry, source offsets, pure whole ranges, derived areas, selected14 and rejected metadata\n",checks);return 0;}
