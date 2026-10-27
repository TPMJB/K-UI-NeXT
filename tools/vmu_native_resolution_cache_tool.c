/* SPDX-License-Identifier: GPL-3.0-only */
/* Host-only explicit-path cache builder/inspector. No card/device discovery. */
#include "kui/vmu_native_resolution_cache.h"
#include "kui/vmu_native_program.h"
#include "kui/hash.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static void hex(const uint8_t bytes[32],char out[65]) {
    static const char digits[]="0123456789abcdef";
    for(unsigned i=0;i<32u;i++) {out[2u*i]=digits[bytes[i]>>4];out[2u*i+1u]=digits[bytes[i]&15u];}
    out[64]='\0';
}
static bool parse_hex(const char *p,uint8_t bytes[32]) {
    if(strlen(p)!=64u) return false;
    for(unsigned i=0;i<32u;i++) {
        unsigned value=0u;
        for(unsigned j=0;j<2u;j++) {
            unsigned char c=(unsigned char)p[2u*i+j];unsigned digit;
            if(c>='0' && c<='9') digit=c-'0';
            else if(c>='a' && c<='f') digit=c-'a'+10u;
            else if(c>='A' && c<='F') digit=c-'A'+10u;
            else return false;
            value=(value<<4)|digit;
        }
        bytes[i]=(uint8_t)value;
    }
    return true;
}
static uint8_t *read_file(const char *path,size_t limit,size_t *bytes) {
    FILE *f=fopen(path,"rb");
    if(!f) {fprintf(stderr,"Cannot open %s: %s\n",path,strerror(errno));return NULL;}
    bool ok=!fseek(f,0,SEEK_END);long length=ok?ftell(f):-1;
    if(length<=0 || (unsigned long)length>limit || fseek(f,0,SEEK_SET)) {
        fprintf(stderr,"Invalid input length for %s\n",path);fclose(f);return NULL;
    }
    uint8_t *p=malloc((size_t)length);
    if(!p) {fprintf(stderr,"Insufficient host memory\n");fclose(f);return NULL;}
    ok=fread(p,1,(size_t)length,f)==(size_t)length;bool closed=!fclose(f);
    if(!ok || !closed) {fprintf(stderr,"Could not read %s completely\n",path);free(p);return NULL;}
    *bytes=(size_t)length;return p;
}
static bool write_new(const char *path,const uint8_t *wire,size_t n) {
    FILE *f=fopen(path,"wbx");
    if(!f) {fprintf(stderr,"Cannot create %s (existing files are preserved): %s\n",path,strerror(errno));return false;}
    bool written=fwrite(wire,1,n,f)==n;bool closed=!fclose(f);
    if(!written || !closed) {fprintf(stderr,"Cache write incomplete; discard %s\n",path);return false;}
    return true;
}
static void summary(const uint8_t wire[1024],const uint8_t pin[32],
    const uint8_t *source,size_t source_bytes,size_t program_bytes) {
    char image[65],record[65];hex(wire+32u,image);hex(pin,record);
    printf("{\"schema\":\"kui.native.resolution-cache.v1\",\"detector_epoch\":%u,"
        "\"record_bytes\":1024,\"record_digest\":\"%s\",\"image_sha256\":\"%s\","
        "\"image_bytes\":%u,\"load_address\":\"%08x\",\"submit\":\"%08x\","
        "\"heap_initializer\":\"%08x\",\"exit_family\":%u",
        get32(wire+16u),record,image,get32(wire+24u),get32(wire+28u),get32(wire+88u),
        get32(wire+632u),get32(wire+708u));
    if(source) {
        uint8_t digest[32];char source_hash[65];struct kui_sha256 hash;
        kui_sha256_init(&hash);kui_sha256_update(&hash,source,source_bytes);
        kui_sha256_digest(&hash,digest);hex(digest,source_hash);
        printf(",\"source_sha256\":\"%s\",\"source_bytes\":%zu,"
            "\"program_bytes\":%zu,\"discarded_suffix_bytes\":%zu",
            source_hash,source_bytes,program_bytes,source_bytes-program_bytes);
    }
    printf("}\n");
}
static int usage(const char *name) {
    fprintf(stderr,"Usage:\n  %s build IMAGE CACHE [LOAD_HEX]\n  %s inspect CACHE\n"
        "  %s verify IMAGE CACHE [LOAD_HEX]\n  %s replay IMAGE CACHE TRUSTED_DIGEST_HEX [LOAD_HEX]\n"
        "build creates a new cache only; verify repeats full discovery.\n"
        "replay requires an independently trusted digest, not the inspected file's own checksum.\n",name,name,name,name);
    return 2;
}
int main(int argc,char **argv) {
    if(argc<2) return usage(argv[0]);
    uint8_t pin[32],wire[1024];size_t bytes=0u;
    if(!strcmp(argv[1],"inspect")) {
        if(argc!=3) return usage(argv[0]);
        uint8_t *record=read_file(argv[2],sizeof(wire),&bytes);if(!record) return 1;
        bool valid=kui_vmu_native_resolution_cache_record_digest(record,bytes,pin);
        if(valid) summary(record,pin,NULL,0u,0u);else fprintf(stderr,"Cache integrity/schema refused\n");
        free(record);return valid?0:1;
    }
    bool build=!strcmp(argv[1],"build"),verify=!strcmp(argv[1],"verify"),replay=!strcmp(argv[1],"replay");
    if((!build && !verify && !replay) || (replay?(argc!=5 && argc!=6):(argc!=4 && argc!=5))) return usage(argv[0]);
    uint32_t load=UINT32_C(0x8c010000);const char *load_text=replay?(argc==6?argv[5]:NULL):(argc==5?argv[4]:NULL);
    if(load_text) {
        char *end;errno=0;unsigned long value=strtoul(load_text,&end,16);
        if(errno || !*load_text || *end || value>UINT32_MAX) return usage(argv[0]);
        load=(uint32_t)value;
    }
    if(replay && !parse_hex(argv[4],pin)) return usage(argv[0]);
    uint8_t *image=read_file(argv[2],UINT32_C(0xc00000),&bytes);if(!image) return 1;
    size_t program_bytes=0u;
    if(!kui_vmu_native_program_extent(image,bytes,load,&program_bytes) ||
       !program_bytes || program_bytes>bytes) {
        fprintf(stderr,"Native executable extent refused this source image\n");
        free(image);return 1;
    }
    struct kui_vmu_native_resolution resolution;bool success=false;
    if(build) {
        success=kui_vmu_native_resolution_cache_build(image,program_bytes,load,wire,sizeof(wire),&resolution);
        if(!success) fprintf(stderr,"Complete native discovery refused this executable\n");
        else success=kui_vmu_native_resolution_cache_record_digest(wire,sizeof(wire),pin) && write_new(argv[3],wire,sizeof(wire));
    } else {
        size_t record_bytes;uint8_t *record=read_file(argv[3],sizeof(wire),&record_bytes);
        if(record) {
            success=replay?kui_vmu_native_resolution_cache_replay(image,program_bytes,load,record,record_bytes,pin,&resolution):
                kui_vmu_native_resolution_cache_validate_untrusted(image,program_bytes,load,record,record_bytes,&resolution);
            if(success) {memcpy(wire,record,sizeof(wire));success=kui_vmu_native_resolution_cache_record_digest(wire,sizeof(wire),pin);}
            free(record);
        }
        if(!success) fprintf(stderr,"Cache miss/refused; a launcher must run full discovery\n");
    }
    if(success) summary(wire,pin,image,bytes,program_bytes);
    free(image);return success?0:1;
}
