/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VC2_CE_VMU_WATCH_H
#define KUI_VC2_CE_VMU_WATCH_H
#include <stddef.h>
#include <stdint.h>
/* Terminal overlay of the retired 828-byte manifest. The exception entry
 * publishes its canonical P1 capture before C reads it through P2. */
struct kui_vc2_vmu_watch_report {
    union {
        struct {
            uint32_t active[16],other[8];
            uint32_t spc,ssr,pr,gbr,mach,macl,expevt,brcr,ccr,mmu,dbr,original_dbr;
        };
        uint32_t words[36];
    };
};
_Static_assert(offsetof(struct kui_vc2_vmu_watch_report,spc)==96u,"UBC SPC ABI");
_Static_assert(offsetof(struct kui_vc2_vmu_watch_report,original_dbr)==140u,"UBC DBR ABI");
_Static_assert(sizeof(struct kui_vc2_vmu_watch_report)<=828u,"UBC terminal overlay");
void kui_vc2_ce_vmu_watch_stop(void) __attribute__((noreturn));
#endif
