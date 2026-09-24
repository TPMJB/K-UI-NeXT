/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/destination.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void normalized(const char *source,const char *wanted) {
    char out[KUI_DEST_ROOT_CAP];assert(kui_destination_normalize(out,source));assert(!strcmp(out,wanted));
}
static void title(const char *source,const char *wanted) {
    char out[KUI_DEST_TITLE_CAP];kui_destination_title(out,source);assert(!strcmp(out,wanted));
}
static void crc(uint8_t *record) {
    uint32_t value=kui_crc32(0,record,KUI_DEST_RECORD_SIZE-4);
    for(unsigned i=0;i<4;i++) record[KUI_DEST_RECORD_SIZE-4+i]=(uint8_t)(value>>(8*i));
}
static void rejects_record(const void *record,size_t size) {
    char out[KUI_DEST_ROOT_CAP]="unchanged";uint64_t sequence=17;
    assert(!kui_destination_decode(out,&sequence,record,size));assert(!strcmp(out,"unchanged") && sequence==17);
}
int main(void) {
    char max_name[KUI_DEST_NAME_CAP+1];memset(max_name,'A',sizeof(max_name));
    max_name[KUI_DEST_NAME_CAP-1]=0;assert(kui_destination_name_valid(max_name));
    max_name[KUI_DEST_NAME_CAP-1]='A';max_name[KUI_DEST_NAME_CAP]=0;
    assert(!kui_destination_name_valid(max_name));
    assert(kui_destination_name_valid("日本語 track.bin"));
    assert(!kui_destination_name_valid(NULL) && !kui_destination_name_valid("") &&
           !kui_destination_name_valid("../track.bin") && !kui_destination_name_valid("NUL.bin") &&
           !kui_destination_name_valid("track.bin ") && !kui_destination_name_valid("bad\xe2\x82"));
    char root[KUI_DEST_ROOT_CAP],name[KUI_DEST_NAME_CAP],job[KUI_DEST_JOB_CAP];
    kui_destination_default(root);assert(!strcmp(root,"/Games"));
    normalized("/","/");normalized("////","/");normalized("//Games///MDK2/./","/Games/MDK2");

    normalized("/Jeux/日本語/é","/Jeux/日本語/é");normalized("/.hidden","/.hidden");
    const char *invalid[]={NULL,"","Games","0:/Games","/Games/../bad","/..","/Games/..", "/SD:/Games",
        "/Bad\\Name","/Bad\"Name","/Bad*Name","/Bad?Name","/Bad<Name","/Bad>Name","/Bad|Name",
        "/bad\tname","/bad\x7f","/bad\xc2\x80","/bad.","/bad ","/CON","/Nul.txt","/A/COM1.bin",
        "/lPt9","/AUX","/PRN","/bad\xc0\xaf","/bad\xed\xa0\x80","/bad\xf4\x90\x80\x80","/bad\xe2\x82"};
    for(size_t i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++) {
        strcpy(root,"old");assert(!kui_destination_normalize(root,invalid[i]));assert(!root[0]);
    }
    normalized("/COM10/console/LPT0","/COM10/console/LPT0");
    char long_root[KUI_DEST_ROOT_CAP+1];long_root[0]='/';memset(long_root+1,'A',KUI_DEST_ROOT_CAP-2);
    long_root[KUI_DEST_ROOT_CAP-1]=0;normalized(long_root,long_root);
    long_root[KUI_DEST_ROOT_CAP-1]='A';long_root[KUI_DEST_ROOT_CAP]=0;
    assert(!kui_destination_normalize(root,long_root));
    strcpy(root,"//Games/MDK2/");assert(kui_destination_normalize(root,root));assert(!strcmp(root,"/Games/MDK2"));
    assert(kui_destination_parent(root,root) && !strcmp(root,"/Games"));
    assert(kui_destination_parent(root,root) && !strcmp(root,"/"));
    assert(kui_destination_parent(root,root) && !strcmp(root,"/"));
    assert(kui_destination_join(root,"/","Games") && !strcmp(root,"/Games"));
    assert(kui_destination_join(root,root,"日本語") && !strcmp(root,"/Games/日本語"));
    assert(!kui_destination_join(root,"/Games","../bad"));assert(!kui_destination_join(root,"/Games",".."));
    assert(!kui_destination_join(root,"/Games","0:bad"));
    title(NULL,"DreamcastDisc");title("", "DreamcastDisc");title(" \t\r\n ","DreamcastDisc");
    title("...", "DreamcastDisc");title(".. CON ","DreamcastDisc");title("lPt1.bin","DreamcastDisc");
    title(" .hidden ","hidden");title("  SWORD   OF\tTHE\nBERSERK. ","SWORD OF THE BERSERK");
    title("A/B\\C:D*E?F\"G<H>I|J","A_B_C_D_E_F_G_H_I_J");title("日本語 é", "日本語 é");
    title("bad\xff", "bad_");title("bad\001name", "bad_name");
    char long_title[160];memset(long_title,'A',94);memcpy(long_title+94,"日本語",10);
    kui_destination_title(name,long_title);assert(strlen(name)==94);
    memset(long_title,'A',150);long_title[150]=0;kui_destination_title(name,long_title);assert(strlen(name)==95);
    assert(kui_destination_folder_name(name,"MDK2",1) && !strcmp(name,"MDK2"));
    assert(kui_destination_folder_name(name,"MDK2",2) && !strcmp(name,"MDK2 (2)"));
    assert(kui_destination_folder_name(name,"MDK2",9999) && !strcmp(name,"MDK2 (9999)"));
    assert(!kui_destination_folder_name(name,"MDK2",0));assert(!kui_destination_folder_name(name,"MDK2",10000));
    assert(!kui_destination_folder_name(name,"CON",1));assert(!kui_destination_folder_name(name,"../bad",1));
    unsigned index=0;assert(kui_destination_folder_index("mdk2","MDK2",&index) && index==1);
    assert(kui_destination_folder_index("mDk2 (2)","MDK2",&index) && index==2);
    assert(kui_destination_folder_index("MDK2 (9999)","MDK2",&index) && index==9999);
    const char *bad_names[]={"MDK2 (1)","MDK2 (0)","MDK2 (02)","MDK2 (10000)","MDK2 (9999999999999999)",
        "MDK2(2)","MDK2 (2)extra","MDK2 (2x)","MDK2 ()","MDK2 ( 2)","MDK2 (-2)","XMDK2","MDK20"};
    for(size_t i=0;i<sizeof(bad_names)/sizeof(bad_names[0]);i++) {
        index=42;assert(!kui_destination_folder_index(bad_names[i],"MDK2",&index) && index==42);
    }
    assert(kui_destination_job_path(job,"//Games/","MDK2 (2)") && !strcmp(job,"0:/Games/MDK2 (2)"));
    assert(kui_destination_job_path(job,"/","MDK2") && !strcmp(job,"0:/MDK2"));
    assert(!kui_destination_job_path(job,"0:/Games","MDK2"));
    assert(!kui_destination_job_path(job,"/Games","../bad"));

    uint8_t record[KUI_DEST_RECORD_SIZE+1],copy[KUI_DEST_RECORD_SIZE];uint64_t sequence;
    assert(kui_destination_encode(record,"//Jeux/日本語/",UINT64_MAX));
    assert(kui_destination_decode(root,&sequence,record,KUI_DEST_RECORD_SIZE));
    assert(sequence==UINT64_MAX && !strcmp(root,"/Jeux/日本語"));
    assert(!kui_destination_encode(record,"/Games",0));
    assert(kui_destination_encode(record,"/Games",1));memcpy(copy,record,sizeof(copy));
    for(unsigned bit=0;bit<KUI_DEST_RECORD_SIZE*8;bit++) {
        memcpy(record,copy,sizeof(copy));record[bit/8]^=(uint8_t)(1u<<(bit%8));rejects_record(record,KUI_DEST_RECORD_SIZE);
    }
    for(size_t n=0;n<KUI_DEST_RECORD_SIZE;n++) rejects_record(copy,n);
    memcpy(record,copy,sizeof(copy));record[KUI_DEST_RECORD_SIZE]=0;rejects_record(record,sizeof(record));
    memcpy(record,copy,sizeof(copy));record[8]=2;crc(record);rejects_record(record,KUI_DEST_RECORD_SIZE);
    memcpy(record,copy,sizeof(copy));record[12]=0;crc(record);rejects_record(record,KUI_DEST_RECORD_SIZE);
    memcpy(record,copy,sizeof(copy));memset(record+16,0,8);crc(record);rejects_record(record,KUI_DEST_RECORD_SIZE);
    memcpy(record,copy,sizeof(copy));record[40]=1;crc(record);rejects_record(record,KUI_DEST_RECORD_SIZE);
    memcpy(record,copy,sizeof(copy));memset(record+24,'A',KUI_DEST_ROOT_CAP);crc(record);rejects_record(record,KUI_DEST_RECORD_SIZE);
    memcpy(record,copy,sizeof(copy));memset(record+24,0,KUI_DEST_ROOT_CAP);strcpy((char *)record+24,"/Games//foo");crc(record);rejects_record(record,KUI_DEST_RECORD_SIZE);
    memcpy(record,copy,sizeof(copy));memset(record+24,0,KUI_DEST_ROOT_CAP);strcpy((char *)record+24,"/Games/../foo");crc(record);rejects_record(record,KUI_DEST_RECORD_SIZE);
    puts("PASS destination: safe normalized paths, UTF-8, titles, ordinal names, qualified paths and versioned CRC records");
    return 0;
}
