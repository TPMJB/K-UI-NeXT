/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/update_zip.h"
#include "kui/update_browser.h"
#include "kui/hash.h"
#include "miniz.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ZIP_LIMIT (UINT64_C(512)*1024u*1024u)
#define DIRECTORY_LIMIT (1024u*1024u)
#define ENTRY_LIMIT 4096u
#define INPUT_BYTES 16384u
#define RING_BYTES 32768u
#define STAGE_DIR "0:/KUI/system/update-stage"
#define PARTIAL STAGE_DIR "/owned.partial"
#define PACKAGE_LIMIT (KUI_UPDATE_MAX_PAYLOAD+KUI_UPDATE_HEADER_BYTES+KUI_UPDATE_MAX_ENTRIES*KUI_UPDATE_ENTRY_BYTES)
struct member {
    char name[176];uint16_t flags,method,name_bytes;
    uint32_t crc,compressed,bytes,local;uint64_t data,directory;
};
struct work {
    FIL zip,sink;struct member member;tinfl_decompressor inflater;
    uint8_t input[INPUT_BYTES],ring[RING_BYTES];
    struct kui_app_status *status;kui_cancel_fn cancel;kui_app_progress_fn progress;
    const char *message;uint64_t zip_bytes,written;uint32_t crc;
    bool zip_open,sink_open,partial_owned;
};
static uint16_t get16(const uint8_t *p) {return (uint16_t)((uint16_t)p[0]|(uint16_t)p[1]<<8);}
static uint32_t get32(const uint8_t *p) {return (uint32_t)get16(p)|(uint32_t)get16(p+2u)<<16;}
static bool stopped(struct work *w) {return w->cancel && w->cancel();}
static enum kui_update_result issue(struct work *w,enum kui_update_result r,const char *message) {
    w->message=message;return r;
}
static bool range(uint64_t offset,uint64_t bytes,uint64_t limit) {return offset<=limit && bytes<=limit-offset;}
static enum kui_update_result read_at(struct work *w,uint64_t offset,void *out,UINT bytes) {
    if(stopped(w))return issue(w,KUI_UPDATE_CANCELLED,"ZIP preparation stopped; selection unchanged");
    if(!range(offset,bytes,w->zip_bytes) || offset>(uint64_t)(FSIZE_t)-1)
        return issue(w,KUI_UPDATE_FORMAT,"ZIP member range is outside the archive");
    UINT got=0;
    if(f_lseek(&w->zip,(FSIZE_t)offset)!=FR_OK || f_read(&w->zip,out,bytes,&got)!=FR_OK || got!=bytes)
        return issue(w,KUI_UPDATE_IO,"Could not read the selected ZIP");
    return KUI_UPDATE_OK;
}
static enum kui_update_result extra(struct work *w,uint64_t offset,uint16_t bytes) {
    uint64_t end=offset+bytes;
    while(offset<end) {
        uint8_t header[4];if(end-offset<4u)return issue(w,KUI_UPDATE_FORMAT,"Malformed ZIP extra field");
        enum kui_update_result r=read_at(w,offset,header,sizeof(header));if(r)return r;
        if(get16(header)==1u)return issue(w,KUI_UPDATE_FORMAT,"ZIP64 update bundles are unsupported");
        offset+=4u;if(get16(header+2u)>end-offset)return issue(w,KUI_UPDATE_FORMAT,"Malformed ZIP extra field");
        offset+=get16(header+2u);
    }
    return KUI_UPDATE_OK;
}
static enum kui_update_result directory(struct work *w) {
    if(w->zip_bytes<22u || w->zip_bytes>ZIP_LIMIT)
        return issue(w,KUI_UPDATE_FORMAT,"ZIP size is outside the supported limit");
    size_t bytes=w->zip_bytes>65557u?65557u:(size_t)w->zip_bytes;
    uint8_t *tail=malloc(bytes);if(!tail)return issue(w,KUI_UPDATE_MEMORY,"Not enough RAM to inspect the ZIP directory");
    uint64_t start=w->zip_bytes-bytes;enum kui_update_result r=read_at(w,start,tail,(UINT)bytes);
    size_t at=bytes-22u;bool found=false;
    if(!r)for(;;) {
        if(get32(tail+at)==UINT32_C(0x06054b50) && at+22u+get16(tail+at+20u)==bytes) {found=true;break;}
        if(!at)break;
        --at;
    }
    uint64_t offset=0u,end=0u;uint16_t count=0u;
    if(!r && !found)r=issue(w,KUI_UPDATE_FORMAT,"ZIP end record is missing or malformed");
    if(!r) {
        const uint8_t *e=tail+at;count=get16(e+10u);offset=get32(e+16u);uint32_t length=get32(e+12u);
        if(get16(e+4u) || get16(e+6u) || get16(e+8u)!=count)
            r=issue(w,KUI_UPDATE_FORMAT,"Split ZIP update bundles are unsupported");
        else if(count==UINT16_MAX || length==UINT32_MAX || offset==UINT32_MAX)
            r=issue(w,KUI_UPDATE_FORMAT,"ZIP64 update bundles are unsupported");
        else if(!count || count>ENTRY_LIMIT || length>DIRECTORY_LIMIT || !range(offset,length,start+at) || offset+length!=start+at)
            r=issue(w,KUI_UPDATE_FORMAT,"ZIP directory exceeds its bounds or entry limit");
        end=offset+length;w->member.directory=offset;
    }
    free(tail);if(r)return r;
    unsigned packages=0u;
    for(unsigned i=0;i<count;i++) {
        uint8_t h[46];if(!range(offset,sizeof(h),end))return issue(w,KUI_UPDATE_FORMAT,"Truncated ZIP directory");
        if((r=read_at(w,offset,h,sizeof(h))))return r;
        if(get32(h)!=UINT32_C(0x02014b50))return issue(w,KUI_UPDATE_FORMAT,"Invalid ZIP directory entry");
        uint16_t flags=get16(h+8u),method=get16(h+10u),name=get16(h+28u),more=get16(h+30u),comment=get16(h+32u);
        uint64_t length=46u+(uint64_t)name+more+comment;
        if(!range(offset,length,end) || !name)return issue(w,KUI_UPDATE_FORMAT,"Invalid ZIP filename or directory range");
        if(get16(h+34u))return issue(w,KUI_UPDATE_FORMAT,"Split ZIP update bundles are unsupported");
        if(get16(h+6u)>20u || get32(h+20u)==UINT32_MAX || get32(h+24u)==UINT32_MAX || get32(h+42u)==UINT32_MAX)
            return issue(w,KUI_UPDATE_FORMAT,"ZIP64 or newer ZIP features are unsupported");
        if(flags&~(UINT16_C(0x080e)))return issue(w,KUI_UPDATE_FORMAT,"Encrypted or unsupported ZIP flags");
        if((method!=0u && method!=8u) || (method==0u && (flags&6u)))
            return issue(w,KUI_UPDATE_FORMAT,"ZIP compression must be stored or deflate");
        if((r=extra(w,offset+46u+name,more)))return r;
        /* Only candidate suffixes need their complete name. No archive name
         * is used as a destination: source/evidence and old GUIs are ignored. */
        uint8_t suffix[4]={0};
        if(name>=4u && (r=read_at(w,offset+46u+name-4u,suffix,sizeof(suffix))))return r;
        bool kup=name>=4u && suffix[0]=='.' && (suffix[1]=='k' || suffix[1]=='K') &&
            (suffix[2]=='u' || suffix[2]=='U') && (suffix[3]=='p' || suffix[3]=='P');
        if(kup) {
            if(++packages>1u)return issue(w,KUI_UPDATE_FORMAT,"ZIP has multiple update packages; use a single-package bundle");
            struct member *m=&w->member;
            if(name>=sizeof(m->name))return issue(w,KUI_UPDATE_FORMAT,"Embedded update package path is too long");
            if((r=read_at(w,offset+46u,m->name,name)))return r;
            m->name[name]=0;
            char canonical[192];int n=snprintf(canonical,sizeof(canonical),KUI_UPDATE_BROWSER_ROOT "/%s",m->name);
            if(memchr(m->name,0,name) || n<0 || (size_t)n>=sizeof(canonical) || !kui_update_browser_path(canonical))
                return issue(w,KUI_UPDATE_FORMAT,"Embedded update package path is not canonical");
            uint32_t kind=(get32(h+38u)>>16u)&0170000u;
            if((get32(h+38u)&16u) || (kind && kind!=0100000u))
                return issue(w,KUI_UPDATE_FORMAT,"Embedded update package is not a regular file");
            m->name_bytes=name;m->flags=flags;m->method=method;m->crc=get32(h+16u);
            m->compressed=get32(h+20u);m->bytes=get32(h+24u);m->local=get32(h+42u);
            if(m->bytes<128u || m->bytes>PACKAGE_LIMIT || !m->compressed)
                return issue(w,KUI_UPDATE_FORMAT,"Embedded update package size is outside its limit");
        }
        offset+=length;
    }
    if(offset!=end)return issue(w,KUI_UPDATE_FORMAT,"ZIP directory length does not match its entries");
    if(!packages)return issue(w,KUI_UPDATE_FORMAT,"Legacy ZIP needs an updater package (.kup)");
    struct member *m=&w->member;uint8_t local[30];
    if(!range(m->local,sizeof(local),m->directory))return issue(w,KUI_UPDATE_FORMAT,"ZIP local header overlaps its directory");
    if((r=read_at(w,m->local,local,sizeof(local))))return r;
    uint16_t name=get16(local+26u),more=get16(local+28u);
    if(get32(local)!=UINT32_C(0x04034b50) || get16(local+4u)>20u || get16(local+6u)!=m->flags ||
       get16(local+8u)!=m->method || name!=m->name_bytes)
        return issue(w,KUI_UPDATE_FORMAT,"ZIP local header does not match its directory");
    m->data=(uint64_t)m->local+30u+name+more;
    if(!range(m->local,30u+(uint64_t)name+more,m->directory) || !range(m->data,m->compressed,m->directory))
        return issue(w,KUI_UPDATE_FORMAT,"ZIP compressed member overlaps its directory");
    if((r=read_at(w,(uint64_t)m->local+30u,w->input,name)))return r;
    if(memcmp(w->input,m->name,name))return issue(w,KUI_UPDATE_FORMAT,"ZIP local filename does not match its directory");
    if((r=extra(w,(uint64_t)m->local+30u+name,more)))return r;
    uint32_t crc=get32(local+14u),compressed=get32(local+18u),plain=get32(local+22u);
    if((!(m->flags&8u) && (crc!=m->crc || compressed!=m->compressed || plain!=m->bytes)) ||
       ((m->flags&8u) && ((crc && crc!=m->crc) || (compressed && compressed!=m->compressed) || (plain && plain!=m->bytes))))
        return issue(w,KUI_UPDATE_FORMAT,"ZIP local sizes or checksum disagree with its directory");
    return KUI_UPDATE_OK;
}
static enum kui_update_result location(struct work *w,const char *path,bool directory_path) {
    FILINFO info;FRESULT r=f_stat(path,&info);
    if(r==FR_NO_FILE || r==FR_NO_PATH)return KUI_UPDATE_NOT_FOUND;
    if(r!=FR_OK)return issue(w,KUI_UPDATE_IO,"Could not inspect the private ZIP staging area");
    if((info.fattrib&AM_RDO) || (bool)(info.fattrib&AM_DIR)!=directory_path)
        return issue(w,KUI_UPDATE_PROTECTED,"Private ZIP staging path is read-only or has the wrong type");
    return KUI_UPDATE_OK;
}
static enum kui_update_result stage(struct work *w) {
    static const char *const dirs[]={"0:/KUI","0:/KUI/system",STAGE_DIR};
    for(unsigned i=0;i<sizeof(dirs)/sizeof(dirs[0]);i++) {
        enum kui_update_result r=location(w,dirs[i],true);if(r && r!=KUI_UPDATE_NOT_FOUND)return r;
        if(r && f_mkdir(dirs[i])!=FR_OK)return issue(w,KUI_UPDATE_IO,"Could not create the private ZIP staging area");
    }
    enum kui_update_result r=location(w,KUI_UPDATE_ZIP_STAGE_PATH,false);if(r && r!=KUI_UPDATE_NOT_FOUND)return r;
    r=location(w,PARTIAL,false);if(r && r!=KUI_UPDATE_NOT_FOUND)return r;
    if(!r && f_unlink(PARTIAL)!=FR_OK)return issue(w,KUI_UPDATE_IO,"Could not remove the old ZIP partial file");
    if(stopped(w))return issue(w,KUI_UPDATE_CANCELLED,"ZIP preparation stopped; selection unchanged");
    DWORD free_clusters=0u;FATFS *volume=NULL;
    if(f_getfree("0:",&free_clusters,&volume)!=FR_OK || !volume || !volume->csize)
        return issue(w,KUI_UPDATE_IO,"Could not check space for the extracted package");
    uint64_t cluster=(uint64_t)volume->csize*512u,required=((w->member.bytes+cluster-1u)/cluster+2u)*cluster;
    if((uint64_t)free_clusters*cluster<required)return issue(w,KUI_UPDATE_SPACE,"Not enough free space to stage the ZIP package");
    if(f_open(&w->sink,PARTIAL,FA_WRITE|FA_CREATE_NEW)!=FR_OK)return issue(w,KUI_UPDATE_IO,"Could not create the private ZIP partial file");
    w->sink_open=true;w->partial_owned=true;return KUI_UPDATE_OK;
}
static enum kui_update_result emit(struct work *w,const void *data,size_t bytes) {
    if(stopped(w))return issue(w,KUI_UPDATE_CANCELLED,"ZIP preparation stopped; selection unchanged");
    if(bytes>w->member.bytes-w->written)return issue(w,KUI_UPDATE_FORMAT,"ZIP expands beyond its declared package size");
    UINT done=0u;if(f_write(&w->sink,data,(UINT)bytes,&done)!=FR_OK || done!=bytes)
        return issue(w,KUI_UPDATE_IO,"Could not write the extracted ZIP package");
    w->crc=kui_crc32(w->crc,data,bytes);w->written+=bytes;
    if(w->status) {w->status->done=w->written;if(w->progress)w->progress(w->status);}
    return KUI_UPDATE_OK;
}
static enum kui_update_result extract(struct work *w) {
    struct member *m=&w->member;uint64_t unread=m->compressed,offset=m->data;enum kui_update_result r;
    if(!m->method) {
        if(m->compressed!=m->bytes)return issue(w,KUI_UPDATE_FORMAT,"Stored ZIP package sizes disagree");
        while(unread) {
            UINT n=(UINT)(unread>INPUT_BYTES?INPUT_BYTES:unread);
            if((r=read_at(w,offset,w->input,n)) || (r=emit(w,w->input,n)))return r;
            offset+=n;unread-=n;
        }
    } else {
        size_t used=0u,available=0u,ring=0u;tinfl_init(&w->inflater);
        for(;;) {
            if(used==available && unread) {
                available=(size_t)(unread>INPUT_BYTES?INPUT_BYTES:unread);used=0u;
                if((r=read_at(w,offset,w->input,(UINT)available)))return r;
                offset+=available;unread-=available;
            }
            if(stopped(w))return issue(w,KUI_UPDATE_CANCELLED,"ZIP preparation stopped; selection unchanged");
            size_t input=available-used,output=RING_BYTES-ring;
            tinfl_status state=tinfl_decompress(&w->inflater,w->input+used,&input,w->ring,w->ring+ring,&output,
                unread?TINFL_FLAG_HAS_MORE_INPUT:0u);
            used+=input;if(output && (r=emit(w,w->ring+ring,output)))return r;
            ring=(ring+output)&(RING_BYTES-1u);
            if(state==TINFL_STATUS_DONE) {
                if(unread || used!=available)return issue(w,KUI_UPDATE_FORMAT,"ZIP deflate stream has trailing compressed bytes");
                break;
            }
            if(state<0 || (!input && !output) || (state==TINFL_STATUS_NEEDS_MORE_INPUT && !unread))
                return issue(w,KUI_UPDATE_FORMAT,"ZIP deflate stream is damaged or truncated");
        }
    }
    if(w->written!=m->bytes || w->crc!=m->crc)return issue(w,KUI_UPDATE_CHECKSUM,"Extracted ZIP package size or CRC does not match");
    if(m->flags&8u) {
        uint64_t at=m->data+m->compressed;uint8_t descriptor[16];
        if(!range(at,12u,m->directory))return issue(w,KUI_UPDATE_FORMAT,"ZIP data descriptor is missing");
        if((r=read_at(w,at,descriptor,12u)))return r;
        bool matches=get32(descriptor)==m->crc && get32(descriptor+4u)==m->compressed && get32(descriptor+8u)==m->bytes;
        if(!matches && get32(descriptor)==UINT32_C(0x08074b50)) {
            if(!range(at,16u,m->directory))return issue(w,KUI_UPDATE_FORMAT,"ZIP data descriptor is truncated");
            if((r=read_at(w,at,descriptor,16u)))return r;
            matches=get32(descriptor+4u)==m->crc && get32(descriptor+8u)==m->compressed && get32(descriptor+12u)==m->bytes;
        }
        if(!matches)
            return issue(w,KUI_UPDATE_FORMAT,"ZIP data descriptor disagrees with its directory");
    }
    return KUI_UPDATE_OK;
}
static enum kui_update_result publish(struct work *w) {
    if(stopped(w))return issue(w,KUI_UPDATE_CANCELLED,"ZIP preparation stopped; selection unchanged");
    if(f_sync(&w->sink)!=FR_OK)return issue(w,KUI_UPDATE_IO,"ZIP partial synchronization failed; selection unchanged");
    FRESULT closed=f_close(&w->sink);w->sink_open=false;
    if(closed!=FR_OK)return issue(w,KUI_UPDATE_IO,"ZIP partial close failed; selection unchanged");
    closed=f_close(&w->zip);w->zip_open=false;
    if(closed!=FR_OK)return issue(w,KUI_UPDATE_IO,"ZIP source close failed; selection unchanged");
    if(stopped(w))return issue(w,KUI_UPDATE_CANCELLED,"ZIP preparation stopped; selection unchanged");
    enum kui_update_result r=location(w,KUI_UPDATE_ZIP_STAGE_PATH,false);if(r && r!=KUI_UPDATE_NOT_FOUND)return r;
    if(!r && f_unlink(KUI_UPDATE_ZIP_STAGE_PATH)!=FR_OK)return issue(w,KUI_UPDATE_IO,"Could not retire the previous ZIP cache; selection unchanged");
    if(f_rename(PARTIAL,KUI_UPDATE_ZIP_STAGE_PATH)!=FR_OK)
        return issue(w,KUI_UPDATE_IO,"ZIP cache publication not confirmed; selection unchanged; retry preparation");
    w->partial_owned=false;return KUI_UPDATE_OK;
}
enum kui_update_result kui_update_zip_prepare(const char *path,char out[192],struct kui_app_status *s,
    kui_log_fn log,kui_cancel_fn cancel,kui_app_progress_fn progress) {
    if(out)out[0]=0;
    if(s) {*s=(struct kui_app_status){0};snprintf(s->message,sizeof(s->message),"Checking ZIP update bundle...");if(progress)progress(s);}
    struct work *w=calloc(1,sizeof(*w));enum kui_update_result r=KUI_UPDATE_MEMORY;
    const char *message="Not enough RAM to prepare the ZIP package";
    if(w) {
        w->status=s;w->cancel=cancel;w->progress=progress;w->message=message;
        if(!out || !path || !kui_update_browser_path(path) || !kui_update_browser_suffix(path,".zip"))
            r=issue(w,KUI_UPDATE_ARGUMENT,"Choose a ZIP inside the Updates folder");
        else if(stopped(w))r=issue(w,KUI_UPDATE_CANCELLED,"ZIP preparation stopped; selection unchanged");
        else {
            FRESULT opened=f_open(&w->zip,path,FA_READ);
            if(opened!=FR_OK)r=issue(w,opened==FR_NO_FILE || opened==FR_NO_PATH?KUI_UPDATE_NOT_FOUND:KUI_UPDATE_IO,"Could not open the selected ZIP");
            else {
                w->zip_open=true;w->zip_bytes=f_size(&w->zip);r=directory(w);
                if(!r && s) {s->total=w->member.bytes;snprintf(s->message,sizeof(s->message),"Preparing the ZIP update package...");if(progress)progress(s);}
                if(!r)r=stage(w);
                if(!r)r=extract(w);
                if(!r)r=publish(w);
            }
        }
        if(w->sink_open && f_close(&w->sink)!=FR_OK && !r)r=issue(w,KUI_UPDATE_IO,"ZIP partial close not confirmed; selection unchanged");
        if(w->zip_open && f_close(&w->zip)!=FR_OK && !r)r=issue(w,KUI_UPDATE_IO,"ZIP source close not confirmed; selection unchanged");
        if(w->partial_owned && f_unlink(PARTIAL)!=FR_OK)
            w->message="ZIP preparation failed; partial cleanup not confirmed; selection unchanged";
        if(!r) {strcpy(out,KUI_UPDATE_ZIP_STAGE_PATH);w->message="ZIP package staged; checking its updater manifest next";}
        message=w->message;
    }
    if(s) {
        s->complete=true;s->passed=r==KUI_UPDATE_OK;s->stopped=r==KUI_UPDATE_CANCELLED;
        s->errors=r && r!=KUI_UPDATE_CANCELLED?1u:0u;
        snprintf(s->message,sizeof(s->message),"%s",message);if(progress)progress(s);
    }
    if(log)log("Update ZIP: %s",message);
    free(w);return r;
}
