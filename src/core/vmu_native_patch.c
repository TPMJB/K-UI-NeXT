/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_native_patch.h"
static uint32_t physical(uint32_t a) {return a&0x1fffffffu;}
static bool ram(uint32_t a,uint32_t n) {
    uint32_t p=physical(a),area=a&0xe0000000u;
    return (area==0u || area==0x80000000u || area==0xa0000000u) && n &&
        p>=0x0c010000u && p<0x0d000000u && n<=0x0d000000u-p;
}
static uint16_t halfword(const uint8_t *p) {
    return (uint16_t)(p[0]|(uint16_t)p[1]<<8);
}
static uint32_t pointer(const uint8_t *p) {
    return (uint32_t)halfword(p)|(uint32_t)halfword(p+2u)<<16;
}
static bool branch_target(uint16_t op,uint32_t pc,uint32_t *target) {
    int32_t displacement;
    if((op&0xe000u)==0xa000u) {
        displacement=(int32_t)(op&0xfffu);if(displacement&0x800) displacement-=0x1000;
    } else if((op&0xff00u)==0x8900u || (op&0xff00u)==0x8b00u ||
              (op&0xff00u)==0x8d00u || (op&0xff00u)==0x8f00u) {
        displacement=(int32_t)(op&255u);if(displacement&128) displacement-=256;
    } else return false;
    *target=physical(pc+4u+(uint32_t)(displacement*2));return true;
}
struct pointer_pool_node {
    uint32_t begin,end,first,cutoff,references,scan,state,height;
    uint32_t mask_word,mask_references;
};
struct pointer_pool_graph {
    struct pointer_pool_node nodes[KUI_VMU_NATIVE_PATCH_POOL_NODES];
    const uint32_t *begins,*ends;
    unsigned count,windows;
};
/* An exact pure dispatcher supplies the second cold root shape. Every arm
 * loads one of four RAM-pointer words and tail-jumps through R3; the final
 * NOP and zero alignment word cannot fall through into the pool. */
static bool pool_dispatcher(const uint8_t *p,uint32_t start,uint32_t count) {
    static const uint16_t code[22]={0x6063u,0x8801u,0x8905u,0x8802u,0x8906u,
        0x8803u,0x8907u,0xa009u,0x0009u,0xd306u,0x432bu,0x0009u,
        0xd305u,0x432bu,0x0009u,0xd305u,0x432bu,0x0009u,
        0xd304u,0x432bu,0x0009u,0x0000u};
    if(count!=4u || start<sizeof(code)) return false;
    for(unsigned i=0;i<22u;i++)
        if(halfword(p+start-sizeof(code)+2u*i)!=code[i]) return false;
    return true;
}
static bool pool_return_delay(uint16_t op) {
    return op==0x0009u || ((op&0xf0ffu)==0x60f6u && (op&0x0f00u)!=0x0f00u);
}
/* This one-way SDK tail veneer shares a halfword IMASK mask with the
 * returning body before it. The veneer loads the final pool word and cannot
 * fall through its JMP/NOP into that mask or the pointer island. */
static bool pool_mask_veneer(const uint8_t *p,uint32_t bytes,uint32_t load,
    uint32_t start,uint32_t end) {
    if(start<12u || halfword(p+start-12u)!=0x000bu || !pool_return_delay(halfword(p+start-10u)) ||
       (halfword(p+start-8u)&0xff00u)!=0xd300u || halfword(p+start-6u)!=0x432bu ||
       halfword(p+start-4u)!=0x0009u || halfword(p+start-2u)!=UINT16_C(0xff0f))return false;
    uint32_t literal=((physical(load)+start-4u)&~3u)+4u*(halfword(p+start-8u)&255u);
    uint32_t target=pointer(p+end-4u);
    return literal==physical(load)+end-4u && !(target&1u) &&
        physical(target)>=physical(load) && physical(target)-physical(load)<=bytes-2u;
}
/* The only permitted partial-word readers of that FF0F mask are complete
 * SDK IMASK restore sequences. Other MOV.W/ADR uses retain the ordinary
 * rejection. Every admitted mask requires exactly two such readers. */
