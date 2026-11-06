/* SPDX-License-Identifier: GPL-3.0-only */
/* Private to vmu_native_exit.c: this is a terminal SDK firmware check, not
 * an executable loader. Its entry must be intercepted before the write to
 * the firmware target cell at +30. All instructions, delay slots, six pool
 * words and encoded firmware consumers are part of the capability. */
#define KUI_VMU_NATIVE_FIRMWARE_EXIT_INCLUDED 1
static bool firmware_exit_prefix(const uint8_t *p,size_t bytes,size_t at) {
    return bytes-at>=8u && (u16(p+at)&0xff00u)==0xd300u &&
        u16(p+at+2u)==0x6430u && u16(p+at+4u)==0x654eu &&
        u16(p+at+6u)==0x6053u;
}
static bool firmware_exit_body(const uint8_t *p,size_t bytes,size_t at) {
    static const uint16_t body[19]={0xd30au,0x6430u,0x654eu,0x6053u,
        0x8820u,0x8901u,0x2558u,0x8b03u,0xd208u,0x6322u,0x432bu,
        0xe401u,0xd006u,0xd307u,0xd206u,0x2322u,0x6102u,0x412bu,0xe403u};
    static const uint32_t pool[6]={UINT32_C(0x8c0080f0),UINT32_C(0x8c0080fc),
        UINT32_C(0x8c0080fd),UINT32_C(0x8c0000e0),UINT32_C(0x8c008000),
        UINT32_C(0x8c000064)};
    if((at&3u)!=2u || at<4u || bytes-at<62u ||
       u16(p+at-4u)!=0x000bu || u16(p+at-2u)!=0x0009u) return false;
    for(unsigned i=0;i<19u;i++) if(u16(p+at+2u*i)!=body[i]) return false;
    for(unsigned i=0;i<6u;i++) if(u32(p+at+38u+4u*i)!=pool[i]) return false;
    return true;
}
/* The PVR pair contains a peripheral pointer whose high halfword resembles
 * BRA. Its data role requires more than the pointer's value: bind the whole
 * pair first, then close its complete 36-byte pool (including its start) to
 * encoded control, raw pointers, address-of and outside/partial reads.
 * Every admitted load below is already an exact decoded pair witness. */
