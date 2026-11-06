/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_RETAIL_DISPLAY_H
#define KUI_RETAIL_DISPLAY_H
#include <stdint.h>
struct retail_display_state { uint32_t regs[14]; };
void retail_display_capture(struct retail_display_state *);
#if KUI_SMASH_CE_GIN_ADOPT || KUI_SMASH_NATIVE_GIN_ADOPT
/* A successful post-GIN adoption must not repaint the owner's video RAM or
 * alter its PVR setup. Failure reporting explicitly unmutes the renderer. */
void retail_display_adoption_quiet(uint32_t);
#endif
void retail_display_restore(const struct retail_display_state *);
void retail_display_line(const char *);
void retail_display_hex(const char *, uint32_t);
void retail_display_values(const char *legend, const uint32_t *values, unsigned count);
void retail_display_progress(uint32_t, uint32_t);
#if KUI_VC2_STAGE_OWNERSHIP_PROFILE
/* Cold-stage page only. Begin/results clear the text body once, preserving
 * build identity and the bottom progress bar. Updates replace aligned rows
 * without touching video registers or pausing storage. */
void retail_display_vc2_ownership_begin(void);
void retail_display_vc2_ownership_status(const uint32_t values[17]);
void retail_display_vc2_ownership_results(uint32_t clock_available);
#endif
#ifdef KUI_RETAIL_CE
void retail_display_status(const char *legend, const uint32_t *values, unsigned count);
#endif
void retail_display_pause(uint32_t frames);
#endif
