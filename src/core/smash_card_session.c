/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/smash_card_session.h"
#include "kui/hash.h"
#include "kui/retail_image.h"
#include <string.h>

static bool session_zero(const void *data,size_t bytes) {
    const uint8_t *p=data;for(size_t i=0u;i<bytes;i++)if(p[i])return false;return true;
}
static bool session_overlap(const void *a,size_t an,const void *b,size_t bn) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return an && bn && (x<=y?y-x<an:x-y<bn);
}
static bool session_build(const char build[12]) {
    if(!build)return false;
    for(unsigned i=0u;i<12u;i++)if(!((build[i]>='0' && build[i]<='9') ||
        (build[i]>='a' && build[i]<='f')))return false;
    return true;
}
static bool session_config_header(const struct kui_smash_card_config *c) {
    return c && c->magic0==KUI_SMASH_CARD_CONFIG_MAGIC0 &&
        c->magic1==KUI_SMASH_CARD_CONFIG_MAGIC1 && c->version==1u && c->bytes==sizeof(*c);
}
static uint32_t session_crc(const void *data,size_t bytes,size_t at) {
    const uint32_t zero=0u;
    uint32_t crc=kui_retail_crc32(0u,data,at);crc=kui_retail_crc32(crc,&zero,4u);
    return kui_retail_crc32(crc,(const uint8_t *)data+at+4u,bytes-at-4u);
}
static bool session_runs(const struct kui_smash_menu_capsule_runs *r) {
    if(!r || !r->blocks || !r->first_lba || !r->first_blocks || r->first_blocks>r->blocks ||
       (!!r->second_lba!=(r->first_blocks<r->blocks)) ||
       (uint64_t)r->first_lba+r->first_blocks>UINT64_C(0x100000000) ||
       (uint64_t)r->second_lba+r->blocks-r->first_blocks>UINT64_C(0x100000000))return false;
    return !r->second_lba || (uint64_t)r->first_lba+r->first_blocks<=r->second_lba ||
        (uint64_t)r->second_lba+r->blocks-r->first_blocks<=r->first_lba;
}
static uint32_t session_lba(const struct kui_smash_menu_capsule_runs *r,uint32_t block) {
    return block<r->first_blocks?r->first_lba+block:r->second_lba+block-r->first_blocks;
}
static bool session_maps(const struct kui_smash_card_config *c) {
    if(c->witness_bytes!=KUI_VMU_FS_WITNESS_FILE_BYTES ||
       !session_runs(&c->data) || !session_runs(&c->allocated) ||
       c->data.blocks!=KUI_VMU_FS_WITNESS_FILE_BYTES/512u || c->allocated.blocks<c->data.blocks)return false;
    /* The useful file is the exact logical prefix of its complete allocation,
     * including a possible second physical run and final cluster slack. */
    for(uint32_t i=0u;i<c->data.blocks;i++)
        if(session_lba(&c->data,i)!=session_lba(&c->allocated,i))return false;
    return true;
}
static bool session_profile_maps(const struct kui_smash_card_config *c,
    const struct kui_vmu_persist_profile *p) {
    uint64_t end=p->geometry.data_first_lba+(uint64_t)p->geometry.cluster_count*p->geometry.sectors_per_cluster;
    const struct kui_smash_menu_capsule_runs *r=&c->allocated;
    uint32_t lba[2]={r->first_lba,r->second_lba},blocks[2]={r->first_blocks,r->blocks-r->first_blocks};
    uint32_t spc=p->geometry.sectors_per_cluster;
    for(unsigned i=0u;i<2u;i++)if(blocks[i] &&
       (lba[i]<p->geometry.data_first_lba || (lba[i]-p->geometry.data_first_lba)%spc ||
        blocks[i]%spc || (uint64_t)lba[i]+blocks[i]>end ||
        (uint64_t)lba[i]+blocks[i]>p->partition_end || (uint64_t)lba[i]+blocks[i]>p->card_sectors))return false;
    struct kui_retail_manifest map={.partition_start=(uint32_t)p->partition_first,.partition_end=p->partition_end};
    return kui_smash_menu_capsule_disjoint(&c->allocated,&map,p) &&
        kui_smash_menu_capsule_disjoint(&c->data,&map,p);
}
bool kui_smash_card_config_blank(const struct kui_smash_card_config *c) {
    return session_config_header(c) && session_zero((const uint8_t *)c+16u,sizeof(*c)-16u);
}
size_t kui_smash_card_config_find(const void *payload,size_t bytes) {
    if(!payload || bytes<sizeof(struct kui_smash_card_config) ||
       (uintptr_t)payload>UINTPTR_MAX-bytes)return SIZE_MAX;
    size_t found=SIZE_MAX;const uint8_t *p=payload;
    for(size_t at=0u;at<=bytes-sizeof(struct kui_smash_card_config);at++) {
        uint32_t h[4];memcpy(h,p+at,sizeof(h));
        if(h[0]!=KUI_SMASH_CARD_CONFIG_MAGIC0 || h[1]!=KUI_SMASH_CARD_CONFIG_MAGIC1 ||
           h[2]!=1u || h[3]!=sizeof(struct kui_smash_card_config))continue;
        struct kui_smash_card_config c;memcpy(&c,p+at,sizeof(c));
        if((at&3u) || found!=SIZE_MAX || !kui_smash_card_config_blank(&c))return SIZE_MAX;
        found=at;
    }
    return found;
}
bool kui_smash_card_config_valid(const struct kui_smash_card_config *c) {
    if(!session_config_header(c) || c->role<KUI_SMASH_CARD_ROLE_OUTER || c->role>KUI_SMASH_CARD_ROLE_SWIRL ||
       !c->generation || c->reserved0 || !session_zero(c->reserved,sizeof(c->reserved)) ||
       !session_build(c->build) || session_zero(c->ownership_sha256,32u) || !session_maps(c) ||
       c->crc32!=session_crc(c,sizeof(*c),offsetof(struct kui_smash_card_config,crc32)))return false;
    if(c->role!=KUI_SMASH_CARD_ROLE_OUTER)return session_zero(c->profile_wire,sizeof(c->profile_wire));
    struct kui_vmu_persist_profile p;uint8_t digest[32];
    return !kui_vmu_persist_profile_decode(&p,c->profile_wire,sizeof(c->profile_wire)) &&
        !kui_vmu_persist_profile_ownership_sha256(&p,digest) &&
        !memcmp(digest,c->ownership_sha256,32u) && session_profile_maps(c,&p);
}
bool kui_smash_card_configure(struct kui_smash_card_config *out,unsigned role,uint32_t generation,
    const char build[13],const struct kui_smash_menu_capsule_runs *data,
    const struct kui_smash_menu_capsule_runs *allocated,const struct kui_vmu_persist_profile *profile) {
    if(!out || !data || !allocated || !profile || !session_build(build) ||
       !kui_smash_card_config_blank(out) || !generation ||
       role<KUI_SMASH_CARD_ROLE_OUTER || role>KUI_SMASH_CARD_ROLE_SWIRL ||
       session_overlap(out,sizeof(*out),build,12u) || session_overlap(out,sizeof(*out),data,sizeof(*data)) ||
       session_overlap(out,sizeof(*out),allocated,sizeof(*allocated)) ||
       session_overlap(out,sizeof(*out),profile,sizeof(*profile)))return false;
    struct kui_smash_card_config c=*out;c.role=role;c.generation=generation;
    c.witness_bytes=KUI_VMU_FS_WITNESS_FILE_BYTES;c.data=*data;c.allocated=*allocated;memcpy(c.build,build,12u);
    if(kui_vmu_persist_profile_ownership_sha256(profile,c.ownership_sha256) ||
       !session_maps(&c) || !session_profile_maps(&c,profile) ||
       (role==KUI_SMASH_CARD_ROLE_OUTER && kui_vmu_persist_profile_encode(profile,c.profile_wire)))return false;
    c.crc32=session_crc(&c,sizeof(c),offsetof(struct kui_smash_card_config,crc32));
    if(!kui_smash_card_config_valid(&c))return false;
    *out=c;return true;
}
static void session_hash32(struct kui_sha256 *h,uint32_t value) {
    uint8_t b[4];for(unsigned i=0u;i<4u;i++)b[i]=(uint8_t)(value>>(8u*i));kui_sha256_update(h,b,sizeof(b));
}
static void session_hash_runs(struct kui_sha256 *h,const struct kui_smash_menu_capsule_runs *r) {
    session_hash32(h,r->first_lba);session_hash32(h,r->first_blocks);
    session_hash32(h,r->second_lba);session_hash32(h,r->blocks);
}
static bool session_binding_valid(const struct kui_vmu_fs_witness_binding *b) {
    if(!b || !b->card_sectors || b->card_sectors>UINT64_C(0x100000000) ||
       b->high_capacity>1u || (!b->high_capacity && b->card_sectors>UINT64_C(0x800000)) ||
       b->partition_first>=b->partition_end || b->partition_end>b->card_sectors ||
       !b->generation || session_zero(b->cid,16u) || session_zero(b->image_id,16u) ||
       session_zero(b->binding_sha256,32u))return false;
    unsigned ones=0u;for(unsigned i=0u;i<16u;i++)ones+=b->cid[i]==0xffu;
    return ones!=16u;
}
bool kui_smash_card_binding(const struct kui_smash_card_config *c,
    const struct kui_vmu_persist_profile *p,struct kui_vmu_fs_witness_binding *out) {
    if(!out)return false;
    if((c && session_overlap(c,sizeof(*c),out,sizeof(*out))) ||
       (p && session_overlap(p,sizeof(*p),out,sizeof(*out))))return false;
    memset(out,0,sizeof(*out));
    uint8_t digest[32];
    if(!kui_smash_card_config_valid(c) || !p ||
       kui_vmu_persist_profile_ownership_sha256(p,digest) ||
       memcmp(digest,c->ownership_sha256,32u) || !session_profile_maps(c,p))return false;
    struct kui_vmu_fs_witness_binding b={.card_sectors=p->card_sectors,
        .partition_first=p->partition_first,.partition_end=p->partition_end,
        .high_capacity=p->high_capacity,.generation=c->generation};
    memcpy(b.cid,p->cid,16u);memcpy(b.image_id,p->image_id,16u);
    struct kui_sha256 hash;static const char domain[]="KUI/SMASH/CARD-SESSION/BINDING/V2";
    kui_sha256_init(&hash);kui_sha256_update(&hash,domain,sizeof(domain));
    kui_sha256_update(&hash,c->build,12u);session_hash32(&hash,c->generation);
    session_hash32(&hash,c->witness_bytes);session_hash_runs(&hash,&c->data);session_hash_runs(&hash,&c->allocated);
    kui_sha256_update(&hash,c->ownership_sha256,32u);kui_sha256_digest(&hash,b.binding_sha256);
    if(!session_binding_valid(&b))return false;
    *out=b;return true;
}
static bool session_receipt_header(const struct kui_smash_card_receipt *r) {
    return r && r->magic0==KUI_SMASH_CARD_RECEIPT_MAGIC0 && r->magic1==KUI_SMASH_CARD_RECEIPT_MAGIC1 &&
        r->version==KUI_SMASH_CARD_RECEIPT_VERSION && r->bytes==sizeof(*r);
}
bool kui_smash_card_receipt_blank(const struct kui_smash_card_receipt *r) {
    return session_receipt_header(r) && session_zero((const uint8_t *)r+16u,sizeof(*r)-16u);
}
bool kui_smash_card_receipt_valid(const struct kui_smash_card_receipt *r) {
    if(!session_receipt_header(r))return false;
    unsigned owner=r->flags>>KUI_SMASH_CARD_RECEIPT_OWNER_SHIFT;
    return owner>=KUI_SMASH_CARD_ROLE_OUTER && owner<=KUI_SMASH_CARD_ROLE_SWIRL &&
        r->flags==(KUI_SMASH_CARD_RECEIPT_VALID|(owner<<KUI_SMASH_CARD_RECEIPT_OWNER_SHIFT)) &&
        r->generation && r->sequence &&
        !session_zero(r->binding_sha256,32u) && !session_zero(r->admission_sha256,32u) &&
        r->crc32==session_crc(r,sizeof(*r),offsetof(struct kui_smash_card_receipt,crc32));
}
unsigned kui_smash_card_receipt_owner(const struct kui_smash_card_receipt *r) {
    return kui_smash_card_receipt_valid(r)?r->flags>>KUI_SMASH_CARD_RECEIPT_OWNER_SHIFT:0u;
}
bool kui_smash_card_receipt_transition_valid(const struct kui_smash_card_receipt *r,unsigned next_role) {
    unsigned owner=kui_smash_card_receipt_owner(r);
    return owner && r->sequence!=UINT32_MAX &&
        ((owner==KUI_SMASH_CARD_ROLE_OUTER && next_role>=KUI_SMASH_CARD_ROLE_NATIVE &&
            next_role<=KUI_SMASH_CARD_ROLE_SWIRL) ||
         (owner==KUI_SMASH_CARD_ROLE_NATIVE && next_role==KUI_SMASH_CARD_ROLE_OUTER));
}
bool kui_smash_card_receipt_mint(struct kui_smash_card_receipt *out,
    const struct kui_vmu_fs_witness_binding *binding,const uint8_t admission[32]) {
    if(!out || !admission || !session_binding_valid(binding) || session_zero(admission,32u) ||
       session_overlap(out,sizeof(*out),binding,sizeof(*binding)) ||
       session_overlap(out,sizeof(*out),admission,32u))return false;
    struct kui_smash_card_receipt r={.magic0=KUI_SMASH_CARD_RECEIPT_MAGIC0,
        .magic1=KUI_SMASH_CARD_RECEIPT_MAGIC1,.version=KUI_SMASH_CARD_RECEIPT_VERSION,.bytes=sizeof(r),
        .flags=KUI_SMASH_CARD_RECEIPT_VALID|(KUI_SMASH_CARD_ROLE_OUTER<<KUI_SMASH_CARD_RECEIPT_OWNER_SHIFT),
        .generation=binding->generation,.sequence=1u};
    memcpy(r.binding_sha256,binding->binding_sha256,32u);memcpy(r.admission_sha256,admission,32u);
    r.crc32=session_crc(&r,sizeof(r),offsetof(struct kui_smash_card_receipt,crc32));*out=r;return true;
}
bool kui_smash_card_receipt_transfer(struct kui_smash_card_receipt *out,unsigned next_role) {
    if(!kui_smash_card_receipt_transition_valid(out,next_role))return false;
    struct kui_smash_card_receipt r=*out;++r.sequence;
    r.flags=KUI_SMASH_CARD_RECEIPT_VALID|(next_role<<KUI_SMASH_CARD_RECEIPT_OWNER_SHIFT);
    r.crc32=session_crc(&r,sizeof(r),offsetof(struct kui_smash_card_receipt,crc32));*out=r;return true;
}
bool kui_smash_card_receipt_ce_return_valid(const struct kui_smash_card_receipt *r) {
    unsigned owner=kui_smash_card_receipt_owner(r);
    return (owner==KUI_SMASH_CARD_ROLE_VC2 || owner==KUI_SMASH_CARD_ROLE_SWIRL) &&
        r->sequence!=UINT32_MAX;
}
bool kui_smash_card_receipt_ce_return(struct kui_smash_card_receipt *out) {
    if(!kui_smash_card_receipt_ce_return_valid(out))return false;
    struct kui_smash_card_receipt r=*out;++r.sequence;
    r.flags=KUI_SMASH_CARD_RECEIPT_VALID|(KUI_SMASH_CARD_ROLE_OUTER<<KUI_SMASH_CARD_RECEIPT_OWNER_SHIFT);
    r.crc32=session_crc(&r,sizeof(r),offsetof(struct kui_smash_card_receipt,crc32));*out=r;return true;
}
bool kui_smash_card_receipt_matches(const struct kui_smash_card_receipt *r,
    const struct kui_vmu_fs_witness_binding *binding) {
    return kui_smash_card_receipt_valid(r) && session_binding_valid(binding) &&
        r->generation==binding->generation && !memcmp(r->binding_sha256,binding->binding_sha256,32u);
}
