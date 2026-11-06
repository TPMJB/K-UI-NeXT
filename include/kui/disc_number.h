/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_DISC_NUMBER_H
#define KUI_DISC_NUMBER_H
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* Dreamcast IP device info is "xxxx GD-ROMn/t  " (or CD-ROM), with
 * single decimal ordinal/total bytes at 0x2b/0x2d. The format is documented by
 * KallistiOS makeip's Device Information field. This is presentation metadata,
 * never a replacement for raw content/checkpoint identity. Unknown or malformed
 * fields produce 0/0; neither file collision suffixes nor rip order are ordinals. */
static inline bool kui_disc_number_read(const uint8_t ip[256],unsigned *disc,unsigned *total) {
    if(!disc || !total || disc==total)return false;
    *disc=*total=0u;
    if(!ip || ip[36]!=' ' || (memcmp(ip+37,"GD-ROM",6u) && memcmp(ip+37,"CD-ROM",6u)) ||
       ip[43]<'1' || ip[43]>'9' || ip[44]!='/' || ip[45]<'1' || ip[45]>'9' ||
       ip[43]>ip[45] || ip[46]!=' ' || ip[47]!=' ')return false;
    *disc=(unsigned)(ip[43]-'0');*total=(unsigned)(ip[45]-'0');return true;
}
#endif
