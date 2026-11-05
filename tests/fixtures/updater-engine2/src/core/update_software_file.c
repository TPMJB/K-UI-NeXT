/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/update_software_file.h"
#include "kui/update_boot.h"
#include "kui/update.h"
#include <string.h>

FRESULT __real_f_open(FIL *,const TCHAR *,BYTE);
FRESULT __real_f_stat(const TCHAR *,FILINFO *);
FRESULT kui_update_physical_open(FIL *file,const TCHAR *path,BYTE mode) {
    return __real_f_open(file,path,mode);
}
FRESULT kui_update_physical_stat(const TCHAR *path,FILINFO *info) {
    return __real_f_stat(path,info);
}

static bool software_path(const char *path) {
    static const char prefix[]="0:/KUI/";
    return path && !strncmp(path,prefix,sizeof(prefix)-1u) &&
        kui_update_module_path_valid(path+sizeof(prefix)-1u);
}
static FRESULT resolve(const char *path,char *resolved,struct kui_update_runtime_stream *stream) {
    if(kui_update_runtime_resolve(path,resolved,KUI_UPDATE_BOOT_PATH_BYTES,stream))return FR_OK;
    return kui_update_runtime_absent(path)?FR_NO_FILE:FR_NOT_READY;
}
static __attribute__((noinline)) FRESULT verify_raw(FIL *file,struct kui_update_runtime_stream *stream) {
    if(f_size(file)!=stream->bytes || !stream->update || !stream->finish)return FR_INT_ERR;
    const FSIZE_t cursor=f_tell(file);
    FRESULT result=f_lseek(file,0u);
    uint8_t buffer[4096];uint64_t remaining=stream->bytes;
    while(result==FR_OK && remaining) {
        UINT bytes=remaining>sizeof(buffer)?(UINT)sizeof(buffer):(UINT)remaining,got=0;
        result=f_read(file,buffer,bytes,&got);
        if(result!=FR_OK)break;
        if(got!=bytes) {result=FR_INT_ERR;break;}
        stream->update(stream,buffer,got);remaining-=got;
    }
    if(result==FR_OK && !stream->finish(stream))result=FR_INT_ERR;
    FRESULT restored=f_lseek(file,cursor);
    return result==FR_OK?restored:result;
}
FRESULT __wrap_f_open(FIL *file,const TCHAR *path,BYTE mode) {
    /* FA_READ alone is the only non-mutating open mode. In particular, an
     * existing file opened for both read and write stays in its root path. */
    if(mode!=FA_READ || !software_path(path))return __real_f_open(file,path,mode);
    char resolved[KUI_UPDATE_BOOT_PATH_BYTES];struct kui_update_runtime_stream stream;
    FRESULT result=resolve(path,resolved,&stream);
    if(result!=FR_OK)return result;
    result=__real_f_open(file,resolved,mode);
    if(result!=FR_OK || !stream.pinned || !stream.raw)return result;
    result=verify_raw(file,&stream);
    if(result!=FR_OK)f_close(file);
    return result;
}
FRESULT __wrap_f_stat(const TCHAR *path,FILINFO *info) {
    if(!software_path(path))return __real_f_stat(path,info);
    char resolved[KUI_UPDATE_BOOT_PATH_BYTES];struct kui_update_runtime_stream stream;
    FRESULT result=resolve(path,resolved,&stream);
    return result==FR_OK?__real_f_stat(resolved,info):result;
}
