/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/disc_launcher.h"
#include "kui/disc_boot.h"
#include "kui/disc_launch.h"
#include "kui/hash.h"
#include "../loader/stage_fast_crc32.h"
#include "platform.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct optical_source {
    kui_log_fn log;
    kui_cancel_fn cancel;
    kui_app_progress_fn progress;
    struct kui_app_status *status;
    uint32_t reads;
    bool io_failed;
    bool single_reads,cooked_reads;
    uint32_t raw_retries,cooked_sectors;
    const char *read_problem;
    uint8_t *raw;
    uint32_t last_fad,last_count;
    uint32_t leadout_tail,leadout_end;
    const char *phase;
    uint8_t *toc_digest; /* Optional release-only witness; baseline is unchanged. */
    struct kui_disc_boot_data_range data_ranges[KUI_DISC_BOOT_MAX_DATA_RANGES];
};
static void hash_word(struct kui_sha256 *hash,uint32_t value) {
    uint8_t bytes[4];
    for(unsigned i=0;i<4u;i++)bytes[i]=(uint8_t)(value>>(8u*i));
    kui_sha256_update(hash,bytes,sizeof(bytes));
}
static void publish(struct optical_source *s) {
    if(s->progress) s->progress(s->status);
}
static bool stopped(void *context) {
    struct optical_source *s=context;
    return s->cancel && s->cancel();
}
enum raw_sector_issue { RAW_SECTOR_OK,RAW_SECTOR_FORMAT,RAW_SECTOR_ADDRESS,RAW_SECTOR_EDC };
static enum raw_sector_issue raw_sector_issue(const uint8_t *raw,uint32_t fad) {
    if(kui_data_offset(raw)!=16 || raw[15]!=1u)return RAW_SECTOR_FORMAT;
    const unsigned fields[3]={fad/4500u,(fad/75u)%60u,fad%75u};
    if(fad<150u || fad>719999u)return RAW_SECTOR_ADDRESS;
    for(unsigned i=0u;i<3u;i++)
        if(raw[12u+i]!=(uint8_t)((fields[i]/10u)*16u+fields[i]%10u))return RAW_SECTOR_ADDRESS;
    return kui_sector_edc_valid(raw)?RAW_SECTOR_OK:RAW_SECTOR_EDC;
}
static const char *issue_text(enum raw_sector_issue issue) {
    switch(issue) {
    case RAW_SECTOR_FORMAT:return "raw sector format";
    case RAW_SECTOR_ADDRESS:return "raw sector address mismatch";
    case RAW_SECTOR_EDC:return "raw sector EDC mismatch";
    default:return "raw transfer incomplete or unstable";
    }
}
static enum kui_disc_boot_io_result terminal_read(struct optical_source *s,
    uint32_t fad,const char *problem) {
    s->last_fad=fad;s->last_count=1u;s->read_problem=problem;s->io_failed=true;
    s->log("Physical disc: %s failed at FAD %" PRIu32 ": %s",s->phase,fad,problem);
    return KUI_DISC_BOOT_IO_ERROR;
}
static enum kui_disc_boot_io_result read_cooked_one(struct optical_source *s,
    uint32_t fad,uint8_t raw[KUI_RAW_BYTES]) {
    for(unsigned attempt=0u;attempt<2u;attempt++) {
        if(stopped(s))return KUI_DISC_BOOT_IO_CANCELLED;
        enum kui_disc_boot_read_result result=kui_disc_read_boot_cooked(NULL,fad,raw+16u);
        if(result==KUI_BOOT_READ_CANCELLED)return KUI_DISC_BOOT_IO_CANCELLED;
        if(result==KUI_BOOT_READ_OK) {
            if(stopped(s))return KUI_DISC_BOOT_IO_CANCELLED;
            ++s->cooked_sectors;return KUI_DISC_BOOT_IO_OK;
        }
        if(result!=KUI_BOOT_READ_RETRY)
            return terminal_read(s,fad,"cooked read fatal; see drive log / reboot if required");
        s->log("Physical disc: cooked repeat attempt %u failed at FAD %" PRIu32,attempt+1u,fad);
    }
    return terminal_read(s,fad,"2048-byte repeated read retries exhausted");
}
static enum kui_disc_boot_io_result recover_one(struct optical_source *s,uint32_t fad,
    uint8_t raw[KUI_RAW_BYTES],bool already_tried,enum raw_sector_issue previous_issue) {
    bool address_refused=previous_issue==RAW_SECTOR_ADDRESS;
    enum raw_sector_issue issue=previous_issue;
    for(unsigned attempt=already_tried?1u:0u;attempt<3u;attempt++) {
        if(stopped(s))return KUI_DISC_BOOT_IO_CANCELLED;
        ++s->raw_retries;
        enum kui_disc_boot_read_result result=kui_disc_read_boot_raw(NULL,fad,1u,raw);
        if(result==KUI_BOOT_READ_CANCELLED)return KUI_DISC_BOOT_IO_CANCELLED;
        if(result!=KUI_BOOT_READ_OK && result!=KUI_BOOT_READ_RETRY)
            return terminal_read(s,fad,"raw read fatal; see drive log / reboot if required");
        if(result==KUI_BOOT_READ_OK) {
            if(stopped(s))return KUI_DISC_BOOT_IO_CANCELLED;
            issue=raw_sector_issue(raw,fad);
            if(issue==RAW_SECTOR_OK)return KUI_DISC_BOOT_IO_OK;
            if(issue==RAW_SECTOR_ADDRESS)address_refused=true;
        } else issue=RAW_SECTOR_OK;
        s->log("Physical disc: raw single attempt %u at FAD %" PRIu32 ": %s",
            attempt+1u,fad,issue_text(issue));
    }
    /* An explicit wrong sector address cannot become trustworthy by dropping
     * its header. Only another correctly addressed raw sector can recover it. */
    if(address_refused)return terminal_read(s,fad,"raw sector address mismatch after retries");
    s->log("Physical disc: trying firmware Mode1 2048-byte repeated reads at FAD %" PRIu32
        "; raw acquisition did not verify; external raw EDC unavailable on this path",fad);
    enum kui_disc_boot_io_result result=read_cooked_one(s,fad,raw);
    if(result==KUI_DISC_BOOT_IO_OK) {
        s->cooked_reads=true;
        s->log("Physical disc: using repeated cooked sectors for this preparation; metadata and staged hashes remain checked");
    }
    return result;
}
static enum kui_disc_boot_io_result read_run(void *context,uint32_t fad,uint32_t count,uint8_t *data) {
    struct optical_source *s=context;
    if(!data || !count || count>KUI_DISC_BOOT_RUN_SECTORS) return KUI_DISC_BOOT_IO_ERROR;
    if(stopped(s)) return KUI_DISC_BOOT_IO_CANCELLED;
    s->last_fad=fad;s->last_count=count;
    if(!s->raw) s->raw=malloc(KUI_DISC_BOOT_RUN_SECTORS*KUI_RAW_BYTES);
    if(!s->raw) {
        s->io_failed=true;s->log("Physical disc: no RAM for bounded optical buffer");
        return KUI_DISC_BOOT_IO_ERROR;
    }
    s->read_problem=NULL;
    bool tail=s->leadout_tail && fad<s->leadout_end && (uint64_t)fad+count>s->leadout_tail;
    if(!s->single_reads && !s->cooked_reads && !tail) {
        enum kui_disc_boot_read_result result=kui_disc_read_boot_raw(NULL,fad,count,s->raw);
        if(result==KUI_BOOT_READ_CANCELLED)return KUI_DISC_BOOT_IO_CANCELLED;
        if(result!=KUI_BOOT_READ_OK && result!=KUI_BOOT_READ_RETRY)
            return terminal_read(s,fad,"raw read fatal; see drive log / reboot if required");
        if(stopped(s))return KUI_DISC_BOOT_IO_CANCELLED;
        uint32_t valid=0u;
        enum raw_sector_issue issues[KUI_DISC_BOOT_RUN_SECTORS]={RAW_SECTOR_OK};
        if(result==KUI_BOOT_READ_OK)for(uint32_t i=0u;i<count;i++) {
            issues[i]=raw_sector_issue(s->raw+(size_t)i*KUI_RAW_BYTES,fad+i);
            if(issues[i]==RAW_SECTOR_OK)valid|=1u<<i;
        }
        if(valid!=(1u<<count)-1u) {
            s->single_reads=true;
            s->log("Physical disc: %s raw run FAD %" PRIu32 " count %" PRIu32
                " did not verify; reducing to single-sector PIO",s->phase,fad,count);
            for(uint32_t i=0u;i<count;i++)if(!(valid&(1u<<i))) {
                enum kui_disc_boot_io_result recovered=s->cooked_reads && issues[i]!=RAW_SECTOR_ADDRESS?
                    read_cooked_one(s,fad+i,s->raw+(size_t)i*KUI_RAW_BYTES):
                    recover_one(s,fad+i,s->raw+(size_t)i*KUI_RAW_BYTES,count==1u,issues[i]);
                if(recovered!=KUI_DISC_BOOT_IO_OK)return recovered;
            }
        }
    } else for(uint32_t i=0u;i<count;i++) {
        enum kui_disc_boot_io_result result=s->cooked_reads?
            read_cooked_one(s,fad+i,s->raw+(size_t)i*KUI_RAW_BYTES):
            recover_one(s,fad+i,s->raw+(size_t)i*KUI_RAW_BYTES,false,RAW_SECTOR_OK);
        if(result!=KUI_DISC_BOOT_IO_OK)return result;
    }
    /* No caller bytes are published until every sector in this exact bounded
     * run passes its selected acquisition policy. A recovered cooked sector
     * occupies the same payload offset but is not claimed to have raw EDC. */
    if(stopped(s))return KUI_DISC_BOOT_IO_CANCELLED;
    for(uint32_t i=0;i<count;i++)memcpy(data+(size_t)i*2048u,s->raw+(size_t)i*KUI_RAW_BYTES+16u,2048u);
    uint32_t before=s->reads;s->reads+=count;s->status->done=(uint64_t)s->reads*2048u;
    if((before/128u)!=(s->reads/128u))publish(s);
    return KUI_DISC_BOOT_IO_OK;
}
static void read_phase(void *context,const char *phase) {
    struct optical_source *s=context;
    if(s->phase && !strcmp(s->phase,phase))return;
    s->phase=phase;
    s->log("Physical disc: reading %s (%s)",phase,s->cooked_reads?"paired cooked PIO":
        s->single_reads?"paired raw single-sector PIO":"paired raw PIO");
    snprintf(s->status->message,sizeof(s->status->message),"Reading physical disc: %s...",phase);
    publish(s);
}
static enum kui_disc_boot_io_result read_sector(void *context,uint32_t fad,uint8_t data[2048]) {
    return read_run(context,fad,1u,data);
}
static void extent_refused(void *context,uint32_t lba,uint32_t count) {
    struct optical_source *s=context;
    s->log("Physical disc: ISO extent refused LBA %" PRIu32 " (FAD %" PRIu64 ") count %" PRIu32
        "; outside checked DATA tracks",lba,(uint64_t)lba+150u,count);
}
static const char *source_open(struct optical_source *s,struct kui_disc_boot_source *source,uint32_t limit) {
    struct kui_toc toc[2];
    bool is_cd=true;
    if(stopped(s)) return "Physical-disc preparation stopped.";
    enum kui_disc_launch_prepare_result ready=kui_disc_prepare_launch(toc,&is_cd,s->cancel);
    if(ready!=KUI_DISC_LAUNCH_PREPARED) {
        if(ready==KUI_DISC_LAUNCH_CANCELLED) return "Physical-disc preparation stopped.";
        /* A real readiness error must not become an apparent user Stop if
         * cancellation arrives after its terminal result. */
        s->io_failed=true;
        switch(ready) {
        case KUI_DISC_LAUNCH_OPEN:return "Drive lid is open; close it before launching.";
        case KUI_DISC_LAUNCH_NO_DISC:return "No disc is inserted.";
        case KUI_DISC_LAUNCH_WAIT_TIMEOUT:return "Disc did not become ready in time; retry when it has settled.";
        case KUI_DISC_LAUNCH_UNAVAILABLE:return "Drive unavailable; check the drive log before retrying.";
        default:return "Disc initialization failed; check the disc and drive log.";
        }
    }
    if(is_cd) return "Inserted media is not a native GD-ROM.";
    if(!toc[1].count || toc[1].count>99u) return "Native GD data track unavailable; check the disc and drive log.";
    if(s->toc_digest) {
        if(toc[0].count>99u)return "Low-density GD track bounds are invalid.";
        struct kui_sha256 hash;kui_sha256_init(&hash);
        static const char domain[]="KUI physical release TOC v1";
        kui_sha256_update(&hash,domain,sizeof(domain));
        for(unsigned session=0;session<2u;session++) {
            hash_word(&hash,session);hash_word(&hash,toc[session].count);
            for(unsigned i=0;i<toc[session].count;i++) {
                const struct kui_track *track=&toc[session].tracks[i];
                hash_word(&hash,track->number);hash_word(&hash,track->control);
                hash_word(&hash,track->start);hash_word(&hash,track->end);
            }
        }
        kui_sha256_digest(&hash,s->toc_digest);
    }
    /* Keep every high-density DATA span. ISO extents use absolute LBAs and
     * may legitimately reside after intervening audio tracks (Shenmue II).
     * Use the same declared 150-sector type-transition gap as GDI capture;
     * it is a conservative format boundary, not measured INDEX 00 metadata. */
    uint32_t count=0u;
    for(unsigned i=0;i<toc[1].count;i++) {
        const struct kui_track *t=&toc[1].tracks[i];
        if((t->control!=0u && t->control!=4u) || t->start<45150u ||
           t->end<=t->start || t->end>UINT32_C(0x01000000) ||
           (i && (toc[1].tracks[i-1u].end!=t->start ||
                  toc[1].tracks[i-1u].number+1u!=t->number)))
            return "Native GD track bounds are invalid.";
        if(!(t->control&4u)) continue;
        uint32_t gap=i+1u<toc[1].count && toc[1].tracks[i+1u].control!=4u?150u:0u;
        if(t->end-t->start<=gap)return "Native GD data track bounds are invalid.";
        uint32_t end=t->end-gap;
        if(count && s->data_ranges[count-1u].end_fad==t->start)
            s->data_ranges[count-1u].end_fad=end;
        else s->data_ranges[count++]=(struct kui_disc_boot_data_range){t->start,end};
        s->log("Physical disc: DATA track %u FAD %" PRIu32 "..%" PRIu32 "; excluded gap %" PRIu32,
            t->number,t->start,end,gap);
    }
    if(count && toc[1].tracks[0].control==4u) {
        const struct kui_track *last=&toc[1].tracks[toc[1].count-1u];
        if(last->control==4u) {
            uint32_t sectors=last->end-last->start;
            s->leadout_end=last->end;
            s->leadout_tail=last->end-(sectors<150u?sectors:150u);
            s->log("Physical disc: leadout approach FAD %" PRIu32 "..%" PRIu32
                " uses single-sector PIO; full extents retained",s->leadout_tail,s->leadout_end);
        }
        *source=(struct kui_disc_boot_source){.context=s,.read_sector=read_sector,
            .cancelled=stopped,.disc_type=KUI_DISC_BOOT_GD_TYPE,
            .first_fad=s->data_ranges[0].first_fad,.end_fad=s->data_ranges[count-1u].end_fad,
            .read_run=read_run,.max_exec_bytes=limit,.data_ranges=s->data_ranges,
            .data_range_count=count,.extent_refused=extent_refused,.read_phase=read_phase};
        return NULL;
    }
    return "Native GD data track unavailable; check the disc and drive log.";
}
static bool fail(struct optical_source *s,const char *message) {
    s->status->passed=false;s->status->complete=true;
    s->status->stopped=!s->io_failed && stopped(s);s->status->errors=s->status->stopped?0u:1u;
    snprintf(s->status->message,sizeof(s->status->message),"%s",message);
    s->status->line_count=s->last_count?2u:1u;
    snprintf(s->status->lines[0],KUI_APP_LINE_CAP,"Stage: %s",s->phase);
    if(s->last_count) snprintf(s->status->lines[1],KUI_APP_LINE_CAP,"Last read: FAD %" PRIu32 ", %" PRIu32 " sector(s)",s->last_fad,s->last_count);
    if(s->read_problem) {
        snprintf(s->status->lines[s->status->line_count++],KUI_APP_LINE_CAP,"%s",s->read_problem);
        snprintf(s->status->message,sizeof(s->status->message),"Read failed at FAD %" PRIu32 ": %.86s",s->last_fad,s->read_problem);
    }
    s->log("Physical disc: %s: %s",s->phase,message);
    free(s->raw);s->raw=NULL;publish(s);return false;
}
static void describe(struct optical_source *s,const struct kui_disc_boot_plan *plan) {
    struct kui_app_status *out=s->status;
    out->line_count=4u;
    snprintf(out->lines[0],KUI_APP_LINE_CAP,"%.70s",plan->metadata.title);
    snprintf(out->lines[1],KUI_APP_LINE_CAP,"Product %.10s / region %.8s",plan->metadata.product,plan->metadata.region);
    snprintf(out->lines[2],KUI_APP_LINE_CAP,"%.16s: %" PRIu32 " bytes",plan->metadata.bootfile,plan->metadata.boot_bytes);
    snprintf(out->lines[3],KUI_APP_LINE_CAP,"IP %08" PRIX32 " / executable %08" PRIX32,plan->ip_crc32,plan->boot_crc32);
    s->log("Physical disc: %s; product %s; region %s; %s",plan->metadata.title,
        plan->metadata.product,plan->metadata.region,plan->metadata.bootfile);
    s->log("Physical disc: session DATA bounds FAD %" PRIu32 "..%" PRIu32 "; boot FAD %" PRIu32 "; %" PRIu32 " bytes",
        plan->first_fad,plan->end_fad,plan->boot_fad,plan->metadata.boot_bytes);
    s->log("Physical disc: full IP CRC32 %08" PRIx32 "; exact executable CRC32 %08" PRIx32,
        plan->ip_crc32,plan->boot_crc32);
    s->log("Physical disc acquisition: raw single requests=%" PRIu32 "; paired cooked sectors=%" PRIu32,
        s->raw_retries,s->cooked_sectors);
}
static bool inspect(struct optical_source *s,struct kui_disc_boot_source *source,
    struct kui_disc_boot_plan *plan,uint32_t limit) {
    snprintf(s->status->message,sizeof(s->status->message),"Waiting for the inserted disc to become ready...");
    s->phase="disc spin-up and identification";
    publish(s);
    const char *problem=source_open(s,source,limit);
    if(problem) return fail(s,problem);
    snprintf(s->status->message,sizeof(s->status->message),"Checking the physical disc's native executable...");
    s->phase="disc metadata and hashes";
    publish(s);
    enum kui_disc_boot_status result=kui_disc_boot_prepare(source,plan);
    if(result!=KUI_DISC_BOOT_OK) return fail(s,result==KUI_DISC_BOOT_LIMIT && limit==KUI_DISC_LAUNCH_EXEC_MAX_BYTES?
        "Native executable exceeds the 12 MiB GD Play limit.":kui_disc_boot_status_text(result));
    describe(s,plan);return true;
}
void kui_disc_launcher_check(struct kui_app_status *out,kui_log_fn log,
    kui_cancel_fn cancel,kui_app_progress_fn progress) {
    if(!out || !log || !cancel) return;
    *out=(struct kui_app_status){0};
    struct optical_source s={.log=log,.cancel=cancel,.progress=progress,.status=out,.phase="disc check"};
    struct kui_disc_boot_source source;struct kui_disc_boot_plan plan;
    if(!inspect(&s,&source,&plan,0u)) return;
    out->complete=out->passed=true;
    snprintf(out->message,sizeof(out->message),"Native GD boot bytes checked. X checks again; Y tests direct boot.");
    free(s.raw);s.raw=NULL;publish(&s);
}
bool kui_disc_launcher_identity(struct kui_disc_boot_plan *plan,struct kui_app_status *out,
    kui_log_fn log,kui_cancel_fn cancel,kui_app_progress_fn progress) {
    if(!plan || !out || !log || !cancel) return false;
    *out=(struct kui_app_status){0};memset(plan,0,sizeof(*plan));
    struct optical_source s={.log=log,.cancel=cancel,.progress=progress,.status=out,.phase="disc identity"};
    struct kui_disc_boot_source source;
    if(!inspect(&s,&source,plan,KUI_DISC_LAUNCH_EXEC_MAX_BYTES)) return false;
    out->complete=out->passed=true;free(s.raw);s.raw=NULL;publish(&s);return true;
}
struct release_metadata {
    struct optical_source *optical;
    const struct kui_disc_boot_source *source;
    struct kui_sha256 hash;
    bool gd_header;
};
static bool release_range(void *context,uint32_t lba,uint32_t count) {
    struct release_metadata *r=context;
    if(!count || lba>UINT32_MAX-150u)return false;
    uint32_t fad=lba+150u;
    for(uint32_t i=0;i<r->source->data_range_count;i++) {
        const struct kui_disc_boot_data_range *range=r->source->data_ranges+i;
        if(fad<range->first_fad)break;
        if(fad<range->end_fad)return count<=range->end_fad-fad;
    }
    extent_refused(r->optical,lba,count);return false;
}
static enum kui_game_metadata_io_result release_sector(void *context,uint32_t lba,uint8_t out[2048]) {
    struct release_metadata *r=context;struct optical_source *s=r->optical;
    if(!release_range(context,lba,1u))return KUI_GAME_METADATA_IO_ERROR;
    uint32_t fad=lba+150u;
    read_phase(s,fad==r->source->first_fad?"release IP header":
        fad>=r->source->first_fad+16u && fad<r->source->first_fad+32u?
            "release ISO descriptors":"release ISO directory");
    enum kui_disc_boot_io_result result=read_sector(s,fad,out);
    if(result==KUI_DISC_BOOT_IO_CANCELLED)return KUI_GAME_METADATA_IO_CANCELLED;
    if(result!=KUI_DISC_BOOT_IO_OK)return KUI_GAME_METADATA_IO_ERROR;
    if(fad==r->source->first_fad)r->gd_header=!memcmp(out+37u,"GD-ROM",6u);
    hash_word(&r->hash,lba);kui_sha256_update(&r->hash,out,2048u);
    return KUI_GAME_METADATA_IO_OK;
}
static bool release_product(const char *product) {
    unsigned used=0u;bool significant=false;
    for(;used<11u && product[used];used++) {
        unsigned c=(unsigned char)product[used];
        if(!((c>='A' && c<='Z') || (c>='a' && c<='z') ||
             (c>='0' && c<='9') || c=='-'))return false;
        if((c>='A' && c<='Z') || (c>='a' && c<='z') || (c>='1' && c<='9'))significant=true;
    }
    return used>0u && used<11u && significant;
}
bool kui_disc_launcher_metadata(struct kui_game_metadata *metadata,uint8_t signature[32],
    struct kui_app_status *out,kui_log_fn log,kui_cancel_fn cancel,kui_app_progress_fn progress) {
    if(metadata)memset(metadata,0,sizeof(*metadata));
    if(signature)memset(signature,0,32u);
    if(!out)return false;
    *out=(struct kui_app_status){0};
    if(!metadata || !signature || !log || !cancel) {
        out->complete=true;out->errors=1u;
        snprintf(out->message,sizeof(out->message),"Invalid physical-disc release request.");
        if(progress)progress(out);
        return false;
    }
    struct kui_game_metadata first={0};uint8_t first_signature[32]={0};uint32_t reads=0u;
    for(unsigned pass=0;pass<2u;pass++) {
        uint8_t toc[32];struct optical_source s={.log=log,.cancel=cancel,.progress=progress,
            .status=out,.reads=reads,.phase="release spin-up and identification",.toc_digest=toc};
        snprintf(out->message,sizeof(out->message),"Waiting for the inserted disc to become ready...");publish(&s);
        struct kui_disc_boot_source source;
        const char *problem=source_open(&s,&source,0u);
        if(problem)return fail(&s,problem);
        struct release_metadata reader={.optical=&s,.source=&source};
        kui_sha256_init(&reader.hash);
        static const char domain[]="KUI physical release metadata v1";
        kui_sha256_update(&reader.hash,domain,sizeof(domain));
        kui_sha256_update(&reader.hash,toc,sizeof(toc));
        const struct kui_game_metadata_ops ops={&reader,release_sector,release_range};
        struct kui_game_metadata current;
        enum kui_game_metadata_status result=kui_game_metadata_read(&ops,source.first_fad-150u,&current);
        if(result!=KUI_GAME_METADATA_OK)return fail(&s,kui_game_metadata_status_text(result));
        if(!reader.gd_header || (!current.native_gd && !current.windows_ce))
            return fail(&s,"Inserted disc has no supported GD game release metadata.");
        if(!release_product(current.product))return fail(&s,"Inserted disc has no usable release ID.");
        uint8_t digest[32];kui_sha256_digest(&reader.hash,digest);
        if(pass && (memcmp(first_signature,digest,sizeof(digest)) || memcmp(&first,&current,sizeof(first))))
            return fail(&s,"Inserted disc metadata changed; read the disc again before linking a card.");
        if(stopped(&s))return fail(&s,"Physical-disc release identification stopped.");
        reads=s.reads;free(s.raw);s.raw=NULL;
        if(!pass) {first=current;memcpy(first_signature,digest,sizeof(first_signature));}
    }
    *metadata=first;memcpy(signature,first_signature,32u);
    out->complete=out->passed=true;
    out->line_count=2u;
    snprintf(out->lines[0],KUI_APP_LINE_CAP,"%.70s",metadata->title);
    snprintf(out->lines[1],KUI_APP_LINE_CAP,"Product %.10s / region %.8s / %.6s",
        metadata->product,metadata->region,metadata->version);
    snprintf(out->message,sizeof(out->message),"Disc release identified: %.10s",metadata->product);
    if(progress)progress(out);
    return true;
}
bool kui_disc_launcher_prepare(struct kui_runtime_image *image,
    struct kui_app_status *out,kui_log_fn log,kui_cancel_fn cancel,kui_app_progress_fn progress) {
    return kui_disc_launcher_prepare_module(KUI_DISC_LAUNCH_PACKAGE,NULL,NULL,NULL,image,out,log,cancel,progress);
}
/* Staging and preflight each need a bounded sector buffer, but never at the
 * same time. Keep this frame separate so the I/O worker does not reserve both
 * buffers while hashing the physical disc during inspect(). */
