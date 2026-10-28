/* SPDX-License-Identifier: GPL-3.0-only */
/* Host-only: whether New may use the reader-only module for a startup. The
 * launcher calls the same finder on the whole boot file once the combined
 * module's discovery refuses it. No retail bytes are embedded here. */
#include "kui/native_sdk_sci_family.h"
#include "kui/native_sdk_sci_route.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc,char **argv) {
    if(argc!=2) {fprintf(stderr,"Usage: %s STARTUP\n",argv[0]);return 2;}
    FILE *file=fopen(argv[1],"rb");if(!file)return 2;
    if(fseek(file,0,SEEK_END))return 2;
    long length=ftell(file);
    if(length<1 || length>(long)KUI_NATIVE_SDK_SCI_ROUTE_MAX_IMAGE_BYTES || fseek(file,0,SEEK_SET))return 2;
    uint8_t *image=malloc((size_t)length);if(!image)return 2;
    if(fread(image,1,(size_t)length,file)!=(size_t)length || fclose(file))return 2;
    struct kui_native_sdk_sci_family family;
    int found=kui_native_sdk_sci_family_find(image,(size_t)length,KUI_NATIVE_SDK_SCI_ROUTE_LOAD,&family);
    printf("{\"sdk_sci_family\":%d,\"sci_vector\":%u,\"sci_normal\":%u,\"sci_fpu\":%u,\"sci_original\":%u}\n",
        found,family.vector,family.normal,family.fpu,family.original);
    free(image);return 0;
}
