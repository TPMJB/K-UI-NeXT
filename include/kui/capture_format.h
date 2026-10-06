/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_CAPTURE_FORMAT_H
#define KUI_CAPTURE_FORMAT_H
enum kui_capture_format {
    KUI_CAPTURE_FORMAT_GDI, KUI_CAPTURE_FORMAT_BIN_CUE, KUI_CAPTURE_FORMAT_CSO,
    KUI_CAPTURE_FORMAT_ZSO, KUI_CAPTURE_FORMAT_CHD, KUI_CAPTURE_FORMAT_COUNT
};
static inline const char *kui_capture_format_name(enum kui_capture_format format) {
    switch(format) {
    case KUI_CAPTURE_FORMAT_GDI:return "GDI";
    case KUI_CAPTURE_FORMAT_BIN_CUE:return "BIN/CUE";
    case KUI_CAPTURE_FORMAT_CSO:return "CSO";
    case KUI_CAPTURE_FORMAT_ZSO:return "ZSO";
    case KUI_CAPTURE_FORMAT_CHD:return "CHD";
    default:return "Unknown";
    }
}
static inline const char *kui_capture_format_extension(enum kui_capture_format format) {
    switch(format) {
    case KUI_CAPTURE_FORMAT_GDI:return ".gdi";
    case KUI_CAPTURE_FORMAT_BIN_CUE:return ".cue";
    case KUI_CAPTURE_FORMAT_CSO:return ".cso";
    case KUI_CAPTURE_FORMAT_ZSO:return ".zso";
    case KUI_CAPTURE_FORMAT_CHD:return ".chd";
    default:return "";
    }
}
#endif
