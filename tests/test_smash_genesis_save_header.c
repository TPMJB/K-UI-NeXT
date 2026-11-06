/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/smash_genesis_save_header.h"
#include "kui/smash_native_menu.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void) {
    uint8_t header[512]={0},before[512];
    header[0x1b0]='R';header[0x1b1]='A';header[0x1b3]=0x20u;
    for(unsigned type=0u;type<256u;type++)for(unsigned size=0u;size<256u;size++) {
        header[0x1b2]=(uint8_t)type;header[0x1ba]=(uint8_t)size;
        uint32_t expected=(size+1u)*256u;
        if(type==0xf0u || type==0xf8u)expected/=2u;
        else if(type!=0xe0u)expected=0u;
        memcpy(before,header,sizeof(before));
        assert(kui_smash_genesis_sram_bytes(header,sizeof(header))==expected);
        assert(!memcmp(before,header,sizeof(header)));
    }
    header[0x1b2]=0xf8u;header[0x1ba]=0x3fu;
    assert(kui_smash_genesis_sram_bytes(header,512u)==8192u);
    for(unsigned i=0u;i<3u;i++) {
        unsigned at=i==2u?0x1b3u:0x1b0u+i;
        uint8_t old=header[at];header[at]^=1u;
        assert(!kui_smash_genesis_sram_bytes(header,512u));header[at]=old;
    }
    assert(!kui_smash_genesis_sram_bytes(NULL,512u));
    assert(!kui_smash_genesis_sram_bytes(header,511u));
    assert(!kui_smash_genesis_sram_bytes(header,513u));
    assert(KUI_SMASH_NATIVE_MENU_SELECTIONS==10u);
    assert(!kui_smash_native_menu_name(0u) && !kui_smash_native_menu_folder(0u) && !kui_smash_native_menu_rom(0u));
    assert(!kui_smash_native_menu_name(11u) && !kui_smash_native_menu_folder(11u) && !kui_smash_native_menu_rom(11u));
    for(unsigned i=1u;i<=10u;i++)assert(kui_smash_native_menu_name(i) && kui_smash_native_menu_folder(i) && kui_smash_native_menu_rom(i));
    puts("PASS exact original-engine SRAM classifier: 65,536 descriptors; immutable inputs and shared ten-row catalog");
    return 0;
}
