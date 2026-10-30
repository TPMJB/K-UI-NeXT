/* SPDX-License-Identifier: GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#include "kui/saved_identity.h"
#include "kui/media.h"
#include <assert.h>
#define DIR HOST_DIR
#include <dirent.h>
#undef DIR
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static struct {
    FILE *image;uint64_t blocks,writes;bool active;const char *fault;
    unsigned optical_reads,source_reads;DWORD source_cluster;
    uint8_t optical[3][40*KUI_RAW_BYTES],alternate[KUI_RAW_BYTES];
} test;
static FATFS fs;
static void log_line(const char *format,...) {(void)format;}
static uint64_t blocks(void *ctx) {(void)ctx;return test.blocks;}
static int read_image(void *ctx,uint32_t block,size_t count,uint8_t *data) {
    (void)ctx;return fseeko(test.image,(off_t)block*512,SEEK_SET) || fread(data,512,count,test.image)!=count?-1:0;
}
static int write_image(void *ctx,uint32_t block,size_t count,const uint8_t *data) {
    (void)ctx;++test.writes;return fseeko(test.image,(off_t)block*512,SEEK_SET) || fwrite(data,512,count,test.image)!=count?-1:0;
}
static int sync_image(void *ctx) {(void)ctx;return fflush(test.image) || fsync(fileno(test.image))?-1:0;}
static const struct kui_media_ops media={NULL,blocks,read_image,write_image,sync_image};
FRESULT __real_f_read(FIL *,void *,UINT,UINT *);
FRESULT __wrap_f_read(FIL *f,void *data,UINT bytes,UINT *got) {
    FRESULT r=__real_f_read(f,data,bytes,got);
    if(test.active && !strcmp(test.fault,"source-unstable") && f->obj.sclust==test.source_cluster && bytes>0 && r==FR_OK && *got==bytes && ++test.source_reads>=2)((uint8_t *)data)[0]^=1;
    return r;
}
static bool cancelled(void *ctx) {(void)ctx;return !strcmp(test.fault,"cancel");}
static struct kui_capture_plan plan={.count=3,.bytes=(40u+4u+40u)*KUI_RAW_BYTES,.tracks={
    {1,4,0,150,190,340},{2,0,0,340,344,344},{3,4,1,45150,45190,45190}}};
static enum kui_read_result optical(void *ctx,uint32_t fad,unsigned count,uint8_t *out) {
    (void)ctx;++test.optical_reads;assert(count==1);
    if(!strcmp(test.fault,"fatal"))return KUI_READ_FATAL;
    if(!strcmp(test.fault,"cd-fixed-silence-mismatch") && fad==150) {memcpy(out,test.alternate,KUI_RAW_BYTES);return KUI_READ_OK;}
    if(!strcmp(test.fault,"known-anchor-silent") && fad==150) {memset(out,0,KUI_RAW_BYTES);return KUI_READ_OK;}
    if((!strcmp(test.fault,"damaged-end") || !strcmp(test.fault,"partial") || !strcmp(test.fault,"wrong-disc")) && (fad==45150 || fad==45169 || fad==45189)) {
        if(!strcmp(test.fault,"wrong-disc") && fad==45169) {memcpy(out,test.alternate,KUI_RAW_BYTES);return KUI_READ_OK;}
        return KUI_READ_RETRY;
    }
    if(!strcmp(test.fault,"anchor-unreadable") && fad==45151)return KUI_READ_RETRY;
    for(unsigned i=0;i<plan.count;++i)if(fad>=plan.tracks[i].start && fad<plan.tracks[i].end) {
        if(!strcmp(test.fault,"anchor-change") && fad==45151)memcpy(out,test.alternate,KUI_RAW_BYTES);
        else memcpy(out,test.optical[i]+(fad-plan.tracks[i].start)*KUI_RAW_BYTES,KUI_RAW_BYTES);
        return KUI_READ_OK;
    }
    assert(false);return KUI_READ_FATAL;
}
static void copy_file(const char *fixture,const char *name) {
    char host[1024],target[384];snprintf(host,sizeof(host),"%s/%s",fixture,name);FILE *f=fopen(host,"rb");assert(f);
    snprintf(target,sizeof(target),"0:/Games/Legacy/%s",name);FIL out;assert(f_open(&out,target,FA_WRITE|FA_CREATE_NEW)==FR_OK);
    uint8_t b[4096];size_t n;while((n=fread(b,1,sizeof(b),f))) {UINT written;assert(f_write(&out,b,(UINT)n,&written)==FR_OK && written==n);}
    assert(!ferror(f) && fclose(f)==0 && f_sync(&out)==FR_OK && f_close(&out)==FR_OK);
}
static void host_bytes(const char *fixture,const char *name,uint8_t *out,size_t bytes) {
    char path[1024];snprintf(path,sizeof(path),"%s/%s",fixture,name);FILE *f=fopen(path,"rb");assert(f && fread(out,1,bytes,f)==bytes && fclose(f)==0);
}
int main(int argc,char **argv) {
    assert(argc==4);test.fault=argv[3];test.image=fopen(argv[1],"r+b");assert(test.image);
    struct stat info;assert(!fstat(fileno(test.image),&info));test.blocks=(uint64_t)info.st_size/512;kui_media_set(&media);
    bool cd=!strncmp(test.fault,"cd-",3);
    if(cd)plan=(struct kui_capture_plan){.profile=KUI_CAPTURE_PROFILE_CD_TOC,.count=2,.bytes=8u*KUI_RAW_BYTES,.tracks={{1,0,0,150,154,154},{2,0,0,154,158,158}}};
    if(!strcmp(test.fault,"cd-data") || !strcmp(test.fault,"cd-mode2"))plan.tracks[0].control=4;
    for(unsigned i=0;i<plan.count;++i) {char name[32];snprintf(name,sizeof(name),"optical%02u.%s",i+1,plan.tracks[i].control==4?"bin":"raw");host_bytes(argv[2],name,test.optical[i],(size_t)(plan.tracks[i].end-plan.tracks[i].start)*KUI_RAW_BYTES);}
    host_bytes(argv[2],"alternate.bin",test.alternate,KUI_RAW_BYTES);
    uint32_t sectors[99]={0},crc[99]={0};uint8_t sha[99][32]={{0}},capture_id[32];memset(capture_id,0x42,sizeof(capture_id));
    char files[99][KUI_DEST_NAME_CAP]={{0}};
    for(unsigned i=0;i<plan.count;++i) {sectors[i]=plan.tracks[i].end-plan.tracks[i].start;snprintf(files[i],sizeof(files[i]),"track%02u.%s",i+1,plan.tracks[i].control==4?"bin":"raw");}
    if(!strcmp(test.fault,"partial"))sectors[2]=20;
    if(!strcmp(test.fault,"no-hd-prefix"))sectors[2]=0;
    for(unsigned i=0;i<plan.count;++i)if(sectors[i]) {struct kui_sha256 h;kui_sha256_init(&h);kui_sha256_update(&h,test.optical[i],(size_t)sectors[i]*KUI_RAW_BYTES);kui_sha256_digest(&h,sha[i]);crc[i]=kui_crc32(0,test.optical[i],(size_t)sectors[i]*KUI_RAW_BYTES);}
    if(!strcmp(test.fault,"sha-mismatch"))sha[2][0]^=1;
    assert(kui_mount(&fs,log_line));assert(f_mkdir("0:/Games")==FR_OK);assert(f_mkdir("0:/Games/Legacy")==FR_OK);
    for(unsigned i=0;i<plan.count;++i)copy_file(argv[2],files[i]);
    FIL src;assert(f_open(&src,cd?"0:/Games/Legacy/track02.raw":"0:/Games/Legacy/track03.bin",FA_READ)==FR_OK);test.source_cluster=src.obj.sclust;assert(f_close(&src)==FR_OK);
    struct kui_saved_identity_source source={.directory="/Games/Legacy",.plan=&plan,.files=(const char (*)[KUI_DEST_NAME_CAP])files,.sectors=sectors,.crc32=crc,.sha256=(const uint8_t (*)[32])sha,.capture_identity=capture_id,.crc_only=!strcmp(test.fault,"crc-only")};
    const struct kui_capture_ops ops={.read=optical,.cancelled=cancelled};struct kui_saved_identity identity={0},before=identity;char message[128];
    test.active=true;test.writes=0;enum kui_saved_identity_result result=kui_saved_identity_discover(&source,&ops,5,&identity,message);
    assert(test.writes==0);test.active=false;
    if(!strcmp(test.fault,"prefix-corrupt") || !strcmp(test.fault,"sha-mismatch") || !strcmp(test.fault,"source-unstable")) {assert(result==KUI_SAVED_IDENTITY_SOURCE_INVALID && !test.optical_reads && !memcmp(&identity,&before,sizeof(identity)));}
    else if(!strcmp(test.fault,"wrong-disc") || !strcmp(test.fault,"cd-fixed-silence-mismatch"))assert(result==KUI_SAVED_IDENTITY_DIFFERENT);
    else if(!strcmp(test.fault,"fatal"))assert(result==KUI_SAVED_IDENTITY_FAILED && test.optical_reads==1);
    else if(!strcmp(test.fault,"cancel"))assert(result==KUI_SAVED_IDENTITY_STOPPED && !test.optical_reads);
    else if(!strcmp(test.fault,"no-hd-prefix") || !strcmp(test.fault,"cd-silence") || !strcmp(test.fault,"cd-repeated"))assert(result==KUI_SAVED_IDENTITY_UNREADABLE);
    else {
        if(result!=KUI_SAVED_IDENTITY_MATCH)fprintf(stderr,"result %u %s\n",result,message);
        assert(result==KUI_SAVED_IDENTITY_MATCH && kui_saved_identity_matches_source(&source,&identity));
        assert(identity.fad[0]!=identity.fad[1]);
        if(!cd)assert(identity.fad[0]>=45150 && identity.fad[1]>=45150);
        uint8_t bytes[KUI_SAVED_IDENTITY_BYTES];kui_saved_identity_encode(&identity,bytes);struct kui_saved_identity decoded;
        assert(kui_saved_identity_decode(bytes,&plan,&decoded) && !memcmp(&identity,&decoded,sizeof(identity)));
        bytes[60]^=1;assert(!kui_saved_identity_decode(bytes,&plan,&decoded));
        assert(kui_saved_identity_recheck(&identity,&plan,&ops,5,message)==KUI_SAVED_IDENTITY_MATCH);
        crc[0]^=1;assert(!kui_saved_identity_matches_source(&source,&identity));crc[0]^=1;
        if(!strcmp(test.fault,"cd-anchor-silence")) {test.fault="known-anchor-silent";assert(kui_saved_identity_recheck(&identity,&plan,&ops,5,message)==KUI_SAVED_IDENTITY_DIFFERENT);test.fault=argv[3];}
        if(!strcmp(test.fault,"damaged-end")) {
            assert(identity.fad[0]==45151 && identity.fad[1]==45188);
            test.fault="anchor-change";assert(kui_saved_identity_recheck(&identity,&plan,&ops,5,message)==KUI_SAVED_IDENTITY_DIFFERENT);
            test.fault="anchor-unreadable";assert(kui_saved_identity_recheck(&identity,&plan,&ops,5,message)==KUI_SAVED_IDENTITY_UNREADABLE);
        }
    }
    if(!cd) {struct kui_checkpoint checkpoint={.sequence=1,.count=3,.track={{.sectors=40},{.sectors=4},{.sectors=20}}};memcpy(checkpoint.identity,capture_id,32);memcpy(checkpoint.build,"000000000000",12);
    uint8_t raw[KUI_CHECKPOINT_BYTES];kui_checkpoint_encode(&checkpoint,raw);struct kui_checkpoint decoded;
    assert(kui_saved_checkpoint_decode(raw,&plan,&decoded) && !memcmp(decoded.identity,capture_id,32));raw[500]^=1;assert(!kui_saved_checkpoint_decode(raw,&plan,&decoded));}
    assert(f_mount(NULL,"0:",0)==FR_OK && fclose(test.image)==0);puts("PASS saved-anchor identity");return 0;
}
