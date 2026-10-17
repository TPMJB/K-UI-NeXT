/* SPDX-License-Identifier: GPL-3.0-only */
/* An isolated exact-Toy passive Maple observer. No virtual device or storage
 * writes are installed by this target. The normal retail stage is unchanged. */
#if !KUI_VMU_MAPLE_OBSERVE || !KUI_RETAIL_LOW_RESIDENT || defined(KUI_RETAIL_CE) || defined(KUI_RETAIL_ASYNC) || KUI_RETAIL_TOY_PILOT
#error VMU observation stage requires its separate synchronous native build
#endif
#define kui_retail_stage_relay kui_vmu_observe_base_relay
#define kui_retail_stage_main kui_vmu_observe_base_main
#include "retail_stage.c"
#undef kui_retail_stage_relay
#undef kui_retail_stage_main
#include "kui/hash.h"
#include "kui/vmu_maple_observe.h"
#include "kui/vmu_maple_observe_boot.h"
#include "kui/vmu_toy_observe_admission.h"
#include "kui/retail_observe.h"
#include "vmu_maple_observe_symbols.h"

#ifndef KUI_VMU_OBSERVE_WORKER_BIN
#error The independent observer worker binary must be embedded explicitly
#endif
__asm__(".section .rodata.vmu_observe_worker,\"a\"\n"
        ".balign 32\n"
        ".global __vmu_observe_worker_blob_start\n"
        "__vmu_observe_worker_blob_start:\n"
        ".incbin \"" KUI_VMU_OBSERVE_WORKER_BIN "\"\n"
        ".global __vmu_observe_worker_blob_end\n"
        "__vmu_observe_worker_blob_end:\n.previous\n");
extern const uint8_t observer_blob[] __asm__("__vmu_observe_worker_blob_start");
extern const uint8_t observer_blob_end[] __asm__("__vmu_observe_worker_blob_end");
extern const uint8_t __retail_stage_bss_end[] __asm__("__retail_stage_bss_end");

struct observer_witness {uint32_t address,value;uint16_t bytes,kind;};
enum { OBS_CHECK,OBS_HEAP_SLOT,OBS_RESET_CALLBACK };
/* Scalar witnesses in the owner image, never an SDK or game code payload. */
static const struct observer_witness witnesses[]={
    {KUI_VMU_TOY_HEAP_SLOT,KUI_VMU_TOY_HEAP_INIT,4,OBS_HEAP_SLOT},
    {0x8c04e9acu,0x0d000000u,4,OBS_CHECK},
    {0x8c04e9b0u,0x80000000u,4,OBS_CHECK},
    {0x8c04e916u,0xd327u,2,OBS_CHECK},
    {0x8c04e920u,0x430bu,2,OBS_CHECK},
    {0x8c04e922u,0x7420u,2,OBS_CHECK},
    {0x8c027a24u,0x8c027104u,4,OBS_RESET_CALLBACK},
    {0x8c02798eu,0x2322u,2,OBS_CHECK},
    {0x8c04f1eeu,0x430bu,2,OBS_CHECK},
    {0x8c0c5bc4u,0x00000105u,4,OBS_CHECK},
    {0x8c0bf264u,0x9028u,2,OBS_CHECK},
    {0x8c0bf266u,0x63e2u,2,OBS_CHECK},
    {0x8c0bf268u,0x023eu,2,OBS_CHECK},
    {0x8c0bf26au,0x12c6u,2,OBS_CHECK},
    {0x8c0bf26cu,0x9029u,2,OBS_CHECK},
    {0x8c0bf26eu,0x63e2u,2,OBS_CHECK},
    {0x8c0b533eu,0x400eu,2,OBS_CHECK},
    {0x8c0b5340u,0xd231u,2,OBS_CHECK},
    {0x8c0b5342u,0x420bu,2,OBS_CHECK},
    {0x8c0b5354u,0x4d11u,2,OBS_CHECK},
    {0x8c0b5356u,0x8901u,2,OBS_CHECK},
    {0x8c0b535cu,0xd32bu,2,OBS_CHECK},
    {0x8c0b535eu,0x430bu,2,OBS_CHECK},
    {0x8c0b5360u,0x0009u,2,OBS_CHECK},
    {0x8c0b5408u,0x8c0bf322u,4,OBS_CHECK},
    {KUI_VMU_MAPLE_OBSERVE_COMPLETION_SLOT,KUI_VMU_MAPLE_OBSERVE_CONSUMER,4,OBS_CHECK},
    {0x8c0bf33au,0x5036u,2,OBS_CHECK},
    {0x8c0bf33cu,0xc801u,2,OBS_CHECK},
    {0x8c0bf33eu,0x8902u,2,OBS_CHECK},
    {0x8c0bf34au,0x401bu,2,OBS_CHECK},
    {0x8c0bf34cu,0x8906u,2,OBS_CHECK},
    {0x8c0bf35cu,0xe000u,2,OBS_CHECK},
    {0x8c0bf3acu,0x420bu,2,OBS_CHECK},
    {0x8c0bf414u,0x8c0bfebeu,4,OBS_CHECK},
    {0x8c0bf3d2u,0x430bu,2,OBS_CHECK},
    {0x8c0bf418u,0x8c0bff84u,4,OBS_CHECK},
    {0x8c0b5394u,0x430bu,2,OBS_CHECK},
    {0x8c0b541cu,0x8c0bf364u,4,OBS_CHECK},
    {0x8c0b5470u,0x410bu,2,OBS_CHECK},
    {0x8c0b54b6u,0x400eu,2,OBS_CHECK},
    {0x8c0b4db6u,0x11e6u,2,OBS_CHECK},
    {0x8c0b4e44u,0x80000200u,4,OBS_CHECK},
    {0x8c0b4d2au,0x8b19u,2,OBS_CHECK},
    {0x8c0b4f68u,0x8c0b4d10u,4,OBS_CHECK},
};
static const uint8_t executable_sha[32]={
    0xee,0x68,0xa3,0x26,0xda,0x96,0x4f,0xec,0x58,0x92,0xb7,0x22,0x63,0x11,0xf2,0x29,
    0x3f,0xd3,0xa0,0xbb,0xf5,0xba,0xc4,0x96,0xec,0x82,0x99,0x19,0x64,0xd4,0x9b,0xbd};

