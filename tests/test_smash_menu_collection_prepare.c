/* SPDX-License-Identifier: GPL-3.0-only */
/* Exercise the production collection selector, including its accepted CE
 * fallback, through explicit read-only package/source-proof seams. */
#include "../src/apps/games_retail.c"
#include <assert.h>

static uint8_t native_payload[16384],ce_payload[16384];
static struct kui_runtime_image native_module,ce_module;
static struct kui_game_metadata metadata;
static struct kui_game_image image;
static struct kui_retail_manifest map;
static struct files files;
static struct smash_collection_request request;
static struct kui_runtime_image loaded;
static bool selected,native_selected;
static uint32_t crc;
static const char *path,*problem;
static FRESULT native_stat,ce_stat;
static BYTE attribute;
static enum kui_runtime_result read_result;
static enum kui_game_result proof_result;
static unsigned stats,loads,frees,proofs,closes;
static unsigned resolver_calls,resolved_stats,resolver_fail_ordinal;
static bool resolver_mapped;
bool __wrap_kui_runtime_resolve_path(const char *path,char *out,size_t capacity) {
    ++resolver_calls;if(resolver_calls==resolver_fail_ordinal)return false;
    int bytes=snprintf(out,capacity,"%s%s",resolver_mapped?"resolved:":"",path);
    return bytes>=0 && (size_t)bytes<capacity;
}
static bool close_failure,bad_ip,bad_crc;
static unsigned bad_sha;
static const size_t foreground_at=0x2100u,cap_at=0x2200u,seed_at=0x2300u,return_at=0x2600u;

