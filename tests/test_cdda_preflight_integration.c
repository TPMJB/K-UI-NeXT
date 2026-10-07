/* SPDX-License-Identifier: GPL-3.0-only */
/* Actual profile13 adapter, independent filesystem/phase ledger and Python
 * hashlib/zlib reference digests. Host files are generated test data only. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kui/cdda_preflight.h"
#include "../src/loader/cdda_aica.h"
static enum kui_cdda_preflight_result model_preflight_read(const struct kui_game_image *,
    const struct kui_cdda_preflight_ops *,struct kui_cdda_preflight_report *);
#define CDDA_PREFLIGHT_HOST_TEST 1
#define kui_cdda_preflight_read model_preflight_read
#include "../src/loader/cdda_preflight_main.c"
#undef kui_cdda_preflight_read

enum scenario { PASS,COOKED_PASS,PREFIX_PASS,PADDING_CORRUPT,MAP_PARSE,MAP_MISSING,
    MAP_TRUNCATED,MAP_OVERLAP,DESCRIPTOR_IO,SOURCE_IO,RAW_SYNC,RAW_MODE,RAW_ADDRESS,
    IP_HEADER,ISO_ENDIAN,BOOT_MISSING,BOOT_EXTENT,PAYLOAD_CORRUPT,CANCEL_BEFORE,
    CANCEL_INSPECT,CANCEL_IP,CANCEL_BOOT,PROGRESS_INSPECT,PROGRESS_IP,PROGRESS_BOOT,CANCEL_LAST,
    GEOMETRY_INVALID,EXTENT_SIZE,RUN_GAP,RUN_ZERO,RUN_PARTITION,RUN_OVERLAP,
    SLOTS64_PASS,SLOTS65_REFUSED,SLOTS161_REFUSED,DEADLINE_INSPECT,DEADLINE_MAP,STACK_GUARD,
    CLOCK_STALLED,CLOCK_CONFIG,CLOCK_STANDBY };
static enum scenario scenario;
static const char *fixture_directory;
static unsigned stats,opens,closes,descriptor_reads,source_reads,probe_reads,blocks,shutdowns,aica_calls;
static unsigned progress_calls,cancel_calls,preflight_calls,reported_failures,finish_calls;
static bool initialized,finished,injected,inside_preflight;
static enum kui_cdda_preflight_phase current_phase;
static uint32_t current_done;
static uint64_t clock_value;
static uint32_t model_tcor=0xabcdef45u,model_tcnt=0x10203040u;
static uint16_t model_tcr=3u;
static uint8_t model_tstr=5u,original_tstr;
static unsigned timer_writes,timer_counter_reads,report_headers,report_polls,report_wraps;
static unsigned report_step,report_finish_calls;
static uint32_t report_anchor;
static bool report_ready,report_active;
/* Independent duration references; do not derive clock stimuli from the
 * implementation's threshold macros. */
#define MODEL_PAGE_TICKS 187030800u
#define MODEL_DEADLINE_TICKS 2244369600u
static struct kui_cdda_preflight_report captured;
static enum kui_cdda_preflight_result captured_result;
static uint32_t expected_ip_crc,expected_boot_crc;
static uint8_t expected_ip_sha[32],expected_boot_sha[32];
static struct kui_cdda_preflight_ops original_ops;
static struct cdda_preflight_storage_scan scan_model;
static uint32_t mapped_track,mapped_runs,mapped_cluster_sectors,mapped_clusters;
static bool (*storage_cancel)(void *);
static void *storage_cancel_context;
static struct {FILE *file;uint32_t bytes,track;bool descriptor;} primary,secondary;
static const uint32_t starts[]={0u,7257u,45000u,201900u,219712u,234091u,255599u,272017u,
    282799u,303092u,323352u,341314u,356690u,374201u,377422u};
static const char *scenario_names[]={"pass","cooked-pass","prefix-pass","padding-corrupt",
    "map-parse","map-missing","map-truncated","map-overlap","descriptor-io","source-io",
    "raw-sync","raw-mode","raw-address","ip-header","iso-endian","boot-missing","boot-extent",
    "payload-corrupt","cancel-before","cancel-inspect","cancel-ip","cancel-boot",
    "progress-inspect","progress-ip","progress-boot","cancel-last","geometry-invalid","extent-size",
    "run-gap","run-zero","run-partition","run-overlap","slots64-pass","slots65-refused",
    "slots161-refused","deadline-inspect","deadline-map","stack-guard",
    "clock-stalled","clock-config","clock-standby"};

