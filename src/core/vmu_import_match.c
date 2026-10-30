/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_import_match.h"
#include <string.h>

enum family { F_NONE, F_SKIES, F_PSO, F_TOY, F_SILVER, F_EVO, F_EVO2,
    F_GRANDIA2, F_POWERSTONE2, F_RAYMAN2, F_LODOSS, F_CT2, F_NOMAD,
    F_ARMADA, F_TIME, F_SONIC2, F_RECV, F_ZOMBIE };
static int overlap(const void *a,size_t an,const void *b,size_t bn) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return an && bn && (x<=y ? y-x<an : x-y<bn);
}
static int range(const void *p,size_t n) {
    return p && n && (uintptr_t)p<=UINTPTR_MAX-(n-1u);
}
static size_t length(const char *s,size_t cap) {
    size_t n=0;while(n<cap && s[n]) ++n;return n;
}
/* ASCII punctuation/spacing differences are spelling, not fuzzy distance.
 * A sanitized '?' or non-ASCII byte makes the entire field unidentified. */
static unsigned normalized(const char *s,size_t cap,char *out) {
    size_t n=length(s,cap);unsigned used=0;
    if(n==cap) {out[0]=0;return 0;}
    for(size_t i=0;i<n;i++) {
        unsigned c=(unsigned char)s[i];
        if(c<0x20u || c>0x7eu || c=='?') {out[0]=0;return 0;}
        if(c>='a' && c<='z') c=c-'a'+'A';
        if((c>='A' && c<='Z') || (c>='0' && c<='9')) out[used++]=(char)c;
    }
    out[used]=0;return used;
}
static int generic(const char *s) {
    static const char *const words[]={"SEGA","SEGACORPORATION","KATANA","DC",
        "DREAMCAST","SEGADREAMCAST","SONICTEAM","CAPCOM","NAMCO","KONAMI",
        "UBISOFT","ACCLAIM","INFOGRAMES","EIDOS","MIDWAY","ACTIVISION",
        "ELECTRONICARTS","EA","VISUALCONCEPTS","SYSTEM","SYSTEMFILE","SAVE",
        "SAVEDATA","SAVEGAME","SAVEDGAME","GAMEDATA","GAME","DATA","DAT",
        "FILE","MAINFILE","MAINDATA","SYSTEMDATA","OPTIONS","OPTION",
        "CONFIG","CONFIGURATION","SETTINGS","GUILDCARD","CHARACTER",
        "CHARACTERDATA","REPLAY","THE","VMU","VMSSAVE"};
    for(unsigned i=0;i<sizeof(words)/sizeof(words[0]);i++)
        if(!strcmp(s,words[i])) return 1;
    return !s[0];
}
static int starts(const char *s,const char *prefix) {
    return !strncmp(s,prefix,strlen(prefix));
}
static int role_tail(const char *s) {
    static const char *const roles[]={"SYSTEMFILE","SYSTEMDATA","SYSTEM","SAVEDGAME",
        "SAVEDATA","SAVEGAME","SAVE","GAMEDATA","DATA","DAT","OPTIONS",
        "OPTION","CONFIGURATION","CONFIG","SETTINGS","MAINFILE","MAINDATA",
        "GUILDCARD","CHARACTERDATA","CHARACTER","REPLAY","FILE"};
    for(unsigned i=0;i<sizeof(roles)/sizeof(roles[0]);i++) {
        size_t n=strlen(roles[i]);
        if(!strncmp(s,roles[i],n)) {
            const char *tail=s+n;
            while(*tail>='0' && *tail<='9') ++tail;
            if(!*tail) return 1;
        }
    }
    return 0;
}
static enum family title_family(const char *s) {
    if(!strcmp(s,"SKIESOFARCADIA") || !strcmp(s,"ETERNALARCADIA")) return F_SKIES;
    if(!strcmp(s,"PHANTASYSTARONLINE") || !strcmp(s,"PHANTASYSTARONLINEVER2") ||
       !strcmp(s,"PHANTASYSTARONLINEVERSION2") || !strcmp(s,"PHANTASYSTARONLINEV2")) return F_PSO;
    if(!strcmp(s,"TOYCOMMANDER")) return F_TOY;
    if(!strcmp(s,"SILVER")) return F_SILVER;
    if(!strcmp(s,"EVOLUTION") || !strcmp(s,"EVOLUTIONTHEWORLDOFSACREDDEVICE")) return F_EVO;
    if(!strcmp(s,"EVOLUTION2") || !strcmp(s,"EVOLUTION2FAROFFPROMISE")) return F_EVO2;
    if(!strcmp(s,"GRANDIA2") || !strcmp(s,"GRANDIAII")) return F_GRANDIA2;
    if(!strcmp(s,"POWERSTONE2")) return F_POWERSTONE2;
    if(!strcmp(s,"RAYMAN2") || !strcmp(s,"RAYMAN2THEGREATESCAPE")) return F_RAYMAN2;
    if(!strcmp(s,"RECORDOFLODOSSWAR")) return F_LODOSS;
    if(!strcmp(s,"CRAZYTAXI2")) return F_CT2;
    if(!strcmp(s,"THENOMADSOUL") || !strcmp(s,"OMIKRONTHENOMADSOUL")) return F_NOMAD;
    if(!strcmp(s,"ARMADA")) return F_ARMADA;
    if(!strcmp(s,"TIMESTALKERS")) return F_TIME;
    if(!strcmp(s,"SONICADVENTURE2")) return F_SONIC2;
    if(!strcmp(s,"RESIDENTEVILCODEVERONICA") || !strcmp(s,"BIOHAZARDCODEVERONICA")) return F_RECV;
    if(!strcmp(s,"ZOMBIEREVENGE")) return F_ZOMBIE;
    return F_NONE;
}
static enum family text_family(const char *s) {
    enum family found=title_family(s);if(found) return found;
    /* Only these concrete metadata spellings, not a publisher or arbitrary
     * acronym, connect their associated files. PSO lacks an application ID. */
    if(!strcmp(s,"PSOMAINDATA") || !strcmp(s,"PSOGUILDCARD") ||
       !strcmp(s,"PSOCHARACTER") || !strcmp(s,"PSOCHARACTERDATA") ||
       !strcmp(s,"PSOSYSTEMDATA")) return F_PSO;
    static const struct {const char *text;enum family family;} prefixes[]={
        {"ETERNALARCADIA",F_SKIES},{"SKIESOFARCADIA",F_SKIES},
        {"TOYCOMMANDER",F_TOY},{"SILVER",F_SILVER},
        {"POWERSTONE2",F_POWERSTONE2},{"CRAZYTAXI2",F_CT2},
        {"SONICADVENTURE2",F_SONIC2},{"ZOMBIEREVENGE",F_ZOMBIE}};
    for(unsigned i=0;i<sizeof(prefixes)/sizeof(prefixes[0]);i++) {
        size_t n=strlen(prefixes[i].text);
        if(!strncmp(s,prefixes[i].text,n) && role_tail(s+n)) return prefixes[i].family;
    }
    return F_NONE;
}
static enum family name_family(const char *s) {
    /* Vetted complete internal-name stems observed in the source inventories.
     * Distinct sequels retain distinct stems; numeric save slots are not
     * interpreted as catalogue revision or version identifiers. */
    if(starts(s,"PSO______") && strlen(s)==12u) return F_PSO;
    if(starts(s,"TOY_COMM.") && strlen(s)==12u) return F_TOY;
    if(starts(s,"_SILVER_.") && strlen(s)==12u) return F_SILVER;
    if(starts(s,"EVO_DATA.") && strlen(s)==12u) return F_EVO;
    if(starts(s,"EVO2_DAT.") && strlen(s)==12u) return F_EVO2;
    if(starts(s,"GRANDIA2_") && strlen(s)==12u) return F_GRANDIA2;
    if(!strcmp(s,"P_STONE2_DAT")) return F_POWERSTONE2;
    if(starts(s,"RAYMAN_2.") && strlen(s)==12u) return F_RAYMAN2;
    if(starts(s,"LODOSS__.") && strlen(s)==12u) return F_LODOSS;
    if(!strcmp(s,"C_TAXI02.SYS")) return F_CT2;
    if(!strcmp(s,"THENOMADSOUL")) return F_NOMAD;
    if(starts(s,"ARMADA") && strlen(s)==12u && !strcmp(s+8u,".DAT")) return F_ARMADA;
    if(starts(s,"TIME_STK.") && strlen(s)==12u) return F_TIME;
    if(starts(s,"SONIC2___") && strlen(s)==12u) return F_SONIC2;
    if(starts(s,"RE_CV") && strlen(s)==12u && s[8]=='.') return F_RECV;
    if(!strcmp(s,"ZOMBIE_U_SYS")) return F_ZOMBIE;
    return F_NONE;
}
static enum family entry_family(const struct kui_vmu_import_entry *e) {
    enum family f=name_family(e->name);
    char text[33];
    const char *fields[]={e->short_description,e->long_description,e->application};
    const size_t caps[]={sizeof(e->short_description),sizeof(e->long_description),sizeof(e->application)};
    for(unsigned i=0;i<3u;i++) {
        normalized(fields[i],caps[i],text);enum family other=text_family(text);
        /* Retain the filename/first family as the grouping key even when
         * another header field contradicts it. resolve() sees BOTH votes and
         * refuses the complete group rather than detaching its companion. */
        if(other && f && other!=f) continue;
        if(other) f=other;
    }
    return f;
}
static int conflicting_family(const struct kui_vmu_import_entry *e) {
    enum family f=name_family(e->name);char text[33];
    const char *fields[]={e->short_description,e->long_description,e->application};
    const size_t caps[]={sizeof(e->short_description),sizeof(e->long_description),sizeof(e->application)};
    for(unsigned i=0;i<3u;i++) {
        normalized(fields[i],caps[i],text);enum family other=text_family(text);
        if(other && f && other!=f) return 1;
        if(other) f=other;
    }
    return 0;
}
static unsigned filename_stem(const char *s,char out[13]) {
    size_t n=strlen(s);
    if(n<5u || s[n-4u]!='.') {out[0]=0;return 0;}
    char raw[13];memcpy(raw,s,n-4u);raw[n-4u]=0;
    unsigned bytes=normalized(raw,sizeof(raw),out);
    if(bytes<6u || generic(out)) {out[0]=0;return 0;}
    return bytes;
}
static uint32_t evidence(const struct kui_vmu_import_entry *e,
                         const struct kui_vmu_import_match_game *game) {
    char title[129],product[12],field[33],stem[13];
    unsigned title_n=normalized(game->title,sizeof(game->title),title);
    unsigned product_n=normalized(game->product,sizeof(game->product),product);
    enum family family=title_family(title);
    uint32_t result=0;
    const char *fields[]={e->name,e->short_description,e->long_description,e->application};
    const size_t caps[]={sizeof(e->name),sizeof(e->short_description),sizeof(e->long_description),sizeof(e->application)};
    for(unsigned i=0;i<4u;i++) {
        unsigned n=normalized(fields[i],caps[i],field);
        if(!n || generic(field)) continue;
        if(product_n>=4u && !generic(product) && !strcmp(field,product))
            result|=KUI_VMU_IMPORT_MATCH_PRODUCT;
        if(title_n>=6u && !generic(title) && (!strcmp(field,title) ||
           (n>title_n && !strncmp(field,title,title_n) && role_tail(field+title_n))))
            result|=KUI_VMU_IMPORT_MATCH_TITLE | (i==3u?KUI_VMU_IMPORT_MATCH_APPLICATION:0u);
        /* Only a full 32-byte long-description field can be a truncated
         * catalogue title; exact uniqueness is evaluated across ALL games. */
        if(i==2u && length(fields[i],caps[i])==32u && n>=20u && title_n>n &&
           !strncmp(field,title,n)) result|=KUI_VMU_IMPORT_MATCH_TRUNCATED;
        if(family && text_family(field)==family) result|=KUI_VMU_IMPORT_MATCH_ALIAS;
    }
    if(title_n>=6u && !generic(title) && filename_stem(e->name,stem) && !strcmp(stem,title))
        result|=KUI_VMU_IMPORT_MATCH_TITLE | KUI_VMU_IMPORT_MATCH_FAMILY;
    if(family && name_family(e->name)==family)
        result|=KUI_VMU_IMPORT_MATCH_ALIAS | KUI_VMU_IMPORT_MATCH_FAMILY;
    return result;
}
static int compatible(const struct kui_vmu_import_match_game *a,
                      const struct kui_vmu_import_match_game *b) {
    char x[129],y[129];normalized(a->title,sizeof(a->title),x);normalized(b->title,sizeof(b->title),y);
    if(x[0] && !strcmp(x,y)) return 1;
    enum family f=title_family(x);return f && f==title_family(y);
}
static int member(const uint8_t *bits,unsigned i) {return (bits[i/8u]&(1u<<(i%8u)))!=0;}
static int product_shaped(const char *s) {
    unsigned first=0,minimum=0;
    if(s[0]=='T') {first=1u;minimum=4u;}
    else if(starts(s,"MK")) {first=2u;minimum=5u;}
    else if(starts(s,"HDR")) {first=3u;minimum=4u;}
    else return 0;
    unsigned at=first;
    while(s[at]>='0' && s[at]<='9') ++at;
    if(at-first<minimum || at-first>6u) return 0;
    unsigned letters=0;
    while(s[at]>='A' && s[at]<='Z') {++letters;++at;}
    return !s[at] && letters<=2u;
}
static int unknown_product(const struct kui_vmu_import_entry *e,
    const struct kui_vmu_import_match_game *games,size_t count) {
    const char *fields[]={e->name,e->short_description,e->long_description,e->application};
    const size_t caps[]={sizeof(e->name),sizeof(e->short_description),sizeof(e->long_description),sizeof(e->application)};
    for(unsigned i=0;i<4u;i++) {
        char field[33];normalized(fields[i],caps[i],field);
        if(!product_shaped(field)) continue;
        int found=0;
        for(unsigned g=0;g<count;g++) {
            char product[12];normalized(games[g].product,sizeof(games[g].product),product);
            if(!strcmp(field,product)) {found=1;break;}
        }
        if(!found) return 1;
    }
    return 0;
}
static void resolve(const struct kui_vmu_import_plan *plan,const struct kui_vmu_import_match_game *games,
    size_t count,struct kui_vmu_import_match_group *group) {
    uint32_t votes[KUI_VMU_IMPORT_MATCH_MAX_GAMES]={0};
    unsigned product_count=0,product_game=0,total=0;
    memset(group->candidates,0,sizeof(group->candidates));
    for(unsigned g=0;g<count;g++) {
        for(unsigned i=0;i<plan->entry_count;i++) if(member(group->members,i))
            votes[g]|=evidence(&plan->entries[i],&games[g]);
        if(votes[g]) {++total;group->candidates[g/8u]|=(uint8_t)(1u<<(g%8u));}
        if(votes[g]&KUI_VMU_IMPORT_MATCH_PRODUCT) {++product_count;product_game=g;}
        group->reasons|=votes[g];
    }
    group->game_index=KUI_VMU_IMPORT_MATCH_NO_GAME;
    group->state=KUI_VMU_IMPORT_MATCH_CHOOSE_GAME;
    group->candidate_count=total;
    for(unsigned i=0;i<plan->entry_count;i++) if(member(group->members,i)) {
        if(conflicting_family(&plan->entries[i])) {
            group->reasons|=KUI_VMU_IMPORT_MATCH_CONFLICT;return;
        }
        if(unknown_product(&plan->entries[i],games,count)) {
            group->reasons|=KUI_VMU_IMPORT_MATCH_CONFLICT | KUI_VMU_IMPORT_MATCH_UNKNOWN;return;
        }
    }
    if(!total) {group->reasons|=KUI_VMU_IMPORT_MATCH_UNKNOWN;return;}
    unsigned candidate=0;
    if(product_count==1u) {
        for(unsigned g=0;g<count;g++) if(votes[g] && g!=product_game &&
            !compatible(&games[g],&games[product_game])) {
            group->reasons|=KUI_VMU_IMPORT_MATCH_CONFLICT;return;
        }
        candidate=product_game;group->candidate_count=1u;
        memset(group->candidates,0,sizeof(group->candidates));
        group->candidates[candidate/8u]|=(uint8_t)(1u<<(candidate%8u));
    } else if(product_count>1u || total>1u) {
        group->reasons|=KUI_VMU_IMPORT_MATCH_AMBIGUOUS;
        if(product_count>1u) group->reasons|=KUI_VMU_IMPORT_MATCH_CONFLICT;
        return;
    } else while(!votes[candidate]) ++candidate;
    if(!games[candidate].native_eligible) {group->reasons|=KUI_VMU_IMPORT_MATCH_UNSUPPORTED;return;}
    group->game_index=candidate;group->state=KUI_VMU_IMPORT_MATCH_AUTO;
}
static unsigned find(unsigned *parents,unsigned i) {
    while(parents[i]!=i) {parents[i]=parents[parents[i]];i=parents[i];}return i;
}
/* An exact common catalogue title still connects companions when several
 * regional releases share it. The group asks once; it never picks a region. */
