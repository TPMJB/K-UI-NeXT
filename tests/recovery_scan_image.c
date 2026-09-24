/* SPDX-License-Identifier: GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#include "kui/recovery_scan.h"
#include "kui/media.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static struct {
    FILE *image;uint64_t blocks;const char *fault;bool scanning,injected,cancelled;
    bool report_seen,complete_seen;uint32_t first_track;
    DWORD track_clusters[3];uint64_t track_read_bytes;unsigned catalogue_cancel_checks;
    unsigned writes;uint64_t now;
    struct kui_scan_status *status;
} test;
static FATFS fs;
static const char *files[]={"manifest.json","disc.gdi","checkpoint-a.bin","checkpoint-b.bin","track01.bin","track02.raw","track03.bin"};
static const char *folder="0:/Games/SCAN TEST";
static bool fault(const char *s) {return test.scanning && !strcmp(test.fault,s);}
static void log_line(const char *format,...) {va_list args;va_start(args,format);vprintf(format,args);va_end(args);puts("");}
static uint64_t blocks(void *ctx) {(void)ctx;return test.blocks;}
static bool contains(const uint8_t *data,size_t n,const char *needle) {
    size_t len=strlen(needle);for(size_t i=0;i+len<=n;++i) if(!memcmp(data+i,needle,len)) return true;return false;
}
static int read_image(void *ctx,uint32_t block,size_t count,uint8_t *data) {
    (void)ctx;
    if((fault("read-fail") || fault("read-fail-cancel")) && block<=test.first_track && test.first_track<block+count) {
        test.injected=true;if(fault("read-fail-cancel")) test.cancelled=true;return -1;
    }
    return fseeko(test.image,(off_t)block*512,SEEK_SET) || fread(data,512,count,test.image)!=count?-1:0;
}
static int write_image(void *ctx,uint32_t block,size_t count,const uint8_t *data) {
    (void)ctx;++test.writes;
    if(test.scanning && contains(data,count*512,"K-UI Advanced saved-file scan v1")) test.report_seen=true;
    if(test.scanning && contains(data,count*512,"COMPLETE CLEAN")) test.complete_seen=true;
    if(fault("write-fail") && test.report_seen) {test.injected=true;return -1;}
    return fseeko(test.image,(off_t)block*512,SEEK_SET) || fwrite(data,512,count,test.image)!=count?-1:0;
}
static int sync_image(void *ctx) {
    (void)ctx;
    if((fault("sync-fail") && test.report_seen) || (fault("final-sync-fail") && test.complete_seen)) {test.injected=true;return -1;}
    return fflush(test.image) || fsync(fileno(test.image))?-1:0;
}
FRESULT __real_f_read(FIL *file,void *buffer,UINT bytes,UINT *read);
FRESULT __wrap_f_read(FIL *file,void *buffer,UINT bytes,UINT *read) {
    FRESULT result=__real_f_read(file,buffer,bytes,read);
    if(test.scanning) for(unsigned i=0;i<3;++i)
        if(file->obj.sclust==test.track_clusters[i]) test.track_read_bytes+=*read;
    return result;
}
static const struct kui_media_ops media={NULL,blocks,read_image,write_image,sync_image};
FRESULT __real_f_rename(const TCHAR *old_path,const TCHAR *new_path);
FRESULT __wrap_f_rename(const TCHAR *old_path,const TCHAR *new_path) {
    if(fault("rename-fail")) {test.injected=true;return FR_DISK_ERR;}
    return __real_f_rename(old_path,new_path);
}
FRESULT __real_f_close(FIL *file);
FRESULT __wrap_f_close(FIL *file) {
    FRESULT result=__real_f_close(file);
    if(fault("close-fail") && test.complete_seen) {test.injected=true;return FR_DISK_ERR;}
    return result;
}
static bool cancel(void *ctx) {
    (void)ctx;
    if(fault("imported-catalogue-cancel") && strstr(test.status->message,"Comparing recorded") &&
        ++test.catalogue_cancel_checks>=2) return true;
    return test.cancelled || fault("cancel-before") ||
        (fault("cancel-scan") && test.status->done>=2*KUI_RAW_BYTES);
}
static uint64_t now_ms(void *ctx) {(void)ctx;test.now+=100;return test.now;}
static void progress(void *ctx,const struct kui_scan_status *status) {
    (void)ctx;assert(status->done<=status->total);assert(status->track<=status->tracks);
}
static void remount(void) {assert(f_mount(NULL,"0:",0)==FR_OK);kui_media_set(&media);assert(kui_mount(&fs,log_line));}
static void file_path(char out[KUI_DEST_PATH_CAP],const char *name) {snprintf(out,KUI_DEST_PATH_CAP,"%s/%s",folder,name);}
static void write_file(const char *path,const void *data,UINT n) {
    FIL file;UINT put;assert(f_open(&file,path,FA_WRITE|FA_CREATE_ALWAYS)==FR_OK);
    assert(f_write(&file,data,n,&put)==FR_OK && put==n);assert(f_sync(&file)==FR_OK && f_close(&file)==FR_OK);
}
static size_t load(const char *path,void *data,size_t cap) {
    FIL file;UINT got;assert(f_open(&file,path,FA_READ)==FR_OK && f_size(&file)<=cap);
    size_t n=(size_t)f_size(&file);assert(f_read(&file,data,(UINT)n,&got)==FR_OK && got==n);assert(f_close(&file)==FR_OK);return n;
}
static void mutate(const char *name,size_t offset,uint8_t value) {
    char path[KUI_DEST_PATH_CAP];uint8_t data[32768];file_path(path,name);
    size_t size=load(path,data,sizeof(data));assert(offset<size);data[offset]^=value;write_file(path,data,(UINT)size);
}
static void seed_catalogue(void) {
    if(!strstr(test.fault,"catalogue")) return;
    if(!strcmp(test.fault,"imported-catalogue-bad-sector")) mutate("track01.bin",2076,1);
    if(!strcmp(test.fault,"catalogue-manifest-mismatch")) mutate("track02.raw",0,1);
    assert(f_mkdir("0:/KUI")==FR_OK);
    char db[16384];size_t used=(size_t)snprintf(db,sizeof(db),"DREAMSHELL_REDUMP_CRC_V1\n");
    if(!strcmp(test.fault,"imported-catalogue-cancel"))
        for(unsigned i=0;i<600;++i) {memcpy(db+used,"# test\n",7);used+=7;}
    used+=(size_t)snprintf(db+used,sizeof(db)-used,"G\t3\tSynthetic independent reference\n");
    for(unsigned i=0;i<3;++i) {
        char path[KUI_DEST_PATH_CAP];uint8_t data[32768];file_path(path,files[4+i]);
        size_t bytes=load(path,data,sizeof(data));uint32_t crc=kui_crc32(0,data,bytes);
        if((!strcmp(test.fault,"imported-catalogue-audio-mismatch") && i==1) ||
           (!strcmp(test.fault,"imported-catalogue-partial") && i==2) ||
           !strcmp(test.fault,"imported-catalogue-unrelated")) crc^=1;
        if(!strcmp(test.fault,"imported-catalogue-size-mismatch") && i==1) bytes+=KUI_RAW_BYTES;
        used+=(size_t)snprintf(db+used,sizeof(db)-used,"T\t%u\t%zu\t%08x\n",i+1,bytes,(unsigned)crc);
    }
    memcpy(db+used,"E\n",2);used+=2;
    if(!strcmp(test.fault,"imported-catalogue-invalid")) db[0]='X';
    assert(used<sizeof(db));write_file("0:/KUI/tosec.db",db,(UINT)used);
}
static void seed(const char *host) {
    assert(f_mkdir("0:/Games")==FR_OK);assert(f_mkdir(folder)==FR_OK);
    for(unsigned i=0;i<sizeof(files)/sizeof(files[0]);++i) {
        char path[1024],dest[KUI_DEST_PATH_CAP];uint8_t data[32768];
        snprintf(path,sizeof(path),"%s/%s",host,files[i]);FILE *file=fopen(path,"rb");assert(file);
        size_t size=fread(data,1,sizeof(data),file);assert(size<sizeof(data) && !ferror(file) && !fclose(file));
        file_path(dest,files[i]);write_file(dest,data,(UINT)size);
    }
    if(!strcmp(test.fault,"unsupported")) mutate("track01.bin",15,3); /* Mode 1 -> Mode 2. */
    if(!strcmp(test.fault,"bad-manifest")) mutate("manifest.json",0,1);
    if(!strcmp(test.fault,"one-checkpoint")) mutate("checkpoint-b.bin",0,1);
    if(!strcmp(test.fault,"both-checkpoints")) {mutate("checkpoint-a.bin",0,1);mutate("checkpoint-b.bin",0,1);}
    if(!strcmp(test.fault,"conflict")) {
        char path[KUI_DEST_PATH_CAP];uint8_t record[KUI_CHECKPOINT_BYTES];file_path(path,"checkpoint-b.bin");assert(load(path,record,sizeof(record))==sizeof(record));
        record[16]=1;record[60]=1;uint32_t crc=kui_crc32(0,record,4092);
        for(unsigned i=0;i<4;++i) record[4092+i]=(uint8_t)(crc>>(8*i));
        write_file(path,record,sizeof(record));
    }
    if(!strcmp(test.fault,"gdi-trailing")) {
        char path[KUI_DEST_PATH_CAP];uint8_t data[1024];file_path(path,"disc.gdi");size_t n=load(path,data,sizeof(data));data[n++]='x';write_file(path,data,(UINT)n);
    }
    if(!strcmp(test.fault,"truncated")) {
        char path[KUI_DEST_PATH_CAP];uint8_t data[32768];file_path(path,"track03.bin");size_t n=load(path,data,sizeof(data));write_file(path,data,(UINT)n-1);
    }
    if(!strncmp(test.fault,"imported",8) || !strcmp(test.fault,"no-metadata") || !strcmp(test.fault,"multiple-gdi")) {
        char path[KUI_DEST_PATH_CAP];file_path(path,"manifest.json");assert(f_unlink(path)==FR_OK);
    }
    if(!strcmp(test.fault,"no-checkpoints")) {
        char path[KUI_DEST_PATH_CAP];file_path(path,"checkpoint-a.bin");assert(f_unlink(path)==FR_OK);
        file_path(path,"checkpoint-b.bin");assert(f_unlink(path)==FR_OK);
    }
    if(!strcmp(test.fault,"named") || !strcmp(test.fault,"named-sha")) {
        char path[KUI_DEST_PATH_CAP],new_path[KUI_DEST_PATH_CAP],data[32768];file_path(path,"manifest.json");
        size_t n=load(path,data,sizeof(data));assert(n+80<sizeof(data));
        const char *addition="\"gdi_file\":\"A Named Game.gdi\",";
        memmove(data+1+strlen(addition),data+1,n-1);memcpy(data+1,addition,strlen(addition));
        write_file(path,data,(UINT)(n+strlen(addition)));
        file_path(path,"disc.gdi");file_path(new_path,"A Named Game.gdi");assert(f_rename(path,new_path)==FR_OK);files[1]="A Named Game.gdi";
    }
    if(!strcmp(test.fault,"no-metadata") || !strcmp(test.fault,"missing-gdi")) {
        char path[KUI_DEST_PATH_CAP];file_path(path,"disc.gdi");assert(f_unlink(path)==FR_OK);
    }
    if(!strcmp(test.fault,"multiple-gdi")) {
        char path[KUI_DEST_PATH_CAP];file_path(path,"extra.gdi");write_file(path,"1\n",2);
    }
    if(!strcmp(test.fault,"imported-overlap")) {
        char path[KUI_DEST_PATH_CAP];file_path(path,"disc.gdi");
        const char *gdi="3\n1 0 4 2352 track01.bin 0\n2 4 0 2352 track02.raw 0\n3 45000 4 2352 track03.bin 0\n";
        write_file(path,gdi,(UINT)strlen(gdi));
    }
    if(!strcmp(test.fault,"imported-quotes")) {
        char path[KUI_DEST_PATH_CAP],new_path[KUI_DEST_PATH_CAP];file_path(path,"track02.raw");file_path(new_path,"Audio Track.raw");
        assert(f_rename(path,new_path)==FR_OK);files[5]="Audio Track.raw";file_path(path,"disc.gdi");
        const char *gdi="3\r\n1 0 4 2352 track01.bin 0\r\n2 155 0 2352 \"Audio Track.raw\" 0\r\n3 45000 4 2352 track03.bin 0\r\n";
        write_file(path,gdi,(UINT)strlen(gdi));
    }
    seed_catalogue();
    for(unsigned i=0;i<3;++i) {
        char path[KUI_DEST_PATH_CAP];file_path(path,files[4+i]);FIL file;assert(f_open(&file,path,FA_READ)==FR_OK);
        test.track_clusters[i]=file.obj.sclust;
        if(!i) test.first_track=(uint32_t)(fs.database+(LBA_t)(file.obj.sclust-2)*fs.csize);
        assert(f_close(&file)==FR_OK);
    }
}
static void originals(uint32_t hashes[7],size_t sizes[7]) {
    for(unsigned i=0;i<7;++i) {
        char path[KUI_DEST_PATH_CAP];uint8_t data[32768];file_path(path,files[i]);FILINFO info;FRESULT r=f_stat(path,&info);
        if(r==FR_NO_FILE) {sizes[i]=SIZE_MAX;hashes[i]=0;continue;}
        assert(r==FR_OK);sizes[i]=load(path,data,sizeof(data));hashes[i]=kui_crc32(0,data,sizes[i]);
    }
}
int main(int argc,char **argv) {
    if(argc!=4) return 2;
    struct stat st;if(lstat(argv[1],&st) || !S_ISREG(st.st_mode) || st.st_size%512) return 2;
    test.image=fopen(argv[1],"r+b");assert(test.image);test.blocks=(uint64_t)st.st_size/512;test.fault=argv[3];
    char long_folder[130];
    if(!strcmp(test.fault,"long-folder")) {
        memcpy(long_folder,"0:/Games/",9);memset(long_folder+9,'a',120);long_folder[129]=0;folder=long_folder;
    }
    kui_media_set(&media);assert(kui_mount(&fs,log_line));seed(argv[2]);
    uint32_t before[7],after[7];size_t before_size[7],after_size[7];originals(before,before_size);
    assert(f_mount(NULL,"0:",0)==FR_OK);unsigned writes=test.writes;
    struct kui_scan_status status;test.status=&status;test.scanning=true;
    const struct kui_scan_ops ops={NULL,cancel,now_ms,progress,log_line};
    enum kui_scan_result result=kui_recovery_scan(!strcmp(test.fault,"parent")?"0:/Games":folder,&ops,&status);
    test.scanning=false;test.cancelled=false;
    bool good=!strcmp(test.fault,"clean") || !strcmp(test.fault,"one-checkpoint") || !strcmp(test.fault,"repeat") || !strcmp(test.fault,"sha") ||
        !strcmp(test.fault,"no-checkpoints") || !strcmp(test.fault,"named") || !strcmp(test.fault,"named-sha") || !strcmp(test.fault,"long-folder") ||
        !strcmp(test.fault,"catalogue-full") || !strcmp(test.fault,"imported-catalogue-full");
    bool structural=!strcmp(test.fault,"imported") || !strcmp(test.fault,"imported-quotes") ||
        !strcmp(test.fault,"imported-catalogue-audio-mismatch") || !strcmp(test.fault,"imported-catalogue-size-mismatch") ||
        !strcmp(test.fault,"imported-catalogue-partial") || !strcmp(test.fault,"imported-catalogue-unrelated") ||
        !strcmp(test.fault,"imported-catalogue-invalid");
    bool issues=!strcmp(test.fault,"damaged") || !strcmp(test.fault,"unsupported") || !strcmp(test.fault,"imported-damaged") ||
        !strcmp(test.fault,"imported-catalogue-bad-sector") || !strcmp(test.fault,"catalogue-manifest-mismatch");
    bool stopped=!strcmp(test.fault,"cancel-before") || !strcmp(test.fault,"cancel-scan") || !strcmp(test.fault,"imported-catalogue-cancel");
    assert(result==(good?KUI_SCAN_CLEAN:structural?KUI_SCAN_STRUCTURAL:issues?KUI_SCAN_ISSUES:stopped?KUI_SCAN_STOPPED:KUI_SCAN_FAILED));
    assert(status.complete==(good || structural || issues));
    if(good || issues || structural) {
        assert(status.done==14u*KUI_RAW_BYTES && status.total==status.done);
        assert(status.data_sectors==10 && status.audio_sectors==4);
        assert(strstr(status.report,".txt"));
        assert(test.track_read_bytes==14u*KUI_RAW_BYTES); /* Catalogue comparison never rereads source tracks. */
        assert(status.catalogue_checked);
    }
    if(structural || !strcmp(test.fault,"imported-damaged")) assert(!status.reference_hashes && !status.checkpoint_checked && !status.crc_mismatches);
    if(!strcmp(test.fault,"no-checkpoints")) assert(status.reference_hashes && !status.checkpoint_checked);
    if(!strcmp(test.fault,"missing-gdi")) assert(strstr(status.message,"disc.gdi"));
    if(!strcmp(test.fault,"parent") || !strcmp(test.fault,"no-metadata")) assert(strstr(status.message,"actual game folder"));
    if(!strcmp(test.fault,"imported-damaged")) assert(status.bad_sectors==2);
    if(!strcmp(test.fault,"damaged")) assert(status.bad_sectors==2 && !status.unsupported_sectors && status.crc_mismatches==3);
    if(!strcmp(test.fault,"unsupported")) assert(!status.bad_sectors && status.unsupported_sectors==1 && status.crc_mismatches==1);
    if(strstr(test.fault,"catalogue")) {
        enum kui_known_result known=KUI_KNOWN_FULL_MATCH;
        if(strstr(test.fault,"audio-mismatch") || strstr(test.fault,"size-mismatch")) known=KUI_KNOWN_DATA_MATCH;
        else if(strstr(test.fault,"partial")) known=KUI_KNOWN_PARTIAL;
        else if(strstr(test.fault,"unrelated")) known=KUI_KNOWN_NO_MATCH;
        else if(strstr(test.fault,"invalid")) known=KUI_KNOWN_ERROR;
        else if(strstr(test.fault,"cancel")) known=KUI_KNOWN_CANCELLED;
        assert(status.catalogue.result==known);
        if(known==KUI_KNOWN_FULL_MATCH) assert(!strcmp(status.catalogue.catalog,"TOSEC"));
        if(!strcmp(test.fault,"imported-catalogue-full"))
            assert(!status.reference_hashes && strstr(status.message,"including audio"));
        if(!strcmp(test.fault,"imported-catalogue-bad-sector")) assert(status.bad_sectors==1);
        if(!strcmp(test.fault,"catalogue-manifest-mismatch")) assert(status.crc_mismatches==1);
    }
    if(!strcmp(test.fault,"cancel-before")) assert(test.writes==writes && !status.report[0]);
    if(strstr(test.fault,"fail")) assert(test.injected);
    remount();originals(after,after_size);assert(!memcmp(before,after,sizeof(before)) && !memcmp(before_size,after_size,sizeof(before_size)));
    if(status.report[0]) {
        FILINFO info;assert(f_stat(status.report,&info)==FR_OK);
        if(!status.complete) assert(strstr(status.report,".part"));
        if(structural || !strcmp(test.fault,"imported-damaged")) {
            uint8_t data[32768];size_t size=load(status.report,data,sizeof(data));
            assert(contains(data,size,"NO manifest hash") && contains(data,size,"Audio: NO manifest hash"));
            assert(!contains(data,size,"COMPLETE CLEAN"));
        }
    }
    if(!strcmp(test.fault,"repeat")) {
        char first[KUI_DEST_PATH_CAP];snprintf(first,sizeof(first),"%s",status.report);uint8_t data[32768];size_t bytes=load(first,data,sizeof(data));uint32_t crc=kui_crc32(0,data,bytes);
        assert(f_mount(NULL,"0:",0)==FR_OK);test.scanning=true;
        assert(kui_recovery_scan(folder,&ops,&status)==KUI_SCAN_CLEAN);test.scanning=false;
        assert(strcmp(first,status.report));remount();assert(load(first,data,sizeof(data))==bytes && kui_crc32(0,data,bytes)==crc);
    }
    assert(f_mount(NULL,"0:",0)==FR_OK && !fclose(test.image));
    printf("PASS Advanced CRC %s; original files unchanged\n",test.fault);return 0;
}
