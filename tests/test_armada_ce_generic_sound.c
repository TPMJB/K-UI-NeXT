/* SPDX-License-Identifier: GPL-3.0-only */
/* Unfamiliar application fixture: the only retained identity is the admitted
 * kernel/sound runtime. All instruction words below remain authored synthetic
 * CRC witnesses, never bytes from a private game, kernel or DLL. */
#define KUI_ARMADA_CE_GENERIC_APPLICATION 1
#define main kui_armada_ce_exact_fixture_unused_main
#include "test_armada_ce_live_sound.c"
#undef main
#include "kui/armada_ce_stream_initial_word.h"
#include "kui/armada_ce_stream_rebind_word.h"

static const uint32_t generic_handle=0x12345679u;
#if defined(KUI_WINCE_CDDA_WORMS) && KUI_WINCE_CDDA_WORMS == 1
static const uint32_t generic_app_offset[5]={0xa4c90u,0xad240u,0xa4d78u,0x127ba8u,0xd215cu};
static const uint32_t generic_app_bytes[5]={32u,12u,12u,4u,4u};
static const uint32_t generic_api_rva[11]={0x8900u,0x83f4u,0x82e4u,0x8414u,0x8358u,
    0x8110u,0x848cu,0x8494u,0x8824u,0x88b8u,0x84ecu};
static const uint32_t generic_buffer_rva[8]={0x2744u,0x283cu,0x2a10u,0x3744u,
    0x2bacu,0x364cu,0x2980u,0x2884u};
#else
static const uint32_t generic_api_rva[11]={0x8d20u,0x8814u,0x8708u,0x8834u,0x877cu,0x852cu,
    0x88acu,0x88b4u,0x8c44u,0x8cd8u,0x890cu};
#if defined(KUI_WINCE_CDDA_WWP) && KUI_WINCE_CDDA_WWP == 1
/* Authored absent application regions, never World Party executable bytes. */
static const uint32_t generic_app_offset[5]={0x995d4u,0xce900u,0x996a0u,0xf7180u,0xf21a8u};
static const uint32_t generic_app_bytes[5]={32u,12u,28u,4u,4u};
static const uint32_t generic_buffer_rva[8]={0x2b7cu,0x2c68u,0x2f28u,0x3afcu,
    0x30c0u,0x39e8u,0x2e98u,0x2cb0u};
#elif defined(KUI_WINCE_CDDA_BAM4) && KUI_WINCE_CDDA_BAM4 == 1
static const uint32_t generic_app_offset[5]={0x1b9b0u,0x5bf98u,0x1bb00u,0x13f14cu,0x5f0c4u};
static const uint32_t generic_app_bytes[5]={32u,12u,40u,4u,4u};
static const uint32_t generic_buffer_rva[8]={0x2918u,0x2a04u,0x2cc4u,0x39b4u,
    0x2e5cu,0x38a0u,0x2c34u,0x2a4cu};
#else
static const uint32_t generic_app_offset[5]={0x995d4u,0xce900u,0x996a0u,0xf7180u,0xf21a8u};
static const uint32_t generic_app_bytes[5]={32u,12u,28u,4u,4u};
static const uint32_t generic_buffer_rva[8]={0x2910u,0x29fcu,0x2cbcu,0x39acu,
    0x2e54u,0x3898u,0x2c2cu,0x2a44u};
