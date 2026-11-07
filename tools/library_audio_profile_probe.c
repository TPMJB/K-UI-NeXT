/* SPDX-License-Identifier: GPL-3.0-only */
/* Host-only: which CD-music contract the launcher would select for a boot
 * file, using the production fingerprints. Windows CE CD music admits only the
 * exact kernels of its four profiles; native CD music only the exact Toy
 * Commander and MDK2 executables. No retail bytes are embedded here. */
#include "kui/hash.h"
#include "kui/native_cdda_compat.h"
#include "kui/wince_cdda_profiles.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc,char **argv) {
    if(argc!=2) {fprintf(stderr,"Usage: %s BOOT\n",argv[0]);return 2;}
    FILE *file=fopen(argv[1],"rb");if(!file)return 2;
    if(fseek(file,0,SEEK_END))return 2;
    long length=ftell(file);
    if(length<1 || length>0x1000000 || fseek(file,0,SEEK_SET))return 2;
    uint8_t *image=malloc((size_t)length);if(!image)return 2;
    if(fread(image,1,(size_t)length,file)!=(size_t)length || fclose(file))return 2;
    uint8_t sha[32];struct kui_sha256 state;
    kui_sha256_init(&state);kui_sha256_update(&state,image,(size_t)length);kui_sha256_digest(&state,sha);
    uint32_t crc=kui_crc32(0u,image,(size_t)length);
    enum kui_wince_cdda_profile ce=kui_wince_cdda_kernel_profile((uint32_t)length,sha);
    enum kui_native_cdda_binding native=kui_native_cdda_boot_binding((uint32_t)length,crc);
    const struct kui_wince_cdda_disc *disc=kui_wince_cdda_disc(ce);
    printf("{\"ce_music_profile\":\"%s\",\"native_cdda_binding\":\"%s\"}\n",disc?disc->name:"",
        native==KUI_NATIVE_CDDA_TOY?"Toy Commander":native==KUI_NATIVE_CDDA_MDK2?"MDK2":"");
    free(image);return 0;
}
