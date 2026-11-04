/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_TOY_REPORT_EXPORT_H
#define KUI_TOY_REPORT_EXPORT_H
#include "kui/toy_decision_observer.h"
#include "kui/report_file.h"
#ifndef KUI_TOY_TEXT_REPORT
#define KUI_TOY_TEXT_REPORT 0
#endif
#if KUI_TOY_TEXT_REPORT != 0 && KUI_TOY_TEXT_REPORT != 1
#error Toy text report selector must be zero or one
#endif
#if KUI_TOY_TEXT_REPORT && !KUI_TOY_DECISION_PROFILE
#error Toy text export belongs only to decision profiles one and two
#endif
#if KUI_TOY_TEXT_REPORT
#define KUI_TOY_REPORT_BIND_MAGIC0 UINT32_C(0x54585954)
#define KUI_TOY_REPORT_BIND_MAGIC1 UINT32_C(0x31444e42)
struct kui_toy_report_binding {
    uint32_t magic0,magic1,version,profile;
    struct kui_report_file_descriptor file;
};
_Static_assert(sizeof(struct kui_toy_report_binding)==272u,"Toy report binding ABI");
/* Scan only the embedded initialized worker image. The stage patches the
 * unique aligned zero descriptor after copy verification, before initialize.
 * It never extends or repurposes the existing worker export table. */
bool kui_toy_report_binding_offset(const uint8_t *,size_t,unsigned,size_t *);
int kui_toy_report_export_save(const struct kui_toy_decision_report *,
    const uint32_t stopped_words[16],bool quiesced);
#endif
#endif
