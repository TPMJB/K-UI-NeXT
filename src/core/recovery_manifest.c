/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/recovery_scan.h"
#include "kui/recovery_checks.h"
#include <limits.h>
#include <string.h>

struct parser { const uint8_t *at,*end; };
static void space(struct parser *p) {
    while(p->at<p->end && (*p->at==' ' || *p->at=='\t' || *p->at=='\n' || *p->at=='\r')) ++p->at;
}
static bool take(struct parser *p,uint8_t ch) {
    space(p);if(p->at==p->end || *p->at!=ch) return false;++p->at;return true;
}
static int hex(unsigned ch) {
    if(ch>='0' && ch<='9') return (int)(ch-'0');
    if(ch>='a' && ch<='f') return (int)(ch-'a'+10);
    if(ch>='A' && ch<='F') return (int)(ch-'A'+10);
    return -1;
}
static bool string(struct parser *p,char *out,size_t cap) {
    if(!cap || !take(p,'"')) return false;
    size_t n=0;
    while(p->at<p->end) {
        unsigned ch=*p->at++;
        if(ch=='"') {out[n]=0;return true;}
        if(ch<32) return false;
        if(ch=='\\') {
            if(p->at==p->end) return false;
            ch=*p->at++;
            if(ch=='b') ch='\b';else if(ch=='f') ch='\f';else if(ch=='n') ch='\n';
            else if(ch=='r') ch='\r';else if(ch=='t') ch='\t';
            else if(ch=='u') {
                if((size_t)(p->end-p->at)<4) return false;
                ch=0;
                for(unsigned i=0;i<4;++i) {int h=hex(*p->at++);if(h<0) return false;ch=ch*16+(unsigned)h;}
                /* Capture metadata is ASCII. Do not truncate a wide escape or
                 * accept embedded NUL that could alias a key or path. */
                if(!ch || ch>127) return false;
            } else if(ch!='"' && ch!='\\' && ch!='/') return false;
        }
        if(n+1>=cap) return false;
        out[n++]=(char)ch;
    }
    return false;
}
static bool number(struct parser *p,uint64_t *out) {
    space(p);if(p->at==p->end || *p->at<'0' || *p->at>'9') return false;
    uint64_t n=0;bool zero=*p->at=='0';unsigned digits=0;
    while(p->at<p->end && *p->at>='0' && *p->at<='9') {
        unsigned v=*p->at++-'0';
        if(n>(UINT64_MAX-v)/10 || (zero && digits)) return false;
        n=n*10+v;++digits;
    }
    *out=n;return true;
}
static bool yes(struct parser *p) {
    space(p);if((size_t)(p->end-p->at)<4 || memcmp(p->at,"true",4)) return false;
    p->at+=4;return true;
}
static bool hex_bytes(struct parser *p,uint8_t *out,size_t bytes) {
    char value[65];if(bytes>32 || !string(p,value,sizeof(value)) || strlen(value)!=bytes*2) return false;
    for(size_t i=0;i<bytes;++i) {
        int a=hex((unsigned char)value[i*2]),b=hex((unsigned char)value[i*2+1]);
        if(a<0 || b<0) return false;
        out[i]=(uint8_t)((unsigned)a*16+(unsigned)b);
    }
    return true;
}
static int key_index(const char *key,const char *const *names,unsigned count) {
    for(unsigned i=0;i<count;++i) if(!strcmp(key,names[i])) return (int)i;
    return -1;
}
static bool next_key(struct parser *p,char *key,size_t cap,uint32_t *seen,
        const char *const *keys,unsigned count,int *id) {
    if(!string(p,key,cap) || !take(p,':')) return false;
    *id=key_index(key,keys,count);
    if(*id<0 || (*seen&(1u<<(unsigned)*id))) return false;
    *seen|=1u<<(unsigned)*id;return true;
}
static bool track(struct parser *p,struct kui_scan_manifest *m,unsigned index,bool *has_sha) {
    static const char *const keys[]={"number","session","control","start_fad","end_fad",
        "toc_end_fad","excluded_tail_sectors","file","bytes","crc32","sha256"};
    if(index>=99 || !take(p,'{')) return false;
    uint32_t seen=0;uint64_t values[9]={0};char key[32],file[32]={0};uint8_t crc[4]={0};
    for(;;) {
        int id;if(!next_key(p,key,sizeof(key),&seen,keys,11,&id)) return false;
        if(id==7) {if(!string(p,file,sizeof(file))) return false;}
        else if(id==9) {if(!hex_bytes(p,crc,4)) return false;}
        else if(id==10) {if(!hex_bytes(p,m->track[index].sha256,32)) return false;}
        else if(!number(p,&values[id])) return false;
        space(p);if(take(p,'}')) break;if(!take(p,',')) return false;
    }
    if((seen&0x3ffu)!=0x3ffu || values[0]!=index+1u || values[1]>1 ||
       (values[2]!=0 && values[2]!=4) || values[3]<150 || values[4]<=values[3] ||
       values[4]>KUI_RECOVERY_FAD_MAX+1u || values[5]<values[4] || values[5]>0xffffff ||
       values[6]!=values[5]-values[4] || (values[6]!=0 && values[6]!=150) ||
       values[8]!=(values[4]-values[3])*KUI_RAW_BYTES || values[8]>UINT32_MAX) return false;
    char expected[32];
    /* Track numbers are bounded before conversion; avoid a printf dependency
     * in the pure parser. All current K-UI raw names have exactly 11 bytes. */
    memcpy(expected,"track00.bin",12);expected[5]=(char)('0'+(index+1)/10);expected[6]=(char)('0'+(index+1)%10);
    if(!values[2]) memcpy(expected+8,"raw",4);
    if(strcmp(file,expected)) return false;
    struct kui_capture_track *t=&m->plan.tracks[index];
    *t=(struct kui_capture_track){(uint32_t)values[0],(uint32_t)values[2],(uint32_t)values[1],
        (uint32_t)values[3],(uint32_t)values[4],(uint32_t)values[5]};
    m->track[index].crc32=(uint32_t)crc[0]<<24|(uint32_t)crc[1]<<16|(uint32_t)crc[2]<<8|crc[3];
    m->plan.bytes+=values[8];*has_sha=(seen&(1u<<10))!=0;return true;
}
static bool tracks(struct parser *p,struct kui_scan_manifest *m,bool *sha) {
    if(!take(p,'[')) return false;
    for(;;) {
        unsigned i=m->plan.count;bool current_sha;
        if(!track(p,m,i,&current_sha) || (i && *sha!=current_sha)) return false;
        *sha=current_sha;++m->plan.count;
        if(take(p,']')) return true;
        if(!take(p,',')) return false;
    }
}
static bool descriptor(const char *s) {
    size_t n=strlen(s);if(n<5 || n>=KUI_DEST_TITLE_CAP+5u || strcmp(s+n-4,".gdi")) return false;
    if(s[0]=='.' || s[n-5]==' ' || s[n-5]=='.') return false;
    for(size_t i=0;i<n;++i)
        if((unsigned char)s[i]<32 || (unsigned char)s[i]>126 || strchr("\\/:*?\"<>|",s[i])) return false;
    return true;
}
bool kui_recovery_manifest_parse(const void *data,size_t size,struct kui_scan_manifest *out) {
    static const char *const keys[]={"schema","complete","profile","identity","capture_build",
        "title","sector_bytes","audio","tracks","gdi_file","hashes","saved_data_verified","reference"};
    if(!data || !size || size>KUI_SCAN_MANIFEST_LIMIT || !out) return false;
    memset(out,0,sizeof(*out));memcpy(out->gdi,"disc.gdi",9);
    struct parser p={(const uint8_t *)data,(const uint8_t *)data+size};
    if(!take(&p,'{')) return false;
    uint32_t seen=0;uint64_t schema=0,sector_bytes=0;bool sha=false;char key[32],value[160];
    for(;;) {
        int id;if(!next_key(&p,key,sizeof(key),&seen,keys,13,&id)) return false;
        switch(id) {
        case 0: if(!number(&p,&schema)) return false;break;
        case 1: case 11: if(!yes(&p)) return false;break;
        case 2: if(!string(&p,value,sizeof(value)) || strcmp(value,KUI_CAPTURE_PROFILE)) return false;break;
        case 3: if(!hex_bytes(&p,out->identity,32)) return false;break;
        case 4:
            if(!string(&p,value,sizeof(value)) || strlen(value)!=12) return false;
            for(unsigned i=0;i<12;++i) if(hex((unsigned char)value[i])<0) return false;
            break;
        case 5: case 7: case 12: if(!string(&p,value,sizeof(value))) return false;break;
        case 6: if(!number(&p,&sector_bytes)) return false;break;
        case 8: if(!tracks(&p,out,&sha)) return false;break;
        case 9: if(!string(&p,out->gdi,sizeof(out->gdi)) || !descriptor(out->gdi)) return false;break;
        case 10:
            if(!take(&p,'[') || !string(&p,value,sizeof(value)) || strcmp(value,"crc32") || !take(&p,']')) return false;
            break;
        default: return false;
        }
        if(take(&p,'}')) break;
        if(!take(&p,',')) return false;
    }
    space(&p);
    if(p.at!=p.end || (seen&0x1ffu)!=0x1ffu || sector_bytes!=KUI_RAW_BYTES ||
       (schema!=1 && schema!=2) || sha!=(schema==1) ||
       ((seen&(1u<<10))!=0)!=(schema==2) || ((seen&(1u<<11))!=0)!=(schema==1) ||
       ((seen&(1u<<12)) && schema!=1)) return false;
    out->crc_only=schema==2;
    bool high=false;
    for(unsigned i=0;i<out->plan.count;++i) {
        const struct kui_capture_track *t=&out->plan.tracks[i];
        if(!i && (t->start!=150 || t->session!=0)) return false;
        if(i) {
            const struct kui_capture_track *prev=&out->plan.tracks[i-1];
            if(t->session<prev->session || t->start<prev->toc_end) return false;
            if(t->session==prev->session && t->start!=prev->toc_end) return false;
        }
        if(t->session && !high) {
            if(t->start!=45150 || t->control!=4) return false;
            high=true;
        }
        uint32_t gap=(i+1<out->plan.count && t->session==out->plan.tracks[i+1].session &&
                      t->control!=out->plan.tracks[i+1].control)?150u:0u;
        if(t->toc_end-t->end!=gap) return false;
    }
    return high;
}

