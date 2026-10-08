/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/toy_pilot.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Ownership-model tests use authored control events, with no game data or
 * AICA emulation. applied_end is evidence supplied by the worker, not a clock
 * evaluated here: consumed Start, matching finite setup, observed activity,
 * retirement and conservative elapsed duration must be checked there. */
static unsigned checks;
#define CHECK(condition) do { \
    ++checks; \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
        abort(); \
    } \
} while (0)
#define REJECT_UNCHANGED(model, operation) do { \
    struct kui_toy_pilot_model saved_model = (model); \
    CHECK(!(operation)); \
    CHECK(memcmp(&saved_model, &(model), sizeof(saved_model)) == 0); \
} while (0)

static void initialize(struct kui_toy_pilot_model *m) {
    memset(m, 0xa5, sizeof(*m));
    kui_toy_pilot_model_init(m);
    CHECK(m->generation == 1 && !m->stopped);
    CHECK(m->active_bank == UINT32_MAX && m->pending_bank == UINT32_MAX);
    CHECK(m->banks[0].state == KUI_TOY_PILOT_BANK_EMPTY);
    CHECK(m->banks[1].state == KUI_TOY_PILOT_BANK_EMPTY);
}

static void ready(struct kui_toy_pilot_model *m, uint32_t b,
                  uint32_t first, uint32_t frames) {
    CHECK(kui_toy_pilot_model_fill_begin(m, b, m->generation, first, frames));
    CHECK(kui_toy_pilot_model_fill_commit(m, b, m->generation, frames));
    CHECK(m->banks[b].state == KUI_TOY_PILOT_BANK_READY);
}

static void queued(struct kui_toy_pilot_model *m, uint32_t b) {
    CHECK(kui_toy_pilot_model_start_queued(m, b, m->generation));
    CHECK(m->pending_bank == b && m->active_bank == UINT32_MAX);
    CHECK(m->banks[b].state == KUI_TOY_PILOT_BANK_START_WAIT);
}

static void playing(struct kui_toy_pilot_model *m, uint32_t b) {
    queued(m, b);
    CHECK(kui_toy_pilot_model_start_applied(m, b, m->generation, true, true));
    CHECK(m->active_bank == b && m->pending_bank == UINT32_MAX);
    CHECK(m->banks[b].state == KUI_TOY_PILOT_BANK_PLAYING);
}

static void partial_fill_cannot_start(void) {
    struct kui_toy_pilot_model m;
    initialize(&m);
    CHECK(kui_toy_pilot_model_fill_begin(&m, 0, 1, 588, 1176));
    CHECK(kui_toy_pilot_model_fill_commit(&m, 0, 1, 588));
    CHECK(m.banks[0].filled == 588 && m.banks[0].first_frame == 588);
    REJECT_UNCHANGED(m, kui_toy_pilot_model_start_queued(&m, 0, 1));
    REJECT_UNCHANGED(m, kui_toy_pilot_model_fill_commit(&m, 0, 1, 589));
    REJECT_UNCHANGED(m, kui_toy_pilot_model_fill_commit(&m, 0, 1, 0));
    REJECT_UNCHANGED(m, kui_toy_pilot_model_fill_begin(&m, 0, 1, 0, 1));
    CHECK(kui_toy_pilot_model_fill_commit(&m, 0, 1, 588));
    CHECK(m.banks[0].state == KUI_TOY_PILOT_BANK_READY);
    REJECT_UNCHANGED(m, kui_toy_pilot_model_fill_commit(&m, 0, 1, 1));
    queued(&m, 0);
    REJECT_UNCHANGED(m, kui_toy_pilot_model_fill_commit(&m, 0, 1, 1));
    REJECT_UNCHANGED(m, kui_toy_pilot_model_fill_begin(&m, 0, 1, 0, 1));
}

