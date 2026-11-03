/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/games_query.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>
static unsigned char lower(unsigned char c) {return c>='A' && c<='Z'?(unsigned char)(c+32):c;}
static bool text_size(const char *text,size_t capacity,size_t *bytes) {
    if(!text)return false;
    size_t at=0;
    while(at<capacity && text[at]) {
        unsigned char c=(unsigned char)text[at++];
        if(c<32 || c==127)return false;
        if(c<128)continue;
        unsigned following;uint32_t point,minimum;
        if(c>=0xc2 && c<=0xdf) {following=1;point=c&31u;minimum=0x80u;}
        else if(c>=0xe0 && c<=0xef) {following=2;point=c&15u;minimum=0x800u;}
        else if(c>=0xf0 && c<=0xf4) {following=3;point=c&7u;minimum=0x10000u;}
        else return false;
        if(following>=capacity-at)return false;
        for(unsigned i=0;i<following;i++) {
            unsigned char next=(unsigned char)text[at++];
            if((next&0xc0u)!=0x80u)return false;
            point=(point<<6)|(next&63u);
        }
        if(point<minimum || point>0x10ffffu || (point>=0xd800u && point<=0xdfffu) || point<0xa0u)return false;
    }
    if(at==capacity)return false;
    if(bytes)*bytes=at;
    return true;
}
void kui_games_query_default(struct kui_games_query *query) {if(query)memset(query,0,sizeof(*query));}
bool kui_games_query_valid(const struct kui_games_query *query) {
    return query && query->activity<3u && query->region<KUI_GAMES_REGION_COUNT && query->disc<=99u &&
        text_size(query->text,sizeof(query->text),NULL);
}
static bool contains(const char *haystack,size_t hay_bytes,const char *needle,size_t needle_bytes) {
    if(needle_bytes>hay_bytes)return false;
    for(size_t start=0;start<=hay_bytes-needle_bytes;start++) {
        size_t i=0;
        while(i<needle_bytes && lower((unsigned char)haystack[start+i])==lower((unsigned char)needle[i]))++i;
        if(i==needle_bytes)return true;
    }
    return false;
}
static unsigned region_flags(const char *region,size_t bytes) {
    unsigned mask=0;
    for(size_t i=0;i<bytes;i++) {
        unsigned char c=lower((unsigned char)region[i]);
        if(c=='j')mask|=1u;
        else if(c=='u')mask|=2u;
        else if(c=='e')mask|=4u;
        else if(c!=' ')return 0;
    }
    return mask;
}
bool kui_games_query_matches(const struct kui_games_query *query,const char *title,
    const char *fallback_name,const char *region,unsigned disc) {
    size_t title_bytes,name_bytes,region_bytes,needle_bytes;
    if(!kui_games_query_valid(query) || disc>99u ||
       !text_size(title,KUI_GAMES_QUERY_TITLE_CAP,&title_bytes) ||
       !text_size(fallback_name,KUI_GAMES_QUERY_NAME_CAP,&name_bytes) ||
       !text_size(region,KUI_GAMES_QUERY_REGION_CAP,&region_bytes) ||
       !text_size(query->text,sizeof(query->text),&needle_bytes))return false;
    if(query->disc && query->disc!=disc)return false;
    unsigned flags=region_flags(region,region_bytes);
    if(query->region==KUI_GAMES_REGION_UNKNOWN && flags)return false;
    if(query->region>=KUI_GAMES_REGION_JAPAN && query->region<=KUI_GAMES_REGION_EUROPE &&
       !(flags&(1u<<(query->region-KUI_GAMES_REGION_JAPAN))))return false;
    return !needle_bytes || contains(title,title_bytes,query->text,needle_bytes) ||
        contains(fallback_name,name_bytes,query->text,needle_bytes);
}
char kui_games_query_letter(const char *title,const char *fallback_name) {
    size_t title_bytes,name_bytes;
    if(!text_size(title,KUI_GAMES_QUERY_TITLE_CAP,&title_bytes) ||
       !text_size(fallback_name,KUI_GAMES_QUERY_NAME_CAP,&name_bytes))return 0;
    const char *text=title_bytes?title:fallback_name;
    while(*text==' ')++text;
    unsigned char c=lower((unsigned char)*text);
    return c>='a' && c<='z'?(char)(c-'a'+'A'):'#';
}
bool kui_games_query_jump_matches(const char *title,const char *fallback_name,char letter) {
    unsigned char c=lower((unsigned char)letter);
    if(c!='#' && !(c>='a' && c<='z'))return false;
    char bucket=kui_games_query_letter(title,fallback_name);
    return bucket && lower((unsigned char)bucket)==c;
}
const char *kui_games_query_region_label(unsigned region) {
    static const char *const labels[]={"Any region","Japan","USA","Europe","Unknown"};
    return region<KUI_GAMES_REGION_COUNT?labels[region]:NULL;
}
