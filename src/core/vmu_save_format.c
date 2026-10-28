/* SPDX-License-Identifier: GPL-3.0-only */
/* Layout references: Marcus Comstedt, VMS fileheader, VMI, and flashmem pages;
 * pinned KallistiOS dc/vmu_pkg.h and dc/vmufs.h. No external implementation. */
#include "kui/vmu_import.h"
#include "kui/crc16.h"
#include <stdio.h>
#include <string.h>
static unsigned get16(const uint8_t *p){return p[0]|((unsigned)p[1]<<8);}
static uint32_t get32(const uint8_t *p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static void put16(uint8_t *p,unsigned n){p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8);}
static bool fail(char *error,size_t cap,const char *text){if(error&&cap)snprintf(error,cap,"%s",text);return false;}
static bool zeros(const uint8_t *p,size_t n){for(size_t i=0;i<n;i++)if(p[i])return false;return true;}
static bool name(const uint8_t *p,size_t n,bool resource){
    bool padding=false,content=false;
    for(size_t i=0;i<n;i++){
        unsigned c=p[i];if(!c){padding=true;continue;}if(padding)return false;
        if(resource){if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'))return false;}
        else if(c<32||c==127)return false;
        if(c!=' ')content=true;
    }return content;
}
static bool date(unsigned year,unsigned month,unsigned day,unsigned hour,unsigned minute,unsigned second,unsigned weekday){
    static const unsigned days[]={31,28,31,30,31,30,31,31,30,31,30,31};
    if(!year||year>9999||!month||month>12||!day||hour>23||minute>59||second>59||weekday>6)return false;
    unsigned limit=days[month-1]+(month==2&&year%4==0&&(year%100!=0||year%400==0));return day<=limit;
}
static uint8_t bcd(unsigned n){return (uint8_t)((n/10)*16+n%10);}
static unsigned unbcd(uint8_t n){return (n>>4)*10+(n&15);}
static bool timestamp(const uint8_t *p){
    if(zeros(p,8))return true;
    for(unsigned i=0;i<8;i++)if((p[i]&15)>9||(p[i]>>4)>9)return false;
    return date(unbcd(p[0])*100+unbcd(p[1]),unbcd(p[2]),unbcd(p[3]),unbcd(p[4]),unbcd(p[5]),unbcd(p[6]),unbcd(p[7]));
}
bool kui_vmu_save_parse_vmi(const uint8_t *bytes,size_t length,struct kui_vmu_save_metadata *out,char *error,size_t cap){
    if(!out||!bytes||length!=108)return fail(error,cap,"VMI must be exactly 108 bytes");
    memset(out,0,sizeof(*out));
    if(get16(bytes+76)||get16(bytes+78)!=1||get16(bytes+102))return fail(error,cap,"Unsupported VMI version, file count or reserved field");
    if(!name(bytes+80,8,true)||!name(bytes+88,12,false))return fail(error,cap,"VMI has an unsafe resource or VMU filename");
    static const uint8_t sega[4]={'S','E','G','A'};
    for(unsigned i=0;i<4;i++)if(bytes[i]!=(uint8_t)(bytes[80+i]&sega[i]))return fail(error,cap,"VMI resource checksum is invalid");
    unsigned mode=get16(bytes+100);if(mode>3)return fail(error,cap,"Unsupported VMI file mode");
    unsigned year=get16(bytes+68);
    if(!date(year,bytes[70],bytes[71],bytes[72],bytes[73],bytes[74],bytes[75]))return fail(error,cap,"VMI timestamp is invalid");
    uint32_t length_vms=get32(bytes+104);if(!length_vms||length_vms>KUI_VMU_IMPORT_MAX_BYTES)return fail(error,cap,"VMI VMS size is outside the standard VMU limit");
    uint8_t *entry=out->entry;entry[0]=mode&2?0xcc:0x33;entry[1]=mode&1?0xff:0;
    memcpy(entry+4,bytes+88,12);entry[16]=bcd(year/100);entry[17]=bcd(year%100);
    for(unsigned i=0;i<5;i++)entry[18+i]=bcd(bytes[70+i]);
    entry[23]=bcd((bytes[75]+6)%7);
    put16(entry+24,(length_vms+511)/512);put16(entry+26,mode&2?1:0);
    memcpy(out->resource,bytes+80,8);out->source_bytes=length_vms;return true;
}
static uint16_t crc16(const uint8_t *p,size_t n){
    static const uint8_t zero[2]={0,0};
    uint16_t crc=kui_crc16_nibble(0,p,70);
    crc=kui_crc16_nibble(crc,zero,2);
    return kui_crc16_nibble(crc,p+72,n-72);
}
static bool header(const uint8_t *bytes,size_t length,unsigned offset,bool game,char *error,size_t cap){
    size_t start=(size_t)offset*512;if(start>length||length-start<128)return fail(error,cap,"VMU header is truncated");
    const uint8_t *p=bytes+start;unsigned icons=get16(p+64),eye=get16(p+68);
    static const unsigned eyes[]={0,8064,4544,2048};
    if(icons<1||icons>3||eye>3||!zeros(p+76,20))return fail(error,cap,"Unsupported VMU icon, eyecatch or reserved header fields");
    size_t prefix=128+(size_t)icons*512+eyes[eye];
    if(prefix>length-start)return fail(error,cap,"VMU icons or eyecatch exceed the source");
    if(game)return true; /* Game CRC and data length fields are unspecified. */
    uint32_t data=get32(p+72);
    if(data>length-start-prefix)return fail(error,cap,"VMU data length exceeds the source");
    size_t used=start+prefix+data,padded=(used+511)/512*512;
    if(length!=used&&length!=padded)return fail(error,cap,"VMS length is neither its exact package nor its padded blocks");
    if(get16(p+70)!=crc16(p,prefix+data))return fail(error,cap,"VMS package CRC16 is invalid");
    return true;
}
bool kui_vmu_save_parse(enum kui_vmu_save_format format,uint8_t *bytes,size_t length,const struct kui_vmu_save_metadata *metadata,const char *stem,struct kui_vmu_save_info *out,char *error,size_t cap){
    if(!out||!bytes)return fail(error,cap,"Missing VMU save input");
    memset(out,0,sizeof(*out));
    if(format!=KUI_VMU_SAVE_VMS&&format!=KUI_VMU_SAVE_VMI&&format!=KUI_VMU_SAVE_DCI)return fail(error,cap,"Unsupported VMU save format");
    if(format==KUI_VMU_SAVE_DCI){
        if(length<32||length-32>KUI_VMU_IMPORT_MAX_BYTES)return fail(error,cap,"DCI size is outside the standard VMU limit");
        memcpy(out->entry,bytes,32);out->payload_offset=32;length-=32;
        if(length%512)return fail(error,cap,"DCI payload must contain complete VMU blocks");
    }else if(!length||length>KUI_VMU_IMPORT_MAX_BYTES)return fail(error,cap,"VMS size is outside the standard VMU limit");
    else if(format==KUI_VMU_SAVE_VMI){
        if(!metadata||metadata->source_bytes!=length)return fail(error,cap,"Companion VMS length differs from the VMI record");
        memcpy(out->entry,metadata->entry,32);
    }else{
        size_t n=stem?strlen(stem):0;if(!n||n>12||!name((const uint8_t *)stem,n,false))return fail(error,cap,"Bare VMS requires a safe SD filename stem of 1 to 12 characters");
        for(size_t i=0;i<n;i++)if((unsigned char)stem[i]>126)return fail(error,cap,"Bare VMS filename must use printable ASCII");
        out->entry[0]=0x33;memcpy(out->entry+4,stem,n);put16(out->entry+24,(unsigned)((length+511)/512));out->name_from_filename=true;
    }
    uint8_t *entry=out->entry;unsigned blocks=get16(entry+24),offset=get16(entry+26);
    /* A DCI entry keeps its copy-protect byte as written (Crazy Taxi 2: 0x10). */
    if((entry[0]!=0x33&&entry[0]!=0xcc)||!blocks||blocks>255||blocks!=(length+511)/512||offset!=(entry[0]==0xcc?1u:0u)||!zeros(entry+28,4)||!name(entry+4,12,false)||!timestamp(entry+16))return fail(error,cap,"VMU directory metadata is invalid or unsupported");
    if(format==KUI_VMU_SAVE_DCI)for(size_t i=32;i<length+32;i+=4){uint8_t a=bytes[i],b=bytes[i+1];bytes[i]=bytes[i+3];bytes[i+1]=bytes[i+2];bytes[i+2]=b;bytes[i+3]=a;}
    if(!header(bytes+out->payload_offset,length,offset,entry[0]==0xcc,error,cap))return false;
    out->timestamp_known=!zeros(entry+16,8);out->payload_bytes=length;out->padded_bytes=(size_t)blocks*512;return true;
}
