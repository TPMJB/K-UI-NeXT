/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/raw_cue.h"
#include "kui/recovery_checks.h"
#include <stdlib.h>
#include <string.h>
struct cue_text {const char *p,*end;};
static bool take(struct cue_text *t,const char *word) {
    size_t n=strlen(word);if((size_t)(t->end-t->p)<n || memcmp(t->p,word,n))return false;t->p+=n;return true;
}
static bool digits(struct cue_text *t,unsigned maximum,unsigned *out) {
    unsigned n=0,count=0;
    while(t->p<t->end && *t->p>='0' && *t->p<='9') {unsigned v=(unsigned)(*t->p-'0');if(v>maximum || n>(maximum-v)/10)return false;n=n*10+v;++t->p;++count;}
    if(!count)return false;
    *out=n;return true;
}
static bool timecode(struct cue_text *t,uint32_t *out) {
    unsigned m,s,f;if(!digits(t,159,&m) || !take(t,":") || !digits(t,59,&s) || !take(t,":") || !digits(t,74,&f) || t->p!=t->end)return false;
    *out=(m*60u+s)*75u+f;return true;
}
static bool equal_name(const char *a,const char *b) {
    while(*a && *b) {unsigned x=(unsigned char)*a++,y=(unsigned char)*b++;if(x>='A'&&x<='Z')x+=32;if(y>='A'&&y<='Z')y+=32;if(x!=y)return false;}
    return *a==*b;
}
static bool parse(const void *data,size_t bytes,const struct kui_capture_plan *p,struct kui_scan_gdi *out) {
    if(!data || !bytes || bytes>KUI_RAW_CUE_LIMIT || !p || p->profile!=KUI_CAPTURE_PROFILE_CD_TOC || !p->count || p->count>99 || memchr(data,0,bytes))return false;
    uint64_t total=0;
    for(unsigned i=0;i<p->count;++i) {
        const struct kui_capture_track *t=&p->tracks[i];if(t->number!=i+1 || t->session || (t->control!=0 && t->control!=4) ||
           t->start<150 || t->end<=t->start || t->end>KUI_RECOVERY_FAD_MAX+1u || t->toc_end<t->end ||
           t->toc_end>KUI_RECOVERY_FAD_MAX+1u || (i && t->start<p->tracks[i-1].end))return false;
        total+=(uint64_t)(t->end-t->start)*KUI_RAW_BYTES;
    }
    if(total!=p->bytes)return false;
    memset(out,0,sizeof(*out));out->plan=*p;
    const char *at=data,*end=at+bytes;unsigned track=0,phase=0;bool pregap=false;uint32_t gap=0;
    while(at<end) {
        const char *start=at;while(at<end && *at!='\n')++at;const char *stop=at;if(at<end)++at;
        if(stop>start && stop[-1]=='\r')--stop;
        while(start<stop && (*start==' ' || *start=='\t'))++start;
        while(stop>start && (stop[-1]==' ' || stop[-1]=='\t'))--stop;
        if(start==stop || ((size_t)(stop-start)>=4 && !memcmp(start,"REM ",4)))continue;
        struct cue_text t={start,stop};
        if(take(&t,"FILE \"")) {
            if(track>=p->count || (phase && phase!=3))return false;
            if(phase==3) {++track;if(track>=p->count)return false;}
            const char *name=t.p;while(t.p<t.end && *t.p!='\"')++t.p;size_t n=(size_t)(t.p-name);
            if(!n || n>=KUI_DEST_NAME_CAP || !take(&t,"\" BINARY") || t.p!=t.end)return false;
            memcpy(out->files[track],name,n);out->files[track][n]=0;
            if(!kui_destination_name_valid(out->files[track]))return false;
            /* FatFs folds Unicode names. Keep this narrow parser ASCII-only
             * rather than admit two spellings that alias the same raw file. */
            for(size_t i=0;i<n;++i)if((unsigned char)out->files[track][i]>=128)return false;
            for(unsigned i=0;i<track;++i)if(equal_name(out->files[i],out->files[track]))return false;
            phase=1;pregap=false;gap=0;continue;
        }
        if(take(&t,"TRACK ")) {
            unsigned number;if(phase!=1 || !digits(&t,99,&number) || !take(&t," ") || number!=track+1)return false;
            if(p->tracks[track].control==0) {if(!take(&t,"AUDIO"))return false;}
            else if(!take(&t,"MODE1/2352") && !take(&t,"MODE2/2352"))return false;
            if(t.p!=t.end)return false;
            phase=2;continue;
        }
        if(take(&t,"PREGAP ")) {
            if(phase!=2 || pregap || !timecode(&t,&gap))return false;
            pregap=true;continue;
        }
        if(take(&t,"INDEX ")) {
            unsigned index;uint32_t frames;
            if(phase!=2 || !digits(&t,99,&index) || !take(&t," ") || index!=1 || !timecode(&t,&frames) || frames)return false;
            uint32_t expected=p->tracks[track].start-(track?p->tracks[track-1].end:150u);
            if(gap!=expected)return false;
            phase=3;continue;
        }
        return false;
    }
    return phase==3 && track+1==p->count;
}
bool kui_raw_cue_parse(const void *data,size_t bytes,const struct kui_capture_plan *prepared,struct kui_scan_gdi *out) {
    if(!out)return false;
    struct kui_scan_gdi *tmp=malloc(sizeof(*tmp));if(!tmp)return false;
    bool ok=parse(data,bytes,prepared,tmp);if(ok)*out=*tmp;free(tmp);return ok;
}
