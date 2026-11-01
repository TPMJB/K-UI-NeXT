/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_DISC_VMU_CARD_H
#define KUI_DISC_VMU_CARD_H
#include "kui/vmu_game_card_service.h"
#include "kui/vmu_game_info.h"

/* Launcher scratch, not resident state. READY supplies the same canonical P2
 * profile used by the SD-image launcher for this disc's normalized product.
 * The physical-disc stage independently admits live ownership before writes.
 * enabled is true only after complete preparation. No embedded/default profile
 * is substituted when the product, stored choice or selected identity fails. */
struct kui_disc_vmu_card {
    bool enabled;
    char product[KUI_VMU_GAME_CARD_KEY_BYTES];
    struct kui_vmu_game_card_choice choice;
    uint8_t profile_wire[KUI_VMU_PERSIST_PROFILE_WIRE_BYTES];
};
enum kui_disc_vmu_card_result {
    KUI_DISC_VMU_CARD_READY=0,
    KUI_DISC_VMU_CARD_PHYSICAL_ONLY=1,
    KUI_DISC_VMU_CARD_CHOICE_REQUIRED=2,
    KUI_DISC_VMU_CARD_FAILED=-1,
    KUI_DISC_VMU_CARD_CANCELLED=-2
};
/* Caller exclusively owns mounted FatFs 0:, with no optical read in flight.
 * This first integration admits SCI storage only, matching the established P2
 * profile codec. Parent reads the global preference once and supplies enabled.
 * Off clears output and returns PHYSICAL_ONLY before inspecting product, fs,
 * selection, storage identity, catalogue, assignment or any card file.
 *
 * On selects the exact GUI choice when supplied, otherwise the existing saved
 * default/single card, or creates one blank 200-block card for this product.
 * Multiple versions without a default return CHOICE_REQUIRED. It never chooses
 * an arbitrary version, changes a saved default or falls back to an old profile.
 * The real disc has no SD-image extents; live filesystem ownership still runs
 * inside the existing selector, with no forbidden game-file extents.
 *
 * All non-READY returns leave profile_wire zero and enabled false. The mounted
 * session remains caller-owned, including after failure; caller unmounts and
 * disconnects it before game handoff. */
int kui_disc_vmu_card_prepare(const char *product,bool enabled,FATFS *mounted_fs,
    const struct kui_vmu_game_selection *selection,struct kui_disc_vmu_card *out,
    kui_log_fn log,kui_cancel_fn cancel);
#endif
