/* SPDX-License-Identifier: GPL-3.0-only */
/* Reuse the session fixture and its manifest/descriptor gates. Only FatFs
 * file access is bridged to a real read-only host stream for this test. */
#define main session_fixture_main
#define f_open fixture_f_open
#define f_read fixture_f_read
#define f_close fixture_f_close
#define kui_native_session_map_file fixture_map_file
#include "test_native_session_prepare.c"
#undef main
#undef f_open
#undef f_read
#undef f_close
#undef kui_native_session_map_file
#include <sys/stat.h>

static FILE *source;
static const char *source_path;
static uint64_t source_bytes,read_bytes;
static unsigned reads,payload_reads,largest,stream_fault,batch_allocations,batch_live;
static bool fail_batch;
static void *batch;
void *__real_malloc(size_t);
void __real_free(void *);
void *__wrap_malloc(size_t bytes) {
    if(bytes==32768u) {
        ++batch_allocations;
        if(fail_batch)return NULL;
        assert(!batch_live);batch=__real_malloc(bytes);
        if(batch)++batch_live;
        return batch;
    }
    return __real_malloc(bytes);
}
void __wrap_free(void *p) {
    if(p && p==batch) {assert(batch_live==1u);batch_live=0u;batch=NULL;}
    __real_free(p);
}
FRESULT f_open(FIL *file,const TCHAR *path,BYTE mode) {
    assert(!source && !strcmp(path,KUI_RUNTIME_PATH) && mode==FA_READ);++opens;
    if(stream_fault==6u)return FR_DISK_ERR;
    source=fopen(source_path,"rb");assert(source);
    struct stat info;assert(!stat(source_path,&info) && info.st_size>=0);
    source_bytes=(uint64_t)info.st_size;
    memset(file,0,sizeof(*file));file->obj.objsize=source_bytes;return FR_OK;
}
FRESULT f_read(FIL *file,void *data,UINT want,UINT *got) {
    assert(source);++reads;
    if(reads>1u) {++payload_reads;if(want>largest)largest=want;}
    if(stream_fault==2u && reads==2u) {*got=0u;return FR_DISK_ERR;}
    UINT take=stream_fault==1u && reads==2u?want-4u:want;
    *got=(UINT)fread(data,1u,take,source);file->fptr+=*got;read_bytes+=*got;
    if(stream_fault==4u && reads==2u)cancelled=true;
    if(stream_fault==5u && reads==2u)((uint8_t *)data)[*got-1u]^=1u;
    return ferror(source)?FR_DISK_ERR:FR_OK;
}
FRESULT f_close(FIL *file) {
    (void)file;assert(source);++closes;
    int result=fclose(source);source=NULL;
    return result || stream_fault==3u?FR_DISK_ERR:FR_OK;
}
bool kui_native_session_map_file(FATFS *fs,const struct kui_volume *v,const char *path,
    struct kui_retail_extent *extents,unsigned cap,unsigned *count,uint64_t *bytes,
    const char **problem,kui_cancel_fn stop) {
    (void)fs;(void)v;(void)problem;
    assert(!strcmp(path,KUI_RUNTIME_PATH) && cap==KUI_NATIVE_RETURN_EXTENTS && !stop());
    if(stream_fault==7u)return false;
    extents[0]=(struct kui_retail_extent){0u,50000u,(uint32_t)((source_bytes+511u)/512u)};
    *count=1u;*bytes=source_bytes;return true;
}
int main(int argc,char **argv) {
    assert(argc==6);source_path=argv[1];fail_batch=atoi(argv[2])!=0;
    stream_fault=(unsigned)atoi(argv[3]);bool expected=atoi(argv[4])!=0;
    fixture();
    uint8_t *wire=malloc(KUI_NATIVE_SESSION_WIRE_BYTES),*before=malloc(KUI_NATIVE_SESSION_WIRE_BYTES);
    assert(wire && before);blank(wire);memcpy(before,wire,KUI_NATIVE_SESSION_WIRE_BYTES);
    struct kui_runtime_image package={.data=wire,.info={.payload_bytes=KUI_NATIVE_SESSION_WIRE_BYTES}};
    FATFS fs={0};struct kui_volume volume={.start=100u,.count=99900u};const char *problem=NULL;
    bool ok=kui_native_session_prepare(&fs,&volume,"/Games/Example/disc.gdi",NULL,
        &member,"V1.000",&package,&problem,log_line,cancel);
    assert(ok==expected && !source && !batch_live);
    if(ok) {
        assert(read_bytes==source_bytes && kui_native_session_wire_valid(wire,KUI_NATIVE_SESSION_WIRE_BYTES,true));
        FILE *output=fopen(argv[5],"wb");assert(output);
        assert(fwrite(wire,1u,KUI_NATIVE_SESSION_WIRE_BYTES,output)==KUI_NATIVE_SESSION_WIRE_BYTES);
        assert(!fclose(output));
    } else assert(!memcmp(before,wire,KUI_NATIVE_SESSION_WIRE_BYTES));
    printf("{\"ok\":%s,\"read_bytes\":%llu,\"payload_reads\":%u,\"largest\":%u,\"opens\":%u,\"closes\":%u,\"batch_allocations\":%u}\n",
        ok?"true":"false",(unsigned long long)read_bytes,payload_reads,largest,opens,closes,batch_allocations);
    free(before);free(wire);return 0;
}