static void range_failures_do_not_claim_memory(void) {
    struct kui_toy_pilot_model m;
    initialize(&m);
    const uint32_t bad_banks[] = {2, UINT32_MAX};
    for (unsigned i = 0; i < sizeof(bad_banks) / sizeof(*bad_banks); ++i) {
        uint32_t b = bad_banks[i];
        REJECT_UNCHANGED(m, kui_toy_pilot_model_fill_begin(&m, b, 1, 0, 1));
        REJECT_UNCHANGED(m, kui_toy_pilot_model_fill_commit(&m, b, 1, 1));
        REJECT_UNCHANGED(m, kui_toy_pilot_model_start_queued(&m, b, 1));
        REJECT_UNCHANGED(m, kui_toy_pilot_model_start_applied(&m, b, 1, true, true));
        REJECT_UNCHANGED(m, kui_toy_pilot_model_finite_end(&m, b, 1, true, true, true));
    }
    REJECT_UNCHANGED(m, kui_toy_pilot_model_fill_begin(&m, 0, 1, 0, 0));
    REJECT_UNCHANGED(m, kui_toy_pilot_model_fill_begin(
        &m, 0, 1, 0, KUI_TOY_PILOT_BANK_FRAMES + 1));
    REJECT_UNCHANGED(m, kui_toy_pilot_model_fill_begin(&m, 0, 1, UINT32_MAX, 1));
    REJECT_UNCHANGED(m, kui_toy_pilot_model_fill_begin(&m, 0, 0, 0, 1));
    REJECT_UNCHANGED(m, kui_toy_pilot_model_fill_begin(&m, 0, 2, 0, 1));
    ready(&m, 0, UINT32_MAX - KUI_TOY_PILOT_BANK_FRAMES,
          KUI_TOY_PILOT_BANK_FRAMES);
    CHECK(m.banks[0].first_frame + m.banks[0].frames == UINT32_MAX);
}

static void stereo_application_requires_both_ports(void) {
    struct kui_toy_pilot_model m;
    initialize(&m);
    ready(&m, 0, 0, KUI_TOY_PILOT_BANK_FRAMES);
    ready(&m, 1, KUI_TOY_PILOT_BANK_FRAMES, KUI_TOY_PILOT_BANK_FRAMES);
    REJECT_UNCHANGED(m, kui_toy_pilot_model_start_applied(&m, 0, 1, true, true));
    queued(&m, 0);
    for (unsigned ports = 0; ports < 3; ++ports)
        REJECT_UNCHANGED(m, kui_toy_pilot_model_start_applied(
            &m, 0, 1, (ports & 1) != 0, (ports & 2) != 0));
    REJECT_UNCHANGED(m, kui_toy_pilot_model_start_applied(&m, 1, 1, true, true));
    REJECT_UNCHANGED(m, kui_toy_pilot_model_start_applied(&m, 0, 0, true, true));
    REJECT_UNCHANGED(m, kui_toy_pilot_model_start_queued(&m, 1, 1));
    CHECK(kui_toy_pilot_model_start_applied(&m, 0, 1, true, true));
    REJECT_UNCHANGED(m, kui_toy_pilot_model_start_applied(&m, 0, 1, true, true));
    REJECT_UNCHANGED(m, kui_toy_pilot_model_start_queued(&m, 1, 1));
    REJECT_UNCHANGED(m, kui_toy_pilot_model_fill_begin(&m, 0, 1, 0, 1));
}

static void retirement_alone_does_not_release_a_bank(void) {
    struct kui_toy_pilot_model m;
    initialize(&m);
    ready(&m, 0, 0, KUI_TOY_PILOT_BANK_FRAMES);
    playing(&m, 0);
    /* The hardware's activity flags can both be zero before the worker has
     * verified consumed-start and elapsed finite duration. This must not
     * authorize writes into the sound bank. */
    for (unsigned evidence = 0; evidence < 7; ++evidence) {
        REJECT_UNCHANGED(m, kui_toy_pilot_model_finite_end(
            &m, 0, 1, (evidence & 1) != 0, (evidence & 2) != 0,
            (evidence & 4) != 0));
        REJECT_UNCHANGED(m, kui_toy_pilot_model_fill_begin(&m, 0, 1, 16384, 1));
    }
    REJECT_UNCHANGED(m, kui_toy_pilot_model_finite_end(&m, 0, 2, true, true, true));
    CHECK(kui_toy_pilot_model_finite_end(&m, 0, 1, true, true, true));
    CHECK(m.active_bank == UINT32_MAX && m.banks[0].state == KUI_TOY_PILOT_BANK_EMPTY);
    REJECT_UNCHANGED(m, kui_toy_pilot_model_finite_end(&m, 0, 1, true, true, true));
    ready(&m, 0, 16384, 100);
}

static void alternate_bank_does_not_restart_while_pair_is_owned(void) {
    struct kui_toy_pilot_model m;
    initialize(&m);
    ready(&m, 0, 0, 16384);
    playing(&m, 0);
    /* Prefill is allowed in the other bank, but a second MultiPlay is not. */
    ready(&m, 1, 16384, 441);
    REJECT_UNCHANGED(m, kui_toy_pilot_model_start_queued(&m, 1, 1));
    REJECT_UNCHANGED(m, kui_toy_pilot_model_finite_end(&m, 1, 1, true, true, true));
    CHECK(kui_toy_pilot_model_finite_end(&m, 0, 1, true, true, true));
    playing(&m, 1);
    CHECK(m.banks[1].first_frame == 16384 && m.banks[1].frames == 441);
    ready(&m, 0, 16825, 1);
    REJECT_UNCHANGED(m, kui_toy_pilot_model_start_queued(&m, 0, 1));
    CHECK(kui_toy_pilot_model_finite_end(&m, 1, 1, true, true, true));
    playing(&m, 0);
}

