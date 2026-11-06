/* SPDX-License-Identifier: GPL-3.0-only */
/* Actual launch-preparation code, with explicit read-only FatFs fixtures. */
#include "../src/apps/games_retail.c"
#include <assert.h>
#include <stdarg.h>

static FATFS helper_fs,other_fs;
static struct kui_volume helper_volume;
static uint8_t payload[512],file_data[KUI_VC2_LATE_FILE_BYTES];
static struct kui_runtime_image module;
static DWORD fixture_table[16];
static size_t table_words;
static unsigned opens,closes,reads,seeks,logs,read_failure,short_read,cancel_reads;
static bool wrong_fs,open_failure,close_failure,cancel_now,seek_failure;
static uint64_t file_bytes;
static const size_t seed_at=128u;

FRESULT f_open(FIL *file,const TCHAR *name,BYTE mode) {
    assert(!strcmp(name,KUI_VC2_LATE_FILE_PATH) && mode==FA_READ);
    if(open_failure)return FR_NO_FILE;
    memset(file,0,sizeof(*file));file->obj.fs=wrong_fs?&other_fs:&helper_fs;
    file->obj.objsize=file_bytes;++opens;return FR_OK;
}
FRESULT f_lseek(FIL *file,FSIZE_t offset) {
    ++seeks;
    if(seek_failure)return FR_DISK_ERR;
    if(offset==CREATE_LINKMAP) {
        assert(file->cltbl);
        if(file->cltbl[0]<table_words) {file->cltbl[0]=(DWORD)table_words;return FR_NOT_ENOUGH_CORE;}
        memcpy(file->cltbl,fixture_table,table_words*sizeof(*fixture_table));
        return FR_OK;
    }
    assert(offset==0u);file->fptr=offset;return FR_OK;
}
FRESULT f_read(FIL *file,void *buffer,UINT want,UINT *got) {
    ++reads;*got=0u;
    if(read_failure==reads)return FR_DISK_ERR;
    assert(file->fptr<=sizeof(file_data) && want<=sizeof(file_data)-file->fptr);
    *got=short_read==reads?want-1u:want;
    memcpy(buffer,file_data+file->fptr,*got);file->fptr+=*got;return FR_OK;
}
FRESULT f_close(FIL *file) {
    assert(!file->cltbl);++closes;return close_failure?FR_DISK_ERR:FR_OK;
}
static bool cancelled(void) {return cancel_now || (cancel_reads && reads>=cancel_reads);}
static void logger(const char *format,...) {(void)format;++logs;}
static struct kui_vc2_vmu_late_seed *seed(void) {return (void *)(payload+seed_at);}
static void fixture(void) {
    memset(&helper_fs,0,sizeof(helper_fs));helper_fs.database=32u;
    helper_fs.csize=1u;helper_fs.n_fatent=200000u;
    helper_volume=(struct kui_volume){.start=2048u,.count=400000u};
    for(unsigned i=0u;i<sizeof(file_data);++i)file_data[i]=(uint8_t)((i*37u+17u)^(i>>6));
    memset(payload,0xa5,sizeof(payload));
    module=(struct kui_runtime_image){.data=payload,.info={.payload_bytes=sizeof(payload)}};
    *seed()=(struct kui_vc2_vmu_late_seed){
        .magic0=KUI_VC2_LATE_SEED_MAGIC0,.magic1=KUI_VC2_LATE_SEED_MAGIC1,
        .version=KUI_VC2_LATE_SEED_VERSION,.bytes=KUI_VC2_LATE_SEED_BYTES,
        .helper_bytes=KUI_VC2_LATE_FILE_BYTES,
        .helper_crc32=kui_retail_crc32(0u,file_data,sizeof(file_data))};
    memset(fixture_table,0,sizeof(fixture_table));
    table_words=4u;fixture_table[0]=4u;fixture_table[1]=96u;fixture_table[2]=100u;
    opens=closes=reads=seeks=logs=read_failure=short_read=cancel_reads=0u;
    wrong_fs=open_failure=close_failure=cancel_now=seek_failure=false;
    file_bytes=sizeof(file_data);
}
static bool prepare(void) {
    const char *problem=NULL;
    bool ok=vc2_ce_vmu_late_prepare(&helper_fs,&helper_volume,&module,&problem,logger,cancelled);
    assert(problem && opens==closes);
    return ok;
}
static void refused(void) {
    uint8_t before[sizeof(payload)];memcpy(before,payload,sizeof(before));
    assert(!prepare() && !memcmp(before,payload,sizeof(before)) && !logs);
}
static void accepted(uint32_t first,uint32_t count,uint32_t second) {
    uint8_t before[sizeof(payload)];memcpy(before,payload,sizeof(before));
    assert(prepare() && opens==1u && closes==1u && reads==12u && logs==1u);
    assert(seed()->first_lba==first && seed()->first_blocks==count && seed()->second_lba==second);
    uint32_t crc=seed()->seed_crc32;seed()->seed_crc32=0u;
    assert(kui_retail_crc32(0u,seed(),sizeof(*seed()))==crc);seed()->seed_crc32=crc;
    assert(!memcmp(before,payload,seed_at));
    assert(!memcmp(before+seed_at+sizeof(*seed()),payload+seed_at+sizeof(*seed()),
                   sizeof(payload)-seed_at-sizeof(*seed())));
}
int main(void) {
    fixture();accepted(2178u,96u,0u);
    fixture();table_words=6u;fixture_table[0]=6u;
    fixture_table[1]=48u;fixture_table[2]=100u;fixture_table[3]=48u;fixture_table[4]=300u;
    accepted(2178u,48u,2378u);
    fixture();table_words=6u;fixture_table[0]=6u;
    fixture_table[1]=48u;fixture_table[2]=300u;fixture_table[3]=48u;fixture_table[4]=100u;
    accepted(2378u,48u,2178u);
    fixture();table_words=6u;fixture_table[0]=6u;
    fixture_table[1]=48u;fixture_table[2]=100u;fixture_table[3]=48u;fixture_table[4]=148u;
    accepted(2178u,96u,0u);
    fixture();helper_fs.csize=64u;helper_fs.n_fatent=6000u;
    table_words=6u;fixture_table[0]=6u;
    fixture_table[1]=1u;fixture_table[2]=100u;fixture_table[3]=1u;fixture_table[4]=300u;
    accepted(8352u,64u,21152u);
    fixture();table_words=8u;fixture_table[0]=8u;
    fixture_table[1]=32u;fixture_table[2]=100u;fixture_table[3]=32u;fixture_table[4]=300u;
    fixture_table[5]=32u;fixture_table[6]=500u;refused();
    fixture();table_words=8u;fixture_table[0]=8u;
    fixture_table[1]=32u;fixture_table[2]=150u;fixture_table[3]=32u;fixture_table[4]=100u;
    fixture_table[5]=32u;fixture_table[6]=132u;refused(); /* Merged second run overlaps first. */
    fixture();table_words=6u;fixture_table[0]=6u;
    fixture_table[1]=64u;fixture_table[2]=100u;fixture_table[3]=32u;fixture_table[4]=110u;refused();
    fixture();fixture_table[1]=95u;refused();
    fixture();fixture_table[2]=1u;refused();
    fixture();fixture_table[2]=helper_fs.n_fatent;refused();
    fixture();fixture_table[1]=helper_fs.n_fatent;refused();
    fixture();fixture_table[3]=1u;refused();
    fixture();helper_fs.database=helper_volume.count;refused();
    fixture();helper_fs.csize=3u;refused();
    fixture();helper_volume.count=1000u;refused();
    fixture();helper_volume.start=UINT32_MAX-100u;refused();
    fixture();wrong_fs=true;refused();assert(!seeks && !reads);
    fixture();file_bytes--;refused();
    fixture();open_failure=true;refused();assert(!opens);
    fixture();seek_failure=true;refused();
    fixture();read_failure=2u;refused();
    fixture();short_read=3u;refused();
    fixture();close_failure=true;refused();
    fixture();cancel_now=true;refused();assert(!opens);
    fixture();cancel_reads=2u;refused();
    fixture();seed()->helper_crc32^=1u;refused();
    fixture();file_data[sizeof(file_data)-1u]^=1u;refused();
    fixture();seed()->helper_bytes--;refused();assert(!opens);
    fixture();seed()->first_lba=1u;refused();assert(!opens);
    fixture();seed()->reserved=1u;refused();
    fixture();seed()->seed_crc32=1u;refused();
    fixture();memcpy(payload+256u,seed(),sizeof(*seed()));refused();assert(!opens);
    fixture();memmove(payload+seed_at+1u,seed(),sizeof(*seed()));payload[seed_at]=0u;
    refused();assert(!opens);
    puts("PASS actual late-helper UI preparation: one/two/adjacent runs, cluster-tail clamp, full file CRC, seed CRC, same-volume/bounds/fragment refusals, cancellation and close failures; only seed bytes change");
    return 0;
}
