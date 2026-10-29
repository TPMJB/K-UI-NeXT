/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VMU_GAME_ASSIGN_H
#define KUI_VMU_GAME_ASSIGN_H
#include "kui/vmu_game_card.h"

/* Launcher-only explicit assignments. A product selects an exact card identity,
 * so deleting and recreating a pNNNN folder cannot adopt an old assignment.
 * The codec owns no files, devices, heap storage or in-game state. */
#define KUI_VMU_GAME_ASSIGN_MAX 256u
#define KUI_VMU_GAME_ASSIGN_HEADER_BYTES 512u
#define KUI_VMU_GAME_ASSIGN_ENTRY_BYTES 32u
#define KUI_VMU_GAME_ASSIGN_WIRE_BYTES 8704u
struct kui_vmu_game_assignment {
    char key[KUI_VMU_GAME_CARD_KEY_BYTES];
    uint32_t pair;
    uint8_t identity[16];
};
struct kui_vmu_game_assign_table {
    uint64_t sequence;
    uint32_t count;
    struct kui_vmu_game_assignment entries[KUI_VMU_GAME_ASSIGN_MAX];
};
/* Fixed little-endian wire: magic KUIVMUA1, version, wire bytes, nonzero
 * monotonic sequence, count, entry bytes, header bytes, maximum entries;
 * reserved header bytes 40..507 are zero. CRC32 at 508 covers the header
 * before the CRC followed by all 8192 record bytes. Unused records are zero.
 * Keys must be canonical and unique; pair is 1..9999 and identity nonzero.
 * Buffers must not overlap (rejected without touching either buffer). Other
 * failures clear the output. */
bool kui_vmu_game_assign_encode(const struct kui_vmu_game_assign_table *table,
    uint8_t out[KUI_VMU_GAME_ASSIGN_WIRE_BYTES]);
bool kui_vmu_game_assign_decode(const void *wire,size_t bytes,
    struct kui_vmu_game_assign_table *table);
/* Full batch is validated before mutation. Existing keys are replaced,
 * other keys appended. Sequence is unchanged; the publisher increments it
 * only after update succeeds, refusing UINT64_MAX rather than wrapping.
 * An empty table with sequence zero is a valid initial in-memory table.
 * Entries must not overlap the table. Failure leaves table byte-identical. */
bool kui_vmu_game_assign_update(struct kui_vmu_game_assign_table *table,
    const struct kui_vmu_game_assignment *entries,size_t count);
const struct kui_vmu_game_assignment *kui_vmu_game_assign_find(
    const struct kui_vmu_game_assign_table *table,const char key[KUI_VMU_GAME_CARD_KEY_BYTES]);
#endif
