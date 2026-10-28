/* SPDX-License-Identifier: GPL-3.0-only */
/* Included after the repair I/O helpers and before repair_identity.inc.
 * The explicit alternate binding applies only to unchanged copy sources. */
static uint32_t repair_saved_get32(const uint8_t *p) {return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static bool repair_saved_identity_source(struct recovery_copy *s,struct kui_saved_identity_source *source) {
    if(!s->manifest_present || s->legacy || s->in_place)return fail(s,"Saved-anchor fallback requires an unchanged normal capture source and separate-copy repair");
    for(unsigned i=0;i<s->source.plan.count;++i) {
        s->trusted_crc[i]=s->manifest.track[i].crc32;
        memcpy(s->trusted_sha[i],s->manifest.track[i].sha256,32);
    }
    *source=(struct kui_saved_identity_source){.directory=s->directory,.plan=&s->source.plan,
        .files=(const char (*)[KUI_DEST_NAME_CAP])s->source.files,.sectors=s->available,.crc32=s->trusted_crc,
        .sha256=(const uint8_t (*)[32])s->trusted_sha,.capture_identity=s->manifest.identity,.crc_only=s->manifest.crc_only};
    return true;
}
static bool repair_saved_identity_recheck(struct recovery_copy *s) {
    if(!s->saved_identity_active)return fail(s,"No immutable saved-anchor recovery receipt is active");
    char message[128];enum kui_saved_identity_result result=kui_saved_identity_recheck(&s->saved_identity,s->optical_plan,s->optical_ops,s->repair_options->attempts,message);
    if(result!=KUI_SAVED_IDENTITY_MATCH)return result==KUI_SAVED_IDENTITY_STOPPED?false:fail(s,message);
    s->repair->alternate_identity=true;
    return true;
}
static bool repair_saved_identity_discover(struct recovery_copy *s) {
    if(!s->identity_unreadable || !s->repair_options->allow_saved_identity)
        return fail(s,"Exact capture identity is unreadable; enable explicit saved-anchor repair binding to attempt recovery");
    struct kui_saved_identity_source source;if(!repair_saved_identity_source(s,&source))return false;
    char message[128];enum kui_saved_identity_result result=kui_saved_identity_discover(&source,s->optical_ops,s->repair_options->attempts,&s->saved_identity,message);
    if(result!=KUI_SAVED_IDENTITY_MATCH)return result==KUI_SAVED_IDENTITY_STOPPED?false:fail(s,message);
    s->saved_identity_active=true;s->repair->alternate_identity=true;
    snprintf(s->repair->message,sizeof(s->repair->message),"New saved-anchor recovery binding; original capture digest was unreadable");update(s,true);
    return true;
}
static bool repair_saved_identity_nonzero(const uint8_t *data) {
    for(unsigned i=0;i<KUI_SAVED_IDENTITY_BYTES;++i)if(data[i])return true;
    return false;
}
static bool repair_saved_identity_prepare(struct recovery_copy *s) {
    if(!s->manifest_present || s->legacy || s->repair_options->new_copy)return true;
    DIR directory;FRESULT r=f_opendir(&directory,"0:/KUI/repair");
    if(r==FR_NO_PATH)return true;
    if(r!=FR_OK)return fail(s,"Cannot inspect saved repair identity receipts");
    uint8_t selected[512];uint64_t selected_bytes=0;bool found=false;unsigned latest=0;FILINFO info;bool ok=true;
    while((r=f_readdir(&directory,&info))==FR_OK && info.fname[0]) {
        if(cancelled(s)) {ok=false;break;}
        if(!(info.fattrib&AM_DIR) || strlen(info.fname)!=8 || strncmp(info.fname,"job-",4))continue;
        unsigned n=0;bool valid=true;
        for(unsigned i=4;i<8;++i) {if(info.fname[i]<'0' || info.fname[i]>'9') {valid=false;break;}n=n*10u+(unsigned)(info.fname[i]-'0');}
        if(!valid || n<=latest)continue;
        char name[KUI_DEST_PATH_CAP];snprintf(name,sizeof(name),"0:/KUI/repair/%.8s/resume.bin",info.fname);
        FIL file;FRESULT opened=f_open(&file,name,FA_READ);if(opened==FR_NO_FILE)continue;
        if(opened!=FR_OK) {ok=fail(s,"Cannot inspect retained repair identity header");break;}
        uint8_t header[512]={0};uint64_t bytes=f_size(&file);
        unsigned prefix=(unsigned)(bytes>sizeof(header)?sizeof(header):bytes);
        bool read=!prefix || exact_read(s,&file,header,prefix);
        if(!close_file(s,&file,read)) {ok=false;break;}
        /* A failed publication in an unrelated job must not block all new
         * recovery. Keep its bytes untouched. A prefix that cannot identify a
         * full exact folder is not a candidate for automatic source selection. */
        size_t required=128u+strlen(s->directory)+1u;
        if(prefix<required || memcmp(header+128,s->directory,strlen(s->directory)+1))continue;
        uint32_t mode=repair_saved_get32(header+12);
        if(mode<=1u && mode!=(s->in_place?1u:0u))continue;
        /* Select the greatest relevant number first. An older damaged header
         * cannot conceal a later valid transaction; a damaged latest relevant
         * header still fails rather than silently restarting that source. */
        latest=n;selected_bytes=bytes;memcpy(selected,header,sizeof(selected));found=true;
    }
    if(f_closedir(&directory)!=FR_OK || r!=FR_OK)return fail(s,"Cannot finish saved repair identity discovery");
    if(!ok || !found)return ok;
    if(selected_bytes!=512 || memcmp(selected,"KUIRPH2",8) || repair_saved_get32(selected+8)!=2 ||
       repair_saved_get32(selected+12)!=(s->in_place?1u:0u) ||
       repair_saved_get32(selected+508)!=kui_crc32(0,selected,508))
        return fail(s,"Latest matching repair identity header is damaged; retained job preserved");
    if(!repair_saved_identity_nonzero(selected+384))return true;
    if(!s->repair_options->allow_saved_identity)return fail(s,"This repair uses saved recovery anchors; enable saved-anchor binding to Resume");
    if(s->in_place)return fail(s,"Saved-anchor identity recovery currently requires a separate copy");
    if(!kui_saved_identity_decode(selected+384,s->optical_plan,&s->saved_identity))return fail(s,"Retained saved-anchor receipt is invalid");
    struct kui_saved_identity_source source;
    if(!repair_saved_identity_source(s,&source) || !kui_saved_identity_matches_source(&source,&s->saved_identity))return fail(s,"Saved-anchor receipt differs from original source metadata");
    s->saved_identity_active=true;s->repair->alternate_identity=true;
    return repair_saved_identity_recheck(s);
}
