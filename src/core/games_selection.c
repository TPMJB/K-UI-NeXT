/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/games_selection.h"
#include <string.h>

static uint32_t get32(const uint8_t *p) {return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void put32(uint8_t *p,uint32_t n) {for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(n>>(8u*i));}
static uint64_t get64(const uint8_t *p) {return (uint64_t)get32(p)|(uint64_t)get32(p+4)<<32;}
static void put64(uint8_t *p,uint64_t n) {put32(p,(uint32_t)n);put32(p+4,(uint32_t)(n>>32));}
void kui_games_selection_default(struct kui_games_selection *s) {
    if(s) {*s=(struct kui_games_selection){0};strcpy(s->root,"/Games");}
}
bool kui_games_selection_valid(const struct kui_games_selection *s) {
    char root[KUI_DEST_ROOT_CAP];
    return s && memchr(s->root,0,sizeof(s->root)) && memchr(s->name,0,sizeof(s->name)) &&
        kui_destination_normalize(root,s->root) && !strcmp(root,s->root) &&
        (!s->name[0] || kui_destination_name_valid(s->name));
}
bool kui_games_selection_equal(const struct kui_games_selection *a,const struct kui_games_selection *b) {
    return kui_games_selection_valid(a) && kui_games_selection_valid(b) && !strcmp(a->root,b->root) && !strcmp(a->name,b->name);
}
bool kui_games_selection_encode(uint8_t out[KUI_GAMES_SELECTION_BYTES],const struct kui_games_selection *s,uint64_t sequence) {
    if(!out || !sequence || !kui_games_selection_valid(s))return false;
    uint8_t record[KUI_GAMES_SELECTION_BYTES]={0};memcpy(record,"KUIGSEL1",8);put32(record+8,1);put64(record+16,sequence);
    memcpy(record+32,s->root,strlen(s->root));memcpy(record+32+KUI_DEST_ROOT_CAP,s->name,strlen(s->name));
    put32(record+508,kui_crc32(0,record,508));memcpy(out,record,sizeof(record));return true;
}
bool kui_games_selection_decode(struct kui_games_selection *out,uint64_t *sequence,const uint8_t record[KUI_GAMES_SELECTION_BYTES]) {
    if(!out || !sequence || !record || memcmp(record,"KUIGSEL1",8) || get32(record+8)!=1 ||
       get32(record+508)!=kui_crc32(0,record,508))return false;
    struct kui_games_selection s;memcpy(s.root,record+32,sizeof(s.root));memcpy(s.name,record+32+KUI_DEST_ROOT_CAP,sizeof(s.name));
    uint64_t seq=get64(record+16);uint8_t canonical[KUI_GAMES_SELECTION_BYTES];
    if(!kui_games_selection_encode(canonical,&s,seq) || memcmp(canonical,record,sizeof(canonical)))return false;
    *out=s;*sequence=seq;return true;
}
