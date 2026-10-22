/* SPDX-License-Identifier: GPL-3.0-only */
/* Include the implementation so this regression can reach the formerly
 * unchecked internal page reader, without adding a runtime test API. */
#include "../src/apps/music_ogg.c"
#include <assert.h>
#include <stdio.h>

static int probe(unsigned char *page,size_t available,ProbedPage *out) {
    stb_vorbis decoder={0};
    decoder.stream_start=decoder.stream=page;
    decoder.stream_end=page+available;decoder.stream_len=(uint32)available;
    int result=get_seek_page_info(&decoder,out);
    if(result) assert(decoder.stream==decoder.stream_start);
    return result;
}
int main(void) {
    unsigned char page[512]={0};memcpy(page,"OggS",4);
    ProbedPage info={.page_end=0x11223344,.last_decoded_sample=0x55667788};
    for(size_t bytes=0;bytes<27u;bytes++) {
        assert(!probe(page,bytes,&info));
        assert(info.page_end==0x11223344 && info.last_decoded_sample==0x55667788);
    }
    page[26]=255;
    for(size_t bytes=27u;bytes<282u;bytes++) {
        assert(!probe(page,bytes,&info));
        assert(info.page_end==0x11223344 && info.last_decoded_sample==0x55667788);
    }
    /* A complete lacing table is accepted and all its bytes contribute. */
    memset(page+27,1,255);page[6]=0x98;page[7]=0xba;page[8]=0xdc;page[9]=0xfe;
    assert(probe(page,282,&info));
    assert(info.page_start==0 && info.page_end==537 && info.last_decoded_sample==0xfedcba98u);
    page[26]=0;memset(page+6,0,4);assert(probe(page,27,&info));
    assert(info.page_end==27 && info.last_decoded_sample==0);
    page[0]='X';assert(!probe(page,27,&info));
    puts("PASS Vorbis seek-page: short header/lacing rejected, complete metadata and unsigned granule accepted");
    return 0;
}