/* Deliberately separate from the JSON parser: GDI is a line-oriented format.
 * Imported names may be quoted, but are never permitted to escape the folder. */
struct gdi_line {const uint8_t *at,*end;};
static void gdi_space(struct gdi_line *p) {
    while(p->at<p->end && (*p->at==' ' || *p->at=='\t')) ++p->at;
}
static bool gdi_number(struct gdi_line *p,uint32_t *out) {
    gdi_space(p);if(p->at==p->end || *p->at<'0' || *p->at>'9') return false;
    uint32_t value=0;
    do {
        unsigned digit=*p->at++-'0';if(value>(UINT32_MAX-digit)/10) return false;
        value=value*10+digit;
    } while(p->at<p->end && *p->at>='0' && *p->at<='9');
    if(p->at<p->end && *p->at!=' ' && *p->at!='\t') return false;
    *out=value;return true;
}
static bool gdi_filename(struct gdi_line *p,char out[KUI_DEST_NAME_CAP]) {
    gdi_space(p);if(p->at==p->end) return false;
    bool quoted=*p->at=='"';if(quoted) ++p->at;
    size_t length=0;
    while(p->at<p->end) {
        unsigned ch=*p->at;
        if((quoted && ch=='"') || (!quoted && (ch==' ' || ch=='\t'))) break;
        if(ch<32 || ch>126 || strchr("\\/:*?\"<>|",(int)ch) || length+1>=KUI_DEST_NAME_CAP) return false;
        out[length++]=(char)ch;++p->at;
    }
    if(quoted && (p->at==p->end || *p->at++!='"')) return false;
    if(p->at<p->end && *p->at!=' ' && *p->at!='\t') return false;
    if(!length || out[0]=='.' || out[length-1]=='.' || out[length-1]==' ') return false;
    out[length]=0;return true;
}
static bool same_name(const char *a,const char *b) {
    while(*a && *b) {
        unsigned x=(unsigned char)*a++,y=(unsigned char)*b++;
        if(x>='A' && x<='Z') x+='a'-'A';
        if(y>='A' && y<='Z') y+='a'-'A';
        if(x!=y) return false;
    }
    return *a==*b;
}
bool kui_recovery_gdi_parse(const void *data,size_t size,struct kui_scan_gdi *out) {
    if(!data || !size || size>KUI_SCAN_MANIFEST_LIMIT || !out) return false;
    memset(out,0,sizeof(*out));
    const uint8_t *at=data,*end=at+size;unsigned line=0;uint32_t count=0;
    while(at<end) {
        const uint8_t *next=at;while(next<end && *next!='\n') ++next;
        struct gdi_line p={at,next};if(p.end>p.at && p.end[-1]=='\r') --p.end;
        if(!line) {
            if(!gdi_number(&p,&count) || !count || count>99) return false;
            out->plan.count=count;
        } else {
            if(line>count) return false;
            uint32_t number,lba,control,bytes,offset;
            if(!gdi_number(&p,&number) || number!=line || !gdi_number(&p,&lba) ||
               lba>KUI_RECOVERY_FAD_MAX-150u || !gdi_number(&p,&control) ||
               (control!=0 && control!=4) || !gdi_number(&p,&bytes) || bytes!=KUI_RAW_BYTES ||
               !gdi_filename(&p,out->files[line-1]) || !gdi_number(&p,&offset) || offset) return false;
            if(line>1 && lba+150<=out->plan.tracks[line-2].start) return false;
            for(unsigned i=0;i+1<line;++i) if(same_name(out->files[i],out->files[line-1])) return false;
            out->plan.tracks[line-1]=(struct kui_capture_track){number,control,lba>=45000?1u:0u,lba+150,0,0};
        }
        gdi_space(&p);if(p.at!=p.end) return false;
        ++line;at=next==end?end:next+1;
    }
    return line==count+1;
}
