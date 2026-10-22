/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VMU_DESCRIPTION_H
#define KUI_VMU_DESCRIPTION_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct kui_vmu_description {
    char description[33],app_id[17];
};
/* Display-only interpretation of the 128-byte embedded VMS header. package_bytes
 * is the file capacity remaining from its directory header-block offset. This
 * checks the documented package dimensions, not CRC or payload contents, and
 * never changes save bytes or authorizes importing/restoring a package. Fixed
 * text fields are bounded, trimmed, and reduced to printable ASCII; unsupported
 * text (including Shift-JIS) uses '?' rather than emitting font control bytes.
 * On an implausible/truncated header, out is empty and false is returned. */
bool kui_vmu_description_parse(const uint8_t *header,size_t header_bytes,
    size_t package_bytes,struct kui_vmu_description *out);
#endif