static volatile struct kui_vmu_observe_boot_control *observe_control(void) {
    return (volatile struct kui_vmu_observe_boot_control *)(uintptr_t)
        (KUI_VMU_OBSERVE_LOW_CONTROL+UINT32_C(0x20000000));
}
static uint32_t observe_read(uint32_t address,unsigned bytes) {
    return bytes==2u?*(const volatile uint16_t *)(uintptr_t)address:
        *(const volatile uint32_t *)(uintptr_t)address;
}
static void observe_write(uint32_t address,unsigned bytes,uint32_t value) {
    if(bytes==2u) *(volatile uint16_t *)(uintptr_t)address=(uint16_t)value;
    else *(volatile uint32_t *)(uintptr_t)address=value;
}

/* Exact span publication from P2, preserving the game's cache mode. */
void kui_vmu_observe_stage_publish(uint32_t begin,uint32_t end);
__asm__(".section .text.vmu_observe_stage_publish,\"ax\"\n.align 2\n"
        ".global _kui_vmu_observe_stage_publish\n"
        "_kui_vmu_observe_stage_publish:\n"
        "mov.l 1f,r0\njmp @r0\nnop\n2:\n"
        "mov #-32,r0\nand r0,r4\n3:\ncmp/hs r5,r4\nbt 4f\n"
        "ocbp @r4\nadd #32,r4\nbra 3b\nnop\n4:\n"
        "mov.l 5f,r0\nmov.l @r0,r1\nmov.l 6f,r2\nor r2,r1\nmov.l r1,@r0\n"
        "nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nrts\nnop\n.align 2\n"
        "1: .long 2b+0x20000000\n5: .long 0xff00001c\n6: .long 0x00000800\n.previous\n");