volatile struct kui_storage_boot_marker cdda_storage_boot_marker={
    KUI_STORAGE_BOOT_MAGIC1,KUI_STORAGE_BOOT_MAGIC2,1u,KUI_STORAGE_SCI,~(uint32_t)KUI_STORAGE_SCI};

static const char *basename_of(const char *path) {
    assert(path);const char *name=strrchr(path,'/');return name?name+1:path;
}
static uint32_t track_number(const char *path) {
    const char *name=basename_of(path);unsigned number=0;char extension[8];
    assert(sscanf(name,"track%2u.%7s",&number,extension)==2 && number>=1u && number<=15u);
    assert(!strcmp(extension,(number==1u || number==3u || number==15u)?"bin":"raw"));
    return number;
}
static void host_path(const char *path,char output[1024]) {
    const char *name=basename_of(path);
    if(strstr(name,".gdi") || strstr(name,".GDI")) name="fixture.gdi";
    int n=snprintf(output,1024,"%s/%s",fixture_directory,name);assert(n>0 && n<1024);
}
static uint32_t actual_size(const char *path) {
    char filename[1024];host_path(path,filename);FILE *file=fopen(filename,"rb");assert(file);
    assert(!fseek(file,0,SEEK_END));long length=ftell(file);assert(length>0 && (uint64_t)length<=UINT32_MAX);
    assert(!fclose(file));return (uint32_t)length;
}
static void close_slot(bool data) {
    if(data) {
        if(secondary.file) {assert(!fclose(secondary.file));closes++;}
        memset(&secondary,0,sizeof(secondary));
    } else {
        if(primary.file) {assert(!fclose(primary.file));closes++;}
        memset(&primary,0,sizeof(primary));
    }
}
static int open_slot(const char *path,uint32_t *bytes,bool data) {
    assert(initialized && !finished && bytes);close_slot(data);
    char filename[1024];host_path(path,filename);FILE *file=fopen(filename,"rb");assert(file);
    uint32_t size=actual_size(path);bool descriptor=strstr(filename,".gdi")!=NULL;
    uint32_t track=descriptor?0u:track_number(path);
    if(data) {secondary.file=file;secondary.bytes=size;secondary.track=track;secondary.descriptor=descriptor;}
    else {primary.file=file;primary.bytes=size;primary.track=track;primary.descriptor=descriptor;}
    opens++;*bytes=size;return 0;
}
static void dual32(uint8_t *out,uint32_t n) {
    for(unsigned i=0;i<4u;i++) out[i]=out[7u-i]=(uint8_t)(n>>(i*8u));
}
static int read_slot(uint32_t offset,void *out,uint32_t bytes,bool data) {
    FILE *file=data?secondary.file:primary.file;
    uint32_t size=data?secondary.bytes:primary.bytes;
    uint32_t track=data?secondary.track:primary.track;
    bool descriptor=data?secondary.descriptor:primary.descriptor;
    assert(initialized && !finished && file && out && bytes && offset<=size && bytes<=size-offset);
    uint8_t *p=out;
    if(descriptor) {
        descriptor_reads++;assert(!inside_preflight && offset==0u && bytes<=32768u);
        if(scenario==DESCRIPTOR_IO) {injected=true;return -1;}
    } else {
        assert(track>=1u && track<=15u && (bytes==2352u || (track==3u && bytes==2048u)));
        uint32_t prefix=scenario==PREFIX_PASS?512u:0u;
        if(track!=3u) prefix=0u;
        assert(offset>=prefix && (offset-prefix)%bytes==0u);
        if(inside_preflight) {
            assert(track==3u);source_reads++;
            if(scenario==SOURCE_IO && source_reads==4u) {injected=true;return -1;}
        } else probe_reads++;
    }
    assert(!fseek(file,(long)offset,SEEK_SET) && fread(out,1,bytes,file)==bytes);
    blocks+=(bytes+511u)/512u;
    if(descriptor) {
        if(scenario==MAP_PARSE) {p[0]='0';injected=true;}
        return 0;
    }
    if(!inside_preflight) return 0;
    uint32_t prefix=scenario==PREFIX_PASS?512u:0u;
    uint32_t relative=(offset-prefix)/bytes;
    unsigned payload=bytes==2352u?16u:0u;
    assert(starts[track-1u]+relative>=45000u && relative<64u);
    if(!injected && current_phase==KUI_CDDA_PREFLIGHT_INSPECT && relative==16u) {
        if(scenario==RAW_SYNC) {p[1]=0;injected=true;}
        if(scenario==RAW_MODE) {p[15]=2;injected=true;}
        if(scenario==RAW_ADDRESS) {p[14]^=1u;injected=true;}
        if(scenario==ISO_ENDIAN) {p[payload+84u]^=1u;injected=true;}
    }
    if(!injected && current_phase==KUI_CDDA_PREFLIGHT_INSPECT && relative==0u && scenario==IP_HEADER) {
        p[payload]^=1u;injected=true;
    }
    if(!injected && current_phase==KUI_CDDA_PREFLIGHT_INSPECT && relative==20u) {
        if(scenario==BOOT_MISSING) {p[payload+68u+33u]='X';injected=true;}
        if(scenario==BOOT_EXTENT) {dual32(p+payload+68u+2u,45063u);injected=true;}
    }
    if(!injected && current_phase==KUI_CDDA_PREFLIGHT_HASH_BOOT) {
        if(scenario==PAYLOAD_CORRUPT && relative==21u) {p[payload+7u]^=0x40u;injected=true;}
        if(scenario==PADDING_CORRUPT && relative==23u) {p[payload+5003u%2048u]^=0x80u;injected=true;}
    }
    return 0;
}

