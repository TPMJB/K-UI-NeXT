/* SPDX-License-Identifier: GPL-3.0-only */
/* Terminal-only diagnostics. All snapshots are private P2 state. No CE
 * pointer, Maple payload or SD block is followed, and no game resumes. */
#include "kui/vc2_ce_vmu_diagnostic.h"
#include "kui/vc2_ce_vmu_handoff.h"
#include "kui/vc2_ce_vmu_prepare.h"
#include "kui/armada_ce_maple_traffic_types.h"
#include "kui/armada_ce_maple_fence_types.h"
#include "kui/armada_ce_vmu_a2_publish.h"
#if KUI_VC2_VMU_PERSIST
#include "kui/armada_ce_vmu_runtime.h"
#include "kui/vc2_ce_vmu_loan.h"
#endif
#include "retail_display.h"
#if KUI_SMASH_SWIRL_VMU
/* Swirl's source-checked text relocation uses additional installer code.
 * Short captions retain every existing numeric snapshot within the same
 * fixed helper image and stack allocation. */
#define CE_DIAG_LABEL(full,short_label) short_label
#else
#define CE_DIAG_LABEL(full,short_label) full
#endif

extern struct kui_armada_ce_maple_traffic_snapshot kui_armada_ce_maple_hook_log;
extern struct kui_armada_ce_maple_fence_snapshot kui_armada_ce_maple_fence_log;
extern struct kui_armada_ce_vmu_a2_publish_snapshot kui_armada_ce_vmu_a2_log;
extern volatile uint32_t kui_armada_ce_maple_traffic_freeze;
extern volatile uint32_t kui_armada_ce_maple_proof_initial,kui_armada_ce_maple_proof_final;
extern struct kui_vc2_ce_vmu_prepare_report kui_vc2_ce_vmu_prepare_report;
uint32_t kui_vc2_ce_vmu_call_context[8];
uint32_t kui_vc2_ce_vmu_owner_context[5];
static struct {
    struct retail_display_state display;
    struct kui_armada_ce_maple_traffic_snapshot traffic;
    struct kui_armada_ce_maple_fence_snapshot fence;
    struct kui_armada_ce_vmu_a2_publish_snapshot a2;
    struct kui_vc2_ce_vmu_prepare_report prepare;
    uint32_t context[8],owner[5],reader[16],top[5],mmu;
#if KUI_VC2_VMU_WRITE_PROBE
    uint32_t write[20];
#endif
#if KUI_VC2_VMU_PERSIST
    uint32_t runtime[KUI_ARMADA_CE_VMU_RUNTIME_SNAPSHOT_WORDS],loan[8];
#endif
} stopped;

