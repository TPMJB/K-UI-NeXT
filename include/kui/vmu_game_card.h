/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VMU_GAME_CARD_H
#define KUI_VMU_GAME_CARD_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* One standard 200-block virtual card per game. Each card is an ordinary
 * /KUI/vmu-test/pNNNN pair (card.vmd, card.journal, identity.bin) plus a
 * game.bin binding naming its game; the stage admits it exactly like an
 * installed pair. Only the launcher chooses the pair and fills the module's
 * P2 profile slot before handoff. Nothing here touches files or devices. */
#define KUI_VMU_GAME_CARD_KEY_BYTES 12u
#define KUI_VMU_GAME_CARD_BINDING_BYTES 64u
#define KUI_VMU_GAME_CARD_BINDING_NAME "game.bin"

/* The binding key is the IP product number, trimmed and upper-cased: 1..10
 * letters, digits or '-' with at least one letter or nonzero digit. Discs of
 * one release share it; other regions keep separate cards. Any other product
 * (blank, inner spaces, punctuation) binds no card: false, out cleared. */
bool kui_vmu_game_card_key(const char *product,char out[KUI_VMU_GAME_CARD_KEY_BYTES]);
/* Binding wire, little endian: 0 "KUIVMUG1", 8 version 1, 12 bytes 64,
 * 16 key NUL padded to 12 bytes, 28..59 zero, 60 CRC32 of bytes 0..59.
 * Decode refuses any other length, field or a key that does not round-trip. */
bool kui_vmu_game_card_binding_encode(const char key[KUI_VMU_GAME_CARD_KEY_BYTES],
    uint8_t out[KUI_VMU_GAME_CARD_BINDING_BYTES]);
bool kui_vmu_game_card_binding_decode(const void *wire,size_t bytes,
    char key[KUI_VMU_GAME_CARD_KEY_BYTES]);
/* Offset of the one 4-byte-aligned 4096-byte P2 profile that decodes inside
 * a module payload: its build template or an installed card. SIZE_MAX when
 * there is none or more than one. */
size_t kui_vmu_game_card_profile_slot(const void *payload,size_t bytes);
/* True when a decoded profile is the public build template, never a card. */
bool kui_vmu_game_card_template(const uint8_t cid[16]);
/* Nonzero pair identity: the first 16 bytes of SHA-256 over a label, the
 * key, the pair number and a caller salt (clock and counter), so a
 * recreated pair never reuses an earlier identity. */
void kui_vmu_game_card_identity(const char key[KUI_VMU_GAME_CARD_KEY_BYTES],unsigned pair,
    const uint8_t salt[16],uint8_t out[16]);
/* Canonical pair number of "pNNNN" (0001..9999), else 0. */
unsigned kui_vmu_game_card_pair_number(const char *name);
#endif
