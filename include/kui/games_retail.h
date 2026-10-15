/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_GAMES_RETAIL_H
#define KUI_GAMES_RETAIL_H
#include "kui/runtime.h"
#include "kui/game_ram_patches.h"
#define KUI_GAMES_RETAIL_PACKAGE "0:/KUI/apps/games/retail-boot.kui"
#define KUI_GAMES_RETAIL_CE_PACKAGE "0:/KUI/apps/games/ce-probe.kui"
#define KUI_GAMES_RETAIL_CE_EXCERPT_PACKAGE "0:/KUI/apps/games/ce-cdda-armada.kui"
#define KUI_GAMES_RETAIL_CE_BAM4_PACKAGE "0:/KUI/apps/games/ce-cdda-bam4.kui"
#define KUI_GAMES_RETAIL_CE_WORMS_PACKAGE "0:/KUI/apps/games/ce-cdda-worms.kui"
#define KUI_GAMES_RETAIL_CDDA_RAW_PACKAGE "0:/KUI/apps/games/cdda-toy-raw.kui"
#define KUI_GAMES_RETAIL_CDDA_COOKED_PACKAGE "0:/KUI/apps/games/cdda-toy-cooked.kui"
#define KUI_GAMES_RETAIL_CDDA_MDK2_PACKAGE "0:/KUI/apps/games/retail-cdda-mdk2.kui"
/* Not a manifest reader: prepare_reader given this prepares the Windows CE
 * boot test instead of a launch. It needs a Windows CE image and the CE probe
 * package, whose stage loads, checks and starts the CE kernel with a tracing
 * reader that stops at the first disc request it cannot serve. Combined with
 * a background reader (KUI_GAMES_RETAIL_CE_PROBE | KUI_RETAIL_READER_ASYNC),
 * the test uses the CE package's background reader, which CE's own interrupt
 * dispatch reaches. */
#define KUI_GAMES_RETAIL_CE_PROBE 0x100u
#define KUI_GAMES_RETAIL_DESCRAMBLE 0x200u
#define KUI_GAMES_RETAIL_BOOT_PLAIN 0x400u
/* Opt-in routing to an exact admitted title/format package. This is an
 * application option, never a manifest reader bit. Unsupported profiles fail
 * before handoff; ordinary launches retain KUI_GAMES_RETAIL_PACKAGE. */
#define KUI_GAMES_RETAIL_CDDA 0x800u
/* Exact admitted original GDI: separately built CE music backend per title.
 * This routes to its separate package; it is never a manifest flag. */
#define KUI_GAMES_RETAIL_CE_EXCERPT 0x1000u
/* Single storage worker only. Prepare one native GD-ROM launch;
 * complete IP/boot CRCs and bounded physical extent map are read-only. Success
 * owns a patched stage package with all files closed and SD disconnected.
 * Cancellation/failure releases the package and leaves image empty. */
bool kui_games_retail_prepare(const char *path,struct kui_runtime_image *image,
    kui_log_fn log,kui_cancel_fn cancelled);
/* As above, asking for a reader (enum kui_retail_reader). The background
 * reader needs SCI microSD and a map in at most KUI_RETAIL_ASYNC_SLOTS slots;
 * otherwise the launch uses the standard reader and says why in the log. */
bool kui_games_retail_prepare_reader(const char *path,uint32_t reader,
    struct kui_runtime_image *image,kui_log_fn log,kui_cancel_fn cancelled);
/* Shell launch: additionally validates the two optional one-time native boot
 * patches and saves a bounded pre-handoff report. Reports contain metadata,
 * not game bytes. An unavailable report is logged and does not turn a valid
 * read-only-card launch into a failure. Patches require an ordinary native
 * package with its exact high-stage capability; CE/CDDA are refused. */
bool kui_games_retail_prepare_launch(const char *path,uint32_t reader,
    const struct kui_game_ram_patches *patches,struct kui_runtime_image *image,
    kui_log_fn log,kui_cancel_fn cancelled);
#endif
