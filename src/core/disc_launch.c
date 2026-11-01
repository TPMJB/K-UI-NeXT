/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/disc_launch.h"
#include "kui/hash.h"
#include <string.h>

static uint32_t word(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static void put(uint8_t *p,uint32_t v) {
    for(unsigned i=0;i<4u;i++) p[i]=(uint8_t)(v>>(8u*i));
}
static bool disjoint(const void *a,size_t an,const void *b,size_t bn) {
    uintptr_t ap=(uintptr_t)a,bp=(uintptr_t)b;
    return a && b && an<=UINTPTR_MAX-ap && bn<=UINTPTR_MAX-bp &&
        (ap+an<=bp || bp+bn<=ap);
}
static bool geometry(const struct kui_disc_launch_plan *p) {
    if(!p || p->module_bytes<KUI_DISC_LAUNCH_STAGE_OFFSET+4u ||
       p->module_bytes>KUI_DISC_LAUNCH_STAGE_OFFSET+KUI_DISC_LAUNCH_STAGE_MAX_BYTES ||
       (p->module_bytes&3u) || p->exec_bytes<128u ||
       p->exec_bytes>KUI_DISC_LAUNCH_EXEC_MAX_BYTES) return false;
    uint32_t ip=(p->module_bytes+31u)&~31u,exec=ip+KUI_DISC_LAUNCH_IP_BYTES;
    uint32_t total=(exec+p->exec_bytes+3u)&~3u;
    return p->ip_offset==ip && p->exec_offset==exec && p->payload_bytes==total &&
        total<KUI_DISC_LAUNCH_STAGE_ADDRESS-KUI_DISC_LAUNCH_LOAD_ADDRESS &&
        p->exec_bytes<KUI_DISC_LAUNCH_STAGE_ADDRESS-KUI_DISC_LAUNCH_EXEC_ADDRESS;
}
static bool capability(const uint8_t *p,size_t bytes) {
    if(!p || bytes<KUI_DISC_LAUNCH_STAGE_OFFSET+4u ||
       bytes>KUI_DISC_LAUNCH_STAGE_OFFSET+KUI_DISC_LAUNCH_STAGE_MAX_BYTES || (bytes&3u)) return false;
    const uint8_t *h=p+KUI_DISC_LAUNCH_HEADER_OFFSET;
    return !memcmp(h,"KUIDISC1",8u) && word(h+8u)==1u &&
        word(h+12u)==KUI_DISC_LAUNCH_HEADER_BYTES &&
        word(h+16u)==KUI_DISC_LAUNCH_CONFIG_OFFSET &&
        word(h+20u)==KUI_DISC_LAUNCH_CONFIG_BYTES &&
        word(h+24u)==KUI_DISC_LAUNCH_STAGE_ADDRESS &&
        word(h+28u)==bytes-KUI_DISC_LAUNCH_STAGE_OFFSET &&
        word(h+32u)==KUI_DISC_LAUNCH_STAGE_ADDRESS &&
        word(h+36u)==KUI_DISC_LAUNCH_EXEC_ADDRESS &&
        word(h+40u)==KUI_DISC_LAUNCH_EXEC_MAX_BYTES &&
        word(h+44u)==KUI_DISC_LAUNCH_STACK &&
        word(h+48u)==KUI_DISC_LAUNCH_STAGE_OFFSET &&
        word(h+52u)==KUI_DISC_LAUNCH_FLAGS && !word(h+56u) && !word(h+60u);
}
bool kui_disc_launch_layout(const void *module,size_t bytes,uint32_t executable_bytes,
    struct kui_disc_launch_plan *out) {
    if(!out || !disjoint(module,bytes,out,sizeof(*out))) return false;
    memset(out,0,sizeof(*out));
    if(!capability(module,bytes)) return false;
    const uint8_t *p=module;
    for(unsigned i=0;i<KUI_DISC_LAUNCH_CONFIG_BYTES;i++)
        if(p[KUI_DISC_LAUNCH_CONFIG_OFFSET+i]) return false;
    struct kui_disc_launch_plan plan={0};
    plan.module_bytes=(uint32_t)bytes;
    plan.ip_offset=(plan.module_bytes+31u)&~31u;
    plan.exec_offset=plan.ip_offset+KUI_DISC_LAUNCH_IP_BYTES;
    plan.exec_bytes=executable_bytes;
    /* Reject the size before rounding so no input can wrap the addition. */
    if(executable_bytes>KUI_DISC_LAUNCH_EXEC_MAX_BYTES) return false;
    plan.payload_bytes=(plan.exec_offset+executable_bytes+3u)&~3u;
    if(!geometry(&plan)) return false;
    *out=plan;return true;
}
bool kui_disc_launch_encode(uint8_t packet[KUI_DISC_LAUNCH_CONFIG_BYTES],
    const struct kui_disc_launch_plan *p) {
    if(!packet || !disjoint(packet,KUI_DISC_LAUNCH_CONFIG_BYTES,p,sizeof(*p))) return false;
    memset(packet,0,KUI_DISC_LAUNCH_CONFIG_BYTES);
    if(!geometry(p)) return false;
    memcpy(packet,"KUICFGD1",8u);put(packet+8u,1u);put(packet+12u,KUI_DISC_LAUNCH_CONFIG_BYTES);
    put(packet+16u,p->module_bytes);put(packet+20u,p->payload_bytes);
    put(packet+24u,p->ip_offset);put(packet+28u,p->exec_offset);put(packet+32u,p->exec_bytes);
    put(packet+36u,KUI_DISC_LAUNCH_FLAGS);put(packet+40u,p->ip_crc32);put(packet+44u,p->exec_crc32);
    memcpy(packet+48u,p->ip_sha256,32u);memcpy(packet+80u,p->exec_sha256,32u);
    put(packet+112u,kui_crc32(0u,packet,KUI_DISC_LAUNCH_CONFIG_BYTES));return true;
}
bool kui_disc_launch_decode(const uint8_t packet[KUI_DISC_LAUNCH_CONFIG_BYTES],
    struct kui_disc_launch_plan *out) {
    if(!out || !disjoint(packet,KUI_DISC_LAUNCH_CONFIG_BYTES,out,sizeof(*out))) return false;
    memset(out,0,sizeof(*out));
    if(memcmp(packet,"KUICFGD1",8u) || word(packet+8u)!=1u ||
       word(packet+12u)!=KUI_DISC_LAUNCH_CONFIG_BYTES || word(packet+36u)!=KUI_DISC_LAUNCH_FLAGS)
        return false;
    uint8_t copy[KUI_DISC_LAUNCH_CONFIG_BYTES];memcpy(copy,packet,sizeof(copy));
    uint32_t crc=word(copy+112u);put(copy+112u,0u);
    if(kui_crc32(0u,copy,sizeof(copy))!=crc) return false;
    for(unsigned i=116u;i<sizeof(copy);i++) if(copy[i]) return false;
    struct kui_disc_launch_plan p={0};
    p.module_bytes=word(packet+16u);p.payload_bytes=word(packet+20u);
    p.ip_offset=word(packet+24u);p.exec_offset=word(packet+28u);p.exec_bytes=word(packet+32u);
    p.ip_crc32=word(packet+40u);p.exec_crc32=word(packet+44u);
    memcpy(p.ip_sha256,packet+48u,32u);memcpy(p.exec_sha256,packet+80u,32u);
    if(!geometry(&p)) return false;
    *out=p;return true;
}
static bool fingerprint(const uint8_t *p,size_t bytes,uint32_t crc,const uint8_t digest[32]) {
    if(kui_crc32(0u,p,bytes)!=crc) return false;
    struct kui_sha256 s;uint8_t actual[32];
    kui_sha256_init(&s);kui_sha256_update(&s,p,bytes);kui_sha256_digest(&s,actual);
    return !memcmp(actual,digest,sizeof(actual));
}
static bool native_ip(const uint8_t *ip) {
    if(memcmp(ip,"SEGA SEGAKATANA ",16u) || memcmp(ip+37u,"GD-ROM",6u) || ip[63u]!=' ') return false;
    uint32_t peripheral=0u;
    for(unsigned i=56u;i<63u;i++) {
        unsigned c=ip[i];
        if(c>='0' && c<='9') c-='0';
        else if(c>='A' && c<='F') c=c-'A'+10u;
        else if(c>='a' && c<='f') c=c-'a'+10u;
        else return false;
        peripheral=(peripheral<<4)|c;
    }
    /* CE has its own entry and kernel-loading contract. Region letters are
     * deliberately not compared with the console locale. */
    return !(peripheral&1u);
}
bool kui_disc_launch_validate(const void *payload,size_t bytes,struct kui_disc_launch_plan *out) {
    if(!out || !disjoint(payload,bytes,out,sizeof(*out))) return false;
    memset(out,0,sizeof(*out));
    if(bytes<KUI_DISC_LAUNCH_STAGE_OFFSET+4u) return false;
    const uint8_t *p=payload;struct kui_disc_launch_plan plan;
    if(!kui_disc_launch_decode(p+KUI_DISC_LAUNCH_CONFIG_OFFSET,&plan) ||
       bytes!=plan.payload_bytes || !capability(p,plan.module_bytes) ||
       !native_ip(p+plan.ip_offset)) return false;
    for(unsigned i=plan.module_bytes;i<plan.ip_offset;i++) if(p[i]) return false;
    for(uint32_t i=plan.exec_offset+plan.exec_bytes;i<plan.payload_bytes;i++) if(p[i]) return false;
    if(!fingerprint(p+plan.ip_offset,KUI_DISC_LAUNCH_IP_BYTES,plan.ip_crc32,plan.ip_sha256) ||
       !fingerprint(p+plan.exec_offset,plan.exec_bytes,plan.exec_crc32,plan.exec_sha256)) return false;
    *out=plan;return true;
}