int cdda_storage_init(void) {
    assert(!finished && timer_writes==5u && timer_counter_reads>=2u && (model_tstr&2u) &&
        preflight_clock_active && storage_cancel && !storage_cancel_context);
    initialized=true;return 0;
}
int cdda_storage_named_stat(const char *path,uint32_t *bytes) {
    assert(initialized && !finished && bytes && !inside_preflight);stats++;
    uint32_t track=track_number(path);
    if(scenario==MAP_MISSING && track==8u) {injected=true;return -1;}
    *bytes=actual_size(path);
    if(scenario==MAP_TRUNCATED && track==14u) {(*bytes)--;injected=true;}
    if(scenario==MAP_OVERLAP && track==3u) {*bytes=(201900u-45000u+1u)*2352u;injected=true;}
    return 0;
}
int cdda_storage_open(const char *path,uint32_t *bytes) {return open_slot(path,bytes,false);}
int cdda_storage_read_at(uint32_t offset,void *out,uint32_t bytes) {return read_slot(offset,out,bytes,false);}
void cdda_storage_close(void) {close_slot(false);}
int cdda_storage_data_open_path(const char *path,uint32_t *bytes) {return open_slot(path,bytes,true);}
int cdda_storage_data_read_at(uint32_t offset,uint8_t *out,uint32_t bytes) {return read_slot(offset,out,bytes,true);}
int cdda_storage_data_open(uint32_t *bytes) {(void)bytes;assert(false);return -1;}
void cdda_storage_data_close(void) {close_slot(true);}
void cdda_storage_shutdown(void) {
    close_slot(false);close_slot(true);initialized=false;shutdowns++;
    /* A change to another channel's start bit must survive channel1 cleanup. */
    if(preflight_clock_owned) model_tstr^=1u;
}
uint32_t cdda_storage_blocks_read(void) {return blocks;}
const char *cdda_storage_last_failure(void) {return "independently injected filesystem failure";}
uint32_t cdda_preflight_host_clock_read(uint32_t address,unsigned width) {
    if(address==PREFLIGHT_STBCR) {
        assert(width==1u);
        if(scenario==CLOCK_STANDBY) {injected=true;return 4u;}
        return 0u;
    }
    assert(scenario!=CLOCK_STANDBY);
    if(address==PREFLIGHT_TSTR) {assert(width==1u);return model_tstr;}
    if(address==PREFLIGHT_TCR1) {assert(width==2u);return model_tcr;}
    if(address==PREFLIGHT_TCOR1) {assert(width==4u);return model_tcor;}
    assert(address==PREFLIGHT_TCNT1 && width==4u);
    if(timer_writes!=5u || !(model_tstr&2u)) return model_tcnt;
    timer_counter_reads++;
    if(scenario==CLOCK_STALLED) {
        injected=true;assert(timer_counter_reads<=1025u);return model_tcnt;
    }
    if(report_ready) {
        assert(preflight_stages==6u && !preflight_failures && !primary.file && !secondary.file &&
            !storage_cancel && !storage_cancel_context);
        report_ready=false;report_active=true;
        clock_value=UINT32_MAX-MODEL_PAGE_TICKS/2u;
    } else if(report_active) {
        assert(finished && finish_calls==report_finish_calls && report_step<3u && ++report_polls<=36u);
        uint32_t before=(uint32_t)clock_value;
        uint32_t delta=report_step==0u?0u:report_step==1u?MODEL_PAGE_TICKS-1u:MODEL_PAGE_TICKS;
        clock_value=(uint32_t)(report_anchor+delta);
        if((uint32_t)clock_value<before) report_wraps++;
        report_step++;
    } else if(timer_counter_reads==3u) {
        /* Start the read deadline near a numerical wrap, after clock proof. */
        clock_value=UINT32_MAX-100000u;
    } else {
        if(!injected && ((scenario==DEADLINE_INSPECT && inside_preflight && source_reads>=1u) ||
           (scenario==DEADLINE_MAP && !inside_preflight && mapped_track))) {
            injected=true;clock_value=(uint64_t)preflight_start+MODEL_DEADLINE_TICKS;
        } else clock_value+=1000u;
    }
    model_tcnt=UINT32_MAX-(uint32_t)clock_value;return model_tcnt;
}
void cdda_preflight_host_clock_write(uint32_t address,uint32_t value,unsigned width) {
    assert(scenario!=CLOCK_STANDBY && ++timer_writes<=10u);
    unsigned step=timer_writes;
    if(step==1u || step==5u || step==6u || step==10u) {
        assert(address==PREFLIGHT_TSTR && width==1u && !(value&~7u));
        uint8_t other=(uint8_t)(original_tstr&~2u);
        if(step>=6u) {assert(shutdowns==1u && finished);other^=1u;}
        assert((value&~2u)==other);
        assert((value&2u)==(step==5u?2u:step==10u?(original_tstr&2u):0u));
        model_tstr=(uint8_t)value;
    } else if(step==2u || step==9u) {
        assert(address==PREFLIGHT_TCR1 && width==2u && value==(step==2u?0u:3u));
        model_tcr=(uint16_t)value;
        if(step==2u && scenario==CLOCK_CONFIG) {model_tcr=1u;injected=true;}
    } else if(step==3u || step==7u) {
        assert(address==PREFLIGHT_TCOR1 && width==4u && value==(step==3u?UINT32_MAX:0xabcdef45u));
        model_tcor=value;
    } else {
        assert((step==4u || step==8u) && address==PREFLIGHT_TCNT1 && width==4u &&
            value==(step==4u?UINT32_MAX:0x10203040u));
        model_tcnt=value;clock_value=UINT32_MAX-value;
    }
}
bool cdda_preflight_host_report_done(void) {
    assert(report_active && finish_calls && finish_calls<=13u && timer_writes==5u &&
        (model_tstr&2u) && initialized && !shutdowns);
    if(finish_calls<13u) return false;
    assert(report_headers==13u && report_polls==36u && report_wraps>=1u);
    return true;
}
bool cdda_preflight_host_stack(uint32_t *used) {
    assert(used);*used=1000u;
    if(scenario==STACK_GUARD) {injected=true;return false;}
    return true;
}
void cdda_display_init(void) {}
void cdda_display_line(const char *text) {
    assert(text);unsigned page;
    if(sscanf(text,"PROFILE13 PREFLIGHT / page %u of 6",&page)==1) {
        assert(report_active && page==finish_calls%6u+1u);report_headers++;
    }
}
void cdda_display_number(const char *text,uint32_t value) {(void)value;assert(text);}
void cdda_display_finish(unsigned count) {
    if(report_active) {
        assert(!count && preflight_stages==6u && (finish_calls?report_step==3u:report_step==0u));
        report_anchor=(uint32_t)clock_value;report_step=0u;report_finish_calls=finish_calls+1u;
    }
    finished=true;reported_failures=count;finish_calls++;
}
enum kui_cdda_aica_result kui_cdda_aica_init(void) {aica_calls++;assert(false);return KUI_CDDA_AICA_ARGUMENT;}
enum kui_cdda_aica_result kui_cdda_aica_start(void) {aica_calls++;assert(false);return KUI_CDDA_AICA_ARGUMENT;}
enum kui_cdda_aica_result kui_cdda_aica_stop(void) {aica_calls++;assert(false);return KUI_CDDA_AICA_ARGUMENT;}
enum kui_cdda_aica_result kui_cdda_aica_position(uint32_t *out) {(void)out;aica_calls++;assert(false);return KUI_CDDA_AICA_ARGUMENT;}
enum kui_cdda_aica_result kui_cdda_aica_write_samples(unsigned h,unsigned o,const int16_t *l,
    const int16_t *r,unsigned n) {(void)h;(void)o;(void)l;(void)r;(void)n;aica_calls++;assert(false);return KUI_CDDA_AICA_ARGUMENT;}