static bool pool_mask_restore(const uint8_t *p,uint32_t bytes,uint32_t pos) {
    static const uint16_t code[]={0x0202u,0x60c3u,0u,0xc90fu,0x4008u,
        0x2239u,0x4008u,0x202bu,0x400eu};
    if(pos<4u || pos>bytes || bytes-pos<14u || (halfword(p+pos)&0xff00u)!=0x9300u)return false;
    for(unsigned i=0u;i<sizeof(code)/sizeof(code[0]);i++)
        if(i!=2u && halfword(p+pos-4u+2u*i)!=code[i])return false;
    return true;
}
/* One scalar can split an otherwise maximal RAM-pointer island. Its unique
 * return/delay root and the two pointer runs give immutable boundaries; the
 * scalar must be internal, never a guessed end delimiter. The ordinary
 * full-image graph below still requires every word's complete MOV.L reference
 * and rejects every encoded branch/address/partial-word entry to the island. */
static bool pool_mixed_shape(const uint8_t *p,uint32_t bytes,uint32_t at,
    struct pointer_pool_node *node) {
    enum { MAX_POOL_BYTES=128u };
    uint32_t word=at&~3u,minimum=word>=MAX_POOL_BYTES-4u?word-(MAX_POOL_BYTES-4u):0u;
    unsigned found=0u;struct pointer_pool_node selected={0};
    for(uint32_t start=minimum;start<=word;start+=4u) {
        uint32_t first,cutoff;
        if(start>=4u && halfword(p+start-4u)==0x000bu && pool_return_delay(halfword(p+start-2u))) {
            first=start-2u;cutoff=start-4u;
        } else if(start>=6u && halfword(p+start-6u)==0x000bu && pool_return_delay(halfword(p+start-4u)) &&
                  (halfword(p+start-2u)==0u || halfword(p+start-2u)==UINT16_C(0xffff))) {
            first=start-4u;cutoff=start-6u;
        } else continue;
        uint32_t end=start;bool mixed=false,bad=false;
        while(end<=bytes-4u && end-start<MAX_POOL_BYTES) {
            if(ram(pointer(p+end),1u)) {end+=4u;continue;}
            bool internal=end>start && end<=bytes-8u && ram(pointer(p+end+4u),1u);
            if(!internal)break;
            if(mixed) {bad=true;break;}
            mixed=true;end+=4u;
        }
        if(bad || !mixed || end-start<12u || word>=end ||
           (end<=bytes-4u && ram(pointer(p+end),1u)))continue;
        if(++found>1u)return false;
        selected=(struct pointer_pool_node){start,end,first,cutoff,0u,0u,0u,1u,0u,0u};
    }
    if(found)*node=selected;
    return found==1u;
}
/* Classification alone never admits data. The separate full-image scan below
 * must prove every reference and discharge every incoming branch source. */
static bool pool_shape(const uint8_t *p,uint32_t bytes,uint32_t load,uint32_t at,
    struct pointer_pool_node *node) {
    enum { MAX_POOL_BYTES=128u };
    uint32_t word=at&~3u,start=word,end=word+4u;
    if((load&3u) || bytes-word<4u || !ram(pointer(p+word),1u)) return false;
    while(start>=4u && word-start<MAX_POOL_BYTES && ram(pointer(p+start-4u),1u)) start-=4u;
    while(end<=bytes-4u && end-start<MAX_POOL_BYTES && ram(pointer(p+end),1u)) end+=4u;
    uint32_t count=(end-start)/4u;
    if(start<4u || count<2u || end-start>MAX_POOL_BYTES ||
       (start>=4u && ram(pointer(p+start-4u),1u)) ||
       (end<=bytes-4u && ram(pointer(p+end),1u))) return pool_mixed_shape(p,bytes,at,node);
    uint16_t delay=halfword(p+start-2u);
    uint32_t first,cutoff,mask_word=0u;
    if(halfword(p+start-4u)==0x000bu &&
       (delay==0x0009u || ((delay&0xf0ffu)==0x60f6u && (delay&0x0f00u)!=0x0f00u))) {
        first=start-2u;cutoff=start-4u;
    } else if(pool_dispatcher(p,start,count)) {
        first=start-4u;cutoff=start-6u;
    } else if(pool_mask_veneer(p,bytes,load,start,end)) {
        first=start-4u;cutoff=start-6u;mask_word=start-2u;
    } else return pool_mixed_shape(p,bytes,at,node);
    *node=(struct pointer_pool_node){start,end,first,cutoff,0u,0u,0u,1u,mask_word,0u};return true;
}
static int pool_node(const uint8_t *p,uint32_t bytes,uint32_t load,uint32_t at,
    struct pointer_pool_graph *graph) {
    for(unsigned i=0;i<graph->count;i++)
        if(at>=graph->nodes[i].begin && at<graph->nodes[i].end) return (int)i;
    struct pointer_pool_node node;
    if(!pool_shape(p,bytes,load,at,&node) || graph->count==KUI_VMU_NATIVE_PATCH_POOL_NODES) return -1;
    for(unsigned i=0;i<graph->count;i++)
        if(node.begin<graph->nodes[i].end && graph->nodes[i].begin<node.end) return -1;
    for(unsigned i=0;i<graph->windows;i++)
        if(physical(load)+node.begin<graph->ends[i] &&
           graph->begins[i]<physical(load)+node.end) return -1;
    graph->nodes[graph->count]=node;return (int)graph->count++;
}
/* Iterative depth-first admission: VISITING is never evidence. A source may
 * be ignored only after its complete independent node becomes DONE. Raw RAM
 * pointers and PC-relative address/partial-word transports are never ignored,
 * even inside DONE pools. No recursion, allocation or image mutation occurs. */
