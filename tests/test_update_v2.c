/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/update.h"
#include "kui/hash.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"Updater v2 codec failed %u: %s\n",__LINE__,#x);exit(1);}}while(0)
struct row {const char *path,*build;uint64_t bytes;uint8_t policy;};
static void put32(uint8_t *p,uint32_t n) {for(unsigned i=0u;i<4u;i++)p[i]=(uint8_t)(n>>(8u*i));}
static void put64(uint8_t *p,uint64_t n) {put32(p,(uint32_t)n);put32(p+4u,(uint32_t)(n>>32));}
static void sha(const void *p,size_t n,uint8_t out[32]) {
    struct kui_sha256 hash;kui_sha256_init(&hash);kui_sha256_update(&hash,p,n);kui_sha256_digest(&hash,out);
}
static void reseal_wire(uint8_t *wire,size_t rows) {
    sha(wire+128u,rows,wire+64u);put32(wire+124u,kui_crc32(0u,wire,124u));
}
/* Construct wire independently of the codec's encoder. No payload is needed
 * to test exact descriptor lengths, offsets, identities and policies. */
static uint64_t wire_make(uint8_t *wire,uint32_t version,const struct row *rows,unsigned count) {
    size_t width=version==1u?128u:160u;uint64_t payload=0u;
    memset(wire,0,KUI_UPDATE_RECEIPT_MAX);memcpy(wire,version==1u?"KUIUPD1\0":"KUIUPD2\0",8u);
    put32(wire+8u,version);put32(wire+12u,128u);put32(wire+16u,count);put32(wire+20u,(uint32_t)(width*count));
    memcpy(wire+32u,"abcdef012345",12u);wire[44u]=1u;
    for(unsigned i=0u;i<count;i++) {
        uint8_t *entry=wire+128u+(size_t)i*width;
        memcpy(entry,rows[i].path,strlen(rows[i].path));put64(entry+80u,rows[i].bytes);entry[120u]=rows[i].policy;
        if(rows[i].policy!=KUI_UPDATE_KEEP && rows[i].policy!=KUI_UPDATE_REMOVE)
            sha(rows[i].path,strlen(rows[i].path),entry+88u);
        if(version==2u && rows[i].build)memcpy(entry+128u,rows[i].build,12u);
        payload+=rows[i].bytes;
    }
    put64(wire+24u,payload);reseal_wire(wire,width*count);return 128u+(uint64_t)width*count+payload;
}
static enum kui_update_result decode(const uint8_t *wire,uint64_t bytes,struct kui_update_package *p) {
    enum kui_update_result result=kui_update_header_decode(wire,bytes,p);
    return result==KUI_UPDATE_OK?kui_update_manifest_decode(p,wire+128u,(size_t)p->count*kui_update_entry_bytes(p)):result;
}
static void mixed_builds_and_sealing(void) {
    static const struct row rows[]={
        {"apps/games/current-reader.kui","111111111111",96u,KUI_UPDATE_SELF_KUI},
        {"runtime.kui","222222222222",512u,KUI_UPDATE_SELF_KUI},
        {"apps/music/player.kui","333333333333",64u,KUI_UPDATE_SELF_KUI},
        {"apps/games/test-helper.bin",NULL,17u,KUI_UPDATE_RAW},
        {"assets/fonts/ui/font.bin",NULL,0u,KUI_UPDATE_RAW},
        {"redump.db",NULL,40u,KUI_UPDATE_RAW},
        {"apps/disc/keep.kui",NULL,0u,KUI_UPDATE_KEEP},
        {"apps/games/obsolete.kui",NULL,0u,KUI_UPDATE_REMOVE}
    };
    uint8_t wire[KUI_UPDATE_RECEIPT_MAX],encoded[KUI_UPDATE_RECEIPT_MAX];
    uint64_t file_bytes=wire_make(wire,2u,rows,sizeof(rows)/sizeof(rows[0]));
    struct kui_update_package p,roundtrip;
    CHECK(decode(wire,file_bytes,&p)==KUI_UPDATE_OK);
    CHECK(kui_update_package_version(&p)==2u && kui_update_entry_bytes(&p)==160u && p.count==8u);
    CHECK(!strcmp(p.build,"abcdef012345"));
    CHECK(!strcmp(kui_update_entry_build(p.build,p.entries),"111111111111"));
    CHECK(!strcmp(kui_update_entry_build(p.build,p.entries+1u),"222222222222"));
    CHECK(!kui_update_entry_build(p.build,p.entries+3u));
    uint64_t offset=128u+8u*160u;
    for(unsigned i=0u;i<p.count;i++) {CHECK(p.entries[i].payload_offset==offset);offset+=rows[i].bytes;}
    CHECK(offset==file_bytes);
    size_t written=0u;CHECK(kui_update_receipt_encode(&p,encoded,sizeof(encoded),&written)==KUI_UPDATE_OK);
    CHECK(written==128u+8u*160u && !memcmp(wire,encoded,written));
    CHECK(decode(encoded,file_bytes,&roundtrip)==KUI_UPDATE_OK);
    struct kui_update_pin pin;kui_update_package_pin(&p,&pin);CHECK(kui_update_pin_matches(&pin,&roundtrip));
    CHECK(pin.version==2u);pin.version=1u;CHECK(!kui_update_pin_matches(&pin,&roundtrip));pin.version=2u;
    struct kui_update_entry member;
    CHECK(kui_update_package_member(&p,"apps/disc/new-reader.kui",&member) && member.policy==KUI_UPDATE_KEEP);
    CHECK(kui_update_package_member(&p,"apps/games/obsolete.kui",&member) && member.policy==KUI_UPDATE_REMOVE);
    CHECK(!kui_update_package_member(&p,"saves/game.vmd",&member));
    for(unsigned i=0u;i<p.count;i++)p.entries[i].payload_offset=UINT64_MAX;
    p.payload_bytes=UINT64_MAX;p.file_bytes=UINT64_MAX;memset(p.manifest_sha256,0xff,32u);
    CHECK(kui_update_package_seal(&p)==KUI_UPDATE_OK);
    CHECK(p.file_bytes==file_bytes && !memcmp(p.manifest_sha256,wire+64u,32u));
    CHECK(kui_update_receipt_encode(&p,encoded,sizeof(encoded),&written)==KUI_UPDATE_OK && !memcmp(wire,encoded,written));
    uint8_t old_sha[32];memcpy(old_sha,p.manifest_sha256,32u);strcpy(p.build,"987654abcdef");
    CHECK(kui_update_package_seal(&p)==KUI_UPDATE_OK && !memcmp(old_sha,p.manifest_sha256,32u));
    CHECK(!strcmp(kui_update_entry_build(p.build,p.entries+1u),"222222222222"));
    p.entries[0].bytes=UINT64_MAX;struct kui_update_package unchanged=p;
    CHECK(kui_update_package_seal(&p)==KUI_UPDATE_FORMAT && !memcmp(&p,&unchanged,sizeof(p)));
}
static void reader_only_and_roles(void) {
    static const struct row rows[]={
        {"apps/games/test.kui","012345abcdef",32u,KUI_UPDATE_SELF_KUI},
        {"tools.kui","111111111111",32u,KUI_UPDATE_SELF_KUI},
        {"recovery.kui","222222222222",32u,KUI_UPDATE_SELF_KUI},
        {"assets/a/b/c.bin",NULL,8u,KUI_UPDATE_RAW},
        {"tosec.db",NULL,8u,KUI_UPDATE_RAW}
    };
    uint8_t wire[KUI_UPDATE_RECEIPT_MAX];struct kui_update_package p;
    uint64_t bytes=wire_make(wire,2u,rows,sizeof(rows)/sizeof(rows[0]));
    CHECK(decode(wire,bytes,&p)==KUI_UPDATE_OK && !kui_update_package_find(&p,"runtime.kui"));
    struct kui_update_entry member;CHECK(kui_update_package_member(&p,"runtime.kui",&member));
    CHECK(member.policy==KUI_UPDATE_KEEP && !member.bytes && !member.build[0]);
    CHECK(kui_update_package_find(&p,"tools.kui")==p.entries+1u);
    static const char *const allowed[]={"runtime.kui","tools.kui","recovery.kui","redump.db","tosec.db",
        "apps/games/a-helper.bin","apps/disc/read.kui","apps/music/player-helper.bin","assets/font.bin","assets/a/b/c/d.bin",
        "apps/games/probe.dat","apps/music/song.ogg","apps/music/song.wav",
        "apps/disc/loader-session.bin.kui"};
    static const char *const denied[]={"settings.ini","prefs.ini","saves/a.bin","vmu/a.bin","games/a.bin",
        "updates/a.kui","system/a.bin","apps/games/a/b.kui","assets/../settings.ini","assets/.hidden",
        "assets/a..b.bin","assets/a./b.bin","assets/A.bin","assets/a/b/c/d/e.bin","assets/a//b.bin",
        "assets/a\\b.bin","0:/KUI/runtime.kui","apps/music/../settings.ini","apps/music/a.vmd",
        "apps/games/arbitrary.dat","apps/games/song.ogg","apps/disc/song.wav",
        "apps/games/selection-a.bin","apps/games/selection-b.bin",
        "apps/games/launch-settings-a.bin","apps/games/launch-settings-b.bin",
        "apps/games/vc2-vmu-session.bin","apps/games/smash-vc2-vmu-session.bin",
        "apps/games/smash-swirl-vmu-session.bin","apps/disc/future-loader-session.bin",
        "apps/music/player-settings-a.bin","apps/music/session-b.bin",
        "apps/disc/future-selection-a.bin","apps/games/settings-a.bin",
        "apps/games/a.bin","apps/music/player.bin","apps/games/custom-software.bin",
        "apps/games/binarysettings-a.bin","apps/games/selection-c.bin"};
    for(unsigned i=0u;i<sizeof(allowed)/sizeof(allowed[0]);i++)CHECK(kui_update_module_path_valid(allowed[i]));
    for(unsigned i=0u;i<sizeof(denied)/sizeof(denied[0]);i++) {
        CHECK(!kui_update_module_path_valid(denied[i]));
        for(unsigned policy=KUI_UPDATE_KEEP;policy<=KUI_UPDATE_REMOVE;policy++) {
            if(policy==KUI_UPDATE_SELF_KUI)continue;
            const struct row invalid={denied[i],NULL,policy==KUI_UPDATE_RAW?8u:0u,(uint8_t)policy};
            uint64_t bytes=wire_make(wire,2u,&invalid,1u);
            struct kui_update_package rejected;
            CHECK(decode(wire,bytes,&rejected)!=KUI_UPDATE_OK);
        }
    }
    char path[192];CHECK(kui_update_bank_path(p.build,"assets/a/b/c/d.bin",path,sizeof(path)));
    CHECK(!strcmp(path,"0:/KUI/system/abcdef012345/assets/a/b/c/d.bin"));
}
static void malformed(void) {
    static const struct row rows[]={
        {"apps/games/test.kui","012345abcdef",32u,KUI_UPDATE_SELF_KUI},
        {"apps/games/test-helper.bin",NULL,9u,KUI_UPDATE_RAW}
    };
    uint8_t wire[KUI_UPDATE_RECEIPT_MAX],saved[KUI_UPDATE_RECEIPT_MAX];struct kui_update_package p;
    uint64_t bytes=wire_make(saved,2u,rows,2u);
    static const unsigned reserved[]={121u,127u,140u,159u};
    for(unsigned i=0u;i<sizeof(reserved)/sizeof(reserved[0]);i++) {
        memcpy(wire,saved,sizeof(wire));wire[128u+reserved[i]]=1u;reseal_wire(wire,320u);
        CHECK(decode(wire,bytes,&p)!=KUI_UPDATE_OK);
    }
    static const unsigned policies[]={0u,1u,6u,255u};
    for(unsigned i=0u;i<sizeof(policies)/sizeof(policies[0]);i++) {
        memcpy(wire,saved,sizeof(wire));wire[128u+120u]=(uint8_t)policies[i];reseal_wire(wire,320u);
        CHECK(decode(wire,bytes,&p)!=KUI_UPDATE_OK);
    }
    memcpy(wire,saved,sizeof(wire));wire[128u+128u]='A';reseal_wire(wire,320u);CHECK(decode(wire,bytes,&p)!=KUI_UPDATE_OK);
    memcpy(wire,saved,sizeof(wire));memset(wire+128u+128u,0,12u);reseal_wire(wire,320u);CHECK(decode(wire,bytes,&p)!=KUI_UPDATE_OK);
    memcpy(wire,saved,sizeof(wire));wire[128u+160u+128u]='1';reseal_wire(wire,320u);CHECK(decode(wire,bytes,&p)!=KUI_UPDATE_OK);
    memcpy(wire,saved,sizeof(wire));wire[128u+120u]=KUI_UPDATE_RAW;reseal_wire(wire,320u);CHECK(decode(wire,bytes,&p)!=KUI_UPDATE_OK);
    memcpy(wire,saved,sizeof(wire));wire[128u+160u+120u]=KUI_UPDATE_SELF_KUI;
    memcpy(wire+128u+160u+128u,"012345abcdef",12u);reseal_wire(wire,320u);CHECK(decode(wire,bytes,&p)!=KUI_UPDATE_OK);
    memcpy(wire,saved,sizeof(wire));memcpy(wire+128u+160u,wire+128u,160u);reseal_wire(wire,320u);
    CHECK(decode(wire,bytes,&p)!=KUI_UPDATE_OK);
    memcpy(wire,saved,sizeof(wire));wire[128u+88u]^=1u;CHECK(decode(wire,bytes,&p)==KUI_UPDATE_CHECKSUM);
    memcpy(wire,saved,sizeof(wire));put64(wire+128u+80u,UINT64_MAX);reseal_wire(wire,320u);CHECK(decode(wire,bytes,&p)!=KUI_UPDATE_OK);
    memcpy(wire,saved,sizeof(wire));put32(wire+20u,256u);put32(wire+124u,kui_crc32(0u,wire,124u));
    CHECK(decode(wire,bytes,&p)==KUI_UPDATE_FORMAT);
    memcpy(wire,saved,sizeof(wire));put32(wire+8u,1u);put32(wire+124u,kui_crc32(0u,wire,124u));
    CHECK(decode(wire,bytes,&p)==KUI_UPDATE_FORMAT);
    memcpy(wire,saved,sizeof(wire));put32(wire+16u,65u);put32(wire+20u,65u*160u);put32(wire+124u,kui_crc32(0u,wire,124u));
    CHECK(decode(wire,bytes,&p)==KUI_UPDATE_FORMAT);
    static const struct row bad[]={{"runtime.kui",NULL,0u,KUI_UPDATE_REMOVE}};
    bytes=wire_make(wire,2u,bad,1u);CHECK(decode(wire,bytes,&p)!=KUI_UPDATE_OK);
    static const struct row keep[]={{"runtime.kui",NULL,0u,KUI_UPDATE_KEEP}};
    bytes=wire_make(wire,2u,keep,1u);CHECK(decode(wire,bytes,&p)==KUI_UPDATE_OK);
    wire[128u+88u]=1u;reseal_wire(wire,160u);CHECK(decode(wire,bytes,&p)!=KUI_UPDATE_OK);
}
static void v1_compatibility_and_engine(void) {
    static const struct row rows[]={{"runtime.kui",NULL,32u,KUI_UPDATE_REPLACE},
        {"apps/disc/current.kui",NULL,0u,KUI_UPDATE_INHERIT_ROOT}};
    uint8_t wire[KUI_UPDATE_RECEIPT_MAX],encoded[KUI_UPDATE_RECEIPT_MAX];struct kui_update_package p;
    uint64_t bytes=wire_make(wire,1u,rows,2u);CHECK(decode(wire,bytes,&p)==KUI_UPDATE_OK);
    CHECK(kui_update_package_version(&p)==1u && kui_update_entry_bytes(&p)==128u);
    CHECK(!strcmp(kui_update_entry_build(p.build,p.entries),p.build));
    p.version=0u;CHECK(kui_update_package_version(&p)==1u && kui_update_entry_bytes(&p)==128u);
    size_t written=0u;CHECK(kui_update_receipt_encode(&p,encoded,sizeof(encoded),&written)==KUI_UPDATE_OK);
    CHECK(written==384u && !memcmp(encoded,wire,written));
    CHECK(kui_update_package_seal(&p)==KUI_UPDATE_OK && !memcmp(p.manifest_sha256,wire+64u,32u));
    struct kui_update_entry member;CHECK(kui_update_package_member(&p,"apps/games/a-helper.bin",&member) &&
        member.policy==KUI_UPDATE_INHERIT_ROOT);
    struct kui_update_pin pin;kui_update_package_pin(&p,&pin);CHECK(pin.version==1u);pin.version=0u;
    CHECK(kui_update_pin_matches(&pin,&p));pin.version=2u;CHECK(!kui_update_pin_matches(&pin,&p));
    static const struct row missing[]={{"apps/disc/current.kui",NULL,32u,KUI_UPDATE_REPLACE}};
    bytes=wire_make(wire,1u,missing,1u);CHECK(decode(wire,bytes,&p)!=KUI_UPDATE_OK);
    static const uint8_t marker[16]=KUI_UPDATE_ENGINE_INITIALIZER;uint8_t payload[64]={0};
    CHECK(!kui_update_engine_level(NULL,64u) && !kui_update_engine_level(marker,15u));
    for(unsigned i=0u;i<16u;i++) {
        memset(payload,0,sizeof(payload));memcpy(payload+i,marker,16u);CHECK(kui_update_engine_level(payload,sizeof(payload))==KUI_UPDATE_ENGINE_CURRENT);
        payload[i+15u]^=1u;CHECK(!kui_update_engine_level(payload,sizeof(payload)));
    }
    memcpy(payload,marker,16u);memcpy(payload+32u,marker,16u);CHECK(!kui_update_engine_level(payload,sizeof(payload)));
    CHECK(strstr(kui_update_result_name(KUI_UPDATE_ENGINE),"Upgrade")!=NULL);
}
static void capacity(void) {
    struct row *rows=calloc(KUI_UPDATE_MAX_ENTRIES,sizeof(*rows));
    char (*names)[KUI_UPDATE_PATH_BYTES]=calloc(KUI_UPDATE_MAX_ENTRIES,sizeof(*names));
    uint8_t *wire=malloc(KUI_UPDATE_RECEIPT_MAX),*encoded=malloc(KUI_UPDATE_RECEIPT_MAX);
    struct kui_update_package *p=malloc(sizeof(*p));CHECK(rows && names && wire && encoded && p);
    for(unsigned i=0u;i<KUI_UPDATE_MAX_ENTRIES;i++) {
        snprintf(names[i],sizeof(names[i]),"apps/games/reader-%03u.kui",i);
        rows[i]=(struct row){names[i],"012345abcdef",32u,KUI_UPDATE_SELF_KUI};
    }
    uint64_t bytes=wire_make(wire,2u,rows,65u);CHECK(decode(wire,bytes,p)==KUI_UPDATE_OK && p->count==65u);
    struct kui_update_pin pin;kui_update_package_pin(p,&pin);CHECK(pin.version==2u && kui_update_pin_matches(&pin,p));
    bytes=wire_make(wire,2u,rows,KUI_UPDATE_MAX_ENTRIES);
    CHECK(decode(wire,bytes,p)==KUI_UPDATE_OK && p->count==256u);
    size_t written=0u;CHECK(kui_update_receipt_encode(p,encoded,KUI_UPDATE_RECEIPT_MAX,&written)==KUI_UPDATE_OK);
    CHECK(written==KUI_UPDATE_RECEIPT_MAX && !memcmp(wire,encoded,written));
    CHECK(kui_update_package_seal(p)==KUI_UPDATE_OK);
    p->count=257u;CHECK(kui_update_package_seal(p)==KUI_UPDATE_LIMIT);
    p->version=1u;p->count=65u;CHECK(kui_update_package_seal(p)==KUI_UPDATE_FORMAT);
    put32(wire+16u,257u);put32(wire+20u,257u*160u);put32(wire+124u,kui_crc32(0u,wire,124u));
    CHECK(decode(wire,bytes+160u,p)==KUI_UPDATE_LIMIT);
    memcpy(wire,"KUIUPD1\0",8u);put32(wire+8u,1u);put32(wire+16u,65u);put32(wire+20u,65u*128u);
    put32(wire+124u,kui_crc32(0u,wire,124u));CHECK(kui_update_header_decode(wire,128u+65u*128u+256u*32u,p)==KUI_UPDATE_FORMAT);
    CHECK(!strcmp(kui_update_result_name(KUI_UPDATE_LIMIT),"Too many software files for this updater"));
    free(p);free(encoded);free(wire);free(names);free(rows);
}
static void full_release_flags(void) {
    static const struct row rows[]={{"runtime.kui","012345abcdef",128u,KUI_UPDATE_SELF_KUI},
        {"assets/fonts/current.bin",NULL,8u,KUI_UPDATE_RAW}};
    uint8_t wire[KUI_UPDATE_RECEIPT_MAX],encoded[KUI_UPDATE_RECEIPT_MAX];struct kui_update_package p;
    uint64_t bytes=wire_make(wire,2u,rows,2u);CHECK(decode(wire,bytes,&p)==KUI_UPDATE_OK && !p.flags);
    struct kui_update_pin overlay_pin;kui_update_package_pin(&p,&overlay_pin);CHECK(!overlay_pin.flags);
    wire[45u]=KUI_UPDATE_FULL;put32(wire+124u,kui_crc32(0u,wire,124u));
    CHECK(decode(wire,bytes,&p)==KUI_UPDATE_OK && p.flags==KUI_UPDATE_FULL);
    CHECK(!kui_update_pin_matches(&overlay_pin,&p));
    struct kui_update_pin pin;kui_update_package_pin(&p,&pin);CHECK(pin.flags==KUI_UPDATE_FULL && kui_update_pin_matches(&pin,&p));
    pin.flags=0u;CHECK(!kui_update_pin_matches(&pin,&p));pin.flags=KUI_UPDATE_FULL;
    struct kui_update_entry member;CHECK(kui_update_package_member(&p,"tools.kui",&member) && member.policy==KUI_UPDATE_REMOVE);
    CHECK(kui_update_package_member(&p,"apps/games/old-reader.kui",&member) && member.policy==KUI_UPDATE_REMOVE);
    CHECK(kui_update_package_member(&p,"apps/music/old.ogg",&member) && member.policy==KUI_UPDATE_REMOVE);
    CHECK(kui_update_package_member(&p,"redump.db",&member) && member.policy==KUI_UPDATE_REMOVE);
    CHECK(kui_update_package_member(&p,"assets/old.bin",&member) && member.policy==KUI_UPDATE_REMOVE);
    CHECK(kui_update_package_member(&p,"runtime.kui",&member) && member.policy==KUI_UPDATE_SELF_KUI);
    CHECK(kui_update_package_seal(&p)==KUI_UPDATE_OK && p.flags==KUI_UPDATE_FULL && kui_update_pin_matches(&pin,&p));
    size_t written=0u;CHECK(kui_update_receipt_encode(&p,encoded,sizeof(encoded),&written)==KUI_UPDATE_OK);
    CHECK(encoded[45u]==KUI_UPDATE_FULL && !memcmp(wire,encoded,written));
    p.flags=4u;CHECK(kui_update_package_seal(&p)==KUI_UPDATE_FORMAT);
    CHECK(kui_update_receipt_encode(&p,encoded,sizeof(encoded),&written)==KUI_UPDATE_FORMAT);
    p.flags=KUI_UPDATE_FULL;p.version=1u;CHECK(kui_update_package_seal(&p)==KUI_UPDATE_FORMAT);
    static const uint8_t invalid_flags[]={4u,5u,255u};
    for(unsigned i=0u;i<sizeof(invalid_flags);i++) {
        wire[45u]=invalid_flags[i];put32(wire+124u,kui_crc32(0u,wire,124u));CHECK(decode(wire,bytes,&p)==KUI_UPDATE_FORMAT);
    }
    wire[45u]=KUI_UPDATE_FULL;wire[46u]=1u;put32(wire+124u,kui_crc32(0u,wire,124u));CHECK(decode(wire,bytes,&p)==KUI_UPDATE_FORMAT);
    static const struct row legacy[]={{"runtime.kui",NULL,128u,KUI_UPDATE_REPLACE}};
    bytes=wire_make(wire,1u,legacy,1u);wire[45u]=KUI_UPDATE_FULL;put32(wire+124u,kui_crc32(0u,wire,124u));
    CHECK(decode(wire,bytes,&p)==KUI_UPDATE_FORMAT);
    static const struct row reader[]={{"apps/games/test.kui","012345abcdef",32u,KUI_UPDATE_SELF_KUI}};
    bytes=wire_make(wire,2u,reader,1u);wire[45u]=KUI_UPDATE_FULL;put32(wire+124u,kui_crc32(0u,wire,124u));
    CHECK(decode(wire,bytes,&p)==KUI_UPDATE_RUNTIME);
    wire[45u]=0u;put32(wire+124u,kui_crc32(0u,wire,124u));CHECK(decode(wire,bytes,&p)==KUI_UPDATE_OK);
    p.flags=KUI_UPDATE_FULL;CHECK(kui_update_package_seal(&p)==KUI_UPDATE_RUNTIME);
    static const struct row keep[]={{"runtime.kui","012345abcdef",128u,KUI_UPDATE_SELF_KUI},
        {"apps/games/keep.kui",NULL,0u,KUI_UPDATE_KEEP}};
    bytes=wire_make(wire,2u,keep,2u);wire[45u]=KUI_UPDATE_FULL;put32(wire+124u,kui_crc32(0u,wire,124u));
    CHECK(decode(wire,bytes,&p)!=KUI_UPDATE_OK);
}
int main(void) {
    mixed_builds_and_sealing();reader_only_and_roles();malformed();v1_compatibility_and_engine();capacity();full_release_flags();
    printf("Updater v2 codec: %u checks passed\n",checks);return 0;
}