static void observe_publish(uint32_t begin,uint32_t end) {
    kui_vmu_observe_stage_publish(begin,end);
}
static void observe_fail(uint32_t reason,uint32_t detail) __attribute__((noreturn));
static void observe_fail(uint32_t reason,uint32_t detail) {
    volatile struct kui_vmu_observe_boot_control *c=observe_control();
    c->status=KUI_VMU_OBS_BOOT_FAILED;c->error=reason;
    c->installer=0;c->stage_stack=0;c->terminal=0;
    retail_display_restore(&display);
    retail_display_hex("TOY MAPLE OBSERVER REFUSED",reason);
    retail_display_hex("DETAIL",detail);
    stopped("PASSIVE OBSERVER: PHOTO SCREEN",reason);
}
static void observe_witnesses(bool installed) {
    for(unsigned i=0;i<sizeof(witnesses)/sizeof(witnesses[0]);i++) {
        const struct observer_witness *w=&witnesses[i];
        uint32_t expected=w->value;
        if(installed && w->kind==OBS_HEAP_SLOT) expected=KUI_VMU_OBSERVE_LOW_HEAP_HOOK;
        if(installed && w->kind==OBS_RESET_CALLBACK) expected=KUI_VMU_OBSERVE_LOW_RETURN_HOOK;
        if(observe_read(w->address,w->bytes)!=expected) observe_fail(4,w->address);
    }
}
static uint32_t observe_p1(uint32_t address) {
    return address>=UINT32_C(0xac000000) && address<UINT32_C(0xad000000)?
        address-UINT32_C(0x20000000):address;
}
static bool observe_code(uint32_t address,uint32_t bytes) {
    return !(address&1u) && address>=KUI_VMU_TOY_LEASE_BEGIN+sizeof(struct kui_vmu_maple_observe_exports) &&
        address<KUI_VMU_TOY_LEASE_BEGIN+bytes;
}
static void observe_exports(struct kui_vmu_maple_observe_exports *e,uint32_t bytes) {
    if(bytes<sizeof(*e) || bytes>0x10000u) observe_fail(2,bytes);
    memcpy(e,observer_blob,sizeof(*e));
    uint32_t bss_begin=observe_p1(e->bss_begin),bss_end=observe_p1(e->bss_end);
    uint32_t stack_bottom=observe_p1(e->stack_bottom),stack_top=observe_p1(e->stack_top);
    if(e->magic!=KUI_VMU_MAPLE_OBSERVE_MAGIC || e->version!=KUI_VMU_MAPLE_OBSERVE_VERSION ||
       e->bytes!=sizeof(*e) || e->lease_bytes!=KUI_VMU_TOY_LEASE_BYTES ||
       !observe_code(e->initialize,bytes) || !observe_code(e->hook,bytes) ||
       !observe_code(e->terminal,bytes) || !observe_code(e->complete_hook,bytes) ||
       bss_begin<KUI_VMU_TOY_LEASE_BEGIN+bytes || bss_begin>bss_end ||
       bss_end>stack_bottom || stack_bottom>stack_top || stack_top!=e->worker_end ||
       stack_top>KUI_VMU_TOY_WORKER_END ||
       e->stack_top-e->stack_bottom!=KUI_VMU_MAPLE_OBSERVE_STACK_BYTES ||
       e->bss_begin!=bss_begin+UINT32_C(0x20000000) ||
       e->bss_end!=bss_end+UINT32_C(0x20000000) ||
       e->stack_bottom!=stack_bottom+UINT32_C(0x20000000) ||
       e->stack_top!=stack_top+UINT32_C(0x20000000) ||
       ((bss_begin|bss_end|stack_bottom|stack_top)&31u) ||
       e->snapshot<e->bss_begin || e->snapshot>=e->bss_end ||
       sizeof(struct kui_vmu_maple_observe_snapshot)>e->bss_end-e->snapshot || (e->snapshot&3u))
        observe_fail(3,e->worker_end);
}

/* This conservative full-file scan catches every encoded direct branch and
 * aligned RAM pointer into the hook interior. It cannot establish indirect
 * dynamic control flow; live observation is still required. */
static bool observe_no_interior_references(void) {
    const uint8_t *data=(const uint8_t *)(uintptr_t)KUI_RETAIL_EXEC_ADDRESS;
    for(uint32_t at=0;at+2u<=exec_bytes;at+=2u) {
        uint32_t op=(uint32_t)data[at]|(uint32_t)data[at+1u]<<8;
        int32_t displacement=0;bool branch=false;
        if((op&0xf000u)==0xa000u || (op&0xf000u)==0xb000u) {
            displacement=(int32_t)(op&0xfffu);if(displacement&0x800) displacement-=0x1000;
            branch=true;
        } else if((op&0xff00u)==0x8900u || (op&0xff00u)==0x8b00u ||
                  (op&0xff00u)==0x8d00u || (op&0xff00u)==0x8f00u) {
            displacement=(int32_t)(op&0xffu);if(displacement&0x80) displacement-=0x100;
            branch=true;
        }
        uint32_t target=KUI_RETAIL_EXEC_ADDRESS+at+4u+(uint32_t)(displacement*2);
        if(branch && target>KUI_VMU_TOY_HOOK_BEGIN && target<KUI_VMU_TOY_HOOK_END) return false;
        if(!(at&3u) && at+4u<=exec_bytes) {
            uint32_t pointer=op|(uint32_t)data[at+2u]<<16|(uint32_t)data[at+3u]<<24;
            uint32_t area=pointer&UINT32_C(0xff000000);
            if(area==UINT32_C(0x0c000000) || area==UINT32_C(0x8c000000) || area==UINT32_C(0xac000000)) {
                pointer=(pointer&UINT32_C(0x1fffffff))|UINT32_C(0x80000000);
                if(pointer>KUI_VMU_TOY_HOOK_BEGIN && pointer<KUI_VMU_TOY_HOOK_END) return false;
            }
        }
    }
    return true;
}