static bool firmware_exit_pvr_pool(const uint8_t *p,size_t bytes,uint32_t load,size_t pair) {
    uint32_t first=physical(load)+(uint32_t)pair+0x48u,last=first+0x24u;
    for(size_t at=0;at+2u<=bytes;at+=2u) {
        uint16_t op=u16(p+at);int32_t displacement=0;bool branch=false;
        if((op&0xe000u)==0xa000u) {
            displacement=(int32_t)((op&4095u)^2048u)-2048;branch=true;
        } else if((op&0xff00u)==0x8900u || (op&0xff00u)==0x8b00u ||
                  (op&0xff00u)==0x8d00u || (op&0xff00u)==0x8f00u) {
            displacement=(int32_t)((op&255u)^128u)-128;branch=true;
        }
        if(branch) {
            uint32_t target=physical(load+(uint32_t)at+4u+(uint32_t)(2*displacement));
            if(target>=first && target<last) return false;
        }
        if(!((load+(uint32_t)at)&3u) && at+4u<=bytes) {
            uint32_t value=u32(p+at),target=physical(value);
            if(direct(value) && target>=first && target<last) return false;
        }
        uint32_t target;
        if((op&0xf000u)==0xd000u || (op&0xff00u)==0xc700u) {
            target=((physical(load)+(uint32_t)at+4u)&~3u)+4u*(op&255u);
            if(target>=first && target<last && ((op&0xf000u)!=0xd000u ||
               at<pair || at>=pair+0x48u)) return false;
        } else if((op&0xf000u)==0x9000u) {
            target=physical(load)+(uint32_t)at+4u+2u*(op&255u);
            if(target>=first && target<last && at!=pair+2u) return false;
        }
    }
    return true;
}
static bool firmware_exit_closed(const uint8_t *p,size_t bytes,uint32_t load,size_t entry,
    size_t pair,bool pvr) {
    /* Split at the two exact internal branch destinations and the one
     * possible PVR-pool apparent branch destination. The separate scans
     * close every boundary and independently prove that source pool. */
    const struct kui_vmu_native_patch_window windows[4]={
        {load+(uint32_t)entry,16u},{load+(uint32_t)entry+16u,8u},
        {load+(uint32_t)entry+24u,30u},{load+(uint32_t)entry+54u,8u}};
    if(!kui_vmu_native_patch_no_interiors(p,bytes,load,windows,4u)) return false;
    if(pvr && !firmware_exit_pvr_pool(p,bytes,load,pair)) return false;
    uint32_t first=physical(load)+(uint32_t)entry,last=first+62u;
    /* The exact body references words 1,3,4,5. The two remaining shared
     * words require pre-entry MOV.L references; no word is unaccounted. */
    unsigned references=(1u<<1)|(1u<<3)|(1u<<4)|(1u<<5);
    for(size_t at=0;at+2u<=bytes;at+=2u) {
        if(at>=entry && at<entry+62u) continue;
        uint16_t op=u16(p+at);int32_t displacement=0;bool branch=false;
        if((op&0xe000u)==0xa000u) {
            displacement=(int32_t)((op&4095u)^2048u)-2048;branch=true;
        } else if((op&0xff00u)==0x8900u || (op&0xff00u)==0x8b00u ||
                  (op&0xff00u)==0x8d00u || (op&0xff00u)==0x8f00u) {
            displacement=(int32_t)((op&255u)^128u)-128;branch=true;
        }
        if(branch) {
            uint32_t target=physical(load+(uint32_t)at+4u+(uint32_t)(2*displacement));
            if(target==first+16u || target==first+24u || target==first+54u) {
                /* The fully validated PVR pair's immutable peripheral
                 * literal is 0xa05f811c. Its high halfword is data, even
                 * when its apparent BRA reaches this exact pool boundary.
                 * No other source or destination receives this role. */
                if(!pvr || at!=pair+0x4eu || target!=first+54u ||
                   op!=0xa05fu || u32(p+pair+0x4cu)!=UINT32_C(0xa05f811c)) return false;
            }
        }
        if(!((load+(uint32_t)at)&3u) && at+4u<=bytes) {
            uint32_t value=u32(p+at),target=physical(value);
            if(direct(value) && (target==first+16u || target==first+24u || target==first+54u)) return false;
        }
        uint32_t target;
        if((op&0xf000u)==0xd000u || (op&0xff00u)==0xc700u) {
            target=((physical(load)+(uint32_t)at+4u)&~3u)+4u*(op&255u);
            if(target>=first && target<last) {
                /* Outside code may share only the three original flag
                 * words, via full-word pre-entry loads. Firmware words
                 * cannot be borrowed to bypass the intercepted entry. */
                if((op&0xf000u)!=0xd000u || at>=entry || target<first+38u ||
                   target>=first+50u || ((target-first-38u)&3u)) return false;
                references|=1u<<((target-first-38u)/4u);
            }
        } else if((op&0xf000u)==0x9000u) {
            target=physical(load)+(uint32_t)at+4u+2u*(op&255u);
            /* An apparent partial-word read is still refused when its
             * source has no independently proved public data role. */
            if(target>=first && target<last) return false;
        }
    }
    return references==63u;
}
static int firmware_exit_extra(const uint8_t *p,size_t bytes,uint32_t load,
    size_t pair,bool early,bool pvr,const struct exit_shape *pair_shape,size_t *entry_out) {
    if(!p || !entry_out || bytes<62u || bytes>UINT32_C(0xc00000) ||
       (load&3u) || !direct(load) || physical(load)<UINT32_C(0x0c010000) ||
       bytes>UINT32_C(0x0d000000)-physical(load) || pair>bytes ||
       bytes-pair<(early?36u:pvr?108u:96u) || (early&&pvr) ||
       (!early && !pair_shape)) return -1;
    /* Do not turn caller-supplied geometry into source data-role evidence.
     * Rebind the complete pair before using any of its immutable pool. */
    uint32_t cleanup;
    if(early?!exit_early_pair(p,bytes,load,pair,&cleanup):
       pvr?!exit_pvr_pair(p,bytes,load,pair,&cleanup):!exit_pair(p,bytes,load,pair,&cleanup)) return -1;
    if(!early && (pair_shape->second!=(pvr?54u:48u) ||
       pair_shape->vector!=(pvr?0x64u:0x58u) || pair_shape->boot!=(pvr?26u:20u) ||
       pair_shape->vector_loads[0]!=(pvr?38u:32u) ||
       pair_shape->vector_loads[1]!=(pvr?46u:40u) ||
       pair_shape->vector_loads[2]!=(pvr?62u:56u))) return -1;
    size_t selected=SIZE_MAX;unsigned matches=0u;
    for(size_t at=0;at+8u<=bytes;at+=2u) if(firmware_exit_prefix(p,bytes,at)) {
        if(!firmware_exit_body(p,bytes,at) || ++matches>1u) return -1;
        selected=at;
    }
    unsigned vectors=0u,boots=0u,extra=0u;
    for(size_t at=0;at+2u<=bytes;at+=2u) {
        uint16_t op=u16(p+at);uint32_t address,value;
        if((op&0xf000u)!=0xd000u ||
           !literal(p,bytes,load,at,(op>>8)&15u,&address,&value) || !direct(value)) continue;
        if(physical(value)==UINT32_C(0x0c0000e0)) {
            bool owned=early?(at==pair || at==pair+16u):
                (at==pair+pair_shape->vector_loads[0] || at==pair+pair_shape->vector_loads[1] ||
                 at==pair+pair_shape->vector_loads[2]);
            if(owned) ++vectors;
            else {
                if(selected==SIZE_MAX || (at!=selected+16u && at!=selected+24u)) return -1;
                ++extra;
            }
        } else if(physical(value)==UINT32_C(0x0c008300)) {
            if(early || at!=pair+pair_shape->boot) return -1;
            ++boots;
        }
    }
    if(vectors!=(early?2u:3u) || boots!=(early?0u:1u)) return -1;
    if(!matches) return 0;
    if(extra!=2u || !firmware_exit_closed(p,bytes,load,selected,pair,pvr)) return -1;
    *entry_out=selected;return 1;
}