static bool __attribute__((noinline)) stage_boot_bytes(struct optical_source *s,
    const struct kui_disc_boot_plan *plan,const struct kui_disc_launch_plan *layout,
    uint8_t *payload) {
    uint8_t sectors[KUI_DISC_BOOT_RUN_SECTORS*2048u];
    for(unsigned which=0;which<2u;which++) {
        read_phase(s,which?"boot executable staging":"IP.BIN staging");
        uint32_t bytes=which?layout->exec_bytes:KUI_DISC_BOOT_IP_BYTES;
        uint32_t fad=which?plan->boot_fad:plan->first_fad;
        uint8_t *destination=payload+(which?layout->exec_offset:layout->ip_offset);
        for(uint32_t done=0;done<bytes;) {
            uint32_t count=(bytes-done+2047u)/2048u;
            if(count>KUI_DISC_BOOT_RUN_SECTORS) count=KUI_DISC_BOOT_RUN_SECTORS;
            if(read_run(s,fad,count,sectors)!=KUI_DISC_BOOT_IO_OK) return false;
            uint32_t n=bytes-done;if(n>count*2048u) n=count*2048u;
            memcpy(destination+done,sectors,n);done+=n;fad+=count;
        }
    }
    return true;
}
bool kui_disc_launcher_prepare_module(const char *package,kui_disc_launcher_card_fn card,
    void *card_context,const struct kui_disc_launcher_region *region,struct kui_runtime_image *image,
    struct kui_app_status *out,kui_log_fn log,kui_cancel_fn cancel,kui_app_progress_fn progress) {
    if(!package || !image || !out || !log || !cancel) return false;
    *image=(struct kui_runtime_image){0};*out=(struct kui_app_status){0};
    struct optical_source s={.log=log,.cancel=cancel,.progress=progress,.status=out,.phase="bootstrap template"};
    if(stopped(&s)) return fail(&s,"Direct disc boot stopped.");
    /* Only the optional frontend supplies a card callback. The validated
     * physical-only baseline never reads a virtual preference or card. */
    if(!kui_sd_connect()) return fail(&s,"SD unavailable for the direct-disc bootstrap.");
    FATFS fs;bool mounted=kui_mount(&fs,log),loaded=false;
    if(mounted) loaded=kui_runtime_read(package,image,log,cancel)==KUI_RUNTIME_OK;
    bool released=f_mount(NULL,"0:",0)==FR_OK;
    kui_sd_disconnect();
    if(!mounted || !loaded || !released) {
        kui_runtime_free(image);return fail(&s,"Direct-disc bootstrap unavailable; copy the complete test KUI folder.");
    }
    struct kui_disc_launch_plan layout;
    if(!kui_disc_launch_layout(image->data,image->info.payload_bytes,4096u,&layout)) {
        kui_runtime_free(image);return fail(&s,"Direct-disc bootstrap template refused.");
    }
    if((layout.flags==KUI_DISC_LAUNCH_VMU_FLAGS)!=!!card) {
        kui_runtime_free(image);return fail(&s,"Disc module does not match the requested VMU mode.");
    }
    struct kui_disc_boot_source source;struct kui_disc_boot_plan plan;
    if(!inspect(&s,&source,&plan,KUI_DISC_LAUNCH_EXEC_MAX_BYTES)) {kui_runtime_free(image);return false;}
    if(region && !region->region_free &&
       ((region->console!='J' && region->console!='U' && region->console!='E') ||
        !memchr(plan.metadata.region,region->console,sizeof(plan.metadata.region)))) {
        kui_runtime_free(image);return fail(&s,"Disc region differs from console; enable Region-free in GD Play Options.");
    }
    if(!kui_disc_launch_layout(image->data,image->info.payload_bytes,plan.metadata.boot_bytes,&layout)) {
        kui_runtime_free(image);return fail(&s,"Installed GD Play module has a smaller executable limit; copy the matching update.");
    }
    s.phase="staging allocation";
    log("Physical disc: allocating %" PRIu32 " byte payload for %" PRIu32 " byte executable",layout.payload_bytes,layout.exec_bytes);
    void *grown=realloc(image->data,layout.payload_bytes);
    if(!grown) {kui_runtime_free(image);return fail(&s,"Not enough contiguous RAM to stage this disc executable.");}
    image->data=grown;
    memset((uint8_t *)grown+layout.module_bytes,0,layout.payload_bytes-layout.module_bytes);
    snprintf(out->message,sizeof(out->message),"Staging checked physical-disc boot bytes...");publish(&s);
    s.phase="staging disc bytes";
    if(!stage_boot_bytes(&s,&plan,&layout,grown)) {
        kui_runtime_free(image);return fail(&s,"Physical-disc read stopped or failed while staging boot bytes.");
    }
    layout.ip_crc32=plan.ip_crc32;layout.exec_crc32=plan.boot_crc32;
    memcpy(layout.ip_sha256,plan.ip_sha256,32u);memcpy(layout.exec_sha256,plan.boot_sha256,32u);
    if(card) {
        s.phase="checking virtual VMU compatibility and card";
        uint8_t profile[KUI_DISC_LAUNCH_VMU_PROFILE_BYTES]={0};
        char refusal[KUI_DISC_LAUNCHER_REFUSAL_BYTES]={0};
        bool ready=!stopped(&s) && card(plan.metadata.product,
            (const uint8_t *)grown+layout.exec_offset,layout.exec_bytes,
            profile,refusal,card_context,log,cancel);
        refusal[sizeof(refusal)-1u]=0;
        if(!ready) {
            memset(profile,0,sizeof(profile));kui_runtime_free(image);
            return fail(&s,refusal[0]?refusal:
                "Virtual VMU preparation refused; check the selected card and Diagnostics.");
        }
        memcpy((uint8_t *)grown+layout.vmu_profile_offset,profile,sizeof(profile));
        struct kui_sha256 hash;kui_sha256_init(&hash);
        kui_sha256_update(&hash,profile,sizeof(profile));kui_sha256_digest(&hash,layout.vmu_profile_sha256);
        memset(profile,0,sizeof(profile));
    }
    struct kui_disc_launch_plan checked;
    s.phase="staged byte fingerprints";
    if(stopped(&s) || !kui_disc_launch_encode((uint8_t *)grown+KUI_DISC_LAUNCH_CONFIG_OFFSET,&layout) ||
       !kui_disc_launch_validate(grown,layout.payload_bytes,&checked)) {
        kui_runtime_free(image);return fail(&s,"Physical-disc boot bytes changed or handoff was stopped.");
    }
    s.phase="drive format handoff";
    if(!kui_disc_native_handoff(plan.first_fad,(const uint8_t *)grown+layout.ip_offset)) {
        kui_runtime_free(image);return fail(&s,"Could not restore checked 2048-byte drive reads for the game.");
    }
    image->info.payload_bytes=image->info.memory_bytes=layout.payload_bytes;
    image->info.crc32=kui_stage_crc32(0,grown,layout.payload_bytes);
    out->complete=out->passed=true;
    snprintf(out->message,sizeof(out->message),"%s",card?
        "Direct-disc boot prepared with the selected virtual VMU.":
        "Direct-disc boot prepared. Virtual VMU is disabled for this test.");
    log("Physical disc: direct native boot prepared; build %s; %s",image->info.build,
        card?"selected virtual card prepared":"no VMU/card lookup");
    free(s.raw);s.raw=NULL;publish(&s);return true;
}
