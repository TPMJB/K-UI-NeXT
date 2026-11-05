/* SPDX-License-Identifier: GPL-3.0-only */
#include "../src/core/games_cdda_profile.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static uint32_t word(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
int main(int argc,char **argv) {
    assert(argc==4);
    for(unsigned i=0u;i<3u;i++) {
        FILE *f=fopen(argv[i+1u],"rb");assert(f && !fseek(f,0,SEEK_END));
        long size=ftell(f);assert(size>192 && size<1048576 && !fseek(f,0,SEEK_SET));
        uint8_t *bytes=malloc((size_t)size);assert(bytes);
        assert(fread(bytes,1,(size_t)size,f)==(size_t)size && !fclose(f));
        struct kui_runtime_image image={.data=bytes+64u};
        image.info.payload_bytes=word(bytes+16u);image.info.memory_bytes=word(bytes+28u);
        image.info.crc32=word(bytes+32u);memcpy(image.info.build,bytes+40u,12u);
        enum kui_games_cdda_profile exact=(enum kui_games_cdda_profile)(i+1u);
        enum kui_games_cdda_profile compat=(enum kui_games_cdda_profile)(i+4u);
        assert(kui_games_cdda_descriptor_admit(compat,&image));
#ifdef KUI_NATIVE_CDDA_MANIFEST
        assert(kui_games_cdda_package_admit(exact,&image));
#else
        assert(!kui_games_cdda_package_admit(exact,&image));
#endif
        for(unsigned j=1u;j<=3u;j++)if(j!=i+1u)
            assert(!kui_games_cdda_package_admit((enum kui_games_cdda_profile)j,&image));
        /* A repaired CRC cannot authorize changed code at an exact old path. */
        bytes[size-1]^=1u;image.info.crc32=kui_retail_crc32(0u,image.data,image.info.payload_bytes);
        assert(!kui_games_cdda_package_admit(exact,&image));
        bytes[size-1]^=1u;image.info.crc32=word(bytes+32u);
        image.info.build[0]^=1u;assert(!kui_games_cdda_package_admit(exact,&image));
        image.info.build[0]^=1u;image.info.memory_bytes+=4u;
        assert(!kui_games_cdda_package_admit(exact,&image));image.info.memory_bytes-=4u;
        unsigned descriptor=0u;
        for(unsigned at=0u;at+128u<=image.info.payload_bytes;at++)
            if(!memcmp(bytes+64u+at,"KUINCD01",8u)){descriptor=at;break;}
        assert(descriptor);
        bytes[64u+descriptor+24u]^=1u;
        image.info.crc32=kui_retail_crc32(0u,image.data,image.info.payload_bytes);
        assert(!kui_games_cdda_package_admit(exact,&image));
        assert(!kui_games_cdda_package_admit(compat,&image));
        free(bytes);
    }
    puts("PASS exact native CDDA cohort: all three aliases, gate-off, cross-profile, modified code/build/geometry/descriptor refusal");
    return 0;
}
