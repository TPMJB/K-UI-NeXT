/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/recovery_guard.h"
#include "kui/destination.h"
#include "kui/hash.h"
#include "kui/media.h"
#include "diskio.h"
#include <stdio.h>
#include <string.h>

#define GUARD_BYTES 512u
static const char guard_name[]=".kui-readback-unstable";
static const char part_name[]=".kui-readback-unstable.part";
static uint32_t get32(const uint8_t *p) {return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void put32(uint8_t *p,uint32_t n) {for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(n>>(8*i));}
static bool paths(const char *directory,char folder[KUI_DEST_JOB_CAP],char final[KUI_DEST_PATH_CAP],char part[KUI_DEST_PATH_CAP]) {
    if(!kui_destination_job_normalize(folder,directory))return false;
    int a=snprintf(final,KUI_DEST_PATH_CAP,"%s/%s",folder,guard_name);
    int b=snprintf(part,KUI_DEST_PATH_CAP,"%s/%s",folder,part_name);
    return a>0 && b>0 && a<(int)KUI_DEST_PATH_CAP && b<(int)KUI_DEST_PATH_CAP;
}
static bool present(const char *path,bool *exists) {
    FILINFO info;FRESULT r=f_stat(path,&info);*exists=r==FR_OK;
    return r==FR_OK || r==FR_NO_FILE;
}
static bool binding(const char *folder,uint8_t result[64]) {
    struct kui_sha256 hash;kui_sha256_init(&hash);
    /* ASCII case aliases identify the same FAT folder. The actual canonical
     * spelling is retained for all filesystem operations. */
    for(const unsigned char *p=(const unsigned char *)folder;*p;++p) {
        uint8_t c=*p;if(c>='A' && c<='Z')c=(uint8_t)(c-'A'+'a');
        kui_sha256_update(&hash,&c,1);
    }
    kui_sha256_digest(&hash,result);
    uint8_t boot[512],extent[8];const struct kui_volume *volume=kui_media_volume();
    if(!volume || !volume->count || disk_read(0,boot,0,1)!=RES_OK || boot[510]!=0x55 || boot[511]!=0xaa)return false;
    put32(extent,volume->start);put32(extent+4,volume->count);kui_sha256_init(&hash);
    kui_sha256_update(&hash,extent,sizeof(extent));
    if(!memcmp(boot+3,"EXFAT   ",8)) {
        kui_sha256_update(&hash,boot+3,8);kui_sha256_update(&hash,boot+64,42);
        kui_sha256_update(&hash,boot+108,4); /* exclude dirty/usage fields */
    } else {
        kui_sha256_update(&hash,"FAT32",5);kui_sha256_update(&hash,boot+11,53);
        kui_sha256_update(&hash,boot+67,4);
    }
    kui_sha256_digest(&hash,result+32);return true;
}
static bool read_marker(const char *path,uint8_t data[GUARD_BYTES]) {
    FIL file;FRESULT r=f_open(&file,path,FA_READ);if(r!=FR_OK)return false;
    UINT got=0;bool ok=f_size(&file)==GUARD_BYTES && f_read(&file,data,GUARD_BYTES,&got)==FR_OK && got==GUARD_BYTES;
    if(f_close(&file)!=FR_OK)ok=false;
    return ok;
}
static bool valid_marker(const uint8_t data[GUARD_BYTES],const uint8_t identity[64]) {
    if(memcmp(data,"KUIGRD1",8) || get32(data+8)!=1 || get32(data+12)!=GUARD_BYTES ||
       memcmp(data+16,identity,64) || get32(data+508)!=kui_crc32(0,data,508))return false;
    for(unsigned i=80;i<508;++i)if(data[i])return false;
    return true;
}
bool kui_recovery_guard_check(const char *directory,kui_log_fn log) {
    char folder[KUI_DEST_JOB_CAP],final[KUI_DEST_PATH_CAP],part[KUI_DEST_PATH_CAP];bool a=false,b=false;
    if(!paths(directory,folder,final,part) || !present(final,&a) || !present(part,&b)) {
        if(log)log("Recovery guard: cannot inspect saved-read quarantine");
        return false;
    }
    if(a || b) {
        if(log)log("Saved reads previously disagreed in %s; complete Advanced CRC repeat-read scan before continuing",folder+2);
        return false;
    }
    return true;
}
bool kui_recovery_guard_mark(const char *directory,kui_log_fn log) {
    char folder[KUI_DEST_JOB_CAP],final[KUI_DEST_PATH_CAP],part[KUI_DEST_PATH_CAP];uint8_t data[GUARD_BYTES],identity[64],saved[GUARD_BYTES];bool exists=false;
    if(!paths(directory,folder,final,part) || !binding(folder,identity) || !present(final,&exists))goto failed;
    if(exists) {
        if(read_marker(final,saved) && valid_marker(saved,identity))return true;
        goto failed;
    }
    memset(data,0,sizeof(data));memcpy(data,"KUIGRD1",8);put32(data+8,1);put32(data+12,GUARD_BYTES);
    memcpy(data+16,identity,64);put32(data+508,kui_crc32(0,data,508));
    FIL file;if(f_open(&file,part,FA_WRITE|FA_CREATE_ALWAYS)!=FR_OK)goto failed;
    UINT put=0;bool ok=f_write(&file,data,GUARD_BYTES,&put)==FR_OK && put==GUARD_BYTES && f_sync(&file)==FR_OK;
    if(f_close(&file)!=FR_OK)ok=false;
    if(!ok || !read_marker(part,saved) || memcmp(data,saved,GUARD_BYTES) || f_rename(part,final)!=FR_OK ||
       !read_marker(final,saved) || !valid_marker(saved,identity))goto failed;
    if(log)log("Saved-read instability quarantined: %s",folder+2);
    return true;
failed:
    if(log)log("Cannot publish saved-read quarantine; operation must remain incomplete");
    return false;
}
bool kui_recovery_guard_validate(const char *directory,kui_log_fn log) {
    char folder[KUI_DEST_JOB_CAP],final[KUI_DEST_PATH_CAP],part[KUI_DEST_PATH_CAP];bool a=false,b=false;uint8_t data[GUARD_BYTES],identity[64];
    if(!paths(directory,folder,final,part) || !present(final,&a) || !present(part,&b))return false;
    if(!a && !b)return true;
    if(!binding(folder,identity))return false;
    if(a && (!read_marker(final,data) || !valid_marker(data,identity))) {
        if(log)log("Saved-read quarantine identity/CRC differs; refusing to clear it");
        return false;
    }
    if(b && read_marker(part,data) && valid_marker(data,data+16) && memcmp(data+16,identity,64)) {
        if(log)log("Interrupted quarantine belongs to another job/volume; refusing to clear it");
        return false;
    }
    return true;
}
bool kui_recovery_guard_clear(const char *directory,kui_log_fn log) {
    char folder[KUI_DEST_JOB_CAP],final[KUI_DEST_PATH_CAP],part[KUI_DEST_PATH_CAP];bool a=false,b=false;
    if(!kui_recovery_guard_validate(directory,log) || !paths(directory,folder,final,part) ||
       !present(final,&a) || !present(part,&b))return false;
    if(!a && !b)return true;
    /* A torn .part is not authoritative. Only a caller's full, consistent
     * repeat-read permits removing that interrupted publication. */
    if(b && f_unlink(part)!=FR_OK)return false;
    if(a && f_unlink(final)!=FR_OK)return false;
    if(disk_ioctl(0,CTRL_SYNC,NULL)!=RES_OK)return false;
    if(log)log("Full consistent saved-data scan cleared quarantine: %s",folder+2);
    return true;
}
