/* SPDX-License-Identifier: GPL-3.0-only */
/* Constructed from the published byte tables, not another implementation or
 * real proprietary saves. A simple bitwise oracle generates the CRC fixtures. */
#include "kui/vmu_import.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static void put16(uint8_t *p,unsigned n){p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8);}
static void put32(uint8_t *p,uint32_t n){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(n>>(8*i));}
static unsigned crc(const uint8_t *p,size_t n){unsigned state=0;for(size_t i=0;i<n;i++){state^=(unsigned)p[i]<<8;for(unsigned j=0;j<8;j++)state=((state<<1)^((state&32768)?0x1021:0))&65535;}return state;}
static size_t payload(uint8_t *p,unsigned icons,unsigned eye,bool game){
    static const unsigned sizes[]={0,8064,4544,2048};unsigned base=game?512:0;size_t used=base+128+icons*512+sizes[eye]+37;
    memset(p,0,16384);memcpy(p+base,"Constructed save",16);memcpy(p+base+16,"Independent format fixture",26);memcpy(p+base+48,"K-UI TEST",9);
    put16(p+base+64,icons);put16(p+base+68,eye);put32(p+base+72,37);
    for(size_t i=base+128;i<used;i++)p[i]=(uint8_t)(i*31+7);
    if(game){put16(p+base+70,0x1234);put32(p+base+72,0xffffffff);}else put16(p+70,crc(p,used));
    return used;
}
static void vmi(uint8_t p[108],size_t bytes,bool game){
    memset(p,0,108);memcpy(p+80,"RESOURCE",8);memcpy(p+88,"TESTSAVE",8);static const uint8_t sega[]={83,69,71,65};for(unsigned i=0;i<4;i++)p[i]=p[80+i]&sega[i];
    put16(p+68,2024);p[70]=2;p[71]=29;p[72]=23;p[73]=59;p[74]=58;p[75]=4;put16(p+78,1);put16(p+100,game?3:0);put32(p+104,(uint32_t)bytes);
}
static void directory(uint8_t p[32],size_t bytes,bool game){memset(p,0,32);p[0]=game?0xcc:0x33;p[1]=0xff;put16(p+2,47);memcpy(p+4,"TWELVECHAR12",12);uint8_t time[]={0x20,0x24,0x02,0x29,0x23,0x59,0x58,0x03};memcpy(p+16,time,8);put16(p+24,(unsigned)(bytes/512));put16(p+26,game?1:0);}
int main(void){
    assert(crc((const uint8_t *)"123456789",9)==0x31c3);
    uint8_t bytes[16384],copy[16416],meta[108];char error[128];struct kui_vmu_save_info info;struct kui_vmu_save_metadata metadata;
    for(unsigned eye=0;eye<4;eye++)for(unsigned icons=1;icons<=3;icons++){
        size_t used=payload(bytes,icons,eye,false),padded=(used+511)/512*512;
        assert(kui_vmu_save_parse(KUI_VMU_SAVE_VMS,bytes,used,NULL,"SAVE",&info,error,sizeof(error)));
        assert(info.payload_bytes==used&&info.padded_bytes==padded&&info.name_from_filename&&!info.timestamp_known&&info.entry[0]==0x33);
        memset(bytes+used,0xa7,padded-used);assert(kui_vmu_save_parse(KUI_VMU_SAVE_VMS,bytes,padded,NULL,"SAVE",&info,error,sizeof(error)));
        assert(!kui_vmu_save_parse(KUI_VMU_SAVE_VMS,bytes,used+1,NULL,"SAVE",&info,error,sizeof(error)));
        bytes[used-1]^=1;assert(!kui_vmu_save_parse(KUI_VMU_SAVE_VMS,bytes,padded,NULL,"SAVE",&info,error,sizeof(error)));
    }
    size_t used=payload(bytes,1,0,false);vmi(meta,used,false);assert(kui_vmu_save_parse_vmi(meta,108,&metadata,error,sizeof(error)));
    assert(metadata.entry[23]==3&&metadata.entry[16]==0x20&&metadata.entry[17]==0x24&&!strcmp(metadata.resource,"RESOURCE"));
    assert(kui_vmu_save_parse(KUI_VMU_SAVE_VMI,bytes,used,&metadata,NULL,&info,error,sizeof(error))&&!info.name_from_filename&&info.timestamp_known&&!memcmp(info.entry+4,"TESTSAVE",8));
    for(unsigned mode=0;mode<4;mode++){vmi(meta,used,false);put16(meta+100,mode);assert(kui_vmu_save_parse_vmi(meta,108,&metadata,error,sizeof(error)));assert(metadata.entry[0]==(mode&2?0xcc:0x33)&&metadata.entry[1]==(mode&1?0xff:0));}
    const unsigned offsets[]={0,76,78,80,88,100,102,68,70,71,72,73,74,75,104};
    const unsigned values[]={255,1,2,'/',0,4,1,0,13,30,24,60,60,7,0};
    for(unsigned i=0;i<sizeof(offsets)/sizeof(offsets[0]);i++){vmi(meta,used,false);unsigned off=offsets[i];meta[off]=(uint8_t)values[i];if(off==68)meta[69]=0;if(off==104)memset(meta+104,0,4);assert(!kui_vmu_save_parse_vmi(meta,108,&metadata,error,sizeof(error)));}
    vmi(meta,used,false);put16(meta+68,2023);assert(!kui_vmu_save_parse_vmi(meta,108,&metadata,error,sizeof(error)));
    vmi(meta,used,false);assert(!kui_vmu_save_parse_vmi(meta,107,&metadata,error,sizeof(error)));assert(!kui_vmu_save_parse_vmi(meta,109,&metadata,error,sizeof(error)));
    assert(!kui_vmu_save_parse(KUI_VMU_SAVE_VMS,bytes,used,NULL,"TOO-LONG-NAME1",&info,error,sizeof(error)));
    assert(!kui_vmu_save_parse(KUI_VMU_SAVE_VMS,bytes,used,NULL,"",&info,error,sizeof(error)));
    assert(!kui_vmu_save_parse(KUI_VMU_SAVE_VMS,bytes,127,NULL,"SAVE",&info,error,sizeof(error)));
    assert(!kui_vmu_save_parse(KUI_VMU_SAVE_VMS,bytes,KUI_VMU_IMPORT_MAX_BYTES+1,NULL,"SAVE",&info,error,sizeof(error)));
    for(unsigned game=0;game<2;game++){
        used=payload(bytes,1,0,game!=0);size_t padded=(used+511)/512*512;
        directory(copy,padded,game!=0);memcpy(copy+32,bytes,padded);
        for(size_t i=32;i<padded+32;i+=4){uint8_t a=copy[i],b=copy[i+1];copy[i]=copy[i+3];copy[i+1]=copy[i+2];copy[i+2]=b;copy[i+3]=a;}
        assert(kui_vmu_save_parse(KUI_VMU_SAVE_DCI,copy,padded+32,NULL,NULL,&info,error,sizeof(error)));
        assert(info.payload_offset==32&&info.padded_bytes==padded&&!memcmp(copy+32,bytes,padded)&&info.entry[2]==47&&info.timestamp_known);
        vmi(meta,padded,game!=0);assert(kui_vmu_save_parse_vmi(meta,108,&metadata,error,sizeof(error)));
        assert(kui_vmu_save_parse(KUI_VMU_SAVE_VMI,bytes,padded,&metadata,NULL,&info,error,sizeof(error)));
        assert(!kui_vmu_save_parse(KUI_VMU_SAVE_VMI,bytes,padded-1,&metadata,NULL,&info,error,sizeof(error)));
    }
    used=payload(bytes,1,0,false);size_t padded=(used+511)/512*512;
    const unsigned bad_offsets[]={0,1,4,24,26,28,29,16,18,19,20,21,22,23};
    const unsigned bad_values[]={0x34,1,0,0,1,1,1,0xfa,0x13,0x30,0x24,0x60,0x60,7};
    for(unsigned i=0;i<sizeof(bad_offsets)/sizeof(bad_offsets[0]);i++){
        directory(copy,padded,false);memcpy(copy+32,bytes,padded);copy[bad_offsets[i]]=(uint8_t)bad_values[i];
        if(bad_offsets[i]==4)memset(copy+4,0,12);
        assert(!kui_vmu_save_parse(KUI_VMU_SAVE_DCI,copy,padded+32,NULL,NULL,&info,error,sizeof(error)));
    }
    directory(copy,padded,false);assert(!kui_vmu_save_parse(KUI_VMU_SAVE_DCI,copy,padded+31,NULL,NULL,&info,error,sizeof(error)));
    for(unsigned i=0;i<5;i++){
        used=payload(bytes,1,0,false);if(i==0)put16(bytes+64,0);if(i==1)put16(bytes+64,4);if(i==2)put16(bytes+68,4);if(i==3)put32(bytes+72,UINT32_MAX);if(i==4)bytes[76]=1;
        assert(!kui_vmu_save_parse(KUI_VMU_SAVE_VMS,bytes,used,NULL,"SAVE",&info,error,sizeof(error)));
    }
    static uint8_t maximum[KUI_VMU_IMPORT_MAX_BYTES];
    put16(maximum+64,1);put32(maximum+72,KUI_VMU_IMPORT_MAX_BYTES-640);
    put16(maximum+70,crc(maximum,sizeof(maximum)));
    assert(kui_vmu_save_parse(KUI_VMU_SAVE_VMS,maximum,sizeof(maximum),NULL,"MAXIMUM",&info,error,sizeof(error)));
    assert(info.padded_bytes==KUI_VMU_IMPORT_MAX_BYTES&&info.entry[24]==255);
    assert(!kui_vmu_save_parse((enum kui_vmu_save_format)-1,bytes,used,NULL,"SAVE",&info,error,sizeof(error)));
    assert(!kui_vmu_save_parse((enum kui_vmu_save_format)3,bytes,used,NULL,"SAVE",&info,error,sizeof(error)));
    puts("PASS VMU save format fixtures");return 0;
}
