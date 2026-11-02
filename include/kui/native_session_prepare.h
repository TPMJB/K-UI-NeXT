/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_NATIVE_SESSION_PREPARE_H
#define KUI_NATIVE_SESSION_PREPARE_H
#include "kui/games.h"
#include "kui/retail_image.h"
#include "kui/runtime.h"

#define KUI_NATIVE_SESSION_DISC_MAX 4u
/* A UI/storage-worker snapshot of real catalogue paths. No catalogue or
 * filesystem pointer survives preparation. Disc order is the catalogue's
 * validated numeric order; selected identifies the game being launched. */
struct kui_native_session_request {
    unsigned count,selected;
    char paths[KUI_NATIVE_SESSION_DISC_MAX][KUI_GAMES_FILE_CAP];
};
struct kui_volume;
/* Read-only mounted-volume helper shared with ordinary launch mapping. Does
 * not initialize, mount, unmount or disconnect storage. Accepts only native
 * GDI/SCI/New-reader images without high-density CDDA, with <=64 map slots. */
bool kui_native_session_map_gdi(FATFS *fs,const struct kui_volume *volume,
    const char *path,uint32_t reader,struct kui_retail_manifest *out,
    char version[7],const char **problem,kui_log_fn log,kui_cancel_fn cancel);
/* Exact file allocation mapping, including the final partially used sector.
 * Physical extents are absolute card LBAs and remain inside this partition.
 * Caller owns the mounted filesystem throughout. Output is cleared on error. */
bool kui_native_session_map_file(FATFS *fs,const struct kui_volume *volume,
    const char *path,struct kui_retail_extent *extents,unsigned capacity,
    unsigned *count,uint64_t *file_bytes,const char **problem,
    kui_cancel_fn cancel);
/* Patch only a uniquely recognized opt-in session module. An ordinary
 * module is a successful no-op; malformed/ambiguous session data refuses. */
bool kui_native_session_prepare(FATFS *fs,const struct kui_volume *volume,
    const char *selected,const struct kui_native_session_request *request,
    const struct kui_retail_manifest *active,const char *version,struct kui_runtime_image *package,
    const char **problem,kui_log_fn log,kui_cancel_fn cancel);
#endif
