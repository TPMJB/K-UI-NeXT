/* SPDX-License-Identifier: GPL-3.0-only */
/* Host preflight uses the production decoder and runtime/handoff validators.
 * It reads package bytes and never executes or installs a payload. */
#include "kui/update.h"
#include "kui/runtime.h"
#include "kui/update_handoff.h"
#include "kui/hash.h"
#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

static int failed(const char *path,const char *reason) {
    fprintf(stderr,"%s: %s\n",path?path:"package",reason);return 1;
}
static bool exact(FILE *file,void *data,size_t bytes) {
    return fread(data,1u,bytes,file)==bytes;
}
static int member(FILE *file,const struct kui_update_package *package,
    const struct kui_update_entry *entry,bool allow_legacy) {
    if(entry->policy==KUI_UPDATE_KEEP || entry->policy==KUI_UPDATE_REMOVE)return 0;
    if(entry->payload_offset>(uint64_t)LONG_MAX || fseek(file,(long)entry->payload_offset,SEEK_SET))
        return failed(entry->path,"payload seek failed");
    bool raw=entry->policy==KUI_UPDATE_RAW;
    struct kui_sha256 hash;kui_sha256_init(&hash);
    uint8_t digest[32];
    if(raw) {
        uint8_t buffer[32768];uint64_t left=entry->bytes;
        while(left) {
            size_t n=left>sizeof(buffer)?sizeof(buffer):(size_t)left;
            if(!exact(file,buffer,n))return failed(entry->path,"truncated raw payload");
            kui_sha256_update(&hash,buffer,n);left-=n;
        }
    } else {
        if(entry->bytes<68u || entry->bytes>KUI_RUNTIME_MAX_BYTES+64u)
            return failed(entry->path,"invalid runtime payload size");
        uint8_t *data=malloc((size_t)entry->bytes);
        if(!data)return failed(entry->path,"cannot allocate runtime validation buffer");
        if(!exact(file,data,(size_t)entry->bytes)) {free(data);return failed(entry->path,"truncated runtime payload");}
        struct kui_runtime_info info;
        enum kui_runtime_result result=kui_runtime_header(data,entry->bytes,&info);
        const char *build=kui_update_entry_build(package->build,entry);
        if(result!=KUI_RUNTIME_OK || !build || strcmp(build,info.build)) {
            free(data);return failed(entry->path,"runtime envelope/build is incompatible");
        }
        if(kui_crc32(0u,data+64u,info.payload_bytes)!=info.crc32) {
            free(data);return failed(entry->path,"runtime payload CRC mismatch");
        }
        if(!strcmp(entry->path,"runtime.kui")) {
            if(!kui_update_capability_valid(data+64u,info.payload_bytes) ||
               !kui_update_handoff_empty(data+64u,info.payload_bytes,NULL)) {
                free(data);return failed(entry->path,"GUI bank/handoff contract is incompatible");
            }
            if(kui_update_engine_level(data+64u,info.payload_bytes)<2u &&
               !(allow_legacy && kui_update_package_version(package)==1u)) {
                free(data);return failed(entry->path,"requires the current Main updater engine v2");
            }
        }
        kui_sha256_update(&hash,data,(size_t)entry->bytes);free(data);
    }
    kui_sha256_digest(&hash,digest);
    return memcmp(digest,entry->sha256,sizeof(digest))?
        failed(entry->path,"full-file SHA-256 mismatch"):0;
}
int main(int argc,char **argv) {
    bool legacy=argc==3 && !strcmp(argv[1],"--allow-legacy-gui");
    if((argc!=2 && !legacy))return failed(NULL,"usage: update-consumer [--allow-legacy-gui] package.kup");
    const char *path=argv[legacy?2:1];FILE *file=fopen(path,"rb");
    if(!file)return failed(NULL,"cannot open package");
    if(fseek(file,0,SEEK_END)) {fclose(file);return failed(NULL,"cannot measure package");}
    long length=ftell(file);
    if(length<128 || fseek(file,0,SEEK_SET)) {fclose(file);return failed(NULL,"truncated package");}
    uint8_t header[128];struct kui_update_package *package=malloc(sizeof(*package));
    if(!package || !exact(file,header,sizeof(header))) {free(package);fclose(file);return failed(NULL,"cannot read package header");}
    enum kui_update_result result=kui_update_header_decode(header,(uint64_t)length,package);
    if(result!=KUI_UPDATE_OK) {free(package);fclose(file);return failed(NULL,kui_update_result_name(result));}
    size_t bytes=package->count*kui_update_entry_bytes(package);uint8_t *manifest=malloc(bytes);
    if(!manifest || !exact(file,manifest,bytes)) {free(manifest);free(package);fclose(file);return failed(NULL,"cannot read package manifest");}
    result=kui_update_manifest_decode(package,manifest,bytes);free(manifest);
    if(result!=KUI_UPDATE_OK) {free(package);fclose(file);return failed(NULL,kui_update_result_name(result));}
    for(uint32_t i=0u;i<package->count;i++)if(member(file,package,package->entries+i,legacy)) {
        free(package);fclose(file);return 1;
    }
    if(fclose(file)) {free(package);return failed(NULL,"package close failed");}
    printf("{\"format\":%u,\"package\":\"%s\",\"members\":%u,\"flags\":%u,\"full\":%s,\"engine_minimum\":2,\"decoder\":\"current-source\"}\n",
        kui_update_package_version(package),package->build,package->count,(unsigned)package->flags,
        package->flags&KUI_UPDATE_FULL?"true":"false");
    free(package);return 0;
}
