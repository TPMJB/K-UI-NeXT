/* SPDX-License-Identifier: GPL-3.0-only */
/* Independent, read-only admission of TPMJB's legacy GD Ripper state v1.
 * The legacy formats are evidence, never new engine checkpoints. */
#include "kui/legacy_import.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct text { const char *p,*end; };
static bool number(struct text *t,uint64_t max,unsigned base,uint64_t *out) {
    uint64_t n=0;unsigned digits=0;
    while(t->p<t->end) {
        unsigned c=(unsigned char)*t->p,v;
        if(c>='0' && c<='9')v=c-'0';
        else if(base==16 && c>='a' && c<='f')v=c-'a'+10;
        else if(base==16 && c>='A' && c<='F')v=c-'A'+10;
        else break;
        if(v>=base || v>max || n>(max-v)/base)return false;
        n=n*base+v;++t->p;++digits;
    }
    if(!digits)return false;
    *out=n;return true;
}
static bool literal(struct text *t,const char *s) {
    size_t n=strlen(s);if((size_t)(t->end-t->p)<n || memcmp(t->p,s,n))return false;
    t->p+=n;return true;
}
static bool newline(struct text *t) {
    if(t->p<t->end && *t->p=='\r')++t->p;
    return literal(t,"\n");
}
static bool field(struct text *t,uint64_t max,unsigned base,uint64_t *out) {
    return number(t,max,base,out) && literal(t," ");
}
static bool prepared_valid(const struct kui_capture_plan *p) {
    if(!p || !p->count || p->count>99 || (p->profile!=KUI_CAPTURE_PROFILE_GD && p->profile!=KUI_CAPTURE_PROFILE_CD_TOC))return false;
    uint64_t total=0;
    for(unsigned i=0;i<p->count;++i) {
        const struct kui_capture_track *t=&p->tracks[i];
        if(t->number!=i+1 || (t->control!=0 && t->control!=4) || t->start<150 || t->end<=t->start ||
           t->end>KUI_RECOVERY_FAD_MAX+1u || t->toc_end<t->end || t->toc_end>KUI_RECOVERY_FAD_MAX+1u ||
           (i && t->start<p->tracks[i-1].end) || t->session>1 ||
           (p->profile==KUI_CAPTURE_PROFILE_CD_TOC && t->session))return false;
        total+=(uint64_t)(t->end-t->start)*KUI_RAW_BYTES;
    }
    return total==p->bytes;
}
static bool parse_state(const void *data,size_t bytes,const struct kui_capture_plan *plan,
    struct kui_legacy_job *out) {
    if(!data || !bytes || bytes>KUI_LEGACY_STATE_LIMIT || !prepared_valid(plan) || memchr(data,0,bytes))return false;
    struct text t={(const char *)data,(const char *)data+bytes};uint64_t disc,use_bin,count;
    if(!literal(&t,"DreamShell GD Ripper state v1") || !newline(&t) ||
       !literal(&t,"disc_type ") || !number(&t,255,10,&disc) || !newline(&t) ||
       !literal(&t,"use_bin ") || !number(&t,1,10,&use_bin) || !newline(&t) || !use_bin ||
       !literal(&t,"tracks ") || !number(&t,99,10,&count) || !newline(&t) || count!=plan->count ||
       (plan->profile==KUI_CAPTURE_PROFILE_GD?disc!=128:(disc!=0 && disc!=16 && disc!=32 && disc!=48)))return false;
    memset(out,0,sizeof(*out));out->source.plan=*plan;
    for(unsigned i=0;i<plan->count;++i) {
        uint64_t track,start,sectors,control,size;const struct kui_capture_track *p=&plan->tracks[i];
        if(!field(&t,99,10,&track) || !field(&t,KUI_RECOVERY_FAD_MAX,10,&start) ||
           !field(&t,KUI_RECOVERY_FAD_MAX,10,&sectors) || !field(&t,4,10,&control) ||
           !field(&t,KUI_RAW_BYTES,10,&size) || track!=p->number || start!=p->start ||
           control!=p->control || size!=KUI_RAW_BYTES || !sectors)return false;
        uint32_t expected=p->end-p->start;
        if(sectors!=expected && !(plan->profile==KUI_CAPTURE_PROFILE_CD_TOC && control==4 && expected>2 && sectors==expected-2))return false;
        char name[32];snprintf(name,sizeof(name),"track%02u.%s",i+1,p->control==4?"bin":"raw");
        if(!literal(&t,name) || !newline(&t))return false;
        memcpy(out->source.files[i],name,strlen(name)+1);out->declared[i]=(uint32_t)sectors;
    }
    return t.p==t.end;
}
bool kui_legacy_state_parse(const void *data,size_t bytes,const struct kui_capture_plan *prepared,
    struct kui_legacy_job *out) {
    if(!out)return false;
    struct kui_legacy_job *tmp=malloc(sizeof(*tmp));if(!tmp)return false;
    bool ok=parse_state(data,bytes,prepared,tmp);if(ok)*out=*tmp;free(tmp);return ok;
}
bool kui_legacy_target_is_marked(const struct kui_legacy_job *j,unsigned track,uint32_t sector) {
    if(!j || track>=j->source.plan.count || sector>=j->declared[track])return false;
    uint32_t fad=j->source.plan.tracks[track].start+sector;
    return (j->target_bits[fad/8]&(1u<<(fad%8)))!=0;
}
struct importer {
    const struct kui_capture_ops *ops;struct kui_legacy_job *job;
    char directory[KUI_DEST_JOB_CAP],path[KUI_DEST_PATH_CAP];
    bool stopped,failed;struct kui_sha256 binding;
    uint8_t saved[16u*KUI_RAW_BYTES],repeat[16u*KUI_RAW_BYTES],optical[KUI_RAW_BYTES];
};
static bool fail(struct importer *s,const char *message) {
    s->failed=true;snprintf(s->job->message,sizeof(s->job->message),"%s",message);return false;
}
static bool stop(struct importer *s) {
    if(s->ops->cancelled && s->ops->cancelled(s->ops->ctx))s->stopped=true;
    return s->stopped;
}
static bool path(struct importer *s,const char *name,const char *suffix) {
    int n=snprintf(s->path,sizeof(s->path),"0:%s/%s%s",s->directory,name,suffix?suffix:"");
    return n>0 && n<(int)sizeof(s->path);
}
static bool exact(struct importer *s,FIL *f,void *data,unsigned bytes) {
    UINT got=0;if(stop(s))return false;
    if(f_read(f,data,bytes,&got)!=FR_OK || got!=bytes)return fail(s,"Legacy source read failed");
    return !stop(s);
}
static bool close_file(struct importer *s,FIL *f,bool ok) {
    if(f_close(f)!=FR_OK)return fail(s,"Legacy source close failed");
    return ok;
}
static void hash32(struct kui_sha256 *h,uint32_t n) {
    uint8_t bytes[4];for(unsigned i=0;i<4;++i)bytes[i]=(uint8_t)(n>>(i*8));kui_sha256_update(h,bytes,4);
}
static uint32_t tag(struct importer *s,unsigned i) {
    char text[96];int n=snprintf(text,sizeof(text),"%u %" PRIu32 " %" PRIu32 " %u",i+1,
        s->job->source.plan.tracks[i].start,s->job->declared[i],KUI_RAW_BYTES);
    return kui_crc32(0,text,(size_t)n);
}
static uint32_t get32(const uint8_t *b) {return (uint32_t)b[0]|(uint32_t)b[1]<<8|(uint32_t)b[2]<<16|(uint32_t)b[3]<<24;}
static void mark(struct importer *s,unsigned i,uint32_t sector) {
    uint32_t fad=s->job->source.plan.tracks[i].start+sector;
    s->job->target_bits[fad/8]|=(uint8_t)(1u<<(fad%8));
}
struct lines { FIL file;uint8_t bytes[512];unsigned at,used;uint64_t remaining; };
static int line(struct importer *s,struct lines *r,char out[384],bool *terminated) {
    unsigned n=0;*terminated=false;
    while(r->remaining || r->at<r->used) {
        if(r->at==r->used) {
            unsigned bytes=(unsigned)(r->remaining>sizeof(r->bytes)?sizeof(r->bytes):r->remaining);
            if(!exact(s,&r->file,r->bytes,bytes))return -1;
            r->remaining-=bytes;r->at=0;r->used=bytes;
        }
        unsigned c=r->bytes[r->at++];if(c=='\n') {*terminated=true;break;}
        if(!c || n+1>=384) {fail(s,"Legacy metadata line is invalid or too long");return -1;}
        out[n++]=(char)c;
    }
    if(n && out[n-1]=='\r')--n;
    out[n]=0;return n || *terminated?1:0;
}
static int open_lines(struct importer *s,unsigned i,const char *suffix,struct lines *r,uint64_t max) {
    memset(r,0,sizeof(*r));if(!path(s,s->job->source.files[i],suffix))return -1;
    FRESULT result=f_open(&r->file,s->path,FA_READ);if(result==FR_NO_FILE)return 0;
    if(result!=FR_OK) {fail(s,"Cannot open legacy sidecar");return -1;}
    if(f_size(&r->file)>max) {close_file(s,&r->file,false);fail(s,"Legacy sidecar is too large");return -1;}
    r->remaining=f_size(&r->file);return 1;
}
static bool checkpoint(struct importer *s,unsigned i) {
    struct lines r;int opened=open_lines(s,i,".crc",&r,(uint64_t)s->job->declared[i]*160u+160u);if(opened<=0)return opened==0;
    char buf[384];bool terminated;int result;uint64_t previous=0;bool found=false;
    while((result=line(s,&r,buf,&terminated))>0) {
        if(!buf[0])continue;
        if(!terminated)break; /* A torn final record did not publish progress. */
        struct text t={buf,buf+strlen(buf)};uint64_t saved_tag,bytes,crc,check;
        bool ok=literal(&t,"CRC1 ") && field(&t,UINT32_MAX,16,&saved_tag) &&
            field(&t,UINT64_MAX,10,&bytes) && number(&t,UINT32_MAX,16,&crc);
        const char *body=t.p;
        ok=ok && literal(&t," ") && number(&t,UINT32_MAX,16,&check) && t.p==t.end &&
            saved_tag==tag(s,i) && bytes%KUI_RAW_BYTES==0 && bytes<=(uint64_t)s->job->available[i]*KUI_RAW_BYTES &&
            bytes>=previous && check==kui_crc32(0,buf,(size_t)(body-buf));
        if(!ok) {result=-1;fail(s,"Invalid legacy CRC1 checkpoint");break;}
        previous=bytes;found=true;s->job->checkpoint_sectors[i]=(uint32_t)(bytes/KUI_RAW_BYTES);s->job->checkpoint_crc[i]=(uint32_t)crc;
        kui_sha256_update(&s->binding,buf,strlen(buf));
    }
    if(result<0)return close_file(s,&r.file,false);
    s->job->checkpoint_valid[i]=found;
    return close_file(s,&r.file,true);
}
static bool target_map(struct importer *s,unsigned i,bool bad) {
    struct lines r;int opened=open_lines(s,i,bad?".bad":".suspect",&r,(uint64_t)s->job->declared[i]*384u+384u);if(opened<=0)return opened==0;
    char buf[384];bool terminated;int result;bool header=!bad;
    while((result=line(s,&r,buf,&terminated))>0) {
        if(!terminated) {result=-1;fail(s,"Truncated legacy target map");break;}
        if(!header) {header=true;if(strcmp(buf,"track,track_sector,disc_lba,disc_fad,file_offset")) {result=-1;break;}continue;}
        struct text t={buf,buf+strlen(buf)};uint64_t track=i+1,sector,fad,lba=0,offset=0,flags=0;bool ok;
        if(bad)ok=number(&t,99,10,&track) && literal(&t,",") && number(&t,UINT32_MAX,10,&sector) && literal(&t,",") &&
            number(&t,KUI_RECOVERY_FAD_MAX,10,&lba) && literal(&t,",") && number(&t,KUI_RECOVERY_FAD_MAX,10,&fad) && literal(&t,",") && number(&t,UINT64_MAX,10,&offset);
        else ok=field(&t,UINT32_MAX,10,&sector) && field(&t,KUI_RECOVERY_FAD_MAX,10,&fad) && number(&t,31,10,&flags);
        ok=ok && t.p==t.end && track==i+1 && sector<s->job->declared[i] && fad==s->job->source.plan.tracks[i].start+sector &&
            (bad?(lba==fad-150 && offset==sector*KUI_RAW_BYTES):(flags && sector<s->job->available[i] && s->job->source.plan.tracks[i].control==4));
        if(!ok) {result=-1;break;}
        mark(s,i,(uint32_t)sector);if(bad)++s->job->bad_records;else ++s->job->suspect_records;
        kui_sha256_update(&s->binding,buf,strlen(buf));
    }
    if(result<0 || !header) {if(!s->failed)fail(s,"Invalid legacy target map");return close_file(s,&r.file,false);}
    return close_file(s,&r.file,true);
}
static bool read_sector(struct importer *s,unsigned i,uint32_t sector,uint8_t *bytes) {
    if(!path(s,s->job->source.files[i],NULL))return fail(s,"Legacy path is too long");
    FIL f;if(f_open(&f,s->path,FA_READ)!=FR_OK)return fail(s,"Cannot open legacy saved anchor");
    uint64_t offset=(uint64_t)sector*KUI_RAW_BYTES;
    bool ok=f_lseek(&f,(FSIZE_t)offset)==FR_OK && f_tell(&f)==offset && exact(s,&f,bytes,KUI_RAW_BYTES);
    if(!ok && !s->failed && !s->stopped)fail(s,"Cannot seek legacy saved sector");
    return close_file(s,&f,ok);
}
static bool baseline(struct importer *s,unsigned i,bool *present,uint32_t *expected) {
    *present=false;if(!path(s,s->job->source.files[i],".recovery-base"))return false;
    FIL f;FRESULT result=f_open(&f,s->path,FA_READ);if(result==FR_NO_FILE)return true;
    if(result!=FR_OK)return fail(s,"Cannot inspect legacy recovery baseline");
    uint8_t header[36],record[8];bool ok=f_size(&f)>=40 && exact(s,&f,header,sizeof(header));
    uint32_t targets=ok?get32(header+28):0;
    ok=ok && !memcmp(header,"GDRCV1\r\n",8) && get32(header+8)==tag(s,i) &&
        get32(header+12)==s->job->source.plan.tracks[i].start && get32(header+16)==s->job->declared[i] &&
        get32(header+20)==s->job->source.plan.tracks[i].control && get32(header+24)==KUI_RAW_BYTES &&
        targets && targets<=s->job->declared[i] && f_size(&f)==40u+(uint64_t)targets*8u && s->job->available[i]==s->job->declared[i];
    uint32_t crc=ok?kui_crc32(0,header,sizeof(header)):0,whole=ok?get32(header+32):0,previous=0;
    for(uint32_t n=0;ok && n<targets;++n) {
        ok=exact(s,&f,record,8);if(!ok)break;uint32_t sector=get32(record);
        if(ok && (sector>=s->job->declared[i] || (n && sector<=previous)))ok=false;
        if(!ok)break;
        crc=kui_crc32(crc,record,8);mark(s,i,sector);previous=sector;
        if(!read_sector(s,i,sector,s->saved)) {ok=false;break;}
        whole=kui_recovery_crc_replace(whole,get32(record+4),kui_crc32(0,s->saved,KUI_RAW_BYTES),
            (uint64_t)(s->job->declared[i]-sector-1)*KUI_RAW_BYTES);
    }
    if(ok)ok=exact(s,&f,record,4) && get32(record)==crc;
    if(ok) {kui_sha256_update(&s->binding,header,sizeof(header));hash32(&s->binding,crc);*present=true;*expected=whole;}
    else if(!s->failed && !s->stopped)fail(s,"Invalid legacy recovery baseline");
    return close_file(s,&f,ok);
}
static bool saved_hash(struct importer *s,unsigned i,bool base,uint32_t expected) {
    if(!s->job->available[i]) {struct kui_sha256 h;kui_sha256_init(&h);kui_sha256_digest(&h,s->job->sha256[i]);return !s->job->checkpoint_valid[i] || s->job->checkpoint_crc[i]==0 || fail(s,"Invalid legacy empty-prefix CRC");}
    if(!path(s,s->job->source.files[i],NULL))return false;
    FIL f;if(f_open(&f,s->path,FA_READ)!=FR_OK)return fail(s,"Cannot hash legacy source");
    struct kui_sha256 hash;kui_sha256_init(&hash);uint32_t crc=0;uint64_t at=0,total=(uint64_t)s->job->available[i]*KUI_RAW_BYTES;
    bool ok=true;uint64_t checkpoint_at=(uint64_t)s->job->checkpoint_sectors[i]*KUI_RAW_BYTES;uint32_t prefix=0;
    while(ok && at<total) {
        unsigned n=(unsigned)(total-at>sizeof(s->saved)?sizeof(s->saved):total-at);
        if(s->job->checkpoint_valid[i] && checkpoint_at>at && checkpoint_at-at<n)n=(unsigned)(checkpoint_at-at);
        ok=exact(s,&f,s->saved,n);if(!ok)break;
        crc=kui_crc32(crc,s->saved,n);kui_sha256_update(&hash,s->saved,n);at+=n;
        if(at==checkpoint_at)prefix=crc;
    }
    if(!close_file(s,&f,ok))return false;
    if(!ok)return false;
    kui_sha256_digest(&hash,s->job->sha256[i]);s->job->crc[i]=crc;
    if((base && crc!=expected) || (!base && s->job->checkpoint_valid[i] && prefix!=s->job->checkpoint_crc[i]))return fail(s,"Legacy saved bytes disagree with CRC evidence");
    /* Reopen and repeat the entire admitted prefix. A one-off saved-file read
     * is never used to bless source instability during import. */
    if(!path(s,s->job->source.files[i],NULL) || f_open(&f,s->path,FA_READ)!=FR_OK)return fail(s,"Cannot reopen legacy source");
    struct kui_sha256 again;kui_sha256_init(&again);at=0;crc=0;
    while(ok && at<total) {unsigned n=(unsigned)(total-at>sizeof(s->repeat)?sizeof(s->repeat):total-at);ok=exact(s,&f,s->repeat,n);if(ok) {crc=kui_crc32(crc,s->repeat,n);kui_sha256_update(&again,s->repeat,n);at+=n;}}
    if(!close_file(s,&f,ok))return false;
    uint8_t digest[32];kui_sha256_digest(&again,digest);
    if(!ok)return false;
    if(crc!=s->job->crc[i] || memcmp(digest,s->job->sha256[i],32))return fail(s,"Legacy saved-file reads are unstable; import blocked");
    return true;
}
static unsigned data_flags(struct importer *s,const uint8_t *raw,uint32_t fad) {
    return s->job->source.plan.profile==KUI_CAPTURE_PROFILE_CD_TOC?kui_recovery_cd_sector_check(raw,KUI_RAW_BYTES,fad):kui_recovery_sector_check(raw,KUI_RAW_BYTES,fad);
}
static bool optical(struct importer *s,uint32_t fad,bool data,uint8_t *bytes) {
    for(unsigned n=0;n<10 && !stop(s);++n) {
        enum kui_read_result r=s->ops->read(s->ops->ctx,fad,1,bytes);
        if(r==KUI_READ_FATAL)return fail(s,"Drive reset required during legacy identity check");
        if(r==KUI_READ_OK && (!data || !data_flags(s,bytes,fad)))return !stop(s);
    }
    return false;
}
static bool audio_useful(const uint8_t *raw) {
    for(unsigned i=1;i<KUI_RAW_BYTES;++i)if(raw[i]!=raw[0])return true;
    return false;
}
static bool identity(struct importer *s) {
    const struct kui_capture_plan *p=&s->job->source.plan;
    if(p->profile==KUI_CAPTURE_PROFILE_GD) {
        if(!path(s,"rip.disc",NULL))return false;
        FIL f;FRESULT r=f_open(&f,s->path,FA_READ);bool saved=false;
        if(r==FR_OK) {bool ok=f_size(&f)==2048 && exact(s,&f,s->saved,2048);if(!close_file(s,&f,ok))return false;if(!ok)return fail(s,"Invalid legacy disc identity size");saved=true;}
        else if(r!=FR_NO_FILE)return fail(s,"Cannot read legacy disc identity");
        if(!saved) {
            for(unsigned i=0;i<p->count;++i)if(p->tracks[i].control==4 && p->tracks[i].start==45150 && s->job->available[i]) {
                if(!read_sector(s,i,0,s->saved))return false;
                if(kui_recovery_sector_check(s->saved,KUI_RAW_BYTES,45150))return fail(s,"Legacy boot sector is invalid and rip.disc is absent");
                memmove(s->saved,s->saved+16,2048);saved=true;break;
            }
        }
        if(!saved)return fail(s,"Legacy GD job has no saved content identity");
        for(unsigned n=0;n<2;++n) {
            if(!optical(s,45150,true,s->optical))return s->failed||s->stopped?false:fail(s,"Cannot read legacy GD identity");
            if(kui_recovery_sector_check(s->optical,KUI_RAW_BYTES,45150) || memcmp(s->saved,s->optical+16,2048))return fail(s,"Inserted disc differs from legacy GD identity");
        }
        s->job->boot_identity=true;s->job->identity_fad[0]=45150;
        struct kui_sha256 anchor;kui_sha256_init(&anchor);kui_sha256_update(&anchor,s->saved,2048);kui_sha256_digest(&anchor,s->job->identity_sha256[0]);
        kui_sha256_update(&s->binding,s->saved,2048);return true;
    }
    unsigned found=0;uint32_t previous=0;
    for(unsigned i=0;i<p->count && found<2;++i) {
        uint32_t count=s->job->available[i];if(!count)continue;
        for(unsigned n=0;n<128 && found<2;++n) {
            uint32_t sector=(uint32_t)((uint64_t)n*(count-1)/127);uint32_t fad=p->tracks[i].start+sector;
            if(fad==previous || kui_legacy_target_is_marked(s->job,i,sector))continue;
            if(!read_sector(s,i,sector,s->saved))return false;
            if(p->tracks[i].control==4 && data_flags(s,s->saved,fad))continue;
            if(p->tracks[i].control==0 && !audio_useful(s->saved))continue;
            if(!optical(s,fad,p->tracks[i].control==4,s->optical)) {if(s->failed || s->stopped)return false;continue;}
            if(memcmp(s->saved,s->optical,KUI_RAW_BYTES))return fail(s,"Inserted CD differs from legacy saved anchor");
            if(!optical(s,fad,p->tracks[i].control==4,s->optical)) {if(s->failed || s->stopped)return false;continue;}
            if(memcmp(s->saved,s->optical,KUI_RAW_BYTES))return fail(s,"Legacy CD identity reads disagree");
            struct kui_sha256 anchor;uint8_t digest[32];kui_sha256_init(&anchor);kui_sha256_update(&anchor,s->optical,KUI_RAW_BYTES);kui_sha256_digest(&anchor,digest);
            if(found && !memcmp(digest,s->job->identity_sha256[0],32))continue;
            s->job->identity_fad[found]=fad;memcpy(s->job->identity_sha256[found],digest,32);
            hash32(&s->binding,fad);kui_sha256_update(&s->binding,s->optical,KUI_RAW_BYTES);previous=fad;++found;
        }
    }
    return found==2 || fail(s,"Legacy CD requires two distinct saved content anchors");
}
enum kui_legacy_result kui_legacy_import(const char *directory,const struct kui_capture_plan *prepared,
    const struct kui_capture_ops *ops,struct kui_legacy_job *out) {
    if(!out || !ops || !ops->read || !prepared_valid(prepared))return KUI_LEGACY_INVALID;
    struct importer *s=calloc(1,sizeof(*s));struct kui_legacy_job *job=calloc(1,sizeof(*job));
    if(!s || !job) {free(s);free(job);return KUI_LEGACY_INVALID;}
    s->job=job;s->ops=ops;enum kui_legacy_result result=KUI_LEGACY_INVALID;
    if(!kui_destination_job_normalize(s->directory,directory)) {fail(s,"Invalid legacy source folder");goto done;}
    if(stop(s))goto done;
    if(!path(s,"rip.state",NULL))goto done;
    FIL f;FRESULT r=f_open(&f,s->path,FA_READ);if(r==FR_NO_FILE) {result=KUI_LEGACY_ABSENT;goto done;}
    if(r!=FR_OK) {fail(s,"Cannot read legacy rip.state");goto done;}
    uint64_t bytes=f_size(&f);char *state=bytes && bytes<=KUI_LEGACY_STATE_LIMIT?malloc((size_t)bytes):NULL;
    bool ok=state && exact(s,&f,state,(unsigned)bytes);
    if(!close_file(s,&f,ok)) {free(state);goto done;}
    ok=ok && parse_state(state,(size_t)bytes,prepared,job);
    if(!ok) {free(state);fail(s,"Unsupported or invalid legacy raw rip.state");goto done;}
    kui_sha256_init(&s->binding);kui_sha256_update(&s->binding,"KUI legacy raw import v1",strlen("KUI legacy raw import v1"));kui_sha256_update(&s->binding,state,(size_t)bytes);free(state);
    path(s,"readback.unstable",NULL);FILINFO info;r=f_stat(s->path,&info);
    if(r!=FR_NO_FILE) {fail(s,r==FR_OK?"Legacy job is marked storage-unstable; clear it with a consistent saved scan":"Cannot inspect legacy storage-instability guard");goto done;}
    for(unsigned i=0;i<prepared->count;++i) {
        if(!path(s,job->source.files[i],NULL))goto done;
        r=f_stat(s->path,&info);uint64_t size=r==FR_OK?(uint64_t)info.fsize:0;
        if((r!=FR_OK && r!=FR_NO_FILE) || (r==FR_OK && (info.fattrib&AM_DIR)) || size%KUI_RAW_BYTES || size>(uint64_t)job->declared[i]*KUI_RAW_BYTES) {fail(s,"Legacy track size is invalid");goto done;}
        job->available[i]=(uint32_t)(size/KUI_RAW_BYTES);
        if(job->available[i]<prepared->tracks[i].end-prepared->tracks[i].start)job->partial=true;
        if(!checkpoint(s,i) || !target_map(s,i,false) || !target_map(s,i,true))goto done;
        bool base=false;uint32_t expected=0;if(!baseline(s,i,&base,&expected))goto done;
        if(!base && job->checkpoint_valid[i] && job->checkpoint_sectors[i]<job->available[i]) {
            job->available[i]=job->checkpoint_sectors[i];job->partial=true;
        }
        if(!saved_hash(s,i,base,expected))goto done;
        hash32(&s->binding,i);hash32(&s->binding,job->available[i]);hash32(&s->binding,job->crc[i]);kui_sha256_update(&s->binding,job->sha256[i],32);
    }
    if(!identity(s))goto done;
    kui_sha256_update(&s->binding,job->target_bits,sizeof(job->target_bits));kui_sha256_digest(&s->binding,job->binding);
    snprintf(job->message,sizeof(job->message),"Legacy raw job admitted; original state and sidecars retained");result=KUI_LEGACY_READY;
 done:
    if(s->stopped)result=KUI_LEGACY_STOPPED;
    if(result!=KUI_LEGACY_ABSENT)*out=*job;
    if(ops->log && job->message[0])ops->log("Legacy import: %s",job->message);
    free(job);free(s);return result;
}

