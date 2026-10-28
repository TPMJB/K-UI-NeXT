/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_RAW_CUE_H
#define KUI_RAW_CUE_H
#include "kui/recovery_scan.h"
#define KUI_RAW_CUE_LIMIT 32768u
/* Read-only admission of a deliberately narrow, lossless raw BIN/CUE subset:
 * single-session prepared CD, one safe ASCII-named BINARY file for each consecutive track,
 * AUDIO/MODE1/2352/MODE2/2352, INDEX 01 00:00:00 in each file. An optional
 * PREGAP must exactly equal the uncaptured gap in the prepared ranges. No
 * shared files, offsets, INDEX00, subchannel flags, compressed/cooked tracks,
 * multisession GD cues or inferred timings. Caller checks every file's raw
 * byte size and performs saved/optical content identity before any mutation.
 * Original track filenames are retained; output is unchanged on failure. */
bool kui_raw_cue_parse(const void *data,size_t bytes,
    const struct kui_capture_plan *prepared,struct kui_scan_gdi *out);
#endif
