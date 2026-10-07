/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/cdda_disc.h"
#include <string.h>
#include <limits.h>

static bool safe_name(const char name[KUI_CDDA_DISC_NAME_CAP]) {
    unsigned n=0;
    for(;n<KUI_CDDA_DISC_NAME_CAP && name[n];n++) {
        unsigned ch=(unsigned char)name[n];
        if(ch<32u || ch>126u || ch=='/' || ch=='\\' || ch==':' || ch=='*' ||
           ch=='?' || ch=='"' || ch=='<' || ch=='>' || ch=='|') return false;
    }
    return n && n<KUI_CDDA_DISC_NAME_CAP && name[0]!='.' && name[0]!=' ' &&
        name[n-1u]!='.' && name[n-1u]!=' ';
}
static unsigned lower(unsigned ch) {return ch>='A' && ch<='Z'?ch+('a'-'A'):ch;}
static bool same_name(const char *a,const char *b) {
    while(*a && *b) if(lower((unsigned char)*a++)!=lower((unsigned char)*b++)) return false;
    return *a==*b;
}
static enum kui_cdda_disc_result valid_track(const struct kui_cdda_disc_track *t) {
    if(!t->number || t->number>KUI_CDDA_DISC_TRACK_MAX || !safe_name(t->name) ||
       (t->control!=0u && t->control!=4u)) return KUI_CDDA_DISC_INVALID;
    if(t->start_fad<150u || t->start_fad>=t->end_fad || t->end_fad>720000u)
        return KUI_CDDA_DISC_RANGE;
    if(t->control==0u ? t->stride!=2352u || t->data_offset!=0u :
       (t->stride==2048u ? t->data_offset!=0u :t->stride!=2352u || t->data_offset!=16u))
        return KUI_CDDA_DISC_UNSUPPORTED;
    uint64_t span=(uint64_t)(t->end_fad-t->start_fad)*t->stride;
    if(!t->file_bytes || t->file_offset>=t->file_bytes || span>t->file_bytes-t->file_offset)
        return KUI_CDDA_DISC_FILE_SIZE;
    return KUI_CDDA_DISC_OK;
}
enum kui_cdda_disc_result kui_cdda_disc_validate(const struct kui_cdda_disc_map *m) {
    if(!m || !m->count || m->count>KUI_CDDA_DISC_TRACK_MAX || (!m->complete && m->count!=1u))
        return KUI_CDDA_DISC_INVALID;
    for(uint32_t i=0;i<m->count;i++) {
        const struct kui_cdda_disc_track *t=&m->tracks[i];
        enum kui_cdda_disc_result r=valid_track(t);if(r!=KUI_CDDA_DISC_OK) return r;
        if(!m->cd_image && t->start_fad<KUI_CDDA_DISC_HIGH_FAD && t->end_fad>KUI_CDDA_DISC_HIGH_FAD)
            return KUI_CDDA_DISC_RANGE;
        if(m->complete && t->number!=i+1u) return KUI_CDDA_DISC_INVALID;
        if(!m->complete && t->control!=0u) return KUI_CDDA_DISC_UNSUPPORTED;
        if(i && (m->tracks[i-1u].number>=t->number || m->tracks[i-1u].end_fad>t->start_fad))
            return KUI_CDDA_DISC_OVERLAP;
        for(uint32_t j=0;j<i;j++) if(same_name(t->name,m->tracks[j].name)) {
            const struct kui_cdda_disc_track *p=&m->tracks[j];
            uint64_t t_end=(uint64_t)t->file_offset+(uint64_t)(t->end_fad-t->start_fad)*t->stride;
            uint64_t p_end=(uint64_t)p->file_offset+(uint64_t)(p->end_fad-p->start_fad)*p->stride;
            if(t->file_bytes!=p->file_bytes || (t->file_offset<p_end && p->file_offset<t_end))
                return KUI_CDDA_DISC_OVERLAP;
        }
    }
    return KUI_CDDA_DISC_OK;
}
enum kui_cdda_disc_result kui_cdda_disc_from_image(const struct kui_game_image *image,struct kui_cdda_disc_map *out) {
    if(!image || !out || !image->count || image->count>KUI_CDDA_DISC_TRACK_MAX ||
       !image->files.stat || !image->files.read || image->format<KUI_GAME_IMAGE_GDI || image->format>KUI_GAME_IMAGE_RAW)
        return KUI_CDDA_DISC_INVALID;
    struct kui_cdda_disc_map m={.count=image->count,.complete=true,.cd_image=image->cd_image};
    for(uint32_t i=0;i<m.count;i++) {
        const struct kui_game_image_track *s=&image->tracks[i];
        if(s->file_bytes>UINT32_MAX || s->file_offset>UINT32_MAX) return KUI_CDDA_DISC_FILE_SIZE;
        if(s->start_lba>=KUI_GAME_LBA_LIMIT || s->end_lba>KUI_GAME_LBA_LIMIT) return KUI_CDDA_DISC_RANGE;
        if((s->control==0u && s->sector_mode!=0u) || (s->control==4u && s->sector_mode!=1u))
            return KUI_CDDA_DISC_UNSUPPORTED;
        struct kui_cdda_disc_track *t=&m.tracks[i];
        *t=(struct kui_cdda_disc_track){.number=s->number,.start_fad=s->start_lba+150u,.end_fad=s->end_lba+150u,
            .control=s->control,.stride=s->sector_bytes,.data_offset=s->data_offset,
            .file_bytes=(uint32_t)s->file_bytes,.file_offset=(uint32_t)s->file_offset};
        memcpy(t->name,s->name,sizeof(t->name));
    }
    enum kui_cdda_disc_result r=kui_cdda_disc_validate(&m);if(r==KUI_CDDA_DISC_OK) *out=m;
    return r;
}
enum kui_cdda_disc_result kui_cdda_disc_selected_audio(const void *gdi,size_t bytes,
    uint32_t number,uint64_t actual_file_bytes,struct kui_cdda_disc_map *out) {
    if(!out || !number || number>KUI_CDDA_DISC_TRACK_MAX || !actual_file_bytes || actual_file_bytes>UINT32_MAX)
        return KUI_CDDA_DISC_INVALID;
    struct kui_game_image layout;
    enum kui_game_result parsed=kui_game_gdi_layout(gdi,bytes,&layout);
    if(parsed!=KUI_GAME_OK) return parsed==KUI_GAME_UNSUPPORTED?KUI_CDDA_DISC_UNSUPPORTED:KUI_CDDA_DISC_INVALID;
    if(number>layout.count) return KUI_CDDA_DISC_UNSUPPORTED;
    const struct kui_game_image_track *s=&layout.tracks[number-1u];
    if(s->control!=0u || s->sector_bytes!=2352u) return KUI_CDDA_DISC_UNSUPPORTED;
    if(s->file_offset>=actual_file_bytes) return KUI_CDDA_DISC_FILE_SIZE;
    uint64_t available=actual_file_bytes-s->file_offset;
    for(uint32_t i=number;i<layout.count;i++) if(same_name(s->name,layout.tracks[i].name)) {
        if(layout.tracks[i].file_offset>actual_file_bytes) return KUI_CDDA_DISC_FILE_SIZE;
        available=layout.tracks[i].file_offset-s->file_offset;break;
    }
    if(!available || available%2352u) return KUI_CDDA_DISC_FILE_SIZE;
    uint64_t sectors=available/2352u;
    if(sectors>KUI_GAME_LBA_LIMIT-s->start_lba) return KUI_CDDA_DISC_RANGE;
    if(number<layout.count && sectors>layout.tracks[number].start_lba-s->start_lba)
        return KUI_CDDA_DISC_OVERLAP;
    struct kui_cdda_disc_track selected={.number=number,.start_fad=s->start_lba+150u,
        .end_fad=s->start_lba+150u+(uint32_t)sectors,.control=0u,.stride=2352u,
        .file_bytes=(uint32_t)actual_file_bytes,.file_offset=(uint32_t)s->file_offset};
    memcpy(selected.name,s->name,sizeof(selected.name));
    enum kui_cdda_disc_result r=valid_track(&selected);
    if(r!=KUI_CDDA_DISC_OK) return r;
    if(selected.start_fad<KUI_CDDA_DISC_HIGH_FAD && selected.end_fad>KUI_CDDA_DISC_HIGH_FAD)
        return KUI_CDDA_DISC_RANGE;
    /* Only one backed extent is published. Keep a track-sized temporary rather
     * than another99-track map alongside the existing GDI parser's image. */
    memset(out,0,sizeof(*out));out->count=1u;out->tracks[0]=selected;
    return KUI_CDDA_DISC_OK;
}
enum kui_cdda_disc_result kui_cdda_disc_track(const struct kui_cdda_disc_map *m,uint32_t number,
    const struct kui_cdda_disc_track **out) {
    if(!out) return KUI_CDDA_DISC_INVALID;
    enum kui_cdda_disc_result r=kui_cdda_disc_validate(m);if(r!=KUI_CDDA_DISC_OK) return r;
    for(uint32_t i=0;i<m->count;i++) if(m->tracks[i].number==number) {*out=&m->tracks[i];return KUI_CDDA_DISC_OK;}
    return m->complete?KUI_CDDA_DISC_RANGE:KUI_CDDA_DISC_UNSUPPORTED;
}
static enum kui_cdda_disc_result containing(const struct kui_cdda_disc_map *m,uint32_t first,uint32_t end,
    const struct kui_cdda_disc_track **out) {
    enum kui_cdda_disc_result r=kui_cdda_disc_validate(m);if(r!=KUI_CDDA_DISC_OK) return r;
    if(first>=end || first<150u || end>720000u) return KUI_CDDA_DISC_RANGE;
    for(uint32_t i=0;i<m->count;i++) {
        const struct kui_cdda_disc_track *t=&m->tracks[i];
        if(first>=t->start_fad && first<t->end_fad) {
            if(end>t->end_fad) return KUI_CDDA_DISC_RANGE;
            *out=t;return KUI_CDDA_DISC_OK;
        }
    }
    if(!m->complete) return KUI_CDDA_DISC_UNSUPPORTED;
    return first<m->tracks[0].start_fad || first>=m->tracks[m->count-1u].end_fad?
        KUI_CDDA_DISC_RANGE:KUI_CDDA_DISC_GAP;
}
enum kui_cdda_disc_result kui_cdda_disc_audio_range(const struct kui_cdda_disc_map *m,
    uint32_t first,uint32_t end,struct kui_cdda_disc_audio *out) {
    if(!out) return KUI_CDDA_DISC_INVALID;
    const struct kui_cdda_disc_track *t;
    enum kui_cdda_disc_result r=containing(m,first,end,&t);if(r!=KUI_CDDA_DISC_OK) return r;
    if(t->control!=0u) return KUI_CDDA_DISC_UNSUPPORTED;
    *out=(struct kui_cdda_disc_audio){.track=t->number,.first_frame=(first-t->start_fad)*588u,
        .end_frame=(end-t->start_fad)*588u,.source={.file_bytes=t->file_bytes,.backing_offset=t->file_offset,
        .sector_stride=t->stride,.sectors=t->end_fad-t->start_fad,.big_endian=false}};
    return KUI_CDDA_DISC_OK;
}
enum kui_cdda_disc_result kui_cdda_disc_data_range(const struct kui_cdda_disc_map *m,
    uint32_t first,uint32_t count,struct kui_cdda_disc_data *out) {
    if(!out || !count || count>UINT32_MAX-first) return KUI_CDDA_DISC_INVALID;
    const struct kui_cdda_disc_track *t;
    enum kui_cdda_disc_result r=containing(m,first,first+count,&t);if(r!=KUI_CDDA_DISC_OK) return r;
    if(t->control!=4u) return KUI_CDDA_DISC_UNSUPPORTED;
    uint32_t relative=first-t->start_fad;
    *out=(struct kui_cdda_disc_data){.track=t->number,.offset=relative*2048u,.bytes=count*2048u,
        .file_offset=t->file_offset+relative*t->stride+t->data_offset,.stride=t->stride,.data_offset=t->data_offset};
    return KUI_CDDA_DISC_OK;
}
static void write32(uint8_t *out,uint32_t word) {
    for(unsigned i=0;i<4u;i++) out[i]=(uint8_t)(word>>(i*8u));
}
enum kui_cdda_disc_result kui_cdda_disc_toc(const struct kui_cdda_disc_map *m,uint32_t area,uint8_t output[408]) {
    if(!output || area>1u) return KUI_CDDA_DISC_INVALID;
    enum kui_cdda_disc_result r=kui_cdda_disc_validate(m);if(r!=KUI_CDDA_DISC_OK) return r;
    if(!m->complete || (m->cd_image && area)) return KUI_CDDA_DISC_UNSUPPORTED;
    const struct kui_cdda_disc_track *first=NULL,*last=NULL;
    for(uint32_t i=0;i<m->count;i++) {
        const struct kui_cdda_disc_track *t=&m->tracks[i];
        if(m->cd_image || (t->start_fad>=KUI_CDDA_DISC_HIGH_FAD)==(area==1u)) {
            if(!first) first=t;
            last=t;
        }
    }
    if(!first) return KUI_CDDA_DISC_UNSUPPORTED;
    /* All failures precede output publication. Entries stay keyed by actual
     * track number, including high-density tracks whose numbers start at3. */
    uint8_t toc[KUI_CDDA_DISC_TOC_BYTES];
    for(unsigned i=0;i<99u;i++) write32(toc+i*4u,UINT32_MAX);
    for(uint32_t i=0;i<m->count;i++) {
        const struct kui_cdda_disc_track *t=&m->tracks[i];
        if(t->number>=first->number && t->number<=last->number)
            write32(toc+(t->number-1u)*4u,(t->control<<28)|0x01000000u|t->start_fad);
    }
    write32(toc+396u,(first->control<<28)|0x01000000u|(first->number<<16));
    write32(toc+400u,(last->control<<28)|0x01000000u|(last->number<<16));
    write32(toc+404u,(last->control<<28)|0x01000000u|last->end_fad);
    memcpy(output,toc,sizeof(toc));
    return KUI_CDDA_DISC_OK;
}
