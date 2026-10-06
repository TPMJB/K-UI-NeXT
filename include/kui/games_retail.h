/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_GAMES_RETAIL_H
#define KUI_GAMES_RETAIL_H
#include "kui/runtime.h"
#define KUI_GAMES_RETAIL_PACKAGE "0:/KUI/apps/games/retail-boot.kui"
#define KUI_GAMES_RETAIL_CE_PACKAGE "0:/KUI/apps/games/ce-probe.kui"
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
#endif