int cdda_storage_geometry(uint64_t *card,uint32_t *start,uint32_t *count) {
    assert(initialized && card && start && count);*card=2097152u;*start=2048u;*count=1048576u;
    if(scenario==GEOMETRY_INVALID) {*card=(uint64_t)*start+*count-1u;injected=true;}
    return 0;
}
void cdda_storage_read_cancel(bool (*callback)(void *),void *context) {
    storage_cancel=callback;storage_cancel_context=context;
}
void cdda_preflight_storage_cancel(bool (*callback)(void *),void *context) {
    storage_cancel=callback;storage_cancel_context=context;
}
int cdda_preflight_storage_discover(void) {
    assert(initialized);scan_model=(struct cdda_preflight_storage_scan){.directories=3u,.entries=19u,
        .descriptors=1u,.candidates=1u};return 0;
}
int cdda_preflight_storage_descriptor(void *out,size_t capacity,size_t *bytes) {
    assert(out && bytes);uint32_t length;
    if(open_slot("fixture.gdi",&length,false)) return -1;
    assert(length<=capacity);int result=read_slot(0,out,length,false);close_slot(false);
    if(!result) *bytes=length;
    return result;
}
const char *cdda_preflight_storage_path(void) {return "0:/Games/Toy/TOY_COMMANDER.gdi";}
const struct cdda_preflight_storage_scan *cdda_preflight_storage_scan(void) {return &scan_model;}
const char *cdda_preflight_storage_failure(void) {return "independently injected preflight adapter failure";}
static enum kui_game_result mapped_stat(void *context,const char *name,uint64_t *bytes) {
    (void)context;uint32_t length;
    if(cdda_storage_named_stat(name,&length)) return KUI_GAME_IO;
    *bytes=length;return KUI_GAME_OK;
}
static enum kui_game_result mapped_read(void *context,const char *name,uint64_t offset,void *out,size_t bytes) {
    (void)context;assert(offset<=UINT32_MAX && bytes<=UINT32_MAX);
    if(storage_cancel && storage_cancel(storage_cancel_context)) return KUI_GAME_CANCELLED;
    uint32_t length;uint32_t track=track_number(name);
    if(!secondary.file || secondary.track!=track) {
        if(open_slot(name,&length,true)) return KUI_GAME_IO;
    }
    return read_slot((uint32_t)offset,out,(uint32_t)bytes,true)?KUI_GAME_IO:KUI_GAME_OK;
}
static const struct kui_game_file_ops mapped_files={NULL,mapped_stat,mapped_read};
const struct kui_game_file_ops *cdda_preflight_storage_files(void) {return &mapped_files;}
int cdda_preflight_storage_extents(const char *name,struct cdda_preflight_storage_extents *out) {
    assert(initialized && out);uint32_t number=track_number(name),bytes=actual_size(name);
    mapped_track=number;mapped_runs=1u;mapped_cluster_sectors=8u;
    if(number==3u) {
        if(scenario==SLOTS64_PASS) {mapped_runs=35u;injected=true;}
        if(scenario==SLOTS65_REFUSED) {mapped_runs=36u;injected=true;}
        if(scenario==SLOTS161_REFUSED) {mapped_runs=132u;mapped_cluster_sectors=1u;injected=true;}
    }
    uint32_t cluster_bytes=mapped_cluster_sectors*512u;
    mapped_clusters=(bytes+cluster_bytes-1u)/cluster_bytes;
    *out=(struct cdda_preflight_storage_extents){.file_bytes=bytes,.cluster_bytes=cluster_bytes,.clusters=mapped_clusters,
        .extents=mapped_runs,.map_words=2u*mapped_runs+2u,.first_volume_lba=4096u+(number-1u)*4096u,
        .rounded_bytes=(uint64_t)mapped_clusters*cluster_bytes};
    if(scenario==EXTENT_SIZE && number==1u) {out->file_bytes++;injected=true;}
    for(unsigned i=0;i<32u;i++) out->sha256[i]=(uint8_t)(number*7u+i);
    return 0;
}
int cdda_preflight_storage_extent_run(unsigned index,struct cdda_preflight_storage_extent_run *out) {
    assert(initialized && out && mapped_track && index<mapped_runs);
    uint32_t clusters=index+1u<mapped_runs?1u:mapped_clusters-(mapped_runs-1u);
    uint32_t volume=4096u+(mapped_track-1u)*4096u+index*2u*mapped_cluster_sectors;
    *out=(struct cdda_preflight_storage_extent_run){.first_cluster=2u+(volume-4096u)/mapped_cluster_sectors,
        .clusters=clusters,.first_volume_lba=volume,
        .file_sector=index*mapped_cluster_sectors,.sectors=clusters*mapped_cluster_sectors};
    if(mapped_track==1u && scenario==RUN_GAP) {out->file_sector=1u;injected=true;}
    if(mapped_track==1u && scenario==RUN_ZERO) {out->sectors=0u;injected=true;}
    if(mapped_track==1u && scenario==RUN_PARTITION) {out->first_volume_lba=1048572u;injected=true;}
    if(mapped_track==2u && scenario==RUN_OVERLAP) {out->first_volume_lba=4096u;injected=true;}
    return 0;
}
void cdda_preflight_storage_close(void) {close_slot(false);close_slot(true);mapped_track=0;report_ready=true;}

