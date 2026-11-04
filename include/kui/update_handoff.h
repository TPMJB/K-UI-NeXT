/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_UPDATE_HANDOFF_H
#define KUI_UPDATE_HANDOFF_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define KUI_UPDATE_HANDOFF_BYTES 192u
/* Emit exactly one initialized slot in the GUI payload. Validators deliberately
 * do not instantiate this initializer: a second copy would fail admission. */
#define KUI_UPDATE_HANDOFF_INITIALIZER {'K','U','I','H','O','F','1',0,1,0,0,0,192,0,0,0}
/* Wire: magic8, LE32 version@8=1,size@12=192,state@16=1,transport@20,
 * inverse@24,zero28..31,build12@32,zero44..47,seq64@48,file_bytes64@56,
 * manifestSHA@64,runtimeSHA@96,slot_offset32@128,nonce16@132,inverse16@148,
 * original payloadCRC32@164,memory_bytes32@168,zero172..187,
 * CRC32@188 over bytes0..187. EMPTY has only magic/version/size;
 * all bytes16..191 are zero. The nonce and CRC establish transfer coherence,
 * not authenticity. Packages must come from a trusted source. */
struct kui_update_handoff {
    char build[13];
    uint32_t transport,payload_offset,payload_crc32,memory_bytes;
    uint64_t sequence,file_bytes;
    uint8_t manifest_sha256[32],runtime_sha256[32],nonce[16];
};
/* Context is copied from the running initialized RAM slot before storage I/O.
 * payload_offset is its actual address minus KUI_RUNTIME_ADDRESS, not a value
 * accepted from the wire. A missing/invalid context requires full validation. */
struct kui_update_handoff_context {
    uint8_t wire[KUI_UPDATE_HANDOFF_BYTES];
    uint32_t payload_offset;
};
bool kui_update_handoff_encode(uint8_t wire[KUI_UPDATE_HANDOFF_BYTES],const struct kui_update_handoff *);
bool kui_update_handoff_decode(const uint8_t wire[KUI_UPDATE_HANDOFF_BYTES],struct kui_update_handoff *);
bool kui_update_handoff_empty(const void *payload,size_t bytes,uint32_t *offset);
struct kui_update_handoff_scan {
    uint8_t tail[KUI_UPDATE_HANDOFF_BYTES-1u];
    size_t bytes;
    unsigned count,empty;
};
void kui_update_handoff_scan_update(struct kui_update_handoff_scan *,const void *,size_t);
bool kui_update_handoff_scan_valid(const struct kui_update_handoff_scan *);
#endif
