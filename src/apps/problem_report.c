/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/problem_report.h"
#include "kui/clock.h"
#include "kui/game_launch_report.h"
#include "kui/games_scan_log.h"
#include "kui/runtime.h"
#include "kui/screenshot.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REPORT_NUMBER_MAX 999999u
#define REPORT_DIRECTORY_MAX 4096u
struct builder {
    char stage[KUI_PROBLEM_REPORT_PATH_CAP];
    char final[KUI_PROBLEM_REPORT_PATH_CAP];
    char manifest[4096];
    size_t used;
    struct kui_problem_report_view *out;
    kui_cancel_fn cancel;
};
static bool stopped(const struct builder *b) { return b->cancel && b->cancel(); }
static bool append(char *out,size_t capacity,size_t *used,const char *format,...) {
    if(*used>=capacity)return false;
    va_list arguments;va_start(arguments,format);
    int n=vsnprintf(out+*used,capacity-*used,format,arguments);va_end(arguments);
    if(n<0 || (size_t)n>=capacity-*used)return false;
    *used+=(size_t)n;return true;
}
static bool text_valid(const char *text) {
    if(!text)return false;
    for(unsigned i=0u;i<64u;++i) {
        unsigned char c=(unsigned char)text[i];
        if(!c)return i!=0u;
        if(c<32u || c>126u || c=='"' || c=='\\')return false;
    }
    return false;
}
static bool folder(const char *path) {
    FILINFO info;FRESULT r=f_stat(path,&info);
    if(r==FR_OK)return (info.fattrib&AM_DIR)!=0u;
    if(r!=FR_NO_FILE && r!=FR_NO_PATH)return false;
    return f_mkdir(path)==FR_OK;
}
static unsigned report_number(const char *name) {
    if(strncmp(name,"REPORT-",7u))return 0u;
    unsigned value=0u;
    for(unsigned i=7u;i<13u;++i) {
        if(name[i]<'0' || name[i]>'9')return 0u;
        value=value*10u+(unsigned)(name[i]-'0');
    }
    return !name[13] || !strcmp(name+13u,".part")?value:0u;
}
static bool new_folder(struct builder *b) {
    if(!folder("0:/KUI") || !folder("0:/KUI/reports") || !folder(KUI_PROBLEM_REPORT_ROOT))return false;
    DIR directory;FILINFO info;
    if(f_opendir(&directory,KUI_PROBLEM_REPORT_ROOT)!=FR_OK)return false;
    unsigned highest=0u,entries=0u;bool ok=true;
    for(;;) {
        if(stopped(b)) {ok=false;break;}
        if(f_readdir(&directory,&info)!=FR_OK) {ok=false;break;}
        if(!info.fname[0])break;
        if(++entries>REPORT_DIRECTORY_MAX) {ok=false;break;}
        unsigned number=report_number(info.fname);if(number>highest)highest=number;
    }
    if(f_closedir(&directory)!=FR_OK)ok=false;
    if(!ok)return false;
    unsigned next=highest==REPORT_NUMBER_MAX?1u:highest+1u;
    for(unsigned attempt=0u;attempt<=REPORT_DIRECTORY_MAX;++attempt) {
        snprintf(b->final,sizeof(b->final),KUI_PROBLEM_REPORT_ROOT "/REPORT-%06u",next);
        snprintf(b->stage,sizeof(b->stage),KUI_PROBLEM_REPORT_ROOT "/REPORT-%06u.part",next);
        FRESULT r=f_stat(b->final,&info);
        if(r==FR_NO_FILE) {
            r=f_mkdir(b->stage);
            if(r==FR_OK)return true;
            if(r!=FR_EXIST)return false;
        } else if(r!=FR_OK)return false;
        next=next==REPORT_NUMBER_MAX?1u:next+1u;
    }
    return false;
}
static bool verify(const char *path,uint32_t expected_bytes,uint32_t expected_crc,struct builder *b) {
    FIL file;if(f_open(&file,path,FA_READ)!=FR_OK)return false;
    bool ok=f_size(&file)==expected_bytes;uint32_t bytes=0u,crc=0u;
    uint8_t page[1024];
    while(ok && bytes<expected_bytes) {
        if(stopped(b)) {ok=false;break;}
        UINT take=expected_bytes-bytes;if(take>sizeof(page))take=sizeof(page);
        UINT got=0u;ok=f_read(&file,page,take,&got)==FR_OK && got==take;
        if(ok) {crc=kui_crc32(crc,page,got);bytes+=got;}
    }
    if(f_close(&file)!=FR_OK)ok=false;
    return ok && bytes==expected_bytes && crc==expected_crc;
}
/* source==NULL writes frozen memory. Both forms use create-new output and
 * verify a closed file before naming it inside the unpublished directory. */