static bool pointer_pool(const uint8_t *p,uint32_t bytes,uint32_t load,uint32_t at,
    struct pointer_pool_graph *graph,uint32_t *pool_begin,uint32_t *pool_end) {
    int root=pool_node(p,bytes,load,at,graph);
    if(root<0) return false;
    unsigned pending[KUI_VMU_NATIVE_PATCH_POOL_DEPTH],depth=1u;
    pending[0]=(unsigned)root;
    if(!graph->nodes[root].state) graph->nodes[root].state=1u;
    while(depth) {
        struct pointer_pool_node *node=&graph->nodes[pending[depth-1u]];
        if(node->state==2u) {
            if(depth>1u) {
                struct pointer_pool_node *parent=&graph->nodes[pending[depth-2u]];
                if(node->height>=KUI_VMU_NATIVE_PATCH_POOL_DEPTH) return false;
                if(parent->height<=node->height) parent->height=node->height+1u;
            }
            --depth;continue;
        }
        if(node->scan+2u>bytes) {
            unsigned count=(node->end-node->begin)/4u;
            uint32_t mask=count==32u?UINT32_MAX:(UINT32_C(1)<<count)-1u;
            if(node->references!=mask || (node->mask_word && node->mask_references!=2u)) return false;
            node->state=2u;continue;
        }
        uint32_t pos=node->scan,first=physical(load)+node->first,last=physical(load)+node->end;
        node->scan+=2u;
        uint16_t op=halfword(p+pos);uint32_t target;
        /* Check all non-branch properties before descending, so resuming a
         * parent can never skip the raw pointer at a dependency's source. */
        if(!((load+pos)&3u) && pos+4u<=bytes) {
            uint32_t value=pointer(p+pos);target=physical(value);
            if(ram(value,1u) && target>=first && target<last) return false;
        }
        if((op&0xf000u)==0xd000u || (op&0xff00u)==0xc700u) {
            target=((physical(load)+pos+4u)&~3u)+4u*(op&255u);
            if(target>=first && target<last) {
                if((op&0xf000u)!=0xd000u || target<physical(load)+node->begin ||
                   pos>=node->cutoff || target+4u>last) return false;
                node->references|=UINT32_C(1)<<((target-physical(load)-node->begin)/4u);
            }
        } else if((op&0xf000u)==0x9000u) {
            target=physical(load)+pos+4u+2u*(op&255u);
            if(target>=first && target<last) {
                if(!node->mask_word || target!=physical(load)+node->mask_word ||
                   pos>=node->cutoff || !pool_mask_restore(p,bytes,pos) ||
                   ++node->mask_references>2u)return false;
            }
        }
        if(branch_target(op,load+pos,&target) && target>=first && target<last) {
            int dependency=pool_node(p,bytes,load,pos,graph);
            if(dependency<0 || graph->nodes[dependency].state==1u) return false;
            if(graph->nodes[dependency].state==2u) {
                uint32_t height=graph->nodes[dependency].height;
                if(height>=KUI_VMU_NATIVE_PATCH_POOL_DEPTH) return false;
                if(node->height<=height) node->height=height+1u;
                continue;
            }
            if(depth==KUI_VMU_NATIVE_PATCH_POOL_DEPTH) return false;
            graph->nodes[dependency].state=1u;pending[depth++]=(unsigned)dependency;
        }
    }
    *pool_begin=graph->nodes[root].begin;*pool_end=graph->nodes[root].end;return true;
}
bool kui_vmu_native_patch_encode(uint32_t address,uint32_t target,uint16_t out[6]) {
    uint32_t area=target&UINT32_C(0xe0000000);
    if(!out || (address&1u) || (target&1u) || !ram(address,12u) ||
       (area!=UINT32_C(0x80000000) && area!=UINT32_C(0xa0000000)) ||
       physical(target)<UINT32_C(0x0c000000) || physical(target)>UINT32_C(0x0cfffffe)) return false;
    uint16_t patch[6]={UINT16_C(0xd001),UINT16_C(0x402b),9u,9u,9u,9u};
    unsigned literal=(address&2u)?3u:4u;
    patch[literal]=(uint16_t)target;patch[literal+1u]=(uint16_t)(target>>16);
    for(unsigned i=0;i<6u;i++) out[i]=patch[i];
    return true;
}
bool kui_vmu_native_patch_encode_head(uint32_t address,uint16_t out[4]) {
    if(!out || (address&1u) || !ram(address,20u)) return false;
    const uint16_t head[4]=KUI_VMU_NATIVE_PATCH_HEAD;
    for(unsigned i=0;i<4u;i++) out[i]=head[i];
    return true;
}
bool kui_vmu_native_patch_no_interiors(const void *image,size_t bytes,uint32_t load,
    const struct kui_vmu_native_patch_window *windows,size_t count) {
    if(!image || bytes>0xc00000u || bytes<2u ||
       bytes>UINTPTR_MAX-(uintptr_t)image || !ram(load,(uint32_t)bytes) ||
       !windows || !count || count>9u || (load&1u)) return false;
    uint32_t begins[9],ends[9],first=UINT32_MAX,last=0u;
    for(size_t i=0;i<count;i++) {
        uint32_t begin=windows[i].begin,span=windows[i].bytes;
        if(!ram(begin,span) || ((begin|span)&1u) || span<2u ||
           physical(begin)<physical(load) || physical(begin)-physical(load)>bytes ||
           span>bytes-(physical(begin)-physical(load))) return false;
        begins[i]=physical(begin);ends[i]=begins[i]+span;
        for(size_t j=0;j<i;j++) if(begins[i]<ends[j] && begins[j]<ends[i]) return false;
        if(begins[i]<first) first=begins[i];
        if(ends[i]>last) last=ends[i];
    }
    const uint8_t *data=image;uint32_t pool_begin=0u,pool_end=0u;
    struct pointer_pool_graph pools;
    pools.count=0u;pools.windows=(unsigned)count;pools.begins=begins;pools.ends=ends;
    for(uint32_t at=0;at+2u<=bytes;at+=2u) {
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
        /* The admitted windows are usually clustered in a small SDK region.
         * Most executable/data halfwords cannot reach it. Do not reread and
         * normalize every window for each unrelated branch or RAM literal. */
        uint32_t target;
        if(branch) {
            target=physical(load+at+4u+(uint32_t)(displacement*2));
            if(target>first && target<last)
                for(size_t i=0;i<count;i++) if(target>begins[i] && target<ends[i]) {
                    if(!(at>=pool_begin && at<pool_end) &&
                       !pointer_pool(data,(uint32_t)bytes,load,at,&pools,&pool_begin,&pool_end)) return false;
                    for(size_t j=0;j<count;j++)
                        if(physical(load)+pool_begin<ends[j] &&
                           begins[j]<physical(load)+pool_end) return false;
                    break;
                }
        }
        if(!((load+at)&3u) && at+4u<=bytes) {
            uint32_t pointer=op|(uint32_t)data[at+2u]<<16|(uint32_t)data[at+3u]<<24;
            target=physical(pointer);
            if(target>first && target<last && ram(pointer,1u))
                for(size_t i=0;i<count;i++) if(target>begins[i] && target<ends[i]) return false;
        }
    }
    return true;
}
bool kui_vmu_native_patch_no_interior(const void *image,size_t bytes,uint32_t load,
    uint32_t begin,uint32_t span) {
    const struct kui_vmu_native_patch_window window={begin,span};
    return kui_vmu_native_patch_no_interiors(image,bytes,load,&window,1u);
}