static bool model_cancelled(void *context) {
    assert(context==&original_ops && inside_preflight);cancel_calls++;
    if(original_ops.cancelled && original_ops.cancelled(original_ops.context)) return true;
    bool cancel=scenario==CANCEL_BEFORE ||
        (scenario==CANCEL_INSPECT && current_phase==KUI_CDDA_PREFLIGHT_INSPECT && current_done>=1u) ||
        (scenario==CANCEL_IP && current_phase==KUI_CDDA_PREFLIGHT_HASH_IP && current_done>=4096u) ||
        (scenario==CANCEL_BOOT && current_phase==KUI_CDDA_PREFLIGHT_HASH_BOOT && current_done>=2048u) ||
        (scenario==CANCEL_LAST && current_phase==KUI_CDDA_PREFLIGHT_HASH_BOOT && current_done==5003u);
    if(cancel) injected=true;
    return cancel;
}
static bool model_progress(void *context,enum kui_cdda_preflight_phase phase,uint32_t done,uint32_t total) {
    assert(context==&original_ops && inside_preflight && phase<=KUI_CDDA_PREFLIGHT_HASH_BOOT);
    assert(total==(phase==KUI_CDDA_PREFLIGHT_INSPECT?145u:phase==KUI_CDDA_PREFLIGHT_HASH_IP?32768u:5003u));
    if(progress_calls && phase==current_phase) assert(done>=current_done);
    if(phase!=current_phase) assert(phase==current_phase+1 && done==0u);
    assert(done<=total);current_phase=phase;current_done=done;progress_calls++;
    unsigned before=source_reads;
    if(original_ops.progress && !original_ops.progress(original_ops.context,phase,done,total)) return false;
    assert(source_reads==before); /* Display/progress callbacks perform no physical reads. */
    bool refuse=(scenario==PROGRESS_INSPECT && phase==KUI_CDDA_PREFLIGHT_INSPECT && done==1u) ||
        (scenario==PROGRESS_IP && phase==KUI_CDDA_PREFLIGHT_HASH_IP && done==4096u) ||
        (scenario==PROGRESS_BOOT && phase==KUI_CDDA_PREFLIGHT_HASH_BOOT && done==2048u);
    if(refuse) injected=true;
    return !refuse;
}
static enum kui_cdda_preflight_result model_preflight_read(const struct kui_game_image *image,
    const struct kui_cdda_preflight_ops *ops,struct kui_cdda_preflight_report *out) {
    assert(!inside_preflight && initialized && image && out);preflight_calls++;
    struct kui_game_image before=*image;struct kui_cdda_preflight_report before_out=*out;
    original_ops=ops?*ops:(struct kui_cdda_preflight_ops){0};
    const struct kui_cdda_preflight_ops wrapped={&original_ops,model_cancelled,model_progress};
    current_phase=KUI_CDDA_PREFLIGHT_INSPECT;current_done=0;inside_preflight=true;
    enum kui_cdda_preflight_result result=kui_cdda_preflight_read(image,&wrapped,out);
    inside_preflight=false;assert(!memcmp(image,&before,sizeof(before)));
    if(result!=KUI_CDDA_PREFLIGHT_OK) assert(!memcmp(out,&before_out,sizeof(before_out)));
    else captured=*out;
    captured_result=result;return result;
}
static void digest_bytes(const char *text,uint8_t output[32]) {
    assert(strlen(text)==64u);
    for(unsigned i=0;i<32u;i++) {unsigned n;assert(sscanf(text+2u*i,"%2x",&n)==1);output[i]=(uint8_t)n;}
}
int main(int argc,char **argv) {
    assert(argc==7);fixture_directory=argv[2];bool found=false;
    for(unsigned i=0;i<sizeof(scenario_names)/sizeof(scenario_names[0]);i++)
        if(!strcmp(argv[1],scenario_names[i])) {scenario=(enum scenario)i;found=true;break;}
    assert(found);expected_ip_crc=(uint32_t)strtoul(argv[3],NULL,16);digest_bytes(argv[4],expected_ip_sha);
    expected_boot_crc=(uint32_t)strtoul(argv[5],NULL,16);digest_bytes(argv[6],expected_boot_sha);
    /* Both inherited stopped and running channel1 are covered. */
    if(scenario==STACK_GUARD) model_tstr=7u;
    original_tstr=model_tstr;cdda_main();
    assert(finished && !primary.file && !secondary.file && opens==closes &&
        !aica_calls && !inside_preflight && !storage_cancel && !storage_cancel_context);
    bool succeeds=scenario==PASS || scenario==COOKED_PASS || scenario==PREFIX_PASS ||
        scenario==PADDING_CORRUPT || scenario==PAYLOAD_CORRUPT || scenario==SLOTS64_PASS;
    if(succeeds) {
        assert(!preflight_failures && !reported_failures && preflight_stages==6u && initialized &&
            !shutdowns && finish_calls==13u && preflight_calls==1u && captured_result==KUI_CDDA_PREFLIGHT_OK &&
            timer_writes==5u && model_tstr==(uint8_t)(original_tstr|2u) && preflight_clock_owned &&
            report_headers==13u && report_polls==36u && report_wraps>=1u);
        assert(stats==15u && descriptor_reads==1u && source_reads==22u && progress_calls==25u);
        assert(captured.map.count==15u && captured.map.complete && !captured.map.cd_image &&
            captured.metadata.ip_valid && captured.metadata.boot_valid && captured.metadata.native_gd &&
            !captured.metadata.windows_ce && !captured.scrambled && captured.metadata.session_lba==45000u &&
            captured.ip_lba==45000u && captured.ip_bytes==32768u && captured.metadata.boot_lba==45021u &&
            captured.metadata.boot_bytes==5003u && captured.metadata_reads==3u && captured.hash_reads==19u);
        assert(!strcmp(captured.metadata.bootfile,"1ST_READ.BIN") &&
            !strcmp(captured.metadata.product,"T-TEST3") && !strcmp(captured.metadata.version,"V1.000") &&
            !strcmp(captured.metadata.region,"JUE"));
        assert(captured.ip_crc32==expected_ip_crc && !memcmp(captured.ip_sha256,expected_ip_sha,32));
        if(scenario==PAYLOAD_CORRUPT) {
            assert(injected && captured.boot_crc32!=expected_boot_crc && memcmp(captured.boot_sha256,expected_boot_sha,32));
        } else assert(captured.boot_crc32==expected_boot_crc && !memcmp(captured.boot_sha256,expected_boot_sha,32));
        for(unsigned i=0;i<15u;i++) assert(captured.map.tracks[i].number==i+1u &&
            captured.map.tracks[i].start_fad==starts[i]+150u);
        if(scenario==PADDING_CORRUPT) assert(injected);
        assert(kui_retail_manifest_validate(&preflight_manifest)==KUI_GAME_OK &&
            preflight_manifest.track_count==15u && preflight_manifest.extent_count==(scenario==SLOTS64_PASS?49u:15u));
        for(unsigned i=0;i<15u;i++) {
            const struct kui_retail_track *t=&preflight_manifest.slots[i].track;
            assert(t->extent_count && t->start_lba==starts[i] && t->end_lba>t->start_lba);
            unsigned first=kui_retail_track_first_extent(t);uint32_t covered=0;
            for(unsigned j=0;j<t->extent_count;j++) {
                const struct kui_retail_extent *e=&preflight_manifest.slots[first+j].extent;
                assert(e->file_block==covered && e->blocks && e->card_lba>=2048u);covered+=e->blocks;
            }
            assert(covered==(captured.map.tracks[i].file_bytes+511u)/512u);
        }
    } else {
        assert(injected && preflight_failures==1u && reported_failures==1u &&
            !initialized && shutdowns==1u && finish_calls==1u);
        assert(!preflight_clock_owned && !preflight_clock_active && !report_active &&
            model_tcor==0xabcdef45u && model_tcnt==0x10203040u && model_tcr==3u);
        if(scenario==CLOCK_STANDBY) assert(!timer_writes && !timer_counter_reads && model_tstr==original_tstr);
        else assert(timer_writes==10u && model_tstr==(uint8_t)(original_tstr^1u));
        if(scenario==CLOCK_STALLED || scenario==CLOCK_CONFIG || scenario==CLOCK_STANDBY) {
            assert(!preflight_stages && !preflight_calls && !stats && !descriptor_reads && !source_reads &&
                !opens && !closes && strstr(preflight_failure,"TMU1 clock"));
            if(scenario==CLOCK_STALLED) assert(timer_counter_reads==1025u);
        } else if(scenario==MAP_PARSE || scenario==MAP_MISSING || scenario==MAP_TRUNCATED ||
           scenario==MAP_OVERLAP || scenario==DESCRIPTOR_IO) assert(!preflight_calls && !source_reads);
        else if(scenario>=GEOMETRY_INVALID && scenario!=DEADLINE_INSPECT) {
            assert(preflight_calls==1u && captured_result==KUI_CDDA_PREFLIGHT_OK && source_reads==22u);
            if(scenario==SLOTS65_REFUSED) assert(preflight_stages==4u && preflight_manifest.extent_count==50u);
            if(scenario==SLOTS161_REFUSED) assert(preflight_stages==3u && preflight_manifest.extent_count<=145u);
            if(scenario==STACK_GUARD) assert(preflight_stages==5u);
            if(scenario==DEADLINE_MAP) assert(preflight_timed_out);
        } else {
            assert(preflight_calls==1u && captured_result!=KUI_CDDA_PREFLIGHT_OK && source_reads<=22u);
            if((scenario>=CANCEL_BEFORE && scenario<=CANCEL_LAST) || scenario==DEADLINE_INSPECT)
                assert(captured_result==KUI_CDDA_PREFLIGHT_CANCELLED);
            if(scenario==RAW_SYNC || scenario==RAW_MODE || scenario==RAW_ADDRESS)
                assert(captured_result==KUI_CDDA_PREFLIGHT_HEADER);
        }
    }
    printf("preflight13 %s: stages%u failures%u stats%u descriptor%u physical_sectors%u "
        "progress%u cancellation_checks%u AICA%u pages%u polls%u wraps%u\n",argv[1],preflight_stages,preflight_failures,stats,descriptor_reads,
        source_reads,progress_calls,cancel_calls,aica_calls,report_headers,report_polls,report_wraps);
    return 0;
}