static bool item(struct builder *b,const char *name,const char *source,
    const void *memory,size_t bytes,bool in_manifest) {
    char path[KUI_PROBLEM_REPORT_PATH_CAP+40u],part[sizeof(path)];
    if(snprintf(path,sizeof(path),"%s/%s",b->stage,name)>=(int)sizeof(path) ||
       snprintf(part,sizeof(part),"%s.part",path)>=(int)sizeof(part) || bytes>UINT32_MAX)return false;
    FIL input,output;bool input_open=false;
    if(source) {
        if(f_open(&input,source,FA_READ)!=FR_OK)return false;
        input_open=true;
        if(f_size(&input)!=bytes) {f_close(&input);return false;}
    }
    if(stopped(b) || f_open(&output,part,FA_WRITE|FA_CREATE_NEW)!=FR_OK) {
        if(input_open)f_close(&input);
        return false;
    }
    uint8_t page[1024];size_t copied=0u;uint32_t crc=0u;bool ok=true;
    while(copied<bytes && ok) {
        if(stopped(b)) {ok=false;break;}
        UINT take=(UINT)(bytes-copied);if(bytes-copied>sizeof(page))take=sizeof(page);
        const uint8_t *data=memory?(const uint8_t *)memory+copied:page;
        if(input_open) {UINT got=0u;ok=f_read(&input,page,take,&got)==FR_OK && got==take;}
        if(ok) {
            UINT written=0u;ok=f_write(&output,data,take,&written)==FR_OK && written==take;
            if(ok) {crc=kui_crc32(crc,data,take);copied+=take;}
        }
    }
    if(ok)ok=f_sync(&output)==FR_OK;
    if(f_close(&output)!=FR_OK)ok=false;
    if(input_open && f_close(&input)!=FR_OK)ok=false;
    if(ok)ok=verify(part,(uint32_t)bytes,crc,b) && !stopped(b) && f_rename(part,path)==FR_OK;
    if(!ok)return false;
    if(in_manifest && !append(b->manifest,sizeof(b->manifest),&b->used,
        "%s    {\"name\":\"%s\",\"bytes\":%lu,\"crc32\":\"%08lx\"}",
        b->out->files?",\n":"",name,(unsigned long)bytes,(unsigned long)crc))return false;
    ++b->out->files;b->out->bytes+=bytes;return true;
}
static bool module_headers(struct builder *b) {
    static const char *const paths[]={
        KUI_RUNTIME_PATH,"0:/KUI/apps/games/retail-boot.kui",
        "0:/KUI/apps/games/retail-native-dual.kui",
        "0:/KUI/apps/games/retail-native-dual-a1.kui",
        "0:/KUI/apps/games/retail-native-standalone.kui",
        "0:/KUI/apps/games/ce-probe.kui","0:/KUI/apps/games/ce-cdda-armada.kui",
        "0:/KUI/apps/games/ce-cdda-bam4.kui","0:/KUI/apps/games/ce-cdda-worms.kui",
        "0:/KUI/apps/games/ce-cdda-wwp.kui","0:/KUI/apps/games/retail-physical.kui"
    };
    char text[4096];size_t used=0u;
    if(!append(text,sizeof(text),&used,"K-UI module headers\nHeader checks only; payloads are not reread.\n"))return false;
    for(unsigned i=0u;i<sizeof(paths)/sizeof(paths[0]);++i) {
        if(stopped(b))return false;
        FIL file;struct kui_runtime_info runtime={0};enum kui_runtime_result result=KUI_RUNTIME_IO;
        FRESULT opened=f_open(&file,paths[i],FA_READ);
        if(opened==FR_OK) {
            uint8_t header[64];UINT got=0u;uint64_t bytes=f_size(&file);
            if(f_read(&file,header,sizeof(header),&got)==FR_OK && got==sizeof(header))
                result=kui_runtime_header(header,bytes,&runtime);
            if(f_close(&file)!=FR_OK)result=KUI_RUNTIME_IO;
        }
        if(result==KUI_RUNTIME_OK) {
            if(!append(text,sizeof(text),&used,"%s | build=%s payload=%lu memory=%lu crc32=%08lx\n",
                paths[i]+2,runtime.build,(unsigned long)runtime.payload_bytes,
                (unsigned long)runtime.memory_bytes,(unsigned long)runtime.crc32))return false;
        } else {
            if(!append(text,sizeof(text),&used,"%s | %s\n",paths[i]+2,
                opened==FR_NO_FILE || opened==FR_NO_PATH?"not installed":kui_runtime_result_name(result)))return false;
        }
    }
    return item(b,"module-headers.txt",NULL,text,used,true);
}
static unsigned screenshot_number(const char *name) {
    if(strlen(name)!=14u)return 0u;
    char normalized[15];
    for(unsigned i=0u;i<15u;++i)normalized[i]=name[i]>='a' && name[i]<='z'?(char)(name[i]-'a'+'A'):name[i];
    if(strncmp(normalized,"KUI-",4u) || strcmp(normalized+10u,".BMP"))return 0u;
    unsigned value=0u;
    for(unsigned i=4u;i<10u;++i) {
        if(name[i]<'0' || name[i]>'9')return 0u;
        value=value*10u+(unsigned)(name[i]-'0');
    }
    return value;
}
static bool latest_screenshot(char path[96],size_t *bytes,struct builder *b) {
    DIR directory;FILINFO info;path[0]=0;*bytes=0u;
    FRESULT r=f_opendir(&directory,KUI_SCREENSHOT_FOLDER);
    if(r==FR_NO_FILE || r==FR_NO_PATH)return true;
    if(r!=FR_OK)return false;
    uint32_t best_date=0u;unsigned best_number=0u,entries=0u;bool ok=true;
    for(;;) {
        if(stopped(b)) {ok=false;break;}
        r=f_readdir(&directory,&info);
        if(r!=FR_OK) {ok=false;break;}
        if(!info.fname[0])break;
        if(++entries>REPORT_DIRECTORY_MAX) {ok=false;break;}
        unsigned number=screenshot_number(info.fname);
        uint32_t date=(uint32_t)info.fdate<<16|info.ftime;
        if(!number || (info.fattrib&AM_DIR) || info.fsize<54u || info.fsize>921654u)continue;
        if(!best_number || date>best_date || (date==best_date && number>best_number)) {
            best_date=date;best_number=number;*bytes=(size_t)info.fsize;
            snprintf(path,96,KUI_SCREENSHOT_FOLDER "/%.14s",info.fname);
        }
    }
    if(f_closedir(&directory)!=FR_OK)ok=false;
    return ok;
}
static bool scan_breadcrumb(struct builder *b,kui_log_fn log) {
    char *text=malloc(KUI_GAMES_SCAN_LOG_TEXT_MAX+1u);size_t bytes=0u;
    if(!text) {++b->out->unavailable;return true;}
    bool loaded=kui_games_scan_log_load(text,KUI_GAMES_SCAN_LOG_TEXT_MAX+1u,&bytes,log);
    bool ok=true;
    if(loaded && bytes)ok=item(b,"games-scan.txt",NULL,text,bytes,true);
    else ++b->out->unavailable;
    free(text);return ok;
}
bool kui_problem_report_export_mounted(const struct kui_problem_report_options *options,
    const char *version,const char *build,const void *log_data,size_t log_bytes,
    struct kui_problem_report_view *out,kui_log_fn log,kui_cancel_fn cancel) {
    if(!out)return false;
    memset(out,0,sizeof(*out));out->status.complete=true;
    if(!options || !text_valid(version) || !text_valid(build) ||
       log_bytes>KUI_PROBLEM_REPORT_LOG_MAX || (log_bytes && !log_data)) {
        out->status.errors=1u;snprintf(out->status.message,sizeof(out->status.message),"Invalid support report request");return false;
    }
    struct builder *b=calloc(1u,sizeof(*b));
    if(!b) {out->status.errors=1u;snprintf(out->status.message,sizeof(out->status.message),"Insufficient memory for report");return false;}
    b->out=out;b->cancel=cancel;struct kui_datetime time={0};bool time_valid=kui_clock_now(&time);
    bool ok=!stopped(b) && new_folder(b);
    if(ok)ok=append(b->manifest,sizeof(b->manifest),&b->used,
        "{\n  \"schema\":\"kui.support-report.v1\",\n  \"version\":\"%s\",\n  \"build\":\"%s\",\n"
        "  \"clock_valid\":%s,\n  \"created_local\":\"%04u-%02u-%02uT%02u:%02u:%02u\",\n  \"files\":[\n",
        version,build,time_valid?"true":"false",time.year,time.month,time.day,time.hour,time.minute,time.second);
    static const char empty_log[]="No session log was available.\n";
    if(ok)ok=item(b,"session-log.txt",NULL,log_bytes?log_data:empty_log,log_bytes?log_bytes:sizeof(empty_log)-1u,true);
    if(ok)ok=module_headers(b);
    if(ok)ok=scan_breadcrumb(b,log);
    static const char *const launch_paths[]={KUI_GAME_LAUNCH_REPORT_PATH_A,KUI_GAME_LAUNCH_REPORT_PATH_B};
    static const char *const launch_names[]={"last-launch-a.txt","last-launch-b.txt"};
    for(unsigned i=0u;ok && i<2u;++i) {
        FILINFO info;FRESULT r=f_stat(launch_paths[i],&info);
        if(r==FR_NO_FILE || r==FR_NO_PATH) {++out->unavailable;continue;}
        if(r!=FR_OK || (info.fattrib&AM_DIR) || !info.fsize || info.fsize>KUI_GAME_LAUNCH_REPORT_MAX) {++out->unavailable;continue;}
        ok=item(b,launch_names[i],launch_paths[i],NULL,(size_t)info.fsize,true);
    }
    if(ok && options->include_latest_screenshot) {
        char screenshot[96];size_t bytes;
        ok=latest_screenshot(screenshot,&bytes,b);
        if(ok && screenshot[0])ok=item(b,"screenshot.bmp",screenshot,NULL,bytes,true);
        else if(ok)++out->unavailable;
    }
    if(ok)ok=append(b->manifest,sizeof(b->manifest),&b->used,
        "\n  ],\n  \"unavailable_optional_inputs\":%u,\n  \"launch_reports_describe_preparation\":true\n}\n",out->unavailable);
    if(ok)ok=item(b,"manifest.json",NULL,b->manifest,b->used,false);
    if(ok)ok=!stopped(b) && f_rename(b->stage,b->final)==FR_OK;
    out->status.stopped=stopped(b);
    if(ok && !out->status.stopped) {
        memcpy(out->path,b->final,strlen(b->final)+1u);out->status.passed=true;
        out->status.done=out->status.total=out->bytes;
        snprintf(out->status.message,sizeof(out->status.message),"Support report saved: %u files",out->files);
        snprintf(out->status.lines[0],KUI_APP_LINE_CAP,"%.79s",out->path+2u);
        snprintf(out->status.lines[1],KUI_APP_LINE_CAP,"%u optional inputs unavailable",out->unavailable);
        out->status.line_count=2u;
        if(log)log("Support report saved: %s",out->path+2u);
    } else {
        out->status.errors=out->status.stopped?0u:1u;
        snprintf(out->status.message,sizeof(out->status.message),"%s",out->status.stopped?
            "Report export stopped; no completed report confirmed":"Report export failed; no completed report confirmed");
        if(log)log("Support report: %s",out->status.message);
    }
    free(b);return out->status.passed;
}
