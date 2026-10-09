/* SPDX-License-Identifier: GPL-3.0-only */
#include "cdda_preflight_storage.h"
#include "ff.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FILE_MAX 300u
struct entry {char path[512];uint32_t bytes;const uint8_t *data;int directory;unsigned mode;};
static struct entry nodes[FILE_MAX];
static unsigned count,open_calls,read_calls,stat_calls,seek_calls,close_calls,checks;
static unsigned fail_read,short_read,fail_scan,cancel_after,cancel_checks,empty_entries,geometry_mode;
static FATFS test_fs;
static const uint8_t original[451]=
    "15\n"
    "1 0 4 2352 track01.bin 0\n"
    "2 7257 0 2352 track02.raw 0\n"
    "3 45000 4 2352 track03.bin 0\n"
    "4 201900 0 2352 track04.raw 0\n"
    "5 219712 0 2352 track05.raw 0\n"
    "6 234091 0 2352 track06.raw 0\n"
    "7 255599 0 2352 track07.raw 0\n"
    "8 272017 0 2352 track08.raw 0\n"
    "9 282799 0 2352 track09.raw 0\n"
    "10 303092 0 2352 track10.raw 0\n"
    "11 323352 0 2352 track11.raw 0\n"
    "12 341314 0 2352 track12.raw 0\n"
    "13 356690 0 2352 track13.raw 0\n"
    "14 374201 0 2352 track14.raw 0\n"
    "15 377422 4 2352 track15.bin 0\n"