static void cancel_pending_start_retains_ownership(void) {
    struct kui_toy_pilot_model m;
    initialize(&m);
    ready(&m, 0, 0, 16384);
    ready(&m, 1, 16384, 16384);
    queued(&m, 0);
    CHECK(kui_toy_pilot_model_reset(&m));
    CHECK(m.generation == 2 && m.stopped && m.pending_bank == 0);
    CHECK(m.banks[0].generation == 1 && m.banks[1].state == KUI_TOY_PILOT_BANK_EMPTY);
    /* Old queued work may still be consumed by ARM. A newer mailbox epoch
     * cannot turn the already queued sound bank into writable memory. */
    REJECT_UNCHANGED(m, kui_toy_pilot_model_start_applied(&m, 0, 1, true, true));
    REJECT_UNCHANGED(m, kui_toy_pilot_model_fill_begin(&m, 0, 2, 0, 1));
    REJECT_UNCHANGED(m, kui_toy_pilot_model_fill_begin(&m, 1, 2, 0, 1));
    REJECT_UNCHANGED(m, kui_toy_pilot_model_finite_end(&m, 0, 1, true, true, false));
    REJECT_UNCHANGED(m, kui_toy_pilot_model_stop_applied(&m, 1, true, true, true));
    CHECK(kui_toy_pilot_model_stop_applied(&m, 2, true, true, true));
    CHECK(!m.stopped && m.pending_bank == UINT32_MAX);
    ready(&m, 0, 22050, 100);
    REJECT_UNCHANGED(m, kui_toy_pilot_model_start_queued(&m, 0, 1));
    playing(&m, 0);
}

static void repeated_supersession_cannot_reclaim_active_memory(void) {
    struct kui_toy_pilot_model m;
    initialize(&m);
    ready(&m, 1, 0, 16384);
    playing(&m, 1);
    CHECK(kui_toy_pilot_model_fill_begin(&m, 0, 1, 16384, 100));
    CHECK(kui_toy_pilot_model_fill_commit(&m, 0, 1, 50));
    CHECK(kui_toy_pilot_model_reset(&m));
    CHECK(kui_toy_pilot_model_reset(&m));
    CHECK(m.generation == 3 && m.active_bank == 1 && m.banks[1].generation == 1);
    CHECK(m.banks[0].state == KUI_TOY_PILOT_BANK_EMPTY);
    REJECT_UNCHANGED(m, kui_toy_pilot_model_fill_commit(&m, 0, 1, 50));
    REJECT_UNCHANGED(m, kui_toy_pilot_model_fill_begin(&m, 1, 3, 0, 1));
    REJECT_UNCHANGED(m, kui_toy_pilot_model_finite_end(&m, 1, 3, true, true, true));
    /* Genuine finite end of the old owned bank is still useful evidence.
     * It releases only that bank; it does not resume a cancelled request. */
    CHECK(kui_toy_pilot_model_finite_end(&m, 1, 1, true, true, true));
    CHECK(m.stopped && m.active_bank == UINT32_MAX);
    REJECT_UNCHANGED(m, kui_toy_pilot_model_fill_begin(&m, 1, 3, 0, 1));
    REJECT_UNCHANGED(m, kui_toy_pilot_model_stop_applied(&m, 2, true, true, true));
    CHECK(kui_toy_pilot_model_stop_applied(&m, 3, true, true, true));
    ready(&m, 1, 44100, 588);
    playing(&m, 1);
    REJECT_UNCHANGED(m, kui_toy_pilot_model_finite_end(&m, 1, 1, true, true, true));
}

static void cancel_stop_requires_current_complete_evidence(void) {
    struct kui_toy_pilot_model m;
    initialize(&m);
    ready(&m, 0, 0, 16384);
    playing(&m, 0);
    ready(&m, 1, 16384, 16384);
    CHECK(kui_toy_pilot_model_reset(&m));
    for (unsigned evidence = 0; evidence < 7; ++evidence) {
        REJECT_UNCHANGED(m, kui_toy_pilot_model_stop_applied(
            &m, 2, (evidence & 1) != 0, (evidence & 2) != 0,
            (evidence & 4) != 0));
        REJECT_UNCHANGED(m, kui_toy_pilot_model_fill_begin(&m, 0, 2, 0, 1));
    }
    CHECK(kui_toy_pilot_model_stop_applied(&m, 2, true, true, true));
    CHECK(m.active_bank == UINT32_MAX && m.pending_bank == UINT32_MAX && !m.stopped);
    CHECK(m.banks[0].state == KUI_TOY_PILOT_BANK_EMPTY);
    CHECK(m.banks[1].state == KUI_TOY_PILOT_BANK_EMPTY);
    REJECT_UNCHANGED(m, kui_toy_pilot_model_start_applied(&m, 0, 1, true, true));
    REJECT_UNCHANGED(m, kui_toy_pilot_model_finite_end(&m, 0, 1, true, true, true));
    ready(&m, 0, 0, 1);
}

