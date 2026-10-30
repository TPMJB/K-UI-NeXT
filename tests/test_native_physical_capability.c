/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/native_physical_capability.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void) {
    unsigned checks=0;
    struct kui_native_physical_capability cap={KUI_NATIVE_PHYSICAL_CAP_WORDS};
    uint8_t wire[32],data[128];memcpy(wire,&cap,32);
#define CHECK(value) do {assert(value);++checks;} while(0)
    CHECK(sizeof(cap)==32);CHECK(kui_native_physical_descriptor_valid(wire,32));
    CHECK(kui_native_physical_capability_valid(wire,32));
    CHECK(!kui_native_physical_descriptor_valid(NULL,32));
    for(size_t bytes=0;bytes<32;bytes++)CHECK(!kui_native_physical_descriptor_valid(wire,bytes));
    for(size_t byte=0;byte<32;byte++) for(unsigned bit=0;bit<8;bit++) {
        uint8_t changed[32];memcpy(changed,wire,32);changed[byte]^=(uint8_t)(1u<<bit);
        CHECK(!kui_native_physical_descriptor_valid(changed,32));
    }
    for(unsigned reader=0;reader<8;reader++)for(unsigned transport=0;transport<16;transport++)
        CHECK(kui_native_physical_launch_valid(reader,transport)==(reader==0 && transport<3));
    CHECK(!kui_native_physical_supports_transport(~0u));
    memset(data,0,sizeof(data));size_t offset=99;
    CHECK(kui_native_physical_descriptor_find(data,sizeof(data),&offset)==0 && offset==99);
    memcpy(data+3,wire,32);
    CHECK(kui_native_physical_descriptor_find(data,sizeof(data),&offset)==1 && offset==3);
    memcpy(data+70,wire,8); /* incidental magic / truncated partial record */
    CHECK(kui_native_physical_descriptor_find(data,78,&offset)==1 && offset==3);
    memcpy(data+70,wire,32);
    CHECK(kui_native_physical_descriptor_find(data,sizeof(data),&offset)==-1);
    CHECK(kui_native_physical_descriptor_find(data+3,31,&offset)==0);
    printf("PASS physical Standard capability: %u checks\n",checks);
    return 0;
}