FRESULT f_stat(const TCHAR *name,FILINFO *info) {
    ++stats;memset(info,0,sizeof(*info));info->fattrib=attribute;
    if(!strncmp(name,"resolved:",9u)) {assert(resolver_mapped);name+=9u;++resolved_stats;}
    else assert(!resolver_mapped);
    if(!strcmp(name,KUI_SMASH_MENU_LOADER_PACKAGE))return native_stat;
    assert(!strcmp(name,KUI_GAMES_RETAIL_PHYSICAL_PACKAGE));return ce_stat;
}
enum kui_runtime_result kui_runtime_read(const char *name,struct kui_runtime_image *out,kui_log_fn log,kui_cancel_fn cancel) {
    assert(!out->data && log && cancel && !files.reader_open);++loads;
    if(read_result!=KUI_RUNTIME_OK)return read_result;
    const struct kui_runtime_image *source=(!strcmp(name,KUI_SMASH_MENU_LOADER_PACKAGE) ||
        !strcmp(name,KUI_SMASH_NATIVE_MENU_PACKAGE))?&native_module:&ce_module;
    if(source==&ce_module)assert(!strcmp(name,KUI_GAMES_RETAIL_PHYSICAL_PACKAGE) ||
        !strcmp(name,KUI_SMASH_CE_VC2_PACKAGE) || !strcmp(name,KUI_SMASH_CE_SWIRL_PACKAGE));
    out->info=source->info;out->data=malloc(out->info.payload_bytes);assert(out->data);
    memcpy(out->data,source->data,out->info.payload_bytes);return KUI_RUNTIME_OK;
}
void kui_runtime_free(struct kui_runtime_image *out) {++frees;free(out->data);memset(out,0,sizeof(*out));}
FRESULT f_close(FIL *file) {(void)file;++closes;return close_failure?FR_DISK_ERR:FR_OK;}
const char *kui_game_result_name(enum kui_game_result result) {(void)result;return "fixture source error";}
static void logger(const char *format,...) {(void)format;}
static bool cancelled(void) {return false;}
static enum kui_game_result proof(const struct smash_collection_request *q,uint32_t *ip,uint8_t sha[32],uint32_t *out_crc) {
    assert(q==&request && !files.reader_open);++proofs;
    static const uint8_t owner[32]={0x3a,0x31,0x1b,0x18,0x9a,0x48,0x70,0xcc,
        0xbe,0xfc,0x4f,0xcd,0x30,0x6e,0x2d,0x38,0xe5,0xae,0x81,0x0a,0xe5,0xed,0xd2,0x25,
        0xaf,0x6b,0x9a,0xa6,0xd5,0xdf,0x5f,0x10};
    *ip=KUI_SMASH_NATIVE_IP_CRC32^(uint32_t)bad_ip;
    *out_crc=KUI_SMASH_NATIVE_MENU_CRC32^(uint32_t)bad_crc;
    memcpy(sha,owner,32u);if(bad_sha)sha[bad_sha-1u]^=1u;return proof_result;
}
static void put(uint8_t *p,uint32_t value) {for(unsigned i=0u;i<4u;i++)p[i]=(uint8_t)(value>>(8u*i));}
static void package_header(uint8_t *payload) {
    uint8_t *h=payload+KUI_RETAIL_HEADER_OFFSET;memcpy(h,KUI_RETAIL_PACKAGE_MAGIC,8u);
    const uint32_t words[]={1u,64u,KUI_RETAIL_MAP_OFFSET,KUI_RETAIL_IMAGE_WIRE_BYTES,
        KUI_RETAIL_STAGE_ADDRESS,8192u,KUI_RETAIL_STAGE_ADDRESS,KUI_RETAIL_EXEC_ADDRESS,
        KUI_RETAIL_EXEC_MAX_BYTES,KUI_RETAIL_STAGE_STACK,KUI_RETAIL_STAGE_BLOB_OFFSET,
        KUI_RETAIL_LOW_RESIDENT_ADDRESS,KUI_RETAIL_LOW_STANDARD_LIMIT,0u};
    for(unsigned i=0u;i<sizeof(words)/sizeof(words[0]);++i)put(h+8u+4u*i,words[i]);
}
static void fixture(void) {
    assert(!loaded.data);
    memset(native_payload,0,sizeof(native_payload));memset(ce_payload,0,sizeof(ce_payload));
    package_header(native_payload);package_header(ce_payload);
    const struct kui_native_foreground_capability foreground={KUI_NATIVE_FOREGROUND_CAP_MAGIC0,
        KUI_NATIVE_FOREGROUND_CAP_MAGIC1,1u,32u,0u,KUI_NATIVE_FOREGROUND_SMASH_SLOTS,0u,KUI_NATIVE_FOREGROUND_CAP_SMASH_MENU};
    memcpy(native_payload+foreground_at,&foreground,sizeof(foreground));
    struct kui_native_foreground_capability ce=foreground;ce.slots=48u;ce.flags=KUI_NATIVE_FOREGROUND_CAP_VC2_PERSIST;
    memcpy(ce_payload+foreground_at,&ce,sizeof(ce));
    const struct kui_smash_menu_loader_capability cap={KUI_SMASH_MENU_LOADER_CAP_MAGIC0,
        KUI_SMASH_MENU_LOADER_CAP_MAGIC1,1u,32u,1u,64u,304u,0u};
    const struct kui_smash_menu_loader_seed seed={KUI_SMASH_MENU_LOADER_SEED_BLANK_WORDS};
    const struct kui_smash_menu_return_seed returned={.magic0=KUI_SMASH_MENU_RETURN_SEED_MAGIC0,
        .magic1=KUI_SMASH_MENU_RETURN_SEED_MAGIC1,.version=1u,.bytes=KUI_SMASH_MENU_RETURN_SEED_BYTES};
    memcpy(native_payload+cap_at,&cap,sizeof(cap));memcpy(native_payload+seed_at,&seed,sizeof(seed));
    memcpy(native_payload+return_at,&returned,sizeof(returned));
    native_module=(struct kui_runtime_image){.data=native_payload,.info={.payload_bytes=sizeof(native_payload),.memory_bytes=sizeof(native_payload)}};
    ce_module=(struct kui_runtime_image){.data=ce_payload,.info={.payload_bytes=sizeof(ce_payload),.memory_bytes=sizeof(ce_payload)}};
    metadata=(struct kui_game_metadata){.ip_valid=true,.boot_valid=true,.native_gd=true,.boot_bytes=KUI_SMASH_NATIVE_MENU_BYTES};
    strcpy(metadata.product,"MK-51146");strcpy(metadata.version,"V1.002");strcpy(metadata.region,"U");strcpy(metadata.bootfile,"1ST_READ.BIN");
    image=(struct kui_game_image){.format=KUI_GAME_IMAGE_GDI};map=(struct kui_retail_manifest){.storage_transport=KUI_STORAGE_SCI};
    memset(&files,0,sizeof(files));request=(struct smash_collection_request){&files,&metadata,&image,&map,true,false,true,0u,0u};
    selected=native_selected=false;crc=0u;path=KUI_GAMES_RETAIL_PACKAGE;problem=NULL;
    native_stat=ce_stat=FR_OK;attribute=0u;read_result=KUI_RUNTIME_OK;proof_result=KUI_GAME_OK;
    stats=loads=frees=proofs=closes=bad_sha=0u;close_failure=bad_ip=bad_crc=false;
    resolver_calls=resolved_stats=resolver_fail_ordinal=0u;resolver_mapped=false;
}
static bool select_collection(void) {
    uint32_t old_ip=map.ip_crc32,old_crc=crc;const char *old_path=path;
    bool ok=smash_collection_select(&request,&loaded,&selected,&native_selected,&crc,&path,&problem,logger,cancelled,proof);
    if(!selected)assert(map.ip_crc32==old_ip && crc==old_crc && path==old_path);
    return ok;
}
static void release(void) {if(loaded.data)kui_runtime_free(&loaded);}
static uint8_t *read_actual(const char *path,struct kui_runtime_image *out) {
    FILE *input=fopen(path,"rb");assert(input && !fseek(input,0,SEEK_END));
    long length=ftell(input);assert(length>64 && !fseek(input,0,SEEK_SET));
    uint8_t header[64];assert(fread(header,1u,sizeof(header),input)==sizeof(header));
    assert(kui_runtime_header(header,(uint64_t)length,&out->info)==KUI_RUNTIME_OK);
    uint8_t *actual=malloc(out->info.payload_bytes);assert(actual);
    assert(fread(actual,1u,out->info.payload_bytes,input)==out->info.payload_bytes && !fclose(input));
    assert(kui_retail_crc32(0u,actual,out->info.payload_bytes)==out->info.crc32);
    out->data=actual;return actual;
}
int main(int argc,char **argv) {
    fixture();assert(select_collection() && selected && native_selected && loaded.data);
    assert(stats==1u && loads==1u && proofs==1u && !strcmp(path,KUI_SMASH_MENU_LOADER_PACKAGE));release();
    fixture();resolver_mapped=true;
    assert(select_collection() && selected && native_selected && resolver_calls==1u && resolved_stats==1u);release();
    fixture();resolver_mapped=true;native_stat=FR_NO_FILE;
    /* A missing explicit bank replacement is a hard refusal. It must not
     * select another root adapter merely because the declared file is gone. */
    assert(!select_collection() && resolver_calls==1u && resolved_stats==1u && !loads && !proofs);release();
    fixture();resolver_fail_ordinal=1u;
    assert(!select_collection() && resolver_calls==1u && !stats && !loads && !proofs);
    fixture();native_stat=FR_NO_FILE;resolver_fail_ordinal=2u;
    assert(!select_collection() && resolver_calls==2u && stats==1u && !loads && !proofs);
    fixture();
    const struct kui_smash_menu_loader_capability cap={KUI_SMASH_MENU_LOADER_CAP_WORDS};
    const struct kui_smash_menu_collection collection={.magic0=KUI_SMASH_MENU_COLLECTION_MAGIC0,
        .magic1=KUI_SMASH_MENU_COLLECTION_MAGIC1,.version=1u,.bytes=KUI_SMASH_MENU_COLLECTION_BYTES,
        .count=KUI_SMASH_MENU_CHILD_COUNT,.flags=KUI_SMASH_MENU_COLLECTION_FLAGS};
    memcpy(native_payload+cap_at,&cap,sizeof(cap));memcpy(native_payload+seed_at,&collection,sizeof(collection));
    assert(select_collection() && selected && native_selected && proofs==1u);release();
    fixture();memcpy(native_payload+cap_at,&cap,sizeof(cap));
    assert(!select_collection() && !proofs);release(); /* v2 cannot masquerade as the old native-only seed */
    fixture();native_stat=FR_NO_FILE;assert(select_collection() && selected && !native_selected);
    assert(stats==2u && loads==1u && proofs==1u && !strcmp(path,KUI_GAMES_RETAIL_PHYSICAL_PACKAGE));release();
    fixture();native_stat=ce_stat=FR_NO_PATH;assert(select_collection() && !selected && !loads && !proofs && stats==2u);
    for(unsigned bad=0u;bad<34u;bad++) {
        fixture();bad_ip=bad==0u;bad_crc=bad==1u;bad_sha=bad>=2u?bad-1u:0u;
        assert(select_collection() && !selected && !native_selected && !loaded.data);
        assert(stats==1u && loads==1u && proofs==1u && frees==1u); /* no second full menu read */
    }
    for(unsigned skip=0u;skip<20u;skip++) {
        fixture();
        switch(skip) {
            case 0:request.native_standard=false;break;
            case 1:request.child=KUI_SMASH_NATIVE_CHILD_PHANTASY_STAR_II;break;
            case 2:request.child=KUI_SMASH_NATIVE_CHILD_SHINING_FORCE;break;
            case 3:request.emulation_enabled=false;break;
            case 4:request.options=KUI_GAMES_RETAIL_CDDA;break;
            case 5:request.patched=true;break;
            case 6:map.storage_transport=KUI_STORAGE_SCIF;break;
            case 7:map.storage_transport=KUI_STORAGE_IDE;break;
            case 8:image.format=KUI_GAME_IMAGE_CUE;break;
            case 9:image.compressed=true;break;
            case 10:image.scrambled=true;break;
            case 11:image.cd_image=true;break;
            case 12:metadata.product[0]='X';break;
            case 13:metadata.version[5]='3';break;
            case 14:metadata.region[0]='E';break;
            case 15:metadata.boot_bytes--;break;
            case 16:metadata.native_gd=false;break;
            case 17:metadata.windows_ce=true;break;
            case 18:metadata.bootfile[0]='X';break;
            default:metadata.ip_valid=false;break;
        }
        assert(select_collection() && !selected && !native_selected && !loaded.data && !stats && !loads && !proofs);
    }
    fixture();native_stat=FR_DISK_ERR;assert(!select_collection() && stats==1u && !loads);
    fixture();attribute=AM_DIR;assert(!select_collection() && !loads);
    const enum kui_runtime_result errors[]={KUI_RUNTIME_IO,KUI_RUNTIME_CHECKSUM,KUI_RUNTIME_CANCELLED};
    for(unsigned i=0u;i<sizeof(errors)/sizeof(errors[0]);++i) {
        fixture();read_result=errors[i];assert(!select_collection() && stats==1u && loads==1u && !proofs);
    }
    fixture();proof_result=KUI_GAME_CANCELLED;assert(!select_collection() && loaded.data && proofs==1u);release();
    fixture();files.reader_open=true;assert(select_collection() && selected && closes==1u && !files.reader_open);release();
    fixture();files.reader_open=true;close_failure=true;assert(!select_collection() && !loads && closes==1u && !files.reader_open);
    const size_t bad_offsets[]={KUI_RETAIL_HEADER_OFFSET,foreground_at+28u,cap_at,cap_at+4u,
        cap_at+8u,cap_at+12u,cap_at+16u,cap_at+20u,cap_at+24u,cap_at+28u,seed_at,seed_at+16u,
        return_at,return_at+16u};
    for(unsigned i=0u;i<sizeof(bad_offsets)/sizeof(bad_offsets[0]);++i) {
        fixture();native_payload[bad_offsets[i]]^=1u;
        assert(!select_collection() && !selected && loaded.data && !proofs && stats==1u);release();
    }
    fixture();memcpy(native_payload+0x2400u,native_payload+cap_at,32u);
    assert(!select_collection() && !proofs);release();
    fixture();memcpy(native_payload+0x2400u,native_payload+seed_at,64u);
    assert(!select_collection() && !proofs);release();
    fixture();memcpy(native_payload+0x2800u,native_payload+return_at,KUI_SMASH_MENU_RETURN_SEED_BYTES);
    assert(!select_collection() && !proofs);release();
    fixture();memmove(native_payload+cap_at+1u,native_payload+cap_at,32u);native_payload[cap_at]=0u;
    assert(!select_collection() && !proofs);release();
    fixture();put(native_payload+foreground_at+20u,64u);put(native_payload+foreground_at+28u,0u);
    assert(!select_collection() && !proofs);release(); /* generic foreground is not the native menu capability */
    fixture();put(native_payload+foreground_at+28u,KUI_NATIVE_FOREGROUND_CAP_VC2_PERSIST);
    assert(!select_collection() && !proofs);release();
    fixture();
    struct kui_smash_menu_capsule_prepared prepared={.runs={20000u,200u,30000u,456u},
        .payload_bytes=233120u,.bytes=233472u,.crc32=UINT32_C(0xa5153e07),.seed_offset=16384u};
    assert(smash_native_menu_seed_prepare(&native_module,&prepared,&problem));
    struct kui_smash_menu_loader_seed sealed;memcpy(&sealed,native_payload+seed_at,sizeof(sealed));
    assert(kui_smash_menu_loader_seed_valid(&sealed) && sealed.payload_bytes==233120u &&
        sealed.padded_bytes==233472u && sealed.payload_crc32==prepared.crc32 &&
        sealed.first_blocks==200u && sealed.second_lba==30000u && !sealed.handoff_crc32 && !sealed.raw_crc32);
    assert(!smash_native_menu_seed_prepare(&native_module,&prepared,&problem)); /* one-time publication */
    fixture();prepared.seed_offset=prepared.payload_bytes;
    assert(!smash_native_menu_seed_prepare(&native_module,&prepared,&problem));
    assert(kui_smash_menu_loader_seed_find(native_payload,sizeof(native_payload))==seed_at);
    fixture();memcpy(native_module.info.build,"a1b2c3d4e5f6",12u);
    assert(smash_menu_capsule_module_read("a1b2c3d4e5f6",&loaded,&problem,logger,cancelled));
    assert(loads==1u && !proofs);release();
    for(unsigned i=0u;i<12u;++i) {
        fixture();memcpy(native_module.info.build,"a1b2c3d4e5f6",12u);native_module.info.build[i]^=1u;
        assert(!smash_menu_capsule_module_read("a1b2c3d4e5f6",&loaded,&problem,logger,cancelled));
        assert(loads==1u && !proofs && strstr(problem,"builds differ"));release();
    }
    if(argc>1) {
        fixture();uint8_t *actual=read_actual(argv[1],&native_module);
        assert(select_collection() && selected && native_selected && stats==1u && proofs==1u);
        release();
        size_t collection=kui_smash_menu_collection_find(actual,native_module.info.payload_bytes);
        const unsigned damaged_words[]={2u,3u,4u,5u,6u,54u,55u};
        if(collection==SIZE_MAX)assert(kui_smash_menu_loader_seed_find(actual,native_module.info.payload_bytes)!=SIZE_MAX);
        else for(unsigned i=0u;i<sizeof(damaged_words)/sizeof(damaged_words[0]);i++) {
            actual[collection+4u*damaged_words[i]]^=1u;
            selected=native_selected=false;stats=loads=proofs=0u;
            assert(!select_collection() && !selected && !native_selected && stats==1u && loads==1u && !proofs);
            release();actual[collection+4u*damaged_words[i]]^=1u;
        }
        free(actual);
        puts("Actual packaged Smash menu loader: production source-bound selection OK");
    }
    if(argc>4)for(unsigned kind=0u;kind<3u;kind++) {
        fixture();struct kui_runtime_image *module=kind?&ce_module:&native_module;
        uint8_t *actual=read_actual(argv[kind+2u],module);
        bool admitted=kind?smash_collection_ce_module_read(module->info.build,kind,&loaded,&problem,logger,cancelled):
            smash_menu_capsule_module_read(module->info.build,&loaded,&problem,logger,cancelled);
        assert(admitted && loaded.data && loads==1u && !proofs);
        size_t child=kind?kui_smash_ce_child_find(loaded.data,loaded.info.payload_bytes,kind):
            kui_smash_native_menu_seed_find(loaded.data,loaded.info.payload_bytes);
        size_t returned=kui_smash_menu_return_seed_find(loaded.data,loaded.info.payload_bytes);
        assert(child!=SIZE_MAX && returned==child+(kind?KUI_SMASH_CE_CHILD_BYTES:KUI_SMASH_NATIVE_MENU_SEED_BYTES));
        release();
        if(kind) {
            /* An exact shipped module with its actual typed seed damaged
             * must not use the validator's literal pair as a fallback. */
            const unsigned words[]={2u,3u,4u,5u,6u};
            for(unsigned i=0u;i<sizeof(words)/sizeof(words[0]);i++) {
                actual[child+4u*words[i]]^=1u;
                assert(!smash_collection_ce_module_read(module->info.build,kind,&loaded,&problem,logger,cancelled));
                release();actual[child+4u*words[i]]^=1u;
            }
        }
        free(actual);
        puts(kind==1u?"Actual packaged VC2 child: production module admission and refusals OK":
            kind==2u?"Actual packaged Swirl child: production module admission and refusals OK":
            "Actual packaged native child: production module admission OK");
    }
    puts("Smash menu collection preparation: OK");return 0;
}
