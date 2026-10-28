/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/saved_identity.h"
#include "kui/recovery_checks.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define PREFIX_CHUNK (16u*KUI_RAW_BYTES)
struct saved_context {
    const struct kui_saved_identity_source *source;
    const struct kui_capture_ops *ops;
    unsigned attempts;
    enum kui_saved_identity_result result;
    char directory[KUI_DEST_JOB_CAP],path[KUI_DEST_PATH_CAP],*message;
    uint8_t buffer[PREFIX_CHUNK],saved[KUI_RAW_BYTES],optical[KUI_RAW_BYTES];
};
static uint32_t get32(const uint8_t *p) {return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void put32(uint8_t *p,uint32_t n) {for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(n>>(8*i));}
static bool outcome(struct saved_context *s,enum kui_saved_identity_result r,const char *message) {
    s->result=r;if(s->message)snprintf(s->message,128,"%s",message);return false;
}
static bool stop(struct saved_context *s) {
    if(s->ops->cancelled && s->ops->cancelled(s->ops->ctx)){outcome(s,KUI_SAVED_IDENTITY_STOPPED,"Saved-anchor identity check stopped");return true;}
    return false;
}
static bool plan_valid(const struct kui_capture_plan *p) {
    if(!p || !p->count || p->count>99 || (p->profile!=KUI_CAPTURE_PROFILE_GD && p->profile!=KUI_CAPTURE_PROFILE_CD_TOC))return false;
    uint64_t bytes=0;
    for(unsigned i=0;i<p->count;++i) {
        const struct kui_capture_track *t=&p->tracks[i];
        if(t->number!=i+1 || (t->control!=0 && t->control!=4) || t->start<150 || t->end<=t->start || t->end>KUI_RECOVERY_FAD_MAX+1u ||
           t->toc_end<t->end || t->toc_end>KUI_RECOVERY_FAD_MAX+1u || t->session>1 || (p->profile==KUI_CAPTURE_PROFILE_CD_TOC && t->session) ||
           (i && t->start<p->tracks[i-1].end))return false;
        bytes+=(uint64_t)(t->end-t->start)*KUI_RAW_BYTES;
    }
    return bytes==p->bytes;
}
static bool source_binding(const struct kui_saved_identity_source *s,uint8_t out[32]) {
    char directory[KUI_DEST_JOB_CAP];
    if(!s || !plan_valid(s->plan) || !s->files || !s->sectors || !s->crc32 || (!s->crc_only && !s->sha256) || !s->capture_identity ||
       !kui_destination_job_normalize(directory,s->directory))return false;
    struct kui_sha256 h;kui_sha256_init(&h);kui_sha256_update(&h,"KUI saved-anchor repair binding v1",strlen("KUI saved-anchor repair binding v1"));
    kui_sha256_update(&h,directory,strlen(directory)+1);kui_sha256_update(&h,s->capture_identity,32);
    uint8_t bytes[4];put32(bytes,(uint32_t)s->plan->profile);kui_sha256_update(&h,bytes,4);put32(bytes,s->plan->count);kui_sha256_update(&h,bytes,4);put32(bytes,s->crc_only?1u:0u);kui_sha256_update(&h,bytes,4);
    for(unsigned i=0;i<s->plan->count;++i) {
        const struct kui_capture_track *t=&s->plan->tracks[i];
        if(!memchr(s->files[i],0,KUI_DEST_NAME_CAP) || !kui_destination_name_valid(s->files[i]) || s->sectors[i]>t->end-t->start || (!s->sectors[i] && s->crc32[i]))return false;
        uint32_t fields[]={t->number,t->control,t->session,t->start,t->end,t->toc_end,s->sectors[i],s->crc32[i]};
        for(unsigned j=0;j<8;++j) {put32(bytes,fields[j]);kui_sha256_update(&h,bytes,4);}
        kui_sha256_update(&h,s->files[i],strlen(s->files[i])+1);
        if(!s->crc_only)kui_sha256_update(&h,s->sha256[i],32);
    }
    kui_sha256_digest(&h,out);return true;
}
bool kui_saved_identity_matches_source(const struct kui_saved_identity_source *source,const struct kui_saved_identity *identity) {
    uint8_t expected[32];return identity && source_binding(source,expected) && identity->profile==source->plan->profile && !memcmp(expected,identity->source_binding,32);
}
static bool close_file(struct saved_context *s,FIL *f,bool ok) {
    if(f_close(f)!=FR_OK)return outcome(s,KUI_SAVED_IDENTITY_FAILED,"Cannot close saved identity source");
    return ok;
}
static bool exact(struct saved_context *s,FIL *f,void *data,unsigned bytes) {
    UINT got=0;if(stop(s))return false;
    if(f_read(f,data,bytes,&got)!=FR_OK || got!=bytes)return outcome(s,KUI_SAVED_IDENTITY_FAILED,"Cannot read saved identity source");
    return !stop(s);
}
static bool open_track(struct saved_context *s,unsigned i,FIL *f) {
    int n=snprintf(s->path,sizeof(s->path),"0:%s/%s",s->directory,s->source->files[i]);
    if(n<0 || n>=(int)sizeof(s->path) || f_open(f,s->path,FA_READ)!=FR_OK)return outcome(s,KUI_SAVED_IDENTITY_FAILED,"Cannot open saved identity source");
    return true;
}
static bool read_sector(struct saved_context *s,unsigned ti,uint32_t sector,uint8_t *out) {
    FIL f;if(!open_track(s,ti,&f))return false;
    uint64_t at=(uint64_t)sector*KUI_RAW_BYTES;
    bool ok=f_lseek(&f,(FSIZE_t)at)==FR_OK && f_tell(&f)==at;
    if(!ok)outcome(s,KUI_SAVED_IDENTITY_FAILED,"Cannot seek saved identity source");
    if(ok)ok=exact(s,&f,out,KUI_RAW_BYTES);
    return close_file(s,&f,ok);
}
static bool verify_prefixes(struct saved_context *s) {
    DWORD clusters[99];unsigned objects=0;
    for(unsigned i=0;i<s->source->plan->count;++i) {
        if(!s->source->sectors[i])continue;
        if(s->ops->log)s->ops->log("Alternate repair identity: checking original saved prefix %u/%u",i+1,s->source->plan->count);
        uint64_t wanted=(uint64_t)s->source->sectors[i]*KUI_RAW_BYTES;
        for(unsigned pass=0;pass<2;++pass) {
            FIL f;if(!open_track(s,i,&f))return false;
            bool ok=f_size(&f)>=wanted;
            if(!ok)outcome(s,KUI_SAVED_IDENTITY_SOURCE_INVALID,"Original saved prefix is truncated");
            if(ok && !pass) {
                for(unsigned j=0;j<objects;++j)if(clusters[j]==f.obj.sclust) {ok=outcome(s,KUI_SAVED_IDENTITY_SOURCE_INVALID,"Saved track names alias the same file");break;}
                if(ok)clusters[objects++]=f.obj.sclust;
            }
            struct kui_sha256 hash;kui_sha256_init(&hash);uint32_t crc=0;uint64_t at=0;
            while(ok && at<wanted) {
                unsigned n=(unsigned)(wanted-at>sizeof(s->buffer)?sizeof(s->buffer):wanted-at);
                ok=exact(s,&f,s->buffer,n);if(ok) {crc=kui_crc32(crc,s->buffer,n);kui_sha256_update(&hash,s->buffer,n);at+=n;}
            }
            if(!close_file(s,&f,ok))return false;
            uint8_t sha[32];kui_sha256_digest(&hash,sha);
            if(crc!=s->source->crc32[i] || (!s->source->crc_only && memcmp(sha,s->source->sha256[i],32)))
                return outcome(s,KUI_SAVED_IDENTITY_SOURCE_INVALID,"Original saved prefix does not match recorded hashes");
        }
    }
    return true;
}
static unsigned data_flags(const struct kui_capture_plan *p,const uint8_t *raw,uint32_t fad) {
    return p->profile==KUI_CAPTURE_PROFILE_CD_TOC?kui_recovery_cd_sector_check(raw,KUI_RAW_BYTES,fad):kui_recovery_sector_check(raw,KUI_RAW_BYTES,fad);
}
static bool optical_read(struct saved_context *s,uint32_t fad,bool data,uint8_t *out) {
    for(unsigned n=0;n<s->attempts && !stop(s);++n) {
        enum kui_read_result r=s->ops->read(s->ops->ctx,fad,1,out);
        if(r==KUI_READ_FATAL)return outcome(s,KUI_SAVED_IDENTITY_FAILED,"Drive reset required during alternate identity check");
        if(r==KUI_READ_OK && (!data || !data_flags(s->source->plan,out,fad)))return !stop(s);
    }
    return false;
}
static bool audio_useful(const uint8_t *raw) {
    for(unsigned i=1;i<KUI_RAW_BYTES;++i)if(raw[i]!=raw[0])return true;
    return false;
}
static bool matched_pair(struct saved_context *s,unsigned ti,uint32_t sector,bool require_useful_anchor) {
    const struct kui_capture_track *t=&s->source->plan->tracks[ti];uint32_t fad=t->start+sector;bool data=t->control==4;
    if(!read_sector(s,ti,sector,s->saved))return false;
    if(require_useful_anchor && data && data_flags(s->source->plan,s->saved,fad))return false;
    if(require_useful_anchor && !data && !audio_useful(s->saved))return false;
    for(unsigned pair=0;pair<2;++pair) {
        if(!optical_read(s,fad,data,s->optical))return false;
        if(memcmp(s->saved,s->optical,KUI_RAW_BYTES))return outcome(s,KUI_SAVED_IDENTITY_DIFFERENT,"Readable optical bytes differ from recorded saved bytes");
    }
    /* Reopen again to reject an unstable saved anchor after its optical reads. */
    if(!read_sector(s,ti,sector,s->buffer))return false;
    if(memcmp(s->saved,s->buffer,KUI_RAW_BYTES))return outcome(s,KUI_SAVED_IDENTITY_SOURCE_INVALID,"Saved identity anchor changed between reads");
    return true;
}
static bool compare_fixed(struct saved_context *s) {
    const struct kui_capture_plan *p=s->source->plan;
    for(unsigned i=0;i<p->count;++i) {
        const struct kui_capture_track *t=&p->tracks[i];uint32_t length=t->end-t->start,points[]={0,(length-1u)/2u,length-1u};
        for(unsigned n=0;n<3;++n) {
            if(points[n]>=s->source->sectors[i])continue;
            (void)matched_pair(s,i,points[n],false);
            if(s->result!=KUI_SAVED_IDENTITY_UNREADABLE)return false;
        }
    }
    return true;
}
static uint32_t sample_sector(uint32_t count,unsigned n,bool reverse) {
    uint32_t sector;
    if(n<16u)sector=n;
    else if(n<48u)sector=(uint32_t)((uint64_t)(count-1u)*(n-15u)/33u);
    else if(n<64u) {uint32_t middle=(count-1u)/2u,delta=(n-48u)/2u;sector=(n&1u)?middle+delta:middle>=delta?middle-delta:0u;}
    else sector=(uint32_t)(((uint64_t)(n-63u)*2654435761u)%count);
    if(sector>=count)sector=count-1u;
    return reverse?count-1u-sector:sector;
}
enum kui_saved_identity_result kui_saved_identity_discover(const struct kui_saved_identity_source *source,const struct kui_capture_ops *ops,
    unsigned attempts,struct kui_saved_identity *out,char message[128]) {
    uint8_t binding[32];
    if(!out || !ops || !ops->read || !attempts || attempts>50 || !source_binding(source,binding)) {if(message)snprintf(message,128,"Invalid alternate saved-identity request");return KUI_SAVED_IDENTITY_FAILED;}
    struct saved_context *s=calloc(1,sizeof(*s));if(!s)return KUI_SAVED_IDENTITY_FAILED;
    s->source=source;s->ops=ops;s->attempts=attempts;s->message=message;s->result=KUI_SAVED_IDENTITY_UNREADABLE;
    kui_destination_job_normalize(s->directory,source->directory);
    struct kui_saved_identity identity={.profile=source->plan->profile};memcpy(identity.source_binding,binding,32);
    if(stop(s) || !verify_prefixes(s) || !compare_fixed(s))goto done;
    for(unsigned anchor=0;anchor<2;++anchor) {
        bool found=false;
        for(unsigned audio_pass=0;audio_pass<(source->plan->profile==KUI_CAPTURE_PROFILE_CD_TOC?2u:1u) && !found;++audio_pass) {
            for(unsigned ti=0;ti<source->plan->count && !found;++ti) {
                const struct kui_capture_track *t=&source->plan->tracks[ti];uint32_t count=source->sectors[ti];
                if(!count || (audio_pass?(t->control!=0):(t->control!=4)) || (source->plan->profile==KUI_CAPTURE_PROFILE_GD && t->session!=1))continue;
                for(unsigned n=0;n<128 && !found;++n) {
                    uint32_t sector=sample_sector(count,n,anchor!=0),fad=t->start+sector;
                    if(anchor && fad==identity.fad[0])continue;
                    if(matched_pair(s,ti,sector,true)) {
                        struct kui_sha256 hash;uint8_t digest[32];kui_sha256_init(&hash);kui_sha256_update(&hash,s->saved,KUI_RAW_BYTES);kui_sha256_digest(&hash,digest);
                        if(anchor && !memcmp(digest,identity.sha256[0],32))continue;
                        identity.fad[anchor]=fad;memcpy(identity.sha256[anchor],digest,32);found=true;
                    } else if(s->result!=KUI_SAVED_IDENTITY_UNREADABLE)goto done;
                }
            }
        }
        if(!found) {outcome(s,KUI_SAVED_IDENTITY_UNREADABLE,"Cannot establish two healthy saved/optical recovery anchors");goto done;}
    }
    *out=identity;outcome(s,KUI_SAVED_IDENTITY_MATCH,"New saved-anchor recovery binding; original optical digest was unreadable");
 done:
    {enum kui_saved_identity_result result=s->result;free(s);return result;}
}
static bool receipt_valid(const struct kui_saved_identity *id,const struct kui_capture_plan *p) {
    if(!id || !plan_valid(p) || id->profile!=p->profile || id->fad[0]==id->fad[1] || !memcmp(id->sha256[0],id->sha256[1],32))return false;
    for(unsigned a=0;a<2;++a) {
        bool found=false;
        for(unsigned i=0;i<p->count;++i) {
            const struct kui_capture_track *t=&p->tracks[i];
            if(id->fad[a]>=t->start && id->fad[a]<t->end && (p->profile==KUI_CAPTURE_PROFILE_CD_TOC || (t->control==4 && t->session==1)))found=true;
        }
        if(!found)return false;
    }
    return true;
}
void kui_saved_identity_encode(const struct kui_saved_identity *id,uint8_t out[KUI_SAVED_IDENTITY_BYTES]) {
    memset(out,0,KUI_SAVED_IDENTITY_BYTES);memcpy(out,"KUISID1",8);put32(out+8,1);put32(out+12,(uint32_t)id->profile);put32(out+16,id->fad[0]);put32(out+20,id->fad[1]);memcpy(out+24,id->sha256,64);memcpy(out+88,id->source_binding,32);put32(out+120,kui_crc32(0,out,120));
}
bool kui_saved_identity_decode(const uint8_t data[KUI_SAVED_IDENTITY_BYTES],const struct kui_capture_plan *plan,struct kui_saved_identity *out) {
    if(!data || !out || memcmp(data,"KUISID1",8) || get32(data+8)!=1 || get32(data+120)!=kui_crc32(0,data,120))return false;
    struct kui_saved_identity id={.profile=(enum kui_capture_profile)get32(data+12)};id.fad[0]=get32(data+16);id.fad[1]=get32(data+20);memcpy(id.sha256,data+24,64);memcpy(id.source_binding,data+88,32);if(!receipt_valid(&id,plan))return false;*out=id;return true;
}
enum kui_saved_identity_result kui_saved_identity_recheck(const struct kui_saved_identity *id,const struct kui_capture_plan *plan,
    const struct kui_capture_ops *ops,unsigned attempts,char message[128]) {
    if(!receipt_valid(id,plan) || !ops || !ops->read || !attempts || attempts>50)return KUI_SAVED_IDENTITY_FAILED;
    struct kui_saved_identity_source source={.plan=plan};
    struct saved_context *s=calloc(1,sizeof(*s));if(!s)return KUI_SAVED_IDENTITY_FAILED;
    s->source=&source;s->ops=ops;s->attempts=attempts;s->message=message;s->result=KUI_SAVED_IDENTITY_UNREADABLE;
    for(unsigned a=0;a<2;++a) {
        bool data=false;for(unsigned ti=0;ti<plan->count;++ti)if(id->fad[a]>=plan->tracks[ti].start && id->fad[a]<plan->tracks[ti].end)data=plan->tracks[ti].control==4;
        for(unsigned n=0;n<2;++n) {
            if(!optical_read(s,id->fad[a],data,s->optical))goto done;
            struct kui_sha256 hash;uint8_t sha[32];kui_sha256_init(&hash);kui_sha256_update(&hash,s->optical,KUI_RAW_BYTES);kui_sha256_digest(&hash,sha);
            if(memcmp(sha,id->sha256[a],32)) {outcome(s,KUI_SAVED_IDENTITY_DIFFERENT,"Saved recovery identity anchor differs; replacement anchors are refused");goto done;}
            if(!data && !audio_useful(s->optical)) {outcome(s,KUI_SAVED_IDENTITY_UNREADABLE,"Constant audio cannot establish saved recovery identity");goto done;}
        }
    }
    outcome(s,KUI_SAVED_IDENTITY_MATCH,"Same saved recovery anchors matched; original capture digest not reestablished");
 done:
    {enum kui_saved_identity_result result=s->result;free(s);return result;}
}
bool kui_saved_checkpoint_decode(const uint8_t data[KUI_CHECKPOINT_BYTES],const struct kui_capture_plan *plan,struct kui_checkpoint *out) {
    return data && plan_valid(plan) && out && kui_checkpoint_decode(data,plan,data+24,out);
}
