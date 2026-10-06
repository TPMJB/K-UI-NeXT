/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_CAPTURE_EXPORT_H
#define KUI_CAPTURE_EXPORT_H
#include "kui/capture.h"
/* Post-capture derivatives run only after the authoritative raw tracks pass
 * complete readback. Memory and callbacks belong to the ordinary I/O worker,
 * never to the resident game reader. A cancelled export restarts on Resume. */
enum kui_capture_export_result { KUI_EXPORT_FAILED, KUI_EXPORT_STOPPED, KUI_EXPORT_COMPLETE };
struct kui_capture_export_io {
    void *ctx;
    bool (*track_read)(void *,unsigned,uint64_t,void *,size_t);
    bool (*read)(void *,uint64_t,void *,size_t);
    bool (*write)(void *,uint64_t,const void *,size_t);
    bool (*sync)(void *);
    bool (*cancelled)(void *);
    void (*progress)(void *,uint64_t,uint64_t,bool);
};
struct kui_capture_export_report {
    uint64_t bytes,logical_bytes;
    uint32_t crc32;
    uint8_t sha256[32];
    unsigned data_track;
};
/* CSO/ZSO require exactly one high-density data track; low-density tracks and
 * CDDA remain in the raw capture. CHD preserves the captured mainchannel and
 * audio, with declared synthesized gap padding and no captured subchannels. */
bool kui_capture_export_preflight(const struct kui_capture_plan *,const struct kui_checkpoint *,
    enum kui_capture_format,char *,size_t);
/* Proven uncompressed-fallback upper bound, including index/map/metadata.
 * Caller reserves this IN ADDITION to authoritative raw capture bytes. */
bool kui_capture_export_bound(const struct kui_capture_plan *,enum kui_capture_format,uint64_t *);
enum kui_capture_export_result kui_capture_export_write(const struct kui_capture_plan *,
    const struct kui_checkpoint *,enum kui_capture_format,const struct kui_capture_export_io *,
    struct kui_capture_export_report *);
/* FatFs adapter: caller owns the mounted volume. primary is one safe filename.
 * Writes a temporary derivative, verifies decoded bytes, syncs and atomically
 * renames; original track files and raw checkpoints are never changed.
 * create=false is strict read-only Verify of an existing derivative. */
enum kui_capture_export_result kui_capture_export_file(const struct kui_capture_plan *,
    const struct kui_checkpoint *,const struct kui_capture_ops *,const char *dir,
    const char *primary,bool create,struct kui_capture_export_report *);
#endif