#endif
#endif
static void no_application_reads(struct test_memory *m,unsigned owner) {
    for(unsigned range=0;range<5u;range++) for(unsigned at=0;at<generic_app_bytes[range];at+=4u) {
        uint32_t address=physical(m,owner_slot(owner)|generic_app_offset[range])+at;
        for(unsigned i=0;i<m->reads;i++) assert(m->addresses[i]!=address);
    }
}
static void generic_fixture(struct test_memory *m,unsigned owner,unsigned disk) {
    const uint32_t nodes[3]={TEST_DS_NODE,TEST_SN_NODE,TEST_CORE_NODE};
    const uint32_t bases[3]={TEST_DS_BASE,TEST_SN_BASE,TEST_CORE_BASE};
    const uint32_t tables[3]=KUI_ARMADA_CE_LIVE_TABLE_RVAS;
    const uint32_t tocs[3]={KUI_ARMADA_CE_LIVE_DSOUND_TOC,KUI_ARMADA_CE_LIVE_SNDCORE_TOC,KUI_ARMADA_CE_LIVE_COREDLL_TOC};
    const uint32_t spans[3]={KUI_ARMADA_CE_LIVE_DSOUND_SPAN,0x9000u,KUI_ARMADA_CE_LIVE_COREDLL_SPAN};
#if defined(KUI_WINCE_CDDA_WWP) && KUI_WINCE_CDDA_WWP == 1
    static const uint32_t rva[3][4]={{0x1000u,0x8000u,0x9000u,0xa000u},
        {0x1000u,0x5000u,0x7000u,0x8000u},{0x1000u,0x2e000u,0x2f000u,0x31000u}};
    static const uint32_t sizes[3][4]={{0x6bd6u,0x460u,0x5a8u,0x806u},
        {0x3db0u,0x118cu,0x3e0u,0x4beu},{0x2ccc4u,0x164u,0x1848u,0x218u}};
    static const uint32_t identity_rva[2][3]={{0x16e8u,0x1ad4u,0x30c0u},
        {0x10d8u,0x11c8u,0x12c4u}};
    static const uint32_t identity_crc[2][3]={{0xa2f5e2c9u,0x34718dd9u,0xeee481b4u},
        {0x5ba8b52bu,0x8303faf9u,0x53f228afu}};
#elif defined(KUI_WINCE_CDDA_WORMS) && KUI_WINCE_CDDA_WORMS == 1
    static const uint32_t rva[3][4]={{0x1000u,0x9000u,0xa000u,0xb000u},
        {0x1000u,0x5000u,0x7000u,0x8000u},{0x1000u,0x1c000u,0x1d000u,0x1e000u}};
    static const uint32_t sizes[3][4]={{0x73a6u,0x400u,0x610u,0x770u},
        {0x3885u,0x1184u,0x378u,0x494u},{0x1a0efu,0xecu,0xfe0u,0x218u}};
    static const uint32_t identity_rva[2][3]={{0x1598u,0x1924u,0x2bacu},{0x1110u,0x1200u,0x12f4u}};
    static const uint32_t identity_crc[2][3]={{0x3486e9e0u,0x06c05de7u,0x17ca5406u},
        {0x5e7cdea8u,0xa437a283u,0x05db23f8u}};
#else
    static const uint32_t rva[3][4]={{0x1000u,0x8000u,0x9000u,0xa000u},
        {0x1000u,0x5000u,0x7000u,0x8000u},{0x1000u,0x2e000u,0x2f000u,0x31000u}};
    static const uint32_t sizes[3][4]={{KUI_ARMADA_CE_LIVE_DSOUND_TEXT_BYTES,0x460u,0x550u,0x764u},
        {0x3c25u,0x118cu,0x3d0u,0x4b4u},{0x2ccc4u,0x164u,0x1848u,0x218u}};
    static const uint32_t identity_rva[2][3]={{0x15ecu,0x19dcu,KUI_ARMADA_CE_LIVE_BUFFER_PLAY_RVA},
        {0x10d8u,0x11c8u,0x12b8u}};
    static const uint32_t identity_crc[2][3]={{0xeb34aa87u,0x34718dd9u,0x3453b46fu},
        {0x5ba8b52bu,0x2718008fu,0x834f5156u}};
#endif
    static const char *const names[3]={"dsound.dll","sndcore.dll","coredll.dll"};
    memset(m,0,sizeof(*m));
    put(m,KUI_ARMADA_CE_LIVE_CURRENT_PROCESS,owner_process(owner));
    put(m,KUI_ARMADA_CE_LIVE_HEAP_LOW,0x8c300000u);put(m,KUI_ARMADA_CE_LIVE_HEAP_HIGH,0x8c308000u);
    put(m,KUI_ARMADA_CE_LIVE_MODULE_HEAD,TEST_DS_NODE);
    for(unsigned k=0;k<3u;k++) {
        uint32_t node=nodes[k],base=bases[k],table=base+tables[k];
        bool external=disk && k<2u;
        unsigned count=k==2u || external?4u:3u;
        put(m,node,node);put(m,node+4u,k<2u?nodes[k+1u]:0u);put(m,node+12u,1u<<owner);
        for(unsigned i=0;i<16u;i++) put(m,node+16u+4u*i,0u);
        put(m,node+16u+4u*(owner/2u),2u<<(16u*(owner&1u)));
        put(m,node+80u,base);put(m,node+84u,0x40000u);put(m,node+96u,external?0x123456u+k:tocs[k]);
        put(m,node+100u,external?2u:3u);put(m,node+112u,count);put(m,node+124u,spans[k]);
        put(m,node+176u,table);
        for(unsigned row=0;row<count;row++) {
            uint32_t flags=row==0u?0x60000020u:row==1u?(k==2u?0xc0300040u:
                external?0xc0000040u:0xc0002040u):k==2u?(row==2u?0x40002040u:0x40000040u):
                row==3u?0x42000040u:external?0x40000040u:0x40002040u;
            virtual_put(m,table+28u*row,row==1u?loaded_data_bytes(sizes[k][row],names[k],count):sizes[k][row]);
            virtual_put(m,table+28u*row+4u,rva[k][row]);
            virtual_put(m,table+28u*row+8u,external && disk==2u && row==3u?0u:base+rva[k][row]);
            virtual_put(m,table+28u*row+16u,flags);
        }
        if(external) for(unsigned range=0;range<3u;range++)
            crc_guard(m,base+identity_rva[k][range],32u,identity_crc[k][range]);
    }
    for(unsigned i=0;i<32u;i++) {
        put(m,owner_process(i),0x77aa0000u|i);put(m,owner_process(i)+12u,i==owner?owner_slot(i):0u);
        put(m,owner_process(i)+8u,i==owner?generic_handle:0u);
    }
    uint32_t device=owner_slot(owner)|TEST_DEVICE;
    virtual_put(m,ds_alias(owner,KUI_ARMADA_CE_LIVE_SINGLETON_RVA),device);
    virtual_put(m,device,(TEST_DS_BASE&0x01ffffffu)+KUI_ARMADA_CE_LIVE_DEVICE_VTABLE_RVA);
    virtual_put(m,device+8u,3u);
    static const unsigned device_offset[4]={4u,8u,12u,24u};
    static const uint32_t device_rva[4]={KUI_ARMADA_CE_LIVE_ADDREF_RVA,KUI_ARMADA_CE_LIVE_RELEASE_RVA,
        KUI_ARMADA_CE_LIVE_CREATE_BUFFER_RVA,KUI_ARMADA_CE_LIVE_COOPERATIVE_RVA};
    for(unsigned i=0;i<4u;i++) virtual_put(m,ds_alias(owner,KUI_ARMADA_CE_LIVE_DEVICE_VTABLE_RVA)+device_offset[i],
        TEST_DS_BASE+device_rva[i]);
    virtual_put(m,ds_alias(owner,KUI_ARMADA_CE_LIVE_ENTER_IAT_RVA),TEST_CORE_BASE+KUI_ARMADA_CE_LIVE_ENTER_RVA);
    virtual_put(m,ds_alias(owner,KUI_ARMADA_CE_LIVE_LEAVE_IAT_RVA),TEST_CORE_BASE+KUI_ARMADA_CE_LIVE_LEAVE_RVA);
    crc_guard(m,TEST_CORE_BASE+KUI_ARMADA_CE_LIVE_ENTER_RVA,32u,KUI_ARMADA_CE_LIVE_ENTER_CRC);
    crc_guard(m,TEST_CORE_BASE+KUI_ARMADA_CE_LIVE_LEAVE_RVA,32u,KUI_ARMADA_CE_LIVE_LEAVE_CRC);
    static const unsigned buffer_offset[8]={8u,12u,44u,76u,48u,72u,36u,16u};
    for(unsigned i=0;i<8u;i++) virtual_put(m,ds_alias(owner,KUI_ARMADA_CE_LIVE_BUFFER_VTABLE_RVA)+buffer_offset[i],
        TEST_DS_BASE+generic_buffer_rva[i]);
}
static void generic_stream_fixture(struct test_memory *m,unsigned owner) {
    generic_fixture(m,owner,0u);
    static const uint32_t size[11]={8u,8u,32u,8u,32u,8u,8u,8u,20u,8u,8u};
    static const uint32_t crc[11]={0xc1eabb03u,0xd3f7581fu,0xa6a3bf77u,0xe1c13a9du,
        0xddf88da0u,0x4dcea5a7u,0x3a94fd2au,0x08a29fa8u,0x7ae32575u,0x914c763fu,0xdce3a077u};
    for(unsigned i=0;i<11u;i++) crc_guard(m,TEST_CORE_BASE+generic_api_rva[i],size[i],crc[i]);
    virtual_put(m,ds_alias(owner,KUI_ARMADA_CE_LIVE_BUFFER_VTABLE_RVA)+52u,TEST_DS_BASE+KUI_ARMADA_CE_LIVE_BUFFER_POSITION_RVA);
    crc_guard(m,TEST_DS_BASE+KUI_ARMADA_CE_LIVE_BUFFER_POSITION_RVA,32u,KUI_ARMADA_CE_LIVE_POSITION_CRC);
}
struct generic_native_fixture {
    struct test_memory memory;
    struct kui_armada_ce_stream_initial_owner owner;
    struct kui_armada_ce_live_workspace workspace;
    unsigned native_reads;
};
static bool generic_table(void *context,uint32_t address,uint32_t *value) {
    struct generic_native_fixture *f=context;
    uint32_t p1;
    assert(kui_armada_ce_ram_table_p1(address,&p1) && p1==address && address<0x8c800000u);
    return lookup(&f->memory,address,value);
}
static bool generic_outer(void *context,uint32_t address,uint32_t *value) {
    struct generic_native_fixture *f=context;
    /* native_values must never fetch a DLL/object value through physical RAM. */
    assert(address<0x8c800000u);
    return read_word(&f->memory,address,value);
}
static bool generic_native(void *context,uint32_t va,uint32_t *value) {
    struct generic_native_fixture *f=context;
    assert(kui_armada_ce_stream_initial_address(&f->owner,&f->workspace,va));
    ++f->native_reads;return lookup(&f->memory,physical(&f->memory,va),value);
}
static enum kui_armada_ce_ram_bind_status generic_mapped(void *context,uint32_t va,
    uint32_t p1,uint32_t page,uint32_t ccr,uint32_t *value) {
    struct generic_native_fixture *f=context;
    struct kui_armada_ce_ram_binding observed;
    *value=0;
    if(ccr!=TEST_CCR || !kui_armada_ce_stream_initial_address(&f->owner,&f->workspace,va))
        return KUI_ARMADA_CE_RAM_BIND_ARGUMENT;
    enum kui_armada_ce_stream_owner_status status=kui_armada_ce_stream_rebind_word(
        TEST_TTB,0x400880f0u,f->owner.process,f->owner.slot,f->owner.handle,
        va,p1,page,generic_table,generic_native,f,&observed);
    if(status!=KUI_ARMADA_CE_STREAM_OWNER_OK) return KUI_ARMADA_CE_RAM_BIND_CHANGED;
    *value=observed.value;return KUI_ARMADA_CE_RAM_BIND_OK;
}
int main(void) {
    static struct test_memory m;
    struct kui_armada_ce_live_sound sound;
    struct kui_armada_ce_live_stream_api api;
    unsigned cases=0,max_reads=0;
    /* Every process slot and both admitted module loading forms succeed even
     * though no application's code, holder or import entries exist. */
    for(unsigned owner=0;owner<32u;owner++) for(unsigned disk=0;disk<3u;disk++) {
        generic_fixture(&m,owner,disk);
        assert(bind(&m,&sound)==KUI_ARMADA_CE_LIVE_OK);
        assert(sound.owner_process==owner_process(owner) && sound.device==(owner_slot(owner)|TEST_DEVICE));
        assert(sound.direct_sound_create==ds_alias(owner,KUI_ARMADA_CE_LIVE_CREATE_RVA));
        no_application_reads(&m,owner);
        if(m.reads>max_reads) max_reads=m.reads;
        ++cases;
    }
    /* The ownership guard is a repeated runtime proof, not an unconditional
     * substitute for the removed application CRCs. */
    generic_fixture(&m,4u,0u);
    virtual_put(&m,ds_alias(4u,KUI_ARMADA_CE_LIVE_SINGLETON_RVA),0u);expect(&m,KUI_ARMADA_CE_LIVE_OBJECT);++cases;
    generic_fixture(&m,4u,0u);
    virtual_put(&m,ds_alias(4u,KUI_ARMADA_CE_LIVE_SINGLETON_RVA),owner_slot(5u)|TEST_DEVICE);
    expect(&m,KUI_ARMADA_CE_LIVE_OBJECT);++cases;
    generic_fixture(&m,4u,0u);
    virtual_put(&m,ds_alias(4u,KUI_ARMADA_CE_LIVE_SINGLETON_RVA),owner_slot(4u)|0x01fffffcu);
    expect(&m,KUI_ARMADA_CE_LIVE_OBJECT);++cases;
    generic_fixture(&m,4u,0u);
    m.change_address=physical(&m,ds_alias(4u,KUI_ARMADA_CE_LIVE_SINGLETON_RVA));m.change_on=2u;m.change_mask=4u;
    expect(&m,KUI_ARMADA_CE_LIVE_CHANGED);++cases;
    generic_fixture(&m,4u,0u);
    m.change_address=owner_process(4u)+8u;m.change_on=2u;m.change_mask=4u;
    expect(&m,KUI_ARMADA_CE_LIVE_CHANGED);++cases;
    generic_fixture(&m,4u,0u);put(&m,owner_process(4u)+8u,0u);
    expect(&m,KUI_ARMADA_CE_LIVE_OWNER);++cases;
    for(unsigned field=0;field<3u;field++) {
        generic_fixture(&m,4u,0u);
        uint32_t at=field==0u?(owner_slot(4u)|TEST_DEVICE):
            field==1u?ds_alias(4u,KUI_ARMADA_CE_LIVE_DEVICE_VTABLE_RVA)+12u:
                ds_alias(4u,KUI_ARMADA_CE_LIVE_BUFFER_VTABLE_RVA)+48u;
        virtual_put(&m,at,virtual_get(&m,at)+4u);
        expect(&m,KUI_ARMADA_CE_LIVE_OBJECT);++cases;
    }
    generic_fixture(&m,4u,0u);
    virtual_put(&m,(owner_slot(4u)|TEST_DEVICE)+8u,0u);
    expect(&m,KUI_ARMADA_CE_LIVE_OBJECT);++cases;
    generic_fixture(&m,4u,0u);
    virtual_put(&m,TEST_CORE_BASE+KUI_ARMADA_CE_LIVE_ENTER_RVA,
        virtual_get(&m,TEST_CORE_BASE+KUI_ARMADA_CE_LIVE_ENTER_RVA)^1u);
    expect(&m,KUI_ARMADA_CE_LIVE_IDENTITY);++cases;
    /* Two attached applications are ambiguous even if one is current. No
     * application memory is inspected to choose a preferred candidate. */
    generic_fixture(&m,4u,0u);
    put(&m,owner_process(5u)+12u,owner_slot(5u));put(&m,owner_process(5u)+8u,generic_handle+4u);
    const uint32_t nodes[3]={TEST_DS_NODE,TEST_SN_NODE,TEST_CORE_NODE};
    for(unsigned i=0;i<3u;i++) {
        put(&m,nodes[i]+12u,(1u<<4)|(1u<<5));put(&m,nodes[i]+24u,0x00020002u);
    }
    expect(&m,KUI_ARMADA_CE_LIVE_OWNER);no_application_reads(&m,4u);++cases;
    /* The native reader retains its owner/TTB/page witnesses with no physical
     * DLL/object reads, while metadata-only destination discovery is unchanged. */
    static struct generic_native_fixture native;
    memset(&native,0,sizeof(native));generic_fixture(&native.memory,4u,2u);
    assert(kui_armada_ce_stream_initial_owner(TEST_TTB,TEST_CCR,0x400880f0u,
        generic_table,&native,&native.owner));
    assert(kui_armada_ce_live_sound_bind_with_value_reader(TEST_TTB,TEST_CCR,
        generic_outer,&native,generic_mapped,true,&native.workspace,&sound)==KUI_ARMADA_CE_LIVE_OK);
    assert(native.native_reads && sound.owner_process==native.owner.process);++cases;
    /* Stream API admission also replaces its old application guard with the
     * singleton ownership proof and preserves native API code identity. */
    generic_stream_fixture(&m,4u);assert(bind(&m,&sound)==KUI_ARMADA_CE_LIVE_OK);
    assert(kui_armada_ce_live_stream_api_bind(&bind_work,&sound,&api)==KUI_ARMADA_CE_LIVE_OK);
    no_application_reads(&m,4u);++cases;
    generic_stream_fixture(&m,4u);assert(bind(&m,&sound)==KUI_ARMADA_CE_LIVE_OK);
    virtual_put(&m,ds_alias(4u,KUI_ARMADA_CE_LIVE_SINGLETON_RVA),(owner_slot(4u)|TEST_DEVICE)+4u);
    assert(kui_armada_ce_live_stream_api_bind(&bind_work,&sound,&api)==KUI_ARMADA_CE_LIVE_CHANGED);++cases;
    generic_stream_fixture(&m,4u);assert(bind(&m,&sound)==KUI_ARMADA_CE_LIVE_OK);
    virtual_put(&m,TEST_CORE_BASE+generic_api_rva[0],virtual_get(&m,TEST_CORE_BASE+generic_api_rva[0])^1u);
    assert(kui_armada_ce_live_stream_api_bind(&bind_work,&sound,&api)==KUI_ARMADA_CE_LIVE_IDENTITY);++cases;
    struct kui_armada_ce_live_stream_api empty={0};assert(!memcmp(&api,&empty,sizeof(api)));
#if defined(KUI_WINCE_CDDA_WWP) && KUI_WINCE_CDDA_WWP == 1
    /* The final word of every external sound witness participates in its
     * 32-byte identity. A matching short prefix is insufficient. */
    static const uint32_t witness_rva[2][3]={{0x16e8u,0x1ad4u,0x30c0u},
        {0x10d8u,0x11c8u,0x12c4u}};
    const uint32_t witness_base[2]={TEST_DS_BASE,TEST_SN_BASE};
    for(unsigned module=0;module<2u;module++) for(unsigned range=0;range<3u;range++) {
        generic_fixture(&m,4u,1u);
        uint32_t address=witness_base[module]+witness_rva[module][range]+28u;
        virtual_put(&m,address,virtual_get(&m,address)^1u);
        /* Discovery treats the changed external image as unrelated, leaving
         * a required sound module missing instead of publishing its APIs. */
        expect(&m,KUI_ARMADA_CE_LIVE_MODULE);++cases;
    }
    generic_fixture(&m,4u,0u);
    virtual_put(&m,(owner_slot(4u)|TEST_DEVICE),TEST_DS_BASE+0x111cu);
    expect(&m,KUI_ARMADA_CE_LIVE_OBJECT);++cases;
    generic_fixture(&m,4u,0u);
    virtual_put(&m,ds_alias(4u,KUI_ARMADA_CE_LIVE_BUFFER_VTABLE_RVA)+48u,TEST_DS_BASE+0x2e5cu);
    expect(&m,KUI_ARMADA_CE_LIVE_OBJECT);++cases;
    generic_stream_fixture(&m,4u);assert(bind(&m,&sound)==KUI_ARMADA_CE_LIVE_OK);
    uint32_t position=TEST_DS_BASE+KUI_ARMADA_CE_LIVE_BUFFER_POSITION_RVA+28u;
    virtual_put(&m,position,virtual_get(&m,position)^1u);
    assert(kui_armada_ce_live_stream_api_bind(&bind_work,&sound,&api)==KUI_ARMADA_CE_LIVE_IDENTITY);
    assert(!memcmp(&api,&empty,sizeof(api)));++cases;
#endif
    printf("Generic CE application sound: %u cases PASS, max %u budgeted reads; no application witnesses/holder/IAT reads.\n",cases,max_reads);
    return 0;
}
