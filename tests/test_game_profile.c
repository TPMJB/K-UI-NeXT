/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/game_profile.h"
#include "kui/hash.h"
#include "kui/wince_cdda_profiles.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static struct kui_game_profile_pack pack,decoded,before;
static uint8_t wire[KUI_GAME_PROFILE_PACK_MAX],changed[KUI_GAME_PROFILE_PACK_MAX];
static size_t wire_bytes;
static unsigned opens,closes,logs;
static int file_mode;
FRESULT f_open(FIL *file,const TCHAR *path,BYTE mode) {
    assert(!strcmp(path,KUI_GAME_PROFILE_PACK_PATH) && mode==FA_READ);++opens;
    if(file_mode==1)return FR_NO_FILE;
    if(file_mode==2)return FR_NO_PATH;
    if(file_mode==3)return FR_DISK_ERR;
    memset(file,0,sizeof(*file));file->obj.objsize=file_mode==7?KUI_GAME_PROFILE_PACK_MAX+1u:wire_bytes;
    return FR_OK;
}
FRESULT f_read(FIL *file,void *out,UINT bytes,UINT *got) {
    (void)file;assert(bytes<=wire_bytes);*got=file_mode==4?bytes-1u:bytes;
    memcpy(out,wire,*got);return file_mode==5?FR_DISK_ERR:FR_OK;
}
FRESULT f_close(FIL *file){(void)file;++closes;return file_mode==6?FR_DISK_ERR:FR_OK;}
static void log_line(const char *format,...){(void)format;++logs;}
static void digest(uint8_t out[32],const char *hex) {
    for(unsigned i=0;i<32u;i++){unsigned n=0;assert(sscanf(hex+2u*i,"%2x",&n)==1);out[i]=(uint8_t)n;}
}
static struct kui_game_profile_identity cv(void) {
    struct kui_game_profile_identity id={.transport=KUI_STORAGE_SCI,.format=KUI_GAME_IMAGE_GDI,.boot_bytes=3915142u,.native_gd=true};
    strcpy(id.product,"T1204N");strcpy(id.version,"V1.000");
    strcpy(id.region,"U");
    digest(id.boot_sha256,"199b72af9bcd7fb80d401d4645c731e595970a0dcbcfd98e1bec187f4382fbbb");return id;
}
static void seed_pack(void) {
    memset(&pack,0,sizeof(pack));pack.count=1u;
    struct kui_game_profile_row *r=&pack.rows[0];r->identity=cv();
    strcpy(r->identity.product,"T-TEST");r->identity.ip_crc32=UINT32_C(0x12345678);
    strcpy(r->name,"Local experimental setup");strcpy(r->note,"User-supplied; requires console testing.");
    kui_game_launch_settings_default(&r->recommended);r->recommended.reader=KUI_GAME_READER_BACKGROUND;
    r->recommended.vmu_slot=KUI_GAME_VMU_SLOT_A1;
}
static void metadata_and_evidence(void) {
    struct kui_game_profile_identity id=cv();struct kui_game_profile_match match;
    assert(kui_game_profile_candidate(&id,NULL) && kui_game_profile_query(&id,NULL,&match));
    assert(match.matched && match.automatic && match.status==KUI_GAME_PROFILE_TESTED);
    assert(strstr(match.evidence,"native-code-veronica-console") && !strcmp(match.tested_build,"2322fda6f92e"));
    assert(match.recommended.reader==KUI_GAME_READER_BACKGROUND && !match.recommended.cdda);
    assert(strstr(match.note,"current build"));
    for(unsigned i=0;i<32u;i++) {
        id=cv();id.boot_sha256[i]^=1u;
        assert(kui_game_profile_candidate(&id,NULL));
        assert(!kui_game_profile_query(&id,NULL,&match) && !match.matched);
    }
    id=cv();id.boot_bytes--;assert(!kui_game_profile_candidate(&id,NULL));
    id=cv();strcpy(id.product,"T1204J");assert(!kui_game_profile_candidate(&id,NULL));
    id=cv();strcpy(id.version,"V1.001");assert(!kui_game_profile_query(&id,NULL,&match));
    id=cv();strcpy(id.region,"J");assert(!kui_game_profile_query(&id,NULL,&match));
    id=cv();id.windows_ce=true;assert(!kui_game_profile_query(&id,NULL,&match));
    id=cv();id.native_gd=false;assert(!kui_game_profile_query(&id,NULL,&match));
    id=cv();id.transport=KUI_STORAGE_SCIF;assert(!kui_game_profile_query(&id,NULL,&match));
    id=cv();id.transport=KUI_STORAGE_IDE;assert(!kui_game_profile_query(&id,NULL,&match));
    id=cv();id.format=KUI_GAME_IMAGE_ISO;assert(!kui_game_profile_candidate(&id,NULL));
    id=cv();memset(id.product,'x',sizeof(id.product));assert(!kui_game_profile_candidate(&id,NULL));
    id=cv();memset(id.version,'x',sizeof(id.version));assert(!kui_game_profile_query(&id,NULL,&match));
    id=cv();id.product[8]='x';assert(!kui_game_profile_query(&id,NULL,&match));
    assert(!kui_game_profile_query(NULL,NULL,&match) && !match.matched);
    assert(!kui_game_profile_query(&id,NULL,NULL));
    /* Existing exact-disc geometry candidate is mandatory for audio rows;
     * a known kernel alone does not select a tested game release. */
    static const uint32_t sizes[]={1253376u,1830912u,1110016u,748444u};
    static const uint32_t ips[]={0x684f8040u,0x8fb049c5u,0x86bbb334u,0x38ba2868u};
    for(unsigned i=0;i<4u;i++) {
        id=cv();id.audio_candidate=i+1u;id.boot_bytes=sizes[i];id.ip_crc32=ips[i];id.windows_ce=i<3u;id.native_gd=i==3u;
        if(i==3u)digest(id.boot_sha256,"ee68a326da964fec5892b7226311f2293fd3a0bbf5bac496ec82991964d49bbd");
        else memcpy(id.boot_sha256,kui_wince_cdda_kernel_sha((enum kui_wince_cdda_profile)(i+1u)),32u);
        assert(kui_game_profile_query(&id,NULL,&match) && match.recommended.cdda);
        assert(match.recommended.reader==(i==3u?KUI_GAME_READER_STANDARD:KUI_GAME_READER_BACKGROUND));
        if(i==3u)assert(strstr(match.note,"FMV stalls"));
        id.ip_crc32^=1u;assert(!kui_game_profile_query(&id,NULL,&match));id.ip_crc32^=1u;
        id.audio_candidate=0u;assert(!kui_game_profile_query(&id,NULL,&match));
    }
    puts("PASS game profiles: exact executable, revision, format, transport, full IP and existing audio geometry gate; historical evidence only");
}
static void explicit_choices(void) {
    struct kui_game_profile_identity id=cv();struct kui_game_profile_match match;
    assert(kui_game_profile_query(&id,NULL,&match));
    struct kui_game_launch_settings baseline,saved,out;
    kui_game_launch_settings_default(&baseline);saved=baseline;saved.vmu_slot=KUI_GAME_VMU_SLOT_A1;
    saved.patches.entries[0]=(struct kui_game_ram_patch){.enabled=true,.address=0x8c010004u,.value=7u};
    assert(kui_game_profile_resolve(&match,&baseline,&saved,true,&out));
    assert(out.reader==KUI_GAME_READER_STANDARD && !out.cdda && out.vmu_slot==KUI_GAME_VMU_SLOT_A1);
    assert(out.patches.entries[0].enabled && out.patches.entries[0].value==7u);
    assert(kui_game_profile_settings_status(&match,&out,true)==KUI_GAME_PROFILE_CUSTOM);
    assert(kui_game_profile_resolve(&match,&baseline,NULL,false,&out) && out.reader==KUI_GAME_READER_BACKGROUND);
    assert(kui_game_profile_settings_status(&match,&out,false)==KUI_GAME_PROFILE_TESTED);
    assert(kui_game_profile_apply(&match,&out) && !out.patches.entries[0].enabled);
    saved=out;saved.reader=99u;
    assert(!kui_game_profile_resolve(&match,&baseline,&saved,true,&out) && out.reader==KUI_GAME_READER_BACKGROUND);
    match.matched=false;assert(!kui_game_profile_apply(&match,&out));
    assert(kui_game_profile_resolve(&match,&baseline,NULL,false,&out) && out.reader==KUI_GAME_READER_STANDARD);
    seed_pack();id=pack.rows[0].identity;assert(kui_game_profile_query(&id,&pack,&match));
    assert(!match.automatic && match.status==KUI_GAME_PROFILE_EXPERIMENTAL);
    assert(kui_game_profile_resolve(&match,&baseline,NULL,false,&out) && out.reader==KUI_GAME_READER_STANDARD);
    assert(kui_game_profile_apply(&match,&out) && out.vmu_slot==KUI_GAME_VMU_SLOT_A1);
    assert(kui_game_profile_settings_status(&match,&out,true)==KUI_GAME_PROFILE_EXPERIMENTAL);
    assert(!strcmp(kui_game_profile_status_name(KUI_GAME_PROFILE_CUSTOM),"Custom"));
    assert(!strcmp(kui_game_profile_status_name(KUI_GAME_PROFILE_TESTED),"Tested setup"));
    puts("PASS game profiles: explicit Standard/Off/A1/patch choices preserved; automatic historical defaults and opt-in imported recommendation");
}
static void reseal(uint8_t *record,size_t bytes) {
    uint32_t crc=kui_crc32(0u,record,bytes-4u);
    for(unsigned i=0;i<4u;i++)record[bytes-4u+i]=(uint8_t)(crc>>(8u*i));
}
static void corrupt_packs(void) {
    seed_pack();assert(kui_game_profile_pack_encode(&pack,wire,sizeof(wire),&wire_bytes));
    assert(kui_game_profile_pack_decode(&decoded,wire,wire_bytes));
    assert(decoded.count==1u && decoded.rows[0].recommended.vmu_slot==KUI_GAME_VMU_SLOT_A1);
    before=decoded;
    for(size_t length=0;length<wire_bytes;length++) {
        assert(!kui_game_profile_pack_decode(&decoded,wire,length));assert(!memcmp(&decoded,&before,sizeof(before)));
    }
    for(size_t at=0;at<wire_bytes;at++) {
        memcpy(changed,wire,wire_bytes);changed[at]^=1u;
        assert(!kui_game_profile_pack_decode(&decoded,changed,wire_bytes));
    }
    /* Resealed malformed inputs are rejected independently of their CRC. */
    static const unsigned offsets[]={8u,12u,16u,20u,24u,32u+217u,32u+218u,32u+220u,32u+221u,32u+231u};
    for(unsigned i=0;i<sizeof(offsets)/sizeof(offsets[0]);i++) {
        memcpy(changed,wire,wire_bytes);changed[offsets[i]]=255u;reseal(changed,wire_bytes);
        assert(!kui_game_profile_pack_decode(&decoded,changed,wire_bytes));
    }
    memcpy(changed,wire,wire_bytes);memset(changed+32u,'x',17u);reseal(changed,wire_bytes);
    assert(!kui_game_profile_pack_decode(&decoded,changed,wire_bytes));
    memcpy(changed,wire,wire_bytes);changed[32u+176u]=KUI_STORAGE_SCIF;reseal(changed,wire_bytes);
    assert(!kui_game_profile_pack_decode(&decoded,changed,wire_bytes));
    memcpy(changed,wire,wire_bytes);changed[32u+180u]=KUI_GAME_IMAGE_ISO;reseal(changed,wire_bytes);
    assert(!kui_game_profile_pack_decode(&decoded,changed,wire_bytes));
    pack.rows[1]=pack.rows[0];pack.count=2u;size_t size=123u;
    memset(changed,0xa5,sizeof(changed));assert(!kui_game_profile_pack_encode(&pack,changed,sizeof(changed),&size));
    assert(size==123u && changed[0]==0xa5u);
    pack.count=KUI_GAME_PROFILE_PACK_ROWS+1u;assert(!kui_game_profile_pack_encode(&pack,changed,sizeof(changed),&size));
    seed_pack();pack.rows[0].recommended.patches.entries[0].enabled=true;
    assert(!kui_game_profile_pack_encode(&pack,changed,sizeof(changed),&size));
    seed_pack();pack.rows[0].recommended.encoding=KUI_GAME_ENCODING_PLAIN;
    assert(!kui_game_profile_pack_encode(&pack,changed,sizeof(changed),&size));
    seed_pack();pack.rows[0].recommended.cdda=true;
    assert(!kui_game_profile_pack_encode(&pack,changed,sizeof(changed),&size));
    pack.rows[0].recommended.reader=KUI_GAME_READER_STANDARD;
    assert(kui_game_profile_pack_encode(&pack,changed,sizeof(changed),&size));
    assert(kui_game_profile_pack_decode(&decoded,changed,size) && decoded.rows[0].recommended.cdda);
    seed_pack();pack.rows[0].identity.windows_ce=true;pack.rows[0].identity.native_gd=false;
    pack.rows[0].recommended.reader=KUI_GAME_READER_STANDARD;
    assert(!kui_game_profile_pack_encode(&pack,changed,sizeof(changed),&size));
    pack.rows[0].recommended.reader=KUI_GAME_READER_EAGER;
    assert(!kui_game_profile_pack_encode(&pack,changed,sizeof(changed),&size));
    pack.rows[0].recommended.reader=KUI_GAME_READER_BACKGROUND;
    assert(kui_game_profile_pack_encode(&pack,changed,sizeof(changed),&size));
    assert(kui_game_profile_pack_decode(&decoded,changed,size) && decoded.rows[0].identity.windows_ce);
    size=123u;
    seed_pack();assert(!kui_game_profile_pack_encode(&pack,changed,wire_bytes-1u,&size) && size==123u);
    pack.count=0u;assert(kui_game_profile_pack_encode(&pack,changed,sizeof(changed),&size) && size==36u);
    assert(kui_game_profile_pack_decode(&decoded,changed,size) && !decoded.count);
    puts("PASS game profile packs: round trip, every truncated/bit-corrupt byte, resealed invalid fields, reserved/tested flags, duplicate keys, bounds and no patch imports");
}
static void file_adapter(void) {
    seed_pack();assert(kui_game_profile_pack_encode(&pack,wire,sizeof(wire),&wire_bytes));
    file_mode=0;opens=closes=logs=0u;
    assert(kui_game_profile_pack_load(&decoded,log_line) && decoded.count==1u && opens==1u && closes==1u);
    for(file_mode=1;file_mode<=7;file_mode++) {
        unsigned oldclose=closes;
        bool ok=kui_game_profile_pack_load(&decoded,log_line);
        assert(ok==(file_mode<=2) && !decoded.count);
        assert(closes==oldclose+(file_mode>=4));
    }
    file_mode=0;wire[72u]^=1u;
    assert(!kui_game_profile_pack_load(&decoded,log_line) && !decoded.count && logs>=5u);
    assert(!kui_game_profile_pack_load(NULL,log_line));
    puts("PASS game profile pack reader: read-only path, missing pack, I/O/short-read/close/size/corruption failures, handles closed and built-ins remain available");
}
int main(void) {metadata_and_evidence();explicit_choices();corrupt_packs();file_adapter();return 0;}
