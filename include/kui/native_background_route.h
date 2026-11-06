/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_NATIVE_BACKGROUND_ROUTE_H
#define KUI_NATIVE_BACKGROUND_ROUTE_H
#include "kui/games_retail.h"
#include "kui/native_dual_reader.h"
#include "kui/native_session.h"

/* Both readers use the accepted compact dual module. Optional session
 * controls may be installed explicitly at that same path; merely leaving
 * a separate session file on the card must never change the default. */
static inline const char *kui_native_background_package(uint32_t reader,bool virtual_a1) {
    if(reader==KUI_RETAIL_READER_ASYNC || reader==KUI_RETAIL_READER_ASYNC_EAGER)
        return virtual_a1?KUI_GAMES_RETAIL_NATIVE_DUAL_A1_PACKAGE:KUI_GAMES_RETAIL_NATIVE_DUAL_PACKAGE;
    return NULL;
}
static inline bool kui_native_background_module_matches(uint32_t reader,const void *data,size_t bytes) {
    size_t at=0u;
    if(kui_native_dual_descriptor_find(data,bytes,&at)!=1)return false;
    const uint8_t *payload=data;
    uint32_t lease=kui_native_dual_u32(payload+at+24u);
    int session=kui_native_session_descriptor_find(data,bytes,NULL);
    if(session==0 && lease!=KUI_NATIVE_DUAL_COMPACT_LEASE_BYTES)return false;
    if(session==1 && lease!=KUI_NATIVE_DUAL_GENERAL_LEASE_BYTES)return false;
    return reader==KUI_RETAIL_READER_ASYNC?session>=0:
           reader==KUI_RETAIL_READER_ASYNC_EAGER && session==0;
}
#endif
