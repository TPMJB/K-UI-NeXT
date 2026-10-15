/* SPDX-License-Identifier: GPL-3.0-only */
/* Synthetic owner/process/module fixture: no game or DLL instructions. */
#define KUI_WINCE_CDDA_WORMS 1
#define main kui_armada_fixture_unused_main
#include "test_armada_ce_live_sound.c"
#undef main
#include "kui/armada_ce_stream_initial_word.h"

#define WORMS_CLASS UINT32_C(0x00123080)
static const uint32_t worms_table[3]={0x03cd940cu,0x03d56190u,0x03d2b0f8u};
static const uint32_t worms_base[3]={TEST_DS_BASE,TEST_SN_BASE,TEST_CORE_BASE};
static const uint32_t worms_node[3]={TEST_DS_NODE,TEST_SN_NODE,TEST_CORE_NODE};
static const uint32_t worms_toc[3]={0x8c11e104u,0x8c11e0e4u,0x8c11dee4u};
static void worms_fixture(struct test_memory *m,unsigned owner,bool disk,bool discarded) {
    memset(m,0,sizeof(*m));
    put(m,KUI_ARMADA_CE_LIVE_CURRENT_PROCESS,owner_process(owner));
    put(m,0x8c126410u,0x8c300000u);put(m,0x8c126414u,0x8c308000u);
    put(m,KUI_ARMADA_CE_LIVE_MODULE_HEAD,TEST_DS_NODE);
    static const uint32_t span[3]={0xc000u,0x9000u,0x21000u};
    static const uint32_t rva[3][4]={{0x1000u,0x9000u,0xa000u,0xb000u},
        {0x1000u,0x5000u,0x7000u,0x8000u},{0x1000u,0x1b000u,0x1c000u,0x1e000u}};
    static const uint32_t size[3][4]={{0x73a6u,0x400u,0x610u,0x770u},
        {0x3885u,0x1184u,0x378u,0x494u},{0x1a0efu,0xecu,0xfe0u,0x218u}};
    static const char *const names[3]={"dsound.dll","sndcore.dll","coredll.dll"};
    for(unsigned k=0;k<3u;k++) {
        uint32_t node=worms_node[k],base=worms_base[k],table=worms_table[k];
        unsigned count=k==2u || disk?4u:3u;
        put(m,node,node);put(m,node+4u,k<2u?worms_node[k+1u]:0u);
        put(m,node+12u,1u<<owner);
        for(unsigned i=0;i<16u;i++) put(m,node+16u+4u*i,0);
        put(m,node+16u+4u*(owner/2u),2u<<(16u*(owner&1u)));
        put(m,node+80u,base);put(m,node+84u,0x40000u);
        put(m,node+96u,disk && k<2u?0x123456u+k:worms_toc[k]);
        put(m,node+100u,disk && k<2u?2u:3u);
        put(m,node+112u,count);put(m,node+124u,span[k]);put(m,node+176u,table);
        for(unsigned i=0;i<count;i++) {
            uint32_t flags=i==0u?0x60000020u:i==1u?(k==2u?0xc0300040u:
                disk?0xc0000040u:0xc0002040u):k==2u?(i==2u?0x40002040u:0x40000040u):
                i==3u?0x42000040u:disk?0x40000040u:0x40002040u;
            virtual_put(m,table+28u*i,i==1u?loaded_data_bytes(size[k][i],names[k],count):size[k][i]);
            virtual_put(m,table+28u*i+4u,rva[k][i]);
            virtual_put(m,table+28u*i+8u,discarded && disk && k<2u && i==3u?0u:base+rva[k][i]);
            virtual_put(m,table+28u*i+16u,flags);
        }
    }
    for(unsigned i=0;i<32u;i++) {
        put(m,owner_process(i),0x77aa0000u|i);
        put(m,owner_process(i)+12u,i==owner?owner_slot(i):0u);
        put(m,owner_process(i)+8u,i==owner?0x100u+i:0u);
    }
    uint32_t slot=owner_slot(owner),device=slot|TEST_DEVICE;
    crc_guard(m,slot|0xa4c90u,32u,0x5dd60ad4u);
    crc_guard(m,slot|0xad240u,12u,0x7d5b8b13u);
    crc_guard(m,slot|0xa4d78u,12u,0x8ba77820u);
    virtual_put(m,slot|0x127ba8u,WORMS_CLASS);
    virtual_put(m,slot|WORMS_CLASS,TEST_DEVICE);
    virtual_put(m,ds_alias(owner,0x90a8u),device);
    virtual_put(m,slot|0xd215cu,TEST_DS_BASE+0x1598u);
    virtual_put(m,device,(TEST_DS_BASE&0x01ffffffu)+0x1154u);
    virtual_put(m,device+8u,3u);
    static const uint32_t offsets[4]={4u,8u,12u,24u};
    static const uint32_t targets[4]={0x18dcu,0x18e4u,0x1924u,0x1b08u};
    for(unsigned i=0;i<4u;i++) virtual_put(m,ds_alias(owner,0x1154u)+offsets[i],TEST_DS_BASE+targets[i]);
    virtual_put(m,ds_alias(owner,0x9008u),TEST_CORE_BASE+0x8a78u);
    virtual_put(m,ds_alias(owner,0x900cu),TEST_CORE_BASE+0x8adcu);
    crc_guard(m,core_alias(owner,0x8a78u),32u,0x12d156c9u);
    crc_guard(m,core_alias(owner,0x8adcu),32u,0x661d8023u);
    crc_guard(m,TEST_CORE_BASE+0x8a78u,32u,0x12d156c9u);
    crc_guard(m,TEST_CORE_BASE+0x8adcu,32u,0x661d8023u);
    static const uint32_t buffer_offset[9]={8u,12u,44u,76u,48u,72u,36u,16u,52u};
    static const uint32_t buffer_target[9]={0x2744u,0x283cu,0x2a10u,0x3744u,0x2bacu,
        0x364cu,0x2980u,0x2884u,0x2f00u};
    for(unsigned i=0;i<9u;i++) virtual_put(m,ds_alias(owner,0x1268u)+buffer_offset[i],TEST_DS_BASE+buffer_target[i]);
    crc_guard(m,ds_alias(owner,0x2f00u),32u,0x3f18ba2du);
    crc_guard(m,TEST_DS_BASE+0x2f00u,32u,0x3f18ba2du);
    static const uint32_t api_rva[11]={0x8900u,0x83f4u,0x82e4u,0x8414u,0x8358u,
        0x8110u,0x848cu,0x8494u,0x8824u,0x88b8u,0x84ecu};
    static const uint32_t api_bytes[11]={8u,8u,32u,8u,32u,8u,8u,8u,20u,8u,8u};
    static const uint32_t api_crc[11]={0xc1eabb03u,0xd3f7581fu,0xa6a3bf77u,0xe1c13a9du,
        0xddf88da0u,0x4dcea5a7u,0x3a94fd2au,0x08a29fa8u,0x7ae32575u,0x914c763fu,0xdce3a077u};
    for(unsigned i=0;i<11u;i++) crc_guard(m,TEST_CORE_BASE+api_rva[i],api_bytes[i],api_crc[i]);
    if(disk) {
        static const uint32_t rv[2][3]={{0x1598u,0x1924u,0x2bacu},{0x1110u,0x1200u,0x12f4u}};
        static const uint32_t crc[2][3]={{0x3486e9e0u,0x06c05de7u,0x17ca5406u},
            {0x5e7cdea8u,0xa437a283u,0x05db23f8u}};
        for(unsigned k=0;k<2u;k++) for(unsigned i=0;i<3u;i++) crc_guard(m,worms_base[k]+rv[k][i],32u,crc[k][i]);
    }
}
int main(void) {
    static struct test_memory m;
    struct kui_armada_ce_live_sound out;
    struct kui_armada_ce_live_stream_api api;
    unsigned cases=0,owner=4u;
    for(unsigned mode=0;mode<3u;mode++) {
        worms_fixture(&m,owner,mode!=0u,mode==2u);
        assert(bind(&m,&out)==KUI_ARMADA_CE_LIVE_OK);
        assert(out.device==(owner_slot(owner)|TEST_DEVICE));
        assert(out.direct_sound_create==TEST_DS_BASE+0x1598u);
        assert(out.buffer_vtable==ds_alias(owner,0x1268u));
        assert(kui_armada_ce_live_stream_api_bind(&bind_work,&out,&api)==KUI_ARMADA_CE_LIVE_OK);
        assert(api.buffer_set_position==TEST_DS_BASE+0x2f00u);
        assert(api.create_thread==TEST_CORE_BASE+0x8900u);cases++;
    }
    worms_fixture(&m,owner,false,false);
    virtual_put(&m,owner_slot(owner)|0x127ba8u,0x03f00000u);
    expect(&m,KUI_ARMADA_CE_LIVE_OBJECT);cases++;
    worms_fixture(&m,owner,false,false);
    m.change_address=physical(&m,owner_slot(owner)|0x127ba8u);
    m.change_on=2u;m.change_mask=4u;
    expect(&m,KUI_ARMADA_CE_LIVE_CHANGED);cases++;
    worms_fixture(&m,owner,false,false);
    m.change_address=physical(&m,owner_slot(owner)|WORMS_CLASS);
    m.change_on=2u;m.change_mask=4u;
    expect(&m,KUI_ARMADA_CE_LIVE_CHANGED);cases++;
    worms_fixture(&m,owner,false,false);
    virtual_put(&m,worms_table[0],0x61a6u);
    expect(&m,KUI_ARMADA_CE_LIVE_MODULE);cases++;
    worms_fixture(&m,owner,false,false);
    virtual_put(&m,owner_slot(owner)|0xd215cu,TEST_DS_BASE+0x15ecu);
    expect(&m,KUI_ARMADA_CE_LIVE_OBJECT);cases++;
    printf("Worms exact synthetic live profile: %u cases PASS\n",cases);
    return 0;
}