static void prepared_work_discarded_on_generation_change(void) {
    struct kui_toy_pilot_model m;
    initialize(&m);
    ready(&m, 0, 0, 100);
    CHECK(kui_toy_pilot_model_fill_begin(&m, 1, 1, 100, 100));
    CHECK(kui_toy_pilot_model_fill_commit(&m, 1, 1, 50));
    CHECK(kui_toy_pilot_model_reset(&m));
    CHECK(m.banks[0].state == KUI_TOY_PILOT_BANK_EMPTY);
    CHECK(m.banks[1].state == KUI_TOY_PILOT_BANK_EMPTY);
    REJECT_UNCHANGED(m, kui_toy_pilot_model_start_queued(&m, 0, 1));
    REJECT_UNCHANGED(m, kui_toy_pilot_model_fill_commit(&m, 1, 1, 50));
    REJECT_UNCHANGED(m, kui_toy_pilot_model_fill_commit(&m, 1, 2, 50));
    CHECK(kui_toy_pilot_model_stop_applied(&m, 2, true, true, true));
    ready(&m, 0, 500, 100);
}

static void missed_activity_capability_needs_explicit_end_evidence(void) {
    struct kui_toy_pilot_model m;
    initialize(&m);
    ready(&m, 0, 0, 100);
    queued(&m, 0);
    REJECT_UNCHANGED(m, kui_toy_pilot_model_finite_end(&m, 0, 1, true, true, false));
    /* The generic model accepts a stronger external completion witness even
     * when a long service gap missed activity. The first hardware pilot must
     * not supply this witness: it requires seeing both ports active. */
    CHECK(kui_toy_pilot_model_finite_end(&m, 0, 1, true, true, true));
    CHECK(m.pending_bank == UINT32_MAX && m.active_bank == UINT32_MAX);
    ready(&m, 0, 100, 100);
}

static void generation_exhaustion_never_wraps_or_releases(void) {
    struct kui_toy_pilot_model m;
    initialize(&m);
    m.generation = UINT32_MAX - 1;
    ready(&m, 0, 0, 16384);
    playing(&m, 0);
    CHECK(kui_toy_pilot_model_reset(&m));
    CHECK(m.generation == UINT32_MAX && m.banks[0].generation == UINT32_MAX - 1);
    REJECT_UNCHANGED(m, kui_toy_pilot_model_reset(&m));
    REJECT_UNCHANGED(m, kui_toy_pilot_model_stop_applied(&m, 0, true, true, true));
    CHECK(kui_toy_pilot_model_stop_applied(&m, UINT32_MAX, true, true, true));
    REJECT_UNCHANGED(m, kui_toy_pilot_model_reset(&m));
    initialize(&m);
    m.generation = 0;
    REJECT_UNCHANGED(m, kui_toy_pilot_model_reset(&m));
}

static void absent_model_is_refused(void) {
    kui_toy_pilot_model_init(NULL);
    CHECK(!kui_toy_pilot_model_reset(NULL));
    CHECK(!kui_toy_pilot_model_fill_begin(NULL, 0, 1, 0, 1));
    CHECK(!kui_toy_pilot_model_fill_commit(NULL, 0, 1, 1));
    CHECK(!kui_toy_pilot_model_start_queued(NULL, 0, 1));
    CHECK(!kui_toy_pilot_model_start_applied(NULL, 0, 1, true, true));
    CHECK(!kui_toy_pilot_model_finite_end(NULL, 0, 1, true, true, true));
    CHECK(!kui_toy_pilot_model_stop_applied(NULL, 1, true, true, true));
}

int main(void) {
    partial_fill_cannot_start();
    range_failures_do_not_claim_memory();
    stereo_application_requires_both_ports();
    retirement_alone_does_not_release_a_bank();
    alternate_bank_does_not_restart_while_pair_is_owned();
    cancel_pending_start_retains_ownership();
    repeated_supersession_cannot_reclaim_active_memory();
    cancel_stop_requires_current_complete_evidence();
    prepared_work_discarded_on_generation_change();
    missed_activity_capability_needs_explicit_end_evidence();
    generation_exhaustion_never_wraps_or_releases();
    absent_model_is_refused();
    printf("Toy pilot ownership: %u checks passed\n", checks);
    return 0;
}
