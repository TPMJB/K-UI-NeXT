/* SPDX-License-Identifier: GPL-3.0-only */
/* Collection-native terminal retirement has the same dead-RAM interval as
 * the existing CE return. Reuse its integer leaf without changing any CE
 * instruction or geometry. Its native caller separately proves retirement,
 * quiescence, cache-off and a wholly low code/data/stack/reader closure. */
#define __kui_smash_ce_return_ram_clear __kui_smash_native_return_ram_clear
#define _kui_smash_ce_return_ram_clear _kui_smash_native_return_ram_clear
#include "smash_ce_return_ram_clear.S"