enum kui_legacy_result kui_legacy_identity_check(const struct kui_legacy_job *job,const struct kui_capture_ops *ops) {
    if(!job || !ops || !ops->read || !prepared_valid(&job->source.plan) ||
       (!job->boot_identity && !memcmp(job->identity_sha256[0],job->identity_sha256[1],32)))return KUI_LEGACY_INVALID;
    uint8_t *raw=malloc(KUI_RAW_BYTES);if(!raw)return KUI_LEGACY_INVALID;
    enum kui_legacy_result result=KUI_LEGACY_READY;
    unsigned anchors=job->boot_identity?1u:2u;
    for(unsigned a=0;a<anchors && result==KUI_LEGACY_READY;++a) {
        uint32_t fad=job->identity_fad[a];bool data=job->boot_identity,in_range=false;
        for(unsigned i=0;i<job->source.plan.count;++i) {
            const struct kui_capture_track *t=&job->source.plan.tracks[i];
            if(fad>=t->start && fad<t->end) {in_range=true;data=t->control==4;break;}
        }
        if(!in_range || (job->boot_identity && fad!=45150) || (a && fad==job->identity_fad[0])) {result=KUI_LEGACY_INVALID;break;}
        for(unsigned pair=0;pair<2 && result==KUI_LEGACY_READY;++pair) {
            bool matched=false;
            for(unsigned n=0;n<10;++n) {
                if(ops->cancelled && ops->cancelled(ops->ctx)) {result=KUI_LEGACY_STOPPED;break;}
                enum kui_read_result r=ops->read(ops->ctx,fad,1,raw);
                if(r==KUI_READ_FATAL) {result=KUI_LEGACY_INVALID;break;}
                if(r!=KUI_READ_OK || (!data && !audio_useful(raw)) || (data && (job->source.plan.profile==KUI_CAPTURE_PROFILE_CD_TOC?kui_recovery_cd_sector_check(raw,KUI_RAW_BYTES,fad):kui_recovery_sector_check(raw,KUI_RAW_BYTES,fad))))continue;
                struct kui_sha256 hash;uint8_t digest[32];kui_sha256_init(&hash);kui_sha256_update(&hash,raw+(job->boot_identity?16u:0u),job->boot_identity?2048u:KUI_RAW_BYTES);kui_sha256_digest(&hash,digest);
                if(memcmp(digest,job->identity_sha256[a],32)) {result=KUI_LEGACY_INVALID;break;}
                matched=true;break;
            }
            if(!matched && result==KUI_LEGACY_READY)result=KUI_LEGACY_INVALID;
        }
    }
    free(raw);return result;
}