static void first_page(void) {
    retail_display_restore(&stopped.display);
#if KUI_VC2_VMU_PERSIST
    retail_display_line(CE_DIAG_LABEL("VC2 SAVE 1 OF 3 STATUS","SWIRL 1/3 STATUS"));
    retail_display_values(CE_DIAG_LABEL("WHY PHASE FIRST FINAL VIS","TOP"),stopped.top,5u);
    retail_display_values(CE_DIAG_LABEL("START READY OWNER BINDFAULT BUSY","STATE"),stopped.runtime,5u);
    retail_display_values(CE_DIAG_LABEL("REQ READ PHASES DURABLE RDFINAL","READ"),stopped.runtime+5u,5u);
    retail_display_values(CE_DIAG_LABEL("BLOCK MASK ACTIVE TRIED RDBLOCK","BLOCK"),stopped.runtime+10u,5u);
    retail_display_values(CE_DIAG_LABEL("QUEUE TICKET OP BLOCK FAIL","QUEUE"),stopped.runtime+15u,5u);
    retail_display_values(CE_DIAG_LABEL("SUBMIT DONE PROVIDERBUSY BUSY IOERR","IO"),stopped.runtime+20u,5u);
    retail_display_values(CE_DIAG_LABEL("A2 FAULT AT ACTUAL EXPECT WHY","VMU FAULT"),stopped.a2.fault,5u);
    retail_display_values(CE_DIAG_LABEL("TRAF FAULT AT ACTUAL EXPECT WHY","TRAF FAULT"),stopped.traffic.fault,5u);
    retail_display_values(CE_DIAG_LABEL("FENCE FAULT AT ACTUAL EXPECT WHY","FENCE FAULT"),stopped.fence.fault,5u);
    retail_display_line(CE_DIAG_LABEL("PHOTO ALL THREE PAGES THEN POWER OFF","PHOTO 3 THEN POWER OFF"));
#else
    retail_display_line(
#if KUI_VC2_VMU_WRITE_PROBE
        "VC2 WRITE 1 OF 3 STATUS"
#else
        "VC2 VMU 1 OF 2 READ ONLY"
#endif
    );
    retail_display_values(CE_DIAG_LABEL("WHY PHASE FIRST FINAL VIS","TOP"),stopped.top,5u);
    retail_display_values("TRAF MAGIC VIS OK BAD READS",stopped.traffic.counts,5u);
    retail_display_values("FENCE MAGIC PRE POST HELD BACK",stopped.fence.counts,5u);
    retail_display_values("A2 MAGIC VIS DONE ADVERT REPLY",stopped.a2.counts,5u);
    retail_display_values("CMD INFO ALL MEDIA READ FINAL",stopped.a2.commands,5u);
    retail_display_values("A2 FAIL CALL STORE FLAGS TRY",stopped.a2.status,5u);
    retail_display_values(CE_DIAG_LABEL("A2 FAULT AT ACTUAL EXPECT WHY","VMU FAULT"),stopped.a2.fault,5u);
    retail_display_values(CE_DIAG_LABEL("TRAF FAULT AT ACTUAL EXPECT WHY","TRAF FAULT"),stopped.traffic.fault,5u);
    retail_display_values(CE_DIAG_LABEL("FENCE FAULT AT ACTUAL EXPECT WHY","FENCE FAULT"),stopped.fence.fault,5u);
    retail_display_line(
#if KUI_VC2_VMU_WRITE_PROBE
        CE_DIAG_LABEL("PHOTO ALL THREE PAGES THEN POWER OFF","PHOTO 3 THEN POWER OFF")
#else
        "PHOTO BOTH PAGES THEN POWER OFF"
#endif
    );
#endif
}
static void second_page(void) {
#if KUI_VC2_VMU_PERSIST
    retail_display_restore(&stopped.display);
    retail_display_line(CE_DIAG_LABEL("VC2 SAVE 2 OF 3 SD FIRST CAUSE","SWIRL 2/3 SD CAUSE"));
    retail_display_values(CE_DIAG_LABEL("SDSTART BOUND BUSY FAULT OP","SD STATE"),stopped.runtime+25u,5u);
    retail_display_values(CE_DIAG_LABEL("BLOCK CRC READ COMMIT BUSY","SD READ"),stopped.runtime+30u,5u);
    retail_display_values(CE_DIAG_LABEL("FAIL IDCHK SECTREAD SECTWRITE LBA","SD IO"),stopped.runtime+35u,5u);
    retail_display_values(CE_DIAG_LABEL("SDWHY SDRESULT STORE LOAN PIOFAULT","SD FAULT"),stopped.runtime+40u,5u);
    retail_display_values(CE_DIAG_LABEL("PIODETAIL STAGE ACQUIRED CAPTURE SELECT","PIO STATE"),stopped.runtime+45u,5u);
    retail_display_values(CE_DIAG_LABEL("BASE JOURNAL HC CAPLOW CAPHIGH","CARD"),stopped.runtime+50u,5u);
    retail_display_values(CE_DIAG_LABEL("PIOMODE WORK BEGIN END CHECKPOINT","PIO WORK"),stopped.runtime+55u,5u);
    retail_display_line(CE_DIAG_LABEL("SDWHY IS THE FIRST PROVIDER CAUSE","SDWHY FIRST CAUSE"));
    retail_display_line(CE_DIAG_LABEL("NEGATIVE RESULTS USE HEX TWOS COMPLEMENT","RESULTS SIGNED HEX"));
    retail_display_line(CE_DIAG_LABEL("PHOTO ALL THREE PAGES THEN POWER OFF","PHOTO 3 THEN POWER OFF"));
#else
    uint32_t words[5];
    retail_display_restore(&stopped.display);
    retail_display_line(
#if KUI_VC2_VMU_WRITE_PROBE
        "VC2 WRITE 2 OF 3 CONTEXT"
#else
        "VC2 VMU 2 OF 2 CONTEXT"
#endif
    );
    retail_display_values(CE_DIAG_LABEL("RAW CEVA PHYSICAL PROC RECORDS","OWNER"),stopped.owner,5u);
    words[0]=stopped.context[2];words[1]=stopped.context[3];
    words[2]=stopped.context[4]+44u;words[3]=stopped.context[5];words[4]=stopped.mmu;
    retail_display_values(CE_DIAG_LABEL("MODE SR CE SP BRIDGE PR MMU","CALLER"),words,5u);
    words[0]=stopped.context[6];words[1]=stopped.context[7];
    words[2]=stopped.prepare.kernel_crc;words[3]=stopped.prepare.ram_end;
    words[4]=stopped.prepare.worker_word;
    retail_display_values("TTB CCR KCRC RAMEND PATCH",words,5u);
    words[0]=stopped.prepare.magic;words[1]=stopped.prepare.phase;
    words[2]=stopped.prepare.reason;words[3]=stopped.prepare.actual;
    words[4]=stopped.prepare.expected;
    retail_display_values(CE_DIAG_LABEL("PREP MAGIC PHASE WHY ACTUAL EXPECT","PREP"),words,5u);
    retail_display_values(CE_DIAG_LABEL("PARENT REQ REPLY RX CAP FUNCTION","PARENT"),stopped.a2.parent,5u);
    retail_display_values(CE_DIAG_LABEL("TARGET REQ REPLY RX CAP BYTES","TARGET"),stopped.a2.target,5u);
    retail_display_values(CE_DIAG_LABEL("BEFORE EN TRIGGER START TX WANT","DMA"),stopped.fence.before,5u);
    retail_display_values("HELD EN IDLE POSTEN POSTST POSTTX",stopped.fence.held,5u);
    retail_display_values("AFTER EN START PREMODE POSTMODE EXT",stopped.fence.after,5u);
    words[0]=stopped.reader[0];words[1]=stopped.reader[1];words[2]=stopped.reader[7];
    words[3]=stopped.reader[8];words[4]=stopped.reader[6];
    retail_display_values(CE_DIAG_LABEL("READER TOKEN CMD STATUS PEND ERR","READER"),words,5u);
#endif
}
#if KUI_VC2_VMU_PERSIST
static void persist_context_page(void) {
    uint32_t words[5];
    retail_display_restore(&stopped.display);
    retail_display_line(CE_DIAG_LABEL("VC2 SAVE 3 OF 3 LOAN AND CONTEXT","SWIRL 3/3 CONTEXT"));
    retail_display_values(CE_DIAG_LABEL("LOAN MAGIC STATE CLAIM RELEASE BUSY","LOAN"),stopped.loan,5u);
    retail_display_values(CE_DIAG_LABEL("FIRSTFAULT REGION RESULT","FAULT REGION RESULT"),stopped.loan+5u,3u);
    retail_display_values(CE_DIAG_LABEL("RAW CEVA PHYSICAL PROC RECORDS","OWNER"),stopped.owner,5u);
    words[0]=stopped.context[2];words[1]=stopped.context[3];
    words[2]=stopped.context[4]+44u;words[3]=stopped.context[5];words[4]=stopped.mmu;
    retail_display_values(CE_DIAG_LABEL("MODE SR CE SP BRIDGE PR MMU","CALLER"),words,5u);
    words[0]=stopped.prepare.magic;words[1]=stopped.prepare.phase;
    words[2]=stopped.prepare.reason;words[3]=stopped.prepare.actual;
    words[4]=stopped.prepare.expected;
    retail_display_values(CE_DIAG_LABEL("PREP MAGIC PHASE WHY ACTUAL EXPECT","PREP"),words,5u);
    retail_display_values(CE_DIAG_LABEL("PARENT REQ REPLY RX CAP FUNCTION","PARENT"),stopped.a2.parent,5u);
    retail_display_values(CE_DIAG_LABEL("TARGET REQ REPLY RX CAP BYTES","TARGET"),stopped.a2.target,5u);
    words[0]=stopped.reader[0];words[1]=stopped.reader[1];words[2]=stopped.reader[7];
    words[3]=stopped.reader[8];words[4]=stopped.reader[6];
    retail_display_values(CE_DIAG_LABEL("READER TOKEN CMD STATUS PEND ERR","READER"),words,5u);
    retail_display_values(CE_DIAG_LABEL("BEFORE EN TRIGGER START TX WANT","DMA"),stopped.fence.before,5u);
    retail_display_line(CE_DIAG_LABEL("PHOTO ALL THREE PAGES THEN POWER OFF","PHOTO 3 THEN POWER OFF"));
}
#endif
#if KUI_VC2_VMU_WRITE_PROBE
static void write_page(void) {
    retail_display_restore(&stopped.display);
    retail_display_line("VC2 WRITE 3 OF 3 STAGED BLOCK");
    retail_display_values("MAGIC READY REQUESTS READS PHASE ACK",stopped.write,5u);
    retail_display_values("COMMIT TRY BLOCK MASK CRC PENDING",stopped.write+5u,5u);
    retail_display_values("ACK0 ACK1 ACK2 ACK3 DURABLE",stopped.write+10u,5u);
    retail_display_values("BUSY REFUSED HEADER LOCATION PRIVATE CAP",stopped.write+15u,5u);
    retail_display_line("MASK 0000000F MEANS FOUR PHASES");
    retail_display_line("PENDING 1 AND DURABLE 0 EXPECTED");
    retail_display_line("STAGED IN RAM - NO SD WRITES");
    retail_display_line(CE_DIAG_LABEL("PHOTO ALL THREE PAGES THEN POWER OFF","PHOTO 3 THEN POWER OFF"));
}
#endif
void kui_vc2_ce_vmu_diagnostic_stop(uint32_t reason,uint32_t phase) {
    /* The calling shim has already masked IRQs and owns this private stack.
     * Take one bounded snapshot, then display only that immutable copy. */
    kui_armada_ce_maple_traffic_freeze=1u;
    const struct retail_display_state *display=kui_vc2_ce_vmu_saved_display();
    if(display)stopped.display=*display;
    else retail_display_capture(&stopped.display);
    stopped.traffic=kui_armada_ce_maple_hook_log;
    stopped.fence=kui_armada_ce_maple_fence_log;
    stopped.a2=kui_armada_ce_vmu_a2_log;
    stopped.prepare=kui_vc2_ce_vmu_prepare_report;
    for(unsigned i=0;i<8u;i++)stopped.context[i]=kui_vc2_ce_vmu_call_context[i];
    for(unsigned i=0;i<5u;i++)stopped.owner[i]=kui_vc2_ce_vmu_owner_context[i];
    kui_vc2_ce_vmu_service_snapshot(stopped.reader);
    stopped.mmu=*(volatile const uint32_t *)(uintptr_t)UINT32_C(0xff000010);
    stopped.top[0]=reason;stopped.top[1]=phase;
    stopped.top[2]=kui_armada_ce_maple_proof_initial;
    stopped.top[3]=kui_armada_ce_maple_proof_final;
    stopped.top[4]=stopped.traffic.counts[1];
#if KUI_VC2_VMU_WRITE_PROBE
    kui_vc2_ce_vmu_write_snapshot(stopped.write);
#endif
#if KUI_VC2_VMU_PERSIST
    kui_armada_ce_vmu_runtime_snapshot(stopped.runtime);
    kui_vc2_ce_vmu_loan_snapshot(stopped.loan);
#endif
    for(;;) {
        first_page();retail_display_pause(480u);
        second_page();retail_display_pause(480u);
#if KUI_VC2_VMU_WRITE_PROBE
        write_page();retail_display_pause(480u);
#elif KUI_VC2_VMU_PERSIST
        persist_context_page();retail_display_pause(480u);
#endif
    }
}