static unsigned identity_game(const struct kui_vmu_import_entry *e,
    const struct kui_vmu_import_match_game *games,size_t count) {
    unsigned first=KUI_VMU_IMPORT_MATCH_NO_GAME;char key[129]={0};
    for(unsigned g=0;g<count;g++) if(evidence(e,&games[g])) {
        char title[129];normalized(games[g].title,sizeof(games[g].title),title);
        if(!title[0]) return KUI_VMU_IMPORT_MATCH_NO_GAME;
        if(first==KUI_VMU_IMPORT_MATCH_NO_GAME) {first=g;memcpy(key,title,strlen(title)+1u);}
        else if(strcmp(key,title)) return KUI_VMU_IMPORT_MATCH_NO_GAME;
    }
    return first;
}
static int application_names_game(const struct kui_vmu_import_entry *e,
    const struct kui_vmu_import_match_game *games,size_t count) {
    char x[17];unsigned n=normalized(e->application,sizeof(e->application),x);
    if(n<4u || generic(x)) return 0;
    for(unsigned g=0;g<count;g++) {
        char title[129],product[12];
        unsigned tn=normalized(games[g].title,sizeof(games[g].title),title);
        normalized(games[g].product,sizeof(games[g].product),product);
        if((tn>=6u && !generic(title) && !strcmp(x,title)) ||
           (product[0] && !generic(product) && !strcmp(x,product))) return 1;
    }
    return 0;
}
static int same_application(const struct kui_vmu_import_entry *a,
    const struct kui_vmu_import_entry *b) {
    char x[17],y[17];normalized(a->application,sizeof(a->application),x);
    normalized(b->application,sizeof(b->application),y);return !strcmp(x,y);
}
static int valid(const struct kui_vmu_import_plan *p) {
    if(p->entry_count>200u || p->entry_count!=p->summary.files ||
       p->summary.used_blocks>200u || p->summary.free_blocks!=200u-p->summary.used_blocks ||
       p->summary.game_files>1u) return 0;
    unsigned used=0,games=0;
    for(unsigned i=0;i<p->entry_count;i++) {
        const struct kui_vmu_import_entry *e=&p->entries[i];
        if(e->slot>=208u || !e->blocks || e->blocks>200u || e->header_offset>=e->blocks ||
           (e->type!=0x33 && e->type!=0xcc) ||
           !memchr(e->name,0,sizeof(e->name)) ||
           !memchr(e->short_description,0,sizeof(e->short_description)) ||
           !memchr(e->long_description,0,sizeof(e->long_description)) ||
           !memchr(e->application,0,sizeof(e->application))) return 0;
        for(unsigned j=0;j<i;j++) if(e->slot==p->entries[j].slot) return 0;
        used+=e->blocks;games+=e->type==0xcc;
    }
    return used==p->summary.used_blocks && games==p->summary.game_files;
}
int kui_vmu_import_match(const struct kui_vmu_import_plan *plan,
    const struct kui_vmu_import_match_game *games,size_t game_count,
    struct kui_vmu_import_matches *out) {
    if(!range(plan,sizeof(*plan)) || !range(out,sizeof(*out)) ||
       game_count>KUI_VMU_IMPORT_MATCH_MAX_GAMES ||
       (game_count && !range(games,game_count*sizeof(*games))) ||
       overlap(out,sizeof(*out),plan,sizeof(*plan)) ||
       overlap(out,sizeof(*out),games,game_count*sizeof(*games)) || !valid(plan)) return -1;
    for(unsigned i=0;i<game_count;i++)
        if(!memchr(games[i].title,0,sizeof(games[i].title)) ||
           !memchr(games[i].product,0,sizeof(games[i].product))) return -1;
    memset(out,0,sizeof(*out));
    unsigned parents[208],single_game[208],identities[208],root_group[208];enum family families[208];
    uint8_t named_application[208];
    for(unsigned i=0;i<plan->entry_count;i++) {
        parents[i]=i;families[i]=entry_family(&plan->entries[i]);
        struct kui_vmu_import_match_group one={0};one.members[i/8u]|=(uint8_t)(1u<<(i%8u));
        resolve(plan,games,game_count,&one);single_game[i]=one.game_index;
        identities[i]=identity_game(&plan->entries[i],games,game_count);
        named_application[i]=(uint8_t)application_names_game(&plan->entries[i],games,game_count);
        root_group[i]=KUI_VMU_IMPORT_MATCH_NO_GAME;
    }
    for(unsigned i=0;i<plan->entry_count;i++) for(unsigned j=0;j<i;j++) {
        char a[13],b[13];
        int same_family=families[i] && families[i]==families[j];
        int same_game=single_game[i]!=KUI_VMU_IMPORT_MATCH_NO_GAME && single_game[i]==single_game[j];
        int same_title=identities[i]!=KUI_VMU_IMPORT_MATCH_NO_GAME && identities[i]==identities[j];
        int same_app=named_application[i] && named_application[j] &&
            same_application(&plan->entries[i],&plan->entries[j]);
        /* Unknown same-stem slots are a related group for one user choice,
         * but the stem alone never names a catalogue game. */
        int same_stem=filename_stem(plan->entries[i].name,a) &&
            filename_stem(plan->entries[j].name,b) && !strcmp(a,b);
        if(same_family || same_game || same_title || same_app || same_stem)
            parents[find(parents,i)]=find(parents,j);
    }
    for(unsigned i=0;i<plan->entry_count;i++) {
        unsigned root=find(parents,i),g=root_group[root];
        if(g==KUI_VMU_IMPORT_MATCH_NO_GAME) {
            g=out->group_count++;root_group[root]=g;
            const struct kui_vmu_import_entry *e=&plan->entries[i];
            const char *label=e->short_description[0]?e->short_description:
                e->long_description[0]?e->long_description:e->name;
            size_t n=strlen(label);if(n>=sizeof(out->groups[g].label)) n=sizeof(out->groups[g].label)-1u;
            memcpy(out->groups[g].label,label,n);
        }
        struct kui_vmu_import_match_group *group=&out->groups[g];
        group->members[i/8u]|=(uint8_t)(1u<<(i%8u));++group->files;
        group->blocks+=plan->entries[i].blocks;
        if(group->files>1u) group->reasons|=KUI_VMU_IMPORT_MATCH_FAMILY;
    }
    for(unsigned g=0;g<out->group_count;g++) resolve(plan,games,game_count,&out->groups[g]);
    return 0;
}