;
static const char *dir_paths[150];
static unsigned dir_count;
static void require(int condition,int line){checks++;if(!condition){fprintf(stderr,"FAIL line %d, %s\n",line,cdda_preflight_storage_failure());exit(2);}}
#define CHECK(x) require(!!(x),__LINE__)
static int equal(const char *a,const char *b){while(*a && (*a|32)==(*b|32)){a++;b++;}return !*a&&!*b;}
static unsigned lookup(const char *path){for(unsigned n=0;n<count;n++)if(equal(nodes[n].path,path))return n+1u;return 0;}
static void add(const char *path,uint32_t bytes,const uint8_t *data,int dir){CHECK(count<FILE_MAX);snprintf(nodes[count].path,sizeof(nodes[count].path),"%s",path);nodes[count].bytes=bytes;nodes[count].data=data;nodes[count].directory=dir;count++;}
static void complete(const char *base,const char *gdi){char path[512];snprintf(path,sizeof(path),"%s/%s",base,gdi);add(path,451,original,0);for(unsigned n=1;n<=15;n++){snprintf(path,sizeof(path),"%s/track%02u.%s",base,n,(n==1||n==3||n==15)?"bin":"raw");add(path,2352,NULL,0);}}
static void reset(void){cdda_preflight_storage_close();cdda_preflight_storage_cancel(NULL,NULL);memset(nodes,0,sizeof(nodes));count=0;open_calls=read_calls=stat_calls=seek_calls=close_calls=0;fail_read=short_read=fail_scan=cancel_after=cancel_checks=empty_entries=geometry_mode=0;memset(&test_fs,0,sizeof(test_fs));test_fs.csize=1;test_fs.database=2048;test_fs.n_fatent=100000;dir_count=0;}
int cdda_storage_init(void){return 0;}
const char *cdda_storage_last_failure(void){return "underlying storage failure";}
int cdda_storage_geometry(uint64_t *card,uint32_t *start,uint32_t *sectors){*card=1000000;*start=2048;*sectors=geometry_mode==1?2058:500000;return geometry_mode==2?-1:0;}
FRESULT f_open(FIL *file,const TCHAR *path,BYTE mode){open_calls++;CHECK(mode==FA_READ);unsigned id=lookup(path);if(!id||nodes[id-1].directory)return FR_NO_FILE;memset(file,0,sizeof(*file));file->obj.fs=&test_fs;file->obj.id=(WORD)id;file->obj.objsize=nodes[id-1].bytes;file->obj.sclust=10;return FR_OK;}
FRESULT f_close(FIL *file){close_calls++;file->obj.fs=NULL;return FR_OK;}
FRESULT f_read(FIL *file,void *out,UINT bytes,UINT *got){read_calls++;if(fail_read&&read_calls>=fail_read)return FR_DISK_ERR;struct entry *e=&nodes[file->obj.id-1];if(bytes>e->bytes-file->fptr)bytes=(UINT)(e->bytes-file->fptr);if(short_read&&read_calls>=short_read&&bytes)bytes--;if(e->data)memcpy(out,e->data+file->fptr,bytes);else for(unsigned n=0;n<bytes;n++)((uint8_t *)out)[n]=(uint8_t)(file->fptr+n);file->fptr+=bytes;*got=bytes;return FR_OK;}
FRESULT f_stat(const TCHAR *path,FILINFO *info){stat_calls++;unsigned id=lookup(path);if(!id)return FR_NO_FILE;memset(info,0,sizeof(*info));info->fsize=nodes[id-1].bytes;info->fattrib=nodes[id-1].directory?AM_DIR:0;return FR_OK;}
FRESULT f_lseek(FIL *file,FSIZE_t offset){seek_calls++;if(offset==CREATE_LINKMAP){CHECK(0);return FR_INT_ERR;}if(offset>file->obj.objsize)offset=file->obj.objsize;file->fptr=offset;if(!offset)return FR_OK;uint32_t cluster=(uint32_t)((offset-1)/(512u*test_fs.csize));struct entry *e=&nodes[file->obj.id-1];if(file->cltbl){DWORD *map=file->cltbl+1;while(*map&&cluster>=*map){cluster-=map[0];map+=2;}if(!*map)return FR_INT_ERR;file->clust=map[1]+cluster;}else if(e->mode==1){file->clust=cluster<2?10+cluster:40+cluster-2;}else if(e->mode==2){file->clust=cluster==3?10:10+cluster;}else if(e->mode==3){file->clust=10+cluster*2;}else if(e->mode==4){file->clust=test_fs.n_fatent;}else file->clust=10+cluster;return FR_OK;}
FRESULT f_opendir(DIR *dir,const TCHAR *path){if(dir_count==150)return FR_INT_ERR;memset(dir,0,sizeof(*dir));dir_paths[dir_count]=strdup(path);dir->obj.sclust=++dir_count;return FR_OK;}
FRESULT f_closedir(DIR *dir){unsigned id=dir->obj.sclust;free((void *)dir_paths[id-1]);dir_paths[id-1]=NULL;return FR_OK;}
FRESULT f_readdir(DIR *dir,FILINFO *info){if(fail_scan)return FR_DISK_ERR;memset(info,0,sizeof(*info));const char *base=dir_paths[dir->obj.sclust-1];size_t len=strlen(base);while(dir->dptr<count){struct entry *e=&nodes[dir->dptr++];if(strncmp(base,e->path,len))continue;const char *name=e->path+len;if(len&&base[len-1]!='/'){if(*name!='/')continue;name++;}if(!*name||strchr(name,'/'))continue;snprintf(info->fname,sizeof(info->fname),"%s",name);info->fattrib=e->directory?AM_DIR:0;info->fsize=e->bytes;return FR_OK;}if(empty_entries){empty_entries--;snprintf(info->fname,sizeof(info->fname),"dummy%u.bin",empty_entries);return FR_OK;}return FR_OK;}
static bool cancel(void *context){(void)context;return ++cancel_checks>=cancel_after;}
static void ordinary(void){add("0:/games",0,NULL,1);add("0:/games/Toy",0,NULL,1);complete("0:/games/Toy","TOY_COMMANDER.gdi");}
int main(void){CHECK(sizeof(original)==451);
 reset();ordinary();CHECK(!cdda_preflight_storage_discover());CHECK(equal(cdda_preflight_storage_path(),"0:/games/Toy/TOY_COMMANDER.gdi"));CHECK(cdda_preflight_storage_scan()->candidates==1);CHECK(cdda_preflight_storage_scan()->directories==3);uint8_t descriptor[451];size_t bytes=0;CHECK(!cdda_preflight_storage_descriptor(descriptor,sizeof(descriptor),&bytes));CHECK(bytes==451&&!memcmp(descriptor,original,451));const struct kui_game_file_ops *ops=cdda_preflight_storage_files();uint64_t file_bytes=0;CHECK(ops->stat(NULL,"track03.bin",&file_bytes)==KUI_GAME_OK&&file_bytes==2352);uint8_t out[1024];unsigned opens=open_calls;CHECK(ops->read(NULL,"track03.bin",0,out,512)==KUI_GAME_OK);CHECK(ops->read(NULL,"track03.bin",512,out+512,512)==KUI_GAME_OK);CHECK(open_calls==opens+1);for(unsigned n=0;n<1024;n++)CHECK(out[n]==(uint8_t)n);CHECK(ops->read(NULL,"../track03.bin",0,out,1)==KUI_GAME_INVALID);CHECK(ops->read(NULL,"track03.bin",2351,out,2)==KUI_GAME_RANGE);
 struct cdda_preflight_storage_extents ext;CHECK(!cdda_preflight_storage_extents("track03.bin",&ext));CHECK(ext.clusters==5&&ext.extents==1&&ext.rounded_bytes==2560&&ext.map_words==4);struct cdda_preflight_storage_extent_run run;unsigned seeks=seek_calls,reads=read_calls;CHECK(!cdda_preflight_storage_extent_run(0,&run));CHECK(run.first_cluster==10&&run.clusters==5&&run.first_volume_lba==2056&&run.file_sector==0&&run.sectors==5);CHECK(seek_calls==seeks&&read_calls==reads);CHECK(cdda_preflight_storage_extent_run(1,&run));CHECK(ops->read(NULL,"track04.raw",0,out,1)==KUI_GAME_OK);CHECK(cdda_preflight_storage_extent_run(0,&run));
 reset();ordinary();nodes[lookup("0:/games/Toy/track03.bin")-1].mode=1;CHECK(!cdda_preflight_storage_discover());CHECK(!cdda_preflight_storage_extents("track03.bin",&ext));CHECK(ext.extents==2);CHECK(!cdda_preflight_storage_extent_run(1,&run)&&run.first_cluster==40&&run.clusters==3&&run.file_sector==2);
 reset();ordinary();nodes[lookup("0:/games/Toy/track03.bin")-1].mode=2;CHECK(!cdda_preflight_storage_discover());memset(&ext,0xa5,sizeof(ext));struct cdda_preflight_storage_extents saved=ext;CHECK(cdda_preflight_storage_extents("track03.bin",&ext));CHECK(!memcmp(&ext,&saved,sizeof(ext)));
 reset();ordinary();struct entry *fragmented=&nodes[lookup("0:/games/Toy/track03.bin")-1];fragmented->mode=3;fragmented->bytes=2352*40;CHECK(!cdda_preflight_storage_discover());CHECK(cdda_preflight_storage_extents("track03.bin",&ext));CHECK(strstr(cdda_preflight_storage_failure(),"160"));
 reset();ordinary();nodes[lookup("0:/games/Toy/track03.bin")-1].mode=4;CHECK(!cdda_preflight_storage_discover());CHECK(cdda_preflight_storage_extents("track03.bin",&ext));
 reset();ordinary();add("0:/games/Copy",0,NULL,1);complete("0:/games/Copy","copy.gdi");CHECK(cdda_preflight_storage_discover());CHECK(strstr(cdda_preflight_storage_failure(),"Multiple"));
 reset();ordinary();nodes[lookup("0:/games/Toy/track15.bin")-1].bytes=0;CHECK(cdda_preflight_storage_discover());CHECK(strstr(cdda_preflight_storage_failure(),"incomplete"));
 reset();ordinary();add("0:/games/Copy",0,NULL,1);complete("0:/games/Copy","copy.gdi");nodes[lookup("0:/games/Copy/track15.bin")-1].bytes=0;CHECK(!cdda_preflight_storage_discover());CHECK(cdda_preflight_storage_scan()->incomplete==1);
 reset();ordinary();static const uint8_t cfg[]="0:/games/Toy/TOY_COMMANDER.gdi\r\n";add("0:/KUI/tests/cdda/preflight.cfg",sizeof(cfg)-1,cfg,0);CHECK(!cdda_preflight_storage_discover());CHECK(cdda_preflight_storage_scan()->configured&&cdda_preflight_storage_scan()->directories==0);
 reset();ordinary();static const uint8_t badcfg[]="0:/games/../Toy/TOY_COMMANDER.gdi\n";add("0:/KUI/tests/cdda/preflight.cfg",sizeof(badcfg)-1,badcfg,0);CHECK(cdda_preflight_storage_discover());
 reset();ordinary();static const uint8_t fixturecfg[]="0:/KUI/tests/cdda/TOY_COMMANDER.gdi\n";add("0:/KUI/tests/cdda/preflight.cfg",sizeof(fixturecfg)-1,fixturecfg,0);CHECK(cdda_preflight_storage_discover());
 reset();ordinary();add("0:/KUI",0,NULL,1);add("0:/KUI/tests",0,NULL,1);add("0:/KUI/tests/cdda",0,NULL,1);complete("0:/KUI/tests/cdda","TOY_COMMANDER.gdi");CHECK(!cdda_preflight_storage_discover());CHECK(cdda_preflight_storage_scan()->candidates==1);
 reset();ordinary();fail_scan=1;CHECK(cdda_preflight_storage_discover());
 reset();ordinary();short_read=1;CHECK(cdda_preflight_storage_discover());
 reset();ordinary();empty_entries=5000;CHECK(cdda_preflight_storage_discover());CHECK(strstr(cdda_preflight_storage_failure(),"entry limit"));
 reset();ordinary();cancel_after=3;cdda_preflight_storage_cancel(cancel,NULL);CHECK(cdda_preflight_storage_discover());CHECK(strstr(cdda_preflight_storage_failure(),"cancel"));
 reset();ordinary();CHECK(!cdda_preflight_storage_discover());cancel_after=2;cdda_preflight_storage_cancel(cancel,NULL);CHECK(cdda_preflight_storage_extents("track03.bin",&ext));

 reset();ordinary();CHECK(!cdda_preflight_storage_discover());geometry_mode=1;CHECK(cdda_preflight_storage_extents("track03.bin",&ext));CHECK(strstr(cdda_preflight_storage_failure(),"coverage"));
 reset();ordinary();CHECK(!cdda_preflight_storage_discover());geometry_mode=2;CHECK(cdda_preflight_storage_extents("track03.bin",&ext));CHECK(strstr(cdda_preflight_storage_failure(),"geometry"));
 reset();ordinary();CHECK(!cdda_preflight_storage_discover());fail_read=read_calls+1;CHECK(ops->read(NULL,"track03.bin",0,out,512)==KUI_GAME_IO);
 reset();ordinary();CHECK(!cdda_preflight_storage_discover());short_read=read_calls+1;CHECK(ops->read(NULL,"track03.bin",0,out,512)==KUI_GAME_IO);
 reset();ordinary();CHECK(!cdda_preflight_storage_discover());CHECK(ops->read(NULL,"track03.bin",2352,NULL,0)==KUI_GAME_OK);CHECK(ops->read(NULL,"track03.bin",2353,NULL,0)==KUI_GAME_RANGE);
 reset();ordinary();char deep[512]="0:";for(unsigned n=0;n<9;n++){strcat(deep,"/level");add(deep,0,NULL,1);}CHECK(cdda_preflight_storage_discover());CHECK(strstr(cdda_preflight_storage_failure(),"scan limit"));
 reset();ordinary();for(unsigned n=0;n<130;n++){char path[32];snprintf(path,sizeof(path),"0:/folder%u",n);add(path,0,NULL,1);}CHECK(cdda_preflight_storage_discover());CHECK(strstr(cdda_preflight_storage_failure(),"scan limit"));
 reset();ordinary();static const uint8_t nulcfg[]="0:/games/Toy/TOY_COMMANDER.gdi\0other";add("0:/KUI/tests/cdda/preflight.cfg",sizeof(nulcfg)-1,nulcfg,0);CHECK(cdda_preflight_storage_discover());
 reset();ordinary();static const uint8_t multicfg[]="0:/games/Toy/TOY_COMMANDER.gdi\nother\n";add("0:/KUI/tests/cdda/preflight.cfg",sizeof(multicfg)-1,multicfg,0);CHECK(cdda_preflight_storage_discover());
 reset();ordinary();nodes[lookup("0:/games/Toy/TOY_COMMANDER.gdi")-1].bytes=450;CHECK(cdda_preflight_storage_discover());
 reset();ordinary();uint8_t changed[451];memcpy(changed,original,sizeof(changed));changed[10]^=1;nodes[lookup("0:/games/Toy/TOY_COMMANDER.gdi")-1].data=changed;CHECK(cdda_preflight_storage_discover());
 reset();ordinary();CHECK(!cdda_preflight_storage_discover());bytes=777;memset(descriptor,0x5a,sizeof(descriptor));CHECK(cdda_preflight_storage_descriptor(descriptor,450,&bytes));CHECK(bytes==777&&descriptor[0]==0x5a);
 cdda_preflight_storage_close();printf("preflight storage host checks: %u\n",checks);return 0;}