void kui_vmu_observe_stage_install(void) {
    volatile struct kui_vmu_observe_boot_control *c=observe_control();
    c->status=KUI_VMU_OBS_BOOT_INITIALIZING;
    if((observe_read(0xff00001cu,4)&0x105u)!=0x105u ||
       c->heap_begin!=KUI_VMU_TOY_HEAP_BEGIN || c->heap_bytes!=KUI_VMU_TOY_HEAP_BYTES ||
       c->entry_sp<0x8c00c100u || c->entry_sp>0x8c00f400u || (c->entry_sp&3u) ||
       observe_read(0x8c117dbcu,4)!=0x8c0c5ac8u ||
       observe_read(0x8c0c5ac8u,4)!=0x8c0b3ef2u ||
       observe_read(0x8c0c5accu,4)!=0x8c0b3fc0u ||
       observe_read(0x8c117de4u,4)!=KUI_VMU_TOY_HEAP_BEGIN ||
       observe_read(0x8c117decu,4)!=KUI_VMU_TOY_HEAP_BEGIN ||
       observe_read(0x8c117de8u,4)!=KUI_VMU_TOY_HEAP_BYTES ||
       observe_read(0x8c117de0u,4)!=0x8c117dc0u ||
       observe_read(0x8c117dc0u,4)!=KUI_VMU_TOY_HEAP_BEGIN ||
       observe_read(0x8c117dc4u,4)!=0u ||
       observe_read(KUI_VMU_TOY_HEAP_BEGIN,4)!=0x8c117dc0u ||
       observe_read(KUI_VMU_TOY_HEAP_BEGIN+4u,4)!=0x76de6u ||
       (uint32_t)(uintptr_t)__retail_stage_bss_end>0x8cfcffe0u)
        observe_fail(5,c->entry_sp);
    observe_witnesses(true);
    uint32_t bytes=(uint32_t)(observer_blob_end-observer_blob);
    struct kui_vmu_maple_observe_exports e;observe_exports(&e,bytes);
    void *(*allocate)(uint32_t)=(void *(*)(uint32_t))(uintptr_t)KUI_VMU_TOY_HEAP_ALLOCATE;
    c->lease=(uint32_t)(uintptr_t)allocate(KUI_VMU_TOY_LEASE_BYTES);
    if(c->lease!=KUI_VMU_TOY_LEASE_BEGIN || observe_read(0x8cfcffe4u,4)!=0x1801u ||
       observe_read(KUI_VMU_TOY_HEAP_BEGIN+4u,4)!=0x755e5u ||
       observe_read(0x8c117de0u,4)!=0x8c117dc0u ||
       observe_read(0x8c117dc0u,4)!=KUI_VMU_TOY_HEAP_BEGIN ||
       observe_read(KUI_VMU_TOY_HEAP_BEGIN,4)!=0x8c117dc0u)
        observe_fail(6,c->lease);
    c->status=KUI_VMU_OBS_BOOT_LEASED;
    memcpy((void *)(uintptr_t)c->lease,observer_blob,bytes);
    /* Remove stale cached lines before the worker's P2 state becomes live. */
    observe_publish(c->lease,e.worker_end);
    memset((void *)(uintptr_t)e.bss_begin,0,e.bss_end-e.bss_begin);
    if(memcmp((const void *)(uintptr_t)(c->lease+UINT32_C(0x20000000)),observer_blob,bytes))
        observe_fail(7,c->lease);
    struct kui_vmu_maple_observe_config config={KUI_VMU_MAPLE_OBSERVE_MAGIC,
        KUI_VMU_MAPLE_OBSERVE_VERSION,sizeof(config),KUI_VMU_MAPLE_OBSERVE_DEFAULT_STOP,
        KUI_VMU_MAPLE_OBSERVE_DEFAULT_POST,{0}};
    memcpy(config.display,display.regs,sizeof(config.display));
    int (*initialize)(const struct kui_vmu_maple_observe_config *)=
        (int (*)(const struct kui_vmu_maple_observe_config *))(uintptr_t)e.initialize;
    if(initialize(&config)) observe_fail(8,e.initialize);
    observe_witnesses(true);
    /* One masked RAM transaction. R0 is dead at the first original load;
     * the worker replays every displaced instruction then resumes at 270. */
    const uint16_t patch[6]={0xd001u,0x402bu,0x0009u,0x0009u,
        (uint16_t)e.hook,(uint16_t)(e.hook>>16)};
    memcpy((void *)(uintptr_t)KUI_VMU_TOY_HOOK_BEGIN,patch,sizeof(patch));
    observe_publish(KUI_VMU_TOY_HOOK_BEGIN,KUI_VMU_TOY_HOOK_END);
    /* The normal SDK consumer is called through this admitted data literal.
     * Only its operand-cache line needs publication; instructions are not
     * patched here and no whole-cache operation is required. */
    observe_write(KUI_VMU_MAPLE_OBSERVE_COMPLETION_SLOT,4,e.complete_hook);
    uint32_t literal_line=KUI_VMU_MAPLE_OBSERVE_COMPLETION_SLOT&~31u;
    __asm__ volatile("ocbp @%0"::"r"(literal_line):"memory");
    if(observe_read(KUI_VMU_MAPLE_OBSERVE_COMPLETION_SLOT+UINT32_C(0x20000000),4)!=e.complete_hook)
        observe_fail(13,KUI_VMU_MAPLE_OBSERVE_COMPLETION_SLOT);
    c->worker_end=e.worker_end;c->terminal=e.terminal;
    c->installer=0;c->stage_stack=0;c->status=KUI_VMU_OBS_BOOT_INSTALLED;
}

void kui_retail_stage_main(const uint8_t *wire) {
    /* Decode before resident selection or card initialization. An unsupported
     * game never reaches the installer or any exact-title memory access. */
    retail_display_capture(&display);
    retail_display_restore(&display);
    if(kui_retail_manifest_decode(wire,&manifest)!=KUI_GAME_OK ||
       !kui_vmu_toy_observe_manifest(&manifest))
        stopped("OBSERVER NEEDS ORIGINAL TOY / SCI / STANDARD",0);
    retail_display_line("PASSIVE MAPLE OBSERVER / NO VIRTUAL VMU YET");
    kui_vmu_observe_base_main(wire);
}

void kui_retail_stage_relay(const uint32_t *frame,uint32_t ccr) {
    kui_vmu_observe_base_relay(frame,ccr);
    if(!kui_vmu_toy_observe_manifest(&manifest) ||
       exec_bytes!=KUI_VMU_TOY_EXEC_BYTES || boot_crc!=KUI_VMU_TOY_EXEC_CRC ||
       manifest.ip_crc32!=0x38ba2868u ||
       !kui_retail_observe_native_ip((const uint8_t *)(uintptr_t)KUI_RETAIL_IP_ADDRESS))
        observe_fail(9,boot_crc);
    struct kui_sha256 hash;uint8_t digest[32];
    kui_sha256_init(&hash);
    kui_sha256_update(&hash,(const void *)(uintptr_t)KUI_RETAIL_EXEC_ADDRESS,exec_bytes);
    kui_sha256_digest(&hash,digest);
    if(memcmp(digest,executable_sha,sizeof(digest))) observe_fail(10,boot_crc);
    if(!observe_no_interior_references()) observe_fail(11,KUI_VMU_TOY_HOOK_BEGIN);
    observe_witnesses(false);
    uint32_t bytes=(uint32_t)(observer_blob_end-observer_blob);
    struct kui_vmu_maple_observe_exports e;observe_exports(&e,bytes);
    volatile struct kui_vmu_observe_boot_control *c=observe_control();
    for(unsigned i=0;i<sizeof(*c)/4u;i++)
        if(((volatile uint32_t *)c)[i]) observe_fail(12,KUI_VMU_OBSERVE_LOW_CONTROL+4u*i);
    c->installer=(uint32_t)(uintptr_t)kui_vmu_observe_stage_install;
    c->stage_stack=KUI_RETAIL_STAGE_STACK-32u;c->status=KUI_VMU_OBS_BOOT_ARMED;
    observe_write(KUI_VMU_TOY_HEAP_SLOT,4,KUI_VMU_OBSERVE_LOW_HEAP_HOOK);
    observe_publish(KUI_VMU_TOY_HEAP_SLOT,KUI_VMU_TOY_HEAP_SLOT+4u);
    observe_write(0x8c027a24u,4,KUI_VMU_OBSERVE_LOW_RETURN_HOOK);
    observe_publish(0x8c027a24u,0x8c027a28u);
    ((uint32_t *)(uintptr_t)frame)[5]=KUI_VMU_OBSERVE_LOW_RETURN_HOOK;
}
